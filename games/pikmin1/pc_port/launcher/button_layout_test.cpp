// Pruebas del margen y los anchos estándar de los botones del launcher: cada
// texto real cabe con el margen completo en su tamaño (Fredoka a 1x y 2x, con
// la misma configuración que el launcher), el ajuste (encoger, "...") nunca se
// sale del rectángulo interior y los anchos son coherentes entre sí.
#include "button_layout.h"

#include "imgui.h"
#include "font_fredoka.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace pikmin::launcher::buttons;

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

struct Use {
    const char* label;
    ButtonSize size;
    float height;
};

// Cada botón de ancho fijo del launcher con su tamaño y alto reales.
const std::vector<Use>& launcherButtons()
{
    static const std::vector<Use> uses = {
        // Pestañas.
        { "Games", role::kTab, 42 }, { "Settings", role::kTab, 42 }, { "Move Install", role::kTab, 42 },
        // Bajo las portadas (Pikmin y Pikmin 2).
        { "Install", role::kGameWide, 44 }, { "HD textures", role::kGameWide, 44 },
        { "Map editor", role::kGameTool, 44 }, { "Model maker", role::kGameTool, 44 },
        { "HD textures", role::kGameTool, 44 }, { "Characters", role::kGameTool, 44 },
        { "Host Co-op", role::kGameTool, 44 }, { "Join Co-op", role::kGameTool, 44 },
        { "Host VS", role::kGameTool, 44 }, { "Join VS", role::kGameTool, 44 },
        { "Update", role::kGameWide, 44 }, { "Update to 10.10.10", role::kGameWide, 44 },
        // Settings: juego, lista lateral (grupos de Pikmin y Pikmin 2), reset.
        { "Pikmin", role::kSettingsGame, 38 }, { "Pikmin 2", role::kSettingsGame, 38 },
        { "Display", role::kSettingsGroup, 40 }, { "Graphics", role::kSettingsGroup, 40 },
        { "Controls", role::kSettingsGroup, 40 }, { "Camera", role::kSettingsGroup, 40 },
        { "Gameplay", role::kSettingsGroup, 40 }, { "Cheats", role::kSettingsGroup, 40 },
        { "Data", role::kSettingsGroup, 40 }, { "Achievements", role::kSettingsGroup, 40 },
        { "Resolution", role::kSettingsGroup, 40 }, { "Texture Packs", role::kSettingsGroup, 40 },
        { "HD Models", role::kSettingsGroup, 40 }, { "Keyboard", role::kSettingsGroup, 40 },
        { "Gamepad", role::kSettingsGroup, 40 },
        { "Reset all", role::kSettingsGroup, 44 }, { "Sure? Click", role::kSettingsGroup, 44 },
        { "Retry", role::kSmallAction, 44 },
        { "Install a pack from a .zip", role::kWide, 46 },
        { "Install a pack from a .zip", role::kWide, 34 },
        { "Install", role::kTexturePackDelete, 30 },
        { "Install", role::kTexturePackDelete, 34 },
        { "Working...", role::kTexturePackDelete, 30 },
        { "Working...", role::kTexturePackDelete, 34 },
        { "Delete", role::kTexturePackDelete, 30 },
        { "Delete", role::kTexturePackDelete, 34 },
        { "On", role::kToggle, 40 }, { "Off", role::kToggle, 40 },
        { "Replace", role::kSettingsRow, 40 }, { "Install", role::kSettingsRow, 40 },
        // Move Install.
        { "Browse", role::kSmallAction, 44 }, { "Move", role::kMove, 44 }, { "Moving...", role::kMove, 44 },
        // Aviso de versión, co-op, instalador.
        { "What's new", role::kAction, 40 },
        { "Copy code", role::kAction, 44 }, { "Paste", role::kAction, 44 },
        { "Connect", role::kAction, 44 }, { "Join", role::kAction, 44 }, { "Close", role::kSmallAction, 50 },
        { "Cancel", role::kAction, 50 }, { "Install", role::kAction, 50 },
    };
    return uses;
}

// Botones de los diálogos: el tamaño sale de sizeFor (alto 50).
const char* const kDialogLabels[] = { "OK", "Close", "Cancel", "Back", "Restart", "Retry", "Try again", "Not now",
                                      "Play now", "Finish now", "Update now", "Choose again", "Back to setup",
                                      "Update from a folder", "Use a downloaded folder", "Update to 10.10.10" };

struct Atlas {
    ImGuiContext* context = nullptr;
    ImFont* font = nullptr;
    explicit Atlas(float density)
    {
        context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig cfg;
        cfg.RasterizerDensity = density;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 3;
        font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontFredoka), int(kFontFredokaSize), 21.0f, &cfg);
        io.Fonts->Build();
    }
    ~Atlas() { ImGui::DestroyContext(context); }
    MeasureFn measure() const
    {
        ImFont* f = font;
        return [f](const std::string& text, float size) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x; };
    }
};

// Medida sintética: cada carácter mide 0.5 * size (para probar el ajuste).
float fakeMeasure(const std::string& text, float size) { return float(text.size()) * size * 0.5f; }

void testWidths()
{
    float previous = 0;
    for (ButtonSize size : kAllSizes) {
        const float w = buttonWidth(size);
        check(w > previous, "widths grow with the size");
        check(std::floor(w) == w, "widths are whole logical points (crisp at 1x and 2x)");
        previous = w;
    }
    check(buttonWidth(ButtonSize::Small) == 120 && buttonWidth(ButtonSize::Medium) == 144 &&
              buttonWidth(ButtonSize::Large) == 190 && buttonWidth(ButtonSize::ExtraLarge) == 300,
          "standard widths 120 / 144 / 190 / 300");
    // El margen es el mismo en todos los tamaños: solo cambia el ancho útil.
    for (float h : { 38.0f, 40.0f, 42.0f, 44.0f, 46.0f, 50.0f }) {
        const float inset = innerTextRect(buttonWidth(ButtonSize::Small), h, kLabelSize).x0;
        for (ButtonSize size : kAllSizes) {
            const Rect r = innerTextRect(buttonWidth(size), h, kLabelSize);
            check(std::fabs(r.x0 - inset) < 1e-4f, "same horizontal inset for every size");
            check(std::fabs((buttonWidth(size) - r.x1) - r.x0) < 1e-4f, "inset is symmetric");
            check(r.x0 >= kButtonBorder + kButtonPadX, "inset clears the border plus padding");
            check(r.height() >= kLabelSize, "label height fits every button height in use");
        }
    }
    // Botones compactos (barra de ajustes encogida, 30 px mín.): el relleno
    // vertical cede pero el texto de 21 px sigue sin pisar el borde.
    for (float h : { 30.0f, 32.0f, 34.0f, 36.0f }) {
        const Rect r = innerTextRect(buttonWidth(ButtonSize::Large), h, kLabelSize);
        check(r.y0 >= kButtonBorder && h - r.y1 >= kButtonBorder, "compact: text stays off the border");
        check(r.height() >= kLabelSize - 1e-4f, "compact: 21 px label still fits");
        check(r.x0 >= kButtonBorder + kButtonPadX, "compact: horizontal padding kept");
    }
    check(verticalInset(44, kLabelSize) == kButtonBorder + kButtonPadY, "full vertical padding when there is room");

    // La curva del extremo se nota: más baja la píldora, más se mete.
    check(capInset(38, 21) > capInset(50, 21) && capInset(50, 21) > 0, "cap inset follows the pill curve");
    check(capInset(44, 0) == 0, "no cap inset for an empty line");
    // Filas de dos: mismos tamaños.
    check(role::kToggle == role::kSettingsRow, "On/Off and Replace share a size");
    // Las dos rejillas bajo las portadas no se tocan ni a 960 ni a la medida
    // por defecto, y caben en la ventana.
    for (float window : { 960.0f, 1180.0f, 1600.0f }) {
        const float cardW = std::fmin(std::fmax(window * 0.19f, 190.0f), 280.0f);
        const float gap = gameCardGap(cardW);
        const float centres = cardW + gap;
        check(centres - gameGridWidth() >= kGameGridGutter - 1e-3f, "game grids keep a gutter at " + std::to_string(int(window)));
        const float startX = (window - (2.0f * cardW + gap)) * 0.5f;
        check(startX + cardW * 0.5f - gameGridWidth() * 0.5f >= 0, "game grid inside the window at " + std::to_string(int(window)));
    }
}

void testLabels(float density)
{
    Atlas atlas(density);
    const MeasureFn measure = atlas.measure();
    const std::string at = " at " + std::to_string(int(density)) + "x";
    for (const Use& use : launcherButtons()) {
        const float w = buttonWidth(use.size);
        check(labelFits(use.label, w, use.height, kLabelSize, measure),
              std::string("'") + use.label + "' fits its size with full padding" + at);
        const FittedLabel fit = fitLabel(use.label, w, use.height, measure);
        check(!fit.shrunk && !fit.truncated && fit.text == use.label, std::string("'") + use.label + "' needs no fallback" + at);
    }
    for (const char* label : kDialogLabels) {
        const ButtonSize size = sizeFor(label, 50, measure, role::kAction);
        check(labelFits(label, buttonWidth(size), 50, kLabelSize, measure), std::string("dialog '") + label + "' fits" + at);
    }
    // Aviso de versión: píldora a medida con el mismo margen.
    const std::string notice = "Open Nectar 10.10.10 is available";
    check(labelFits(notice, fitWidth(notice, 40, 19, measure), 40, 19, measure), "release notice fits" + at);
}

void testFallback()
{
    // Cabe tal cual.
    FittedLabel fit = fitLabel("Abcd", 120, 44, fakeMeasure);
    check(fit.text == "Abcd" && !fit.shrunk && !fit.truncated && fit.size == kLabelSize, "short label untouched");

    // Un poco largo: encoge en pasos, sin recortar.
    const float room = innerTextRect(120, 44, kLabelSize).width();
    const int chars = int(room / (kLabelSize * 0.5f)) + 1; // uno más de lo que cabe a 21
    fit = fitLabel(std::string(size_t(chars), 'x'), 120, 44, fakeMeasure);
    check(fit.shrunk && !fit.truncated && fit.size < kLabelSize && fit.size >= kLabelFloor, "slightly long label shrinks");
    check(std::fmod(kLabelSize - fit.size, kLabelStep) == 0, "shrinks in whole steps");
    check(fakeMeasure(fit.text, fit.size) <= innerTextRect(120, 44, fit.size).width(), "shrunk label inside inner rect");

    // Muy largo: suelo y "..." dentro del rectángulo interior.
    fit = fitLabel("A very long label that cannot fit", 120, 44, fakeMeasure);
    check(fit.shrunk && fit.truncated && fit.size == kLabelFloor, "long label hits the floor and truncates");
    check(fit.text.size() > 3 && fit.text.compare(fit.text.size() - 3, 3, kEllipsis) == 0, "truncated label ends with ...");
    check(fakeMeasure(fit.text, fit.size) <= innerTextRect(120, 44, fit.size).width(), "truncated label inside inner rect");
    check(fit.text.find(" ...") == std::string::npos, "no space before the ellipsis");

    // UTF-8: no parte un carácter.
    fit = fitLabel("Ñandú ñandú ñandú ñandú ñandú", 120, 44, fakeMeasure);
    const std::string body = fit.text.substr(0, fit.text.size() - 3);
    check(fit.truncated && (body.empty() || (static_cast<unsigned char>(body.back()) & 0xC0) != 0xC0), "truncation keeps UTF-8 whole");

    // Imposible: ni "..." cabe, queda vacío (y el recorte al dibujar).
    fit = fitLabel("Hello", 40, 44, fakeMeasure);
    check(fit.truncated && fakeMeasure(fit.text, fit.size) <= std::fmax(0.0f, innerTextRect(40, 44, fit.size).width()),
          "never wider than the inner rect");

    // Con Fredoka real: "Model maker" en un botón de 110 encoge o recorta, y
    // siempre queda dentro.
    Atlas atlas(1.0f);
    const MeasureFn measure = atlas.measure();
    for (float w : { 136.0f, 120.0f, 100.0f, 80.0f }) {
        fit = fitLabel("Model maker", w, 44, measure);
        check(measure(fit.text, fit.size) <= innerTextRect(w, 44, fit.size).width() + 1e-3f,
              "Fredoka fallback stays inside at width " + std::to_string(int(w)));
    }

    // sizeFor elige el menor que cabe, y respeta el mínimo.
    check(sizeFor("OK", 50, measure) == ButtonSize::Small, "sizeFor OK -> Small");
    check(sizeFor("OK", 50, measure, ButtonSize::Medium) == ButtonSize::Medium, "sizeFor respects minimum");
    check(sizeFor("Use a downloaded folder", 50, measure) == ButtonSize::ExtraLarge, "sizeFor long -> ExtraLarge");
    // Muy bajo: encoge, pero no recorta lo que cabe a lo ancho.
    fit = fitLabel("Graphics", 190, 18, fakeMeasure);
    check(!fit.truncated && fit.text == "Graphics" && fit.size == kLabelFloor, "height-only squeeze never truncates");
}

} // namespace

int main()
{
    testWidths();
    testLabels(1.0f);
    testLabels(2.0f);
    testFallback();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("button_layout_test: ok");
    return 0;
}
