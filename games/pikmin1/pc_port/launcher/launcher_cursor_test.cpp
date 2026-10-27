// Headless ImGui frames: hovering a combo, a combo entry or a radio button sets
// the hand cursor; disabled controls and plain text keep the arrow.
#include "launcher_cursor.h"

#include <cstdlib>
#include <functional>
#include <iostream>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// Draws one frame with the mouse at `mouse` and returns the cursor ImGui asked for.
static ImGuiMouseCursor frame(ImVec2 mouse, const std::function<void()>& body)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    io.AddMousePosEvent(mouse.x, mouse.y);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 400));
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    body();
    ImGui::End();
    ImGui::Render();
    return ImGui::GetMouseCursor();
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 400);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    // Layout: combo at (20,20), radio below it, plain text below that.
    ImVec2 comboAt, radioAt, textAt, disabledAt;
    bool comboOpen = false;
    ImVec2 entryAt;
    const auto body = [&](bool disabled) {
        return [&, disabled] {
            ImGui::SetCursorScreenPos(ImVec2(20, 20));
            comboAt = ImGui::GetCursorScreenPos();
            ImGui::SetNextItemWidth(200);
            comboOpen = handCombo("##combo", "One");
            if (comboOpen) {
                entryAt = ImGui::GetCursorScreenPos();
                handSelectable("One", true);
                handSelectable("Two", false);
                ImGui::EndCombo();
            }
            ImGui::SetCursorScreenPos(ImVec2(20, 200));
            radioAt = ImGui::GetCursorScreenPos();
            handRadioButton("Radio", false);
            ImGui::SetCursorScreenPos(ImVec2(20, 260));
            textAt = ImGui::GetCursorScreenPos();
            ImGui::TextUnformatted("Just text");
            ImGui::SetCursorScreenPos(ImVec2(20, 320));
            disabledAt = ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(disabled);
            ImGui::SetNextItemWidth(200);
            if (handCombo("##off", "Off")) ImGui::EndCombo();
            ImGui::EndDisabled();
        };
    };
    const auto enabled = body(false);
    const auto withDisabled = body(true);

    // Warm up so item rects exist.
    frame(ImVec2(390, 390), enabled);
    frame(ImVec2(390, 390), enabled);

    check(frame(ImVec2(390, 390), enabled) == ImGuiMouseCursor_Arrow, "empty space keeps the arrow");
    check(frame(ImVec2(comboAt.x + 30, comboAt.y + 5), enabled) == ImGuiMouseCursor_Hand, "closed combo shows the hand");
    frame(ImVec2(radioAt.x + 5, radioAt.y + 5), enabled);
    check(frame(ImVec2(radioAt.x + 5, radioAt.y + 5), enabled) == ImGuiMouseCursor_Hand, "radio button shows the hand");
    frame(ImVec2(textAt.x + 5, textAt.y + 5), enabled);
    check(frame(ImVec2(textAt.x + 5, textAt.y + 5), enabled) == ImGuiMouseCursor_Arrow, "plain text keeps the arrow");
    frame(ImVec2(disabledAt.x + 30, disabledAt.y + 5), withDisabled);
    check(frame(ImVec2(disabledAt.x + 30, disabledAt.y + 5), withDisabled) == ImGuiMouseCursor_Arrow,
          "disabled combo keeps the arrow");
    check(frame(ImVec2(disabledAt.x + 30, disabledAt.y + 5), enabled) == ImGuiMouseCursor_Hand,
          "the same combo enabled shows the hand");

    // Open the combo with a click, then hover its frame and an entry.
    const ImVec2 onCombo(comboAt.x + 30, comboAt.y + 5);
    frame(onCombo, enabled);
    io.AddMouseButtonEvent(0, true);
    frame(onCombo, enabled);
    io.AddMouseButtonEvent(0, false);
    frame(onCombo, enabled);
    check(comboOpen, "combo opened");
    check(frame(onCombo, enabled) == ImGuiMouseCursor_Hand, "open combo frame shows the hand");
    const ImVec2 onEntry(entryAt.x + 20, entryAt.y + ImGui::GetTextLineHeight() * 1.5f + 4);
    frame(onEntry, enabled);
    check(comboOpen, "combo still open");
    check(frame(onEntry, enabled) == ImGuiMouseCursor_Hand, "combo entry shows the hand");

    ImGui::DestroyContext();
    std::cout << "launcher_cursor_test passed\n";
    return 0;
}
