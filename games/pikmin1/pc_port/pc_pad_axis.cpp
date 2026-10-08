/**
 * @file pc_pad_axis.cpp
 * @brief SDL axis to pad axis conversion.
 *
 * Its own translation unit so a test can link it without dragging in the whole
 * window, renderer and audio layer.
 */

#include "pc_window.h"

s8 pc_pad_axis_from_sdl(int sdlAxisValue) {
    // SDL axes run to -32768, so negating one gives 32768, and 32768/256 is
    // 128 -- one past what a signed byte holds, which wraps to -128. Pushing a
    // stick fully in one direction therefore registered as fully the other way,
    // and only at the very end of its travel, which is why easing the stick
    // worked and slamming it did not.
    const int scaled = sdlAxisValue / 256;
    if (scaled > 127) return 127;
    if (scaled < -127) return -127;
    return static_cast<s8>(scaled);
}
