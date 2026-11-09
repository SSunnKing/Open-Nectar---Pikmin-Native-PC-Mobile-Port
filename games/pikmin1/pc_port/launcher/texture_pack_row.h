#pragma once

#include "imgui.h"
#include <algorithm>

namespace pikmin::launcher {

// Reserve the same row even when the action is absent (Off/legacy packs).
// Explicit positions avoid SameLine's text-baseline offset on InvisibleButton.
template <class Option, class Action>
void texturePackRow(float width, ImVec2 actionSize, Option option, Action action)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float radioHeight = ImGui::GetFrameHeight();
    const float height = std::max(radioHeight, actionSize.y);
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (height - radioHeight) * 0.5f));
    option();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - actionSize.x,
                                   origin.y + (height - actionSize.y) * 0.5f));
    action();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
}

} // namespace pikmin::launcher
