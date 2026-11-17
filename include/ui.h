#pragma once
// Screens of the editor, drawn through Gfx. No console or SDL code in here, so it is testable on a PC.
#include <string>
#include <vector>

#include "catalog.h"
#include "gfx.h"
#include "save.h"

namespace dsr {

enum class Mode { Edit, ConfirmSave, ConfirmQuit };
enum class Screen { Stats, Items, Add };
enum class Tone { Info, Good, Bad };

struct Banner {
    std::string text;
    Tone tone = Tone::Info;
};

struct EditorView {
    const Save* save = nullptr;
    const AddList* add = nullptr;
    std::string user, file;
    Screen screen = Screen::Stats;
    int statSel = 0, itemSel = 0;
    Mode mode = Mode::Edit;
    Banner msg;
    uint32_t addQty = 1;    // amount the Add screen will add (stackable items)
    size_t pending = 0;     // number of edits waiting to be saved
    bool recovery = false;  // a save may have damaged the file: saving is off
    std::vector<std::string> summary; // lines for the save dialog; build once with changeSummary() when it opens
};

struct ChoiceRow {
    std::string title, detail;
};

// Number of edits that would be written: changed stats, changed quantities and added entries.
size_t countChanges(const Save& s);

// One line per pending edit (at most `maxLines`, the last one says how many more there are).
std::vector<std::string> changeSummary(const Save& s, size_t maxLines);

// Text helpers (public so they can be tested).
std::string fitText(Gfx& g, const std::string& s, int maxW, int size);                  // adds "..." if too wide
std::vector<std::string> wrapText(Gfx& g, const std::string& s, int maxW, int size);    // honours '\n'
std::string withCommas(uint32_t v);
// First visible row so that `sel` is on screen, roughly centred.
int scrollTop(int sel, int total, int rows);

void drawWarning(Gfx& g);
void drawNotice(Gfx& g, const std::string& title, const std::string& detail, const std::string& hint, bool bad);
void drawChoice(Gfx& g, const std::string& title, const std::string& subtitle, const std::vector<ChoiceRow>& rows, int sel,
                const std::string& hint);
void drawEditor(Gfx& g, const EditorView& v);

} // namespace dsr
