#include "catalog.h"
#include <algorithm>
#include <cctype>

namespace dsr {

namespace {
const Category kCats[] = {
#include "category_data.inc"
};
static_assert(sizeof(kCats) / sizeof(kCats[0]) > 0, "category table is empty");

std::string lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}
} // namespace

const char* categoryName(Category c) {
    static const char* const n[kCategoryCount] = {"All", "Weapons", "Armor", "Rings", "Items", "Materials", "Ammo", "Spells", "Key items"};
    return n[(size_t)c < kCategoryCount ? (size_t)c : 0];
}

Category categoryOf(size_t addable) {
    return addable < sizeof(kCats) / sizeof(kCats[0]) ? kCats[addable] : Category::Items;
}

const char* kindName(size_t addable) {
    switch (categoryOf(addable)) {
        case Category::Weapons: return "Weapon";
        case Category::Armor: return "Armor";
        case Category::Rings: return "Ring";
        case Category::Materials: return "Material";
        case Category::Ammo: return "Ammo";
        case Category::Spells: return "Spell";
        case Category::KeyItems: return "Key item";
        default: return "Item";
    }
}

Availability availabilityOf(const Save& s, size_t addable) {
    if (addable >= kAddableCount || !s.owns(addable)) return Availability::Missing;
    return s.addedThisSession(addable) ? Availability::Staged : Availability::Owned;
}

bool canStage(const Save& s, size_t addable) {
    if (!s.editable() || addable >= kAddableCount) return false;
    if (kAddableItems[addable].tag == kGoodsTag) return true; // owned stacks are set to max instead
    return !s.owns(addable) || s.addedThisSession(addable);   // equipment: not twice, but a staged add can be undone
}


void AddList::setCategory(Category c) { if (c != cat_) { cat_ = c; sel_ = top_ = 0; rebuild(false); } }
void AddList::stepCategory(int dir) {
    size_t n = kCategoryCount, c = ((size_t)cat_ + (dir < 0 ? n - 1 : 1)) % n;
    setCategory((Category)c);
}
void AddList::setQuery(const std::string& q) { query_ = q; rebuild(true); }
void AddList::setShowCut(bool on) { if (on != showCut_) { showCut_ = on; rebuild(true); } }

void AddList::rebuild(bool keepSelection) {
    size_t keep = (keepSelection && !view_.empty()) ? view_[sel_] : (size_t)-1;
    std::string q = lower(query_);
    view_.clear();
    size_t limit = showCut_ ? kAddableCount : kNormalAddableCount;
    for (size_t i = 0; i < limit; i++) {
        if (cat_ != Category::All && categoryOf(i) != cat_) continue;
        if (!q.empty() && lower(kAddableItems[i].name).find(q) == std::string::npos) continue;
        view_.push_back(i);
    }
    sel_ = 0;
    if (keep != (size_t)-1) {
        auto it = std::find(view_.begin(), view_.end(), keep);
        if (it != view_.end()) sel_ = (size_t)(it - view_.begin());
    }
    clampScroll();
}

void AddList::clampScroll() {
    if (view_.empty()) { sel_ = top_ = 0; return; }
    if (sel_ >= view_.size()) sel_ = view_.size() - 1;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows_) top_ = sel_ + 1 - rows_;
    if (top_ + rows_ > view_.size() && view_.size() >= rows_) top_ = view_.size() - rows_;
    if (view_.size() < rows_) top_ = 0;
}

void AddList::move(int delta) {
    if (view_.empty()) return;
    long n = (long)view_.size();
    long p = ((long)sel_ + delta) % n;
    if (p < 0) p += n;
    sel_ = (size_t)p;
    clampScroll();
}

void AddList::page(int dir) {
    if (view_.empty()) return;
    long p = (long)sel_ + (long)dir * (long)rows_;
    p = std::max(0L, std::min(p, (long)view_.size() - 1));
    sel_ = (size_t)p;
    clampScroll();
}

void AddList::jumpTo(size_t addable) {
    auto it = std::find(view_.begin(), view_.end(), addable);
    if (it == view_.end()) return;
    sel_ = (size_t)(it - view_.begin());
    clampScroll();
}

size_t AddList::visibleCount() const { return std::min(rows_, view_.size() - top_); }

size_t AddList::countIn(Category c) const {
    size_t n = 0, limit = showCut_ ? kAddableCount : kNormalAddableCount;
    for (size_t i = 0; i < limit; i++) if (c == Category::All || categoryOf(i) == c) n++;
    return n;
}

} // namespace dsr
