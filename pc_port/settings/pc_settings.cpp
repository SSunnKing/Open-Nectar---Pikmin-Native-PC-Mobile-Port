/**
 * @file pc_settings.cpp
 * @brief Settings menu for the Pikmin PC port (opened with F1).
 *
 * Fully PC-only. Renders through the game's GX/GL stack so it shares the same
 * visual language as the rest of the game, and persists a small config file so
 * video preferences survive restarts. Video changes are applied immediately but
 * guarded by an on-screen confirm/revert dialog that auto-reverts on timeout.
 *
 * To disable entirely: remove the PIKI_PC_SETTINGS_MENU compile definition and
 * the three hook sites in pc_window.cpp / vi_stubs.cpp, then delete this file.
 */

#include "settings/pc_settings.h"
#include "settings/pc_settings_p2d.h"
#include "pc_menu_repeat.h"
#ifdef __ANDROID__
#include "android/pc_texpack_android.h"
#endif
#include "gl/pc_texpack.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>
#include <atomic>
#include <filesystem>
#include <mutex>

#include "pc_window.h"
#include "pc_permadeath.h"
#if PIKI_PC_TOUCH
#include "touch/pc_touch.h"
#endif
#include "gl/pc_gfx.h"
#include "gl/pc_postprocess.h"
#include "Graphics.h"
#include "Font.h"
#include "Colour.h"
#include "Matrix4f.h"
#include "Geometry.h"
#include "system.h"
#include "types.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kConfigFilename = "pikmin_settings.conf";

struct PcConfig {
    int windowWidth = 1280;
    int windowHeight = 720;
    int displayMode = PC_WINDOW_FULLSCREEN_WINDOWED; // 0 windowed, 1 fullscreen, 2 borderless
    double refreshRate = 0.0;                        // 0 = auto (detect)
    bool vsync = true;                               // presentation pacing on/off
    float renderScale = 2.0f / 3.0f;                 // internal 3D resolution multiplier
    int aspectRatioMode = 0;                         // 0=auto, 1=4:3, 2=16:10, 3=16:9, 4=21:9

    // FPS mode (0=30fps, 1=60fps, 2=120fps experimental)
    int fpsMode = 0;

    // Control mode (0=Classic, 1=Mouse Cursor)
    int controlMode = PC_CONTROL_CLASSIC;

    // Keyboard bindings (scancodes for each action)
    int keyboardBindings[PC_KEY_ACT_COUNT];

    // Mouse sensitivity (0.1 - 5.0, default 1.0)
    float mouseSensitivity = 1.0f;

    // Stick dead zone (0 - 127, default 8)
    int stickDeadZone = 8;

    // Stick inversion flags (bitmask: bit 0=horizontal, bit 1=vertical)
    // 0 = normal, 1 = inverted
    int stickInvert = 0;  // bit 0: X, bit 1: Y
    int cStickInvert = 0; // bit 0: X, bit 1: Y

    // Gamepad button bindings (SDL GameController button IDs)
    int gamepadBindings[PC_KEY_ACT_COUNT];

    // Mod: chain Pikmin actions (0=off/faithful, 1=on).
    // Off by default. The stock behaviour -- finish a job, walk back to the
    // squad -- is what the retail game does; this only changes it on request.
    int chainActions = 0;
    // Hold Extract to keep plucking (0=off/faithful, 1=on). Off by default.
    int holdToPluck = 0;
    // What the mouse wheel does: 0 = pick the Pikmin colour to throw,
    // 1 = zoom the camera. One setting rather than two toggles, so the two
    // uses cannot both be on or both be off.
    int mouseWheelAction = 0;
    // Pikmin allowed on the field at once. The game treats this as a design
    // parameter of its own (AIConstant "p15"), so raising it is supported
    // rather than forced. 100 is the original.
    int pikiLimit = 100;
    // Minutes of play per in-game day, as shown in the menu. 10 is the original.
    int dayMinutes = 10;
    // Colour grading. Neutral by default: the port should look like the game
    // until someone asks otherwise.
    int antialiasing = 0;   // 0 off, 1 FXAA
    int fog = 1;            // the game's own fog, on by default
    int bloom = 0;          // 0 off, 1 subtle, 2 normal, 3 strong
    int ssao = 0;           // 0 off, 1 subtle, 2 normal, 3 strong
    int dof = 0;            // 0 off, 1 subtle, 2 normal, 3 strong
    int anisotropy = 0;     // 0 off, else 2/4/8/16 samples
    int colourGrading = 0;
    float gamma       = 1.0f;
    float brightness  = 0.0f;
    float saturation  = 1.0f;
    // Debug shortcuts (F5/F6). A menu option rather than an environment
    // variable: the launcher starts the game as a child process, so an
    // exported variable does not reliably reach it.
    int debugKeys = 0;
    // Texture pack (PLAN_TEXTURAS_HD fase 2): nombre de carpeta bajo
    // Load/Textures/ que se indexa al arrancar. Se aplica reiniciando: el
    // índice del pack se construye una sola vez, en pc_texpack_init.
    std::string texturePack;
    int texturePackEnabled = 0;

    void applyDefaults() {
        windowWidth = 1280;
        windowHeight = 720;
        displayMode = PC_WINDOW_FULLSCREEN_WINDOWED;
        refreshRate = 0.0;
        vsync = true;
        renderScale = 2.0f / 3.0f;
        aspectRatioMode = 0;
        fpsMode = 0;
        controlMode = PC_CONTROL_CLASSIC;
        mouseSensitivity = 1.0f;
        stickDeadZone = 8;
        stickInvert = 0;
        cStickInvert = 0;
        chainActions = 0;
        holdToPluck = 0;
        mouseWheelAction = 0;
        pikiLimit = 100;
        dayMinutes = 10;
        antialiasing  = 0;
        fog           = 1;
        bloom         = 0;
        ssao          = 0;
        dof           = 0;
        anisotropy    = 0;
        colourGrading = 0;
        gamma         = 1.0f;
        brightness    = 0.0f;
        saturation    = 1.0f;
        debugKeys = 0;
        texturePack.clear();
        texturePackEnabled = 0;
        for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
            keyboardBindings[i] = kDefaultKeyBindings[i];
            gamepadBindings[i] = -1; // -1 = not remapped (use default)
        }
    }
};

PcConfig sConfig;      // the confirmed, persisted settings

namespace {
int menuStickThreshold()
{
	// Gameplay uses a small pad-step dead zone. The F1 list must not: DualSense
	// rest noise and the Linux IMU device sit well above 2048 and looked like
	// a held down. Half throw is a flick, not drift.
	const int threshold = sConfig.stickDeadZone * 256;
	return threshold < 16384 ? 16384 : threshold;
}

bool menuStickVertical(SDL_GameController* c, int sign)
{
	const int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
	const int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
	const int t = menuStickThreshold();
	if (std::abs(y) <= t || std::abs(y) < std::abs(x))
		return false;
	return sign < 0 ? y < 0 : y > 0;
}

bool menuStickHorizontal(SDL_GameController* c, int sign)
{
	const int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
	const int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
	const int t = menuStickThreshold();
	if (std::abs(x) <= t || std::abs(x) < std::abs(y))
		return false;
	return sign < 0 ? x < 0 : x > 0;
}
} // namespace
PcConfig sPending;     // settings staged while editing

// ---------------------------------------------------------------------------
// Menu state
// ---------------------------------------------------------------------------

enum Row {
    ROW_DISPLAY_MODE = 0,
    ROW_RESOLUTION,
    ROW_ASPECT_RATIO,
    ROW_RENDER_SCALE,
    ROW_REFRESH_RATE,
    ROW_VSYNC,
    ROW_FPS_MODE,
#if defined(VERSION_GPIP01)
    // Only the European release carries more than one language. On any other
    // disc the row would be a control with one position.
    ROW_LANGUAGE,
#endif
    ROW_CONTROLS,
    ROW_GAMEPAD,
    ROW_ADVANCED,
    ROW_GRAPHICS,
    ROW_MODS,
    ROW_RESET,
    ROW_SAVE,
    ROW_CLOSE,
    ROW_COUNT,
};

// La lista de resoluciones se construye en ejecucion a partir de lo que el
// monitor declara, en vez de la tabla fija que habia antes (un 4:3 y seis
// 16:9). Aquella dejaba sin ninguna entrada util a los paneles 16:10, 21:9 y a
// los portatiles con tamanos raros: la imagen no se deformaba —el recorte se
// centra con barras en calculate_output_area()— pero se desperdiciaba pantalla,
// y en pantalla completa exclusiva se pedia un modo de video que el monitor
// podia no admitir.
struct Resolution {
    int w;
    int h;
    bool isNative;  // coincide con el modo de escritorio
    bool isDerived; // fraccion de la nativa: vale como ventana, no como modo de video
};

std::vector<Resolution> sResolutions;
int sDesktopW = 0;
int sDesktopH = 0;
bool sHadConfigFile = false;

bool sMenuOpen = false;
int sSelection = ROW_DISPLAY_MODE;
static std::vector<Uint8> gPrevKeys; // previous-frame keyboard state snapshot
// SDL calls the Xbox Select/View button BACK.  Keep its edge independently of
// the keyboard snapshot: settings input is polled from more than one hook per
// frame, and a held button must not reopen the menu after a modal closes.
bool sPrevMenuToggleHeld = false;

// Video confirm/revert dialog state.
bool sVideoConfirmActive = false;
// Reloj de pared, no fotogramas ni sondeos. Antes esto contaba llamadas a
// pc_settings_consume_game_input(), que se invoca desde pc_window_poll_events()
// -- y a esa la llaman DOS sitios por fotograma: el retrazo (vi_stubs) y cada
// lectura del mando (pad_stubs). El contador avanzaba al doble o mas, y los
// "8 segundos" se agotaban en tres o cuatro. Con SDL_GetTicks() el plazo es el
// mismo pase lo que pase con la tasa de refresco o el sondeo del mando.
Uint32 sVideoConfirmStartMs = 0;
constexpr Uint32 kVideoConfirmDurationMs = 8000;

// Lazy font state.
Font* sFont = nullptr;
bool sFontTried = false;

int sResolutionIdx = 0; // se resuelve al construir la lista (ver defaultResolutionIndex)

// Controls submenu state.
bool sInControlsSubmenu = false;
int sControlSelection = 0; // index into PC_KEY_ACT_COUNT
bool sWaitingForKey = false; // true while capturing a new key
// Enter / Space / pad A started capture while still held. Ignore them until
// they are released, otherwise the same press is stored as the new binding.
bool sCaptureWaitRelease = false;

// Gamepad controls submenu state.
bool sInGamepadSubmenu = false;
int sGamepadSelection = 0;
bool sWaitingForButton = false;

// Advanced settings submenu state.
bool sInAdvancedSubmenu = false;
int sAdvancedSelection = 0; // 0=sensitivity, 1=dead zone, 2=stick invert, 3=c-stick invert
constexpr int kAdvancedRowCount = 4;

// Graphics submenu state. Everything here changes how the game *looks* without
// changing how it plays, which is why it is kept apart from Mods: someone
// chasing frame rate and someone chasing fidelity are looking for different
// pages, and neither wants the other's rows in the way.
//
// Every effect is switchable. The port runs on modest hardware -- the
// reference machine is a GTX 1050 -- so nothing here may be mandatory.
bool sInGraphicsSubmenu = false;
int sGraphicsSelection = 0;
constexpr int kGraphicsRowCount = 11;

// Texture packs submenu state (PLAN_TEXTURAS_HD fase 2). La instalación la
// hace el selector de archivos de Android y termina en un hilo Java; el
// resultado llega por pc_texpack_install_finished() y se pinta aquí.
bool sInTexturePacksSubmenu = false;
int sTexturePacksSelection = 0;      // 0 = instalar, 1.. = packs instalados
bool sTexturePackPickerActive = false;  // picker abierto o extracción en curso
std::atomic<int> sTexturePackInstallFiles{0};  // ficheros extraídos (hilo Java)
// Mensaje de la última acción (instalación o aviso de sobremesa).
bool sTexturePackRestartPrompt = false; // modal "reiniciar para aplicar"
constexpr int kTexturePackInstallRow = 0;
std::mutex sTexturePackNoticeMutex;
char sTexturePackNotice[256] = {};
bool sTexturePackNoticeError = false;
Uint32 sTexturePackNoticeMs = 0;
constexpr Uint32 kTexturePackNoticeTimeoutMs = 6000;

void texturePackNotice(bool error, const char* message)
{
    std::lock_guard<std::mutex> lock(sTexturePackNoticeMutex);
    snprintf(sTexturePackNotice, sizeof(sTexturePackNotice), "%s", message ? message : "");
    sTexturePackNoticeError = error;
    sTexturePackNoticeMs = SDL_GetTicks();
}

// Colour grading stops. Neutral is in every list, and the pass is skipped
// entirely when all three sit there.
constexpr float kGammaStops[]      = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.4f, 1.6f };
constexpr int kGammaStopCount      = int(sizeof(kGammaStops) / sizeof(kGammaStops[0]));
constexpr float kBrightnessStops[] = { -0.15f, -0.10f, -0.05f, 0.0f, 0.05f, 0.10f, 0.15f, 0.20f };
constexpr int kBrightnessStopCount = int(sizeof(kBrightnessStops) / sizeof(kBrightnessStops[0]));
constexpr float kSaturationStops[] = { 0.0f, 0.5f, 0.8f, 1.0f, 1.2f, 1.5f, 2.0f };
constexpr int kSaturationStopCount = int(sizeof(kSaturationStops) / sizeof(kSaturationStops[0]));

// Mods submenu state. Everything here changes how the game *plays* rather than
// how it looks or reads input hardware, so it lives apart from the rest: a
// player who wants the original experience only has to leave this one page
// alone.
bool sInModsSubmenu = false;
int sModsSelection = 0;
#if PIKI_DEBUG_KEYS
constexpr int kModsRowCount = 7;
#else
// The debug row is the last one, so leaving it off simply shortens the list.
constexpr int kModsRowCount = 6;
#endif

// Field-limit stops. 100 is what the original game uses.
constexpr int kPikiLimits[]   = { 50, 100, 150, 200, 300, 500, 750, 999 };
constexpr int kPikiLimitCount = int(sizeof(kPikiLimits) / sizeof(kPikiLimits[0]));

// Day length, in real minutes of actual play. The clock's own figure covers a
// full 24-hour cycle, but a day is played from 7am to 7pm -- half of it -- so
// the menu shows the half the player experiences. 10 is the original.
constexpr int kDayMinutes[]    = { 5, 7, 10, 15, 20, 30 };
constexpr int kDayMinutesCount = int(sizeof(kDayMinutes) / sizeof(kDayMinutes[0]));

// Submenu de resolucion. La lista sale del monitor, asi que puede traer veinte
// o cuarenta entradas segun el panel: recorrerlas de una en una con
// izquierda/derecha en la fila principal era inviable. `sResolutionChoices`
// guarda los indices de `sResolutions` validos para el modo de pantalla actual,
// resueltos al abrir, de modo que la lista solo enseña lo que de verdad se
// puede elegir.
bool sInResolutionSubmenu = false;
int sResolutionSubmenuSel = 0;
std::vector<int> sResolutionChoices;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Nombra la forma del panel para que la fila diga de un vistazo si una entrada
// encaja con el monitor. Es justo lo que faltaba cuando la lista era fija.
void aspectLabel(int w, int h, char* out, size_t n) {
    if (w <= 0 || h <= 0) { snprintf(out, n, "?"); return; }
    int a = w, b = h;
    while (b) { const int t = a % b; a = b; b = t; }
    const int rw = w / a, rh = h / a;
    // Nombres comerciales: la reduccion exacta de 16:10 es 8:5 y la de 21:9 es
    // 64:27 o 43:18 segun el panel, pero nadie los reconoce escritos asi.
    if (rw == 8 && rh == 5) { snprintf(out, n, "16:10"); return; }
    if ((rw == 64 && rh == 27) || (rw == 43 && rh == 18)) { snprintf(out, n, "21:9"); return; }
    if (rw == 32 && rh == 9) { snprintf(out, n, "32:9"); return; }
    if (rw <= 64 && rh <= 64) { snprintf(out, n, "%d:%d", rw, rh); return; }
    snprintf(out, n, "%.2f", float(w) / float(h));
}

void addResolution(int w, int h, bool derived) {
    if (w < 320 || h < 240) return;
    for (const Resolution& r : sResolutions) {
        if (r.w == w && r.h == h) return; // deduplicado por tamano: el refresco es otra fila
    }
    sResolutions.push_back({ w, h, w == sDesktopW && h == sDesktopH, derived });
}

void rebuildResolutionList() {
    sResolutions.clear();
    const int display = pc_window_get_display_index();

    SDL_DisplayMode desktop {};
    if (SDL_GetDesktopDisplayMode(display, &desktop) == 0) {
        sDesktopW = desktop.w;
        sDesktopH = desktop.h;
    }

    const int modeCount = SDL_GetNumDisplayModes(display);
    for (int i = 0; i < modeCount; i++) {
        SDL_DisplayMode dm {};
        if (SDL_GetDisplayMode(display, i, &dm) == 0) addResolution(dm.w, dm.h, false);
    }
    if (sDesktopW > 0) addResolution(sDesktopW, sDesktopH, false);

    // Una ventana no tiene por que coincidir con un modo de video, asi que se
    // ofrecen fracciones de la nativa: es la unica forma de tener una ventana
    // pequena con la forma del monitor.
    if (sDesktopW > 0) {
        const float fractions[] = { 0.75f, 2.0f / 3.0f, 0.5f };
        for (float f : fractions) {
            addResolution(int(sDesktopW * f) & ~1, int(sDesktopH * f) & ~1, true);
        }
    }

    if (sResolutions.empty()) addResolution(1280, 720, false); // ultimo recurso

    std::sort(sResolutions.begin(), sResolutions.end(),
              [](const Resolution& a, const Resolution& b) {
                  return a.w != b.w ? a.w > b.w : a.h > b.h;
              });
}

// Pantalla completa exclusiva cambia el modo de video de verdad, asi que solo
// admite modos que el monitor declara. En ventana no tiene sentido ofrecer
// tamanos mayores que el escritorio.
bool resolutionSelectable(const Resolution& r, int displayMode) {
    if (displayMode == PC_WINDOW_FULLSCREEN_EXCLUSIVE) return !r.isDerived;
    if (sDesktopW > 0 && (r.w > sDesktopW || r.h > sDesktopH)) return false;
    return true;
}

int resolutionIndexFor(int w, int h) {
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (sResolutions[i].w == w && sResolutions[i].h == h) return (int)i;
    }
    return -1;
}

void openResolutionSubmenu() {
    sResolutionChoices.clear();
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (resolutionSelectable(sResolutions[i], sPending.displayMode)) {
            sResolutionChoices.push_back((int)i);
        }
    }
    sResolutionSubmenuSel = 0;
    for (size_t k = 0; k < sResolutionChoices.size(); k++) {
        const Resolution& r = sResolutions[sResolutionChoices[k]];
        if (r.w == sPending.windowWidth && r.h == sPending.windowHeight) {
            sResolutionSubmenuSel = (int)k;
            break;
        }
    }
    sInResolutionSubmenu = true;
}

int defaultResolutionIndex() {
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (sResolutions[i].isNative) return (int)i;
    }
    return 0;
}

bool isVideoSettingChanged() {
    return sPending.windowWidth != sConfig.windowWidth ||
           sPending.windowHeight != sConfig.windowHeight ||
           sPending.displayMode != sConfig.displayMode ||
           sPending.vsync != sConfig.vsync ||
           sPending.renderScale != sConfig.renderScale ||
           sPending.aspectRatioMode != sConfig.aspectRatioMode ||
           (fabs(sPending.refreshRate - sConfig.refreshRate) > 0.5);
}

void applyVideo() {
    pc_window_set_display_mode(sPending.displayMode);
    pc_window_set_window_size(sPending.windowWidth, sPending.windowHeight);
    double rate = sPending.refreshRate;
    if (rate <= 0.0) rate = pc_window_get_refresh_rate(); // keep auto/detected
    pc_window_set_refresh_rate(rate);
    pc_window_set_vsync_enabled(sPending.vsync);
    pc_gfx_set_render_scale(sPending.renderScale);
    pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
}

// Pushes the grading settings down to the renderer. The pass decides for
// itself whether it is worth running, so this can be called freely.
void applyGraphics(const PcConfig& config) {
    PcPostEffects fx;
    pc_gfx_set_fog_allowed(config.fog);
    pc_gfx_set_anisotropy(config.anisotropy);
    fx.fxaa          = config.antialiasing != 0;
    // Presets rather than sliders: bloom looks wrong across most of the range
    // a slider would offer, and three named steps are easier to choose between
    // than a number whose good values are not obvious.
    // Occlusion darkens contact points; too much of it turns every crease into
    // a black line, so the strong step is still well short of 1.
    static const float kAoIntensity[4] = { 0.0f, 0.5f, 0.8f, 1.2f };
    static const float kAoRadius[4]    = { 40.0f, 28.0f, 40.0f, 55.0f };
    const int aoStep = (config.ssao >= 0 && config.ssao <= 3) ? config.ssao : 0;
    fx.ssao          = aoStep != 0;
    fx.ssaoDebug     = getenv("PIKMIN_AO_DEBUG") != nullptr;
    fx.ssaoIntensity = kAoIntensity[aoStep];
    fx.ssaoRadius    = kAoRadius[aoStep];

    static const float kBloomIntensity[4] = { 0.0f, 0.35f, 0.6f, 1.0f };
    static const float kBloomThreshold[4] = { 0.75f, 0.80f, 0.72f, 0.62f };
    const int bloomStep = (config.bloom >= 0 && config.bloom <= 3) ? config.bloom : 0;
    fx.bloom           = bloomStep != 0;
    fx.bloomIntensity  = kBloomIntensity[bloomStep];
    fx.bloomThreshold  = kBloomThreshold[bloomStep];
    // Depth of field, focused on the captain. The blur is deliberately much
    // wider than a camera's would be: this is the miniature look of the Link's
    // Awakening remake, where the shallow focus is what makes a world read as
    // a diorama, not an attempt at a real lens.
    //
    // Strength is capped below 1 even at Strong. Mixing the blurred image in
    // completely erases the geometry it came from, and a Pikmin that has walked
    // out of focus still has to be findable on screen.
    static const float kDofStrength[4]  = { 0.0f, 0.55f, 0.75f, 0.92f };
    // The sharp band, as a fraction of the distance to the captain. Wide
    // enough at every step that the Pikmin around him stay readable -- they
    // spread far further from him than a real depth of field would forgive.
    static const float kDofSharp[4]     = { 0.30f, 0.34f, 0.28f, 0.22f };
    // How fast it falls off past that band. Shorter means a more abrupt
    // separation, which is what sells the diorama.
    static const float kDofFalloff[4]   = { 1.00f, 1.10f, 0.80f, 0.55f };
    // Blur passes. Each one widens the kernel; the cost is two half-resolution
    // draws, which is why the strong step is worth measuring on the GTX 1050.
    static const int kDofIterations[4]  = { 1, 1, 2, 3 };
    const int dofStep = (config.dof >= 0 && config.dof <= 3) ? config.dof : 0;
    fx.dof                = dofStep != 0;
    fx.dofStrength        = kDofStrength[dofStep];
    fx.dofSharpFraction   = kDofSharp[dofStep];
    fx.dofFalloffFraction = kDofFalloff[dofStep];
    fx.dofIterations      = kDofIterations[dofStep];

    fx.colourGrading = config.colourGrading != 0;
    fx.gamma         = config.gamma;
    fx.brightness    = config.brightness;
    fx.saturation    = config.saturation;
    pc_gfx_set_post_effects(fx);
}

void applyControls(const PcConfig& config) {
    pc_window_set_control_mode(config.controlMode);
    pc_window_set_mouse_sensitivity(config.mouseSensitivity);
    pc_window_set_stick_dead_zone(config.stickDeadZone);
    pc_window_set_stick_invert(config.stickInvert);
    pc_window_set_cstick_invert(config.cStickInvert);
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        pc_window_set_key_binding(i, static_cast<SDL_Scancode>(config.keyboardBindings[i]));
        pc_window_set_gamepad_binding(i, config.gamepadBindings[i]);
    }
}

void startVideoConfirm() {
    sVideoConfirmStartMs = SDL_GetTicks();
    sVideoConfirmActive = true;
}

void saveConfig(); // defined below

void confirmVideoSettings() {
    sConfig = sPending;
    applyVideo();
    applyControls(sConfig);
    applyGraphics(sConfig);
    saveConfig();
    sVideoConfirmActive = false;
}

// Tras restaurar valores hay que reapuntar el indice: si no, el siguiente
// izquierda/derecha saltaria desde la entrada que se acaba de rechazar.
void syncResolutionIndex() {
    const int idx = resolutionIndexFor(sPending.windowWidth, sPending.windowHeight);
    sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
}

void revertVideoSettings() {
    sPending = sConfig;
    applyVideo();
    syncResolutionIndex();
    sVideoConfirmActive = false;
}

void closeMenu() {
    // Revert any video settings that were not confirmed.
    if (sVideoConfirmActive) {
        revertVideoSettings();
    } else if (isVideoSettingChanged()) {
        sPending = sConfig;
        applyVideo();
        syncResolutionIndex();
    }
    // No dejar el menu memorizado dentro de la lista: al reabrir F1 se espera
    // la pagina principal.
    sInResolutionSubmenu = false;
    sInTexturePacksSubmenu = false;
    sTexturePackRestartPrompt = false;
    sMenuOpen = false;
    pc_window_set_settings_menu_open(false);
}

void resetToDefaults() {
    sConfig.applyDefaults();
    sPending = sConfig;
    applyVideo();
    applyControls(sConfig);
    applyGraphics(sConfig);
    saveConfig();
    sVideoConfirmActive = false;
}

} // namespace

// Defined outside the anonymous namespace and deliberately self-contained: it
// runs during static initialisation, so it cannot rely on sConfig having been
// loaded, or even on this file's own globals having been constructed.
//
// PAL's GamePrefs constructor calls OSGetLanguage() before main(). On MinGW
// that can be before ios_base::Init; std::ifstream / std::string there is a
// crash-at-launch while the USA binary (which never asks) starts fine. Stay
// on getenv/fopen/fgets.
// The language in force, as an OS_LANG_* value. Seeded from the file at boot
// and changed from the F1 menu; saved back on every write.
static unsigned char sLanguage = 0xFF; // 0xFF = not yet seeded

static unsigned char decodeLanguageCode(const char* text, unsigned char fallback)
{
    static const struct {
        char a;
        char b;
        unsigned char value;
    } kCodes[] = {
        { 'e', 'n', 0 }, { 'd', 'e', 1 }, { 'f', 'r', 2 },
        { 'e', 's', 3 }, { 'i', 't', 4 }, { 'n', 'l', 5 },
    };
    if (!text || !text[0] || !text[1])
        return fallback;
    for (unsigned i = 0; i < sizeof(kCodes) / sizeof(kCodes[0]); ++i) {
        if (text[0] == kCodes[i].a && text[1] == kCodes[i].b)
            return kCodes[i].value;
    }
    return fallback;
}

static void trimCString(char* text)
{
    char* start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n'))
        text[--n] = '\0';
}

unsigned char pc_settings_startup_language(void) {
    static unsigned char language = 0;
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        language = 0;
        if (const char* fromEnvironment = getenv("NECTAR_LANGUAGE")) {
            language = decodeLanguageCode(fromEnvironment, 0);
        } else if (FILE* in = fopen(kConfigFilename, "r")) {
            char line[512];
            while (fgets(line, sizeof(line), in)) {
                char* equals = strchr(line, '=');
                if (!equals)
                    continue;
                *equals = '\0';
                char* key = line;
                char* value = equals + 1;
                trimCString(key);
                trimCString(value);
                if (strcmp(key, "language") == 0) {
                    language = decodeLanguageCode(value, 0);
                    break;
                }
            }
            fclose(in);
        }
    }
    if (sLanguage == 0xFF)
        sLanguage = language;
    return language;
}

unsigned char pc_settings_get_language(void) {
    if (sLanguage == 0xFF) pc_settings_startup_language();
    return sLanguage;
}

void pc_settings_set_language(unsigned char language) {
    sLanguage = (language < 6) ? language : 0;
}

namespace {

void saveConfig() {
    std::string path = std::string(kConfigFilename);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        printf("[PC Settings] Failed to write %s\n", path.c_str());
        return;
    }
    out << "# Open Nectar settings (F1 in-game to change)\n";
    out << "windowWidth = " << sConfig.windowWidth << "\n";
    out << "windowHeight = " << sConfig.windowHeight << "\n";
    out << "displayMode = " << sConfig.displayMode << "\n";
    out << "aspectRatioMode = " << sConfig.aspectRatioMode << "\n";
    out << "refreshRate = " << sConfig.refreshRate << "\n";
    out << "vsync = " << (sConfig.vsync ? 1 : 0) << "\n";
    {
        // Written back so the key survives a save from the F1 menu. The value
        // is whatever pc_settings_startup_language() resolved at boot: this is
        // the installer's choice, and nothing in the game changes it yet.
        static const char* const kCodes[] = { "en", "de", "fr", "es", "it", "nl" };
        out << "language = " << kCodes[pc_settings_get_language()] << "\n";
    }
    out << "renderScale = " << sConfig.renderScale << "\n";
    out << "fpsMode = " << sConfig.fpsMode << "\n";
    out << "chainActions = " << sConfig.chainActions << "\n";
    out << "holdToPluck = " << sConfig.holdToPluck << "\n";
    out << "mouseWheelAction = " << sConfig.mouseWheelAction << "\n";
    out << "pikiLimit = " << sConfig.pikiLimit << "\n";
    out << "dayMinutes = " << sConfig.dayMinutes << "\n";
    out << "antialiasing = " << sConfig.antialiasing << "\n";
    out << "fog = " << sConfig.fog << "\n";
    out << "bloom = " << sConfig.bloom << "\n";
    out << "ssao = " << sConfig.ssao << "\n";
    out << "dof = " << sConfig.dof << "\n";
    out << "anisotropy = " << sConfig.anisotropy << "\n";
    out << "colourGrading = " << sConfig.colourGrading << "\n";
    out << "gamma = " << sConfig.gamma << "\n";
    out << "brightness = " << sConfig.brightness << "\n";
    out << "saturation = " << sConfig.saturation << "\n";
    out << "debugKeys = " << sConfig.debugKeys << "\n";
    out << "texturePack = " << sConfig.texturePack << "\n";
    out << "texturePackEnabled = " << sConfig.texturePackEnabled << "\n";
    out << "controlMode = " << sConfig.controlMode << "\n";
    out << "mouseSensitivity = " << sConfig.mouseSensitivity << "\n";
    out << "stickDeadZone = " << sConfig.stickDeadZone << "\n";
    out << "stickInvert = " << sConfig.stickInvert << "\n";
    out << "cStickInvert = " << sConfig.cStickInvert << "\n";
    // Keyboard bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "key_" << i << " = " << sConfig.keyboardBindings[i] << "\n";
    }
    // Gamepad bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "gp_" << i << " = " << sConfig.gamepadBindings[i] << "\n";
    }
    out.close();
    printf("[PC Settings] Saved %s\n", path.c_str());
}

void loadConfig() {
    sConfig.applyDefaults();
    std::string path = std::string(kConfigFilename);
    std::ifstream in(path, std::ios::in);
    if (!in) {
        printf("[PC Settings] No config file (%s); using defaults.\n", path.c_str());
        sHadConfigFile = false;
        return;
    }
    sHadConfigFile = true;
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        size_t ka = key.find_first_not_of(" \t");
        size_t kb = key.find_last_not_of(" \t");
        key = key.substr(ka, kb - ka + 1);
        size_t va = val.find_first_not_of(" \t");
        size_t vb = val.find_last_not_of(" \t\r\n");
        val = (vb == std::string::npos) ? "" : val.substr(va, vb - va + 1);

        if (key == "windowWidth") sConfig.windowWidth = atoi(val.c_str());
        else if (key == "windowHeight") sConfig.windowHeight = atoi(val.c_str());
        else if (key == "displayMode") sConfig.displayMode = atoi(val.c_str());
        else if (key == "refreshRate") sConfig.refreshRate = atof(val.c_str());
        else if (key == "vsync") sConfig.vsync = atoi(val.c_str()) != 0;
        else if (key == "renderScale") {
            sConfig.renderScale = (float)atof(val.c_str());
            if (sConfig.renderScale < 0.25f || sConfig.renderScale > 4.0f) sConfig.renderScale = 2.0f / 3.0f;
        }
        else if (key == "controlMode") {
            sConfig.controlMode = atoi(val.c_str());
            if (sConfig.controlMode < PC_CONTROL_CLASSIC || sConfig.controlMode > PC_CONTROL_MOUSE_CURSOR) {
                sConfig.controlMode = PC_CONTROL_CLASSIC;
            }
        }
        else if (key == "mouseSensitivity") {
            sConfig.mouseSensitivity = (float)atof(val.c_str());
            if (sConfig.mouseSensitivity < 0.1f) sConfig.mouseSensitivity = 0.1f;
            if (sConfig.mouseSensitivity > 5.0f) sConfig.mouseSensitivity = 5.0f;
        }
        else if (key == "stickDeadZone") {
            sConfig.stickDeadZone = atoi(val.c_str());
            if (sConfig.stickDeadZone < 0) sConfig.stickDeadZone = 0;
            if (sConfig.stickDeadZone > 127) sConfig.stickDeadZone = 127;
        }
        else if (key == "aspectRatioMode") {
            sConfig.aspectRatioMode = atoi(val.c_str());
            if (sConfig.aspectRatioMode < 0) sConfig.aspectRatioMode = 0;
            if (sConfig.aspectRatioMode > 4) sConfig.aspectRatioMode = 4;
        }
        else if (key == "fpsMode") {
            sConfig.fpsMode = atoi(val.c_str());
            if (sConfig.fpsMode < 0) sConfig.fpsMode = 0;
            if (sConfig.fpsMode > 2) sConfig.fpsMode = 2;
        }
        else if (key == "chainActions") {
            sConfig.chainActions = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "holdToPluck") {
            sConfig.holdToPluck = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "mouseWheelAction") {
            sConfig.mouseWheelAction = atoi(val.c_str());
            if (sConfig.mouseWheelAction < 0 || sConfig.mouseWheelAction > 1) sConfig.mouseWheelAction = 0;
        }
        else if (key == "pikiLimit") {
            sConfig.pikiLimit = atoi(val.c_str());
            if (sConfig.pikiLimit < 50 || sConfig.pikiLimit > 999) sConfig.pikiLimit = 100;
        }
        else if (key == "anisotropy") {
            const int v = atoi(val.c_str());
            sConfig.anisotropy = (v == 2 || v == 4 || v == 8 || v == 16) ? v : 0;
        }
        else if (key == "dof") {
            sConfig.dof = atoi(val.c_str());
            if (sConfig.dof < 0 || sConfig.dof > 3) sConfig.dof = 0;
        }
        else if (key == "ssao") {
            sConfig.ssao = atoi(val.c_str());
            if (sConfig.ssao < 0 || sConfig.ssao > 3) sConfig.ssao = 0;
        }
        else if (key == "bloom") {
            sConfig.bloom = atoi(val.c_str());
            if (sConfig.bloom < 0 || sConfig.bloom > 3) sConfig.bloom = 0;
        }
        else if (key == "fog") {
            sConfig.fog = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "antialiasing") {
            sConfig.antialiasing = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "colourGrading") {
            sConfig.colourGrading = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "gamma") {
            sConfig.gamma = (float)atof(val.c_str());
            if (!(sConfig.gamma >= 0.5f && sConfig.gamma <= 2.0f)) sConfig.gamma = 1.0f;
        }
        else if (key == "brightness") {
            sConfig.brightness = (float)atof(val.c_str());
            if (!(sConfig.brightness >= -0.5f && sConfig.brightness <= 0.5f)) sConfig.brightness = 0.0f;
        }
        else if (key == "saturation") {
            sConfig.saturation = (float)atof(val.c_str());
            if (!(sConfig.saturation >= 0.0f && sConfig.saturation <= 2.0f)) sConfig.saturation = 1.0f;
        }
        else if (key == "dayMinutes") {
            sConfig.dayMinutes = atoi(val.c_str());
            if (sConfig.dayMinutes < 1 || sConfig.dayMinutes > 120) sConfig.dayMinutes = 10;
        }
        else if (key == "debugKeys") {
            sConfig.debugKeys = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "texturePack") {
            const bool safe = !val.empty() && val.size() < 64
                && val.find('/') == std::string::npos
                && val.find('\\') == std::string::npos
                && val.find("..") == std::string::npos;
            sConfig.texturePack = safe ? val : std::string();
        }
        else if (key == "texturePackEnabled") {
            sConfig.texturePackEnabled = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "stickInvert") sConfig.stickInvert = atoi(val.c_str()) & 3;
        else if (key == "cStickInvert") sConfig.cStickInvert = atoi(val.c_str()) & 3;
        else if (key.rfind("key_", 0) == 0) {
            int idx = atoi(key.substr(4).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int scancode = atoi(val.c_str());
                if (scancode >= 0 && scancode < SDL_NUM_SCANCODES) {
                    sConfig.keyboardBindings[idx] = scancode;
                }
            }
        }
        else if (key.rfind("gp_", 0) == 0) {
            int idx = atoi(key.substr(3).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int button = atoi(val.c_str());
                const bool isButton = button >= -1 && button < SDL_CONTROLLER_BUTTON_MAX;
                const int axis = (button - PC_GP_AXIS_BIND) / 2;
                const bool isAxis = button >= PC_GP_AXIS_BIND && axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX;
                if (isButton || isAxis) {
                    sConfig.gamepadBindings[idx] = button;
                }
            }
        }
    }
    in.close();
    if (sConfig.windowWidth <= 0) sConfig.windowWidth = 1280;
    if (sConfig.windowHeight <= 0) sConfig.windowHeight = 720;
    printf("[PC Settings] Loaded %s: %dx%d mode=%d vsync=%d refresh=%.0f\n",
           path.c_str(), sConfig.windowWidth, sConfig.windowHeight,
           sConfig.displayMode, sConfig.vsync ? 1 : 0, sConfig.refreshRate);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

namespace {
static u16 sTouchButtons = 0;
static u16 sTouchFrameButtons = 0;
/// Menu input repeat lives in pc_menu_repeat so its timing can be tested
/// without a controller and without waiting. See that header.
bool padEdge(bool pressed, int slot) { return pc_menu_edge(pressed, slot, SDL_GetTicks()); }

/// Stick threshold for menus. Follows the configured dead zone, which a fixed
/// 8000 used to ignore -- so changing the setting appeared to do nothing.
int menuStickThreshold();
bool menuStickVertical(SDL_GameController* c, int sign);
bool menuStickHorizontal(SDL_GameController* c, int sign);

bool padNavUp(SDL_GameController* c)
{
	return padEdge((c && (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP)
	                       || menuStickVertical(c, -1))) || (sTouchFrameButtons & PAD_BUTTON_UP), 0);
}
bool padNavDown(SDL_GameController* c)
{
	return padEdge((c && (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
	                   || menuStickVertical(c, 1))) || (sTouchFrameButtons & PAD_BUTTON_DOWN), 1);
}
bool padNavLeft(SDL_GameController* c)
{
	return padEdge((c && (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT)
	                   || menuStickHorizontal(c, -1))) || (sTouchFrameButtons & PAD_BUTTON_LEFT), 2);
}
bool padNavRight(SDL_GameController* c)
{
	return padEdge((c && (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
	                   || menuStickHorizontal(c, 1))) || (sTouchFrameButtons & PAD_BUTTON_RIGHT), 3);
}
bool padNavA(SDL_GameController* c) { return padEdge((c && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)) || (sTouchFrameButtons & PAD_BUTTON_A), 4); }
bool padNavB(SDL_GameController* c) { return padEdge((c && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B)) || (sTouchFrameButtons & PAD_BUTTON_B), 5); }

// The new-game prompt is a game-facing dialog rather than the F1 settings
// menu. Its accept/cancel actions follow the configured A/B bindings, which
// may be either SDL buttons or the axis encodings used by the remapping page.
// Keep these separate from padNavA/B: F1 navigation deliberately retains its
// physical A/B convention.
bool promptPadBinding(SDL_GameController* c, int action, int edgeSlot)
{
	return c && padEdge(pc_window_gamepad_bind_held(c, pc_window_get_gamepad_binding(action)), edgeSlot);
}

bool promptPadA(SDL_GameController* c)
{
	return promptPadBinding(c, PC_KEY_ACT_A, 4);
}

bool promptPadB(SDL_GameController* c)
{
	return promptPadBinding(c, PC_KEY_ACT_B, 5);
}

bool captureConfirmHeld(SDL_GameController* ctl)
{
	int numKeys = 0;
	const Uint8* keys = SDL_GetKeyboardState(&numKeys);
	if (SDL_SCANCODE_RETURN < numKeys && keys[SDL_SCANCODE_RETURN])
		return true;
	if (SDL_SCANCODE_SPACE < numKeys && keys[SDL_SCANCODE_SPACE])
		return true;
	return ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A);
}

bool isCaptureModifierScancode(int sc)
{
	return sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL || sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT
	    || sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_LGUI || sc == SDL_SCANCODE_RGUI;
}
} // namespace

bool keyWentDown(SDL_Scancode sc) {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    bool now = (int)sc < numKeys && state[sc] != 0;
    bool prev = (int)sc < (int)gPrevKeys.size() && gPrevKeys[sc] != 0;
    return now && !prev;
}

void latchKeys() {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    if (numKeys < 0) numKeys = 0;
    gPrevKeys.assign(state, state + numKeys);
}

// Defined further down, with the rest of the prompt. Declared here because the
// prompt has to read input from inside this function: keys are latched right
// after it returns, so anything polling later in the frame sees no edges.
void pcNewGamePromptInput();

static bool sToggleRequested = false;
static bool sTouchTapPending = false;
static float sTouchTapX = 0.0f, sTouchTapY = 0.0f;

void pollMenuInput() {
    sTouchFrameButtons = sTouchButtons;
    sTouchButtons = 0;
    SDL_GameController* ctl = pc_window_get_controller();
    const bool toggleRequested = sToggleRequested;
    sToggleRequested = false;
    const bool menuToggleHeld = ctl && SDL_GameControllerGetButton(
        ctl, SDL_CONTROLLER_BUTTON_BACK) != 0;
    const bool menuTogglePressed = menuToggleHeld && !sPrevMenuToggleHeld;
    // Latch before every modal early return.  A Select press used while the
    // new-game/video/capture modal owns input must not become a fresh press
    // when that modal exits while the button is still held.
    sPrevMenuToggleHeld = menuToggleHeld;

    if (pc_newgame_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        // The prompt owns input while it is up, including F1: opening the
        // settings menu over a modal that is deciding a save file's rules
        // would leave two menus fighting for the same keys.
        pcNewGamePromptInput();
        return;
    }

    auto openMenu = [] {
        sPending = sConfig;
        sPending.controlMode = pc_window_get_control_mode();
        sMenuOpen = true;
        pc_window_set_settings_menu_open(true);
        sSelection = ROW_DISPLAY_MODE;
        sVideoConfirmActive = false;
        rebuildResolutionList();
        const int idx = resolutionIndexFor(pc_window_get_width(), pc_window_get_height());
        sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
    };

    // F1 always toggles. Select/View can open the menu while it is closed;
    // closing is handled after video confirmation has had first refusal.
    if (keyWentDown(SDL_SCANCODE_F1) || toggleRequested) {
        if (sMenuOpen) closeMenu();
        else openMenu();
        return;
    }
    if (menuTogglePressed && !sMenuOpen) {
        openMenu();
        return;
    }

    if (!sMenuOpen) {
        // No arrastrar a un menú futuro un toque hecho durante el juego.
        sTouchTapPending = false;
        sTouchButtons = 0;
        return;
    }

#if PIKI_PC_TOUCH
    pc_touch_claim_port_menu();
#endif

    // Modal video-confirm dialog.
    if (sVideoConfirmActive) {
        // Auto-revert on timeout. La resta sin signo se comporta bien cuando
        // SDL_GetTicks() da la vuelta.
        const Uint32 elapsed = SDL_GetTicks() - sVideoConfirmStartMs;
        if (elapsed >= kVideoConfirmDurationMs) {
            revertVideoSettings();
            return;
        }
        if (keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE) ||
            keyWentDown(SDL_SCANCODE_J) ||
            padNavA(ctl)) {
            confirmVideoSettings();
        } else if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                   keyWentDown(SDL_SCANCODE_B) ||
                   padNavB(ctl)) {
            revertVideoSettings();
        }
        return;
    }

    // Select/View closes the menu from ordinary pages and submenus.  Capture
    // owns the button while waiting for a binding (including the short
    // wait-release period after accepting one), so it can be assigned or
    // released without toggling the menu underneath.
    if (menuTogglePressed && !sWaitingForKey && !sWaitingForButton &&
        !sCaptureWaitRelease) {
        closeMenu();
        return;
    }

    // Controls submenu (key capture mode).
    if (sInControlsSubmenu) {
        if (sWaitingForKey) {
            if (keyWentDown(SDL_SCANCODE_ESCAPE) || padNavB(ctl)) {
                sWaitingForKey = false;
                sCaptureWaitRelease = false;
                return;
            }
            if (sCaptureWaitRelease) {
                if (!captureConfirmHeld(ctl))
                    sCaptureWaitRelease = false;
                return;
            }
            for (int sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
                if (!keyWentDown(static_cast<SDL_Scancode>(sc)))
                    continue;
                if (isCaptureModifierScancode(sc))
                    continue;
                sPending.keyboardBindings[sControlSelection] = sc;
                sWaitingForKey = false;
                break;
            }
            return;
        }

        // Navigation in controls list.
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl))
                up = true;
            if (padNavDown(ctl))
                down = true;
            if (padNavLeft(ctl))
                left = true;
            if (padNavRight(ctl))
                right = true;
            if (padNavA(ctl))
                ok = true;
        }

        if (up) {
            sControlSelection = (sControlSelection + PC_KEY_ACT_COUNT - 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (down) {
            sControlSelection = (sControlSelection + 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (ok) {
            sWaitingForKey = true;
            sCaptureWaitRelease = true;
            return;
        }
        if (left || right) {
            // Reset to default on left/right.
            sPending.keyboardBindings[sControlSelection] = kDefaultKeyBindings[sControlSelection];
            return;
        }
        // B / ESC exits submenu.
        if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
            keyWentDown(SDL_SCANCODE_B) || padNavB(ctl)) {
            sInControlsSubmenu = false;
            sWaitingForKey = false;
            sCaptureWaitRelease = false;
        }
        return;
    }

    // Gamepad submenu (button capture mode).
    if (sInGamepadSubmenu) {
        if (sWaitingForButton) {
            // B/Circle is a bindable face button. Only Esc cancels capture.
            if (keyWentDown(SDL_SCANCODE_ESCAPE)) {
                sWaitingForButton = false;
                sCaptureWaitRelease = false;
                return;
            }
            if (sCaptureWaitRelease) {
                if (!captureConfirmHeld(ctl))
                    sCaptureWaitRelease = false;
                return;
            }
            if (ctl || sTouchFrameButtons) {
                const int bind = pc_window_gamepad_first_held_binding(ctl);
                if (bind >= 0) {
                    sPending.gamepadBindings[sGamepadSelection] = bind;
                    sWaitingForButton = false;
                    sCaptureWaitRelease = true;
                }
            }
            return;
        }

        // The button just bound is still held; do not treat it as Back.
        if (sCaptureWaitRelease) {
            if (!pc_window_gamepad_any_held(ctl) && !captureConfirmHeld(ctl))
                sCaptureWaitRelease = false;
            return;
        }

        // Navigation in gamepad list.
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl))
                up = true;
            if (padNavDown(ctl))
                down = true;
            if (padNavLeft(ctl))
                left = true;
            if (padNavRight(ctl))
                right = true;
            if (padNavA(ctl))
                ok = true;
        }

        if (up) {
            sGamepadSelection = (sGamepadSelection + PC_KEY_ACT_COUNT - 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (down) {
            sGamepadSelection = (sGamepadSelection + 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (ok) {
            sWaitingForButton = true;
            sCaptureWaitRelease = true;
            return;
        }
        if (left || right) {
            sPending.gamepadBindings[sGamepadSelection] = -1;
            return;
        }
        if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
            keyWentDown(SDL_SCANCODE_B) || padNavB(ctl)) {
            sInGamepadSubmenu = false;
            sWaitingForButton = false;
            sCaptureWaitRelease = false;
        }
        return;
    }

    // Advanced settings submenu.
    if (sInAdvancedSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl))
                up = true;
            if (padNavDown(ctl))
                down = true;
            if (padNavLeft(ctl))
                left = true;
            if (padNavRight(ctl))
                right = true;
            if (padNavA(ctl))
                ok = true;
            if (padNavB(ctl))
                cancel = true;
        }

        if (up) {
            sAdvancedSelection = (sAdvancedSelection + kAdvancedRowCount - 1) % kAdvancedRowCount;
            return;
        }
        if (down) {
            sAdvancedSelection = (sAdvancedSelection + 1) % kAdvancedRowCount;
            return;
        }
        if (cancel) {
            sInAdvancedSubmenu = false;
            return;
        }

        // Sensitivity (0.1 - 5.0, step 0.1)
        if (sAdvancedSelection == 0) {
            float step = 0.1f;
            if (left) sPending.mouseSensitivity = fmaxf(0.1f, sPending.mouseSensitivity - step);
            if (right) sPending.mouseSensitivity = fminf(5.0f, sPending.mouseSensitivity + step);
        }
        // Stick dead zone (0 - 127, step 4)
        else if (sAdvancedSelection == 1) {
            int step = 4;
            if (left) sPending.stickDeadZone = std::max(0, sPending.stickDeadZone - step);
            if (right) sPending.stickDeadZone = std::min(127, sPending.stickDeadZone + step);
        }
        // Stick invert (bitmask)
        else if (sAdvancedSelection == 2) {
            if (left || right) {
                sPending.stickInvert ^= 3; // toggle X and Y bits
            }
        }
        // C-stick invert (bitmask)
        else if (sAdvancedSelection == 3) {
            if (left || right) {
                sPending.cStickInvert ^= 3; // toggle X and Y bits
            }
        }
        return;
    }

    // Resolution submenu.
    if (sInResolutionSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl))
                up = true;
            if (padNavDown(ctl))
                down = true;
            if (padNavA(ctl))
                ok = true;
            if (padNavB(ctl))
                cancel = true;
        }

        const int choiceCount = (int)sResolutionChoices.size();
        if (cancel || choiceCount == 0) {
            sInResolutionSubmenu = false;
            return;
        }
        if (up) {
            sResolutionSubmenuSel = (sResolutionSubmenuSel + choiceCount - 1) % choiceCount;
            return;
        }
        if (down) {
            sResolutionSubmenuSel = (sResolutionSubmenuSel + 1) % choiceCount;
            return;
        }
        if (ok) {
            const int idx = sResolutionChoices[sResolutionSubmenuSel];
            sResolutionIdx = idx;
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            // Cerrar antes de aplicar: el dialogo de confirmacion se dibuja
            // sobre el menu principal y tiene prioridad sobre los submenus.
            sInResolutionSubmenu = false;
            applyVideo();
            startVideoConfirm();
        }
        return;
    }

    // Texture packs submenu. The restart prompt owns input while it is up:
    // activating a pack asks for a restart, and the choice must not leak into
    // the pack list underneath as a stray press.
    if (sTexturePackRestartPrompt) {
        bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);
        if (ctl || sTouchFrameButtons) {
            if (padNavA(ctl)) accept = true;
            if (padNavB(ctl)) cancel = true;
        }
        // Esc also never leaves a confirm dialog answered.
        if (accept && !cancel) {
            sTexturePackRestartPrompt = false;
#ifdef __ANDROID__
            pc_texpack_android_restart();
#else
            texturePackNotice(true, "Packs de texturas activos. Reinicia el juego para aplicarlos.");
            sInTexturePacksSubmenu = false;
#endif
        } else if (cancel) {
            sTexturePackRestartPrompt = false;
        }
        return;
    }

    if (sInTexturePacksSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl)) up = true;
            if (padNavDown(ctl)) down = true;
            if (padNavLeft(ctl)) left = true;
            if (padNavRight(ctl)) right = true;
            if (padNavA(ctl)) ok = true;
            if (padNavB(ctl)) cancel = true;
        }

        // Lista de packs desde el disco: muestra una instalación recién hecha
        // sin reiniciar. La fila 0 es "instalar"; las siguientes son packs.
        std::vector<std::string> packs = pc_texpack_list_packs();
        const int rowCount = 1 + static_cast<int>(packs.size());
        if (sTexturePacksSelection >= rowCount) sTexturePacksSelection = rowCount - 1;

        if (up) { sTexturePacksSelection = (sTexturePacksSelection + rowCount - 1) % rowCount; return; }
        if (down) { sTexturePacksSelection = (sTexturePacksSelection + 1) % rowCount; return; }
        if (cancel) { sInTexturePacksSubmenu = false; return; }

        if (sTexturePacksSelection == kTexturePackInstallRow) {
            if ((ok || left || right) && !sTexturePackPickerActive) {
#ifdef __ANDROID__
                sTexturePackPickerActive = true;
                pc_texpack_android_open_picker();
#else
                texturePackNotice(true,
                    "En escritorio, descomprime el pack en Load/Textures/ y reinicia. (En Android se elige el .zip/.rar aquí.)");
#endif
            }
            return;
        }

        const int packIndex = sTexturePacksSelection - kTexturePackInstallRow - 1;
        if (packIndex >= 0 && packIndex < static_cast<int>(packs.size())) {
            const std::string folder = packs[packIndex];
            const bool active = sConfig.texturePackEnabled && folder == sConfig.texturePack;
            if ((ok || left || right) && sTexturePackPickerActive) {
                // Con el zip a medio extraer, activar y reiniciar mataría la
                // instalación: es justo lo que dejaba packs con 126 ficheros
                // de 3000 y el menú diciendo "Active".
                texturePackNotice(true, "Wait: the pack is still being installed.");
            } else if (ok || left || right) {
                if (active) {
                    // Retirar el pack: también requiere reinicio para reconstruir
                    // el índice sin él, pero no merece un modal: ya está visible
                    // en marcha, solo seguirá indexándolo hasta el próximo arranque.
                    sConfig.texturePackEnabled = 0;
                    sConfig.texturePack.clear();
                    sPending.texturePackEnabled = 0;
                    sPending.texturePack.clear();
                    saveConfig();
                    texturePackNotice(false, "Pack desactivado. Se aplica al reiniciar.");
                } else {
                    sConfig.texturePack = folder;
                    sConfig.texturePackEnabled = 1;
                    sPending.texturePack = folder;
                    sPending.texturePackEnabled = 1;
                    saveConfig();
                    sTexturePackRestartPrompt = true;
                }
            }
        }
        return;
    }

    // Graphics submenu.
    if (sInGraphicsSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl)) up = true;
            if (padNavDown(ctl)) down = true;
            if (padNavLeft(ctl)) left = true;
            if (padNavRight(ctl)) right = true;
            if (padNavA(ctl)) ok = true;
            if (padNavB(ctl)) cancel = true;
        }

        if (up) {
            sGraphicsSelection = (sGraphicsSelection + kGraphicsRowCount - 1) % kGraphicsRowCount;
            return;
        }
        if (down) {
            sGraphicsSelection = (sGraphicsSelection + 1) % kGraphicsRowCount;
            return;
        }
        if (cancel) {
            sInGraphicsSubmenu = false;
            return;
        }

        // Texture packs lives in the Graphics page: it is a rendering choice,
        // it needs restarting to take effect, and it is opened rather than
        // cycled so the install entry has room next to the pack list.
        if (sGraphicsSelection == 10) {
            if (ok) {
                sInTexturePacksSubmenu = true;
                sTexturePacksSelection = 0;
            }
            return;
        }

        // Stepped through meaningful values rather than one hundredth at a
        // time: the menu repeats slowly on purpose, and a fine slider would
        // take hundreds of presses to cross the range.
        auto step = [](float current, const float* stops, int count, bool back) {
            int idx = 0;
            for (int i = 0; i < count; i++) {
                if (stops[i] == current) { idx = i; break; }
            }
            idx = back ? (idx + count - 1) % count : (idx + 1) % count;
            return stops[idx];
        };

        if (sGraphicsSelection == 0) {
            if (left || right) sPending.antialiasing = sPending.antialiasing ? 0 : 1;
        } else if (sGraphicsSelection == 1) {
            if (left || right) sPending.fog = sPending.fog ? 0 : 1;
        } else if (sGraphicsSelection == 2) {
            if (left) sPending.bloom = (sPending.bloom + 3) % 4;
            else if (right) sPending.bloom = (sPending.bloom + 1) % 4;
        } else if (sGraphicsSelection == 3) {
            if (left) sPending.ssao = (sPending.ssao + 3) % 4;
            else if (right) sPending.ssao = (sPending.ssao + 1) % 4;
        } else if (sGraphicsSelection == 4) {
            if (left) sPending.dof = (sPending.dof + 3) % 4;
            else if (right) sPending.dof = (sPending.dof + 1) % 4;
        } else if (sGraphicsSelection == 5) {
            // 0, 2, 4, 8, 16. Anything the driver will not give is clamped
            // where it is applied rather than hidden from the menu, so the
            // setting reads the same on every machine.
            static const int kAniso[5] = { 0, 2, 4, 8, 16 };
            int idx = 0;
            for (int i = 0; i < 5; i++) {
                if (kAniso[i] == sPending.anisotropy) { idx = i; break; }
            }
            if (left) idx = (idx + 4) % 5;
            else if (right) idx = (idx + 1) % 5;
            sPending.anisotropy = kAniso[idx];
        } else if (sGraphicsSelection == 6) {
            if (left || right) sPending.colourGrading = sPending.colourGrading ? 0 : 1;
        } else if (sGraphicsSelection == 7) {
            if (left || right) sPending.gamma = step(sPending.gamma, kGammaStops, kGammaStopCount, left);
        } else if (sGraphicsSelection == 8) {
            if (left || right) sPending.brightness = step(sPending.brightness, kBrightnessStops, kBrightnessStopCount, left);
        } else if (sGraphicsSelection == 9) {
            if (left || right) sPending.saturation = step(sPending.saturation, kSaturationStops, kSaturationStopCount, left);
        }
        // Applied as you move, so the effect can be judged against the scene
        // behind the menu instead of by reading numbers.
        applyGraphics(sPending);
        return;
    }

    // Mods submenu.
    if (sInModsSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl))
                up = true;
            if (padNavDown(ctl))
                down = true;
            if (padNavLeft(ctl))
                left = true;
            if (padNavRight(ctl))
                right = true;
            if (padNavB(ctl))
                cancel = true;
        }

        if (up) {
            sModsSelection = (sModsSelection + kModsRowCount - 1) % kModsRowCount;
            return;
        }
        if (down) {
            sModsSelection = (sModsSelection + 1) % kModsRowCount;
            return;
        }
        if (cancel) {
            sInModsSubmenu = false;
            return;
        }

        // Control scheme: Classic (GameCube) or Mouse Cursor.
        if (sModsSelection == 0) {
            if (left) sPending.controlMode = (sPending.controlMode - 1 + 2) % 2;
            else if (right) sPending.controlMode = (sPending.controlMode + 1) % 2;
        }
        // Chain Pikmin actions.
        else if (sModsSelection == 1) {
            if (left || right) sPending.chainActions = sPending.chainActions ? 0 : 1;
        }
        // Hold Extract to keep plucking after the first sprout.
        else if (sModsSelection == 2) {
            if (left || right) sPending.holdToPluck = sPending.holdToPluck ? 0 : 1;
        }
        // What the mouse wheel controls.
        else if (sModsSelection == 3) {
            if (left || right) sPending.mouseWheelAction = sPending.mouseWheelAction ? 0 : 1;
        }
        // Pikmin field limit. Stepped through meaningful values rather than one
        // at a time: the menu has no key repeat, so a fine slider would take
        // hundreds of presses to cross the range.
        else if (sModsSelection == 4) {
            if (pc_hardmode_active())
                return;
            int idx = 0;
            for (int i = 0; i < kPikiLimitCount; i++) {
                if (kPikiLimits[i] == sPending.pikiLimit) { idx = i; break; }
            }
            if (left) idx = (idx + kPikiLimitCount - 1) % kPikiLimitCount;
            else if (right) idx = (idx + 1) % kPikiLimitCount;
            sPending.pikiLimit = kPikiLimits[idx];
        }
        // Day length.
        else if (sModsSelection == 5) {
            if (pc_hardmode_active())
                return;
            int idx = 0;
            for (int i = 0; i < kDayMinutesCount; i++) {
                if (kDayMinutes[i] == sPending.dayMinutes) { idx = i; break; }
            }
            if (left) idx = (idx + kDayMinutesCount - 1) % kDayMinutesCount;
            else if (right) idx = (idx + 1) % kDayMinutesCount;
            sPending.dayMinutes = kDayMinutes[idx];
        }
        // Debug shortcuts.
        else if (sModsSelection == 6) {
            if (left || right) sPending.debugKeys = sPending.debugKeys ? 0 : 1;
        }
        return;
    }

    // Main menu navigation (existing logic below).
    bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                  keyWentDown(SDL_SCANCODE_B);

    // Los botones táctiles sintetizados llegan después de este poll en el
    // frame que los genera; se guardan y se consumen aquí en el siguiente.
    const u16 touchButtons = sTouchFrameButtons;
    up |= (touchButtons & PAD_BUTTON_UP) != 0;
    down |= (touchButtons & PAD_BUTTON_DOWN) != 0;
    left |= (touchButtons & PAD_BUTTON_LEFT) != 0;
    right |= (touchButtons & PAD_BUTTON_RIGHT) != 0;
    ok |= (touchButtons & PAD_BUTTON_A) != 0;
    cancel |= (touchButtons & PAD_BUTTON_B) != 0;

    if (sTouchTapPending) {
        sTouchTapPending = false;
        // El panel F1 está en un lienzo lógico 640x480 centrado en la ventana.
        int dw = 0, dh = 0;
        pc_gfx_get_drawable_size(&dw, &dh);
        const float aspect = dh > 0 ? float(dw) / float(dh) : 4.0f / 3.0f;
        const float logicalX = sTouchTapX * aspect * 480.0f
                             - (aspect * 480.0f - 640.0f) * 0.5f;
        const float logicalY = sTouchTapY * 480.0f;
        const int rowH = pc_settings_p2d_active() ? 20 : 18;
        const int panelY = pc_settings_p2d_active() ? 52 : 64;
        const int row = int((logicalY - (panelY + 34 + 12)) / float(rowH));
        if (logicalX >= 74.0f && logicalX <= 566.0f && row >= 0 && row < ROW_COUNT) {
            sSelection = row;
            // Tocar una fila de valor avanza su valor; tocar una acción o
            // submenú equivale a A. Así el texto visible es el control.
            if (row < ROW_CONTROLS) right = true;
            else ok = true;
        }
    }

    if (ctl || sTouchFrameButtons) {
        if (padNavUp(ctl))
            up = true;
        if (padNavDown(ctl))
            down = true;
        if (padNavLeft(ctl))
            left = true;
        if (padNavRight(ctl))
            right = true;
        if (padNavA(ctl))
            ok = true;
        if (padNavB(ctl))
            cancel = true;
    }

    if (up) {
        sSelection = (sSelection + ROW_COUNT - 1) % ROW_COUNT;
        return;
    }
    if (down) {
        sSelection = (sSelection + 1) % ROW_COUNT;
        return;
    }

    // Left/Right/OK/Cancel handling via switch.
    auto cycleResolution = [](int dir) {
        // Borderless siempre usa el escritorio, asi que la fila no se toca.
        if (sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS) return;
        const int n = (int)sResolutions.size();
        if (n == 0) return;
        for (int step = 1; step <= n; step++) {
            const int idx = ((sResolutionIdx + dir * step) % n + n) % n;
            if (!resolutionSelectable(sResolutions[idx], sPending.displayMode)) continue;
            sResolutionIdx = idx;
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            return;
        }
    };

    switch (sSelection) {
    case ROW_DISPLAY_MODE:
        if (left) sPending.displayMode = (sPending.displayMode + 3 - 1) % 3;
        else if (right) sPending.displayMode = (sPending.displayMode + 1) % 3;
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    case ROW_RESOLUTION:
        // Enter abre la lista completa; izquierda/derecha sigue sirviendo para
        // un salto rapido a la entrada contigua.
        if (ok && sPending.displayMode != PC_WINDOW_FULLSCREEN_BORDERLESS) {
            openResolutionSubmenu();
            break;
        }
        if (left) cycleResolution(-1);
        else if (right) cycleResolution(1);
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    case ROW_ASPECT_RATIO:
        if (left) sPending.aspectRatioMode = (sPending.aspectRatioMode + 5 - 1) % 5;
        else if (right) sPending.aspectRatioMode = (sPending.aspectRatioMode + 1) % 5;
        if (left || right) {
            pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
            applyVideo();
            startVideoConfirm();
        }
        break;
    case ROW_RENDER_SCALE: {
        static const float kScales[] = { 2.0f / 3.0f, 0.5f, 1.0f, 1.5f, 2.0f };
        constexpr int kScaleCount = 5;
        int idx = 0;
        float cur = sPending.renderScale;
        for (int i = 0; i < kScaleCount; i++) {
            if (fabsf(kScales[i] - cur) < 0.01f) { idx = i; break; }
        }
        if (left) idx = (idx + kScaleCount - 1) % kScaleCount;
        else if (right) idx = (idx + 1) % kScaleCount;
        sPending.renderScale = kScales[idx];
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    }
    case ROW_REFRESH_RATE: {
        static const double kRates[] = { 0.0, 60.0, 120.0, 144.0, 165.0, 240.0 };
        constexpr int kRateCount = 6;
        double cur = sPending.refreshRate;
        int idx = 0;
        for (int i = 0; i < kRateCount; i++) {
            if (fabs(kRates[i] - cur) < 0.5) { idx = i; break; }
        }
        if (left) idx = (idx + kRateCount - 1) % kRateCount;
        else if (right) idx = (idx + 1) % kRateCount;
        sPending.refreshRate = kRates[idx];
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    }
    case ROW_VSYNC:
        if (left || right || ok) {
            sPending.vsync = !sPending.vsync;
            applyVideo();
            startVideoConfirm();
        }
        break;
    case ROW_FPS_MODE:
        if (left) {
            sPending.fpsMode = (sPending.fpsMode - 1 + 3) % 3;
        } else if (right) {
            sPending.fpsMode = (sPending.fpsMode + 1) % 3;
        }
        break;
#if defined(VERSION_GPIP01)
    case ROW_LANGUAGE: {
        // Only the five the European disc actually carries. Dutch exists in the
        // hardware's list and not on the disc, so offering it would point the
        // game at files that are not there.
        const int kCount = 5;
        int language = pc_settings_get_language();
        if (language >= kCount) language = 0;
        if (left) language = (language + kCount - 1) % kCount;
        else if (right) language = (language + 1) % kCount;
        pc_settings_set_language((unsigned char)language);
        break;
    }
#endif
    case ROW_CONTROLS:
        if (ok) {
            sInControlsSubmenu = true;
            sControlSelection = 0;
            sWaitingForKey = false;
            sCaptureWaitRelease = false;
        }
        break;
    case ROW_GAMEPAD:
        if (ok) {
            sInGamepadSubmenu = true;
            sGamepadSelection = 0;
            sWaitingForButton = false;
            sCaptureWaitRelease = false;
        }
        break;
    case ROW_ADVANCED:
        if (ok) {
            sInAdvancedSubmenu = true;
            sAdvancedSelection = 0;
        }
        break;
    case ROW_GRAPHICS:
        if (ok) {
            sInGraphicsSubmenu = true;
            sGraphicsSelection = 0;
        }
        break;
    case ROW_MODS:
        if (ok) {
            sInModsSubmenu = true;
            sModsSelection = 0;
        }
        break;
    case ROW_RESET:
        if (ok) resetToDefaults();
        break;
    case ROW_SAVE:
        if (ok) {
            // Save pending changes (confirming video if applicable).
            if (sVideoConfirmActive) {
                confirmVideoSettings();
            } else {
                sConfig = sPending;
                applyVideo();
                applyControls(sConfig);
                applyGraphics(sConfig);
                saveConfig();
            }
        }
        break;
    case ROW_CLOSE:
        if (ok) closeMenu();
        break;
    default:
        break;
    }

    // Esc / B closes the menu (reverting unconfirmed changes).
    if (cancel) {
        closeMenu();
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

int gDrawCursorX = 0;
int gDrawCursorY = 0;

void ensureFont() {
    if (sFontTried) return;
    sFontTried = true;
    if (gsys) {
        const int previousHeap = gsys->getHeapNum();
        gsys->setHeap(SYSHEAP_Sys);
        sFont = new Font;
        Texture* tex = gsys->loadTexture("consFont.bti", true);
        if (tex) {
            sFont->setTexture(tex, 16, 8);
        } else {
            delete sFont;
            sFont = nullptr;
        }
        gsys->setHeap(previousHeap);
    }
}

int menuTextWidth(const char* text) {
    return pc_settings_p2d_active() ? pc_settings_p2d_text_width(text) : sFont->stringWidth(text);
}

void drawText(const char* fmt, ...) {
    char buf[512];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);
    static_cast<DGXGraphics*>(gsys->mDGXGfx)->texturePrintf(sFont, gDrawCursorX, gDrawCursorY, buf);
}

Colour lerpColour(Colour a, Colour b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Colour(int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t),
                 int(a.b + (b.b - a.b) * t), int(a.a + (b.a - a.a) * t));
}

// Filled rounded rectangle (all 4 corners radius r) built from 2px horizontal
// strips. Each strip's colour is lerped top->bottom to fake a vertical gradient.
void fillRoundRectGrad(DGXGraphics* gfx, int x, int y, int w, int h, int r,
                       Colour top, Colour bottom) {
    if (w <= 0 || h <= 0) return;
    if (pc_settings_p2d_active()) {
        // Native selection is a game cursor plus yellow text.
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    const int step = 2;
    for (int yy = y; yy < y + h; yy += step) {
        int bandH = step;
        if (yy + bandH > y + h) bandH = y + h - yy;
        int t = yy - y; // 0..h
        // horizontal inset from the rounded corners
        int inset = 0;
        int lo = (t < r) ? t : (t > h - r ? h - t : -1);
        if (lo >= 0) {
            int dy = r - lo;                    // vertical distance to corner-circle center line
            int half = (int)floorf(sqrtf((float)(r * r - dy * dy)));
            inset = r - half;
        }
        Colour c = lerpColour(top, bottom, (float)(yy - y) / (float)h);
        gfx->setColour(c, true);
        gfx->setAuxColour(c);
        gfx->fillRectangle(RectArea(x + inset, yy, x + w - inset, yy + bandH));
    }
}

// Text with a heavy dark outline (drawn offset in shadow colour, then main).
void drawTextOutline(int x, int y, const char* fmt, Colour main, Colour shadow, ...) {
    char buf[512];
    va_list vl;
    va_start(vl, shadow);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);

    if (pc_settings_p2d_active()) {
        pc_settings_p2d_text(x, y, buf, main);
        return;
    }
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    gfx->setColour(shadow, true);
    gfx->setAuxColour(shadow);
    for (int ox = -2; ox <= 2; ox++) {
        for (int oy = -1; oy <= 1; oy++) {
            gDrawCursorX = x + ox; gDrawCursorY = y + oy;
            drawText("%s", buf);
        }
    }
    gfx->setColour(main, true);
    gfx->setAuxColour(main);
    gDrawCursorX = x; gDrawCursorY = y;
    drawText("%s", buf);
}

void drawPikminPanel(DGXGraphics* gfx, int x, int y, int w, int h, int radius) {
    if (pc_settings_p2d_active()) {
        pc_settings_p2d_plate(x, y, w, h, 0);
        pc_settings_p2d_plate(x, y, w, h, 1);
        return;
    }
    // Soft offset shadow, then the broad silver/black bezel used throughout
    // Pikmin's menus. Layering rounded fills keeps this independent of assets.
    fillRoundRectGrad(gfx, x + 7, y + 9, w, h, radius,
                      Colour(0, 0, 0, 150), Colour(0, 0, 0, 220));
    fillRoundRectGrad(gfx, x, y, w, h, radius,
                      Colour(225, 232, 242, 245), Colour(54, 58, 66, 255));
    fillRoundRectGrad(gfx, x + 3, y + 4, w - 6, h - 8, radius - 3,
                      Colour(32, 34, 40, 255), Colour(3, 4, 7, 255));
    fillRoundRectGrad(gfx, x + 9, y + 10, w - 18, h - 20, radius - 8,
                      Colour(41, 49, 83, 248), Colour(12, 17, 35, 252));

    // Reflected strip along the upper inner edge.
    fillRoundRectGrad(gfx, x + 18, y + 12, w - 36, 16, 9,
                      Colour(255, 255, 255, 76), Colour(128, 151, 196, 4));
}

void drawPikminHeader(DGXGraphics* gfx, int panelX, int panelY, int panelW,
                      const char* title) {
    int titleW = menuTextWidth(title);
    if (pc_settings_p2d_active()) {
        pc_settings_p2d_plate(panelX + (panelW - 250) / 2, panelY - 18, 250, 52, 1);
        const int nativeTitleW = pc_settings_p2d_text_width(title, 16);
        pc_settings_p2d_text(panelX + (panelW - nativeTitleW) / 2, panelY - 2, title, Colour(218,255,255,255), 16, 24);
        return;
    }
    int w = titleW + 92;
    if (w < 250) w = 250;
    if (w > panelW - 70) w = panelW - 70;
    int x = panelX + (panelW - w) / 2;
    int y = panelY - 18;

    fillRoundRectGrad(gfx, x + 5, y + 7, w, 52, 18,
                      Colour(0, 0, 0, 130), Colour(0, 0, 0, 210));
    fillRoundRectGrad(gfx, x, y, w, 52, 18,
                      Colour(224, 230, 238, 220), Colour(66, 70, 78, 245));
    fillRoundRectGrad(gfx, x + 3, y + 4, w - 6, 44, 15,
                      Colour(20, 22, 27, 248), Colour(2, 3, 5, 252));
    fillRoundRectGrad(gfx, x + 10, y + 8, w - 20, 16, 10,
                      Colour(255, 255, 255, 92), Colour(255, 255, 255, 2));

    drawTextOutline(panelX + panelW / 2 - titleW / 2, y + 20, "%s",
                    Colour(218, 255, 255, 255), Colour(0, 8, 12, 255), title);
}

void drawSubmenuSurface(DGXGraphics* gfx, int x, int y, int w, int h,
                        const char* title, const char* helpTop,
                        const char* helpBottom) {
    if (pc_settings_p2d_active()) {
        // A submenu replaces the parent page; translucent native plates must
        // not reveal a second list of settings underneath.
        pc_settings_p2d_clear();
        pc_settings_p2d_plate(x, y, w, h, 0);
        pc_settings_p2d_plate(x, y, w, h, 1);
        pc_settings_p2d_text(x + (w - pc_settings_p2d_text_width(title, 14))/2, y+10, title, Colour(255,207,75,255), 14, 20);
        pc_settings_p2d_text(x + (w-pc_settings_p2d_text_width(helpTop, 10))/2, y+h-39, helpTop, Colour(205,239,250,255), 10, 15);
        pc_settings_p2d_text(x + (w-pc_settings_p2d_text_width(helpBottom, 10))/2, y+h-23, helpBottom, Colour(205,239,250,255), 10, 15);
        return;
    }
    // Opaque surface: the parent settings must not remain legible through a
    // child page. The old translucent rectangle caused both lists to overlap.
    fillRoundRectGrad(gfx, x, y, w, h, 14,
                      Colour(5, 7, 12, 255), Colour(0, 1, 4, 255));
    fillRoundRectGrad(gfx, x + 4, y + 4, w - 8, 30, 11,
                      Colour(74, 84, 112, 255), Colour(18, 23, 40, 255));
    int titleX = x + w / 2 - menuTextWidth(title) / 2;
    drawTextOutline(titleX, y + 12, "%s", Colour(255, 207, 75, 255),
                    Colour(49, 20, 0, 255), title);

    int helpY = y + h - 39;
    drawTextOutline(x + w / 2 - menuTextWidth(helpTop) / 2, helpY,
                    "%s", Colour(205, 239, 250, 255), Colour(0, 8, 13, 255), helpTop);
    drawTextOutline(x + w / 2 - menuTextWidth(helpBottom) / 2, helpY + 16,
                    "%s", Colour(205, 239, 250, 255), Colour(0, 8, 13, 255), helpBottom);
}

void drawSubmenuRow(DGXGraphics* gfx, int x, int y, int w,
                    const char* label, const char* value, bool selected) {
    if (selected) {
        fillRoundRectGrad(gfx, x, y - 3, w, 22, 8,
                          Colour(58, 51, 31, 235), Colour(7, 7, 8, 245));
        drawTextOutline(x + 10, y, ">", Colour(255, 229, 120, 255),
                        Colour(48, 18, 0, 255));
    }
    Colour main = selected ? Colour(255, 190, 28, 255) : Colour(185, 237, 255, 255);
    Colour shadow = selected ? Colour(62, 25, 0, 255) : Colour(0, 9, 15, 255);
    const int split = x + w / 2;
    drawTextOutline(split - 14 - menuTextWidth(label), y, "%s",
                    main, shadow, label);
    drawTextOutline(split + 14, y, "%s", main, shadow, value);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void pc_settings_request_toggle(void) {
    sToggleRequested = true;
}

void pc_settings_touch_buttons(unsigned short pressed) {
    sTouchButtons |= pressed;
}

void pc_settings_touch_tap(float x, float y) {
    sTouchTapX = x;
    sTouchTapY = y;
    sTouchTapPending = true;
}

void pc_settings_init(void) {
    loadConfig();
    sPending = sConfig;
    rebuildResolutionList();
    // Sin fichero previo, arrancar a la resolucion del monitor en vez de a un
    // 1280x720 fijo que en un panel 16:10 o 21:9 deja barras desde el principio.
    int idx = sHadConfigFile ? resolutionIndexFor(sPending.windowWidth, sPending.windowHeight) : -1;
    if (idx < 0) {
        idx = defaultResolutionIndex();
        if (!sResolutions.empty()) {
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            sConfig.windowWidth = sPending.windowWidth;
            sConfig.windowHeight = sPending.windowHeight;
        }
    }
    sResolutionIdx = idx;
    // Apply persisted display settings at startup.
    pc_window_set_vsync_enabled(sPending.vsync);
    pc_window_set_display_mode(sPending.displayMode);
    pc_window_set_window_size(sPending.windowWidth, sPending.windowHeight);
    if (sPending.refreshRate > 0.0) pc_window_set_refresh_rate(sPending.refreshRate);
    pc_gfx_set_render_scale(sPending.renderScale);
    pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
    applyControls(sConfig);
    applyGraphics(sConfig);
    // PLAN_TEXTURAS_HD fase 2: si hay un pack activo, solicitar su
    // indexación antes de que pc_texpack_init() construya el mapa en
    // pc_gfx_init(). pc_texpack_select_pack() se llama aquí para que el
    // menú pueda mostrar la carpeta activa sin depender del orden entre
    // pc_settings_init y el arranque de GL.
    if (sConfig.texturePackEnabled && !sConfig.texturePack.empty()) {
        pc_texpack_select_pack(sConfig.texturePack.c_str());
        pc_texpack_request_enable();
        printf("[PC Settings] Texture pack active: %s\n", sConfig.texturePack.c_str());
    }
    printf("[PC Settings] Init complete.\n");
}

bool pc_settings_consume_game_input(void) {
    const bool promptWasOpen = pc_newgame_prompt_active();
    pollMenuInput();   // edge-detect using the previous frame's snapshot
    latchKeys();       // snapshot AFTER polling so next frame sees this one
    // Swallow the frame the prompt closes on too, or the button that dismissed
    // it reaches the screen underneath as a fresh press.
    return sMenuOpen || promptWasOpen;
}

void pc_settings_apply_video(void) {
    if (!sVideoConfirmActive && isVideoSettingChanged()) {
        applyVideo();
    }
}

bool pc_settings_has_pending_video(void) {
    return sVideoConfirmActive;
}

// Llega desde el hilo Java que instaló el pack (selector F1 → Android).
// Guarda el resultado para que el submenú de packs lo pinte; el picker se
// considera cerrado y el pack aparece en la lista en el siguiente dibujo.
void pc_texpack_install_progress(int files) {
    sTexturePackInstallFiles.store(files);
}

void pc_texpack_install_finished(bool ok, const char* message) {
    sTexturePackPickerActive = false;
    sTexturePackInstallFiles.store(0);
    texturePackNotice(!ok, message ? message : (ok ? "Pack instalado." : "No se pudo instalar el pack."));
}

// ─── Permadeath badge on the file-select screen ───
//
// The file screen is BLO data and its panes carry no colour the port can
// change -- P2DPaneLibrary offers alpha and mirroring, nothing else -- so the
// mark is drawn over the slot rather than tinting it. Coordinates arrive in
// the 640x480 space the BLO screens use, and are scaled to the window here.

void pc_permadeath_draw_slot_badge(int vx, int vy, int vw)
{
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const int x = vx * screenW / 640;
    const int y = vy * screenH / 480;
    const int w = vw * screenW / 640;

    const char* label = "PERMADEATH";
    const int textW  = menuTextWidth(label);
    const int padX   = 16;
    const int badgeW = textW + padX * 2;
    const int badgeH = 26;
    const int badgeX = x + w / 2 - badgeW / 2;
    const int radius = badgeH / 2;   // a capsule, like the screen's own plates

    // Everything on this screen glows rather than having edges, so the mark
    // fades outward instead of carrying a border. Three passes, each wider and
    // fainter, approximate the falloff.
    for (int i = 3; i >= 1; i--) {
        const int grow = i * 5;
        const u8 alpha = (u8)(26 - i * 6);
        fillRoundRectGrad(gfx, badgeX - grow, y - grow,
                          badgeW + grow * 2, badgeH + grow * 2,
                          radius + grow,
                          Colour(255, 70, 70, alpha), Colour(180, 20, 30, alpha));
    }

    // Pale rim, then the plate itself: the glass look here is a light edge
    // around a darker translucent body, not a drawn outline.
    fillRoundRectGrad(gfx, badgeX - 2, y - 2, badgeW + 4, badgeH + 4, radius + 2,
                      Colour(255, 190, 190, 150), Colour(120, 30, 40, 130));
    fillRoundRectGrad(gfx, badgeX, y, badgeW, badgeH, radius,
                      Colour(196, 44, 52, 214), Colour(74, 6, 14, 224));

    // Reflected strip along the upper inner edge, the same trick the port's
    // panels use to read as glass.
    fillRoundRectGrad(gfx, badgeX + 6, y + 3, badgeW - 12, badgeH / 2 - 2,
                      (badgeH / 2 - 2) / 2,
                      Colour(255, 255, 255, 70), Colour(255, 200, 200, 6));

    drawTextOutline(badgeX + padX, y + 6, "%s",
                    Colour(255, 240, 240, 255), Colour(50, 0, 6, 255), label);
}

// ─── New-game permadeath prompt ───
//
// Shown by the file-select section when a run is about to be created, so the
// choice belongs to the file rather than to the port's configuration. It is
// drawn with the same native P2D toolkit as F1. The explanatory strings stay
// in the port; the original message archives and save-file rules are unchanged.

namespace {
bool sNewGamePromptOpen = false;
int  sNewGamePromptStep = 0;     // 0 = normal/permadeath, 1 = difficulty
int  sNewGamePromptChoice = 0;   // current step: 0 = left option, 1 = right
int  sNewGamePromptRules = 0;    // 0 = normal file, 1 = permadeath
int  sNewGamePromptResult = PC_NEWGAME_PENDING;
bool sNewGamePromptHard = false;
}

void pc_newgame_prompt_open(void) {
    sNewGamePromptOpen   = true;
    sNewGamePromptStep   = 0;
    sNewGamePromptChoice = 0;
    sNewGamePromptRules  = 0;
    sNewGamePromptResult = PC_NEWGAME_PENDING;
    sNewGamePromptHard   = false;
    pc_menu_edge_reset();
}

bool pc_newgame_prompt_active(void) { return sNewGamePromptOpen; }

int pc_newgame_prompt_result(void) { return sNewGamePromptResult; }

bool pc_newgame_prompt_chose_hard(void) { return sNewGamePromptHard; }

namespace {
void pcNewGamePromptInput() {
    if (!sNewGamePromptOpen) return;

    bool left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE);

    left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    accept |= (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    cancel |= (sTouchFrameButtons & PAD_BUTTON_B) != 0;

    if (sTouchTapPending) {
        sTouchTapPending = false;
        int dw = 0, dh = 0;
        pc_gfx_get_drawable_size(&dw, &dh);
        const float aspect = dh > 0 ? float(dw) / float(dh) : 4.0f / 3.0f;
        const float screenW = aspect * 480.0f;
        const float x = sTouchTapX * screenW;
        const float y = sTouchTapY * 480.0f;
        const float panelX = screenW * 0.5f - 310.0f;
        const float panelY = 110.0f;
        const float optY = panelY + 108.0f;
        for (int i = 0; i < 2; ++i) {
            const float boxX = panelX + 40.0f + i * 270.0f;
            if (x >= boxX && x <= boxX + 230.0f && y >= optY - 8.0f && y <= optY + 52.0f) {
                sNewGamePromptChoice = i;
                accept = true;
                break;
            }
        }
    }

    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (padNavLeft(ctl))  left   = true;
        if (padNavRight(ctl)) right  = true;
        if (promptPadA(ctl))  accept = true;
        if (promptPadB(ctl))  cancel = true;
    }

    if (left || right) sNewGamePromptChoice = sNewGamePromptChoice ? 0 : 1;

    if (accept) {
        if (sNewGamePromptStep == 0) {
            sNewGamePromptRules  = sNewGamePromptChoice;
            sNewGamePromptStep   = 1;
            sNewGamePromptChoice = 0;
            pc_menu_edge_reset();
            return;
        }
        sNewGamePromptHard   = sNewGamePromptChoice != 0;
        sNewGamePromptResult = sNewGamePromptRules ? PC_NEWGAME_PERMADEATH
                                                   : PC_NEWGAME_NORMAL;
        sNewGamePromptOpen   = false;
    } else if (cancel) {
        if (sNewGamePromptStep == 1) {
            sNewGamePromptStep   = 0;
            sNewGamePromptChoice = sNewGamePromptRules;
            pc_menu_edge_reset();
            return;
        }
        sNewGamePromptResult = PC_NEWGAME_CANCELLED;
        sNewGamePromptOpen   = false;
    }
}

} // namespace

void pc_newgame_prompt_draw(void) {
    if (!sNewGamePromptOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    gfx->setColour(Colour(0, 0, 0, 170), true);
    gfx->setAuxColour(Colour(0, 0, 0, 170));
    gfx->fillRectangle(RectArea(0, 0, screenW, screenH));

    const int panelW = 620;
    const int panelH = 260;
    const int panelX = screenW / 2 - panelW / 2;
    const int panelY = screenH / 2 - panelH / 2;

    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "New Game");

    const bool difficultyStep = sNewGamePromptStep != 0;
    const char* line1 = difficultyStep ? "How hard should this file be?"
                                       : "How should this file play?";
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(line1) / 2, panelY + 62,
                    "%s", Colour(214, 224, 245, 255), Colour(8, 12, 28, 255), line1);

    const char* options[2] = { "Normal", difficultyStep ? "Hard" : "Permadeath" };
    const int optY = panelY + 108;
    for (int i = 0; i < 2; i++) {
        const bool sel = (i == sNewGamePromptChoice);
        const int boxW = 230;
        const int boxX = panelX + 40 + i * (boxW + 40);
        if (pc_settings_p2d_active()) {
            if (sel) pc_settings_p2d_plate(boxX, optY, boxW, 44, 2);
            pc_settings_p2d_plate(boxX, optY, boxW, 44, 1);
            if (sel) pc_settings_p2d_text(boxX + 16, optY + 12, ">", Colour(255,229,120,255));
        } else {
            gfx->setColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220), true);
            gfx->setAuxColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220));
            gfx->fillRectangle(RectArea(boxX, optY, boxX + boxW, optY + 40));
        }
        const int tw = menuTextWidth(options[i]);
        drawTextOutline(boxX + boxW / 2 - tw / 2, optY + 12, "%s",
                        sel ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                        Colour(8, 12, 28, 255), options[i]);
    }

    // Say plainly what the current option does. The first screen is the only
    // place permadeath is explained; the second is the only place Hard is.
    const char* detail;
    if (!difficultyStep) {
        detail = sNewGamePromptChoice
                     ? "If Olimar loses all his health, this file is erased."
                     : "Losing Olimar ends the day. The original rules.";
    } else {
        detail = sNewGamePromptChoice
                     ? "Tougher enemies, Olimar takes more damage. 8-minute days, 80 Pikmin."
                     : "Original enemy health, day length and field limit.";
    }
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(detail) / 2, panelY + 172,
                    "%s",
                    sNewGamePromptChoice ? Colour(255, 150, 150, 255)
                                         : Colour(190, 200, 220, 255),
                    Colour(8, 12, 28, 255), detail);

    const char* help = difficultyStep
                           ? "Left/Right: choose    A / Enter: start    B / Esc: back"
                           : "Left/Right: choose    A / Enter: next    B / Esc: back";
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(help) / 2, panelY + 212,
                    "%s", Colour(150, 165, 195, 255), Colour(8, 12, 28, 255), help);
}

void pc_settings_draw(void) {
    if (!sMenuOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    // Dim ignores GX 640 mapping (title/file-select leave a left-aligned
    // 4:3 scissor). The panel then uses centred 4:3 without stretching and
    // without fill_ui_43_bars, which would overwrite the dim with opaque black.
    // Mapping stays live through the P2D destructor, including submenu returns.
    struct F1Map {
        F1Map()
        {
            pc_gfx_set_menu_clip_43(0);
            pc_gfx_set_hud_wide(0);
            pc_gfx_set_ui_43_no_bars(0);
        }
        void bindPanel() { pc_gfx_set_ui_43_no_bars(1); }
        ~F1Map() { pc_gfx_set_ui_43_no_bars(0); }
    } f1Map;

    const int screenW = gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);

    pc_gfx_dim_full_target(160);
    f1Map.bindPanel();

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const int panelX = 74;
    const int panelY = pc_settings_p2d_active() ? 52 : 64;
    const int panelW = screenW - 148;
    const int panelH = screenH - (pc_settings_p2d_active() ? 88 : 116);
    const int px1 = panelX, py1 = panelY;
    const int px2 = panelX + panelW, py2 = panelY + panelH;
    const int radius = 26;
    const int headerH = 34;

    drawPikminPanel(gfx, px1, py1, panelW, panelH, radius);
    drawPikminHeader(gfx, px1, py1, panelW, "PC Settings");

    const char* modeNames[3] = { "Windowed", "Fullscreen", "Borderless" };

    // Modal video-confirm dialog.
    if (sVideoConfirmActive) {
        const Uint32 elapsed = SDL_GetTicks() - sVideoConfirmStartMs;
        const Uint32 remainMs = elapsed >= kVideoConfirmDurationMs
                                    ? 0u : kVideoConfirmDurationMs - elapsed;
        const int remainS = (int)((remainMs + 999) / 1000); // redondeo al alza

        int cy = py1 + headerH + 26;
        drawTextOutline(px1 + panelW / 2 - menuTextWidth("Video settings changed.") / 2, cy,
                        "Video settings changed.", Colour(255, 240, 180, 255), Colour(18, 26, 56, 255));
        drawTextOutline(px1 + panelW / 2 - menuTextWidth("A: keep   B: revert") / 2, cy + 26,
                        "A: keep   B: revert", Colour(255, 255, 255, 255), Colour(18, 26, 56, 255));
        char autoBuf[64];
        snprintf(autoBuf, sizeof(autoBuf), "Auto-reverting in %d s", remainS);
        drawTextOutline(px1 + panelW / 2 - menuTextWidth(autoBuf) / 2, cy + 52,
                        "%s", Colour(255, 255, 255, 255), Colour(18, 26, 56, 255), autoBuf);
        return;
    }

    const char* labels[ROW_COUNT] = {
        "Display Mode", "Resolution", "Aspect Ratio", "3D Resolution", "Refresh Rate", "Frame Sync (VSync)",
        "FPS Mode",
#if defined(VERSION_GPIP01)
        "Language",
#endif
        "Controls", "Gamepad", "Advanced Settings", "Graphics", "Mods",
        "Reset to Defaults", "Save", "Close",
    };
    bool actionRow[ROW_COUNT] = {};
    actionRow[ROW_RESET] = actionRow[ROW_SAVE] = actionRow[ROW_CLOSE] = true;

    const char* aspectNames[5] = { "Auto", "4:3", "16:10", "16:9", "21:9" };
    char aspectBuf[32];
    snprintf(aspectBuf, sizeof(aspectBuf), "%s", aspectNames[sPending.aspectRatioMode >= 0 && sPending.aspectRatioMode < 5 ? sPending.aspectRatioMode : 0]);

    const char* fpsModeNames[3] = { "30 FPS (stable)", "60 FPS (experimental)", "120 FPS (experimental)" };
    char fpsModeBuf[32];
    snprintf(fpsModeBuf, sizeof(fpsModeBuf), "%s", fpsModeNames[sPending.fpsMode >= 0 && sPending.fpsMode < 3 ? sPending.fpsMode : 0]);

    char valueBuf[ROW_CONTROLS][128];
    snprintf(valueBuf[0], sizeof(valueBuf[0]), "%s",
             modeNames[sPending.displayMode >= 0 && sPending.displayMode < 3 ? sPending.displayMode : 0]);
    if (sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS) {
        if (sDesktopW > 0) {
            snprintf(valueBuf[1], sizeof(valueBuf[1]), "Desktop (%dx%d)", sDesktopW, sDesktopH);
        } else {
            snprintf(valueBuf[1], sizeof(valueBuf[1]), "Desktop");
        }
    } else {
        char aspectTag[16];
        aspectLabel(sPending.windowWidth, sPending.windowHeight, aspectTag, sizeof(aspectTag));
        const bool native = sPending.windowWidth == sDesktopW && sPending.windowHeight == sDesktopH;
        snprintf(valueBuf[1], sizeof(valueBuf[1]), "%dx%d  %s%s", sPending.windowWidth,
                 sPending.windowHeight, aspectTag, native ? "  (native)" : "");
    }
    snprintf(valueBuf[2], sizeof(valueBuf[2]), "%s", aspectBuf);
    {
        float rs = sPending.renderScale;
        if (fabsf(rs - 2.0f / 3.0f) < 0.01f) snprintf(valueBuf[3], sizeof(valueBuf[3]), "Auto (native)");
        else snprintf(valueBuf[3], sizeof(valueBuf[3]), "%.2fx", rs);
    }
    if (sPending.refreshRate <= 0.0) snprintf(valueBuf[4], sizeof(valueBuf[4]), "Auto");
    else snprintf(valueBuf[4], sizeof(valueBuf[4]), "%.0f Hz", sPending.refreshRate);
    snprintf(valueBuf[5], sizeof(valueBuf[5]), "%s", sPending.vsync ? "On" : "Off");
    snprintf(valueBuf[ROW_FPS_MODE], sizeof(valueBuf[0]), "%s", fpsModeBuf);
#if defined(VERSION_GPIP01)
    {
        static const char* const kNames[] = { "English", "Deutsch", "Francais",
                                              "Espanol", "Italiano", "Nederlands" };
        const unsigned char language = pc_settings_get_language();
        snprintf(valueBuf[ROW_LANGUAGE], sizeof(valueBuf[0]), "%s%s", kNames[language],
                 language == pc_settings_startup_language() ? "" : "  (on restart)");
    }
#endif

    // The rows before ROW_CONTROLS carry a computed value; from there to
    // ROW_RESET they open a submenu and all read "Open >".
    //
    // This used to be a table with one entry per row, and it had one "Open >"
    // too few: a submenu row was added without extending it, so the last one --
    // Mods -- read a value-initialised null and drew "(null)" beside itself.
    // Derived from the row index instead, it cannot fall out of step again.
    auto rowValue = [&](int row) -> const char* {
        return (row < ROW_CONTROLS) ? valueBuf[row] : "Open >";
    };

    const int rowH = pc_settings_p2d_active() ? 20 : 18;
    const int labelRight = px1 + panelW / 2 - 12;
    const int valueLeft = px1 + panelW / 2 + 20;
    int y = py1 + headerH + 12;
    for (int i = 0; i < ROW_COUNT; i++) {
        bool selected = (i == sSelection);
        if (actionRow[i]) {
            int tx = px1 + panelW / 2 - menuTextWidth(labels[i]) / 2;
            if (selected) {
                if (pc_settings_p2d_active())
                    drawTextOutline(px1 + 29, y, ">", Colour(255,232,130,255), Colour(0,0,0,255));
                fillRoundRectGrad(gfx, px1 + 70, y - 3, panelW - 140, rowH + 4, 8,
                                  Colour(47, 45, 35, 210), Colour(8, 8, 10, 220));
            }
            drawTextOutline(tx, y, "%s",
                            selected ? Colour(255, 190, 28, 255) : Colour(211, 246, 255, 255),
                            selected ? Colour(62, 25, 0, 255) : Colour(0, 10, 14, 255), labels[i]);
        } else {
            if (selected) {
                fillRoundRectGrad(gfx, px1 + 48, y - 3, panelW - 96, rowH + 4, 8,
                                  Colour(54, 49, 34, 210), Colour(8, 8, 10, 220));
            }
            Colour main = selected ? Colour(255, 190, 28, 255) : Colour(178, 235, 255, 255);
            Colour shadow = selected ? Colour(62, 25, 0, 255) : Colour(0, 10, 18, 255);
            drawTextOutline(labelRight - menuTextWidth(labels[i]), y, "%s",
                            main, shadow, labels[i]);
            drawTextOutline(valueLeft, y, "%s", main, shadow, rowValue(i));
            if (selected) {
                drawTextOutline(px1 + 29, y, ">", Colour(255, 232, 130, 255),
                                Colour(45, 18, 0, 255));
            }
        }
        y += rowH;
    }

    // Controls submenu overlay.
    if (sInControlsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Keyboard Controls",
                           "Enter: capture   Left/Right: default",
                           "Up/Down: select   Esc/B: back");

        // List of actions.
        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        int startIdx = 0;
        if (sControlSelection >= visibleItems) {
            startIdx = sControlSelection - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > PC_KEY_ACT_COUNT) endIdx = PC_KEY_ACT_COUNT;

        for (int i = startIdx; i < endIdx; i++) {
            int itemY = listStartY + (i - startIdx) * itemH;
            bool selected = (i == sControlSelection);
            bool waiting = sWaitingForKey && selected;

            const char* actionName = pc_window_get_key_action_name(i);
            SDL_Scancode boundSc = static_cast<SDL_Scancode>(sPending.keyboardBindings[i]);
            const char* scName = SDL_GetScancodeName(boundSc);

            char value[96];
            if (waiting) {
                snprintf(value, sizeof(value), "[Press a key...]");
            } else {
                snprintf(value, sizeof(value), "%s", scName ? scName : "None");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           actionName, value, selected);
        }

        // Scroll hint if more items exist.
        if (PC_KEY_ACT_COUNT > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sControlSelection + 1, PC_KEY_ACT_COUNT);
            drawTextOutline(subX + subW - 12 - menuTextWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when submenu is open.
    }

    // Gamepad submenu overlay.
    if (sInGamepadSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Gamepad Controls",
                           sWaitingForButton ? "Press a button, trigger or stick   Esc: cancel"
                                            : "Enter: capture   Left/Right: default",
                           sWaitingForButton ? "" : "Up/Down: select   Esc/B: back");

        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        int startIdx = 0;
        if (sGamepadSelection >= visibleItems) {
            startIdx = sGamepadSelection - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > PC_KEY_ACT_COUNT) endIdx = PC_KEY_ACT_COUNT;

        for (int i = startIdx; i < endIdx; i++) {
            int itemY = listStartY + (i - startIdx) * itemH;
            bool selected = (i == sGamepadSelection);
            bool waiting = sWaitingForButton && selected;

            const char* actionName = pc_window_get_key_action_name(i);
            int boundBtn = sPending.gamepadBindings[i];
            if (boundBtn < 0) boundBtn = kDefaultGamepadBindings[i];
            const char* btnName = pc_window_get_gamepad_button_name(boundBtn);

            char value[96];
            if (waiting) {
                snprintf(value, sizeof(value), "[Press a button...]");
            } else {
                snprintf(value, sizeof(value), "%s", btnName ? btnName : "None");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           actionName, value, selected);
        }

        if (PC_KEY_ACT_COUNT > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sGamepadSelection + 1, PC_KEY_ACT_COUNT);
            drawTextOutline(subX + subW - 12 - menuTextWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when gamepad submenu is open.
    }

    // Advanced settings submenu overlay.
    if (sInAdvancedSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Advanced Settings",
                           "Left/Right: adjust",
                           "Up/Down: select   Esc/B: back");

        const char* advancedLabels[kAdvancedRowCount] = {
            "Mouse Sensitivity",
            "Stick Dead Zone",
            "Stick Invert (X/Y)",
            "C-Stick Invert (X/Y)",
        };

        const int listStartY = subY + 62;
        const int itemH = 28;

        for (int i = 0; i < kAdvancedRowCount; i++) {
            int itemY = listStartY + i * itemH;
            bool selected = (i == sAdvancedSelection);

            char value[64];
            if (i == 0) {
                snprintf(value, sizeof(value), "%.2f", sPending.mouseSensitivity);
            } else if (i == 1) {
                snprintf(value, sizeof(value), "%d", sPending.stickDeadZone);
            } else if (i == 2) {
                snprintf(value, sizeof(value), "%s / %s",
                         (sPending.stickInvert & 1) ? "InvX" : "NorX",
                         (sPending.stickInvert & 2) ? "InvY" : "NorY");
            } else {
                snprintf(value, sizeof(value), "%s / %s",
                         (sPending.cStickInvert & 1) ? "InvX" : "NorX",
                         (sPending.cStickInvert & 2) ? "InvY" : "NorY");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           advancedLabels[i], value, selected);
        }

        return; // Don't draw footer when advanced submenu is open.
    }

    // Resolution submenu overlay.
    if (sInResolutionSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Resolution",
                           "Enter: apply",
                           "Up/Down: select   Esc/B: back");

        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        const int choiceCount = (int)sResolutionChoices.size();
        int startIdx = 0;
        if (sResolutionSubmenuSel >= visibleItems) {
            startIdx = sResolutionSubmenuSel - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > choiceCount) endIdx = choiceCount;

        for (int k = startIdx; k < endIdx; k++) {
            const Resolution& r = sResolutions[sResolutionChoices[k]];
            const int itemY = listStartY + (k - startIdx) * itemH;
            const bool selected = (k == sResolutionSubmenuSel);
            const bool current = r.w == sPending.windowWidth && r.h == sPending.windowHeight;

            char label[64];
            snprintf(label, sizeof(label), "%s%dx%d", current ? "> " : "  ", r.w, r.h);

            char aspectTag[16];
            aspectLabel(r.w, r.h, aspectTag, sizeof(aspectTag));
            char value[96];
            snprintf(value, sizeof(value), "%s%s", aspectTag,
                     r.isNative ? "  (native)" : (r.isDerived ? "  (window)" : ""));

            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40, label, value, selected);
        }

        if (choiceCount > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sResolutionSubmenuSel + 1, choiceCount);
            drawTextOutline(subX + subW - 12 - menuTextWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when submenu is open.
    }

    // Texture packs submenu overlay.
    if (sInTexturePacksSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Texture Packs",
                           "A: activate / deactivate",
                           "Up/Down: select   Esc/B: back");

        std::vector<std::string> packs = pc_texpack_list_packs();
        const int rowCount = 1 + static_cast<int>(packs.size());
        const int listStartY = subY + 62;
        const int itemH = packs.empty() ? 26 : 22;
        // 9 filas caben cómodas; más se desplazan marcando la fila activa abajo.
        const int visibleItems = 9;
        int startIdx = 0;
        if (sTexturePacksSelection >= visibleItems)
            startIdx = sTexturePacksSelection - visibleItems + 1;
        else if (rowCount < visibleItems)
            startIdx = 0;

        const int fromRow = startIdx;
        const int toRow = std::min(rowCount, startIdx + visibleItems);

        // Instalar desde fichero.
        if (fromRow <= kTexturePackInstallRow && kTexturePackInstallRow < toRow) {
            const int itemY = listStartY + (kTexturePackInstallRow - fromRow) * itemH;
            const bool selected = (sTexturePacksSelection == kTexturePackInstallRow);
            char value[96];
            if (sTexturePackPickerActive && sTexturePackInstallFiles.load() > 0)
                snprintf(value, sizeof(value), "Installing... %d files", sTexturePackInstallFiles.load());
            else if (sTexturePackPickerActive)
                snprintf(value, sizeof(value), "Selecting file...");
            else
#ifdef __ANDROID__
                snprintf(value, sizeof(value), "Android picker");
#else
                snprintf(value, sizeof(value), "manual");
#endif
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           "Install from file (ZIP / RAR)", value, selected);
        }

        // Packs instalados.
        for (int r = std::max(fromRow, kTexturePackInstallRow + 1); r < toRow; r++) {
            const int idx = r - kTexturePackInstallRow - 1;
            const int itemY = listStartY + (r - fromRow) * itemH;
            const bool selected = (sTexturePacksSelection == r);
            const std::string& folder = packs[idx];
            const bool active = sConfig.texturePackEnabled && folder == sConfig.texturePack;
            // "Active" solo si este arranque lo indexó de verdad; si es el
            // elegido pero el cargador no pudo con él, decirlo, no fingir.
            const bool loadedNow = pc_texpack_enabled() && folder == pc_texpack_selected_pack();
            const bool chosenAtBoot = !pc_texpack_selected_pack().empty()
                && folder == pc_texpack_selected_pack();
            std::error_code markerEc;
            const bool incomplete = !sTexturePackPickerActive
                && std::filesystem::exists(std::filesystem::path("Load") / "Textures" / ".incomplete", markerEc);
            const char* state = incomplete ? "INCOMPLETE: install again"
                : !active ? "Inactive"
                : loadedNow ? "Active"
                : chosenAtBoot ? "Failed to load (see below)"
                : "Active  (on restart)";
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40, folder.c_str(), state, selected);
        }

        if (packs.empty()) {
            const int itemY = listStartY + itemH;
            const bool selected = false;
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           "No packs installed", "install one above", selected);
        }

        if (rowCount > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sTexturePacksSelection + 1, rowCount);
            drawTextOutline(subX + subW - 12 - menuTextWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        // Estado real del cargador en este arranque (qué indexó y cómo sube
        // los DDS): es lo que distingue "activo en el .conf" de "en uso".
        {
            char status[200];
            snprintf(status, sizeof(status), "Loader: %s", pc_texpack_status());
            drawTextOutline(subX + subW / 2 - menuTextWidth(status) / 2, subY + subH - 48,
                            "%s", Colour(180, 190, 215, 255), Colour(10, 16, 36, 255), status);
            size_t replaced = 0, missing = 0, failed = 0;
            pc_texpack_stats(&replaced, &missing, &failed);
            char counts[120];
            snprintf(counts, sizeof(counts), "Textures: %zu replaced, %zu not in pack, %zu failed",
                     replaced, missing, failed);
            drawTextOutline(subX + subW / 2 - menuTextWidth(counts) / 2, subY + subH - 30,
                            "%s", Colour(180, 190, 215, 255), Colour(10, 16, 36, 255), counts);
        }

        // Aviso de la última instalación o acción, con su color.
        {
            std::lock_guard<std::mutex> lock(sTexturePackNoticeMutex);
            const bool fresh = SDL_GetTicks() - sTexturePackNoticeMs < kTexturePackNoticeTimeoutMs;
            if (fresh && sTexturePackNotice[0]) {
                char msg[sizeof(sTexturePackNotice)];
                snprintf(msg, sizeof(msg), "%s", sTexturePackNotice);
                const Colour colour = sTexturePackNoticeError ? Colour(255, 140, 140, 255)
                                                              : Colour(150, 235, 170, 255);
                const int msgY = subY + subH - 8;
                drawTextOutline(subX + subW / 2 - menuTextWidth(msg) / 2, msgY,
                                "%s", colour, Colour(10, 16, 36, 255), msg);
            }
        }

        // Modal de reinicio: se dibuja sobre el submenú, con prioridad.
        if (sTexturePackRestartPrompt) {
            const int boxW = 560, boxH = 150;
            const int boxX = screenW / 2 - boxW / 2;
            const int boxY = screenH / 2 - boxH / 2;
            drawPikminPanel(gfx, boxX, boxY, boxW, boxH, 18);
            const int ty = boxY + 42;
            const char* line1 = "Texture pack active.";
            const char* line2 = "Restart now so it takes effect.";
            drawTextOutline(boxX + boxW / 2 - menuTextWidth(line1) / 2, ty, "%s",
                            Colour(255, 240, 180, 255), Colour(18, 26, 56, 255), line1);
            drawTextOutline(boxX + boxW / 2 - menuTextWidth(line2) / 2, ty + 26, "%s",
                            Colour(255, 240, 180, 255), Colour(18, 26, 56, 255), line2);
            const char* prompt = "A: Restart now   B: Not yet";
            drawTextOutline(boxX + boxW / 2 - menuTextWidth(prompt) / 2, ty + 64, "%s",
                            Colour(255, 255, 255, 255), Colour(18, 26, 56, 255), prompt);
        }

        return; // Don't draw footer when texture packs submenu is open.
    }

    // Graphics submenu overlay.
    if (sInGraphicsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Graphics",
                           "Left/Right: change   These change how the game looks",
                           "Up/Down: select   Esc/B: back");

        const char* labels[kGraphicsRowCount] = {
            "Antialiasing",
            "Fog",
            "Bloom",
            "Ambient Occlusion",
            "Depth of Field",
            "Texture Filtering",
            "Colour Grading",
            "Gamma",
            "Brightness",
            "Saturation",
            "Texture Packs",
        };

        const int listStartY = subY + 62;
        const int itemH = 22;
        const bool gradingOn = sPending.colourGrading != 0;

        for (int i = 0; i < kGraphicsRowCount; i++) {
            const int itemY = listStartY + i * itemH;
            const bool selected = (i == sGraphicsSelection);

            char value[64];
            if (i == 0) {
                snprintf(value, sizeof(value), "%s", sPending.antialiasing ? "FXAA" : "Off");
            } else if (i == 1) {
                // The game draws fog of its own, so on is the original and off
                // is the deviation. Say which is which.
                snprintf(value, sizeof(value), "%s", sPending.fog ? "On  (original)" : "Off");
            } else if (i == 2) {
                const char* bloomNames[4] = { "Off", "Subtle", "Normal", "Strong" };
                const int b = (sPending.bloom >= 0 && sPending.bloom <= 3) ? sPending.bloom : 0;
                snprintf(value, sizeof(value), "%s", bloomNames[b]);
            } else if (i == 3) {
                const char* aoNames[4] = { "Off", "Subtle", "Normal", "Strong" };
                const int a = (sPending.ssao >= 0 && sPending.ssao <= 3) ? sPending.ssao : 0;
                snprintf(value, sizeof(value), "%s", aoNames[a]);
            } else if (i == 4) {
                const char* dofNames[4] = { "Off", "Subtle", "Normal", "Strong" };
                const int d = (sPending.dof >= 0 && sPending.dof <= 3) ? sPending.dof : 0;
                snprintf(value, sizeof(value), "%s", dofNames[d]);
            } else if (i == 5) {
                if (sPending.anisotropy <= 1) snprintf(value, sizeof(value), "Trilinear");
                else snprintf(value, sizeof(value), "Anisotropic %dx", sPending.anisotropy);
            } else if (i == 6) {
                snprintf(value, sizeof(value), "%s", gradingOn ? "On" : "Off");
            } else if (i == 10) {
                // Rowing into a submenu rather than cycling a value. Mirror the
                // main-list convention so the row reads like the others.
                snprintf(value, sizeof(value), "Manage >");
            } else if (!gradingOn) {
                // The three sliders do nothing while grading is off. Saying so
                // beats letting someone move them and conclude it is broken.
                snprintf(value, sizeof(value), "--");
            } else if (i == 7) {
                snprintf(value, sizeof(value), sPending.gamma == 1.0f ? "%.2f  (neutral)" : "%.2f", sPending.gamma);
            } else if (i == 8) {
                snprintf(value, sizeof(value), sPending.brightness == 0.0f ? "%+.2f  (neutral)" : "%+.2f", sPending.brightness);
            } else {
                snprintf(value, sizeof(value), sPending.saturation == 1.0f ? "%.2f  (neutral)" : "%.2f", sPending.saturation);
            }

            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40, labels[i], value, selected);
        }

        return; // Don't draw footer when submenu is open.
    }

    // Mods submenu overlay.
    if (sInModsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Mods",
                           "Left/Right: change   These change how the game plays",
                           "Up/Down: select   Esc/B: back");

        const char* modsLabels[kModsRowCount] = {
            "Control Scheme",
            "Chain Pikmin Actions",
            "Hold to Pluck",
            "Mouse Wheel",
            "Pikmin Limit",
            "Day Length",
#if PIKI_DEBUG_KEYS
            "Debug Keys (F5/F6)",
#endif
        };

        const int listStartY = subY + 62;
        const int itemH = 28;

        for (int i = 0; i < kModsRowCount; i++) {
            int itemY = listStartY + i * itemH;
            bool selected = (i == sModsSelection);

            char value[64];
            if (i == 0) {
                snprintf(value, sizeof(value), "%s",
                         sPending.controlMode == PC_CONTROL_CLASSIC ? "Classic (original)"
                                                                    : "Mouse Cursor");
            } else if (i == 1) {
                snprintf(value, sizeof(value), "%s",
                         sPending.chainActions ? "On" : "Off (original)");
            } else if (i == 2) {
                snprintf(value, sizeof(value), "%s",
                         sPending.holdToPluck ? "On" : "Off (original)");
            } else if (i == 3) {
                snprintf(value, sizeof(value), "%s",
                         sPending.mouseWheelAction ? "Camera Zoom" : "Pikmin Colour");
            } else if (i == 6) {
                snprintf(value, sizeof(value), "%s",
                         sPending.debugKeys ? "On" : "Off");
            } else if (i == 5) {
                if (pc_hardmode_active()) {
                    snprintf(value, sizeof(value), "%d min (Hard)", PC_HARDMODE_DAY_MINUTES);
                } else if (sPending.dayMinutes == 10) {
                    snprintf(value, sizeof(value), "10 min (original)");
                } else {
                    snprintf(value, sizeof(value), "%d min", sPending.dayMinutes);
                }
            } else {
                if (pc_hardmode_active()) {
                    snprintf(value, sizeof(value), "%d (Hard)", PC_HARDMODE_PIKI_LIMIT);
                } else if (sPending.pikiLimit == 100) {
                    snprintf(value, sizeof(value), "100 (original)");
                } else if (sPending.pikiLimit > 200) {
                    snprintf(value, sizeof(value), "%d  (may cost performance)",
                             sPending.pikiLimit);
                } else {
                    snprintf(value, sizeof(value), "%d", sPending.pikiLimit);
                }
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           modsLabels[i], value, selected);
        }

        return; // Don't draw footer when mods submenu is open.
    }

    // Footer / help.
    drawTextOutline(px1 + panelW / 2 - menuTextWidth("Left/Right: change   Up/Down: move   Esc: close") / 2, y + 14,
                    "Left/Right: change   Up/Down: move   Esc: close",
                    Colour(200, 210, 235, 255), Colour(10, 16, 36, 255));
    if (pc_window_get_last_error()[0]) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Video error: %s", pc_window_get_last_error());
        drawTextOutline(px1 + panelW / 2 - menuTextWidth(errBuf) / 2, y + 34,
                        "%s", Colour(255, 120, 120, 255), Colour(10, 16, 36, 255), errBuf);
    }
}

int pc_settings_get_fps_mode(void) {
    return sConfig.fpsMode;
}

int pc_settings_get_chain_actions(void) {
    return sConfig.chainActions;
}

int pc_settings_get_hold_to_pluck(void) {
    return sConfig.holdToPluck;
}

int pc_settings_get_mouse_wheel_action(void) {
    return sConfig.mouseWheelAction;
}

int pc_settings_get_piki_limit(void) {
    if (pc_hardmode_active() && sConfig.pikiLimit > PC_HARDMODE_PIKI_LIMIT)
        return PC_HARDMODE_PIKI_LIMIT;
    return sConfig.pikiLimit;
}

int pc_settings_get_day_minutes(void) {
    if (pc_hardmode_active() && sConfig.dayMinutes > PC_HARDMODE_DAY_MINUTES)
        return PC_HARDMODE_DAY_MINUTES;
    return sConfig.dayMinutes;
}

int pc_settings_get_debug_keys(void) {
    return sConfig.debugKeys;
}
