#include "settings/pc_settings_p2d.h"
#include "P2D/Font.h"
#include "P2D/Graph.h"
#include "P2D/Picture.h"
#include "P2D/Print.h"
#include "P2D/Screen.h"
#include "system.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kCapacity = 256; // teclado completo del visor de speedrun incluido
P2DFont* font;
P2DScreen* screen;
Texture* plates[4];
Texture* cursorTex;      // dot_32.bti: bola de cristal del cursor de los menús
Texture* buttonTex[5];   // a/b/x/y/z_btn.bti: iconos de botón del juego
bool active;
int count, width, height;
int contentLeft, contentRight;

class MenuPane : public P2DPicture {
public:
    MenuPane() : P2DPicture(plates[0]) { hide(); }
    bool isText = false;
    bool isIcon = false;
    bool isImage = false;
    float imageU0 = 0.0f, imageU1 = 1.0f, imageV1 = 1.0f;
    int fontWidth = 12, fontHeight = 18;
    char text[512] = {};
    Colour color;
    Colour color2;      // abajo del degradado (igual que color si no hay)
    int bigCorners = 0; // placa con esquinas grandes (estilo burbuja)
protected:
    void drawSelf(int x, int y, immut Matrix4f* view) override
    {
        if (isText) {
            Matrix4f matrix;
            view->multiplyTo(mWorldMtx, matrix);
            GXLoadPosMtxImm(matrix.mMtx, 0);
            P2DPrint print(font, 0, 0, color, color2);
            print.setFontSize(fontWidth, fontHeight);
            print.locate(x, y);
            print.printReturn(text, getWidth(), getHeight(), TBOXHBIND_Left, TBOXVBIND_Top, 0, 0);
            return;
        }
        if (isIcon) {
            P2DPicture::drawSelf(x, y, view);
            return;
        }
        if (isImage) {
            drawTexCoord(0, 0, getWidth(), getHeight(), imageU0, 0, imageU1, 0, imageU0, imageV1, imageU1, imageV1, view);
            return;
        }
        // Nine slices preserve the original rounded corners instead of
        // magnifying a 160x88 window into stretched corners at panel size.
        const int edge  = bigCorners ? bigCorners : 16;
        const int edgeX = std::min(edge, getWidth() / 2);
        const int edgeY = std::min(edge, getHeight() / 2);
        const int xs[] = {0, edgeX, getWidth() - edgeX, getWidth()};
        const int ys[] = {0, edgeY, getHeight() - edgeY, getHeight()};
        // The glass highlight extends beyond 16 source texels. Keep the
        // entire highlight in its corner instead of stretching it into a streak.
        const float sourceEdge = getTexture(0) == plates[1] ? 32.0f : 16.0f;
        const float us[] = {0, sourceEdge / 160, 1 - sourceEdge / 160, 1};
        const float vs[] = {0, sourceEdge / 88, 1 - sourceEdge / 88, 1};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                drawTexCoord(xs[col], ys[row], xs[col+1]-xs[col], ys[row+1]-ys[row],
                    us[col], vs[row], us[col+1], vs[row], us[col], vs[row+1], us[col+1], vs[row+1], view);
            }
        }
    }
};
MenuPane* panes[kCapacity];
MenuPane* next(int x, int y, int w, int h)
{
    if (!active || count == kCapacity || w <= 0 || h <= 0) return nullptr;
    MenuPane* pane = panes[count++];
    pane->place(PUTRect(x, y, x+w, y+h));
    pane->show();
    return pane;
}
}

void pc_settings_p2d_init()
{
    if (screen || !gsys) return;
    const int previousHeap = gsys->getHeapNum();
    gsys->setHeap(SYSHEAP_Sys);
    // These are original, non-textual window assets, loaded independently of
    // whichever screen has most recently changed gsys->mTexDir.
    plates[0] = gsys->loadTexture("screen/eng_tex/ws08_160.bti", true);
    plates[1] = gsys->loadTexture("screen/eng_tex/w08_160.bti", true);
    plates[2] = gsys->loadTexture("screen/eng_tex/ws08_yel.bti", true);
    plates[3] = gsys->loadTexture("screen/eng_tex/dot_24.bti", true);
    cursorTex = gsys->loadTexture("screen/eng_tex/dot_32.bti", true);
    static const char* const kButtons[5] = { "a", "b", "x", "y", "z" };
    for (int i = 0; i < 5; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "screen/eng_tex/%s_btn.bti", kButtons[i]);
        buttonTex[i] = gsys->loadTexture(path, true);
    }
    if (plates[0] && plates[1] && plates[2] && plates[3]) {
        font = new P2DFont("sumiw9_2.bfn");
        screen = new P2DScreen();
        for (auto& pane : panes) {
            pane = new MenuPane();
            screen->appendChild(pane);
        }
    }
    gsys->setHeap(previousHeap);
}
bool pc_settings_p2d_active() { return active; }
// Los textos del port están en UTF-8; la fuente del juego indexa por byte
// (Latin-1 en la versión europea). Se convierte cada "á", "ñ"... a su byte
// Latin-1 si la fuente tiene ese glifo, y si no a la letra sin acento.
static void toFontText(const char* in, char* out, size_t n)
{
    static const char* const kAscii = // 0xC0..0xFF
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
        "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    int capacity = 0;
    if (font && font->mFont && font->mFont->mTexture && font->mFont->mCharWidth > 0 && font->mFont->mCharHeight > 0)
        capacity = (font->mFont->mTexture->mWidth / font->mFont->mCharWidth)
                 * (font->mFont->mTexture->mHeight / font->mFont->mCharHeight);
    size_t o = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p && o + 1 < n; ++p) {
        unsigned c = *p;
        if ((c == 0xC2 || c == 0xC3) && (p[1] & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            ++p;
            if (int(c) - 0x20 >= capacity) {
                if (c >= 0xC0) c = (unsigned char)kAscii[c - 0xC0];
                else if (c == 0xBF) c = '?';
                else if (c == 0xA1) c = '!';
                else continue;
            }
        } else if (c >= 0xC4 && (p[1] & 0xC0) == 0x80) {
            // Otro carácter UTF-8 multibyte: no está en la fuente, se omite.
            while ((p[1] & 0xC0) == 0x80) ++p;
            continue;
        } // Un byte alto suelto ya es Latin-1 (textos del juego): se deja.

        out[o++] = char(c);
    }
    out[o] = '\0';
}

int pc_settings_p2d_text_width(const char* utf8, int fontWidth)
{
    char text[512];
    toFontText(utf8, text, sizeof(text));
    float result = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        result += font->getWidth(*p, fontWidth);
    return int(result + 0.5f);
}
void pc_settings_p2d_text(int x, int y, const char* text, Colour color, int fontWidth, int fontHeight)
{
    if (!active) return;
    if (std::strcmp(text, ">") == 0) {
        MenuPane* pane = next(x, y + 1, 16, 16);
        if (!pane) return;
        pane->isText = false;
        pane->isIcon = true;
        pane->isImage = false;
        pane->setTexture(plates[3], 0);
        pane->setWhite(Colour(255, 220, 80, 255));
        pane->initBlack();
        pane->setAlpha(255);
        return;
    }
    // Long device names and help lines stay inside the current page. Labels
    // and values retain their normal size unless they would cross its edge.
    const int available = contentRight - contentLeft;
    int textWidth = pc_settings_p2d_text_width(text, fontWidth);
    int limit = available;
    bool centered = textWidth > available;
    if (!centered && x < contentLeft) limit = x + textWidth - contentLeft;
    else if (!centered && x + textWidth > contentRight) limit = contentRight - x;
    limit = std::max(1, limit);
    if (textWidth > limit) {
        const int originalWidth = fontWidth;
        while (fontWidth > 1 && textWidth > limit) {
            --fontWidth;
            textWidth = pc_settings_p2d_text_width(text, fontWidth);
        }
        fontHeight = std::max(1, fontHeight * fontWidth / originalWidth);
    }
    if (centered) x = contentLeft + (available - textWidth) / 2;
    else x = std::max(x, contentLeft);
    MenuPane* pane = next(x, y, textWidth + 2, fontHeight + 6);
    if (!pane) return;
    pane->isText = true;
    pane->fontWidth = fontWidth;
    pane->fontHeight = fontHeight;
    pane->color = color;
    pane->color2 = color;
    toFontText(text, pane->text, sizeof(pane->text));
}

void pc_settings_p2d_text_styled(int x, int y, const char* text, Colour top, Colour bottom, int fontWidth, int fontHeight,
                                 bool shadow)
{
    if (!active) return;
    // Sombra de los menús del juego: copia tenue del texto desplazada abajo.
    if (shadow) {
        Colour ghost = top;
        ghost.a = u8(top.a * 0.35f);
        pc_settings_p2d_text(x, y + fontHeight / 4 + 1, text, ghost, fontWidth, fontHeight);
    }
    pc_settings_p2d_text(x, y, text, top, fontWidth, fontHeight);
    if (count > 0) panes[count - 1]->color2 = bottom;
}

void pc_settings_p2d_image(int x, int y, int w, int h, Texture* texture, float u1, float v1, Colour tint, float u0)
{
    if (!texture) return;
    MenuPane* pane = next(x, y, w, h);
    if (!pane) return;
    pane->isText = false;
    pane->isIcon = false;
    pane->isImage = true;
    pane->imageU0 = u0;
    pane->imageU1 = u1;
    pane->imageV1 = v1;
    pane->setTexture(texture, 0);
    pane->initBlack();
    pane->setWhite(tint);
    pane->setAlpha(tint.a);
}

void pc_settings_p2d_plate(int x, int y, int w, int h, int style)
{
    // Fondo azul como el F1 de Pikmin 2. El cristal de P1 (w08_160) es casi
    // transparente, así que teñirlo no basta: los paneles grandes de cristal
    // (columnas, ventanas) llevan debajo la placa 0 en azul. Las cajas
    // pequeñas no, porque bajo ellas va la placa amarilla de selección.
    const bool glassPanel = (style == 1 || style == 3) && w >= 150 && h >= 150;
    if (glassPanel) {
        // La placa 0 fija la zona en la que se encoge el texto; la base no debe
        // cambiarla, o todo el texto se comprime a esta columna.
        const int keepLeft = contentLeft, keepRight = contentRight;
        pc_settings_p2d_plate(x, y, w, h, 0);
        contentLeft  = keepLeft;
        contentRight = keepRight;
    }
    MenuPane* pane = next(x,y,w,h);
    if (!pane) return;
    pane->isText = false;
    pane->isIcon = false;
    pane->isImage = false;
    pane->initWhite();
    pane->setAlpha(255);
    if (style == 0) {
        // De la textura oscura de la placa sale un degradado azul.
        pane->setBlack(Colour(22, 50, 150, 0));
        pane->setWhite(Colour(120, 175, 255, 255));
        pane->setAlpha(235);
    } else {
        pane->initBlack();
    }
    // 3: el cristal de las ventanas del juego con las esquinas a tamaño de
    // burbuja: borde plateado grueso y el brillo grande de la esquina.
    pane->bigCorners = style == 3 ? std::min(34, std::min(w, h) / 2) : 0;
    pane->setTexture(plates[style == 3 ? 1 : std::clamp(style, 0, 2)], 0);
    if (style == 0) {
        contentLeft = x + 16;
        contentRight = x + w - 16;
    }
}
void pc_settings_p2d_cursor(int cx, int cy, int size, float angle)
{
    if (!cursorTex) return;
    // Como SpectrumCursorMgr: giro sobre el eje Y (el ancho sigue |cos|,
    // espejado por detrás) y una estela de copias más tenues y retrasadas.
    constexpr int kTrail = 3;
    for (int i = kTrail - 1; i >= 0; --i) {
        const float a = angle - float(i) * 0.35f;
        const float c = std::cos(a);
        const int w = std::max(2, int(std::fabs(c) * size + 0.5f));
        const u8 alpha = i == 0 ? 255 : u8((1.0f - float(i) / kTrail) * 100.0f);
        const bool back = c < 0.0f;
        pc_settings_p2d_image(cx - w / 2, cy - size / 2, w, size, cursorTex, back ? 0.0f : 1.0f, 1.0f,
                              Colour(255, 255, 255, alpha), back ? 1.0f : 0.0f);
    }
}
bool pc_settings_p2d_button(int x, int y, int size, char button)
{
    static const char kNames[5] = { 'A', 'B', 'X', 'Y', 'Z' };
    for (int i = 0; i < 5; ++i) {
        if (kNames[i] != button) continue;
        if (!buttonTex[i]) return false;
        pc_settings_p2d_image(x, y, size, size, buttonTex[i], 1.0f, 1.0f, Colour(255, 255, 255, 255));
        return true;
    }
    return false;
}
void pc_settings_p2d_set_content(int left, int right)
{
    contentLeft = left;
    contentRight = right;
}
void pc_settings_p2d_clear()
{
    if (!active) return;
    for (int i = 0; i < count; ++i) panes[i]->hide();
    count = 0;
}
PcSettingsP2DFrame::PcSettingsP2DFrame(int w, int h)
{
    active = screen != nullptr;
    if (!active) return;
    width = w; height = h;
    contentLeft = 0; contentRight = w;
    screen->place(PUTRect(0, 0, w, h));
    pc_settings_p2d_clear();
}
PcSettingsP2DFrame::~PcSettingsP2DFrame()
{
    if (!active) return;
    P2DOrthoGraph graph(0, 0, width, height);
    // P2DGrafContext::setScissor subtracts one from the Y bounds for the
    // console. Compensate here so this independent overlay leaves a full
    // viewport scissor, including the bottom row, for the next frame.
    graph.scissor(PUTRect(0, 1, width, height + 1));
    graph.setPort();
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    screen->draw(0, 0, &graph);
    active = false;
}
