// Implementacion GX de la capa de compatibilidad del menu F1 para Pikmin 2
// (ver pc_settings_p2_shim.h). Replica lo que hace DGXGraphics en Pikmin 1:
// proyeccion ortografica, quads con color por vertice y un quad texturizado
// por caracter.
#include "settings/pc_settings_p2_shim.h"
#include "settings/pc_settings_p2d.h"
#include "settings/pc_ui_font.h"

#include "gl/pc_gfx.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Avance entre letras: mas estrecho que la celda del atlas (12 px) para
// parecerse al ancho de la fuente de Pikmin 1.
constexpr int kAdvance = 10;

DGXGraphics sGfx;
PcP2SettingsSystem sSystem = { &sGfx };
Texture sFontTexture;

// Lote del frame: estatico para no usar el heap del juego (el operator new
// global del port va a JKRHeap). El texto con contorno dibuja cada cadena
// 16 veces, de ahi el tamano.
constexpr int kMaxVerts = 240000;
PcUiVertex sVerts[kMaxVerts];
int sVertCount = 0;

void push(float x, float y, float u, float v, const Colour& c, float tex)
{
    if (sVertCount >= kMaxVerts)
        return;
    PcUiVertex& o = sVerts[sVertCount++];
    o.x = x; o.y = y; o.u = u; o.v = v;
    o.r = c.r; o.g = c.g; o.b = c.b; o.a = c.a;
    o.tex = tex;
}

void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
          const Colour& top, const Colour& bottom, float tex)
{
    push(x0, y0, u0, v0, top, tex);
    push(x1, y0, u1, v0, top, tex);
    push(x1, y1, u1, v1, bottom, tex);
    push(x0, y0, u0, v0, top, tex);
    push(x1, y1, u1, v1, bottom, tex);
    push(x0, y1, u0, v1, bottom, tex);
}

} // namespace

PcP2SettingsSystem* gsys = &sSystem;

int Font::stringWidth(const char* text) const { return (int)std::strlen(text) * kAdvance; }

Texture* PcP2SettingsSystem::loadTexture(const char*, bool) { return &sFontTexture; }

DGXGraphics::DGXGraphics()
    : mScreenWidth(640)
    , mScreenHeight(480)
{
}

void DGXGraphics::setColour(const Colour& colour, bool setAux)
{
    mPrimaryColour = colour;
    if (setAux)
        mAuxiliaryColour = colour;
}

void DGXGraphics::setAuxColour(const Colour& colour) { mAuxiliaryColour = colour; }

void DGXGraphics::setOrthogonal(Mtx44, const RectArea&) { }

void DGXGraphics::fillRectangle(const RectArea& rect)
{
    quad((float)rect.mMinX, (float)rect.mMinY, (float)rect.mMaxX, (float)rect.mMaxY, 0, 0, 0, 0,
         mPrimaryColour, mAuxiliaryColour, 0.0f);
}

void DGXGraphics::texturePrintf(Font*, int x, int y, const char* format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    const float du = (float)kPcUiFontCellW / kPcUiFontW;
    const float dv = (float)kPcUiFontCellH / kPcUiFontH;
    float px       = (float)x;
    for (const char* p = buf; *p; ++p) {
        int c = (unsigned char)*p;
        if (c < kPcUiFontFirst || c > kPcUiFontLast)
            c = '?';
        const int idx = c - kPcUiFontFirst;
        const float u = (idx % kPcUiFontCols) * du;
        const float v = (idx / kPcUiFontCols) * dv;
        if (c != ' ')
            quad(px - 1.0f, (float)y, px - 1.0f + kPcUiFontCellW, (float)y + kPcUiFontCellH, u, v, u + du, v + dv,
                 mPrimaryColour, mAuxiliaryColour, 1.0f);
        px += kAdvance;
    }
}

// Texto a tamano variable para la pagina F1 (fw = ancho nominal, como en el
// P2D de Pikmin 1): el glifo se escala a fw/12 y el avance es 0,78*fw, mas
// cerca de la fuente proporcional del original que el avance fijo de arriba.
namespace {
constexpr float kScaledAdvance = 0.78f;

void scaledGlyphs(float x, float y, const char* text, int fw, const Colour& c)
{
    const float du = (float)kPcUiFontCellW / kPcUiFontW;
    const float dv = (float)kPcUiFontCellH / kPcUiFontH;
    const float s  = fw / 12.0f;
    const float gw = kPcUiFontCellW * s, gh = kPcUiFontCellH * s;
    float px       = x;
    for (const char* p = text; *p; ++p) {
        int ch = (unsigned char)*p;
        if (ch < kPcUiFontFirst || ch > kPcUiFontLast)
            ch = '?';
        const int idx = ch - kPcUiFontFirst;
        const float u = (idx % kPcUiFontCols) * du;
        const float v = (idx / kPcUiFontCols) * dv;
        if (ch != ' ')
            quad(px - s, y, px - s + gw, y + gh, u, v, u + du, v + dv, c, c, 1.0f);
        px += fw * kScaledAdvance;
    }
}
} // namespace

int pc_p2_text_width(const char* text, int fw) { return (int)(std::strlen(text) * fw * kScaledAdvance + 0.5f); }

void pc_p2_text(int x, int y, const char* text, Colour main, Colour shadow, int fw)
{
    for (int ox = -1; ox <= 1; ox++)
        for (int oy = -1; oy <= 1; oy++)
            if (ox || oy)
                scaledGlyphs((float)(x + ox), (float)(y + oy), text, fw, shadow);
    scaledGlyphs((float)x, (float)y, text, fw, main);
}

void pc_settings_p2_flush(void)
{
    if (sVertCount > 0)
        pc_gfx_ui_draw(sVerts, sVertCount, sGfx.mScreenWidth, sGfx.mScreenHeight, kPcUiFont, kPcUiFontW, kPcUiFontH);
    sVertCount = 0;
}

// El sistema 2D nativo (P2D) de Pikmin 1 no existe en Pikmin 2: el menu usa
// siempre el camino DGXGraphics de arriba.
void pc_settings_p2d_init() { }
bool pc_settings_p2d_active() { return false; }
int pc_settings_p2d_text_width(const char* text, int fontWidth) { return (int)std::strlen(text) * fontWidth; }
void pc_settings_p2d_text(int, int, const char*, Colour, int, int) { }
void pc_settings_p2d_plate(int, int, int, int, int) { }
void pc_settings_p2d_clear() { }
void pc_settings_p2d_image(int, int, int, int, Texture*, float, float, Colour, float) { }
PcSettingsP2DFrame::PcSettingsP2DFrame(int, int) { }
PcSettingsP2DFrame::~PcSettingsP2DFrame() { }

int GameStat::freePikis = 0;
