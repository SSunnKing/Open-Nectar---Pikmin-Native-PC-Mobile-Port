#pragma once

#include "imgui.h"
#include "imgui_internal.h"

// Gamepad navigation for the launcher.
//
// Dear ImGui already moves focus from the d-pad, activates with A
// (ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A) and cancels with B
// (ImGuiKey_GamepadFaceRight). It uses the left stick to scroll, not to move
// focus. These helpers turn a stick deflection into one d-pad direction so
// the stick moves focus the same way, and decide which dialog button B
// presses. The launcher feeds the result into ImGui; it does not track focus.

#include <string>

namespace pikmin {
namespace launcher {

// 0..1 magnitudes after the SDL dead zone, matching imgui_impl_sdl2
// (dead zone 8000, full scale 32767 / -32768). Positive Y is down, as in SDL.
struct LauncherStick {
    float left = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float down = 0.0f;
};

struct LauncherDpad {
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;

    bool any() const { return left || right || up || down; }
};

// ImGui's SDL2 backend treats an analog key as held above this.
constexpr float kLauncherStickNavThreshold = 0.10f;

// One SDL axis sample. `edge` is the dead-zone boundary and `full` the end of
// the scale (negative for the negative half). At the dead zone the result is
// 0; at full scale it is 1.
inline float launcherStickMagnitude(int raw, int edge, int full)
{
    if (full == edge) return 0.0f;
    float value = float(raw - edge) / float(full - edge);
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

inline LauncherStick launcherStickFromAxes(int x, int y, int deadZone = 8000)
{
    LauncherStick stick;
    stick.left = launcherStickMagnitude(x, -deadZone, -32768);
    stick.right = launcherStickMagnitude(x, deadZone, 32767);
    stick.up = launcherStickMagnitude(y, -deadZone, -32768);
    stick.down = launcherStickMagnitude(y, deadZone, 32767);
    return stick;
}

inline bool launcherStickActive(const LauncherStick& stick, float threshold = kLauncherStickNavThreshold)
{
    return stick.left >= threshold || stick.right >= threshold || stick.up >= threshold || stick.down >= threshold;
}

// One cardinal direction. A held d-pad button wins, so the stick cannot fight
// it. Otherwise the strongest stick axis at or above `threshold`. An exact
// tie keeps the earlier axis: left, right, up, then down.
inline LauncherDpad launcherNavDirection(const LauncherStick& stick, const LauncherDpad& dpad,
                                        float threshold = kLauncherStickNavThreshold)
{
    if (dpad.any()) return dpad;
    LauncherDpad out;
    float best = 0.0f;
    bool any = false;
    const auto take = [&](float magnitude, LauncherDpad direction) {
        if (magnitude < threshold) return;
        if (any && magnitude <= best) return;
        any = true;
        best = magnitude;
        out = direction;
    };
    take(stick.left, LauncherDpad{true, false, false, false});
    take(stick.right, LauncherDpad{false, true, false, false});
    take(stick.up, LauncherDpad{false, false, true, false});
    take(stick.down, LauncherDpad{false, false, false, true});
    return out;
}

// When the stick is past the threshold, the launcher rewrites the d-pad keys
// to `dpad` and clears the stick so ImGui moves focus instead of scrolling.
struct LauncherNavOverride {
    bool apply = false;
    LauncherDpad dpad{};
};

inline LauncherNavOverride launcherNavOverride(const LauncherStick& stick, const LauncherDpad& dpad,
                                              float threshold = kLauncherStickNavThreshold)
{
    LauncherNavOverride out;
    out.apply = launcherStickActive(stick, threshold);
    out.dpad = out.apply ? launcherNavDirection(stick, dpad, threshold) : dpad;
    return out;
}

// B closes a dialog the way Escape does when there is a cancel button, and
// otherwise presses a secondary button only when its label dismisses the
// dialog. Other secondaries start something ("Update from a folder"), so B
// leaves those dialogs alone.
inline bool launcherGamepadDismissLabel(const std::string& label)
{
    return label == "Cancel" || label == "Close" || label == "Back" || label == "Not now";
}

// -1: leave the dialog open. 2: the cancel slot (same answer as Escape).
// 1: the secondary button.
inline int launcherGamepadDialogBack(bool hasCancel, const std::string& secondary)
{
    if (hasCancel) return 2;
    if (launcherGamepadDismissLabel(secondary)) return 1;
    return -1;
}

// ImGui selects a default item as soon as the window appears, but keeps the
// cursor hidden until a pad move. That first move would skip the item. Reveal
// it instead, and let the next move change focus. A cleared item (B on the
// home screen, NavId == 0) is left for ImGui to initialise.
inline bool launcherNavRevealFocus(bool cursorVisible, bool hasNavItem, bool directionHeld)
{
    return !cursorVisible && hasNavItem && directionHeld;
}

// Rewrite the queued gamepad events before ImGui::NewFrame().
//
// The left stick is turned into one d-pad direction and its analog is cleared
// in place, so the stick moves focus and does not scroll. Appending a second
// event would be held back by ImGui's input trickle and would not take effect
// until the next frame. The first direction while ImGui already has a hidden
// item only shows that item; the key is left released so the press does not
// also move away from it.
inline void launcherApplyGamepadNav()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) return;
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) return;

    ImGuiContext& g = *context;
    LauncherStick stick;
    LauncherDpad dpad;
    bool sawStick = false;
    for (const ImGuiInputEvent& event : g.InputEventsQueue) {
        if (event.Type != ImGuiInputEventType_Key) continue;
        switch (event.Key.Key) {
        case ImGuiKey_GamepadLStickLeft: stick.left = event.Key.AnalogValue; sawStick = true; break;
        case ImGuiKey_GamepadLStickRight: stick.right = event.Key.AnalogValue; sawStick = true; break;
        case ImGuiKey_GamepadLStickUp: stick.up = event.Key.AnalogValue; sawStick = true; break;
        case ImGuiKey_GamepadLStickDown: stick.down = event.Key.AnalogValue; sawStick = true; break;
        case ImGuiKey_GamepadDpadLeft: dpad.left = event.Key.Down; break;
        case ImGuiKey_GamepadDpadRight: dpad.right = event.Key.Down; break;
        case ImGuiKey_GamepadDpadUp: dpad.up = event.Key.Down; break;
        case ImGuiKey_GamepadDpadDown: dpad.down = event.Key.Down; break;
        default: break;
        }
    }

    LauncherDpad move = dpad;
    bool clearStick = false;
    if (sawStick) {
        const LauncherNavOverride nav = launcherNavOverride(stick, dpad);
        const bool residual = stick.left > 0.0f || stick.right > 0.0f || stick.up > 0.0f || stick.down > 0.0f;
        if (nav.apply) {
            move = nav.dpad;
            clearStick = true;
        } else if (residual) {
            clearStick = true;
        }
    }

    const bool reveal = launcherNavRevealFocus(g.NavCursorVisible, g.NavId != 0, move.any());
    const LauncherDpad written = reveal ? LauncherDpad{} : move;
    const bool rewriteDpad = reveal || (sawStick && launcherNavOverride(stick, dpad).apply);

    if (rewriteDpad) {
        bool seenLeft = false, seenRight = false, seenUp = false, seenDown = false;
        for (ImGuiInputEvent& event : g.InputEventsQueue) {
            if (event.Type != ImGuiInputEventType_Key) continue;
            bool down = false;
            bool* seen = nullptr;
            switch (event.Key.Key) {
            case ImGuiKey_GamepadDpadLeft: seen = &seenLeft; down = written.left; break;
            case ImGuiKey_GamepadDpadRight: seen = &seenRight; down = written.right; break;
            case ImGuiKey_GamepadDpadUp: seen = &seenUp; down = written.up; break;
            case ImGuiKey_GamepadDpadDown: seen = &seenDown; down = written.down; break;
            default: continue;
            }
            *seen = true;
            event.Key.Down = down;
            event.Key.AnalogValue = down ? 1.0f : 0.0f;
        }
        if (written.left && !seenLeft) io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, true);
        if (written.right && !seenRight) io.AddKeyEvent(ImGuiKey_GamepadDpadRight, true);
        if (written.up && !seenUp) io.AddKeyEvent(ImGuiKey_GamepadDpadUp, true);
        if (written.down && !seenDown) io.AddKeyEvent(ImGuiKey_GamepadDpadDown, true);
    }

    if (clearStick) {
        for (ImGuiInputEvent& event : g.InputEventsQueue) {
            if (event.Type != ImGuiInputEventType_Key) continue;
            switch (event.Key.Key) {
            case ImGuiKey_GamepadLStickLeft:
            case ImGuiKey_GamepadLStickRight:
            case ImGuiKey_GamepadLStickUp:
            case ImGuiKey_GamepadLStickDown:
                event.Key.Down = false;
                event.Key.AnalogValue = 0.0f;
                break;
            default: break;
            }
        }
    }

    if (!reveal) return;
    ImGui::SetNavCursorVisible(true);
    g.NavHighlightItemUnderNav = true;
}

} // namespace launcher
} // namespace pikmin
