/**
 * @file pc_pad_axis.cpp
 * @brief SDL axis to pad axis conversion.
 *
 * Its own translation unit so a test can link it without dragging in the whole
 * window, renderer and audio layer. Keep it a pure conversion: diagnostics live
 * in pc_pad_trace.cpp, which needs a log file and the environment.
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

s8 pc_pad_axis_from_sdl_deadzoned(int sdlAxisValue, int deadZone) {
    const int magnitude = sdlAxisValue < 0 ? -sdlAxisValue : sdlAxisValue;
    if (magnitude <= deadZone) return 0;

    // Rescale so the edge of the dead zone is zero and the rail is still full
    // throw. Without this the first live value is deadZone/256 units, so a
    // wider dead zone (which an Xbox pad needs to ignore its stick drift) would
    // leave an audible step right where the cursor starts moving.
    const int span = 32768 - deadZone;
    if (span <= 0) return sdlAxisValue < 0 ? -127 : 127;

    int scaled = static_cast<int>(((static_cast<long long>(magnitude - deadZone) * 127) + span / 2) / span);
    if (scaled > 127) scaled = 127;
    return static_cast<s8>(sdlAxisValue < 0 ? -scaled : scaled);
}
