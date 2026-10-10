#include "pc_speedrun.h"
#include "pc_livesplit.h"
#include "pc_window.h"
#include "settings/pc_settings_p2d.h"
#include "settings/pc_settings.h"
#if PIKI_PC_TOUCH
#include "touch/pc_touch.h"
#include "Dolphin/pad.h"
#endif
#include <SDL.h>
#include <cmath>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

std::string pc_card_speedrun_dir(); // card_stubs.cpp

namespace {

bool sActive = false;
bool sRunning = false;
bool sFinished = false;
Uint64 sStart = 0, sEnd = 0;


// ─── Dibujo ────────────────────────────────────────────────────────────────
// Todo se maqueta en el espacio de 480 de alto y se escala a píxeles reales.

DGXGraphics* sGfx = nullptr;
float sScale = 1.0f;

int px(float v) { return int(v * sScale + 0.5f); }

void fillRect(float x, float y, float w, float h, Colour c)
{
    sGfx->setColour(c, true);
    sGfx->setAuxColour(c);
    sGfx->fillRectangle(RectArea(px(x), px(y), px(x + w), px(y + h)));
}

// Bandas horizontales: rectángulo redondeado o círculo (r = w/2 = h/2).
void fillRound(float x, float y, float w, float h, float r, Colour c)
{
    const int X = px(x), Y = px(y), W = px(x + w) - X, H = px(y + h) - Y;
    int R = std::min(px(r), std::min(W, H) / 2);
    if (W <= 0 || H <= 0) return;
    sGfx->setColour(c, true);
    sGfx->setAuxColour(c);
    for (int yy = 0; yy < H; ++yy) {
        int inset = 0;
        const int lo = yy < R ? R - yy : (yy >= H - R ? yy - (H - R) + 1 : 0);
        if (lo > 0) inset = R - int(std::sqrt(float(R * R - (lo - 0.5f) * (lo - 0.5f))));
        sGfx->fillRectangle(RectArea(X + inset, Y + yy, X + W - inset, Y + yy + 1));
    }
}

void fillCircle(float cx, float cy, float r, Colour c) { fillRound(cx - r, cy - r, 2 * r, 2 * r, r, c); }

void text(float x, float y, const char* s, Colour c, float fw, float fh)
{
    pc_settings_p2d_text(px(x), px(y), s, c, std::max(1, px(fw)), std::max(1, px(fh)));
}
void textCentered(float cx, float cy, const char* s, Colour c, float fw, float fh)
{
    const int w = pc_settings_p2d_text_width(s, std::max(1, px(fw)));
    pc_settings_p2d_text(px(cx) - w / 2, px(cy - fh / 2), s, c, std::max(1, px(fw)), std::max(1, px(fh)));
}

const Colour kPanel(8, 12, 22, 120);
const Colour kIdle(190, 205, 225, 70);
const Colour kDown(255, 170, 30, 230);
const Colour kLabel(225, 240, 255, 230);

// ─── Mando ─────────────────────────────────────────────────────────────────

struct PadLabels {
    const char* face[4]; // abajo, derecha, izquierda, arriba (posición física)
    const char* l1;
    const char* r1;
    const char* l2;
    const char* r2;
    const char* back;
    const char* start;
};

PadLabels padLabels(SDL_GameController* ctl)
{
    switch (SDL_GameControllerGetType(ctl)) {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5:
        return { { "x", "o", "[]", "^" }, "L1", "R1", "L2", "R2", "SH", "OP" };
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return { { "B", "A", "Y", "X" }, "L", "R", "ZL", "ZR", "-", "+" };
    default:
        return { { "A", "B", "X", "Y" }, "LB", "RB", "LT", "RT", "<<", ">>" };
    }
}

void drawStick(float cx, float cy, float ax, float ay, bool click)
{
    fillCircle(cx, cy, 14.0f, click ? kDown : kIdle);
    const bool moved = std::fabs(ax) > 0.25f || std::fabs(ay) > 0.25f;
    fillCircle(cx + ax * 8.0f, cy + ay * 8.0f, 6.0f, moved ? kDown : Colour(225, 235, 250, 160));
}

void drawPad(float x, float y, SDL_GameController* ctl)
{
    auto btn  = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(ctl, b) != 0; };
    auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(ctl, a) / 32767.0f; };
    const PadLabels lab = padLabels(ctl);

    // Gatillos (se llenan según lo apretados) y botones de hombro.
    const float lt = std::clamp(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.0f, 1.0f);
    const float rt = std::clamp(axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.0f, 1.0f);
    fillRound(x + 14, y, 46, 9, 4, kIdle);
    if (lt > 0.02f) fillRound(x + 14, y, 46 * lt, 9, 4, kDown);
    fillRound(x + 140, y, 46, 9, 4, kIdle);
    if (rt > 0.02f) fillRound(x + 186 - 46 * rt, y, 46 * rt, 9, 4, kDown);
    textCentered(x + 37, y + 4.5f, lab.l2, kLabel, 5, 8);
    textCentered(x + 163, y + 4.5f, lab.r2, kLabel, 5, 8);
    fillRound(x + 14, y + 11, 46, 9, 4, btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? kDown : kIdle);
    fillRound(x + 140, y + 11, 46, 9, 4, btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? kDown : kIdle);
    textCentered(x + 37, y + 15.5f, lab.l1, kLabel, 5, 8);
    textCentered(x + 163, y + 15.5f, lab.r1, kLabel, 5, 8);

    // Cuerpo del mando.
    fillRound(x, y + 22, 200, 82, 30, kPanel);

    drawStick(x + 44, y + 52, axis(SDL_CONTROLLER_AXIS_LEFTX), axis(SDL_CONTROLLER_AXIS_LEFTY),
              btn(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    drawStick(x + 130, y + 88, axis(SDL_CONTROLLER_AXIS_RIGHTX), axis(SDL_CONTROLLER_AXIS_RIGHTY),
              btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK));

    // Cruceta.
    const float dx = x + 70, dy = y + 88;
    fillRect(dx - 4, dy - 4, 8, 8, kIdle);
    fillRect(dx - 4, dy - 13, 8, 9, btn(SDL_CONTROLLER_BUTTON_DPAD_UP) ? kDown : kIdle);
    fillRect(dx - 4, dy + 4, 8, 9, btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? kDown : kIdle);
    fillRect(dx - 13, dy - 4, 9, 8, btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? kDown : kIdle);
    fillRect(dx + 4, dy - 4, 9, 8, btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? kDown : kIdle);

    // Botones frontales en rombo (SDL los nombra por posición: A abajo...).
    const float fx = x + 156, fy = y + 52;
    const SDL_GameControllerButton face[4] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                                               SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
    const float off[4][2] = { { 0, 13 }, { 13, 0 }, { -13, 0 }, { 0, -13 } };
    for (int i = 0; i < 4; ++i) {
        fillCircle(fx + off[i][0], fy + off[i][1], 7.0f, btn(face[i]) ? kDown : kIdle);
        textCentered(fx + off[i][0], fy + off[i][1], lab.face[i], kLabel, 5, 8);
    }

    // Select/View y Start/Menu.
    fillRound(x + 84, y + 47, 13, 8, 4, btn(SDL_CONTROLLER_BUTTON_BACK) ? kDown : kIdle);
    fillRound(x + 103, y + 47, 13, 8, 4, btn(SDL_CONTROLLER_BUTTON_START) ? kDown : kIdle);
    textCentered(x + 90.5f, y + 51, lab.back, kLabel, 4, 6);
    textCentered(x + 109.5f, y + 51, lab.start, kLabel, 4, 6);
}

#if PIKI_PC_TOUCH
// Pantalla táctil: un mando de GameCube pequeño con lo que usa el juego
// original (stick, C-Stick, A, B, X, Y, Z, L, R y Start), encendido según el
// estado que el juego recibe de la capa táctil.
void drawTouchPad(float x, float y)
{
    u16 b = 0;
    s8 sx = 0, sy = 0, cx = 0, cy = 0;
    pc_touch_last_pad(&b, &sx, &sy, &cx, &cy);
    auto on = [&](u16 mask) { return (b & mask) != 0; };

    // Gatillos L y R arriba, Z junto a R.
    fillRound(x + 6, y, 34, 8, 4, on(PAD_TRIGGER_L) ? kDown : kIdle);
    fillRound(x + 100, y, 34, 8, 4, on(PAD_TRIGGER_R) ? kDown : kIdle);
    fillRound(x + 104, y + 10, 26, 6, 3, on(PAD_TRIGGER_Z) ? kDown : kIdle);
    textCentered(x + 23, y + 4, "L", kLabel, 4, 6);
    textCentered(x + 117, y + 4, "R", kLabel, 4, 6);
    textCentered(x + 117, y + 13, "Z", kLabel, 3, 5);

    // Cuerpo.
    fillRound(x, y + 18, 140, 56, 22, kPanel);

    // Stick (izquierda) y C-Stick (abajo a la derecha). En GC la Y va hacia
    // arriba; en pantalla, hacia abajo.
    drawStick(x + 30, y + 42, sx / 127.0f, -sy / 127.0f, false);
    fillCircle(x + 86, y + 62, 8.0f, kIdle);
    const bool cMoved = std::abs(cx) > 30 || std::abs(cy) > 30;
    fillCircle(x + 86 + cx / 127.0f * 4.0f, y + 62 - cy / 127.0f * 4.0f, 4.0f, cMoved ? kDown : Colour(225, 235, 250, 160));

    // Start en el centro.
    fillCircle(x + 64, y + 40, 4.0f, on(PAD_BUTTON_START) ? kDown : kIdle);

    // A grande, B abajo a la izquierda, X a la derecha, Y arriba.
    const float ax = x + 110, ay = y + 44;
    fillCircle(ax, ay, 9.0f, on(PAD_BUTTON_A) ? kDown : kIdle);
    textCentered(ax, ay, "A", kLabel, 5, 8);
    fillCircle(ax - 15, ay + 9, 5.5f, on(PAD_BUTTON_B) ? kDown : kIdle);
    textCentered(ax - 15, ay + 9, "B", kLabel, 4, 6);
    fillCircle(ax + 16, ay - 2, 5.5f, on(PAD_BUTTON_X) ? kDown : kIdle);
    textCentered(ax + 16, ay - 2, "X", kLabel, 4, 6);
    fillCircle(ax - 3, ay - 15, 5.5f, on(PAD_BUTTON_Y) ? kDown : kIdle);
    textCentered(ax - 3, ay - 15, "Y", kLabel, 4, 6);
}
#endif

// ─── Teclado y ratón ───────────────────────────────────────────────────────

struct Key {
    SDL_Scancode sc;
    float w;           // ancho en unidades de tecla
    const char* label; // nullptr: el nombre que da la distribución activa
};
constexpr SDL_Scancode GAP = SDL_SCANCODE_UNKNOWN; // hueco sin tecla

const Key kRow0[] = { { SDL_SCANCODE_ESCAPE, 1, "Esc" }, { GAP, 1, nullptr },
                      { SDL_SCANCODE_F1, 1, "F1" }, { SDL_SCANCODE_F2, 1, "F2" }, { SDL_SCANCODE_F3, 1, "F3" },
                      { SDL_SCANCODE_F4, 1, "F4" }, { GAP, 0.5f, nullptr }, { SDL_SCANCODE_F5, 1, "F5" },
                      { SDL_SCANCODE_F6, 1, "F6" }, { SDL_SCANCODE_F7, 1, "F7" }, { SDL_SCANCODE_F8, 1, "F8" },
                      { GAP, 0.5f, nullptr }, { SDL_SCANCODE_F9, 1, "F9" }, { SDL_SCANCODE_F10, 1, "F10" },
                      { SDL_SCANCODE_F11, 1, "F11" }, { SDL_SCANCODE_F12, 1, "F12" } };
const Key kRow1[] = { { SDL_SCANCODE_GRAVE, 1, nullptr }, { SDL_SCANCODE_1, 1, nullptr }, { SDL_SCANCODE_2, 1, nullptr },
                      { SDL_SCANCODE_3, 1, nullptr }, { SDL_SCANCODE_4, 1, nullptr }, { SDL_SCANCODE_5, 1, nullptr },
                      { SDL_SCANCODE_6, 1, nullptr }, { SDL_SCANCODE_7, 1, nullptr }, { SDL_SCANCODE_8, 1, nullptr },
                      { SDL_SCANCODE_9, 1, nullptr }, { SDL_SCANCODE_0, 1, nullptr }, { SDL_SCANCODE_MINUS, 1, nullptr },
                      { SDL_SCANCODE_EQUALS, 1, nullptr }, { SDL_SCANCODE_BACKSPACE, 2, "<-" } };
const Key kRow2[] = { { SDL_SCANCODE_TAB, 1.5f, "Tab" }, { SDL_SCANCODE_Q, 1, nullptr }, { SDL_SCANCODE_W, 1, nullptr },
                      { SDL_SCANCODE_E, 1, nullptr }, { SDL_SCANCODE_R, 1, nullptr }, { SDL_SCANCODE_T, 1, nullptr },
                      { SDL_SCANCODE_Y, 1, nullptr }, { SDL_SCANCODE_U, 1, nullptr }, { SDL_SCANCODE_I, 1, nullptr },
                      { SDL_SCANCODE_O, 1, nullptr }, { SDL_SCANCODE_P, 1, nullptr },
                      { SDL_SCANCODE_LEFTBRACKET, 1, nullptr }, { SDL_SCANCODE_RIGHTBRACKET, 1, nullptr },
                      { SDL_SCANCODE_BACKSLASH, 1.5f, nullptr } };
const Key kRow3[] = { { SDL_SCANCODE_CAPSLOCK, 1.75f, "Caps" }, { SDL_SCANCODE_A, 1, nullptr },
                      { SDL_SCANCODE_S, 1, nullptr }, { SDL_SCANCODE_D, 1, nullptr }, { SDL_SCANCODE_F, 1, nullptr },
                      { SDL_SCANCODE_G, 1, nullptr }, { SDL_SCANCODE_H, 1, nullptr }, { SDL_SCANCODE_J, 1, nullptr },
                      { SDL_SCANCODE_K, 1, nullptr }, { SDL_SCANCODE_L, 1, nullptr },
                      { SDL_SCANCODE_SEMICOLON, 1, nullptr }, { SDL_SCANCODE_APOSTROPHE, 1, nullptr },
                      { SDL_SCANCODE_RETURN, 2.25f, "Enter" } };
const Key kRow4[] = { { SDL_SCANCODE_LSHIFT, 1.25f, "Shift" }, { SDL_SCANCODE_NONUSBACKSLASH, 1, nullptr },
                      { SDL_SCANCODE_Z, 1, nullptr }, { SDL_SCANCODE_X, 1, nullptr }, { SDL_SCANCODE_C, 1, nullptr },
                      { SDL_SCANCODE_V, 1, nullptr }, { SDL_SCANCODE_B, 1, nullptr }, { SDL_SCANCODE_N, 1, nullptr },
                      { SDL_SCANCODE_M, 1, nullptr }, { SDL_SCANCODE_COMMA, 1, nullptr },
                      { SDL_SCANCODE_PERIOD, 1, nullptr }, { SDL_SCANCODE_SLASH, 1, nullptr },
                      { SDL_SCANCODE_RSHIFT, 2.75f, "Shift" } };
const Key kRow5[] = { { SDL_SCANCODE_LCTRL, 1.25f, "Ctrl" }, { SDL_SCANCODE_LGUI, 1.25f, "Win" },
                      { SDL_SCANCODE_LALT, 1.25f, "Alt" }, { SDL_SCANCODE_SPACE, 6.25f, "" },
                      { SDL_SCANCODE_RALT, 1.25f, "AltGr" }, { SDL_SCANCODE_RGUI, 1.25f, "Win" },
                      { SDL_SCANCODE_APPLICATION, 1.25f, "Menu" }, { SDL_SCANCODE_RCTRL, 1.25f, "Ctrl" } };

// Bloque de navegación y flechas (3 columnas) y teclado numérico (4).
const Key kNav0[] = { { SDL_SCANCODE_PRINTSCREEN, 1, "PrS" }, { SDL_SCANCODE_SCROLLLOCK, 1, "ScL" },
                      { SDL_SCANCODE_PAUSE, 1, "Pau" } };
const Key kNav1[] = { { SDL_SCANCODE_INSERT, 1, "Ins" }, { SDL_SCANCODE_HOME, 1, "Ini" },
                      { SDL_SCANCODE_PAGEUP, 1, "RePg" } };
const Key kNav2[] = { { SDL_SCANCODE_DELETE, 1, "Sup" }, { SDL_SCANCODE_END, 1, "Fin" },
                      { SDL_SCANCODE_PAGEDOWN, 1, "AvPg" } };
const Key kNav4[] = { { GAP, 1, nullptr }, { SDL_SCANCODE_UP, 1, "^" }, { GAP, 1, nullptr } };
const Key kNav5[] = { { SDL_SCANCODE_LEFT, 1, "<" }, { SDL_SCANCODE_DOWN, 1, "v" }, { SDL_SCANCODE_RIGHT, 1, " >" } }; // ">" solo es el icono de cursor de P2D
const Key kNum1[] = { { SDL_SCANCODE_NUMLOCKCLEAR, 1, "Num" }, { SDL_SCANCODE_KP_DIVIDE, 1, "/" },
                      { SDL_SCANCODE_KP_MULTIPLY, 1, "*" }, { SDL_SCANCODE_KP_MINUS, 1, "-" } };
const Key kNum2[] = { { SDL_SCANCODE_KP_7, 1, "7" }, { SDL_SCANCODE_KP_8, 1, "8" }, { SDL_SCANCODE_KP_9, 1, "9" } };
const Key kNum3[] = { { SDL_SCANCODE_KP_4, 1, "4" }, { SDL_SCANCODE_KP_5, 1, "5" }, { SDL_SCANCODE_KP_6, 1, "6" } };
const Key kNum4[] = { { SDL_SCANCODE_KP_1, 1, "1" }, { SDL_SCANCODE_KP_2, 1, "2" }, { SDL_SCANCODE_KP_3, 1, "3" } };
const Key kNum5[] = { { SDL_SCANCODE_KP_0, 2, "0" }, { SDL_SCANCODE_KP_PERIOD, 1, "." } };

constexpr float kU = 9.0f;   // tamaño de una tecla (espacio de 480)
constexpr float kGapK = 1.0f; // separación entre teclas

void drawKey(float x, float y, float w, float h, const Key& k, const Uint8* keys)
{
    if (k.sc == GAP) return;
    const bool down = keys[k.sc] != 0;
    fillRound(x, y, w, h, 1.5f, down ? kDown : kIdle);
    const char* label = k.label;
    char buf[8];
    if (!label) {
        // Letras, números y signos: lo que pone la tecla en la distribución
        // del jugador (Ñ incluida si la fuente la tiene).
        const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(k.sc));
        std::snprintf(buf, sizeof(buf), "%s", name);
        label = buf;
    }
    if (*label) textCentered(x + w / 2, y + h / 2 + 0.5f, label, kLabel, 3.2f, 5.0f);
}

template <size_t N>
float drawRow(float x, float y, const Key (&row)[N], const Uint8* keys)
{
    for (const Key& k : row) {
        const float w = k.w * kU + (k.w - 1.0f) * kGapK;
        drawKey(x, y, w, kU, k, keys);
        x += w + kGapK;
    }
    return x;
}

void drawKeyboard(float x, float y)
{
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const float step = kU + kGapK;
    const float mainW = 15.0f * kU + 14.0f * kGapK;
    const float navX = x + mainW + 6.0f, numX = navX + 3.0f * step + 6.0f;
    const float totalW = numX + 4.0f * step - x;

    fillRound(x - 4, y - 4, totalW + 8 + 34, 6 * step + 4 + 8, 4, kPanel);

    drawRow(x, y, kRow0, keys);
    const float y1 = y + step + 3.0f; // la fila de F va un poco separada
    drawRow(x, y1, kRow1, keys);
    drawRow(x, y1 + step, kRow2, keys);
    drawRow(x, y1 + 2 * step, kRow3, keys);
    drawRow(x, y1 + 3 * step, kRow4, keys);
    drawRow(x, y1 + 4 * step, kRow5, keys);

    drawRow(navX, y, kNav0, keys);
    drawRow(navX, y1, kNav1, keys);
    drawRow(navX, y1 + step, kNav2, keys);
    drawRow(navX, y1 + 3 * step, kNav4, keys);
    drawRow(navX, y1 + 4 * step, kNav5, keys);

    drawRow(numX, y1, kNum1, keys);
    drawRow(numX, y1 + step, kNum2, keys);
    drawRow(numX, y1 + 2 * step, kNum3, keys);
    drawRow(numX, y1 + 3 * step, kNum4, keys);
    drawRow(numX, y1 + 4 * step, kNum5, keys);
    // + y Enter del numérico ocupan dos filas.
    const Key plus = { SDL_SCANCODE_KP_PLUS, 1, "+" };
    const Key enter = { SDL_SCANCODE_KP_ENTER, 1, "Ent" };
    drawKey(numX + 3 * step, y1 + step, kU, 2 * kU + kGapK, plus, keys);
    drawKey(numX + 3 * step, y1 + 3 * step, kU, 2 * kU + kGapK, enter, keys);

    // Ratón a la derecha: botones izquierdo, central (rueda) y derecho.
    const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    const float mx = x + totalW + 8.0f, my = y1 + step;
    fillRound(mx, my, 24, 36, 11, kIdle);
    fillRound(mx, my, 11.5f, 14, 5, (mouse & SDL_BUTTON_LMASK) ? kDown : Colour(225, 235, 250, 60));
    fillRound(mx + 12.5f, my, 11.5f, 14, 5, (mouse & SDL_BUTTON_RMASK) ? kDown : Colour(225, 235, 250, 60));
    fillRound(mx + 10, my + 3, 4, 8, 2, (mouse & SDL_BUTTON_MMASK) ? kDown : Colour(40, 50, 70, 200));
}

// ─── Splits y registro de tiempos ──────────────────────────────────────────

constexpr int kMaxSplits = 64;
constexpr int kMaxBest   = 96;
constexpr int kMaxRecent = 5;

struct Split {
    char name[32];
    u64 ms; // tiempo acumulado al cerrar el tramo
};

struct Records {
    bool hasPb = false;
    u64 pbMs = 0;
    int pbDays = 0;
    char pbDate[16] = {};
    int pbCount = 0;
    Split pb[kMaxSplits];
    int bestCount = 0;
    Split best[kMaxBest]; // mejor tramo (no acumulado) por nombre de split
    int recentCount = 0;
    u64 recentMs[kMaxRecent] = {};
    int recentDays[kMaxRecent] = {};
    char recentDate[kMaxRecent][16] = {};
};

Records sRec;
bool sRecLoaded = false;
int sRecCategory = -1; // categoría del registro cargado

struct Category {
    const char* name;
    const char* slug;    // carpeta de sus puntos de práctica
    const char* records; // archivo en save/speedrun
    const char* lss;     // splits de LiveSplit exportados, en save/speedrun
    const char* srcName; // nombre de la categoría en speedrun.com
    int finish;          // PC_SR_FINISH_* que cierra la run
};
const Category kCategories[PC_SR_CAT_COUNT] = {
    { "Pay Off Debt", "pay-off-debt", "records-pay-off-debt", "pikmin2-pay-off-debt.lss", "Pay Off Debt", PC_SR_FINISH_DEBT },
    { "All Treasures", "all-treasures", "records-all-treasures", "pikmin2-all-treasures.lss", "All Treasures", PC_SR_FINISH_COMPLETE },
    { "No Major Exploits", "no-major-exploits", "records-no-major-exploits", "pikmin2-no-major-exploits.lss", "No Major Exploits",
      PC_SR_FINISH_DEBT },
    { "Challenge All Levels", "challenge-all-levels", "records-challenge-all-levels",
      "pikmin2-challenge-all-levels.lss", "Challenge Mode All Levels", PC_SR_FINISH_CHALLENGE },
    { "Challenge All Treasures", "challenge-all-treasures", "records-challenge-all-treasures",
      "pikmin2-challenge-all-treasures.lss", "Challenge Mode All Treasures", PC_SR_FINISH_CHALLENGE },
};

// Nombres fijos en inglés: son la clave del mejor tramo, no el texto traducido.
const char* const kCourseNames[] = { "Valley of Repose", "Awakening Wood", "Perplexing Pool", "Wistful Wild" };

struct CaveName {
    unsigned id;
    const char* name;
};
#define CAVE_ID(a, b, c, d) ((unsigned(a) << 24) | (unsigned(b) << 16) | (unsigned(c) << 8) | unsigned(d))
const CaveName kCaveNames[] = {
    { CAVE_ID('t', '_', '0', '1'), "Emergence Cave" },      { CAVE_ID('t', '_', '0', '2'), "Subterranean Complex" },
    { CAVE_ID('t', '_', '0', '3'), "Frontier Cavern" },     { CAVE_ID('f', '_', '0', '1'), "Hole of Beasts" },
    { CAVE_ID('f', '_', '0', '2'), "White Flower Garden" }, { CAVE_ID('f', '_', '0', '3'), "Bulblax Kingdom" },
    { CAVE_ID('f', '_', '0', '4'), "Snagret Hole" },        { CAVE_ID('y', '_', '0', '1'), "Citadel of Spiders" },
    { CAVE_ID('y', '_', '0', '2'), "Glutton's Kitchen" },   { CAVE_ID('y', '_', '0', '3'), "Shower Room" },
    { CAVE_ID('y', '_', '0', '4'), "Submerged Castle" },    { CAVE_ID('l', '_', '0', '1'), "Cavern of Chaos" },
    { CAVE_ID('l', '_', '0', '2'), "Hole of Heroes" },      { CAVE_ID('l', '_', '0', '3'), "Dream Den" },
};
#undef CAVE_ID

// Run en curso.
Split sRun[kMaxSplits];
int sRunCount = 0;
bool sNewPb = false;
bool sAwaitingStart = false;     // Desafío: run elegida, el reloj espera al primer nivel
unsigned long long sChalDone = 0; // Desafío: niveles ya contados (bit por índice de la lista)
bool sChalAllTreasures = true;    // Desafío: el nivel en curso no ha dejado tesoros atrás
int sPracticeDay = 0;     // día de práctica elegido en el menú (0 = no)
bool sPracticing = false; // la run en curso es de práctica
constexpr int kMaxDays = 99;
u64 sPrevPbMs = 0; // PB anterior, para la diferencia final

u64 elapsedMs()
{
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 now = sFinished ? sEnd : SDL_GetPerformanceCounter();
    return (sRunning || sFinished) ? (now - sStart) * 1000 / (freq ? freq : 1) : 0;
}

bool envFlag(const char* name)
{
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}

int category() { return pc_speedrun_category(); }

std::string recordsPath() { return pc_card_speedrun_dir() + "/" + kCategories[category()].records; }

void saveRecords()
{
    FILE* f = std::fopen(recordsPath().c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# Open Nectar - registro del modo Speedrun\n");
    if (sRec.hasPb) {
        std::fprintf(f, "pb=%llu|%d|%s\n", (unsigned long long)sRec.pbMs, sRec.pbDays, sRec.pbDate);
        for (int i = 0; i < sRec.pbCount; i++)
            std::fprintf(f, "split=%llu|%s\n", (unsigned long long)sRec.pb[i].ms, sRec.pb[i].name);
    }
    for (int i = 0; i < sRec.bestCount; i++)
        std::fprintf(f, "best=%llu|%s\n", (unsigned long long)sRec.best[i].ms, sRec.best[i].name);
    for (int i = 0; i < sRec.recentCount; i++)
        std::fprintf(f, "recent=%llu|%d|%s\n", (unsigned long long)sRec.recentMs[i], sRec.recentDays[i], sRec.recentDate[i]);
    std::fclose(f);
}

void loadRecords()
{
    if (sRecLoaded && sRecCategory == category()) return;
    sRecLoaded = true;
    sRecCategory = category();
    sRec = Records();
    FILE* f = std::fopen(recordsPath().c_str(), "r");
    if (!f) return;
    char line[160];
    while (std::fgets(line, sizeof(line), f)) {
        line[std::strcspn(line, "\r\n")] = '\0';
        unsigned long long ms = 0;
        int days = 0, used = 0;
        if (std::sscanf(line, "pb=%llu|%d|%n", &ms, &days, &used) == 2) {
            sRec.hasPb = true;
            sRec.pbMs = ms;
            sRec.pbDays = days;
            std::snprintf(sRec.pbDate, sizeof(sRec.pbDate), "%s", line + used);
        } else if (std::sscanf(line, "split=%llu|%n", &ms, &used) == 1 && sRec.pbCount < kMaxSplits) {
            sRec.pb[sRec.pbCount].ms = ms;
            std::snprintf(sRec.pb[sRec.pbCount].name, sizeof(Split::name), "%s", line + used);
            sRec.pbCount++;
        } else if (std::sscanf(line, "best=%llu|%n", &ms, &used) == 1 && sRec.bestCount < kMaxBest) {
            sRec.best[sRec.bestCount].ms = ms;
            std::snprintf(sRec.best[sRec.bestCount].name, sizeof(Split::name), "%s", line + used);
            sRec.bestCount++;
        } else if (std::sscanf(line, "recent=%llu|%d|%n", &ms, &days, &used) == 2 && sRec.recentCount < kMaxRecent) {
            sRec.recentMs[sRec.recentCount] = ms;
            sRec.recentDays[sRec.recentCount] = days;
            std::snprintf(sRec.recentDate[sRec.recentCount], 16, "%s", line + used);
            sRec.recentCount++;
        }
    }
    std::fclose(f);
}

const Split* findIn(const Split* list, int count, const char* name)
{
    for (int i = 0; i < count; i++)
        if (std::strcmp(list[i].name, name) == 0) return &list[i];
    return nullptr;
}

// Duración del tramo i de la run en curso (no acumulada).
u64 segmentMs(int i) { return sRun[i].ms - (i > 0 ? sRun[i - 1].ms : 0); }

void formatMs(u64 ms, char* out, size_t n)
{
    const unsigned h = unsigned(ms / 3600000), m = unsigned(ms / 60000 % 60), s = unsigned(ms / 1000 % 60);
    if (h) std::snprintf(out, n, "%u:%02u:%02u.%03u", h, m, s, unsigned(ms % 1000));
    else std::snprintf(out, n, "%u:%02u.%03u", m, s, unsigned(ms % 1000));
}

// Diferencia con signo, al estilo LiveSplit: "+0:12.3" / "-1:05.0".
void formatDelta(long long ms, char* out, size_t n)
{
    const char sign = ms < 0 ? '-' : '+';
    const unsigned long long a = (unsigned long long)(ms < 0 ? -ms : ms);
    const unsigned m = unsigned(a / 60000), s = unsigned(a / 1000 % 60), t = unsigned(a / 100 % 10);
    if (m) std::snprintf(out, n, "%c%u:%02u.%u", sign, m, s, t);
    else std::snprintf(out, n, "%c%u.%u", sign, s, t);
}

const Colour kGreen(80, 225, 110, 255);
const Colour kRed(255, 95, 85, 255);
const Colour kGold(255, 205, 60, 255);

// Últimos splits bajo el reloj. Devuelve la altura usada.
float drawSplits(float x, float y)
{
    if (sRunCount == 0) return 0.0f;
    const int kShown = pc_settings_get_speedrun_splits_shown();
    const int first = std::max(0, sRunCount - kShown);
    const float rowH = 15.0f, w = 230.0f;
    const int rows = sRunCount - first;
    fillRound(x, y, w, rows * rowH + 8, 6, kPanel);
    for (int i = first; i < sRunCount; i++) {
        const float ry = y + 4 + (i - first) * rowH;
        text(x + 8, ry, sRun[i].name, kLabel, 7, 11);
        char cum[24];
        formatMs(sRun[i].ms, cum, sizeof(cum));
        const int cw = pc_settings_p2d_text_width(cum, px(7));
        pc_settings_p2d_text(px(x + w - 8) - cw, px(ry), cum, kLabel, px(7), px(11));
        const Split* pb = findIn(sRec.pb, sRec.pbCount, sRun[i].name);
        if (!pb) continue;
        const Split* best = findIn(sRec.best, sRec.bestCount, sRun[i].name);
        const long long delta = (long long)sRun[i].ms - (long long)pb->ms;
        char d[24];
        formatDelta(delta, d, sizeof(d));
        const Colour c = best && segmentMs(i) < best->ms ? kGold : (delta < 0 ? kGreen : kRed);
        const int dw = pc_settings_p2d_text_width(d, px(7));
        pc_settings_p2d_text(px(x + w - 8) - cw - px(8) - dw, px(ry), d, c, px(7), px(11));
    }
    return rows * rowH + 8;
}

void addNamedSplit(const char* name, u64 ms)
{
    if (sRunCount >= kMaxSplits) return;
    std::snprintf(sRun[sRunCount].name, sizeof(Split::name), "%s", name);
    sRun[sRunCount].ms = ms;
    sRunCount++;
    if (!sPracticing) pc_livesplit_split(ms);

    char t[24], line[96];
    formatMs(ms, t, sizeof(t));
    std::snprintf(line, sizeof(line), "%s", t);
    if (const Split* pb = findIn(sRec.pb, sRec.pbCount, name)) {
        char d[24];
        formatDelta((long long)ms - (long long)pb->ms, d, sizeof(d));
        std::snprintf(line, sizeof(line), "%s  PB %s", t, d);
    }
    const Split* best = findIn(sRec.best, sRec.bestCount, name);
    std::fprintf(stderr, "[Speedrun] split %d \"%s\": %s%s\n", sRunCount, name, line,
                 !best || segmentMs(sRunCount - 1) < best->ms ? "  (best segment)" : "");
}

// "Nombre N", N = veces que aparece en la run (visitas a la zona o a la cueva).
void addVisitSplit(const char* place)
{
    const size_t len = std::strlen(place);
    int visit = 1;
    for (int i = 0; i < sRunCount; i++)
        if (std::strncmp(sRun[i].name, place, len) == 0 && sRun[i].name[len] == ' ') visit++;
    char name[32];
    std::snprintf(name, sizeof(name), "%s %d", place, visit);
    addNamedSplit(name, elapsedMs());
}

void today(char* out, int n)
{
    const time_t t = time(nullptr);
    const struct tm* lt = localtime(&t);
    if (lt) std::strftime(out, n, "%Y-%m-%d", lt);
    else std::snprintf(out, n, "-");
}

// Run completa: mejores tramos, PB si toca y últimas runs.
void finishRun(int days)
{
    loadRecords();
    for (int i = 0; i < sRunCount; i++) {
        const u64 seg = segmentMs(i);
        Split* best = const_cast<Split*>(findIn(sRec.best, sRec.bestCount, sRun[i].name));
        if (best) {
            if (seg < best->ms) best->ms = seg;
        } else if (sRec.bestCount < kMaxBest) {
            sRec.best[sRec.bestCount] = sRun[i];
            sRec.best[sRec.bestCount].ms = seg;
            sRec.bestCount++;
        }
    }
    const u64 total = elapsedMs();
    char date[16];
    today(date, sizeof(date));
    sPrevPbMs = sRec.hasPb ? sRec.pbMs : 0;
    sNewPb = !sRec.hasPb || total < sRec.pbMs;
    if (sNewPb) {
        sRec.hasPb = true;
        sRec.pbMs = total;
        sRec.pbDays = days;
        std::snprintf(sRec.pbDate, sizeof(sRec.pbDate), "%s", date);
        sRec.pbCount = sRunCount;
        for (int i = 0; i < sRunCount; i++) sRec.pb[i] = sRun[i];
    }
    for (int i = std::min(sRec.recentCount, kMaxRecent - 1); i > 0; i--) {
        sRec.recentMs[i] = sRec.recentMs[i - 1];
        sRec.recentDays[i] = sRec.recentDays[i - 1];
        std::memcpy(sRec.recentDate[i], sRec.recentDate[i - 1], 16);
    }
    sRec.recentMs[0] = total;
    sRec.recentDays[0] = days;
    std::snprintf(sRec.recentDate[0], 16, "%s", date);
    sRec.recentCount = std::min(sRec.recentCount + 1, kMaxRecent);
    saveRecords();

    char t[24];
    formatMs(total, t, sizeof(t));
    if (sNewPb && sPrevPbMs) {
        char d[24];
        formatDelta((long long)total - (long long)sPrevPbMs, d, sizeof(d));
        std::fprintf(stderr, "[Speedrun] %s: %s, %d days - new PB (%s)\n", kCategories[category()].name, t, days, d);
    } else if (sNewPb) {
        std::fprintf(stderr, "[Speedrun] %s: %s, %d days - first PB\n", kCategories[category()].name, t, days);
    } else {
        char d[24];
        formatDelta((long long)total - (long long)sPrevPbMs, d, sizeof(d));
        std::fprintf(stderr, "[Speedrun] %s: %s, %d days - PB %s\n", kCategories[category()].name, t, days, d);
    }
}

// TimeSpan de .NET, como lo escribe LiveSplit: "00:05:12.3450000".
std::string lssTime(u64 ms)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%03u0000", unsigned(ms / 3600000), unsigned(ms / 60000 % 60),
                  unsigned(ms / 1000 % 60), unsigned(ms % 1000));
    return buf;
}

std::string xmlEscape(const char* s)
{
    std::string out;
    for (; *s; s++) {
        switch (*s) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break; // Glutton's Kitchen
        default: out += *s;
        }
    }
    return out;
}

// Split de fin de día ("Zona N"), no de cueva ni el final.
bool isDaySplit(const char* name)
{
    for (const char* course : kCourseNames) {
        const size_t len = std::strlen(course);
        if (std::strncmp(name, course, len) == 0 && name[len] == ' ') return true;
    }
    return false;
}

// Tramo del día N en el PB: del fin del día N-1 al fin del día N (o al final
// de la run, si acabó ese día). false si el PB no llega a ese día.
bool pbDaySegment(int day, u64* out)
{
    u64 prevEnd = 0;
    int dayIdx = 0;
    for (int i = 0; i < sRec.pbCount; i++) {
        if (!isDaySplit(sRec.pb[i].name) && i != sRec.pbCount - 1) continue;
        if (++dayIdx == day) {
            *out = sRec.pb[i].ms - prevEnd;
            return true;
        }
        prevEnd = sRec.pb[i].ms;
    }
    return false;
}

std::string practicePath(int day)
{
    return pc_card_speedrun_dir() + "/practice/" + kCategories[category()].slug + "/day" + std::to_string(day);
}

// Cabecera de un punto de práctica: tamaño de los datos de PlayData.
constexpr unsigned kPracticeMagic = 0x50524143; // 'PRAC'

bool practiceExists(int day)
{
    if (day == 1) return true; // partida nueva
    FILE* f = std::fopen(practicePath(day).c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Práctica: el día acaba (o la run, ese día). Un split más y se para.
void finishPractice(const char* name)
{
    sEnd = SDL_GetPerformanceCounter();
    sRunning = false;
    sFinished = true;
    addNamedSplit(name, elapsedMs());
    std::fprintf(stderr, "[Speedrun] practice day %d: %llu ms\n", sPracticeDay, (unsigned long long)elapsedMs());
}

bool challengeCategory()
{
    return category() == PC_SR_CAT_CHALLENGE_LEVELS || category() == PC_SR_CAT_CHALLENGE_TREASURES;
}

void resetRun()
{
    sAwaitingStart = false;
    sChalDone = 0;
    sRunning = false;
    sFinished = false;
    sStart = sEnd = 0;
    sRunCount = 0;
    sNewPb = false;
}

// ─── Atajos de prueba ──────────────────────────────────────────────────────

// Un aviso dura hasta que el juego lo recoge, con caducidad: un fotograma del
// juego puede durar dos refrescos.
constexpr Uint32 kLatchMs = 500;

struct Latch {
    bool on = false;
    Uint32 at = 0;
    void set()
    {
        on = true;
        at = SDL_GetTicks();
    }
    bool take()
    {
        const bool pending = sActive && on && SDL_GetTicks() - at < kLatchMs;
        on = false;
        return pending;
    }
};

Latch sDbgDayEnd, sDbgCaveExit, sDbgFinish, sDbgCaveWarp;

// ─── Reset rápido ──────────────────────────────────────────────────────────
constexpr Uint32 kResetHoldMs = 1000;
Uint32 sResetHeldSince = 0; // 0 = sin pulsar
bool sResetArmed = true;    // no se repite hasta soltar
Latch sResetLatch;          // se ha cumplido el segundo, aún sin recoger
bool sRestartPending = false;

bool resetHeld()
{
    int numKeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numKeys);
    const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    for (const SDL_Scancode k : { pc_window_get_key_binding(PC_KEY_ACT_SPEEDRUN_RESET),
                                  pc_window_get_key_binding2(PC_KEY_ACT_SPEEDRUN_RESET) })
        if (k > SDL_SCANCODE_UNKNOWN && pc_window_binding_held(int(k), keys, mouse)) return true;
    SDL_GameController* ctl = pc_window_get_controller();
    const int bind = pc_window_get_gamepad_binding(PC_KEY_ACT_SPEEDRUN_RESET);
    return ctl && bind >= 0 && pc_window_gamepad_bind_held(ctl, bind);
}

// Una vez por refresco. Solo con una run en marcha o terminada.
void pollReset()
{
    if (!(sRunning || sFinished) || !resetHeld()) {
        sResetHeldSince = 0;
        sResetArmed = true;
        return;
    }
    const Uint32 now = SDL_GetTicks();
    if (!sResetHeldSince) sResetHeldSince = now ? now : 1;
    if (sResetArmed && now - sResetHeldSince >= kResetHoldMs) {
        sResetLatch.set();
        sResetArmed = false;
    }
}

void pollDebug()
{
    static const bool enabled = envFlag("PIKMIN_SR_DEBUG");
    if (!enabled) return;
    static bool prev[4] = {};
    const SDL_Scancode keys[4] = { SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F10 };
    Latch* const latches[4] = { &sDbgDayEnd, &sDbgCaveExit, &sDbgFinish, &sDbgCaveWarp };
    const int names[4] = { 6, 7, 8, 10 };
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    for (int i = 0; i < 4; i++) {
        const bool now = int(keys[i]) < numKeys && state[keys[i]];
        const bool went = now && !prev[i];
        prev[i] = now;
        if (!went) continue;
        std::fprintf(stderr, "[SR debug] F%d (running=%d)\n", names[i], int(sRunning));
        if (sRunning) latches[i]->set();
    }
}

} // namespace

extern "C" {

void pc_speedrun_set_active(int on)
{
    if (sRunning && !sPracticing) pc_livesplit_reset(); // run abandonada
    sActive = on != 0;
    resetRun();
    if (sActive) loadRecords();
}

int pc_speedrun_active(void) { return sActive; }

int pc_speedrun_category(void)
{
    const int c = pc_settings_get_speedrun_category();
    return c >= 0 && c < PC_SR_CAT_COUNT ? c : 0;
}

const char* pc_speedrun_category_name(int c)
{
    return c >= 0 && c < PC_SR_CAT_COUNT ? kCategories[c].name : "";
}

void pc_speedrun_start_timer(void)
{
    if (!sActive) return;
    resetRun();
    loadRecords();
    if (challengeCategory()) {
        // El reloj del Desafío empieza al elegir el primer nivel.
        sAwaitingStart = true;
        sPracticing = false;
        std::fprintf(stderr, "[Speedrun] %s: waiting for the first level\n", kCategories[category()].name);
        return;
    }
    sStart = SDL_GetPerformanceCounter();
    sRunning = true;
    sPracticing = pc_speedrun_practice_day() > 0;
    if (sPracticing) {
        std::fprintf(stderr, "[Speedrun] %s: practice day %d started\n", kCategories[category()].name, sPracticeDay);
    } else {
        pc_livesplit_start();
        std::fprintf(stderr, "[Speedrun] %s: run started\n", kCategories[category()].name);
    }
}

void pc_speedrun_on_day_end(int courseIndex)
{
    if (!sActive || !sRunning) return;
    const char* area = courseIndex >= 0 && courseIndex < 4 ? kCourseNames[courseIndex] : "Area";
    if (sPracticing) {
        char name[32];
        std::snprintf(name, sizeof(name), "%s %d", area, sPracticeDay);
        finishPractice(name);
        return;
    }
    addVisitSplit(area);
}

void pc_speedrun_on_cave_exit(unsigned caveId)
{
    if (!sActive || !sRunning) return;
    const char* name = "Cave";
    for (const CaveName& c : kCaveNames)
        if (c.id == caveId) name = c.name;
    addVisitSplit(name);
}

void pc_speedrun_on_finish(int kind, int days)
{
    if (!sActive || !sRunning) return;
    if (kind >= 0 && kind != kCategories[category()].finish) return;
    if (sPracticing) {
        finishPractice(kCategories[category()].finish == PC_SR_FINISH_DEBT ? "Debt Paid" : "All Treasures");
        return;
    }
    sEnd = SDL_GetPerformanceCounter();
    sRunning = false;
    sFinished = true;
    // El último tramo acaba donde acaba la run.
    addNamedSplit(kCategories[category()].finish == PC_SR_FINISH_DEBT ? "Debt Paid" : "All Treasures",
                  elapsedMs());
    finishRun(days);
}

void pc_speedrun_frame(void)
{
    pc_livesplit_update(sActive && pc_settings_get_speedrun_livesplit(), sRunning, elapsedMs());
    if (!sActive) return;
    pollReset();
    pollDebug();
}

void pc_speedrun_set_practice_day(int day) { sPracticeDay = day >= 0 && day <= kMaxDays ? day : 0; }

int pc_speedrun_practice_day(void) { return challengeCategory() ? 0 : sPracticeDay; }

int pc_speedrun_next_practice_day(int day, int dir)
{
    if (challengeCategory()) return 0; // la práctica es por días de la historia
    // Off, día 1 y los días con punto de práctica, en círculo.
    for (int i = 0; i <= kMaxDays; i++) {
        day = (day + (dir < 0 ? kMaxDays : 1)) % (kMaxDays + 1);
        if (day == 0 || practiceExists(day)) return day;
    }
    return 0;
}

int pc_speedrun_wants_practice_point(void) { return sActive && sRunning && !sPracticing; }

void pc_speedrun_practice_point_save(int day, const void* data, int size)
{
    if (day < 2 || day > kMaxDays || !data || size <= 0) return;
    // save/speedrun ya existe (pc_card_speedrun_dir); faltan practice/<categoría>.
    const std::string practice = pc_card_speedrun_dir() + "/practice";
    for (const std::string& dir : { practice, practice + "/" + kCategories[category()].slug }) {
#ifdef _WIN32
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
    }
    const std::string path = practicePath(day);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const unsigned header[2] = { kPracticeMagic, unsigned(size) };
    const bool ok = std::fwrite(header, sizeof(header), 1, f) == 1 && std::fwrite(data, size_t(size), 1, f) == 1;
    if (std::fclose(f) != 0 || !ok) {
        std::remove(path.c_str());
        return;
    }
    std::fprintf(stderr, "[Speedrun] practice point: day %d\n", day);
}

int pc_speedrun_practice_point_load(int day, void* data, int size)
{
    if (day < 2 || !data || size <= 0) return 0;
    FILE* f = std::fopen(practicePath(day).c_str(), "rb");
    if (!f) return 0;
    unsigned header[2] = {};
    const bool ok = std::fread(header, sizeof(header), 1, f) == 1 && header[0] == kPracticeMagic
                    && header[1] == unsigned(size) && std::fread(data, size_t(size), 1, f) == 1;
    std::fclose(f);
    return ok;
}

int pc_speedrun_category_is_challenge(void) { return challengeCategory(); }

int pc_speedrun_challenge_awaiting(void) { return sActive && sAwaitingStart && challengeCategory(); }

void pc_speedrun_on_challenge_start(int stage)
{
    sChalAllTreasures = true;
    if (!pc_speedrun_challenge_awaiting()) return;
    sAwaitingStart = false;
    sStart = SDL_GetPerformanceCounter();
    sRunning = true;
    pc_livesplit_start();
    std::fprintf(stderr, "[Speedrun] %s: run started on level index %d\n", kCategories[category()].name, stage);
}

void pc_speedrun_on_challenge_floor_end(int remainingTreasures)
{
    if (remainingTreasures > 0) sChalAllTreasures = false;
}

void pc_speedrun_on_challenge_result(int stage, int displayIndex, int cleared, int stageCount)
{
    if (!sActive || !sRunning || !challengeCategory() || stage < 0 || stage >= 64) return;
    const bool counts = category() == PC_SR_CAT_CHALLENGE_TREASURES ? cleared && sChalAllTreasures : cleared;
    const unsigned long long bit = 1ull << stage;
    if (!counts || (sChalDone & bit)) return;
    sChalDone |= bit;
    char name[32];
    std::snprintf(name, sizeof(name), "Level %d", displayIndex + 1);
    int done = 0;
    for (int i = 0; i < stageCount && i < 64; i++) done += int((sChalDone >> i) & 1);
    if (done < stageCount) {
        addNamedSplit(name, elapsedMs());
        return;
    }
    // El último que faltaba: cierra la run.
    sEnd = SDL_GetPerformanceCounter();
    sRunning = false;
    sFinished = true;
    addNamedSplit(name, elapsedMs());
    finishRun(0);
}

int pc_speedrun_take_reset(void)
{
    if (!sResetLatch.take()) return 0;
    std::fprintf(stderr, "[Speedrun] reset\n");
    const bool practicing = sPracticing;
    resetRun();
    if (!practicing) pc_livesplit_reset();
    sRestartPending = true;
    return 1;
}

int pc_speedrun_take_restart(void)
{
    const bool pending = sRestartPending;
    sRestartPending = false;
    return pending && sActive;
}

void pc_speedrun_draw(void)
{
    if (!sActive || (!sRunning && !sFinished && !sAwaitingStart)) return;
    if (!gsys || !gsys->mDGXGfx) return;
    sGfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    const int screenW = sGfx->mScreenWidth, screenH = sGfx->mScreenHeight;
    sScale = screenH / 480.0f;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    Matrix4f ortho;
    sGfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    // Reloj: tiempo real desde la partida nueva; dorado al terminar. Con
    // LiveSplit conectado y "Hide game clock", el reloj y los splits los pone
    // LiveSplit.
    const bool lsEnabled = pc_settings_get_speedrun_livesplit() != 0;
    const bool lsConnected = lsEnabled && pc_livesplit_connected();
    const bool showClock = !(lsConnected && pc_settings_get_speedrun_ls_hide_clock());
    float y = 8.0f;
    if (showClock) {
        char clock[32];
        formatMs(elapsedMs(), clock, sizeof(clock));
        const float fw = 14.0f, fh = 21.0f;
        const int tw = pc_settings_p2d_text_width(clock, px(fw));
        fillRound(8, 8, tw / sScale + 20, fh + 12, 8, kPanel);
        const Colour top = sFinished ? Colour(255, 225, 70, 255) : Colour(225, 255, 255, 255);
        const Colour bottom = sFinished ? Colour(255, 135, 0, 255) : Colour(170, 225, 235, 255);
        pc_settings_p2d_text_styled(px(18), px(13), clock, top, bottom, px(fw), px(fh));
        // Estado de LiveSplit: verde conectado, rojo esperando.
        if (lsEnabled) fillCircle(tw / sScale + 36, 8 + (fh + 12) / 2, 4, lsConnected ? kGreen : kRed);
        y = 46.0f;
    }
    if (sResetHeldSince && sResetArmed) {
        // Barra de "mantener para reiniciar".
        const float t = std::min(1.0f, (SDL_GetTicks() - sResetHeldSince) / float(kResetHoldMs));
        fillRound(8, y, 120, 14, 6, kPanel);
        fillRound(8, y, 120 * t, 14, 6, kDown);
        text(14, y + 1, "Reset", kLabel, 7, 11);
        y += 20.0f;
    }
    if (sAwaitingStart) {
        const char* hint = "Timer starts on the first level";
        pc_settings_p2d_text_styled(px(14), px(y), hint, kLabel, kLabel, px(8), px(12));
        y += 18.0f;
    }
    if (sPracticing) {
        // Práctica: el día y, al acabar, su tiempo contra el mismo día del PB.
        char line[64];
        std::snprintf(line, sizeof(line), "Practice - Day %d", sPracticeDay);
        pc_settings_p2d_text_styled(px(14), px(y), line, kLabel, kLabel, px(9), px(14));
        y += 20.0f;
        if (sFinished) {
            char t[24];
            formatMs(elapsedMs(), t, sizeof(t));
            u64 pbSeg = 0;
            Colour c = kLabel;
            if (pbDaySegment(sPracticeDay, &pbSeg)) {
                char d[24];
                formatDelta((long long)elapsedMs() - (long long)pbSeg, d, sizeof(d));
                std::snprintf(line, sizeof(line), "%s  PB %s", t, d);
                c = elapsedMs() < pbSeg ? kGreen : kRed;
            } else {
                std::snprintf(line, sizeof(line), "%s  (no PB for this day)", t);
            }
            pc_settings_p2d_text_styled(px(14), px(y), line, c, c, px(9), px(14));
            y += 20.0f;
        }
    } else if (sFinished) {
        // Resultado: récord nuevo o diferencia con el anterior.
        char line[48];
        Colour c = kGold;
        if (sNewPb) {
            std::snprintf(line, sizeof(line), "New Personal Best!");
        } else {
            char d[24];
            formatDelta((long long)elapsedMs() - (long long)sRec.pbMs, d, sizeof(d));
            std::snprintf(line, sizeof(line), "PB %s", d);
            c = kRed;
        }
        pc_settings_p2d_text_styled(px(14), px(y), line, c, c, px(9), px(14));
        y += 20.0f;
    }
    const float used = showClock ? drawSplits(8, y) : 0.0f;
    y += used > 0.0f ? used + 6.0f : 0.0f;

    if (envFlag("PIKMIN_SR_DEBUG")) {
        fillRound(8, 452, 400, 18, 6, kPanel);
        text(14, 455, "TEST  F6 day end  F7 cave exit  F8 finish  F10 to cave", kGold, 6, 11);
    }

    // Visor de controles: el mando si es lo último que se ha usado.
#if PIKI_PC_TOUCH
    // Móvil: la capa táctil en vez del teclado y ratón.
    if (pc_touch_visible()) {
        drawTouchPad(12, y + 4);
        return;
    }
#endif
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl && pc_window_last_input_is_gamepad()) drawPad(12, y + 4, ctl);
    else drawKeyboard(14, y + 6);
}

void pc_speedrun_format_time(unsigned long long ms, char* out, int n) { formatMs(ms, out, size_t(n)); }
int pc_speedrun_has_pb(void) { loadRecords(); return sRec.hasPb; }
unsigned long long pc_speedrun_pb_ms(void) { loadRecords(); return sRec.pbMs; }
int pc_speedrun_pb_days(void) { loadRecords(); return sRec.pbDays; }
const char* pc_speedrun_pb_date(void) { loadRecords(); return sRec.pbDate; }
int pc_speedrun_pb_split_count(void) { loadRecords(); return sRec.pbCount; }
const char* pc_speedrun_pb_split_name(int i) { return i >= 0 && i < sRec.pbCount ? sRec.pb[i].name : ""; }
unsigned long long pc_speedrun_pb_split_ms(int i) { return i >= 0 && i < sRec.pbCount ? sRec.pb[i].ms : 0; }

unsigned long long pc_speedrun_sum_of_best(void)
{
    loadRecords();
    u64 sum = 0;
    for (int i = 0; i < sRec.pbCount; i++) {
        const Split* best = findIn(sRec.best, sRec.bestCount, sRec.pb[i].name);
        sum += best ? best->ms : sRec.pb[i].ms - (i > 0 ? sRec.pb[i - 1].ms : 0);
    }
    return sum;
}

int pc_speedrun_recent_count(void) { loadRecords(); return sRec.recentCount; }

void pc_speedrun_recent(int i, unsigned long long* ms, int* days, const char** date)
{
    if (i < 0 || i >= sRec.recentCount) return;
    if (ms) *ms = sRec.recentMs[i];
    if (days) *days = sRec.recentDays[i];
    if (date) *date = sRec.recentDate[i];
}

void pc_speedrun_reset_records(void)
{
    sRec = Records();
    sRecLoaded = true;
    sRecCategory = category();
    std::remove(recordsPath().c_str());
}

int pc_speedrun_export_lss(char* outName, int n)
{
    loadRecords();
    if (!sRec.hasPb || sRec.pbCount == 0) return 0;
    const Category& cat = kCategories[category()];
    const std::string path = pc_card_speedrun_dir() + "/" + cat.lss;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return 0;
    std::fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n  <GameIcon />\n");
    std::fprintf(f, "  <GameName>Pikmin 2</GameName>\n  <CategoryName>%s</CategoryName>\n", xmlEscape(cat.srcName).c_str());
    std::fprintf(f, "  <Metadata>\n    <Run id=\"\" />\n    <Platform usesEmulator=\"False\"></Platform>\n"
                    "    <Region></Region>\n    <Variables />\n  </Metadata>\n");
    std::fprintf(f, "  <Offset>00:00:00</Offset>\n  <AttemptCount>0</AttemptCount>\n  <AttemptHistory />\n");
    std::fprintf(f, "  <Segments>\n");
    for (int i = 0; i < sRec.pbCount; i++) {
        const std::string pb = lssTime(sRec.pb[i].ms);
        std::fprintf(f, "    <Segment>\n      <Name>%s</Name>\n      <Icon />\n      <SplitTimes>\n",
                     xmlEscape(sRec.pb[i].name).c_str());
        std::fprintf(f, "        <SplitTime name=\"Personal Best\">\n");
        std::fprintf(f, "          <RealTime>%s</RealTime>\n          <GameTime>%s</GameTime>\n", pb.c_str(), pb.c_str());
        std::fprintf(f, "        </SplitTime>\n      </SplitTimes>\n");
        if (const Split* best = findIn(sRec.best, sRec.bestCount, sRec.pb[i].name)) {
            const std::string b = lssTime(best->ms);
            std::fprintf(f, "      <BestSegmentTime>\n        <RealTime>%s</RealTime>\n        <GameTime>%s</GameTime>\n"
                            "      </BestSegmentTime>\n", b.c_str(), b.c_str());
        } else {
            std::fprintf(f, "      <BestSegmentTime />\n");
        }
        std::fprintf(f, "      <SegmentHistory />\n    </Segment>\n");
    }
    std::fprintf(f, "  </Segments>\n  <AutoSplitterSettings />\n</Run>\n");
    const bool ok = std::fclose(f) == 0;
    if (ok && outName) std::snprintf(outName, size_t(n), "%s", cat.lss);
    return ok;
}

int pc_speedrun_debug_take_day_end(void) { return sDbgDayEnd.take(); }
int pc_speedrun_debug_take_cave_exit(void) { return sDbgCaveExit.take(); }
int pc_speedrun_debug_take_finish(void) { return sDbgFinish.take(); }
int pc_speedrun_debug_take_cave_warp(void) { return sDbgCaveWarp.take(); }

} // extern "C"
