#pragma once
// The few drawing operations the editor's screens need. The screens (ui.cpp) are written against this
// interface so their layout can be tested on a PC; gfx_sdl.cpp implements it for the Switch.
#include <cstdint>
#include <string>

namespace dsr {

struct Color { uint8_t r, g, b, a; };
struct Rect { int x, y, w, h; };
enum class Align { Left, Center, Right };

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;

class Gfx {
public:
    virtual ~Gfx() {}
    virtual void fill(Rect r, Color c) = 0;
    virtual void outline(Rect r, Color c, int thickness) = 0;
    // Rounded versions. A backend without them can keep these defaults (square corners).
    virtual void fillRound(Rect r, Color c, int radius) { (void)radius; fill(r, c); }
    virtual void outlineRound(Rect r, Color c, int thickness, int radius) { (void)radius; outline(r, c, thickness); }
    // `y` is the top of the text's line box (see lineHeight). For Align::Left `x` is the left edge,
    // for Center the middle and for Right the right edge.
    virtual void text(const std::string& s, int x, int y, int size, Color c, Align a = Align::Left) = 0;
    virtual int textWidth(const std::string& s, int size) = 0;
    virtual int lineHeight(int size) = 0;
    // Draws the game icon for an inventory entry scaled into `r`. Returns false if there is no icon
    // (the caller then draws a placeholder).
    virtual bool icon(uint32_t tag, uint32_t id, Rect r) = 0;
    // Everything drawn until clipOff() is cut to `r`.
    virtual void clipOn(Rect r) = 0;
    virtual void clipOff() = 0;
};

} // namespace dsr
