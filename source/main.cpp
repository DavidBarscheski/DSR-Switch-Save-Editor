// Dark Souls Remastered save editor for Nintendo Switch homebrew (libnx + SDL2).
//
// Flow: warning -> pick user -> mount the game's save -> verify -> edit -> back up -> write -> commit.
// All screens are drawn by ui.cpp through SdlGfx; this file holds the console-specific parts.
#include <switch.h>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "catalog.h"
#include "gfx_sdl.h"
#include "input.h"
#include "persist.h"
#include "save.h"
#include "ui.h"

namespace {

constexpr u64 kDsrTitleId = 0x01004AB00A260000ULL;
constexpr const char* kMount = "save";
constexpr const char* kAppDir = "sdmc:/switch/DSRSaveEditor";
constexpr const char* kBackupDir = "sdmc:/switch/DSRSaveEditor/backups";
constexpr const char* kIconDir = "sdmc:/switch/DSRSaveEditor/icons";

PadState g_pad;
dsr::SdlGfx g_gfx;

// ---------------------------------------------------------------- input / screen

u64 pollDown() {
    padUpdate(&g_pad);
    return padGetButtonsDown(&g_pad);
}

// Draws `draw()` every frame until one of `mask` is pressed. Returns false if the app is asked to close.
template <class Draw>
bool waitFor(u64 mask, Draw draw, u64* pressed = nullptr) {
    while (appletMainLoop()) {
        u64 d = pollDown();
        if (d & mask) {
            if (pressed) *pressed = d & mask;
            return true;
        }
        draw();
        g_gfx.present();
    }
    return false;
}

void fatalScreen(const std::string& title, const std::string& detail) {
    waitFor(HidNpadButton_Plus, [&] { dsr::drawNotice(g_gfx, title, detail, "Exit", true); });
}

// Opens the system keyboard. Returns false if it could not open or the user cancelled.
bool askText(const char* header, const std::string& initial, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, 32);
    char buf[128] = {};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = buf;
    return true;
}

// ---------------------------------------------------------------- files

constexpr long kMaxFileSize = 64L * 1024 * 1024; // a real save is about 4 MB

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    long sz = std::ftell(f);
    if (sz <= 0 || sz > kMaxFileSize || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)sz);
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Platform file system behind the save transaction (see include/persist.h).
class SwitchFs : public dsr::IFileSystem {
public:
    bool readFile(const std::string& path, std::vector<uint8_t>& out) override { return ::readFile(path, out); }

    dsr::CreateResult createNew(const std::string& path, const std::vector<uint8_t>& data) override {
        if (FILE* t = std::fopen(path.c_str(), "rb")) { std::fclose(t); return dsr::CreateResult::Exists; }
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return dsr::CreateResult::Error;
        size_t put = std::fwrite(data.data(), 1, data.size(), f);
        bool ok = (put == data.size()) && (std::fflush(f) == 0);
        ok = (std::fclose(f) == 0) && ok; // close errors count too
        return ok ? dsr::CreateResult::Ok : dsr::CreateResult::Error;
    }

    // Overwrites in place without truncating, so the file never exists in a shorter, half-written state.
    dsr::WriteResult overwriteInPlace(const std::string& path, const std::vector<uint8_t>& data) override {
        FILE* f = std::fopen(path.c_str(), "r+b");
        if (!f) return dsr::WriteResult::OpenFailed;
        size_t put = std::fwrite(data.data(), 1, data.size(), f);
        bool ok = (put == data.size()) && (std::fflush(f) == 0);
        ok = (std::fclose(f) == 0) && ok;
        return ok ? dsr::WriteResult::Ok : dsr::WriteResult::Error;
    }

    bool commit() override { return R_SUCCEEDED(fsdevCommitDevice(kMount)); }
    bool removeFile(const std::string& path) override { return std::remove(path.c_str()) == 0; }
};

struct Entry {
    std::string path; // relative to save:/
    long size;
    bool isDir;
};

// Lists everything on the mounted save, a few levels deep.
void scanDir(const std::string& rel, int depth, std::vector<Entry>& out) {
    std::vector<std::string> subdirs;
    DIR* d = opendir(("save:/" + rel).c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string r = rel.empty() ? n : rel + "/" + n;
        bool isDir = (e->d_type == DT_DIR);
        long size = 0;
        if (!isDir) {
            struct stat st;
            if (stat(("save:/" + r).c_str(), &st) == 0) size = (long)st.st_size;
        }
        out.push_back({r, size, isDir});
        if (isDir) subdirs.push_back(r);
    }
    closedir(d);
    if (depth < 3)
        for (const std::string& s : subdirs) scanDir(s, depth + 1, out);
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Finds the DRAKS*.sl2 save file anywhere on the mounted save (case-insensitive).
// Returns its path relative to save:/ or "" if none; `all` receives the full listing.
std::string findSaveFile(std::vector<Entry>& all) {
    all.clear();
    scanDir("", 0, all);
    std::vector<std::string> hits;
    for (const Entry& e : all) {
        if (e.isDir) continue;
        size_t slash = e.path.find_last_of('/');
        std::string base = lower(slash == std::string::npos ? e.path : e.path.substr(slash + 1));
        if (base.size() > 9 && base.compare(0, 5, "draks") == 0 && base.compare(base.size() - 4, 4, ".sl2") == 0)
            hits.push_back(e.path);
    }
    if (hits.empty()) return "";
    std::sort(hits.begin(), hits.end());
    return hits.front();
}

std::string describeListing(const std::vector<Entry>& all) {
    if (all.empty()) return "(the save is empty)";
    std::string s;
    size_t shown = 0;
    for (const Entry& e : all) {
        if (shown >= 24) break;
        char line[200];
        if (e.isDir)
            std::snprintf(line, sizeof(line), "  %s/\n", e.path.c_str());
        else
            std::snprintf(line, sizeof(line), "  %s  (%ld bytes)\n", e.path.c_str(), e.size);
        s += line;
        shown++;
    }
    if (all.size() > shown) s += "  ... and " + std::to_string(all.size() - shown) + " more\n";
    return s;
}

std::string backupStamp() {
    char stamp[32];
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    if (lt)
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", lt);
    else
        std::snprintf(stamp, sizeof(stamp), "t%lld", (long long)t);
    return stamp;
}

// ---------------------------------------------------------------- accounts

std::string userName(AccountUid uid) {
    std::string name = "(unknown)";
    AccountProfile prof;
    if (R_SUCCEEDED(accountGetProfile(&prof, uid))) {
        AccountProfileBase base;
        std::memset(&base, 0, sizeof(base));
        if (R_SUCCEEDED(accountProfileGet(&prof, nullptr, &base))) name = base.nickname;
        accountProfileClose(&prof);
    }
    return name;
}


// Returns false if the user quit.
bool pickUser(AccountUid& chosen, std::string& name) {
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 total = 0;
    Result rc = accountListAllUsers(uids, ACC_USER_LIST_SIZE, &total);
    if (R_FAILED(rc) || total <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Could not list user profiles (rc=0x%X).", rc);
        fatalScreen("No user profiles", msg);
        return false;
    }

    std::vector<std::string> names;
    for (s32 i = 0; i < total; i++) names.push_back(userName(uids[i]));

    if (total == 1) {
        chosen = uids[0];
        name = names[0];
        return true;
    }

    std::vector<dsr::ChoiceRow> rows;
    for (const std::string& n : names) rows.push_back({n, ""});
    int sel = 0;
    while (appletMainLoop()) {
        u64 d = pollDown();
        if (d & HidNpadButton_Plus) return false;
        if (d & HidNpadButton_Up) sel = (sel + total - 1) % total;
        if (d & HidNpadButton_Down) sel = (sel + 1) % total;
        if (d & HidNpadButton_A) {
            chosen = uids[sel];
            name = names[(size_t)sel];
            return true;
        }
        dsr::drawChoice(g_gfx, "Which user?", "Select the user whose Dark Souls save to edit.", rows, sel, "Select");
        g_gfx.present();
    }
    return false;
}

// ---------------------------------------------------------------- editor

// Result of one save attempt as the editor loop needs it.
struct SaveAttempt {
    std::string message;
    dsr::Tone tone = dsr::Tone::Info;
    bool reload = false;   // verified: the file on the console is the new baseline
    bool recovery = false; // the file may differ from what was loaded: no further saves until a restart
};

std::string baseName(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Backup, write, commit and verify through dsr::saveWithBackup (the logic is tested on a PC).
SaveAttempt performSave(dsr::Save& save, const std::string& saveName, const std::string& userKey) {
    mkdir(kAppDir, 0777);
    mkdir(kBackupDir, 0777);

    SwitchFs fs;
    const std::vector<uint8_t>& out = save.finalize();
    std::string prefix = userKey + "_" + baseName(saveName);
    dsr::SaveReport r = dsr::saveWithBackup(fs, std::string("save:/") + saveName, kBackupDir, prefix, backupStamp(),
                                            save.originalBytes(), out, (int64_t)save.base());

    const std::string bak = r.backupPath.empty() ? std::string() : " Backup: " + baseName(r.backupPath);
    SaveAttempt a;
    a.tone = dsr::Tone::Bad;
    switch (r.outcome) {
        case dsr::SaveOutcome::Verified:
            a.message = "Saved and verified." + bak;
            a.tone = dsr::Tone::Good;
            a.reload = true;
            break;
        case dsr::SaveOutcome::StaleLive:
            a.message = "NOT saved: the save file changed since it was loaded. Your edits are kept. Restart the editor.";
            break;
        case dsr::SaveOutcome::BackupFailed:
            a.message = "NOT saved: a verified SD card backup could not be made. Your edits are kept.";
            break;
        case dsr::SaveOutcome::OpenFailed:
            a.message = "NOT saved: the save file could not be opened for writing. Your edits are kept." + bak;
            break;
        case dsr::SaveOutcome::WriteFailed:
            a.message = "Write failed part way. The save may be damaged. Restore the backup, then restart." + bak;
            a.recovery = true;
            break;
        case dsr::SaveOutcome::CommitFailed:
            a.message = "Commit failed. The changes may not have been kept. Restart and check the save." + bak;
            a.recovery = true;
            break;
        case dsr::SaveOutcome::VerifyFailed:
            a.message = "The written file did not verify. Restore the backup, then restart." + bak;
            a.recovery = true;
            break;
    }
    return a;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

void editorLoop(dsr::Save& save, const std::string& user, const std::string& file, const std::string& userKey) {
    using namespace dsr;
    bool recovery = false; // set after a save that may have changed the file; blocks further saves
    Screen screen = Screen::Stats;
    int sels[2] = {0, 0};  // selection on the Stats and Items screens
    AddList add(8);
    Mode mode = Mode::Edit;
    Banner msg;
    std::vector<std::string> summary;
    auto say = [&](const std::string& t, Tone tone = Tone::Info) { msg.text = t; msg.tone = tone; };
    if (!save.editable()) say("Editing is disabled for this file. Nothing will be written.", Tone::Bad);
    Hold hUp, hDown, hLeft, hRight, hPageU, hPageD, hCatL, hCatR;
    uint32_t addQty = 1;                 // amount the Add screen will add
    size_t lastPick = (size_t)-1;        // the item addQty belongs to

    auto sel = [&]() -> int& { return sels[screen == Screen::Items ? 1 : 0]; };
    auto listSize = [&]() -> int { return screen == Screen::Stats ? (int)kFieldCount : (int)save.itemCount(); };
    auto clampItemSel = [&]() {
        int n = (int)save.itemCount();
        if (sels[1] >= n) sels[1] = n ? n - 1 : 0;
    };
    auto adjust = [&](int64_t delta) {
        if (screen == Screen::Stats) save.adjust((size_t)sel(), delta);
        else save.adjustItemQty((size_t)sel(), delta);
    };
    auto setMax = [&]() {
        if (screen == Screen::Stats) save.setMax((size_t)sel());
        else save.setItemMax((size_t)sel());
    };
    auto revert = [&]() {
        if (screen == Screen::Stats) save.revert((size_t)sel());
        else { save.revertItem((size_t)sel()); clampItemSel(); }
    };

    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 d = padGetButtonsDown(&g_pad);
        u64 h = padGetButtons(&g_pad);

        // The D-pad and the left stick both move and change values; the right stick pages and changes
        // category. Every input is polled every frame so a released button always resets its repeat.
        const HidAnalogStickState ls = padGetStickPos(&g_pad, 0), rs = padGetStickPos(&g_pad, 1);
        const bool upH = (h & HidNpadButton_Up) || stickHeld(ls.y, 1), downH = (h & HidNpadButton_Down) || stickHeld(ls.y, -1);
        const bool leftH = (h & HidNpadButton_Left) || stickHeld(ls.x, -1), rightH = (h & HidNpadButton_Right) || stickHeld(ls.x, 1);
        const bool up = holdFire(hUp, upH), down = holdFire(hDown, downH);
        const bool stepLeft = holdFire(hLeft, leftH), stepRight = holdFire(hRight, rightH);
        const int mag = stepLeft ? holdMagnitude(hLeft) : holdMagnitude(hRight);
        const bool pageUp = holdFire(hPageU, stickHeld(rs.y, 1), 24, 8, 1000, 8), pageDown = holdFire(hPageD, stickHeld(rs.y, -1), 24, 8, 1000, 8);
        const bool catPrev = holdFire(hCatL, stickHeld(rs.x, -1), 24, 12, 1000, 12), catNext = holdFire(hCatR, stickHeld(rs.x, 1), 24, 12, 1000, 12);

        if (mode == Mode::ConfirmSave) {
            if (d & HidNpadButton_A) {
                if (recovery) {
                    say("Saving is disabled after a failed write. Restore your backup and restart the editor.", Tone::Bad);
                } else {
                    SaveAttempt at = performSave(save, file, userKey);
                    say(at.message, at.tone);
                    if (at.recovery) recovery = true;
                    // Only a verified write replaces the baseline. In every other case the user's
                    // edits stay in memory.
                    if (at.reload) {
                        std::vector<uint8_t> fresh;
                        if (readFile(std::string("save:/") + file, fresh)) save.load(std::move(fresh), save.base());
                        else recovery = true;
                        clampItemSel(); // the item list is rebuilt on load
                    }
                }
                mode = Mode::Edit;
            } else if (d & HidNpadButton_B) {
                mode = Mode::Edit;
                msg = Banner();
            }
        } else if (mode == Mode::ConfirmQuit) {
            if (d & HidNpadButton_A) return;
            if (d & HidNpadButton_B) { mode = Mode::Edit; msg = Banner(); }
        } else {
            if (d & (HidNpadButton_B | HidNpadButton_Plus)) {
                if (save.dirty()) mode = Mode::ConfirmQuit;
                else return;
            }
            // ZL and ZR (and -) switch between the Stats, Items and Add screens.
            int tabStep = (d & HidNpadButton_ZR) || (d & HidNpadButton_Minus) ? 1 : (d & HidNpadButton_ZL) ? -1 : 0;
            if (tabStep) {
                screen = (Screen)((((int)screen + tabStep) % 3 + 3) % 3);
                msg = Banner();
            }

            if (screen == Screen::Add) {
                if (up) add.move(-1);
                if (down) add.move(1);
                if (pageUp) add.page(-1);
                if (pageDown) add.page(1);
                if ((d & HidNpadButton_L) || catPrev) add.stepCategory(-1);
                if ((d & HidNpadButton_R) || catNext) add.stepCategory(1);

                // The amount to add starts at 1 for every item you land on.
                if (add.hasSelection() && add.selected() != lastPick) { addQty = 1; lastPick = add.selected(); }
                if (add.hasSelection() && (stepLeft || stepRight)) {
                    const int64_t cap = kAddableItems[add.selected()].maxNum;
                    int64_t q = (int64_t)addQty + (stepRight ? mag : -mag);
                    addQty = (uint32_t)std::max<int64_t>(1, std::min<int64_t>(q, cap));
                }

                if (d & HidNpadButton_StickL) {
                    std::string q;
                    if (askText("Search items by name (leave empty to clear)", add.query(), q)) {
                        add.setQuery(trim(q));
                        say(add.query().empty() ? "Search cleared." : "Searching for \"" + add.query() + "\".");
                    } else {
                        say("Search cancelled.");
                    }
                }
                if (d & HidNpadButton_StickR) {
                    add.setShowCut(!add.showCut());
                    say(add.showCut() ? "Cut items shown at the end of the list (unobtainable in the real game)." : "Cut items hidden.");
                }
                if (save.editable() && (d & HidNpadButton_X) && add.hasSelection()) {
                    size_t idx = 0;
                    const size_t pick = add.selected();
                    const uint32_t want = std::max<uint32_t>(1, std::min<uint32_t>(addQty, kAddableItems[pick].maxNum));
                    AddResult r = save.addItem(pick, want, &idx);
                    const char* nm = kAddableItems[pick].name;
                    bool goods = kAddableItems[pick].tag == kGoodsTag;
                    char buf[160];
                    Tone tone = Tone::Info;
                    switch (r) {
                        case AddResult::Added:
                            if (goods) std::snprintf(buf, sizeof(buf), "Added %s x%u. Press A to save.", nm, (unsigned)want);
                            else std::snprintf(buf, sizeof(buf), "Added %s. Press A to save.", nm);
                            tone = Tone::Good;
                            break;
                        case AddResult::SetExisting:
                            std::snprintf(buf, sizeof(buf), "You already own %s: set it to x%u.", nm, (unsigned)want);
                            tone = Tone::Good;
                            break;
                        case AddResult::AlreadyOwned:
                            std::snprintf(buf, sizeof(buf), "You already own %s. Nothing changed.", nm);
                            break;
                        case AddResult::NoFreeSlot:
                            std::snprintf(buf, sizeof(buf), "No suitable empty inventory slot was found. Nothing changed.");
                            tone = Tone::Bad;
                            break;
                        default:
                            std::snprintf(buf, sizeof(buf), "Cannot add items to this save.");
                            tone = Tone::Bad;
                            break;
                    }
                    say(buf, tone);
                }
                if (save.editable() && (d & HidNpadButton_Y) && add.hasSelection())
                    say(save.revertAdded(add.selected()) ? "Removed the item you added." : "That item was not added in this session.");
            } else {
                int n = listSize(); // may be 0 on the Items screen
                if (n > 0) {
                    if (up) sel() = (sel() + n - 1) % n;
                    if (down) sel() = (sel() + 1) % n;
                }
                if (save.editable() && n > 0) {
                    // Left/Right: 1 per press; holding speeds up to 10 and then 100 per step.
                    if (stepLeft) { adjust(-mag); msg = Banner(); }
                    if (stepRight) { adjust(mag); msg = Banner(); }
                    if (screen == Screen::Stats) { // big steps only make sense for souls/HP-sized numbers
                        if (d & HidNpadButton_L) { adjust(-1000); msg = Banner(); }
                        if (d & HidNpadButton_R) { adjust(1000);  msg = Banner(); }
                    }
                    if (d & HidNpadButton_X) { setMax(); msg = Banner(); }
                    if (d & HidNpadButton_Y) { revert(); msg = Banner(); }
                }
            }
            if (save.editable() && (d & HidNpadButton_A)) {
                if (save.dirty()) {
                    summary = changeSummary(save, 9);
                    mode = Mode::ConfirmSave;
                } else {
                    say("No changes to save.");
                }
            }
        }

        EditorView v;
        v.save = &save;
        v.add = &add;
        v.user = user;
        v.file = file;
        v.screen = screen;
        v.statSel = sels[0];
        v.itemSel = sels[1];
        v.mode = mode;
        v.msg = msg;
        v.addQty = addQty;
        v.pending = countChanges(save);
        v.recovery = recovery;
        v.summary = summary;
        drawEditor(g_gfx, v);
        g_gfx.present();
    }
}

// Lets the user choose among several characters in one save file. Returns the index into `c`,
// or -1 if the app is closing or the user backed out.
int pickCharacter(const std::vector<dsr::CharacterInfo>& c) {
    int sel = 0;
    std::vector<dsr::ChoiceRow> rows;
    for (size_t i = 0; i < c.size(); i++) {
        if (c[i].isFileDefault) sel = (int)i;
        rows.push_back({"Character " + std::to_string(i + 1),
                        "level " + std::to_string(c[i].level) + "    souls " + dsr::withCommas(c[i].souls) + (c[i].isFileDefault ? "    (the game's slot 1)" : "")});
    }
    const std::string sub = "This save has " + std::to_string(c.size()) + " characters. Choose one to edit.";
    while (appletMainLoop()) {
        u64 d = pollDown();
        if (d & HidNpadButton_Plus) return -1;
        if (d & HidNpadButton_A) return sel;
        if (d & HidNpadButton_Up) sel = (sel + (int)c.size() - 1) % (int)c.size();
        if (d & HidNpadButton_Down) sel = (sel + 1) % (int)c.size();
        dsr::drawChoice(g_gfx, "Which character?", sub, rows, sel, "Edit this one");
        g_gfx.present();
    }
    return -1;
}

bool showWarning() {
    u64 pressed = 0;
    if (!waitFor(HidNpadButton_A | HidNpadButton_Plus, [] { dsr::drawWarning(g_gfx); }, &pressed)) return false;
    return pressed & HidNpadButton_A;
}

} // namespace

int main(int, char**) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    if (!g_gfx.init(kIconDir)) {
        // No graphics: say why on the plain text console, so the problem can be reported.
        std::string why = g_gfx.error();
        g_gfx.shutdown();
        consoleInit(nullptr);
        std::printf("Dark Souls Remastered Save Editor\n\nCould not start graphics:\n%s\n\nPress + to exit.\n", why.c_str());
        consoleUpdate(nullptr);
        while (appletMainLoop()) {
            if (pollDown() & HidNpadButton_Plus) break;
            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
        return 1;
    }

    bool accountsReady = false, mounted = false;

    do {
        if (!showWarning()) break;

        Result rc = accountInitialize(AccountServiceType_Application);
        if (R_FAILED(rc)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "accountInitialize failed (rc=0x%X).", rc);
            fatalScreen("Account service error", msg);
            break;
        }
        accountsReady = true;

        AccountUid uid;
        std::string user;
        if (!pickUser(uid, user)) break;

        rc = fsdevMountSaveData(kMount, kDsrTitleId, uid);
        if (R_FAILED(rc)) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "Could not open Dark Souls Remastered save data for %s (rc=0x%X).\n\n"
                          "Make sure the game is installed, has been started at least once with this\n"
                          "user (so a save exists), and is not running.",
                          user.c_str(), rc);
            fatalScreen("Cannot mount save", msg);
            break;
        }
        mounted = true;

        std::vector<Entry> listing;
        std::string file = findSaveFile(listing);
        if (file.empty()) {
            std::string msg = "No DRAKS*.sl2 file was found on this save.\n"
                              "If you have several user profiles, try the one that plays Dark Souls.\n\n"
                              "What is actually on the save for " + user + ":\n" + describeListing(listing);
            fatalScreen("No save file found", msg.c_str());
            break;
        }

        std::vector<uint8_t> bytes;
        if (!readFile(std::string("save:/") + file, bytes)) {
            fatalScreen("Read error", "Could not read the save file.");
            break;
        }

        dsr::Save save;
        std::vector<dsr::CharacterInfo> chars = dsr::Save::listCharacters(bytes);
        if (chars.size() > 1) {
            int pick = pickCharacter(chars);
            if (pick < 0) break;
            save.load(std::move(bytes), chars[(size_t)pick].base);
        } else if (chars.size() == 1) {
            save.load(std::move(bytes), chars[0].base);
        } else {
            // A matching checksum does not make an unknown layout safe to edit, so a file with no
            // recognised character is not opened for editing at all.
            fatalScreen("Unsupported save", "No character with a recognised inventory layout was found.\nNothing can be edited, and nothing was changed.");
            break;
        }
        if (!save.readable()) {
            fatalScreen("Unsupported save", dsr::statusText(save.status()));
            break;
        }

        char key[16];
        std::snprintf(key, sizeof(key), "%08x", (unsigned)(uid.uid[0] & 0xFFFFFFFFu)); // tells accounts' backups apart
        editorLoop(save, user, file, key);
    } while (false);

    if (mounted) fsdevUnmountDevice(kMount);
    if (accountsReady) accountExit();
    g_gfx.shutdown();
    return 0;
}
