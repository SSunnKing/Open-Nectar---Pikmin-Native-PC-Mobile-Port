#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pikmin::launcher {

// Reparto vertical de las páginas del launcher en ventanas bajas. Todo es
// aritmética pura para poder probarlo sin ventana.

// Alto máximo del logo. Hasta 700 px de alto manda el ancho, como siempre; por
// debajo el logo encoge con la altura para dejar sitio a la página.
inline float logoHeightCap(float windowH)
{
    if (windowH >= 700.0f) return 1e9f;
    return std::max(56.0f, 118.0f - (700.0f - windowH) * 0.34f);
}

// Una página de alto fijo (Move Install): el hueco de arriba encoge hasta un
// mínimo y, si ni así cabe, la página pasa a tener scroll.
struct PageFit {
    float top = 0.0f;     // hueco entre las pestañas y la página
    bool scroll = false;
    float content = 0.0f; // alto total (hueco + página) para la región con scroll
};

inline PageFit fitPage(float content, float available, float preferredTop, float minTop)
{
    PageFit out;
    out.top = std::clamp(available - content, minTop, preferredTop);
    out.content = out.top + content;
    out.scroll = out.content > available + 1e-3f;
    return out;
}

// Huecos verticales de la página Games, de arriba abajo.
struct GamesSpacing {
    float top;     // pestañas → portadas
    float name;    // portada → nombre del juego
    float buttons; // nombre → primera fila de botones (el nombre mide 28 px)
    float update;  // última fila → Update
};

constexpr GamesSpacing kGamesNormal{ 24.0f, 22.0f, 38.0f, 32.0f };
constexpr GamesSpacing kGamesTight{ 12.0f, 12.0f, 34.0f, 16.0f };

struct GamesMetrics {
    float buttonH = 44.0f;
    float rowGap = 10.0f;
    float aspect = 1.40f;      // alto / ancho de la portada
    float comfortCoverH = 180.0f;
    float minCoverH = 120.0f;
};

struct GamesLayout {
    float columnW = 0.0f; // ancho de los botones bajo cada portada
    float gap = 0.0f;     // entre las dos columnas
    float coverW = 0.0f, coverH = 0.0f;
    GamesSpacing space = kGamesNormal;
    bool scroll = false;
    float content = 0.0f; // de las pestañas al final de la última fila
};

// Alto fijo debajo de las portadas: nombre, filas de botones y Update.
inline float gamesBelowCovers(const GamesSpacing& s, float rows, bool update, const GamesMetrics& m = {})
{
    const float rowsH = rows * m.buttonH + std::max(0.0f, rows - 1.0f) * m.rowGap;
    return s.name + s.buttons + rowsH + (update ? s.update + m.buttonH : 0.0f);
}

// Las portadas se quedan a su tamaño mientras caben; después encogen hasta un
// mínimo cómodo, luego se aprietan los huecos, luego encogen hasta el mínimo y
// solo entonces la página pasa a tener scroll. Los botones mantienen el ancho
// de su columna aunque la portada encoja. Cada paso continúa el anterior, así
// que al cambiar el alto de la ventana nada salta.
inline GamesLayout fitGames(float windowW, float available, float rows, bool update, GamesMetrics m = {})
{
    GamesLayout out;
    out.columnW = std::clamp(windowW * 0.19f, 190.0f, 280.0f);
    out.gap = out.columnW * 0.42f;
    const float preferredH = out.columnW * m.aspect;
    available = std::max(0.0f, available);

    auto fixed = [&](const GamesSpacing& s) { return s.top + gamesBelowCovers(s, rows, update, m); };
    const float normal = fixed(kGamesNormal), tight = fixed(kGamesTight);
    const float comfort = std::min(preferredH, m.comfortCoverH);
    if (available - normal >= comfort) {
        out.coverH = std::min(preferredH, available - normal);
        out.space = kGamesNormal;
    } else if (available - tight >= comfort) {
        // Huecos de normal a apretado, en proporción a lo que falta.
        const float k = (normal + comfort - available) / (normal - tight);
        auto lerp = [k](float a, float b) { return a + (b - a) * k; };
        out.space = { lerp(kGamesNormal.top, kGamesTight.top), lerp(kGamesNormal.name, kGamesTight.name),
                      lerp(kGamesNormal.buttons, kGamesTight.buttons), lerp(kGamesNormal.update, kGamesTight.update) };
        out.coverH = comfort;
    } else {
        out.space = kGamesTight;
        out.coverH = std::max(available - tight, std::min(comfort, m.minCoverH));
        out.scroll = available - tight < std::min(comfort, m.minCoverH) - 1e-3f;
    }
    out.coverW = out.coverH / m.aspect;
    out.content = fixed(out.space) + out.coverH;
    return out;
}

// Ajustes: filas arriba, caja de ayuda debajo. La caja cede primero (hasta dos
// líneas) para que las filas conserven unas tres opciones; si ni así caben, se
// oculta y las filas se quedan todo el alto.
struct SettingsPanelFit {
    float rowsH = 0.0f;
    float helpH = 0.0f; // 0: sin caja de ayuda
};

inline constexpr float kSettingsHelpFull = 104.0f; // cuatro líneas
inline constexpr float kSettingsHelpMin = 54.0f;   // dos líneas
inline constexpr float kSettingsRowsComfort = 250.0f;
inline constexpr float kSettingsRowsMin = 140.0f;
inline constexpr float kSettingsGap = 12.0f;

inline SettingsPanelFit fitSettingsPanel(float total)
{
    SettingsPanelFit out;
    if (total - kSettingsGap - kSettingsHelpMin < kSettingsRowsMin) {
        out.rowsH = std::max(1.0f, total);
        return out;
    }
    out.helpH = std::clamp(total - kSettingsGap - kSettingsRowsComfort, kSettingsHelpMin, kSettingsHelpFull);
    out.rowsH = total - kSettingsGap - out.helpH;
    return out;
}

// ── Anchos estrechos ───────────────────────────────────────────────────────

// Una fila de ajustes: etiqueta a la izquierda y control a la derecha. Si la
// etiqueta no cabe en una línea, en los paneles anchos se parte en dos al lado
// del control; si ni así cabe (o el panel es estrecho) va entera encima del
// control. Nunca se recorta ni queda debajo del control.
enum class SettingRowMode { Inline, Wrapped, Stacked };

struct SettingRowFit {
    SettingRowMode mode = SettingRowMode::Inline;
    float controlW = 0.0f;
    float labelMaxW = 0.0f; // ancho en el que se parte la etiqueta
};

inline constexpr float kSettingControlMaxW = 300.0f;
inline constexpr float kSettingLabelGap = 12.0f;
inline constexpr float kSettingStackBelowW = 520.0f; // ancho interior del panel
inline constexpr int kSettingLabelMaxLines = 2;

// `labelLines(width)` devuelve cuántas líneas ocupa la etiqueta partida a ese ancho.
template <class LabelLines>
SettingRowFit fitSettingRow(float innerW, float labelW, LabelLines labelLines)
{
    SettingRowFit out;
    innerW = std::max(1.0f, innerW);
    out.controlW = std::min(kSettingControlMaxW, innerW * 0.45f);
    out.labelMaxW = std::max(1.0f, innerW - out.controlW - kSettingLabelGap);
    if (labelW <= out.labelMaxW) return out;
    if (innerW >= kSettingStackBelowW && labelLines(out.labelMaxW) <= kSettingLabelMaxLines) {
        out.mode = SettingRowMode::Wrapped;
        return out;
    }
    out.mode = SettingRowMode::Stacked;
    out.controlW = std::min(kSettingControlMaxW, innerW);
    out.labelMaxW = innerW;
    return out;
}

// Botones de un diálogo: Cancelar a la izquierda, las acciones a la derecha.
// Si no caben en una fila, las acciones van arriba repartidas a partes iguales
// y Cancelar debajo; si ni así, una por fila a todo el ancho.
struct DialogButton {
    float x = 0.0f;
    float w = 0.0f;
    int row = 0;
};

inline constexpr float kDialogGap = 14.0f;
inline constexpr float kDialogCancelGap = 32.0f; // mínimo entre Cancelar y las acciones

// widths: anchos deseados de las acciones (de izquierda a derecha); cancelW 0 = sin Cancelar.
inline std::vector<DialogButton> fitDialogButtons(const std::vector<float>& widths, float cancelW, float innerW, int& rows)
{
    std::vector<DialogButton> out;
    float actions = 0.0f;
    for (float w : widths) actions += w;
    actions += kDialogGap * float(widths.empty() ? 0 : widths.size() - 1);
    const float oneRow = actions + (cancelW > 0.0f ? cancelW + kDialogCancelGap : 0.0f);
    if (oneRow <= innerW) {
        float x = innerW - actions;
        for (float w : widths) { out.push_back({ x, w, 0 }); x += w + kDialogGap; }
        if (cancelW > 0.0f) out.push_back({ 0.0f, cancelW, 0 });
        rows = 1;
        return out;
    }
    int row = 0;
    const float n = float(widths.size());
    const float even = (innerW - kDialogGap * (n - 1.0f)) / std::max(1.0f, n);
    float widest = 0.0f;
    for (float w : widths) widest = std::max(widest, w);
    if (widest <= even) {
        float x = 0.0f;
        for (std::size_t i = 0; i < widths.size(); ++i) { out.push_back({ x, even, 0 }); x += even + kDialogGap; }
        row = 1;
    } else {
        // Una por fila: la principal (la de más a la derecha) arriba.
        for (std::size_t i = 0; i < widths.size(); ++i) out.push_back({ 0.0f, innerW, int(widths.size() - 1 - i) });
        row = int(widths.size());
    }
    if (cancelW > 0.0f) out.push_back({ 0.0f, std::min(cancelW, innerW), row++ });
    rows = row;
    return out;
}

// ── Ventanas grandes ───────────────────────────────────────────────────────

// La interfaz se diseña para 1180x780. En ventanas mayores se dibuja más
// grande (hasta kDesktopUiScaleMax), en proporción al lado que menos crece,
// para no dejar una isla pequeña en medio de la pantalla.
inline constexpr float kDesignW = 1180.0f;
inline constexpr float kDesignH = 780.0f;
inline constexpr float kDesktopUiScaleMax = 1.75f;

inline float desktopUiScale(float windowW, float windowH)
{
    const float s = std::min(windowW / kDesignW, windowH / kDesignH);
    return std::max(1.0f, std::min(s, kDesktopUiScaleMax));
}

// Escala a la que se rasteriza algo (fuentes, botones): sube en pasos de 0.5
// para no rehacerlo en cada píxel de un cambio de tamaño.
inline float bakeScaleFor(float pixelScale)
{
    return std::ceil(std::max(1.0f, pixelScale) * 2.0f - 0.001f) * 0.5f;
}

// Densidad de las fuentes para una escala de interfaz y de pantalla.
inline float fontDensityFor(float uiScale, float framebufferScale)
{
    return bakeScaleFor(uiScale * framebufferScale);
}

// "Reset all" justo debajo de la lista de páginas (no en el fondo de la
// ventana) mientras la lista no tenga scroll.
inline float resetAllY(float listTop, float listContent, bool listScrolls, float bottom, float resetH, float gap = 16.0f)
{
    if (listScrolls) return bottom - resetH;
    return std::min(bottom - resetH, listTop + listContent + gap);
}

// Alto de un diálogo que se ajusta a su contenido: `content` es lo que mide
// lo de dentro (el fotograma anterior; < 0 si aún no se sabe), `chrome` el
// título, márgenes y botones.
inline float fitDialogHeight(float content, float chrome, float maxH, float minH)
{
    maxH = std::max(minH, maxH);
    if (content < 0.0f) return maxH;
    return std::max(minH, std::min(maxH, content + chrome));
}

// ── Pie ────────────────────────────────────────────────────────────────────

// La placa "Installed in: ..." ocupa los últimos 38 px de la ventana; las
// páginas acaban por encima con un pequeño hueco.
inline constexpr float kFooterTop = 38.0f;    // desde el borde inferior
inline constexpr float kFooterReserve = 48.0f;
inline float hubPageBottom(float windowH) { return windowH - kFooterReserve; }

// Alto del logo y comienzo del contenido (bajo las pestañas, si las hay).
inline constexpr float kLogoTop = 18.0f;
inline constexpr float kTabsH = 42.0f;

inline float hubLogoHeight(float windowW, float windowH, float logoAspect)
{
    return std::min(std::min(windowW * 0.56f, 700.0f) / logoAspect, logoHeightCap(windowH));
}

inline float hubContentTop(float windowW, float windowH, float logoAspect, bool tabs)
{
    return kLogoTop + hubLogoHeight(windowW, windowH, logoAspect) + 10.0f + (tabs ? kTabsH : 0.0f);
}

// ── Versión junto al logo (#327) ────────────────────────────────────────────

// La píldora "v1.3" va a la derecha del rótulo OPEN NECTAR, apoyada en su
// línea base. En logo.png las letras acaban al 72,5 % del alto.
inline constexpr float kLogoTextBaseline = 0.725f;

// Texto de la píldora: "v" + versión ("1.3" → "v1.3"). Una etiqueta que ya
// trae la "v" no la duplica; sin versión no hay píldora.
inline std::string versionBadgeLabel(const std::string& version)
{
    if (version.empty()) return {};
    if (version[0] == 'v' || version[0] == 'V') return "v" + version.substr(1);
    return "v" + version;
}

// Tamaño de letra de la píldora: crece con el logo, entre el de los pies de
// página (16) y el del texto normal (21).
inline float versionBadgeTextSize(float logoH) { return std::clamp(logoH * 0.16f, 16.0f, 21.0f); }

struct BadgeRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

// Coloca la píldora (`badgeW` x `badgeH`) tras el logo. `noticeLeft` es el
// borde izquierdo del aviso de actualización de arriba a la derecha (>=
// windowW si no hay aviso) y `noticeBottom` su borde inferior: si se tocan, la
// píldora baja hasta quedar justo debajo del aviso, al lado del logo.
inline BadgeRect placeVersionBadge(float windowW, float logoX, float logoY, float logoW, float logoH, float badgeW,
                                   float badgeH, float noticeLeft, float noticeBottom)
{
    constexpr float kMargin = 12.0f;
    constexpr float kNoticeGap = 6.0f;
    BadgeRect r;
    r.x0 = logoX + logoW + std::max(6.0f, logoH * 0.06f);
    r.x0 = std::min(r.x0, windowW - kMargin - badgeW);
    r.y0 = logoY + logoH * kLogoTextBaseline - badgeH;
    if (noticeLeft < windowW && r.x0 + badgeW > noticeLeft - kNoticeGap)
        r.y0 = std::max(r.y0, noticeBottom + kNoticeGap);
    r.x1 = r.x0 + badgeW;
    r.y1 = r.y0 + badgeH;
    return r;
}

} // namespace pikmin::launcher
