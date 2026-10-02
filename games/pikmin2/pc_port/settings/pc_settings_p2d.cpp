#include "settings/pc_settings_p2d.h"
#include "P2D/Font.h"
#include "P2D/Graph.h"
#include "P2D/Picture.h"
#include "P2D/Print.h"
#include "P2D/Screen.h"
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kCapacity = 128;
P2DFont* font;
P2DScreen* screen;
Texture* plates[4];
bool active;
int count, width, height;
int contentLeft, contentRight;

class MenuPane : public P2DPicture {
public:
    MenuPane() : P2DPicture(plates[0]) { hide(); }
    bool isText = false;
    bool isIcon = false;
    int fontWidth = 12, fontHeight = 18;
    char text[512] = {};
    Colour color;
protected:
    void drawSelf(int x, int y, immut Matrix4f* view) override
    {
        if (isText) {
            Matrix4f matrix;
            view->multiplyTo(mWorldMtx, matrix);
            GXLoadPosMtxImm(matrix.mMtx, 0);
            P2DPrint print(font, 0, 0, color, color);
            print.setFontSize(fontWidth, fontHeight);
            print.locate(x, y);
            print.printReturn(text, getWidth(), getHeight(), TBOXHBIND_Left, TBOXVBIND_Top, 0, 0);
            return;
        }
        if (isIcon) {
            P2DPicture::drawSelf(x, y, view);
            return;
        }
        // Nine slices preserve the original rounded corners instead of
        // magnifying a 160x88 window into stretched corners at panel size.
        const int edgeX = std::min(16, getWidth() / 2);
        const int edgeY = std::min(16, getHeight() / 2);
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
int pc_settings_p2d_text_width(const char* text, int fontWidth)
{
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
        pane->setTexture(plates[3], 0);
        pane->setWhite(Colour(255, 220, 80, 255));
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
    std::snprintf(pane->text, sizeof(pane->text), "%s", text);
}

void pc_settings_p2d_plate(int x, int y, int w, int h, int style)
{
    MenuPane* pane = next(x,y,w,h);
    if (!pane) return;
    pane->isText = false;
    pane->isIcon = false;
    pane->initWhite();
    pane->setTexture(plates[std::clamp(style, 0, 2)], 0);
    if (style == 0) {
        contentLeft = x + 16;
        contentRight = x + w - 16;
    }
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
