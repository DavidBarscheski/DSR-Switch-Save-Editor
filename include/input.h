#pragma once
// Hold-to-repeat and speed-up logic for held buttons. No console code, so it is tested on a PC.
#include <cstdint>

namespace dsr {

struct Hold {
    int frames = 0; // how many frames the input has been held (0 = not held)
};

// Call once per frame with whether the input is held right now. Returns true on the frame the press
// starts, then (after `delay` frames) every `interval` frames, and every `fastInterval` frames once it has
// been held for `fastAfter` frames.
inline bool holdFire(Hold& h, bool held, int delay = 24, int interval = 4, int fastAfter = 90, int fastInterval = 2) {
    if (!held) { h.frames = 0; return false; }
    int f = h.frames++;
    if (f == 0) return true;
    if (f < delay) return false;
    int step = f < fastAfter ? interval : fastInterval;
    return (f - delay) % step == 0;
}

// How much a value changes per step while the input is held: 1 at first, then 10, then 100.
inline int holdMagnitude(const Hold& h) {
    int f = h.frames - 1; // frames held before this one
    return f < 60 ? 1 : f < 120 ? 10 : 100;
}

// An analogue stick axis (-32768..32767) as a held direction, with a dead zone.
inline bool stickHeld(int axis, int dir /* -1 or +1 */, int threshold = 18000) {
    return dir < 0 ? axis <= -threshold : axis >= threshold;
}

} // namespace dsr
