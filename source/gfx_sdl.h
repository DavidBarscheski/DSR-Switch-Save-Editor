#pragma once
// SDL2 implementation of Gfx for the Switch. Only built for the console (needs libnx and SDL2).
#include <SDL.h>
#include <SDL_ttf.h>

#include <map>
#include <string>

#include "gfx.h"

namespace dsr {

class SdlGfx : public Gfx {
public:
    ~SdlGfx() override { shutdown(); }
    // `iconDir` holds <8 hex digits tag>_<id>.png files; it need not exist. Returns false (and sets
    // error()) if graphics could not start.
    bool init(const std::string& iconDir);
    void shutdown();
    const std::string& error() const { return error_; }
    void present();

    void fill(Rect r, Color c) override;
    void outline(Rect r, Color c, int thickness) override;
    void fillRound(Rect r, Color c, int radius) override;
    void outlineRound(Rect r, Color c, int thickness, int radius) override;
    void text(const std::string& s, int x, int y, int size, Color c, Align a) override;
    int textWidth(const std::string& s, int size) override;
    int lineHeight(int size) override;
    bool icon(uint32_t tag, uint32_t id, Rect r) override;
    void clipOn(Rect r) override;
    void clipOff() override;

private:
    struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; };
    TTF_Font* font(int size);
    void clearTextCache();
    void clearIconCache();
    SDL_Texture* cornerTex(int radius, int thickness); // white anti-aliased disc (thickness 0) or ring, 2r x 2r
    void clearCornerCache();
    void setColor(Color c);

    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    bool sdl_ = false, ttf_ = false, img_ = false, pl_ = false;
    std::string iconDir_, error_;
    std::map<int, TTF_Font*> fonts_;
    std::map<std::string, Tex> texts_;
    std::map<uint64_t, SDL_Texture*> corners_;
    std::map<uint64_t, SDL_Texture*> icons_;   // nullptr = looked for, not there
};

} // namespace dsr
