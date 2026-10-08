// Teclas en los menús del juego (ver pc_keycaps.h). Cada etiqueta se dibuja
// una vez en una textura RGBA (tecla redondeada, borde oscuro, nombre) con la
// fuente bitmap del port, y se pinta con la capa final de pc_gfx, la misma que
// usa la interfaz táctil.
#include "pc_keycaps.h"

#include "gl/pc_gfx.h"
#include "pc_window.h"
#include "settings/pc_glass_menu.h"
#include "settings/pc_settings.h"
#include "settings/pc_ui_font.h"
#if PIKI_PC_TOUCH
#include "touch/pc_touch.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Mark {
    char tag;
    float cx, cy, size, alpha;
};
std::vector<Mark> sMarks;

struct Cap {
    unsigned tex;
    int w, h;
};
std::unordered_map<std::string, Cap> sCaps;

// Escala del texto dentro de la textura: la fuente es de 12x16 por celda.
constexpr int kScale   = 2;
constexpr int kAdvance = 9; // avance por letra en píxeles de la fuente (celda de 12)
constexpr int kCapH    = 16 * kScale + 12;

Cap makeCap(const char* label)
{
    // Ancho fijo: cuadrada para una letra y 1,6 veces el alto para nombres
    // más largos, que encogen el texto para caber en vez de ensanchar la
    // tecla y tapar lo que haya al lado.
    const int len   = std::max(1, (int)std::strlen(label));
    const int h     = kCapH;
    const int w     = len == 1 ? kCapH : (int)(kCapH * 1.6f);
    const float sc  = std::min((float)kScale, (float)(w - 2 * 6) / (len * kAdvance));
    const int textW = (int)(len * kAdvance * sc);
    std::vector<unsigned char> px((size_t)w * h * 4, 0);
    auto blend = [&](int x, int y, int r, int g, int b, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
        unsigned char* p = &px[((size_t)y * w + x) * 4];
        const float ia   = 1.0f - a;
        p[0] = (unsigned char)(r * a + p[0] * ia);
        p[1] = (unsigned char)(g * a + p[1] * ia);
        p[2] = (unsigned char)(b * a + p[2] * ia);
        p[3] = (unsigned char)std::min(255.0f, 255.0f * a + p[3] * ia);
    };
    // Tecla: rectángulo redondeado con borde oscuro, cara clara y un canto
    // inferior más oscuro (relieve), como una tecla vista desde arriba.
    const float rad = 9.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float qx = std::max(0.0f, std::max(rad - (x + 0.5f), (x + 0.5f) - (w - rad)));
            const float qy = std::max(0.0f, std::max(rad - (y + 0.5f), (y + 0.5f) - (h - rad)));
            const float d  = std::sqrt(qx * qx + qy * qy); // 0 dentro, rad en el borde
            const float outside = d - rad;                // >0 fuera
            const float cover   = std::clamp(0.5f - outside, 0.0f, 1.0f);
            if (cover <= 0.0f) continue;
            const bool edge   = outside > -2.5f;
            const bool bottom = y > h - 7;
            int r = 238, g = 236, b = 228;
            if (bottom) { r = 168; g = 166; b = 160; }
            if (edge) { r = 34; g = 32; b = 40; }
            blend(x, y, r, g, b, cover);
        }
    }
    // Texto centrado, gris muy oscuro, sobre la cara de la tecla, a la
    // escala `sc` (muestreo del glifo por vecino más cercano).
    const int glyphH = (int)(16 * sc), glyphW = (int)(kPcUiFontCellW * sc);
    float pen        = (w - textW) * 0.5f;
    const int top    = (h - 7 - glyphH) / 2 + 1;
    for (const char* p = label; *p; ++p, pen += kAdvance * sc) {
        int c = (unsigned char)*p;
        if (c < kPcUiFontFirst || c > kPcUiFontLast) c = '?';
        const int idx = c - kPcUiFontFirst;
        const int gx  = (idx % kPcUiFontCols) * kPcUiFontCellW;
        const int gy  = (idx / kPcUiFontCols) * kPcUiFontCellH;
        for (int y = 0; y < glyphH; y++) {
            for (int x = 0; x < glyphW; x++) {
                const int sx  = std::min(kPcUiFontCellW - 1, (int)(x / sc));
                const int sy  = std::min(kPcUiFontCellH - 1, (int)(y / sc));
                const float a = kPcUiFont[(gy + sy) * kPcUiFontW + gx + sx] / 255.0f;
                blend((int)(pen - 1.5f * sc) + x, top + y, 32, 30, 38, a);
            }
        }
    }
    Cap cap;
    cap.tex = pc_gfx_overlay_texture_create(w, h, px.data());
    cap.w   = w;
    cap.h   = h;
    return cap;
}

const Cap* capFor(const char* label)
{
    auto it = sCaps.find(label);
    if (it != sCaps.end()) return &it->second;
    if (sCaps.size() > 64) return nullptr; // nombres de teclas: nunca tantos
    return &sCaps.emplace(label, makeCap(label)).first->second;
}

} // namespace

char pc_keycaps_tag_for_texture(const char* texName)
{
    if (!texName) return 0;
    const char* name = texName;
    for (const char* p = texName; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    // Los dibujos completos de botón de GameCube. Las siluetas *_base no: son
    // los botones del dibujo del mando en la página de controles, que se
    // queda como esquema de GameCube (sus ventanas ya llevan la tecla), y L y
    // R comparten silueta (lr_base), así que no se pueden distinguir.
    static const struct {
        const char* file;
        char tag;
    } kEntries[] = {
        { "a_40.bti", 'a' },  { "a_btn.bti", 'a' }, { "b_btn.bti", 'b' },  { "x_btn.bti", 'x' },  { "y_btn.bti", 'y' },
        { "z_btn.bti", 'z' }, { "l_btn.bti", 'l' }, { "r_btn.bti", 'r' },  { "c_btn.bti", 'c' },  { "3d_btn.bti", 's' },
        { "st_btn.bti", 'p' },
    };
    for (const auto& e : kEntries)
        if (std::strcmp(name, e.file) == 0) return e.tag;
    return 0;
}

bool pc_keycaps_wanted(void)
{
#if PIKI_PC_TOUCH
    if (pc_touch_visible()) return false; // el táctil pone los suyos
#endif
    return pc_window_prompt_uses_gamepad() == 0;
}

void pc_keycaps_mark(char tag, float cx, float cy, float size, float alpha)
{
    if (size <= 0.0f || alpha <= 0.0f || sMarks.size() >= 32) return;
    // Un pane se dibuja a veces por duplicado (sombra, máscara de color).
    for (const Mark& m : sMarks)
        if (m.tag == tag && std::fabs(m.cx - cx) < size * 0.25f && std::fabs(m.cy - cy) < size * 0.25f) return;
    sMarks.push_back({ tag, cx, cy, size, alpha });
}

void pc_keycaps_draw(void)
{
    if (sMarks.empty()) return;
    // Con F1 o el menú de cristal abiertos, el juego queda debajo: sus teclas
    // no deben pintarse encima del menú.
    if (pc_settings_menu_is_open() || pc_glass_menu_active()) {
        sMarks.clear();
        return;
    }
    pc_gfx_overlay_begin();
    for (const Mark& m : sMarks) {
        char label[48];
        pc_window_key_prompt_label(m.tag, label, sizeof(label));
        if (!label[0]) continue;
        const Cap* cap = capFor(label);
        if (!cap || !cap->tex) continue;
        // Alto como el dibujo original (algo mayor, para tapar la letra que
        // algunas pantallas escriben encima de la silueta); el ancho, el que
        // pida el nombre.
        const float h = m.size * 1.2f;
        const float w = h * cap->w / cap->h;
        pc_gfx_overlay_sprite(cap->tex, m.cx - w * 0.5f, m.cy - h * 0.5f, w, h, 1.0f, 1.0f, 1.0f, m.alpha, 0.0f);
    }
    pc_gfx_overlay_end();
    sMarks.clear();
}
