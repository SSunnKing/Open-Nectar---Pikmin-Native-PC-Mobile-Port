// Implementacion GX de la capa de compatibilidad del menu F1 para Pikmin 2
// (ver pc_settings_p2_shim.h). Replica lo que hace DGXGraphics en Pikmin 1:
// proyeccion ortografica, quads con color por vertice y un quad texturizado
// por caracter.
#include "settings/pc_settings_p2_shim.h"
#include "settings/pc_settings_p2d.h"
#include "settings/pc_ui_font.h"
#include "settings/pc_p2_glass_assets.h"

#include <algorithm>
#include <cmath>

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



// Capa "de cristal" del menú (la API de pc_settings_p2d.h): la misma que en
// Pikmin 1 dibuja su P2D, aquí con texturas y fuente de Pikmin 2 sacadas a un
// atlas (pc_p2_glass_assets). Va en un lote propio que se dibuja después del
// de DGXGraphics, igual que en P1 las placas P2D van sobre lo demás.
namespace {
PcUiVertex sGlassVerts[60000];
int sGlassCount = 0;
int sContentLeft = 0, sContentRight = 0;
const PcGlassAtlas* sAtlas = nullptr;

void gpush(float x, float y, float u, float v, const Colour& c)
{
    if (sGlassCount >= (int)(sizeof(sGlassVerts) / sizeof(sGlassVerts[0])))
        return;
    PcUiVertex& o = sGlassVerts[sGlassCount++];
    o.x = x; o.y = y; o.u = u; o.v = v;
    o.r = c.r; o.g = c.g; o.b = c.b; o.a = c.a;
    o.tex = 2.0f;
}

// Quad con UV del atlas en píxeles; top/bottom: color de arriba y de abajo.
void gquad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const Colour& top,
           const Colour& bottom)
{
    const float iw = 1.0f / sAtlas->w, ih = 1.0f / sAtlas->h;
    u0 *= iw; u1 *= iw; v0 *= ih; v1 *= ih;
    gpush(x0, y0, u0, v0, top);
    gpush(x1, y0, u1, v0, top);
    gpush(x1, y1, u1, v1, bottom);
    gpush(x0, y0, u0, v0, top);
    gpush(x1, y1, u1, v1, bottom);
    gpush(x0, y1, u0, v1, bottom);
}

// Nine slices: las esquinas redondeadas no se estiran. srcEdge: borde en la
// textura; dstEdge: en pantalla.
void nineSlice(const PcGlassRect& r, int x, int y, int w, int h, float srcEdge, int dstEdge, const Colour& top,
               const Colour& bottom)
{
    const int ex = std::min(dstEdge, w / 2), ey = std::min(dstEdge, h / 2);
    const float xs[] = { (float)x, (float)(x + ex), (float)(x + w - ex), (float)(x + w) };
    const float ys[] = { (float)y, (float)(y + ey), (float)(y + h - ey), (float)(y + h) };
    const float us[] = { (float)r.x, r.x + srcEdge, r.x + r.w - srcEdge, (float)(r.x + r.w) };
    const float vs[] = { (float)r.y, r.y + srcEdge, r.y + r.h - srcEdge, (float)(r.y + r.h) };
    for (int row = 0; row < 3; ++row) {
        // Degradado vertical repartido por filas.
        auto mix = [&](float yy) {
            const float t = h > 0 ? (yy - y) / h : 0.0f;
            return Colour((u8)(top.r + (bottom.r - top.r) * t), (u8)(top.g + (bottom.g - top.g) * t),
                          (u8)(top.b + (bottom.b - top.b) * t), (u8)(top.a + (bottom.a - top.a) * t));
        };
        const Colour ct = mix(ys[row]), cb = mix(ys[row + 1]);
        for (int col = 0; col < 3; ++col)
            gquad(xs[col], ys[row], xs[col + 1], ys[row + 1], us[col], vs[row], us[col + 1], vs[row + 1], ct, cb);
    }
}

bool ensureAtlas()
{
    if (!sAtlas)
        sAtlas = pc_p2_glass_atlas();
    else if (!sAtlas->hasFont)
        sAtlas = pc_p2_glass_atlas(); // la fuente puede llegar más tarde
    return sAtlas != nullptr;
}

float glyphScaleX(int fw) { return sAtlas && sAtlas->cellW > 0 ? (float)fw / sAtlas->cellW : 1.0f; }
float glyphScaleY(int fh) { return sAtlas && sAtlas->cellH > 0 ? (float)fh / sAtlas->cellH : 1.0f; }

int textWidthPx(const char* text, int fw)
{
    if (!sAtlas || !sAtlas->hasFont)
        return (int)(std::strlen(text) * fw * 0.78f + 0.5f);
    const float sx = glyphScaleX(fw);
    float w = 0.0f;
    for (const char* p = text; *p; ++p) {
        const int c = (unsigned char)*p;
        const PcGlassGlyph& g = sAtlas->glyphs[c < 128 ? c : '?'];
        w += (g.valid ? g.width : sAtlas->cellW / 2) * sx;
    }
    return (int)(w + 0.5f);
}

void drawText(float x, float y, const char* text, Colour top, Colour bottom, int fw, int fh)
{
    if (!sAtlas || !sAtlas->hasFont)
        return;
    float sx = glyphScaleX(fw);
    const float sy = glyphScaleY(fh);
    // Como la placa 0 de P1: si no cabe en la zona de contenido, se estrecha.
    if (sContentRight > sContentLeft && x < sContentRight) {
        const int width = textWidthPx(text, fw);
        const float room = (float)sContentRight - x;
        if (width > room && width > 0)
            sx *= room / width;
    }
    float pen = x;
    for (const char* p = text; *p; ++p) {
        int c = (unsigned char)*p;
        if (c >= 128)
            c = '?';
        const PcGlassGlyph& g = sAtlas->glyphs[c];
        if (!g.valid) {
            pen += sAtlas->cellW / 2 * sx;
            continue;
        }
        if (c != ' ') {
            // Como JUTResFont::drawChar_scale: la celda empieza w0 antes del
            // trazo y se avanza w1.
            const float x0 = pen - g.offset * sx;
            gquad(x0, y, x0 + sAtlas->cellW * sx, y + sAtlas->cellH * sy, (float)g.x, (float)g.y,
                  (float)(g.x + sAtlas->cellW), (float)(g.y + sAtlas->cellH), top, bottom);
        }
        pen += g.width * sx;
    }
}

void image(const PcGlassRect& r, int x, int y, int w, int h, float u0, float u1, Colour tint)
{
    if (r.w <= 0)
        return;
    gquad((float)x, (float)y, (float)(x + w), (float)(y + h), r.x + u0 * r.w, (float)r.y, r.x + u1 * r.w,
          (float)(r.y + r.h), tint, tint);
}
} // namespace

void pc_settings_p2d_init() { }

bool pc_settings_p2d_active() { return ensureAtlas() && sAtlas->hasFont; }

int pc_settings_p2d_text_width(const char* text, int fontWidth)
{
    ensureAtlas();
    return textWidthPx(text, fontWidth);
}

void pc_settings_p2d_text(int x, int y, const char* text, Colour color, int fontWidth, int fontHeight)
{
    if (!ensureAtlas())
        return;
    drawText((float)x, (float)y, text, color, color, fontWidth, fontHeight);
}

void pc_settings_p2d_text_styled(int x, int y, const char* text, Colour top, Colour bottom, int fontWidth,
                                 int fontHeight, bool shadow)
{
    if (!ensureAtlas())
        return;
    if (shadow) {
        // La sombra tenue hacia abajo de los menús del juego.
        const Colour s(0, 0, 0, (u8)(top.a * 0.45f));
        drawText((float)x + 1.0f, (float)y + 2.0f, text, s, s, fontWidth, fontHeight);
    }
    drawText((float)x, (float)y, text, top, bottom, fontWidth, fontHeight);
}

void pc_settings_p2d_plate(int x, int y, int w, int h, int style)
{
    if (!ensureAtlas() || w <= 0 || h <= 0)
        return;
    const PcGlassRect& glass = sAtlas->tex[PCG_Glass];
    const PcGlassRect& gra   = sAtlas->tex[PCG_Gradient];
    // Como en P1: 0 es la placa azul del menú de pausa y 2 la amarilla de
    // selección (aquí, la forma del degradado del juego teñida); 1 y 3 son
    // solo el cristal, que se pone encima de otra placa o del fondo.
    const int corner = style == 3 ? std::min(34, std::min(w, h) / 2) : 16;
    if (gra.w > 0 && (style == 0 || style == 2)) {
        const Colour top    = style == 0 ? Colour(60, 115, 235, 235) : Colour(255, 225, 80, 235);
        const Colour bottom = style == 0 ? Colour(25, 55, 160, 235) : Colour(240, 175, 25, 235);
        nineSlice(gra, x, y, w, h, 10.0f, corner, top, bottom);
    }
    // El brillo de la esquina del cristal pasa de 16 texels: en la placa
    // grande se conserva entero en su esquina.
    nineSlice(glass, x, y, w, h, style == 3 ? 32.0f : 16.0f, corner, Colour(255, 255, 255, 255),
              Colour(255, 255, 255, 255));
    if (style == 0) {
        sContentLeft  = x + 16;
        sContentRight = x + w - 16;
    }
}

void pc_settings_p2d_cursor(int cx, int cy, int size, float angle)
{
    if (!ensureAtlas())
        return;
    // Burbuja del cursor girando sobre su eje vertical, con estela.
    const PcGlassRect& dot = sAtlas->tex[PCG_Dot32];
    constexpr int kTrail = 3;
    for (int i = kTrail - 1; i >= 0; --i) {
        const float a  = angle - float(i) * 0.35f;
        const float c  = std::cos(a);
        const int w    = std::max(2, int(std::fabs(c) * size + 0.5f));
        const u8 alpha = i == 0 ? 255 : u8((1.0f - float(i) / kTrail) * 100.0f);
        const bool back = c < 0.0f;
        image(dot, cx - w / 2, cy - size / 2, w, size, back ? 1.0f : 0.0f, back ? 0.0f : 1.0f,
              Colour(255, 255, 255, alpha));
    }
}

bool pc_settings_p2d_button(int x, int y, int size, char button)
{
    if (!ensureAtlas())
        return false;
    static const char kNames[5]       = { 'A', 'B', 'X', 'Y', 'Z' };
    static const PcGlassTex kSlots[5] = { PCG_BtnA, PCG_BtnB, PCG_BtnX, PCG_BtnY, PCG_BtnZ };
    for (int i = 0; i < 5; ++i) {
        if (kNames[i] != button)
            continue;
        const PcGlassRect& r = sAtlas->tex[kSlots[i]];
        if (r.w <= 0)
            return false;
        image(r, x, y, size, size, 0.0f, 1.0f, Colour(255, 255, 255, 255));
        return true;
    }
    return false;
}

void pc_settings_p2d_set_content(int left, int right)
{
    sContentLeft  = left;
    sContentRight = right;
}

// Las imágenes con Texture de P1 (iconos de logros...) no existen aquí.
void pc_settings_p2d_image(int, int, int, int, Texture*, float, float, Colour, float) { }

void pc_settings_p2d_clear()
{
    sGlassCount   = 0;
    sContentLeft  = 0;
    sContentRight = 0;
}

PcSettingsP2DFrame::PcSettingsP2DFrame(int, int) { }
PcSettingsP2DFrame::~PcSettingsP2DFrame() { }

void pc_settings_p2_flush(void)
{
    // El lote del cristal va detrás del de DGXGraphics, como las placas P2D.
    if (sGlassCount > 0 && sAtlas && sVertCount + sGlassCount <= kMaxVerts) {
        pc_gfx_ui_set_atlas(sAtlas->rgba, sAtlas->w, sAtlas->h, sAtlas->generation);
        std::memcpy(sVerts + sVertCount, sGlassVerts, sizeof(PcUiVertex) * sGlassCount);
        sVertCount += sGlassCount;
    }
    sGlassCount = 0;
    if (sVertCount > 0)
        pc_gfx_ui_draw(sVerts, sVertCount, sGfx.mScreenWidth, sGfx.mScreenHeight, kPcUiFont, kPcUiFontW, kPcUiFontH);
    sVertCount = 0;
}

int GameStat::freePikis = 0;
