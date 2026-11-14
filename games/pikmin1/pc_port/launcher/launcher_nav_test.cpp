// Stick and d-pad mapping for launcher gamepad navigation, plus a headless
// ImGui check that those keys move focus and activate a button. No window.
#include "launcher_nav.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static bool same(const LauncherDpad& pad, bool left, bool right, bool up, bool down)
{
    return pad.left == left && pad.right == right && pad.up == up && pad.down == down;
}

static void testAxes()
{
    const LauncherStick rest = launcherStickFromAxes(0, 0);
    check(rest.left == 0.0f && rest.right == 0.0f && rest.up == 0.0f && rest.down == 0.0f, "centred stick is zero");

    const LauncherStick edge = launcherStickFromAxes(8000, -8000);
    check(edge.right == 0.0f && edge.up == 0.0f && edge.left == 0.0f && edge.down == 0.0f,
          "dead zone (8000) is not a direction");

    const LauncherStick full = launcherStickFromAxes(32767, 32767);
    check(full.right == 1.0f && full.down == 1.0f && full.left == 0.0f && full.up == 0.0f, "full right/down is 1");

    const LauncherStick fullNeg = launcherStickFromAxes(-32768, -32768);
    check(fullNeg.left == 1.0f && fullNeg.up == 1.0f && fullNeg.right == 0.0f && fullNeg.down == 0.0f,
          "full left/up is 1");

    const LauncherStick partial = launcherStickFromAxes(20000, -20000);
    check(partial.right > kLauncherStickNavThreshold && partial.right < 1.0f, "partial right is inside the range");
    check(partial.up > kLauncherStickNavThreshold && partial.left == 0.0f && partial.down == 0.0f,
          "partial up does not spill onto the other axes");
}

static void testDirection()
{
    LauncherStick stick;
    const LauncherDpad none;
    stick.right = 1.0f;
    check(same(launcherNavDirection(stick, none), false, true, false, false), "stick right");

    stick = {};
    stick.right = 0.09f;
    check(!launcherNavDirection(stick, none).any(), "below the threshold is not a move");
    check(!launcherNavOverride(stick, none).apply, "a small deflection does not rewrite the d-pad");

    stick = {};
    stick.right = 0.2f;
    stick.down = 0.8f;
    check(same(launcherNavDirection(stick, none), false, false, false, true), "the stronger axis wins");

    stick = {};
    stick.left = 0.5f;
    stick.right = 0.5f;
    check(same(launcherNavDirection(stick, none), true, false, false, false), "an exact tie keeps the earlier axis");

    const LauncherDpad up{false, false, true, false};
    stick = {};
    stick.right = 1.0f;
    const LauncherNavOverride held = launcherNavOverride(stick, up);
    check(held.apply, "a deflected stick still suppresses stick-scrolling");
    check(same(held.dpad, false, false, true, false), "a held d-pad wins over the stick");

    stick = {};
    check(!launcherNavOverride(stick, none).apply, "a centred stick leaves the d-pad alone");
}

static void testDialogBack()
{
    check(launcherGamepadDialogBack(true, "1080p (1.8 GB)") == 2, "cancel wins over another secondary");
    check(launcherGamepadDialogBack(false, "Close") == 1, "Close is a dismiss");
    check(launcherGamepadDialogBack(false, "Back") == 1, "Back is a dismiss");
    check(launcherGamepadDialogBack(false, "Not now") == 1, "Not now is a dismiss");
    check(launcherGamepadDialogBack(false, "Cancel") == 1, "Cancel as the secondary is a dismiss");
    check(launcherGamepadDialogBack(false, "Use a downloaded folder") == -1, "an action secondary is not B");
    check(launcherGamepadDialogBack(false, "Update from a folder") == -1, "updating from a folder is not B");
    check(launcherGamepadDialogBack(false, "Use a .zip I have") == -1, "picking a zip is not B");
    check(launcherGamepadDialogBack(false, "Play now") == -1, "Play now is not a dismiss");
    check(launcherGamepadDialogBack(false, "") == -1, "a single button stays for A");

    check(launcherNavRevealFocus(false, true, true), "the first direction shows the hidden item");
    check(!launcherNavRevealFocus(true, true, true), "a visible cursor moves instead of revealing");
    check(!launcherNavRevealFocus(false, false, true), "no item yet is left for ImGui to initialise");
    check(!launcherNavRevealFocus(false, true, false), "no direction does not reveal");
}

// One headless frame. `body` draws the window and reports which button is focused.
struct NavFrame {
    bool playFocused = false;
    bool settingsFocused = false;
    int playClicks = 0;
    bool popupOpen = false;
};

static bool gOpenAsk = false;

static NavFrame navFrame(const std::function<void(ImGuiIO&)>& before)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    before(io);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 400));
    ImGui::Begin("hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    NavFrame frame;
    if (gOpenAsk) {
        ImGui::OpenPopup("ask");
        gOpenAsk = false;
    }
    if (ImGui::Button("Play", ImVec2(160, 40))) frame.playClicks++;
    frame.playFocused = ImGui::IsItemFocused() && io.NavVisible;
    ImGui::SetCursorPos(ImVec2(20, 120));
    ImGui::Button("Settings", ImVec2(160, 40));
    frame.settingsFocused = ImGui::IsItemFocused() && io.NavVisible;
    if (ImGui::BeginPopupModal("ask", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        frame.popupOpen = true;
        // Same rule as the launcher popups: B closes, it does not confirm.
        if (ImGui::Button("Close", ImVec2(110, 38)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::Render();
    return frame;
}

static void press(ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, true);
}

static void release(ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, false);
}

// The launcher rewrites the queue before NewFrame. Do the same here.
static void pressDirection(ImGuiKey key)
{
    press(key);
    launcherApplyGamepadNav();
}

static void testImGuiNav()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(400, 400);
    // Keyboard navigation stays off, as in the launcher.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    const auto idle = [](ImGuiIO&) {};
    navFrame(idle);
    navFrame(idle);
    NavFrame frame = navFrame(idle);
    check(!frame.playFocused && !frame.settingsFocused, "focus stays hidden until the pad is used");

    // Arrow keys must not start keyboard navigation.
    frame = navFrame([](ImGuiIO&) { press(ImGuiKey_DownArrow); });
    release(ImGuiKey_DownArrow);
    frame = navFrame(idle);
    check(!frame.playFocused && !frame.settingsFocused, "arrow keys do not move gamepad focus");

    // The first direction reveals Play and does not move. The next one does.
    frame = navFrame([](ImGuiIO&) { pressDirection(ImGuiKey_GamepadDpadDown); });
    check(frame.playFocused, "the first d-pad press shows Play");
    check(io.NavVisible, "gamepad focus is visible");
    release(ImGuiKey_GamepadDpadDown);
    frame = navFrame(idle);
    check(frame.playFocused, "the first d-pad press does not leave Play");

    frame = navFrame([](ImGuiIO&) { pressDirection(ImGuiKey_GamepadDpadDown); });
    release(ImGuiKey_GamepadDpadDown);
    frame = navFrame(idle);
    check(frame.settingsFocused && !frame.playFocused, "d-pad down moves to Settings");

    frame = navFrame([](ImGuiIO&) { pressDirection(ImGuiKey_GamepadDpadUp); });
    release(ImGuiKey_GamepadDpadUp);
    frame = navFrame(idle);
    check(frame.playFocused, "d-pad up moves back to Play");

    // The left stick is rewritten into a d-pad press, and its analog is cleared
    // so the page does not scroll.
    LauncherStick stick;
    stick.down = 1.0f;
    const LauncherNavOverride moved = launcherNavOverride(stick, {});
    check(moved.apply && moved.dpad.down && !moved.dpad.up, "stick down becomes d-pad down");
    frame = navFrame([](ImGuiIO& input) {
        input.AddKeyEvent(ImGuiKey_GamepadDpadLeft, false);
        input.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
        input.AddKeyEvent(ImGuiKey_GamepadDpadUp, false);
        input.AddKeyEvent(ImGuiKey_GamepadDpadDown, false);
        input.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, true, 1.0f);
        input.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, false, 0.0f);
        input.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, false, 0.0f);
        input.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, false, 0.0f);
        launcherApplyGamepadNav();
    });
    check(ImGui::GetKeyData(ImGuiKey_GamepadLStickDown)->AnalogValue == 0.0f, "stick analog is cleared");
    check(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "stick down holds d-pad down");
    release(ImGuiKey_GamepadDpadDown);
    frame = navFrame(idle);
    check(frame.settingsFocused, "stick down moves focus like the d-pad");

    frame = navFrame([](ImGuiIO&) { pressDirection(ImGuiKey_GamepadDpadUp); });
    release(ImGuiKey_GamepadDpadUp);
    frame = navFrame(idle);
    check(frame.playFocused, "back on Play before A");

    frame = navFrame([](ImGuiIO&) { press(ImGuiKey_GamepadFaceDown); });
    check(frame.playClicks == 1, "A activates the focused Play button");
    release(ImGuiKey_GamepadFaceDown);
    navFrame(idle);

    // Mouse click still activates the button under the cursor.
    int clicks = 0;
    for (int step = 0; step < 3; ++step) {
        frame = navFrame([&](ImGuiIO& input) {
            input.AddMousePosEvent(40, 20);
            if (step == 1) input.AddMouseButtonEvent(0, true);
            if (step == 2) input.AddMouseButtonEvent(0, false);
        });
        clicks += frame.playClicks;
    }
    check(clicks == 1, "mouse click still presses Play");
    frame = navFrame(idle);
    check(!frame.playFocused, "mouse use hides the gamepad focus ring");

    // B closes a popup and does not take its confirm action.
    gOpenAsk = true;
    frame = navFrame(idle);
    if (!frame.popupOpen) frame = navFrame(idle);
    check(frame.popupOpen, "popup is open");
    frame = navFrame([](ImGuiIO&) { press(ImGuiKey_GamepadFaceRight); });
    check(frame.popupOpen, "B is handled while the popup is still open this frame");
    release(ImGuiKey_GamepadFaceRight);
    frame = navFrame(idle);
    check(!frame.popupOpen, "B closes the popup");

    ImGui::DestroyContext();
}

int main()
{
    testAxes();
    testDirection();
    testDialogBack();
    testImGuiNav();
    std::cout << "launcher_nav_test ok\n";
    return 0;
}
