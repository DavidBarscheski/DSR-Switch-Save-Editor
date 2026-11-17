// Host-side tests for the platform-independent core. Build & run with:  make -C tests
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "catalog.h"
#include "input.h"
#include "md5.h"
#include "persist.h"
#include "save.h"
#include "ui.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);              \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

static std::string md5hex(const std::string& s) {
    uint8_t out[16];
    dsr::md5((const uint8_t*)s.data(), s.size(), out);
    return hex(out, 16);
}

static void putLe32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    for (int i = 0; i < 4; i++) v[off + i] = (uint8_t)(x >> (8 * i));
}
static uint32_t getLe32(const std::vector<uint8_t>& v, size_t off) {
    return v[off] | (v[off + 1] << 8) | (v[off + 2] << 16) | ((uint32_t)v[off + 3] << 24);
}

// Builds a synthetic save with a valid checksum.
static std::vector<uint8_t> makeSave(uint32_t base, size_t extra = 0x100) {
    std::vector<uint8_t> v(base + dsr::kChecksumSize + dsr::kHashedSize + extra);
    for (size_t i = 0; i < v.size(); i++) v[i] = (uint8_t)((i * 131 + 7) & 0xFF);
    putLe32(v, dsr::kBasePtrOffset, base);
    // plausible starting values
    for (size_t i = 0; i < dsr::kFieldCount; i++) putLe32(v, base + dsr::kFields[i].addr, 10);

    // Inventory: header + 0x800 blank slots, then a few real-looking entries.
    putLe32(v, base + dsr::kInvEntrySizeOff, 0x1C);
    putLe32(v, base + dsr::kInvCapacityOff, 0x800);
    putLe32(v, base + dsr::kInvCounterOff, 0x100);
    for (uint32_t s = 0; s < 0x800; s++) {
        size_t o = base + dsr::kInvArrayOff + (size_t)s * 0x1C;
        putLe32(v, o + 0, 0xFFFFFFFFu);
        putLe32(v, o + 4, 0xFFFFFFFFu);
        putLe32(v, o + 8, 0);
        putLe32(v, o + 12, 0xFD0000 | s);
        putLe32(v, o + 16, 0);
        putLe32(v, o + 20, 0);
        putLe32(v, o + 24, 0);
    }
    auto put = [&](uint32_t slot, uint32_t tag, uint32_t id, uint32_t qty) {
        size_t o = base + dsr::kInvArrayOff + (size_t)slot * 0x1C;
        putLe32(v, o + 0, tag);
        putLe32(v, o + 4, id);
        putLe32(v, o + 8, qty);
        putLe32(v, o + 16, 1);
    };
    put(5, dsr::kGoodsTag, 1030, 2);      // Titanite Chunk x2
    put(9, dsr::kGoodsTag, 1010, 12);     // Large Titanite Shard x12
    put(12, dsr::kGoodsTag, 500, 3);      // Humanity x3
    put(84, dsr::kGoodsTag, 1234, 5);     // not on the editable list
    put(85, 0x10000000u, 1030, 1);        // armor-type entry with a colliding id: must be ignored
    put(86, dsr::kGoodsTag, 1070, 150);   // impossible stack size: must be ignored
    uint8_t d[16];
    dsr::md5(v.data() + base + 16, dsr::kHashedSize, d);
    std::memcpy(v.data() + base, d, 16);
    return v;
}


// A fake file system with fault injection, to test the save transaction without a console.
struct FakeFs : dsr::IFileSystem {
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> ops;
    bool failCreate = false, corruptBackup = false, failOpenLive = false, partialWrite = false;
    bool failCommit = false, corruptReadback = false, failRead = false;
    std::string live;
    bool readFile(const std::string& p, std::vector<uint8_t>& out) override {
        ops.push_back("read " + p);
        if (failRead && p == live) return false;
        auto it = files.find(p);
        if (it == files.end()) return false;
        out = it->second;
        bool afterWrite = false;
        for (auto& o : ops) if (o == "overwrite") afterWrite = true;
        if (corruptReadback && p == live && afterWrite) out[out.size() / 2] ^= 0xFF;
        return true;
    }
    dsr::CreateResult createNew(const std::string& p, const std::vector<uint8_t>& d) override {
        ops.push_back("create");
        if (files.count(p)) return dsr::CreateResult::Exists;
        if (failCreate) { files[p] = std::vector<uint8_t>(d.begin(), d.begin() + 10); return dsr::CreateResult::Error; }
        files[p] = d;
        if (corruptBackup) files[p][0] ^= 0xFF;
        return dsr::CreateResult::Ok;
    }
    dsr::WriteResult overwriteInPlace(const std::string& p, const std::vector<uint8_t>& d) override {
        if (failOpenLive) { ops.push_back("open-fail"); return dsr::WriteResult::OpenFailed; }
        ops.push_back("overwrite");
        if (partialWrite) { std::memcpy(files[p].data(), d.data(), d.size() / 2); return dsr::WriteResult::Error; }
        files[p] = d;
        return dsr::WriteResult::Ok;
    }
    bool commit() override { ops.push_back("commit"); return !failCommit; }
    bool removeFile(const std::string& p) override { ops.push_back("remove"); return files.erase(p) > 0; }
};


// A Gfx that records what was drawn, with a pessimistic fake font (wider than most real ones).
struct RecGfx : dsr::Gfx {
    struct T { std::string s; int x, y, size; dsr::Align a; int w; };
    std::vector<T> texts;
    std::vector<dsr::Rect> fills;
    std::vector<dsr::Rect> clips;
    int icons = 0, iconMisses = 0;
    bool haveIcons = true;
    std::string log; // JSON lines, only used by DSR_DUMP
    static std::string jc(dsr::Color c) { char b[40]; std::snprintf(b, sizeof b, "[%d,%d,%d,%d]", c.r, c.g, c.b, c.a); return b; }
    static std::string js(const std::string& t) { std::string o; for (char ch : t) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; } return o; }
    size_t dialogFrom = 0; // texts before this index are hidden under a dimming overlay
    void fill(dsr::Rect r, dsr::Color c) override {
        fills.push_back(r);
        { char b[160]; std::snprintf(b, sizeof b, "{\"op\":\"fill\",\"r\":[%d,%d,%d,%d],\"c\":%s}\n", r.x, r.y, r.w, r.h, jc(c).c_str()); log += b; }
        if (c.a < 255 && r.w == dsr::kScreenW && r.h == dsr::kScreenH) dialogFrom = texts.size();
    }
    void outline(dsr::Rect r, dsr::Color c, int t) override { fills.push_back(r); char b[160]; std::snprintf(b, sizeof b, "{\"op\":\"outline\",\"r\":[%d,%d,%d,%d],\"c\":%s,\"t\":%d}\n", r.x, r.y, r.w, r.h, jc(c).c_str(), t); log += b; }
    void fillRound(dsr::Rect r, dsr::Color c, int rad) override {
        fills.push_back(r);
        char b[180]; std::snprintf(b, sizeof b, "{\"op\":\"rfill\",\"r\":[%d,%d,%d,%d],\"c\":%s,\"rad\":%d}\n", r.x, r.y, r.w, r.h, jc(c).c_str(), rad); log += b;
        if (c.a < 255 && r.w == dsr::kScreenW && r.h == dsr::kScreenH) dialogFrom = texts.size();
    }
    void outlineRound(dsr::Rect r, dsr::Color c, int t, int rad) override {
        fills.push_back(r);
        char b[180]; std::snprintf(b, sizeof b, "{\"op\":\"routline\",\"r\":[%d,%d,%d,%d],\"c\":%s,\"t\":%d,\"rad\":%d}\n", r.x, r.y, r.w, r.h, jc(c).c_str(), t, rad); log += b;
    }
    int textWidth(const std::string& s, int size) override { return (int)(s.size() * size * 0.58); }
    int lineHeight(int size) override { return size * 5 / 4; }
    void text(const std::string& s, int x, int y, int size, dsr::Color c, dsr::Align a) override {
        { char b[64]; std::snprintf(b, sizeof b, "\",\"x\":%d,\"y\":%d,\"size\":%d,\"a\":%d,\"c\":", x, y, size, (int)a); log += "{\"op\":\"text\",\"s\":\"" + js(s) + b + jc(c) + "}\n"; }
        texts.push_back({s, x, y, size, a, textWidth(s, size)});
    }
    bool icon(uint32_t tag, uint32_t id, dsr::Rect r) override { fills.push_back(r); { char b[160]; std::snprintf(b, sizeof b, "{\"op\":\"icon\",\"tag\":%u,\"id\":%u,\"r\":[%d,%d,%d,%d]}\n", tag, id, r.x, r.y, r.w, r.h); log += b; } if (haveIcons) { icons++; return true; } iconMisses++; return false; }
    void clipOn(dsr::Rect r) override { clips.push_back(r); }
    void clipOff() override {}
    // Left edge and right edge of a text as it will really be drawn.
    int left(const T& t) const { return t.a == dsr::Align::Left ? t.x : t.a == dsr::Align::Center ? t.x - t.w / 2 : t.x - t.w; }
    int right(const T& t) const { return left(t) + t.w; }
};
// Everything drawn must be on the 1280x720 screen.
static bool onScreen(const RecGfx& g) {
    for (auto& r : g.fills) if (r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 || r.x + r.w > dsr::kScreenW || r.y + r.h > dsr::kScreenH) return false;
    for (auto& t : g.texts) {
        int top = t.y, bottom = t.y + g.texts.size() * 0 + t.size * 5 / 4;
        if (g.left(t) < 0 || g.right(t) > dsr::kScreenW || top < 0 || bottom > dsr::kScreenH) {
            std::printf("  off screen: '%s' x=%d..%d y=%d..%d\n", t.s.c_str(), g.left(t), g.right(t), top, bottom);
            return false;
        }
    }
    return true;
}

static size_t addableIdxByTag(uint32_t tag) {
    for (size_t i = 0; i < dsr::kAddableCount; i++)
        if (dsr::kAddableItems[i].tag == tag) return i;
    return (size_t)-1;
}

int main() {
    // --- MD5 known-answer tests (RFC 1321 and common vectors) ---
    CHECK(md5hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(md5hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(md5hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    CHECK(md5hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    CHECK(md5hex(std::string(55, 'a')) == "ef1772b6dff9a122358552954ad0df65"); // padding edge: 55
    CHECK(md5hex(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218"); // padding edge: 56
    CHECK(md5hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367"); // exact block
    CHECK(md5hex(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21");

    // --- Save model ---
    {
        dsr::Save s;
        CHECK(s.load(makeSave(0x2C0)) == dsr::LoadStatus::Ok);
        CHECK(s.editable());
        CHECK(!s.dirty());
        CHECK(s.get(0) == 10); // Souls
    }
    {
        // Edit, finalize, reload: values persist and checksum is valid again.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.set(0, 999999999);
        s.set(1, 120);
        CHECK(s.dirty() && s.changed(0) && s.changed(1) && !s.changed(2));
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, 0x2C0 + 0xF0) == 999999999);
        CHECK(getLe32(out, 0x2C0 + 0xEC) == 120);

        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.get(0) == 999999999 && t.get(1) == 120);

        // Only intended bytes differ: field bytes + 16 checksum bytes.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 8 + 16);
    }
    {
        // Clamping and revert.
        dsr::Save s;
        s.load(makeSave(0x40));
        s.set(2, 1000);          // Vitality max 99
        CHECK(s.get(2) == 99);
        s.set(2, -5);            // min 1
        CHECK(s.get(2) == 1);
        s.adjust(0, -100);       // Souls floor at 0
        CHECK(s.get(0) == 0);
        s.setMax(0);
        CHECK(s.get(0) == 999999999);
        s.adjust(0, 1);          // stays at max
        CHECK(s.get(0) == 999999999);
        s.revert(0);
        CHECK(s.get(0) == 10 && !s.changed(0));
    }
    {
        // Tampered data -> checksum mismatch -> read-only, edits ignored.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 1000] ^= 0xFF;
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::ChecksumMismatch);
        CHECK(!s.editable() && s.readable());
        s.set(0, 5);
        CHECK(s.get(0) == 10);
    }
    {
        // Bad header / truncated file.
        dsr::Save s;
        CHECK(s.load(std::vector<uint8_t>(10)) == dsr::LoadStatus::TooSmall);
        auto v = makeSave(0x2C0);
        putLe32(v, dsr::kBasePtrOffset, 0xFFFFFF00u);
        CHECK(s.load(v) == dsr::LoadStatus::BadBase);
        auto w = makeSave(0x2C0, 0);
        w.resize(w.size() - 1);
        CHECK(s.load(w) == dsr::LoadStatus::BadBase);
        CHECK(!s.readable() && s.get(0) == 0);
    }

    // --- Inventory ---
    {
        dsr::Save s;
        CHECK(s.load(makeSave(0x2C0)) == dsr::LoadStatus::Ok);
        // Found: Titanite Chunk (slot 5), Large Titanite Shard (9), Humanity (12), in slot order.
        // Ignored: unlisted id 1234, armor-tagged 1030, impossible stack of 150.
        CHECK(s.itemCount() == 3);
        CHECK(std::string(s.itemName(0)) == "Titanite Chunk" && s.itemQty(0) == 2);
        CHECK(std::string(s.itemName(1)) == "Large Titanite Shard" && s.itemQty(1) == 12);
        CHECK(std::string(s.itemName(2)) == "Humanity" && s.itemQty(2) == 3);
        CHECK(!s.dirty());
    }
    {
        // Max the Titanite Chunk stack, finalize, reload.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.setItemMax(0);
        CHECK(s.itemQty(0) == 99 && s.itemChanged(0) && s.dirty());
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));

        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.itemQty(0) == 99 && t.itemQty(1) == 12 && t.itemQty(2) == 3);

        // Only the quantity (4 bytes) and the checksum (16 bytes) may differ.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 4 + 16);
        // Untouched neighbours, including the look-alikes, keep their exact bytes.
        size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };
        CHECK(getLe32(out, slotOff(84) + 8) == 5);
        CHECK(getLe32(out, slotOff(85) + 8) == 1);
        CHECK(getLe32(out, slotOff(86) + 8) == 150);
    }
    {
        // Clamp to 1..99, adjust, revert.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.setItemQty(1, 0);
        CHECK(s.itemQty(1) == 1);
        s.setItemQty(1, 1000);
        CHECK(s.itemQty(1) == 99);
        s.adjustItemQty(1, -10);
        CHECK(s.itemQty(1) == 89);
        s.revertItem(1);
        CHECK(s.itemQty(1) == 12 && !s.dirty());
    }
    {
        // Read-only (checksum mismatch) files cannot have items edited.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 5000] ^= 0xFF;
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::ChecksumMismatch);
        s.setItemMax(0);
        CHECK(s.itemQty(0) == 2);
    }
    {
        // Unrecognised inventory header: no items offered, stats still work.
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kInvCapacityOff, 0x400);
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        CHECK(s.itemCount() == 0);
        CHECK(s.get(0) == 10);
    }

    // --- The u32 at 0x374 varies between real saves and must not matter ---
    {
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kInvEntrySizeOff, 0x14);
        dsr::Save s;
        CHECK(s.load(v) != dsr::LoadStatus::TooSmall);
        CHECK(s.itemCount() == 3);
        // A table entry that is not a valid item (bad tag) makes the inventory unrecognised.
        auto w = makeSave(0x2C0);
        putLe32(w, 0x2C0 + dsr::kInvArrayOff + 5 * 0x1C, 0x30000000u);
        dsr::Save t; t.load(w);
        CHECK(t.itemCount() == 0);
    }

    // --- Header counters and character slots ---
    {
        // Adding to the equipment region raises the >=64 counter only.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        CHECK(s.addItem(addableIdxByTag(0x20000000u), 1) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == 0x101);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvKeyCounterOff) == getLe32(s.originalBytes(), 0x2C0 + dsr::kInvKeyCounterOff));

        // Two characters in one BND4-like file.
        const uint32_t base1 = 0x2C0, base2 = 0x2C0 + 0x60010;
        auto v = makeSave(base1);
        v.resize(base2 + 0x60010 + 0x100);
        std::memcpy(v.data(), "BND4", 4);
        putLe32(v, 0x0C, 2);
        putLe32(v, 0x40 + 0x10, base1);
        putLe32(v, 0x40 + 0x20 + 0x10, base2);
        std::memcpy(v.data() + base2, v.data() + base1, 0x60010);
        putLe32(v, base2 + dsr::kFields[dsr::kFieldLevel].addr, 77);
        uint8_t dg[16];
        dsr::md5(v.data() + base2 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + base2, dg, 16);

        auto chars = dsr::Save::listCharacters(v);
        CHECK(chars.size() == 2);
        CHECK(chars.size() == 2 && chars[0].base == base1 && chars[0].isFileDefault && !chars[1].isFileDefault);
        CHECK(chars.size() == 2 && chars[1].base == base2 && chars[1].level == 77);

        dsr::Save t;
        CHECK(t.load(v, base2) == dsr::LoadStatus::Ok);
        CHECK(t.base() == base2 && t.get(dsr::kFieldLevel) == 77);
        t.set(dsr::kFirstStat, 50);
        auto o2 = t.finalize();
        CHECK(dsr::Save::checksumValid(o2, base2));
        CHECK(std::memcmp(o2.data(), v.data(), base2) == 0); // first character untouched
        dsr::Save fresh;
        CHECK(fresh.load(o2) == dsr::LoadStatus::Ok && fresh.get(dsr::kFirstStat) == 10); // default still char 1
    }

    // --- Key items and ammunition ---
    {
        size_t key = (size_t)-1, ammo = (size_t)-1, plain = (size_t)-1;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            const dsr::AddableDef& d = dsr::kAddableItems[i];
            if (d.keyItem && key == (size_t)-1) key = i;
            if (d.tag == 0 && d.maxNum > 1 && ammo == (size_t)-1) ammo = i;
            if (d.tag == 0 && d.maxNum == 1 && plain == (size_t)-1) plain = i;
        }
        CHECK(key != (size_t)-1 && ammo != (size_t)-1 && plain != (size_t)-1);
        CHECK(dsr::kAddableItems[ammo].maxNum == 999 && dsr::kAddableItems[ammo].durability == 0);
        dsr::Save s;
        s.load(makeSave(0x2C0));
        uint32_t keyCtr0 = getLe32(s.originalBytes(), 0x2C0 + dsr::kInvKeyCounterOff);
        uint32_t ctr0 = getLe32(s.originalBytes(), 0x2C0 + dsr::kInvCounterOff);
        CHECK(s.addItem(key, 1) == dsr::AddResult::Added);
        CHECK(s.addItem(key, 1) == dsr::AddResult::AlreadyOwned);
        CHECK(s.addItem(ammo, 5) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        // The key item went below slot 64 and raised only the key counter; ammo went to the main region.
        CHECK(getLe32(out, 0x2C0 + dsr::kInvKeyCounterOff) == keyCtr0 + 1);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == ctr0 + 1);
        int keySlot = -1, ammoSlot = -1;
        for (uint32_t sl = 0; sl < 0x800; sl++) {
            size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)sl * 0x1C;
            if (getLe32(out, o) == dsr::kGoodsTag && getLe32(out, o + 4) == dsr::kAddableItems[key].id) keySlot = (int)sl;
            if (getLe32(out, o) == 0 && getLe32(out, o + 4) == dsr::kAddableItems[ammo].id) {
                ammoSlot = (int)sl;
                CHECK(getLe32(out, o + 8) == 999);
            }
        }
        CHECK(keySlot >= 0 && keySlot < 64);
        CHECK(ammoSlot >= 64);
        CHECK(dsr::kAddableItems[plain].maxNum == 1);
    }

    // --- Adding rings and armor ---
    {
        size_t ring = (size_t)-1, armor = (size_t)-1;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            if (dsr::kAddableItems[i].tag == 0x20000000u && ring == (size_t)-1) ring = i;
            if (dsr::kAddableItems[i].tag == 0x10000000u && armor == (size_t)-1) armor = i;
        }
        CHECK(ring != (size_t)-1 && armor != (size_t)-1);
        const size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };
        dsr::Save s;
        s.load(makeSave(0x2C0));
        CHECK(!s.owns(ring) && !s.owns(armor));
        CHECK(s.addItem(ring, 99) == dsr::AddResult::Added);
        CHECK(s.addItem(armor, 99) == dsr::AddResult::Added);
        CHECK(s.owns(ring) && s.owns(armor));
        CHECK(s.addItem(ring, 1) == dsr::AddResult::AlreadyOwned);
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x102);
        // The first freed slot after the first equip entry is 23, then 24 if it is also freed.
        int found = 0;
        for (uint32_t slot = 0; slot < 0x800; slot++) {
            uint32_t tag = getLe32(out, slotOff(slot));
            uint32_t id = getLe32(out, slotOff(slot) + 4);
            if (tag == 0x20000000u && id == dsr::kAddableItems[ring].id) {
                found++;
                CHECK(getLe32(out, slotOff(slot) + 8) == 1);
                CHECK(getLe32(out, slotOff(slot) + 12) == ((dsr::kAddableItems[ring].sortKey << 12) | slot));
                CHECK(getLe32(out, slotOff(slot) + 20) == 0);
            }
            if (tag == 0x10000000u && id == dsr::kAddableItems[armor].id) {
                found++;
                CHECK(getLe32(out, slotOff(slot) + 8) == 1);
                CHECK(getLe32(out, slotOff(slot) + 12) == ((dsr::kAddableItems[armor].sortKey << 12) | slot));
                CHECK(getLe32(out, slotOff(slot) + 20) == dsr::kAddableItems[armor].durability);
                CHECK(dsr::kAddableItems[armor].durability > 0);
            }
        }
        CHECK(found == 2);
        // Undo: removing both returns the file to unchanged.
        dsr::Save u;
        u.load(makeSave(0x2C0));
        u.addItem(ring, 1); u.addItem(armor, 1);
        CHECK(u.dirty());
        CHECK(u.revertAdded(ring) && u.revertAdded(armor) && !u.revertAdded(ring));
        CHECK(!u.dirty());
        // Read-only saves refuse.
        auto bad = makeSave(0x2C0); bad[0x2C0 + 20] ^= 1;
        dsr::Save ro; ro.load(bad);
        CHECK(ro.addItem(ring, 1) == dsr::AddResult::NotAllowed);
    }

    // --- Adding items ---
    {
        auto addableIdx = [](uint32_t id) {
            for (size_t i = 0; i < dsr::kAddableCount; i++)
                if (dsr::kAddableItems[i].tag == dsr::kGoodsTag && dsr::kAddableItems[i].id == id) return i;
            return (size_t)-1;
        };
        // Table sanity: unique ids and keys, and every addable item is also editable afterwards.
        for (size_t a = 0; a < dsr::kAddableCount; a++) {
            if (dsr::kAddableItems[a].tag == dsr::kGoodsTag) {
                bool editable = false;
                for (size_t e = 0; e < dsr::kEditableItemCount; e++)
                    editable |= dsr::kEditableItems[e].id == dsr::kAddableItems[a].id;
                CHECK(editable);
            }
            CHECK(dsr::kAddableItems[a].sortKey > 0 && dsr::kAddableItems[a].sortKey < 0x100000);
            for (size_t b2 = a + 1; b2 < dsr::kAddableCount; b2++) {
                bool sameTag = dsr::kAddableItems[a].tag == dsr::kAddableItems[b2].tag;
                CHECK(!(sameTag && dsr::kAddableItems[a].id == dsr::kAddableItems[b2].id));
                // Weapons/armor may legitimately share a key (transformed weapons carry their source's key).
                bool uniqueKeys = dsr::kAddableItems[a].tag == dsr::kGoodsTag || dsr::kAddableItems[a].tag == 0x20000000u;
                CHECK(!(sameTag && uniqueKeys && dsr::kAddableItems[a].sortKey == dsr::kAddableItems[b2].sortKey));
            }
        }

        const size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };

        // Add a new stack: goes to the first freed slot after the first armor entry (85) -> 87,
        // because slot 86 is occupied.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t blue = addableIdx(1040), idx = 999;
        CHECK(blue != (size_t)-1);
        CHECK(s.addItem(blue, 99, &idx) == dsr::AddResult::Added);
        CHECK(s.itemCount() == 4 && idx < s.itemCount());
        CHECK(std::string(s.itemName(idx)) == "Blue Titanite Chunk");
        CHECK(s.itemQty(idx) == 99 && s.itemIsNew(idx) && s.itemChanged(idx) && s.dirty());

        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, slotOff(87) + 0) == dsr::kGoodsTag);
        CHECK(getLe32(out, slotOff(87) + 4) == 1040);
        CHECK(getLe32(out, slotOff(87) + 8) == 99);
        CHECK(getLe32(out, slotOff(87) + 12) == ((1006u << 12) | 87u));
        CHECK(getLe32(out, slotOff(87) + 16) == 1);
        CHECK(getLe32(out, slotOff(87) + 20) == 0 && getLe32(out, slotOff(87) + 24) == 0);
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x101);

        // Calling finalize again must not raise the counter a second time.
        std::vector<uint8_t> again = s.finalize();
        CHECK(again == out);

        // Only the new slot (<=28 bytes), the counter (<=4) and the checksum (<=16) may differ.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 28 + 4 + 16);
        CHECK(getLe32(out, slotOff(86) + 4) == 1070 && getLe32(out, slotOff(85) + 4) == 1030);

        // Reload: the new stack is an ordinary, editable stack now.
        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.itemCount() == 4);
        size_t k = t.findOwned(blue);
        CHECK(k < t.itemCount() && t.itemQty(k) == 99 && !t.itemIsNew(k) && !t.dirty());
    }
    {
        // Two additions use consecutive slots and raise the counter by two.
        const size_t base = 0x2C0;
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t a = 0, b2 = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            if (dsr::kAddableItems[i].id == 1050) a = i;
            if (dsr::kAddableItems[i].id == 1130) b2 = i;
        }
        CHECK(s.addItem(a, 5) == dsr::AddResult::Added);
        CHECK(s.addItem(b2, 7) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 87 * 0x1C + 4) == 1050);
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 88 * 0x1C + 4) == 1130);
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x102);
        CHECK(dsr::Save::checksumValid(out));
    }
    {
        // Owning the item already: its quantity is set, no new slot, counter untouched.
        const size_t base = 0x2C0;
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t chunk = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++)
            if (dsr::kAddableItems[i].id == 1030) chunk = i;
        size_t idx = 0;
        CHECK(s.addItem(chunk, 99, &idx) == dsr::AddResult::SetExisting);
        CHECK(!s.itemIsNew(idx) && s.itemQty(idx) == 99 && s.itemCount() == 3);
        auto out = s.finalize();
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x100);
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 87 * 0x1C) == 0xFFFFFFFFu); // slot 87 still blank
    }
    {
        // Reverting a new item removes it and leaves the file byte-for-byte as it was.
        dsr::Save s;
        auto orig = makeSave(0x2C0);
        s.load(orig);
        size_t a = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++)
            if (dsr::kAddableItems[i].id == 1060) a = i;
        size_t idx = 0;
        CHECK(s.addItem(a, 50, &idx) == dsr::AddResult::Added);
        CHECK(s.itemCount() == 4);
        s.revertItem(idx);
        CHECK(s.itemCount() == 3 && !s.dirty());
        CHECK(s.finalize() == orig);
    }
    {
        // No freed slot left (all blanks are never-used ones): the game appends after the last
        // used slot, so that is what happens (slot 86 is the last used one -> 87, then 88).
        auto v = makeSave(0x2C0);
        for (uint32_t slot = 0; slot < 0x800; slot++) {
            size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)slot * 0x1C;
            if (getLe32(v, o) == 0xFFFFFFFFu) putLe32(v, o + 12, 0xFFFFFFFFu);
        }
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        size_t w = addableIdxByTag(0x00000000u);
        CHECK(w != (size_t)-1);
        CHECK(s.addItem(5, 10) == dsr::AddResult::Added);
        CHECK(s.addItem(w, 1) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 87 * 0x1C + 4) == dsr::kAddableItems[5].id);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 4) == dsr::kAddableItems[w].id);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 0) == 0u);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 20) == dsr::kAddableItems[w].durability);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == 0x102);

        // A save whose last slot is used has nowhere to append: refuse, change nothing.
        auto full = v;
        size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)0x7FF * 0x1C;
        putLe32(full, o + 0, dsr::kGoodsTag); putLe32(full, o + 4, 1234); putLe32(full, o + 8, 1); putLe32(full, o + 12, 0x7FF);
        dsr::md5(full.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(full.data() + 0x2C0, dg, 16);
        dsr::Save f;
        CHECK(f.load(full) == dsr::LoadStatus::Ok);
        CHECK(f.addItem(5, 10) == dsr::AddResult::NoFreeSlot);
        CHECK(!f.dirty() && f.finalize() == full);
    }
    {
        // Read-only file, bad index, and unrecognised inventory header are all refused.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 5000] ^= 0xFF;
        dsr::Save ro;
        CHECK(ro.load(v) == dsr::LoadStatus::ChecksumMismatch);
        CHECK(ro.addItem(5, 10) == dsr::AddResult::NotAllowed);

        dsr::Save ok;
        ok.load(makeSave(0x2C0));
        CHECK(ok.addItem(dsr::kAddableCount, 10) == dsr::AddResult::NotAllowed);

        auto w = makeSave(0x2C0);
        putLe32(w, 0x2C0 + dsr::kInvCapacityOff, 0x400);
        uint8_t dg[16];
        dsr::md5(w.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(w.data() + 0x2C0, dg, 16);
        dsr::Save un;
        un.load(w);
        CHECK(un.addItem(5, 10) == dsr::AddResult::NotAllowed);
    }
    {
        // Quantity is clamped to 1..99.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t idx = 0;
        CHECK(s.addItem(2, 0, &idx) != dsr::AddResult::NotAllowed); // kAddableItems[2]: quantity 0 is raised to 1
        CHECK(s.itemQty(idx) == 1);
        s.setItemQty(idx, 500);
        CHECK(s.itemQty(idx) == 99);
    }

    // --- Automatic Level ---
    {
        CHECK(std::string(dsr::kFields[dsr::kFieldLevel].name) == "Level");
        CHECK(std::string(dsr::kFields[dsr::kFirstStat].name) == "Vitality");
        CHECK(std::string(dsr::kFields[dsr::kLastStat].name) == "Faith");
        CHECK(dsr::kLastStat - dsr::kFirstStat == 7); // exactly eight stats

        dsr::Save s;
        s.load(makeSave(0x2C0)); // eight stats of 10 (sum 80), Level 10 -> offset 70
        CHECK(s.levelOffset() == 70);
        s.set(2, 15); // Vitality 15 -> sum 85
        CHECK(s.get(1) == 15 && s.changed(1));
        s.adjust(5, 5); // Strength 15 -> sum 90
        CHECK(s.get(1) == 20);
        s.revert(2);
        CHECK(s.get(1) == 15);
        s.revert(5);
        CHECK(s.get(1) == 10 && !s.changed(1) && !s.dirty());

        // Level can still be set directly, and stays put until a stat changes again.
        s.set(1, 50);
        CHECK(s.get(1) == 50);
        s.set(0, 123); // souls do not touch Level
        CHECK(s.get(1) == 50);

        // Clamping: all stats to 1 would give a level below 1.
        dsr::Save lo;
        lo.load(makeSave(0x2C0));
        for (size_t i = dsr::kFirstStat; i <= dsr::kLastStat; i++) lo.set(i, 1);
        CHECK(lo.get(1) == 1);

        // Saved level survives a write and reload, and the offset is unchanged.
        dsr::Save w;
        w.load(makeSave(0x2C0));
        w.set(3, 25); // Attunement 25 -> sum 95 -> Level 25
        auto out = w.finalize();
        CHECK(dsr::Save::checksumValid(out));
        dsr::Save r;
        CHECK(r.load(out) == dsr::LoadStatus::Ok);
        CHECK(r.get(1) == 25 && r.get(3) == 25 && r.levelOffset() == 70);
    }
    {
        // Implausible offset in the file (Level far above the stat total): fall back to 82.
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kFields[dsr::kFieldLevel].addr, 500);
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        CHECK(s.levelOffset() == 82);
        s.set(2, 99); // sum 169 -> Level 87
        CHECK(s.get(1) == 87);
    }


    // --- Review fixes (regression tests) ---
    {
        // F06: add -> finalize -> undo -> finalize must give back the original bytes.
        auto v = makeSave(0x2C0);
        dsr::Save s;
        s.load(v);
        size_t armor = addableIdxByTag(0x10000000u);
        CHECK(s.addItem(armor, 1) == dsr::AddResult::Added);
        CHECK(s.finalize() != v);
        CHECK(s.revertAdded(armor));
        CHECK(s.finalize() == v);
    }
    {
        // F07: an existing stack with an impossible quantity is present, so it is not duplicated.
        auto v = makeSave(0x2C0);
        size_t idx = (size_t)-1;
        for (size_t i = 0; i < dsr::kAddableCount; i++)
            if (dsr::kAddableItems[i].tag == dsr::kGoodsTag && dsr::kAddableItems[i].id == 1070) idx = i;
        CHECK(idx != (size_t)-1);
        dsr::Save s;
        s.load(v);
        CHECK(s.owns(idx));
        CHECK(idx != (size_t)-1 && s.addItem(idx, 1) == dsr::AddResult::AlreadyOwned);
        CHECK(s.finalize() == v); // nothing changed
    }
    {
        // F12: reverting restores a loaded out-of-range value exactly (and the loaded level).
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kFields[dsr::kFirstStat].addr, 100);
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        s.load(v);
        uint32_t lvl = s.get(dsr::kFieldLevel);
        s.set(dsr::kFirstStat, 50);
        CHECK(s.get(dsr::kFieldLevel) != lvl);
        s.revert(dsr::kFirstStat);
        CHECK(s.get(dsr::kFirstStat) == 100 && s.get(dsr::kFieldLevel) == lvl);
        CHECK(!s.dirty());
    }

    // --- Save transaction (include/persist.h), with injected failures ---
    {
        const std::string live = "save:/DRAKS0005.sl2", dir = "bk";
        auto orig = makeSave(0x2C0);
        dsr::Save s;
        s.load(orig);
        s.set(dsr::kFirstStat, 20);
        std::vector<uint8_t> edited = s.finalize();
        auto fresh = [&]() { FakeFs f; f.live = live; f.files[live] = orig; return f; };
        auto order = [](const FakeFs& f, const std::string& a, const std::string& b) {
            int ia = -1, ib = -1;
            for (size_t i = 0; i < f.ops.size(); i++) { if (f.ops[i] == a && ia < 0) ia = (int)i; if (f.ops[i] == b && ib < 0) ib = (int)i; }
            return ia >= 0 && ib >= 0 && ia < ib;
        };

        {   // success: backup verified before the live write, then commit, read-back
            FakeFs f = fresh();
            auto r = dsr::saveWithBackup(f, live, dir, "u1_DRAKS0005.sl2", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::Verified && !r.liveMayHaveChanged);
            CHECK(f.files[live] == edited && f.files[r.backupPath] == orig);
            CHECK(order(f, "create", "overwrite") && order(f, "overwrite", "commit"));
        }
        {   // two saves in the same second keep both backups
            FakeFs f = fresh();
            auto r1 = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            f.files[live] = orig; // pretend the file was restored and reloaded
            auto r2 = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r1.backupPath != r2.backupPath && f.files.count(r1.backupPath) && f.files.count(r2.backupPath));
            CHECK(f.files[r1.backupPath] == orig && f.files[r2.backupPath] == orig);
        }
        {   // an existing file at the backup name is never overwritten
            FakeFs f = fresh();
            f.files[dir + "/p.T.bak"] = std::vector<uint8_t>{1, 2, 3};
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::Verified && r.backupPath != dir + "/p.T.bak");
            CHECK((f.files[dir + "/p.T.bak"] == std::vector<uint8_t>{1, 2, 3}));
        }
        {   // backup write fails (short write / close error): live file untouched, nothing left behind
            FakeFs f = fresh(); f.failCreate = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::BackupFailed && !r.liveMayHaveChanged);
            CHECK(f.files[live] == orig && !order(f, "create", "overwrite") && f.files.size() == 1);
        }
        {   // backup reads back different: live file untouched
            FakeFs f = fresh(); f.corruptBackup = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::BackupFailed && f.files[live] == orig && f.files.size() == 1);
        }
        {   // live file changed since it was loaded: refuse, no backup, no write
            FakeFs f = fresh(); f.files[live][100] ^= 1;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::StaleLive && f.files.size() == 1);
        }
        {   // unreadable live file: refuse
            FakeFs f = fresh(); f.failRead = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::StaleLive);
        }
        {   // live file cannot be opened: nothing changed, edits can be kept
            FakeFs f = fresh(); f.failOpenLive = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::OpenFailed && !r.liveMayHaveChanged && f.files[live] == orig);
            CHECK(!r.backupPath.empty() && f.files[r.backupPath] == orig);
        }
        {   // partial write: recovery required, the backup survives
            FakeFs f = fresh(); f.partialWrite = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::WriteFailed && r.liveMayHaveChanged);
            CHECK(f.files[r.backupPath] == orig);
        }
        {   // commit failure: recovery required, never reported as saved
            FakeFs f = fresh(); f.failCommit = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::CommitFailed && r.liveMayHaveChanged && f.files[r.backupPath] == orig);
        }
        {   // read-back mismatch
            FakeFs f = fresh(); f.corruptReadback = true;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", orig, edited, 0x2C0);
            CHECK(r.outcome == dsr::SaveOutcome::VerifyFailed && r.liveMayHaveChanged);
        }
        {   // F04: a valid second character is verified by its own region even when the default
            //      (first) character's checksum is invalid.
            const uint32_t base1 = 0x2C0, base2 = 0x2C0 + 0x60010;
            auto v = makeSave(base1);
            v.resize(base2 + 0x60010 + 0x100);
            std::memcpy(v.data(), "BND4", 4);
            putLe32(v, 0x0C, 2);
            putLe32(v, 0x40 + 0x10, base1);
            putLe32(v, 0x40 + 0x20 + 0x10, base2);
            std::memcpy(v.data() + base2, v.data() + base1, 0x60010);
            uint8_t dg[16];
            dsr::md5(v.data() + base2 + 16, dsr::kHashedSize, dg);
            std::memcpy(v.data() + base2, dg, 16);
            v[base1 + 100] ^= 0xFF; // damage the default character only
            CHECK(!dsr::Save::checksumValid(v, base1) && dsr::Save::checksumValid(v, base2));
            dsr::Save t;
            CHECK(t.load(v, base2) == dsr::LoadStatus::Ok);
            t.set(dsr::kFirstStat, 33);
            std::vector<uint8_t> ed = t.finalize();
            FakeFs f; f.live = live; f.files[live] = v;
            auto r = dsr::saveWithBackup(f, live, dir, "p", "T", v, ed, base2);
            CHECK(r.outcome == dsr::SaveOutcome::Verified);
        }
    }

    // ---- Add-screen logic (catalog.h) ----
    {
        using dsr::Category;
        // Every entry has a category, and the counts match what the preview showed.
        size_t cnt[dsr::kCategoryCount] = {};
        for (size_t i = 0; i < dsr::kNormalAddableCount; i++) {
            CHECK(dsr::categoryOf(i) != Category::All);
            cnt[(size_t)dsr::categoryOf(i)]++;
        }
        CHECK(cnt[(size_t)Category::Rings] == 41);
        CHECK(cnt[(size_t)Category::Armor] == 232);
        CHECK(cnt[(size_t)Category::Weapons] + cnt[(size_t)Category::Ammo] == 210);
        CHECK(cnt[(size_t)Category::KeyItems] > 0 && cnt[(size_t)Category::Spells] > 0);
        // Category must agree with the entry's own data.
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            const auto& d = dsr::kAddableItems[i];
            Category c = dsr::categoryOf(i);
            if (d.tag == 0x20000000u) CHECK(c == Category::Rings);
            if (d.tag == 0x10000000u) CHECK(c == Category::Armor);
            if (d.keyItem) CHECK(c == Category::KeyItems);
            if (c == Category::KeyItems) CHECK(d.keyItem == 1 && d.tag == dsr::kGoodsTag);
            if (c == Category::Ammo) CHECK(d.tag == 0 && d.maxNum > 1);
        }

        dsr::AddList l(8);
        CHECK(l.size() == dsr::kNormalAddableCount);          // cut armor hidden by default
        l.setShowCut(true);
        CHECK(l.size() == dsr::kAddableCount);
        l.setShowCut(false);

        l.setCategory(Category::Rings);
        CHECK(l.size() == 41 && l.countIn(Category::Rings) == 41);
        CHECK(l.visibleCount() == 8 && l.top() == 0 && l.selectedPos() == 0);
        // Moving past the end of the window scrolls it; the selection is always on screen.
        for (int i = 0; i < 20; i++) {
            l.move(1);
            CHECK(l.selectedPos() >= l.top() && l.selectedPos() < l.top() + l.visibleCount());
        }
        l.move(-1000 % 41);
        CHECK(l.selectedPos() < l.size());
        l.setCategory(Category::Rings);                        // same category: keeps position
        // Wrap-around both ways.
        dsr::AddList w(8);
        w.setCategory(Category::Rings);
        w.move(-1);
        CHECK(w.selectedPos() == 40 && w.top() == 41 - 8);
        w.move(1);
        CHECK(w.selectedPos() == 0 && w.top() == 0);
        // Paging clamps instead of wrapping.
        w.page(1); CHECK(w.selectedPos() == 8);
        w.page(-1); w.page(-1); CHECK(w.selectedPos() == 0);
        for (int i = 0; i < 20; i++) w.page(1);
        CHECK(w.selectedPos() == 40);
        // Category stepping wraps through all nine.
        dsr::AddList c(8);
        c.stepCategory(-1); CHECK(c.category() == Category::KeyItems);
        c.stepCategory(1);  CHECK(c.category() == Category::All);

        // Search is case-insensitive, keeps the selection when it still matches, and can be empty.
        dsr::AddList q(8);
        q.setQuery("HAVEL");
        CHECK(q.size() > 0);
        for (size_t i = 0; i < q.size(); i++) {
            std::string n = dsr::kAddableItems[q.at(i < q.visibleCount() ? i : 0)].name;
            for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
            CHECK(n.find("havel") != std::string::npos);
        }
        size_t havel = q.selected();
        q.setQuery("havel's");
        CHECK(q.size() > 0);
        q.setQuery("zzzzzz-no-such-item");
        CHECK(q.size() == 0 && !q.hasSelection() && q.visibleCount() == 0);
        q.move(1); q.page(1);                                   // must not crash on an empty list
        q.setQuery("");
        CHECK(q.size() == dsr::kNormalAddableCount);
        q.jumpTo(havel);
        CHECK(q.hasSelection() && q.selected() == havel);
        // Toggling cut items keeps the selection.
        q.setShowCut(true); CHECK(q.selected() == havel);

        // Availability and canStage follow the save.
        dsr::Save s;
        CHECK(s.load(makeSave(0x2C0)) == dsr::LoadStatus::Ok);
        size_t ring = addableIdxByTag(0x20000000u);
        CHECK(dsr::availabilityOf(s, ring) == dsr::Availability::Missing && dsr::canStage(s, ring));
        CHECK(s.addItem(ring, 1) == dsr::AddResult::Added);
        CHECK(dsr::availabilityOf(s, ring) == dsr::Availability::Staged && s.addedThisSession(ring));
        CHECK(dsr::canStage(s, ring));                         // staged: can be undone
        CHECK(s.revertAdded(ring));
        CHECK(dsr::availabilityOf(s, ring) == dsr::Availability::Missing && !s.addedThisSession(ring));
        // Something already in the file is Owned, not Staged, and cannot be added twice.
        dsr::Save t;
        auto v = makeSave(0x2C0);
        size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)90 * 0x1C;
        putLe32(v, o, dsr::kAddableItems[ring].tag); putLe32(v, o + 4, dsr::kAddableItems[ring].id);
        uint8_t dg[16]; dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg); std::memcpy(v.data() + 0x2C0, dg, 16);
        CHECK(t.load(v) == dsr::LoadStatus::Ok);
        CHECK(dsr::availabilityOf(t, ring) == dsr::Availability::Owned && !t.addedThisSession(ring));
        CHECK(!dsr::canStage(t, ring));
        // A read-only (bad checksum) save can stage nothing.
        auto bad = makeSave(0x2C0); bad[0x2C0 + 200] ^= 1;
        dsr::Save r; r.load(bad);
        CHECK(!dsr::canStage(r, ring));
    }

    // ---- Screens (ui.cpp), drawn into a recording Gfx ----
    {
        using namespace dsr;
        // No two pieces of text may overlap each other.
        auto noOverlap = [](const RecGfx& g, const char* what) {
            bool ok = true;
            for (size_t i = g.dialogFrom; i < g.texts.size(); i++)
                for (size_t j = i + 1; j < g.texts.size(); j++) {
                    const auto &a = g.texts[i], &b = g.texts[j];
                    int ah = a.size * 5 / 4, bh = b.size * 5 / 4;
                    bool vert = a.y + ah - 4 > b.y && b.y + bh - 4 > a.y;
                    bool horiz = g.left(a) < g.right(b) && g.left(b) < g.right(a);
                    if (vert && horiz && !a.s.empty() && !b.s.empty()) {
                        std::printf("  %s: '%s' overlaps '%s'\n", what, a.s.c_str(), b.s.c_str());
                        ok = false;
                    }
                }
            return ok;
        };
        // Helpers.
        {
            RecGfx g;
            CHECK(withCommas(0) == "0" && withCommas(999) == "999" && withCommas(1000) == "1,000" && withCommas(1234567) == "1,234,567");
            CHECK(fitText(g, "short", 500, 20) == "short");
            std::string cut = fitText(g, "a very long item name that cannot fit", 200, 20);
            CHECK(cut.size() < 38 && cut.size() > 3 && cut.substr(cut.size() - 3) == "..." && g.textWidth(cut, 20) <= 200);
            CHECK(fitText(g, "xxxxxxxx", 5, 20).empty());
            auto lines = wrapText(g, "one two three\nfour five six seven eight nine ten", 150, 20);
            for (auto& l : lines) CHECK(g.textWidth(l, 20) <= 150);
            CHECK(lines.size() >= 3 && lines[0] == "one two" + std::string(lines[0].size() > 7 ? " three" : ""));
            CHECK(wrapText(g, "unbrokenwordthatistoolongtofit", 100, 20).size() == 1);
            CHECK(scrollTop(0, 100, 8) == 0 && scrollTop(99, 100, 8) == 92 && scrollTop(50, 100, 8) == 46 && scrollTop(3, 5, 8) == 0);
        }
        Save s;
        CHECK(s.load(makeSave(0x2C0)) == LoadStatus::Ok);
        EditorView v;
        v.save = &s; v.user = "A rather long player nickname"; v.file = "DRAKS0005.sl2";
        AddList al(8);
        v.add = &al;
        for (bool icons : {true, false}) {
            RecGfx g; g.haveIcons = icons;
            drawWarning(g); CHECK(onScreen(g)); CHECK(noOverlap(g, "warning"));
            RecGfx g2; drawNotice(g2, "No save file found", std::string(800, 'x') + "\n  a/b/c (123 bytes)\n  d/e/f (45 bytes)", "Exit", true);
            CHECK(onScreen(g2)); CHECK(noOverlap(g2, "notice"));
            std::vector<ChoiceRow> rows;
            for (int i = 0; i < 12; i++) rows.push_back({"Character " + std::to_string(i + 1) + " with a very long name indeed", "level 120  souls 123,456,789"});
            for (int sel : {0, 5, 11}) { RecGfx g3; drawChoice(g3, "Choose", "This save has several", rows, sel, ""); CHECK(onScreen(g3)); CHECK(noOverlap(g3, "choice")); }
            // Stats and Items screens, every selection, with changes and long numbers.
            for (size_t f = 0; f < kFieldCount; f++) s.setMax(f);
            v.pending = countChanges(s);
            CHECK(v.pending > 0);
            v.screen = Screen::Stats;
            for (int sel = 0; sel < (int)kFieldCount; sel++) { RecGfx g4; g4.haveIcons = icons; v.statSel = sel; drawEditor(g4, v); CHECK(onScreen(g4)); CHECK(noOverlap(g4, "stats")); }
            v.screen = Screen::Items;
            for (int sel = 0; sel < (int)s.itemCount(); sel++) { RecGfx g5; g5.haveIcons = icons; v.itemSel = sel; drawEditor(g5, v); CHECK(onScreen(g5)); CHECK(noOverlap(g5, "items")); }
            // Add screen: every category, every selected item (longest names included), cut items on.
            v.screen = Screen::Add;
            al.setShowCut(true);
            for (size_t c = 0; c < kCategoryCount; c++) {
                al.setCategory((Category)c);
                for (size_t k = 0; k < al.size(); k += (icons ? 1 : 7)) {
                    al.jumpTo(al.size() ? al.at(0) : 0);
                    al.move((int)k);
                    RecGfx g6; g6.haveIcons = icons; v.msg = {"Added Some Item Name x99. Press A to save, or Items screen to adjust.", Tone::Good};
                    drawEditor(g6, v);
                    if (!onScreen(g6) || !noOverlap(g6, "add")) { std::printf("  category %zu selection %zu (%s)\n", c, k, kAddableItems[al.selected()].name); CHECK(false); k = al.size(); c = kCategoryCount; }
                }
            }
            al.setCategory(Category::All);
            al.setQuery("zzzz"); { RecGfx g7; drawEditor(g7, v); CHECK(onScreen(g7)); CHECK(noOverlap(g7, "empty")); }
            al.setQuery("");
            al.setShowCut(false);
            for (Mode m : {Mode::ConfirmSave, Mode::ConfirmQuit}) { RecGfx g8; v.mode = m; v.screen = Screen::Stats; v.summary = changeSummary(s, 9); drawEditor(g8, v); CHECK(onScreen(g8)); CHECK(noOverlap(g8, "dialog")); }
            v.mode = Mode::Edit; v.msg = {};
            s.load(makeSave(0x2C0));
        }
        // The icon is requested for every visible row, and a missing icon still draws something.
        { RecGfx g; v.screen = Screen::Add; al.setCategory(Category::Rings); drawEditor(g, v); CHECK(g.icons == 8 + 1); }
        { RecGfx g; g.haveIcons = false; drawEditor(g, v); CHECK(g.iconMisses == 9 && g.icons == 0); }
        // Change counting and the summary shown in the save dialog.
        { Save t; t.load(makeSave(0x2C0));
          CHECK(changeSummary(t, 9).empty() && countChanges(t) == 0);
          t.set(kFirstStat, 40);
          CHECK(countChanges(t) == 2);                          // the stat and the level that follows it
          size_t ring = addableIdxByTag(0x20000000u);
          t.addItem(ring, 1);
          CHECK(countChanges(t) == 3 && t.addedEntryCount() == 1);
          size_t goods = addableIdxByTag(kGoodsTag);             // an item you do not own yet
          while (t.owns(goods) && goods < kAddableCount) goods++;
          t.addItem(goods, 3);
          auto sm = changeSummary(t, 9);
          CHECK(sm.size() == 4);                                 // stat, level, the goods stack, the ring
          CHECK(changeSummary(t, 2).size() == 2 && changeSummary(t, 2)[1] == "... and 3 more");
          for (auto& l : sm) CHECK(!l.empty());
          t.revertAdded(ring); CHECK(t.addedEntryCount() == 1); }
    }

    // ---- Hold-to-repeat (input.h) ----
    {
        using namespace dsr;
        Hold h;
        std::vector<int> fired;
        for (int f = 0; f < 130; f++) if (holdFire(h, true)) fired.push_back(f);
        CHECK(!fired.empty() && fired[0] == 0);                 // fires on the press itself
        CHECK(fired[1] == 24 && fired[2] == 28 && fired[3] == 32); // then after the delay, steadily
        int gapsFast = 0;
        for (size_t i = 1; i < fired.size(); i++) if (fired[i] >= 90 && fired[i] - fired[i - 1] == 2) gapsFast++;
        CHECK(gapsFast > 10);                                   // faster once held long enough
        for (size_t i = 1; i < fired.size(); i++) CHECK(fired[i] - fired[i - 1] <= 24);
        CHECK(!holdFire(h, false) && h.frames == 0);            // letting go resets it
        CHECK(holdFire(h, true));                               // and the next press fires at once
        // Magnitude steps 1 -> 10 -> 100 as the hold gets longer, and starts at 1 on a fresh press.
        Hold m;
        holdFire(m, true); CHECK(holdMagnitude(m) == 1);
        for (int f = 0; f < 59; f++) holdFire(m, true);
        CHECK(holdMagnitude(m) == 1);
        holdFire(m, true); holdFire(m, true); CHECK(holdMagnitude(m) == 10);
        for (int f = 0; f < 70; f++) holdFire(m, true);
        CHECK(holdMagnitude(m) == 100);
        holdFire(m, false); holdFire(m, true); CHECK(holdMagnitude(m) == 1);
        // A tap (shorter than the delay) fires exactly once.
        Hold t; int n = 0;
        for (int f = 0; f < 10; f++) n += holdFire(t, true);
        n += holdFire(t, false);
        CHECK(n == 1);
        // Stick dead zone.
        CHECK(!stickHeld(0, 1) && !stickHeld(5000, 1) && stickHeld(30000, 1) && stickHeld(-30000, -1) && !stickHeld(-30000, 1));
    }

    // ---- Add amount on the Add screen ----
    {
        using namespace dsr;
        Save s; s.load(makeSave(0x2C0));
        AddList al(8); al.setCategory(Category::Materials);
        EditorView v; v.save = &s; v.add = &al; v.user = "P"; v.file = "f"; v.screen = Screen::Add;
        auto has = [](const RecGfx& g, const std::string& t) { for (auto& x : g.texts) if (x.s == t) return true; return false; };
        size_t a = al.selected();
        CHECK(kAddableItems[a].maxNum > 1);
        { RecGfx g; drawEditor(g, v); CHECK(has(g, "Add x1   (X)") && has(g, "x1")); }
        v.addQty = 7;
        { RecGfx g; drawEditor(g, v); CHECK(has(g, "Add x7   (X)") && has(g, "x7")); }
        v.addQty = 5000; // shown amount never exceeds the stack limit
        { RecGfx g; drawEditor(g, v); CHECK(has(g, "Add x" + std::to_string(kAddableItems[a].maxNum) + "   (X)")); }
        // A single item has no amount row.
        al.setCategory(Category::Rings); v.addQty = 9;
        { RecGfx g; drawEditor(g, v); CHECK(has(g, "Add   (X)") && !has(g, "x9")); }
        // An owned stack says "Set to"; a staged one can still be changed.
        v.addQty = 3; al.setCategory(Category::Materials);
        for (size_t i = 0; i < al.size(); i++) { if (s.owns(al.selected())) break; al.move(1); }
        if (s.owns(al.selected())) { RecGfx g; drawEditor(g, v); CHECK(has(g, "Set to x3   (X)")); }
        al.setCategory(Category::Materials);
        size_t m = al.selected();
        CHECK(s.addItem(m, 4) == AddResult::Added || s.owns(m));
        { RecGfx g; v.addQty = 6; drawEditor(g, v); CHECK(has(g, "Set to x6   (X)") || has(g, "Add x6   (X)")); CHECK(onScreen(g)); }
    }

    // DSR_DUMP=dir writes the draw commands of a few screens for tools/render_dump.py.
    if (const char* dir = std::getenv("DSR_DUMP")) {
        using namespace dsr;
        Save s; s.load(makeSave(0x2C0));
        auto save = [&](const char* name, const std::string& data) { FILE* f = std::fopen((std::string(dir) + "/" + name + ".jsonl").c_str(), "w"); if (f) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); } };
        AddList al(8); EditorView v; v.save = &s; v.add = &al; v.user = "Player"; v.file = "DRAKS0005.sl2"; v.screen = Screen::Add;
        s.set(0, 12345); s.set(kFirstStat, 22); v.pending = countChanges(s);
        { RecGfx g; drawWarning(g); save("warning", g.log); }
        { RecGfx g; drawNotice(g, "No save file found", "No DRAKS*.sl2 file was found on this save.\nIf you have several user profiles, try the one that plays Dark Souls.\n\nWhat is actually on the save for Player:\n  userdata/a.sl2  (1234 bytes)", "Exit", true); save("notice", g.log); }
        { RecGfx g; std::vector<ChoiceRow> r = {{"Character 1", "level 70   souls 12,345   (the game's slot 1)"}, {"Character 2", "level 12   souls 400"}, {"Character 3", "level 120   souls 9,999,999"}}; drawChoice(g, "Which character?", "This save has 3 characters. Choose one to edit.", r, 1, ""); save("choice", g.log); }
        v.summary = changeSummary(s, 9);
        { RecGfx g; v.screen = Screen::Stats; v.statSel = 3; drawEditor(g, v); save("stats", g.log); }
        { RecGfx g; v.screen = Screen::Items; v.itemSel = 1; drawEditor(g, v); save("items", g.log); }
        v.screen = Screen::Add;
        size_t ring = addableIdxByTag(0x20000000u);
        al.setCategory(Category::Rings); s.addItem(ring + 1, 1); v.pending = countChanges(s); al.move(1);
        v.msg = {"Added a ring. Press A to save.", Tone::Good};
        { RecGfx g; drawEditor(g, v); save("add_rings", g.log); }
        al.setCategory(Category::All); al.setQuery("sign"); v.msg = {};
        { RecGfx g; drawEditor(g, v); save("add_search", g.log); }
        al.setQuery(""); v.mode = Mode::ConfirmSave;
        { RecGfx g; drawEditor(g, v); save("confirm", g.log); }
    }

    if (failures == 0) std::printf("All tests passed.\n");
    return failures ? 1 : 0;
}
