#include "ui.h"

#include <algorithm>
#include <cstdio>

namespace dsr {

namespace {

// Colours and sizes match the layout preview.
constexpr Color kBg{20, 22, 26, 255}, kPanel{28, 31, 37, 255}, kPanel2{35, 39, 47, 255}, kLine{52, 58, 69, 255};
constexpr Color kText{236, 230, 216, 255}, kMuted{163, 158, 146, 255}, kDim{110, 115, 128, 255};
constexpr Color kEmber{240, 165, 60, 255}, kEmberDark{201, 113, 27, 255}, kEmberText{32, 21, 7, 255};
constexpr Color kGood{143, 196, 143, 255}, kBad{224, 122, 106, 255}, kFooter{16, 18, 22, 255}, kDim50{0, 0, 0, 170};

constexpr int kPad = 28;
constexpr int kContentTop = 118, kContentBottom = 626;       // area between the tabs and the message strip
constexpr int kRowH = 56, kRows = 8;                          // icon rows (Add, Items)
constexpr int kListW = 690, kGap = 22;
constexpr int kFooterY = 666;

Rect R(int x, int y, int w, int h) { return Rect{x, y, w, h}; }

Color toneColor(Tone t) { return t == Tone::Bad ? kBad : t == Tone::Good ? kGood : kEmber; }

// Vertically centres a line of text inside [y, y+h).
void textMid(Gfx& g, const std::string& s, int x, int y, int h, int size, Color c, Align a = Align::Left) {
    g.text(s, x, y + (h - g.lineHeight(size)) / 2, size, c, a);
}

void pill(Gfx& g, Rect r, const std::string& label, int size, Color bg, Color fg, Color border, int radius = -1) {
    if (radius < 0) radius = r.h / 2;
    g.fillRound(r, bg, radius);
    if (border.a) g.outlineRound(r, border, 1, radius);
    textMid(g, label, r.x + r.w / 2, r.y, r.h, size, fg, Align::Center);
}

void placeholder(Gfx& g, Rect r) {
    int s = std::min(r.w, r.h) * 5 / 6;
    Rect q = R(r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s, s);
    g.fillRound(q, kPanel2, 8);
    g.outlineRound(q, kLine, 1, 8);
}

void drawIcon(Gfx& g, uint32_t tag, uint32_t id, Rect r) {
    if (!g.icon(tag, id, r)) placeholder(g, r);
}

void rowBase(Gfx& g, Rect r, bool selected) {
    if (!selected) return;
    g.fillRound(r, kPanel2, 9);
    g.fillRound(R(r.x + 6, r.y + 10, 4, r.h - 20), kEmber, 2);
}

struct FooterItem { const char* key; const char* label; };

void footer(Gfx& g, const std::vector<FooterItem>& items) {
    g.fill(R(0, kFooterY, kScreenW, kScreenH - kFooterY), kFooter);
    g.fill(R(0, kFooterY, kScreenW, 1), kLine);
    int x = kPad;
    for (const FooterItem& it : items) {
        int kw = std::max(30, g.textWidth(it.key, 17) + 18);
        pill(g, R(x, kFooterY + 14, kw, 28), it.key, 17, kPanel2, kText, Color{0, 0, 0, 0}, 8);
        x += kw + 8;
        g.text(it.label, x, kFooterY + 14 + (28 - g.lineHeight(19)) / 2, 19, kMuted);
        x += g.textWidth(it.label, 19) + 22;
    }
}

void title(Gfx& g, const std::string& sub) {
    g.text("DSR SAVE EDITOR", kPad, 22, 22, kEmber);
    if (!sub.empty()) g.text(sub, kScreenW - kPad, 24, 20, kMuted, Align::Right);
}

} // namespace

// ------------------------------------------------------------------ helpers

size_t countChanges(const Save& s) {
    size_t n = 0;
    for (size_t i = 0; i < kFieldCount; i++)
        if (i != kFieldLevel && s.changed(i)) n++;
    if (s.changed(kFieldLevel)) n++;
    for (size_t i = 0; i < s.itemCount(); i++)
        if (!s.itemIsNew(i) && s.itemChanged(i)) n++;
    return n + s.addedEntryCount();
}

std::vector<std::string> changeSummary(const Save& s, size_t maxLines) {
    std::vector<std::string> all;
    for (size_t i = 0; i < kFieldCount; i++)
        if (s.changed(i)) all.push_back(std::string(kFields[i].name) + ":  " + withCommas(s.original(i)) + "  ->  " + withCommas(s.get(i)));
    for (size_t i = 0; i < s.itemCount(); i++) {
        if (s.itemIsNew(i)) all.push_back(std::string("Add ") + s.itemName(i) + " x" + std::to_string(s.itemQty(i)));
        else if (s.itemChanged(i)) all.push_back(std::string(s.itemName(i)) + ":  " + std::to_string(s.itemOriginalQty(i)) + "  ->  " + std::to_string(s.itemQty(i)));
    }
    if (s.addedEntryCount() > 0)
        for (size_t a = 0; a < kAddableCount; a++)
            if (kAddableItems[a].tag != kGoodsTag && s.addedThisSession(a)) all.push_back(std::string("Add ") + kAddableItems[a].name);
    if (maxLines == 0 || all.size() <= maxLines) return all;
    std::vector<std::string> out(all.begin(), all.begin() + (std::ptrdiff_t)(maxLines - 1));
    out.push_back("... and " + std::to_string(all.size() - (maxLines - 1)) + " more");
    return out;
}

std::string withCommas(uint32_t v) {
    std::string s = std::to_string(v), r;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        r.insert(r.begin(), s[(size_t)i]);
        if (++c % 3 == 0 && i > 0) r.insert(r.begin(), ',');
    }
    return r;
}

std::string fitText(Gfx& g, const std::string& s, int maxW, int size) {
    if (g.textWidth(s, size) <= maxW) return s;
    std::string t = s;
    while (!t.empty()) {
        // drop one whole UTF-8 character
        size_t k = t.size() - 1;
        while (k > 0 && ((unsigned char)t[k] & 0xC0) == 0x80) k--;
        t.erase(k);
        if (g.textWidth(t + "...", size) <= maxW) return t + "...";
    }
    return "";
}

std::vector<std::string> wrapText(Gfx& g, const std::string& s, int maxW, int size) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        std::string para = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? s.size() + 1 : nl + 1;
        if (para.empty()) { out.push_back(""); continue; }
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            i = sp == std::string::npos ? para.size() : sp + 1;
            std::string trial = line.empty() ? word : line + " " + word;
            if (line.empty() || g.textWidth(trial, size) <= maxW) line = trial;
            else { out.push_back(line); line = word; }
            if (g.textWidth(line, size) > maxW) { out.push_back(fitText(g, line, maxW, size)); line.clear(); }
        }
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

int scrollTop(int sel, int total, int rows) {
    if (total <= rows) return 0;
    int top = sel - rows / 2;
    return std::max(0, std::min(top, total - rows));
}

// ------------------------------------------------------------------ simple screens

void drawWarning(Gfx& g) {
    g.fill(R(0, 0, kScreenW, kScreenH), kBg);
    title(g, "");
    g.text("Before you start", kPad, 110, 34, kText);
    static const char* const lines[] = {
        "This edits your REAL save data on this console.",
        "A backup is written to the SD card before every write.",
        "Edited saves can be flagged online. Play OFFLINE with an edited save, and keep a copy of your untouched save.",
        "If the save holds several characters you will be asked which one to edit.",
        "Close the game completely before running this.",
    };
    int y = 176;
    for (const char* l : lines) {
        g.fill(R(kPad, y + 12, 6, 6), kEmber);
        for (const std::string& w : wrapText(g, l, kScreenW - 2 * kPad - 28, 24)) {
            g.text(w, kPad + 28, y, 24, kText);
            y += g.lineHeight(24);
        }
        y += 14;
    }
    footer(g, {{"A", "Continue"}, {"+", "Exit"}});
}

void drawNotice(Gfx& g, const std::string& ttl, const std::string& detail, const std::string& hint, bool bad) {
    g.fill(R(0, 0, kScreenW, kScreenH), kBg);
    title(g, "");
    g.text(ttl, kPad, 110, 34, bad ? kBad : kText);
    int y = 170;
    for (const std::string& w : wrapText(g, detail, kScreenW - 2 * kPad, 22)) {
        if (y + g.lineHeight(22) > kFooterY - 8) break;
        g.text(w, kPad, y, 22, kText);
        y += g.lineHeight(22);
    }
    footer(g, {{"+", hint.empty() ? "Exit" : hint.c_str()}});
}

void drawChoice(Gfx& g, const std::string& ttl, const std::string& subtitle, const std::vector<ChoiceRow>& rows, int sel,
                const std::string& hint) {
    g.fill(R(0, 0, kScreenW, kScreenH), kBg);
    title(g, "");
    g.text(ttl, kPad, 100, 32, kText);
    if (!subtitle.empty()) g.text(subtitle, kPad, 144, 21, kMuted);
    const int top0 = 190, n = (int)rows.size(), visible = std::min(n, 8);
    g.fillRound(R(kPad, top0, kScreenW - 2 * kPad, visible * kRowH + 8), kPanel, 12);
    int first = scrollTop(sel, n, visible);
    for (int i = 0; i < visible; i++) {
        int idx = first + i, y = top0 + 4 + i * kRowH;
        if (idx == sel) {
            rowBase(g, R(kPad + 6, y, kScreenW - 2 * kPad - 12, kRowH), true);
        }
        textMid(g, fitText(g, rows[(size_t)idx].title, 400, 24), kPad + 30, y, kRowH, 24, kText);
        textMid(g, fitText(g, rows[(size_t)idx].detail, 720, 20), kScreenW - kPad - 22, y, kRowH, 20, kMuted, Align::Right);
    }
    footer(g, {{"Up Down", "Choose"}, {"A", hint.empty() ? "Select" : hint.c_str()}, {"+", "Exit"}});
}

// ------------------------------------------------------------------ editor

namespace {

void header(Gfx& g, const EditorView& v) {
    const Save& s = *v.save;
    g.text("DSR SAVE EDITOR", kPad, 22, 22, kEmber);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u changes", (unsigned)v.pending);
    if (v.pending == 1) std::snprintf(buf, sizeof(buf), "1 change");
    int pw = g.textWidth(buf, 19) + 28;
    Rect p = R(kScreenW - kPad - pw, 18, pw, 34);
    pill(g, p, buf, 19, kBg, v.pending ? kEmber : kMuted, v.pending ? kEmberDark : kLine);
    std::string who = "Level " + std::to_string(s.get(kFieldLevel)) + "    Souls " + withCommas(s.get(0));
    int wx = p.x - 24;
    g.text(who, wx, 24, 20, kText, Align::Right);
    g.text(fitText(g, v.user, 220, 20), wx - g.textWidth(who, 20) - 28, 24, 20, kMuted, Align::Right);
}

void tabs(Gfx& g, Screen cur) {
    static const char* const names[] = {"Stats", "Items", "Add"};
    int x = kPad;
    for (int i = 0; i < 3; i++) {
        int w = g.textWidth(names[i], 22) + 44;
        bool on = (int)cur == i;
        textMid(g, names[i], x + w / 2, 66, 40, 22, on ? kText : kMuted, Align::Center);
        if (on) g.fill(R(x, 104, w, 4), kEmber);
        x += w + 6;
    }
    g.fill(R(kPad, 108, kScreenW - 2 * kPad, 1), kLine);
}


// Right-hand panel with a big icon, a name and aligned fields.
struct Field { std::string label, value; Color color; };
void detailPanel(Gfx& g, Rect p, bool hasIcon, uint32_t tag, uint32_t id, const std::string& name, const std::string& sub,
                 const std::vector<Field>& fields, const std::string& note, const std::string& actionLabel, bool actionEnabled) {
    g.fillRound(p, kPanel, 12);
    int x = p.x + 26, w = p.w - 52, y = p.y + 22;
    if (hasIcon) {
        Rect pic = R(x, y, 150, 150);
        g.fillRound(pic, kPanel2, 12);
        drawIcon(g, tag, id, R(pic.x + 10, pic.y + 10, 130, 130));
        int tx = x + 150 + 22, tw = w - 172;
        auto lines = wrapText(g, name, tw, 30);
        int ty = y + 20;
        for (size_t i = 0; i < lines.size() && i < 3; i++) { g.text(lines[i], tx, ty, 30, kText); ty += g.lineHeight(30); }
        g.text(fitText(g, sub, tw, 19), tx, ty + 10, 19, kMuted);
        y += 150 + 14;
    } else {
        g.text(fitText(g, name, w, 30), x, y, 30, kText);
        y += g.lineHeight(30) + 4;
        if (!sub.empty()) { g.text(fitText(g, sub, w, 19), x, y, 19, kMuted); y += g.lineHeight(19) + 8; }
        y += 6;
    }
    for (const Field& f : fields) {
        g.text(f.label, x, y, 20, kMuted);
        g.text(fitText(g, f.value, w - 170, 20), x + 170, y, 20, f.color);
        y += 30;
    }
    int noteTop = y + 8;
    g.fill(R(x, noteTop, w, 1), kLine);
    int ny = noteTop + 12, limit = p.y + p.h - 70;
    for (const std::string& l : wrapText(g, note, w, 19)) {
        if (ny + g.lineHeight(19) > limit) break;
        g.text(l, x, ny, 19, kMuted);
        ny += g.lineHeight(19);
    }
    if (!actionLabel.empty()) {
        Rect b = R(x, p.y + p.h - 64, w, 46);
        pill(g, b, actionLabel, 22, actionEnabled ? kEmber : kPanel2, actionEnabled ? kEmberText : kMuted, Color{0, 0, 0, 0}, 12);
    }
}

// Everything has been added and played on a real console except key items and the cut armor.
const char* supportText(size_t addable) {
    return (kAddableItems[addable].keyItem || addable >= kNormalAddableCount) ? "Experimental" : "Tested on hardware";
}

void drawAdd(Gfx& g, const EditorView& v) {
    const Save& s = *v.save;
    const AddList& l = *v.add;
    // category chips
    int x = kPad;
    for (size_t c = 0; c < kCategoryCount; c++) {
        std::string label = categoryName((Category)c);
        int w = g.textWidth(label, 18) + 26;
        bool on = l.category() == (Category)c;
        Rect r = R(x, kContentTop, w, 34);
        g.fillRound(r, on ? kEmber : kBg, 17);
        g.outlineRound(r, on ? kEmber : kLine, 1, 17);
        textMid(g, label, r.x + w / 2, r.y, r.h, 18, on ? kEmberText : kMuted, Align::Center);
        x += w + 6;
    }
    {
        std::string r;
        if (!l.query().empty()) r = "Search: " + fitText(g, l.query(), 150, 18) + "    ";
        if (l.hasSelection()) r += std::to_string(l.selectedPos() + 1) + " / " + std::to_string(l.size());
        g.text(r, kScreenW - kPad, kContentTop + 5, 18, l.query().empty() ? kMuted : kEmber, Align::Right);
    }

    const int top = kContentTop + 46, ph = kRows * kRowH + 8;
    Rect list = R(kPad, top, kListW, ph);
    g.fillRound(list, kPanel, 12);
    if (!l.hasSelection()) {
        textMid(g, "No items match.", list.x + 24, list.y, 60, 22, kMuted);
    }
    for (size_t i = 0; i < l.visibleCount(); i++) {
        size_t a = l.at(i);
        const AddableDef& d = kAddableItems[a];
        Rect row = R(list.x + 6, list.y + 4 + (int)i * kRowH, list.w - 12, kRowH);
        rowBase(g, row, l.top() + i == l.selectedPos());
        drawIcon(g, d.tag, d.id, R(row.x + 14, row.y + 4, 48, 48));
        // The type column only helps on the "All" tab; on a category tab the name gets the room instead.
        const bool showKind = l.category() == Category::All;
        textMid(g, fitText(g, d.name, showKind ? 360 : 500, 22), row.x + 76, row.y, kRowH, 22, kText);
        if (showKind) textMid(g, kindName(a), row.x + 456, row.y, kRowH, 17, kMuted);
        Availability av = availabilityOf(s, a);
        textMid(g, av == Availability::Staged ? "Staged" : av == Availability::Owned ? "Owned" : "Missing", row.x + row.w - 16, row.y,
                kRowH, 18, av == Availability::Staged ? kEmber : av == Availability::Owned ? kGood : kDim, Align::Right);
    }

    Rect det = R(kPad + kListW + kGap, top, kScreenW - 2 * kPad - kListW - kGap, ph);
    if (!l.hasSelection()) {
        g.fillRound(det, kPanel, 12);
        g.text("Nothing selected", det.x + 26, det.y + 22, 30, kText);
        g.text("Try another category or clear the search.", det.x + 26, det.y + 70, 19, kMuted);
        return;
    }
    size_t a = l.selected();
    const AddableDef& d = kAddableItems[a];
    Availability av = availabilityOf(s, a);
    std::vector<Field> f;
    f.push_back({"Status", av == Availability::Staged ? "Staged" : av == Availability::Owned ? "Owned" : "Missing",
                 av == Availability::Staged ? kEmber : av == Availability::Owned ? kGood : kMuted});
    if (d.tag == kGoodsTag) {
        size_t o = s.findOwned(a);
        if (o < s.itemCount()) f.push_back({"You have", "x" + std::to_string(s.itemQty(o)), kText});
    }
    const bool stackable = d.maxNum > 1;
    const uint32_t qty = std::max<uint32_t>(1, std::min(v.addQty, d.maxNum));
    if (stackable) f.push_back({"Amount", "x" + std::to_string(qty), qty > 1 ? kEmber : kText});
    f.push_back({"Stack limit", std::to_string(d.maxNum), kText});
    if (d.durability) f.push_back({"Durability", std::to_string(d.durability), kText});
    f.push_back({"Support", supportText(a), kText});
    std::string note;
    bool equipment = d.tag != kGoodsTag;
    if (a >= kNormalAddableCount) note = "Cut armor: not obtainable in the real game. Untested, so keep your backup.";
    else if (d.keyItem) note = "Key items can skip story progress. Only add one if you know what it does.";
    else if (!equipment && av != Availability::Missing) note = stackable ? "You own this. X sets the stack to the amount shown (Left/Right to change, hold to speed up)." : "You own this. Nothing will change.";
    else if (!equipment) note = stackable ? "Adds a new stack. Left/Right choose how many (hold to speed up)." : "Adds one.";
    else if (av == Availability::Owned) note = "You already own this item. Nothing will change.";
    else if (av == Availability::Staged) note = "Added in this session. Press Y to undo.";
    else note = "Adds one new copy at +0 with full durability.";
    // A staged stack of goods can still have its amount changed; a staged piece of equipment can only be undone.
    const bool stagedGoods = av == Availability::Staged && !equipment && stackable;
    const bool can = canStage(s, a) && (av != Availability::Staged || stagedGoods);
    std::string label = (av == Availability::Staged && !stagedGoods) ? "Staged   (Y to undo)" : !can ? "Already owned"
                      : stackable ? ((av == Availability::Owned || stagedGoods) ? "Set to x" : "Add x") + std::to_string(qty) + "   (X)" : "Add   (X)";
    detailPanel(g, det, true, d.tag, d.id, d.name, std::string(kindName(a)) + "   ID " + std::to_string(d.id), f, note, label,
                can);
}

void drawItems(Gfx& g, const EditorView& v) {
    const Save& s = *v.save;
    const int top = kContentTop + 4, ph = kRows * kRowH + 8, n = (int)s.itemCount();
    Rect list = R(kPad, top, kListW, ph);
    g.fillRound(list, kPanel, 12);
    if (n == 0) {
        textMid(g, "No supported items found in this inventory.", list.x + 24, list.y, 60, 22, kMuted);
        g.fillRound(R(kPad + kListW + kGap, top, kScreenW - 2 * kPad - kListW - kGap, ph), kPanel, 12);
        return;
    }
    int first = scrollTop(v.itemSel, n, kRows);
    for (int i = 0; i < kRows && first + i < n; i++) {
        size_t k = (size_t)(first + i);
        Rect row = R(list.x + 6, list.y + 4 + i * kRowH, list.w - 12, kRowH);
        rowBase(g, row, first + i == v.itemSel);
        drawIcon(g, kGoodsTag, s.itemId(k), R(row.x + 14, row.y + 4, 48, 48));
        textMid(g, fitText(g, s.itemName(k), 360, 22), row.x + 76, row.y, kRowH, 22, kText);
        char q[48];
        std::snprintf(q, sizeof(q), "%u / %u", s.itemQty(k), s.itemMaxQty(k));
        textMid(g, q, row.x + row.w - 16, row.y, kRowH, 22, s.itemChanged(k) ? kEmber : kText, Align::Right);
    }
    char pos[32];
    std::snprintf(pos, sizeof(pos), "%d / %d", v.itemSel + 1, n);
    g.text(pos, list.x + list.w - 14, list.y + list.h - 24, 15, kMuted, Align::Right);

    size_t k = (size_t)std::min(std::max(v.itemSel, 0), n - 1);
    std::vector<Field> f;
    f.push_back({"Quantity", std::to_string(s.itemQty(k)), s.itemChanged(k) ? kEmber : kText});
    f.push_back({"Stack limit", std::to_string(s.itemMaxQty(k)), kText});
    f.push_back({"In the file", s.itemIsNew(k) ? "Not owned (added now)" : std::to_string(s.itemOriginalQty(k)), kText});
    detailPanel(g, R(kPad + kListW + kGap, top, kScreenW - 2 * kPad - kListW - kGap, ph), true, kGoodsTag, s.itemId(k), s.itemName(k), "Item", f,
                "Left/Right change the quantity (hold to speed up). X sets it to the limit, Y puts it back.", "", false);
}

void drawStats(Gfx& g, const EditorView& v) {
    const Save& s = *v.save;
    const int top = kContentTop + 4, ph = kRows * kRowH + 8, n = (int)kFieldCount;
    const int rowH = (ph - 8) / n;               // all fields fit, no scrolling
    Rect list = R(kPad, top, kListW, ph);
    g.fillRound(list, kPanel, 12);
    for (int i = 0; i < n; i++) {
        Rect row = R(list.x + 6, list.y + 4 + i * rowH, list.w - 12, rowH);
        rowBase(g, row, i == v.statSel);
        bool ch = s.changed((size_t)i);
        textMid(g, kFields[i].name, row.x + 26, row.y, rowH, 21, kText);
        if (ch) textMid(g, "was " + withCommas(s.original((size_t)i)), row.x + 430, row.y, rowH, 17, kMuted);
        textMid(g, withCommas(s.get((size_t)i)), row.x + row.w - 18, row.y, rowH, 21, ch ? kEmber : kText, Align::Right);
    }
    size_t k = (size_t)std::min(std::max(v.statSel, 0), n - 1);
    std::vector<Field> f;
    f.push_back({"Now", withCommas(s.get(k)), s.changed(k) ? kEmber : kText});
    f.push_back({"In the file", withCommas(s.original(k)), kText});
    f.push_back({"Allowed", withCommas(kFields[k].minV) + " to " + withCommas(kFields[k].maxV), kText});
    std::string note = "Left/Right change it (hold to speed up), L/R by 1,000. X sets the maximum, Y puts it back.";
    if (k != kFieldLevel && k >= kFirstStat && k <= kLastStat)
        note += " Level follows your stats automatically.";
    else if (k == kFieldLevel)
        note += " Level normally follows the stats (total minus " + std::to_string(s.levelOffset()) + ").";
    detailPanel(g, R(kPad + kListW + kGap, top, kScreenW - 2 * kPad - kListW - kGap, ph), false, 0, 0, kFields[k].name, "Character", f, note, "", false);
}

void dialog(Gfx& g, const EditorView& v) {
    g.fill(R(0, 0, kScreenW, kScreenH), kDim50);
    std::vector<std::string> lines;
    std::string head, hint;
    if (v.mode == Mode::ConfirmSave) {
        head = "Write changes to the Switch save?";
        lines = v.summary;
        hint = "A: Save    B: Cancel      A backup is written to the SD card first.";
    } else {
        head = "Discard unsaved changes and exit?";
        hint = "A: Discard and exit    B: Cancel";
    }
    int h = 150 + (int)lines.size() * 30;
    Rect b = R(240, (kScreenH - h) / 2 - 20, 800, h);
    g.fillRound(b, kPanel, 14);
    g.outlineRound(b, kEmberDark, 2, 14);
    g.text(head, b.x + 30, b.y + 24, 28, kText);
    int y = b.y + 80;
    for (const std::string& l : lines) { g.text(fitText(g, l, b.w - 60, 20), b.x + 30, y, 20, kText); y += 30; }
    g.text(fitText(g, hint, b.w - 60, 19), b.x + 30, b.y + b.h - 44, 19, kEmber);
}

} // namespace

void drawEditor(Gfx& g, const EditorView& v) {
    g.fill(R(0, 0, kScreenW, kScreenH), kBg);
    header(g, v);
    tabs(g, v.screen);
    if (v.screen == Screen::Stats) drawStats(g, v);
    else if (v.screen == Screen::Items) drawItems(g, v);
    else drawAdd(g, v);

    if (!v.msg.text.empty()) {
        g.text(fitText(g, v.msg.text, kScreenW - 2 * kPad, 19), kPad, 634, 19, toneColor(v.msg.tone));
    } else if (!v.save->editable()) {
        g.text("Read-only: " + std::string(statusText(v.save->status())), kPad, 634, 19, kBad);
    }

    if (v.screen == Screen::Add)
        footer(g, {{"Up Down", "Move"}, {"Left Right", "Amount"}, {"L R", "Category"}, {"X", "Add"}, {"Y", "Undo"}, {"L3", "Search"}, {"A", "Save"}, {"ZL ZR", "Tabs"}});
    else if (v.screen == Screen::Items)
        footer(g, {{"Up Down", "Item"}, {"Left Right", "Change"}, {"X", "Max"}, {"Y", "Revert"}, {"A", "Save"}, {"ZL ZR", "Tabs"}});
    else
        footer(g, {{"Up Down", "Field"}, {"Left Right", "Change"}, {"L R", "1,000"}, {"X", "Max"}, {"Y", "Revert"}, {"A", "Save"}, {"ZL ZR", "Tabs"}});

    if (v.mode != Mode::Edit) dialog(g, v);
}

} // namespace dsr
