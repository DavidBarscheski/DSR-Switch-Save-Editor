#pragma once
// Browsing helpers for the Add screen. Platform-independent, so the logic is unit-tested on a PC.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "save.h"

namespace dsr {

enum class Category : uint8_t { All, Weapons, Armor, Rings, Items, Materials, Ammo, Spells, KeyItems };
constexpr size_t kCategoryCount = 9;

const char* categoryName(Category c);
Category categoryOf(size_t addable);      // never All; All is only a filter
const char* kindName(size_t addable);     // "Weapon", "Armor", "Ring", ...

enum class Availability { Missing, Owned, Staged };
// Staged = present now but absent in the file as loaded (added in this session).
Availability availabilityOf(const Save& s, size_t addable);

// Whether adding this entry would change anything: a ring/armor/weapon you own cannot be added again.
bool canStage(const Save& s, size_t addable);

class AddList {
public:
    // `rows` is how many rows fit on screen.
    explicit AddList(size_t rows) : rows_(rows ? rows : 1) { rebuild(false); }

    void setCategory(Category c);
    void stepCategory(int dir);                 // wraps
    void setQuery(const std::string& q);        // case-insensitive substring of the name
    void setShowCut(bool on);
    bool showCut() const { return showCut_; }

    void move(int delta);                        // wraps around the ends
    void page(int dir);                          // moves one screenful, clamped (no wrap)
    void jumpTo(size_t addable);                 // select an entry if it is visible

    Category category() const { return cat_; }
    const std::string& query() const { return query_; }
    size_t size() const { return view_.size(); }
    size_t selectedPos() const { return sel_; }
    bool hasSelection() const { return !view_.empty(); }
    size_t selected() const { return view_[sel_]; }          // index into kAddableItems; needs hasSelection()
    size_t top() const { return top_; }                       // first visible row
    size_t visibleCount() const;                              // rows actually shown
    size_t at(size_t row) const { return view_[top_ + row]; } // row < visibleCount()
    size_t countIn(Category c) const;                         // size of a category ignoring the search

private:
    void rebuild(bool keepSelection);
    void clampScroll();

    size_t rows_;
    Category cat_ = Category::All;
    std::string query_;
    bool showCut_ = false;
    std::vector<size_t> view_;
    size_t sel_ = 0, top_ = 0;
};

} // namespace dsr
