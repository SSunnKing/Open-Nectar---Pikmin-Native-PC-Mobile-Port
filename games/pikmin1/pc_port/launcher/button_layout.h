// Medidas comunes de los botones del launcher: anchos estándar, márgenes del
// texto dentro del borde y el ajuste del texto cuando no cabe. Sin ImGui: la
// medida del texto llega como función, así se prueba sin ventana.
#pragma once

#include <functional>
#include <string>

namespace pikmin::launcher::buttons {

// Anchos estándar (puntos lógicos; ImGui los pasa a píxeles con la escala de
// la pantalla, y el dewdrop se hornea a esa escala).
enum class ButtonSize { Small, Medium, Large, ExtraLarge };
constexpr int kButtonSizeCount = 4;
constexpr ButtonSize kAllSizes[kButtonSizeCount] = { ButtonSize::Small, ButtonSize::Medium, ButtonSize::Large,
                                                     ButtonSize::ExtraLarge };

constexpr float buttonWidth(ButtonSize size)
{
    switch (size) {
    case ButtonSize::Small: return 120.0f;
    case ButtonSize::Medium: return 144.0f;
    case ButtonSize::Large: return 190.0f;
    case ButtonSize::ExtraLarge: return 300.0f;
    }
    return 144.0f;
}

// Hueco entre dos botones de una misma fila o rejilla.
constexpr float kButtonGap = 12.0f;

// Margen del texto. Se mide desde dentro del borde (el más grueso, 3 px al
// estar seleccionado); en los extremos redondos se suma lo que la curva
// invade a la altura del texto (capInset).
constexpr float kButtonBorder = 3.0f;
constexpr float kButtonPadX = 12.0f;
constexpr float kButtonPadY = 5.0f;

// Texto de los botones y su suelo al encoger.
constexpr float kLabelSize = 21.0f;
constexpr float kLabelFloor = 15.0f;
constexpr float kLabelStep = 1.0f;
inline const char* const kEllipsis = "...";

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
};

// Cuánto se mete la curva del extremo de una píldora de alto `height` en la
// banda horizontal que ocupa un texto de alto `textHeight`.
float capInset(float height, float textHeight);
// Margen horizontal total a cada lado: borde + kButtonPadX + capInset.
float horizontalInset(float height, float textHeight);
// Margen vertical: borde + kButtonPadY. En botones compactos (la barra de
// ajustes encogida) el relleno cede hasta 0, pero el texto nunca pisa el borde.
float verticalInset(float height, float textHeight);
// Rectángulo donde puede ir el texto, relativo a la esquina del botón.
Rect innerTextRect(float width, float height, float textHeight);

// Ancho del texto a un tamaño de letra dado. El alto de línea se toma igual
// al tamaño (Fredoka en ImGui).
using MeasureFn = std::function<float(const std::string& text, float size)>;

struct FittedLabel {
    std::string text;
    float size = kLabelSize;
    bool shrunk = false;
    bool truncated = false;
};

// Encaja `label` en un botón de width x height: primero encoge la letra en
// pasos hasta el suelo y, si aún no cabe, recorta con "...". Quien dibuja
// además recorta al rectángulo interior por si acaso.
FittedLabel fitLabel(const std::string& label, float width, float height, const MeasureFn& measure,
                     float baseSize = kLabelSize, float floorSize = kLabelFloor, float step = kLabelStep);

// True si `label` cabe en width x height a `size` con el margen completo.
bool labelFits(const std::string& label, float width, float height, float size, const MeasureFn& measure);

// El tamaño estándar más pequeño (no menor que `minimum`) donde `label` cabe
// sin encoger; ExtraLarge si no cabe en ninguno (y entonces actúa el ajuste).
ButtonSize sizeFor(const std::string& label, float height, const MeasureFn& measure,
                   ButtonSize minimum = ButtonSize::Small, float baseSize = kLabelSize);

// Ancho de una píldora que se ajusta a su texto (aviso de versión): texto +
// el mismo margen que los botones.
float fitWidth(const std::string& label, float height, float size, const MeasureFn& measure);

// Página de juegos: ancho de la rejilla de dos botones bajo cada portada y
// hueco entre portadas para que las dos rejillas no se toquen.
float gameGridWidth();
float gameCardGap(float cardW);
constexpr float kGameGridGutter = 28.0f;

// Qué tamaño usa cada grupo de botones del launcher (los comparte la prueba).
namespace role {
constexpr ButtonSize kTab = ButtonSize::Medium;           // Games / Settings / Move Install
constexpr ButtonSize kGameTool = ButtonSize::Medium;      // rejilla bajo la carátula, co-op
constexpr ButtonSize kGameWide = ButtonSize::Large;       // Install, HD textures solo, Update
constexpr ButtonSize kSettingsGame = ButtonSize::Medium;  // Pikmin / Pikmin 2
constexpr ButtonSize kSettingsGroup = ButtonSize::Large;  // lista lateral y Reset all
constexpr ButtonSize kTexturePackDelete = ButtonSize::Small; // full Delete label, even in the compact panel
constexpr ButtonSize kSettingsRow = ButtonSize::Medium;   // Replace / Install (fila con On/Off)
constexpr ButtonSize kToggle = ButtonSize::Medium;        // On / Off
constexpr ButtonSize kWide = ButtonSize::ExtraLarge;      // Install a pack from a .zip
constexpr ButtonSize kSmallAction = ButtonSize::Small;    // Browse, Retry, Close (coop)
constexpr ButtonSize kAction = ButtonSize::Medium;        // Cancel / Install, Paste / Connect, Copy code, What's new, diálogos
constexpr ButtonSize kMove = ButtonSize::Large;           // Move / Moving...
} // namespace role

} // namespace pikmin::launcher::buttons
