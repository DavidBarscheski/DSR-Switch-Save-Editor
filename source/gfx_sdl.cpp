#include "gfx_sdl.h"

#include <SDL_image.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dsr {

namespace {
constexpr size_t kMaxTexts = 600;  // text textures kept between frames
constexpr size_t kMaxIcons = 300;  // icon textures kept (each is 96x96)
}

bool SdlGfx::init(const std::string& iconDir) {
    iconDir_ = iconDir;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { error_ = std::string("SDL_Init: ") + SDL_GetError(); return false; }
    sdl_ = true;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    win_ = SDL_CreateWindow("DSR Save Editor", 0, 0, kScreenW, kScreenH, 0);
    if (!win_) { error_ = std::string("SDL_CreateWindow: ") + SDL_GetError(); return false; }
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) { error_ = std::string("SDL_CreateRenderer: ") + SDL_GetError(); return false; }
    SDL_RenderSetLogicalSize(ren_, kScreenW, kScreenH);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    if (TTF_Init() != 0) { error_ = std::string("TTF_Init: ") + TTF_GetError(); return false; }
    ttf_ = true;
    img_ = (IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != 0; // without it the editor just shows no icons
    if (R_FAILED(plInitialize(PlServiceType_User))) { error_ = "plInitialize failed (no system font)"; return false; }
    pl_ = true;
    if (!font(20)) { error_ = std::string("Could not open the system font: ") + TTF_GetError(); return false; }
    return true;
}

void SdlGfx::shutdown() {
    clearCornerCache();
    clearTextCache();
    clearIconCache();
    for (auto& f : fonts_) TTF_CloseFont(f.second);
    fonts_.clear();
    if (pl_) { plExit(); pl_ = false; }
    if (img_) { IMG_Quit(); img_ = false; }
    if (ttf_) { TTF_Quit(); ttf_ = false; }
    if (ren_) { SDL_DestroyRenderer(ren_); ren_ = nullptr; }
    if (win_) { SDL_DestroyWindow(win_); win_ = nullptr; }
    if (sdl_) { SDL_Quit(); sdl_ = false; }
}

void SdlGfx::present() { SDL_RenderPresent(ren_); }

TTF_Font* SdlGfx::font(int size) {
    auto it = fonts_.find(size);
    if (it != fonts_.end()) return it->second;
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) return nullptr;
    // The shared font stays mapped for as long as pl is initialised, so the memory outlives the font.
    TTF_Font* f = TTF_OpenFontRW(SDL_RWFromMem(fd.address, fd.size), 1, size);
    if (f) fonts_[size] = f;
    return f;
}

void SdlGfx::setColor(Color c) { SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a); }

void SdlGfx::fill(Rect r, Color c) {
    setColor(c);
    SDL_Rect q{r.x, r.y, r.w, r.h};
    SDL_RenderFillRect(ren_, &q);
}

void SdlGfx::outline(Rect r, Color c, int t) {
    fill(Rect{r.x, r.y, r.w, t}, c);
    fill(Rect{r.x, r.y + r.h - t, r.w, t}, c);
    fill(Rect{r.x, r.y, t, r.h}, c);
    fill(Rect{r.x + r.w - t, r.y, t, r.h}, c);
}

void SdlGfx::clearCornerCache() {
    for (auto& c : corners_) if (c.second) SDL_DestroyTexture(c.second);
    corners_.clear();
}

// A white shape of size 2r x 2r whose alpha is its anti-aliased coverage: a filled disc, or a ring of the
// given thickness. Quarters of it are used as rounded corners and tinted with SDL_SetTextureColorMod.
SDL_Texture* SdlGfx::cornerTex(int radius, int thickness) {
    uint64_t key = ((uint64_t)radius << 16) | (uint64_t)thickness;
    auto it = corners_.find(key);
    if (it != corners_.end()) return it->second;
    SDL_Texture* tex = nullptr;
    const int n = radius * 2;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surf) {
        SDL_LockSurface(surf);
        for (int y = 0; y < n; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)surf->pixels + (size_t)y * (size_t)surf->pitch);
            for (int x = 0; x < n; x++) {
                float dx = x + 0.5f - radius, dy = y + 0.5f - radius;
                float d = std::sqrt(dx * dx + dy * dy);
                float outer = std::min(1.0f, std::max(0.0f, radius - d + 0.5f));
                float cover = outer;
                if (thickness > 0) {
                    float inner = std::min(1.0f, std::max(0.0f, (radius - thickness) - d + 0.5f));
                    cover = outer * (1.0f - inner);
                }
                row[x] = ((uint32_t)(cover * 255.0f + 0.5f) << 24) | 0x00FFFFFFu;
            }
        }
        SDL_UnlockSurface(surf);
        tex = SDL_CreateTextureFromSurface(ren_, surf);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(surf);
    }
    corners_.emplace(key, tex);
    return tex;
}

void SdlGfx::fillRound(Rect r, Color c, int radius) {
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    SDL_Texture* t = radius >= 2 ? cornerTex(radius, 0) : nullptr;
    if (!t) { fill(r, c); return; }
    setColor(c);
    SDL_Rect mid{r.x + radius, r.y, r.w - 2 * radius, r.h};
    SDL_Rect left{r.x, r.y + radius, radius, r.h - 2 * radius};
    SDL_Rect right{r.x + r.w - radius, r.y + radius, radius, r.h - 2 * radius};
    SDL_RenderFillRect(ren_, &mid);
    SDL_RenderFillRect(ren_, &left);
    SDL_RenderFillRect(ren_, &right);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    const int q = radius;
    for (int i = 0; i < 4; i++) {
        int cx = i & 1, cy = i >> 1;
        SDL_Rect src{cx * q, cy * q, q, q};
        SDL_Rect dst{r.x + (cx ? r.w - q : 0), r.y + (cy ? r.h - q : 0), q, q};
        SDL_RenderCopy(ren_, t, &src, &dst);
    }
}

void SdlGfx::outlineRound(Rect r, Color c, int th, int radius) {
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    SDL_Texture* t = radius >= 2 && th < radius ? cornerTex(radius, th) : nullptr;
    if (!t) { outline(r, c, th); return; }
    setColor(c);
    SDL_Rect top{r.x + radius, r.y, r.w - 2 * radius, th};
    SDL_Rect bot{r.x + radius, r.y + r.h - th, r.w - 2 * radius, th};
    SDL_Rect lft{r.x, r.y + radius, th, r.h - 2 * radius};
    SDL_Rect rgt{r.x + r.w - th, r.y + radius, th, r.h - 2 * radius};
    SDL_RenderFillRect(ren_, &top);
    SDL_RenderFillRect(ren_, &bot);
    SDL_RenderFillRect(ren_, &lft);
    SDL_RenderFillRect(ren_, &rgt);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    const int q = radius;
    for (int i = 0; i < 4; i++) {
        int cx = i & 1, cy = i >> 1;
        SDL_Rect src{cx * q, cy * q, q, q};
        SDL_Rect dst{r.x + (cx ? r.w - q : 0), r.y + (cy ? r.h - q : 0), q, q};
        SDL_RenderCopy(ren_, t, &src, &dst);
    }
}

int SdlGfx::lineHeight(int size) {
    TTF_Font* f = font(size);
    return f ? TTF_FontHeight(f) : size;
}

int SdlGfx::textWidth(const std::string& s, int size) {
    TTF_Font* f = font(size);
    int w = 0, h = 0;
    if (!f || s.empty() || TTF_SizeUTF8(f, s.c_str(), &w, &h) != 0) return 0;
    return w;
}

void SdlGfx::clearTextCache() {
    for (auto& t : texts_) if (t.second.t) SDL_DestroyTexture(t.second.t);
    texts_.clear();
}

void SdlGfx::text(const std::string& s, int x, int y, int size, Color c, Align a) {
    if (s.empty()) return;
    char key[40];
    std::snprintf(key, sizeof(key), "%d|%02x%02x%02x%02x|", size, c.r, c.g, c.b, c.a);
    std::string k = std::string(key) + s;
    auto it = texts_.find(k);
    if (it == texts_.end()) {
        if (texts_.size() >= kMaxTexts) clearTextCache();
        Tex tex;
        TTF_Font* f = font(size);
        if (f) {
            SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, c.a});
            if (surf) {
                tex.t = SDL_CreateTextureFromSurface(ren_, surf);
                tex.w = surf->w;
                tex.h = surf->h;
                SDL_FreeSurface(surf);
            }
        }
        it = texts_.emplace(k, tex).first; // a failed render is cached too, so it is not retried every frame
    }
    const Tex& t = it->second;
    if (!t.t) return;
    SDL_Rect dst{a == Align::Left ? x : a == Align::Center ? x - t.w / 2 : x - t.w, y, t.w, t.h};
    SDL_RenderCopy(ren_, t.t, nullptr, &dst);
}

void SdlGfx::clearIconCache() {
    for (auto& i : icons_) if (i.second) SDL_DestroyTexture(i.second);
    icons_.clear();
}

bool SdlGfx::icon(uint32_t tag, uint32_t id, Rect r) {
    if (!img_) return false;
    uint64_t key = ((uint64_t)tag << 32) | id;
    auto it = icons_.find(key);
    if (it == icons_.end()) {
        if (icons_.size() >= kMaxIcons) clearIconCache();
        char path[320];
        std::snprintf(path, sizeof(path), "%s/%08x_%u.png", iconDir_.c_str(), (unsigned)tag, (unsigned)id);
        SDL_Texture* tex = nullptr;
        if (SDL_Surface* surf = IMG_Load(path)) {
            tex = SDL_CreateTextureFromSurface(ren_, surf);
            SDL_FreeSurface(surf);
        }
        it = icons_.emplace(key, tex).first;
    }
    if (!it->second) return false;
    SDL_Rect dst{r.x, r.y, r.w, r.h};
    SDL_RenderCopy(ren_, it->second, nullptr, &dst);
    return true;
}

void SdlGfx::clipOn(Rect r) {
    SDL_Rect q{r.x, r.y, r.w, r.h};
    SDL_RenderSetClipRect(ren_, &q);
}

void SdlGfx::clipOff() { SDL_RenderSetClipRect(ren_, nullptr); }

} // namespace dsr
