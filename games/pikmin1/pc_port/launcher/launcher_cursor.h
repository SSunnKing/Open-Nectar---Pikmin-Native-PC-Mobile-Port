#pragma once

#include "imgui.h"

namespace pikmin {
namespace launcher {

// Mano al pasar por encima de cualquier cosa que se pueda pulsar. ImGui ya
// devuelve false en IsItemHovered() para los controles desactivados, así que
// esos se quedan con la flecha.
inline void handCursorIfHovered()
{
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// BeginCombo con mano. Si el desplegable se abre, el último elemento pasa a
// ser la ventana emergente, así que el marco se mide antes de dibujarlo (y
// sin recortar: el recorte activo ya es el de la emergente).
inline bool handCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0)
{
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 frameEnd(at.x + ImGui::CalcItemWidth(), at.y + ImGui::GetFrameHeight());
    const bool open = ImGui::BeginCombo(id, preview, flags);
    if (open ? ImGui::IsMouseHoveringRect(at, frameEnd, false) : ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return open;
}

inline bool handSelectable(const char* label, bool selected)
{
    const bool clicked = ImGui::Selectable(label, selected);
    handCursorIfHovered();
    return clicked;
}

inline bool handRadioButton(const char* label, bool active)
{
    const bool clicked = ImGui::RadioButton(label, active);
    handCursorIfHovered();
    return clicked;
}

} // namespace launcher
} // namespace pikmin
