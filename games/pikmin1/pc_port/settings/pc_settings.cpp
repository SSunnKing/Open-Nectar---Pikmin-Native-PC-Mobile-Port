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
#include "settings/pc_settings_rows.h"
#include "settings/pc_glass_menu.h"
#include "GlobalGameOptions.h"
#include "mods/pc_hd_models.h"
#include "mods/pc_hd_model_convert.h"
#include "pc_file_dialog.h"
#include "settings/pc_settings_p2d.h"
#include "pc_menu_repeat.h"
#ifdef __ANDROID__
#include "android/pc_texpack_android.h"
#include "android/pc_save_android.h"
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
#include "pc_gyro.h"
#include "pc_permadeath.h"
#include "pc_coop.h"
#include "pc_vs.h"
#include "pc_achievements.h"
#include "pc_speedrun.h"
#include "gameflow.h"
#include "SoundMgr.h"
#include "pc_art.h"
#include "Texture.h"
#if PIKI_PC_TOUCH
#include "touch/pc_touch.h"
#endif
#include "gl/pc_gfx.h"
#include "gl/pc_postprocess.h"
#include "Graphics.h"
#include "GameStat.h"
#include "Font.h"
#include "Colour.h"
#include "Matrix4f.h"
#include "Geometry.h"
#include "system.h"
#include "types.h"
#include "zen/ogSub.h"
#include <map>

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
    int keyboardBindings2[PC_KEY_ACT_COUNT]; // issue #68: segunda tecla (0 = ninguna)

    // Mouse sensitivity (0.1 - 5.0, default 1.0)
    float mouseSensitivity = 1.0f;

    // Gyro aiming (compatible pads and the Android device's own sensor).
    int gyroEnabled = 0;
    float gyroSensitivity = 1.0f; // 0.1 - 5.0
    int gyroInvert = 0;           // bit 0 = horizontal, bit 1 = vertical
    float gyroBias[3] = { 0.0f, 0.0f, 0.0f }; // rad/s, measured by Calibrate

    // Stick dead zone (0 - 127, default 8)
    int stickDeadZone = 8;

    // Stick inversion flags (bitmask: bit 0=horizontal, bit 1=vertical)
    // 0 = normal, 1 = inverted
    int stickInvert = 0;  // bit 0: X, bit 1: Y
    int cStickInvert = 0; // bit 0: X, bit 1: Y

    // Gamepad button bindings (SDL GameController button IDs)
    int gamepadBindings[PC_KEY_ACT_COUNT];
    // Coop: bindings del mando de J2 (mismo formato).
    int gamepadBindingsP2[PC_KEY_ACT_COUNT];

    // Mod: chain Pikmin actions (0=off/faithful, 1=on).
    // Off by default. The stock behaviour -- finish a job, walk back to the
    // squad -- is what the retail game does; this only changes it on request.
    int chainActions = 0;
    // Hold Extract to keep plucking (0=off/faithful, 1=on). Off by default.
    int holdToPluck = 0;
    // Mod: unstick Pikmin that stop making progress along a route (0=off, 1=on).
    int betterPathfinding = 0;
    // Mod: a non-blue Pikmin that wanders into water on its own is pushed back
    // to dry land instead of drowning. Being thrown in still drowns it.
    int bluesOnlyWater = 0;
    // Mod: show how many Pikmin are idle on the map (0=off, 1=on).
    int idleCounter = 0;
    // Mods: health as a percentage of the original. Applied as a divisor on
    // incoming damage rather than by resizing the health bar, so the life
    // gauge and every "below a quarter" check keep reading correctly.
    int naviHealthPct = 100;
    int tekiHealthPct = 100;
    // Mod: the playable day never advances (0=off, 1=on).
    int infiniteDay = 0;
    // Mod: free camera. Shift + mouse orbits on keyboard, the right stick
    // orbits on a pad, and the squad moves to the Swarm button.
    int freeCamera = 0;
    int whistleRadiusPct = 100; // radio máximo del silbato, % del original
    int throwSpeedPct = 100;    // velocidad de las animaciones de coger y lanzar
    int throwCancelB = 0;       // B con un Pikmin en la mano lo devuelve al grupo
    int quickGrab = 0;          // el Pikmin elegido aparece en la mano (sin andar hasta ella)
    int noTrip = 0;             // los Pikmin no tropiezan al correr
    // Whistling over sprouts plucks them one at a time (0=off/faithful, 1=on).
    int whistlePluck = 0;
    int bombControl = 0;
    int hideOlimarText = 0;     // sin los textos de Olimar (primer Pikmin, piezas, avisos)
    int speedrunIntroHidden = 0; // explicación del modo Speedrun ya vista (se abre sola solo la primera vez)        // botón Bomb: el amarillo con bomba la lanza al cursor o la suelta
    int onionStep10 = 0;        // Y + arriba/abajo en la cebolla mueve de 10 en 10
    int instantWhistle = 0;     // los Pikmin silbados se unen sin la reacción de girarse
    // Cheats.
    int pikiInvincible = 0;     // los Pikmin no mueren (ataques, fuego, agua, gas, aplastados)
    int allFlowers = 0;         // todo Pikmin lleva flor
    int carrySpeedPct = 100;    // velocidad al cargar objetos
    int naviSpeedPct = 100;     // velocidad de Olimar al andar
    int unlockZones = 0;        // abre todas las zonas (se graba en la partida)
    int noDayAdvance = 0;       // el contador de días no avanza
    int allOnions = 0;          // cebollas roja, amarilla y azul (se graba en la partida)
    int breakableGates = 0;     // issue #70: los Pikmin rompen a golpes las compuertas reforzadas
    int freeCamPadPct = 100;    // issue #66: sensibilidad del stick derecho con la cámara libre
    int eternalNight = 0;       // siempre de noche (luz y luna), el reloj del día sigue igual
    int p2Selection = 0;        // issue #67: cruceta como en Pikmin 2 (tipo fijo, arriba/abajo hoja/capullo/flor/bomba)
    // Reglas del modo VS (índices de las opciones del menú previo).
    int vsDuration = 2;  // 5 / 10 / 15 / 25 / 30 min
    int vsRocketWin = 1; // cohete asediable y destruirlo gana
    int vsRocketHp = 1;  // baja / normal / alta
    int vsBigPiece = 1;  // minuto 3 / minuto 7 / desde el inicio / sin gorda
    int vsPikiLimit = 2; // 25 / 40 / 50 por jugador
    int vsPellets = 1;   // cada 30 / 45 / 60 s / sin pastillas
    // Mods de Pikmin 3: fijar objetivo, y mandar el escuadrón contra él.
    int lockOn = 0;
    int charge = 0;
    // Mod: cerrar el relevo del lanzamiento con el capitán en marcha.
    int throwWhileMoving = 0;
    // Mod: vista en primera persona, conmutada en marcha con su propia tecla.
    int firstPerson = 0;
    // What the mouse wheel does: 0 = pick the Pikmin colour to throw,
    // 1 = zoom the camera. One setting rather than two toggles, so the two
    // uses cannot both be on or both be off.
    int mouseWheelAction = 0;
    // Pikmin allowed on the field at once. The game treats this as a design
    // parameter of its own (AIConstant "p15"), so raising it is supported
    // rather than forced. 100 is the original.
    int pikiLimit = 100;
    // Minutes of play per in-game day, as shown in the menu. 10 is the original.
    int dayMinutes = 0; // 0 = original (13.5 min de luz)
    // Última elección del selector 1P/2P, solo para preseleccionarla. La
    // partida en sí no la guarda (PLAN_COOP).
    int coopPlayers = 1;
    // Pantalla partida: 0 = vertical (izq/der), 1 = horizontal (arriba/abajo).
    // En táctil (móvil apaisado) por defecto horizontal.
#if PIKI_PC_TOUCH
    int coopSplit = 1;
#else
    int coopSplit = 0;
#endif
    // Cámara cooperativa dinámica: una sola cámara con los Olimar cerca y
    // división fluida al alejarse. 0 = pantalla partida fija.
    int coopMergeCamera = 0;
    // Colour grading. Neutral by default: the port should look like the game
    // until someone asks otherwise.
    int antialiasing = 0;   // 0 off, 1 FXAA
    int fog = 1;            // the game's own fog, on by default
    int perPixelLighting = 0; // 0 por vértice (GX original), 1 por píxel
    int shadows = 0;          // sombras del sol: 0 off, 1 suave, 2 normal, 3 fuerte
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
    // Modelos HD (Load/Models) apagados, un bit por fila de su selector
    // (Olimar, Louie, Louie HD, Pikmin, Bulborb, Dwarf Bulborb). Apagado =
    // se dibuja el original aunque esté instalado. Se aplica reiniciando.
    int hdModelsDisabled = 0;

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
        gyroEnabled = 0;
        gyroSensitivity = 1.0f;
        gyroInvert = 0;
        gyroBias[0] = gyroBias[1] = gyroBias[2] = 0.0f;
        stickDeadZone = 8;
        stickInvert = 0;
        cStickInvert = 0;
        chainActions = 0;
        holdToPluck = 0;
        betterPathfinding = 0;
        bluesOnlyWater = 0;
        idleCounter = 0;
        naviHealthPct = 100;
        tekiHealthPct = 100;
        infiniteDay = 0;
        freeCamera = 0;
        whistleRadiusPct = 100;
        throwSpeedPct = 100;
        throwCancelB = 0;
        quickGrab = 0;
        noTrip = 0;
        whistlePluck = 0;
        bombControl = 0;
        hideOlimarText = 0;
        onionStep10 = 0;
        instantWhistle = 0;
        pikiInvincible = 0;
        allFlowers = 0;
        carrySpeedPct = 100;
        naviSpeedPct = 100;
        unlockZones = 0;
        noDayAdvance = 0;
        breakableGates = 0;
        freeCamPadPct = 100;
        p2Selection = 0;
        eternalNight = 0;
        allOnions = 0;
        vsDuration = 2;
        vsRocketWin = 1;
        vsRocketHp = 1;
        vsBigPiece = 1;
        vsPikiLimit = 2;
        vsPellets = 1;
        lockOn = 0;
        charge = 0;
        throwWhileMoving = 0;
        firstPerson = 0;
        mouseWheelAction = 0;
        pikiLimit = 100;
        dayMinutes = 0;
        coopPlayers = 1;
#if PIKI_PC_TOUCH
        coopSplit = 1;
#else
        coopSplit = 0;
#endif
        coopMergeCamera = 0;
        antialiasing  = 0;
        fog           = 1;
        perPixelLighting = 0;
        shadows = 0;
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
        hdModelsDisabled = 0;
        for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
            keyboardBindings[i] = kDefaultKeyBindings[i];
            keyboardBindings2[i] = pc_window_default_key_binding2(i);
            gamepadBindings[i] = -1; // -1 = not remapped (use default)
            gamepadBindingsP2[i] = -1;
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
    ROW_DISPLAY_COUNT, ///< filas del grupo Display (el resto son grupos en pc_settings_rows)
};

// Página de F1: pestañas de grupo arriba, filas del grupo (con cabeceras de
// sección) a la izquierda y, a la derecha, las opciones de la fila
// seleccionada con su explicación debajo. En escritorio va en el lienzo
// 640x480 4:3 como el resto de menús; en móvil el lienzo es más bajo (todo se
// ve más grande en pantallas pequeñas) y tan ancho como la pantalla.
// f1Layout() lo recalcula cada frame (la pantalla puede girar o cambiar).
constexpr int kF1TabH = 22, kF1CloseW = 26, kF1ItemH = 22, kF1OptH = 22, kF1OptVisible = 8;
int kF1CanvasW = 640, kF1CanvasH = 480;
int kF1PanelX = 36, kF1PanelY = 34, kF1PanelW = 568, kF1PanelH = 418;
int kF1TabY, kF1TabX, kF1TabW;
int kF1ColY, kF1ColH, kF1LeftX, kF1LeftW, kF1RightX, kF1RightW;
int kF1Visible; // cabeceras y filas visibles a la vez

void f1Layout() {
#ifdef __ANDROID__
    int dw = 0, dh = 0;
    pc_gfx_get_drawable_size(&dw, &dh);
    const float aspect = dh > 0 ? float(dw) / float(dh) : 16.0f / 9.0f;
    kF1CanvasH = 400;
    kF1CanvasW = std::max(600, int(lroundf(400.0f * aspect)));
    kF1PanelX = 8;
    kF1PanelY = 8;
    kF1PanelH = kF1CanvasH - 16;
#else
    kF1CanvasW = 640;
    kF1CanvasH = 480;
    kF1PanelX = 36;
    kF1PanelY = 34;
    kF1PanelH = 480 - 62;
#endif
    kF1PanelW = kF1CanvasW - 2 * kF1PanelX;
    kF1TabY = kF1PanelY + 14;
    kF1TabX = kF1PanelX + 18;
    kF1TabW = (kF1PanelW - 36 - kF1CloseW) / PC_SET_GROUP_COUNT;
    kF1ColY = kF1PanelY + 46;
    kF1ColH = kF1PanelH - 46 - 34;
    kF1LeftX = kF1PanelX + 16;
    kF1LeftW = (kF1PanelW - 44) * 4 / 7;
    kF1RightX = kF1LeftX + kF1LeftW + 12;
    kF1RightW = kF1PanelX + kF1PanelW - 16 - kF1RightX;
    kF1Visible = (kF1ColH - 16) / kF1ItemH;
}

// Toque normalizado (0..1 de la ventana) -> lienzo de F1, que se escala
// uniforme y centrado en la pantalla.
void f1TapToCanvas(float nx, float ny, int* x, int* y) {
    int dw = 0, dh = 0;
    pc_gfx_get_drawable_size(&dw, &dh);
    if (dw <= 0 || dh <= 0) { dw = kF1CanvasW; dh = kF1CanvasH; }
    const float scale = std::min(float(dw) / kF1CanvasW, float(dh) / kF1CanvasH);
    const float ox = (dw - kF1CanvasW * scale) * 0.5f, oy = (dh - kF1CanvasH * scale) * 0.5f;
    *x = int((nx * dw - ox) / scale);
    *y = int((ny * dh - oy) / scale);
}

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
static std::vector<Uint8> gPrevKeys; // previous-frame keyboard state snapshot
// SDL calls the Xbox Select/View button BACK.  Keep its edge independently of
// the keyboard snapshot: settings input is polled from more than one hook per
// frame, and a held button must not reopen the menu after a modal closes.
bool sPrevMenuToggleHeld = false;
bool sPrevMenuToggleHeldP2 = false;
// Jugador que abrió el menú F1 (0 = J1, 1 = J2 con su mando): decide qué
// mando lo maneja y qué bindings de mando edita.
int sMenuPlayer = 0;
int* pendingPadBinds() { return sMenuPlayer == 1 ? sPending.gamepadBindingsP2 : sPending.gamepadBindings; }
// Mando que maneja el menú: el de J2 si lo abrió él (y sigue conectado).
SDL_GameController* menuController() {
    SDL_GameController* p2 = sMenuPlayer == 1 ? pc_window_get_controller_p2() : nullptr;
    return p2 ? p2 : pc_window_get_controller();
}

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
bool sCaptureSecond = false; // issue #68: la captura va a la segunda tecla
void startKeyCapture(int action, bool second);
// "Space / Mouse Left", o solo la principal si no hay segunda.
void keyBindingPairName(int action, char* out, size_t n);
// Enter / Space / pad A started capture while still held. Ignore them until
// they are released, otherwise the same press is stored as the new binding.
bool sCaptureWaitRelease = false;
Uint32 sCapturePrevMouse = 0; // mouse buttons seen on the previous capture tick

// Gamepad controls submenu state.
bool sInGamepadSubmenu = false;
int sGamepadSelection = 0;
bool sWaitingForButton = false;

// Pestaña abierta (-1 = menú cerrado), su fila y la primera entrada visible
// de la columna izquierda (cuenta también las cabeceras).
int sOpenGroup = -1;
int sGroupSel = 0;
int sF1Scroll = 0;
// Mientras se sondean las opciones de una fila (columna derecha) los cambios
// solo tocan sPending: nada se aplica al vídeo ni al render.
bool sProbing = false;
// Modo sin ventana (--settings-*, lo usa el launcher): como el sondeo, los
// cambios solo tocan sPending y nunca la ventana ni el render.
bool sHeadless = false;
const char* rowSection(int group, int row);
bool rowProbeable(int group, int row);
bool rowIsResolution(int group, int row);
// Cursor en la columna derecha de F1: A en una fila con lista entra, arriba/
// abajo recorre las opciones, A aplica la marcada y B vuelve a las filas.
bool sF1OptFocus = false;
int sF1OptSel = 0;

// Texture packs submenu state (PLAN_TEXTURAS_HD fase 2). La instalación la
// hace el selector de archivos de Android y termina en un hilo Java; el
// resultado llega por pc_texpack_install_finished() y se pinta aquí.
bool sInTexturePacksSubmenu = false;
bool sInHdModelsSubmenu = false;
// Fila del submenú HD Models: 0 Olimar, 1 Louie (Pikmin 2), 2 Louie HD (Pikmin 3), 3 Pikmin, 4 Bulborb, 5 Dwarf Bulborb.
int sHdModelsSelection = 0;
std::atomic<bool> sHdModelInstallActive{false};
bool sHdModelRestartPrompt = false;
int sTexturePacksSelection = 0;      // 0 = instalar, 1.. = packs instalados
std::atomic<bool> sTexturePackPickerActive{false}; // picker abierto o extracción en curso (hilo Java)
std::atomic<int> sTexturePackInstallFiles{0};  // ficheros extraídos (hilo Java)
// Mensaje de la última acción (instalación o aviso de sobremesa).
bool sTexturePackRestartPrompt = false; // modal "reiniciar para aplicar"
constexpr int kTexturePackInstallRow = 0;
std::mutex sTexturePackNoticeMutex;
char sTexturePackNotice[256] = {};
bool sTexturePackNoticeError = false;
Uint32 sTexturePackNoticeMs = 0;
constexpr Uint32 kTexturePackNoticeTimeoutMs = 6000;

// Submenú "Save Data" (issue #36): exportar/importar la tarjeta de memoria en
// Android a través del selector SAF. El .zip lo escribe Java en un hilo; el
// resultado llega por pc_save_transfer_finished() y se pinta aquí.
std::atomic<bool> sSaveTransferActive{false}; // picker abierto o transferencia en curso (hilo Java)

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


// Field-limit stops. 100 is what the original game uses.
constexpr int kPikiLimits[]   = { 50, 100, 150, 200, 300, 500, 750, PIKI_FIELD_LIMIT_MAX };
constexpr int kPikiLimitCount = int(sizeof(kPikiLimits) / sizeof(kPikiLimits[0]));

// Day length, in real minutes of actual play. The clock's own figure covers a
// full 24-hour cycle, but a day is played from 7am to 7pm -- half of it -- so
// the menu shows the half the player experiences. 0 is the original: 27 min
// per 24h in the game's parameters, i.e. 13.5 min of play (issue #50).
constexpr int kDayMinutes[]    = { 5, 7, 10, 0, 15, 20, 30 };
constexpr int kDayMinutesCount = int(sizeof(kDayMinutes) / sizeof(kDayMinutes[0]));

// Health stops, as a percentage of the original. Shared by Olimar and the
// enemies so both rows read the same way.
// Radio del silbato y velocidad de lanzamiento, en % del original.
constexpr int kWhistlePcts[]    = { 50, 75, 100, 125, 150, 200, 250, 300 };
constexpr int kWhistlePctCount  = int(sizeof(kWhistlePcts) / sizeof(kWhistlePcts[0]));
constexpr int kThrowSpeedPcts[] = { 50, 75, 100, 125, 150, 175, 200 };
constexpr int kThrowSpeedCount  = int(sizeof(kThrowSpeedPcts) / sizeof(kThrowSpeedPcts[0]));

constexpr int kSpeedPcts[]      = { 100, 150, 200, 250, 300, 400, 500 };
constexpr int kSpeedPctCount   = int(sizeof(kSpeedPcts) / sizeof(kSpeedPcts[0]));

int clampSpeedPct(int pct) {
    for (int i = 0; i < kSpeedPctCount; i++) {
        if (kSpeedPcts[i] == pct) return pct;
    }
    return 100;
}

void speedPctLabel(int pct, char* value, size_t n) {
    if (pc_hardmode_active()) snprintf(value, n, "1x (Hard)");
    else if (pct == 100) snprintf(value, n, "1x (original)");
    else if (pct % 100 == 0) snprintf(value, n, "%dx", pct / 100);
    else snprintf(value, n, "%d.%dx", pct / 100, (pct % 100) / 10);
}

int stepPct(int current, const int* stops, int count, bool back) {
    int idx = 0;
    for (int i = 0; i < count; i++) {
        if (stops[i] == current) { idx = i; break; }
    }
    return stops[back ? (idx + count - 1) % count : (idx + 1) % count];
}

// Sensibilidad del stick derecho con la cámara libre (issue #66).
constexpr int kFreeCamPadPcts[]   = { 25, 50, 75, 100, 125, 150, 200 };
constexpr int kFreeCamPadPctCount = int(sizeof(kFreeCamPadPcts) / sizeof(kFreeCamPadPcts[0]));

int clampFreeCamPadPct(int pct) {
    for (int i = 0; i < kFreeCamPadPctCount; i++) {
        if (kFreeCamPadPcts[i] == pct) return pct;
    }
    return 100;
}

// -1 al final: Infinite para Olimar, Insta Kill para los enemigos.
constexpr int kHealthPcts[]    = { 25, 50, 75, 100, 150, 200, 300, 500, -1 };
constexpr int kHealthPctCount  = int(sizeof(kHealthPcts) / sizeof(kHealthPcts[0]));

int clampHealthPct(int pct) {
    for (int i = 0; i < kHealthPctCount; i++) {
        if (kHealthPcts[i] == pct) return pct;
    }
    return 100;
}

int stepHealthPct(int pct, bool left) {
    int idx = 0;
    for (int i = 0; i < kHealthPctCount; i++) {
        if (kHealthPcts[i] == pct) { idx = i; break; }
    }
    idx = left ? (idx + kHealthPctCount - 1) % kHealthPctCount : (idx + 1) % kHealthPctCount;
    return kHealthPcts[idx];
}

void healthPctLabel(int pct, const char* special, char* value, size_t n) {
    if (pc_hardmode_active()) snprintf(value, n, "100%% (Hard)");
    else if (pct < 0) snprintf(value, n, "%s", special);
    else if (pct == 100) snprintf(value, n, "100%% (original)");
    else snprintf(value, n, "%d%%", pct);
}

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
    if (sProbing || sHeadless) return;
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
    if (sProbing || sHeadless) return;
    PcPostEffects fx;
    pc_gfx_set_fog_allowed(config.fog);
    pc_gfx_set_anisotropy(config.anisotropy);
    pc_gfx_set_per_pixel_lighting(config.perPixelLighting);
    fx.fxaa          = config.antialiasing != 0;
    // Sombras del sol: la fuerza es cuánto oscurece; el mapa es más pequeño
    // en móvil, donde la pasada extra pesa más.
    static const float kShadowStrength[4] = { 0.0f, 0.25f, 0.38f, 0.5f };
    const int shadowStep = (config.shadows >= 0 && config.shadows <= 3) ? config.shadows : 0;
    fx.shadows        = shadowStep != 0;
    fx.shadowStrength = kShadowStrength[shadowStep];
#ifdef __ANDROID__
    fx.shadowMapSize  = 1024;
#else
    fx.shadowMapSize  = 2048;
#endif

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
        pc_window_set_key_binding2(i, static_cast<SDL_Scancode>(config.keyboardBindings2[i]));
        pc_window_set_gamepad_binding(i, config.gamepadBindings[i]);
        pc_window_set_gamepad_binding_p2(i, config.gamepadBindingsP2[i]);
    }
}

void startVideoConfirm() {
    if (sProbing || sHeadless) return;
    sVideoConfirmStartMs = SDL_GetTicks();
    sVideoConfirmActive = true;
}

void saveConfig(); // defined below
void mainRowChange(int row, bool left, bool right, bool ok);
void mainRowValue(int row, char* out, size_t n);
void advancedRowChange(int row, bool left, bool right);
void advancedRowValue(int i, char* value, size_t n);
void graphicsRowChange(int row, bool left, bool right, bool ok);
void modsRowChange(int row, bool left, bool right);
void saveDataRowAction(int row);
void texturePacksRowAction(int row, const std::vector<std::string>& packs);
void hdModelsRowAction(int row);
void pollKeyCapture(SDL_GameController* ctl);
bool pollButtonCapture(SDL_GameController* ctl);
void pcCaptainPromptInput();

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

// Al salir de cualquiera de los dos menús se guarda todo lo cambiado. La
// excepción es un cambio de vídeo que sigue esperando confirmación: solo esos
// campos vuelven a lo guardado, sin arrastrar el resto de cambios con ellos.
void commitPendingOnExit() {
    if (sVideoConfirmActive || isVideoSettingChanged()) {
        sPending.windowWidth     = sConfig.windowWidth;
        sPending.windowHeight    = sConfig.windowHeight;
        sPending.displayMode     = sConfig.displayMode;
        sPending.vsync           = sConfig.vsync;
        sPending.renderScale     = sConfig.renderScale;
        sPending.aspectRatioMode = sConfig.aspectRatioMode;
        sPending.refreshRate     = sConfig.refreshRate;
        sVideoConfirmActive      = false;
        applyVideo();
        syncResolutionIndex();
    }
    sConfig = sPending;
    applyControls(sConfig);
    applyGraphics(sConfig);
    saveConfig();
}

void closeMenu() {
    commitPendingOnExit();
    sMenuPlayer = 0;
    // No dejar el menu memorizado dentro de la lista: al reabrir F1 se espera
    // la pagina principal.
    sInResolutionSubmenu = false;
    sInTexturePacksSubmenu = false;
    sInHdModelsSubmenu = false;
    sTexturePackRestartPrompt = false;
    sHdModelRestartPrompt = false;
    sInControlsSubmenu = false;
    sInGamepadSubmenu = false;
    sWaitingForKey = false;
    sWaitingForButton = false;
    sCaptureWaitRelease = false;
    sOpenGroup = -1;
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
    out << "betterPathfinding = " << sConfig.betterPathfinding << "\n";
    out << "bluesOnlyWater = " << sConfig.bluesOnlyWater << "\n";
    out << "idleCounter = " << sConfig.idleCounter << "\n";
    out << "naviHealthPct = " << sConfig.naviHealthPct << "\n";
    out << "tekiHealthPct = " << sConfig.tekiHealthPct << "\n";
    out << "infiniteDay = " << sConfig.infiniteDay << "\n";
    out << "freeCamera = " << sConfig.freeCamera << "\n";
    out << "whistleRadiusPct = " << sConfig.whistleRadiusPct << "\n";
    out << "throwSpeedPct = " << sConfig.throwSpeedPct << "\n";
    out << "throwCancelB = " << sConfig.throwCancelB << "\n";
    out << "quickGrab = " << sConfig.quickGrab << "\n";
    out << "noTrip = " << sConfig.noTrip << "\n";
    out << "whistlePluck = " << sConfig.whistlePluck << "\n";
    out << "bombControl = " << sConfig.bombControl << "\n";
    out << "breakableGates = " << sConfig.breakableGates << "\n";
    out << "freeCamPadPct = " << sConfig.freeCamPadPct << "\n";
    out << "p2Selection = " << sConfig.p2Selection << "\n";
    out << "eternalNight = " << sConfig.eternalNight << "\n";
    out << "hideOlimarText = " << sConfig.hideOlimarText << "\n";
    out << "speedrunIntroHidden = " << sConfig.speedrunIntroHidden << "\n";
    out << "onionStep10 = " << sConfig.onionStep10 << "\n";
    out << "instantWhistle = " << sConfig.instantWhistle << "\n";
    out << "pikiInvincible = " << sConfig.pikiInvincible << "\n";
    out << "allFlowers = " << sConfig.allFlowers << "\n";
    out << "carrySpeedPct = " << sConfig.carrySpeedPct << "\n";
    out << "naviSpeedPct = " << sConfig.naviSpeedPct << "\n";
    out << "unlockZones = " << sConfig.unlockZones << "\n";
    out << "noDayAdvance = " << sConfig.noDayAdvance << "\n";
    out << "allOnions = " << sConfig.allOnions << "\n";
    out << "vsDuration = " << sConfig.vsDuration << "\n";
    out << "vsRocketWin = " << sConfig.vsRocketWin << "\n";
    out << "vsRocketHp = " << sConfig.vsRocketHp << "\n";
    out << "vsBigPiece = " << sConfig.vsBigPiece << "\n";
    out << "vsPikiLimit = " << sConfig.vsPikiLimit << "\n";
    out << "vsPellets = " << sConfig.vsPellets << "\n";
    out << "lockOn = " << sConfig.lockOn << "\n";
    out << "charge = " << sConfig.charge << "\n";
    out << "throwWhileMoving = " << sConfig.throwWhileMoving << "\n";
    out << "firstPerson = " << sConfig.firstPerson << "\n";
    out << "mouseWheelAction = " << sConfig.mouseWheelAction << "\n";
    out << "pikiLimit = " << sConfig.pikiLimit << "\n";
    out << "dayLength = " << sConfig.dayMinutes << "\n";
    out << "coopPlayers = " << sConfig.coopPlayers << "\n";
    out << "coopSplit = " << sConfig.coopSplit << "\n";
    out << "coopMergeCamera = " << sConfig.coopMergeCamera << "\n";
    out << "antialiasing = " << sConfig.antialiasing << "\n";
    out << "fog = " << sConfig.fog << "\n";
    out << "perPixelLighting = " << sConfig.perPixelLighting << "\n";
    out << "shadows = " << sConfig.shadows << "\n";
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
    out << "hdModelsDisabled = " << sConfig.hdModelsDisabled << "\n";
    out << "controlMode = " << sConfig.controlMode << "\n";
    out << "mouseSensitivity = " << sConfig.mouseSensitivity << "\n";
    out << "gyroEnabled = " << sConfig.gyroEnabled << "\n";
    out << "gyroSensitivity = " << sConfig.gyroSensitivity << "\n";
    out << "gyroInvert = " << sConfig.gyroInvert << "\n";
    out << "gyroBias = " << sConfig.gyroBias[0] << " " << sConfig.gyroBias[1] << " " << sConfig.gyroBias[2] << "\n";
    out << "stickDeadZone = " << sConfig.stickDeadZone << "\n";
    out << "stickInvert = " << sConfig.stickInvert << "\n";
    out << "cStickInvert = " << sConfig.cStickInvert << "\n";
    // Keyboard bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "key_" << i << " = " << sConfig.keyboardBindings[i] << "\n";
        out << "key2_" << i << " = " << sConfig.keyboardBindings2[i] << "\n";
    }
    // Gamepad bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "gp_" << i << " = " << sConfig.gamepadBindings[i] << "\n";
    }
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "gp2_" << i << " = " << sConfig.gamepadBindingsP2[i] << "\n";
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
        else if (key == "gyroEnabled") {
            sConfig.gyroEnabled = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "gyroSensitivity") {
            sConfig.gyroSensitivity = std::clamp((float)atof(val.c_str()), 0.1f, 5.0f);
        }
        else if (key == "gyroInvert") {
            sConfig.gyroInvert = atoi(val.c_str()) & 3;
        }
        else if (key == "gyroBias") {
            float b[3] = { 0.0f, 0.0f, 0.0f };
            if (sscanf(val.c_str(), "%f %f %f", &b[0], &b[1], &b[2]) == 3) {
                for (int i = 0; i < 3; i++) sConfig.gyroBias[i] = b[i];
            }
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
        else if (key == "betterPathfinding") {
            sConfig.betterPathfinding = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "bluesOnlyWater") {
            sConfig.bluesOnlyWater = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "idleCounter") {
            sConfig.idleCounter = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "naviHealthPct") {
            sConfig.naviHealthPct = clampHealthPct(atoi(val.c_str()));
        }
        else if (key == "tekiHealthPct") {
            sConfig.tekiHealthPct = clampHealthPct(atoi(val.c_str()));
        }
        else if (key == "infiniteDay") {
            sConfig.infiniteDay = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "freeCamera") {
            sConfig.freeCamera = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "whistleRadiusPct") {
            sConfig.whistleRadiusPct = std::clamp(atoi(val.c_str()), 50, 300);
        }
        else if (key == "throwSpeedPct") {
            sConfig.throwSpeedPct = std::clamp(atoi(val.c_str()), 50, 200);
        }
        else if (key == "quickGrab") {
            sConfig.quickGrab = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "throwCancelB") {
            sConfig.throwCancelB = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "noTrip") {
            sConfig.noTrip = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "speedrunIntroHidden") {
            sConfig.speedrunIntroHidden = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "hideOlimarText") {
            sConfig.hideOlimarText = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "breakableGates") {
            sConfig.breakableGates = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "eternalNight") sConfig.eternalNight = atoi(val.c_str()) ? 1 : 0;
        else if (key == "p2Selection") {
            sConfig.p2Selection = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "freeCamPadPct") {
            sConfig.freeCamPadPct = clampFreeCamPadPct(atoi(val.c_str()));
        }
        else if (key == "bombControl") {
            sConfig.bombControl = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "whistlePluck") {
            sConfig.whistlePluck = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "onionStep10") {
            sConfig.onionStep10 = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "instantWhistle") {
            sConfig.instantWhistle = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "pikiInvincible") sConfig.pikiInvincible = atoi(val.c_str()) ? 1 : 0;
        else if (key == "allFlowers") sConfig.allFlowers = atoi(val.c_str()) ? 1 : 0;
        else if (key == "carrySpeedPct") sConfig.carrySpeedPct = clampSpeedPct(atoi(val.c_str()));
        else if (key == "naviSpeedPct") sConfig.naviSpeedPct = clampSpeedPct(atoi(val.c_str()));
        else if (key == "unlockZones") sConfig.unlockZones = atoi(val.c_str()) ? 1 : 0;
        else if (key == "noDayAdvance") sConfig.noDayAdvance = atoi(val.c_str()) ? 1 : 0;
        else if (key == "allOnions") sConfig.allOnions = atoi(val.c_str()) ? 1 : 0;
        else if (key == "vsDuration") sConfig.vsDuration = std::clamp(atoi(val.c_str()), 0, 4);
        else if (key == "vsRocketWin") sConfig.vsRocketWin = atoi(val.c_str()) ? 1 : 0;
        else if (key == "vsRocketHp") sConfig.vsRocketHp = std::clamp(atoi(val.c_str()), 0, 2);
        else if (key == "vsBigPiece") sConfig.vsBigPiece = std::clamp(atoi(val.c_str()), 0, 3);
        else if (key == "vsPikiLimit") sConfig.vsPikiLimit = std::clamp(atoi(val.c_str()), 0, 2);
        else if (key == "vsPellets") sConfig.vsPellets = std::clamp(atoi(val.c_str()), 0, 3);
        else if (key == "lockOn") {
            // 0 = off, 1 = manual (lo que era "On"), 2 = automático.
            sConfig.lockOn = std::clamp(atoi(val.c_str()), 0, 2);
        }
        else if (key == "charge") {
            sConfig.charge = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "throwWhileMoving") {
            sConfig.throwWhileMoving = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "firstPerson") {
            sConfig.firstPerson = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "mouseWheelAction") {
            sConfig.mouseWheelAction = atoi(val.c_str());
            if (sConfig.mouseWheelAction < 0 || sConfig.mouseWheelAction > 1) sConfig.mouseWheelAction = 0;
        }
        else if (key == "pikiLimit") {
            sConfig.pikiLimit = atoi(val.c_str());
            if (sConfig.pikiLimit < 50 || sConfig.pikiLimit > PIKI_FIELD_LIMIT_MAX) sConfig.pikiLimit = 100;
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
        else if (key == "perPixelLighting") {
            sConfig.perPixelLighting = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "shadows") {
            sConfig.shadows = std::clamp(atoi(val.c_str()), 0, 3);
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
        else if (key == "dayLength") {
            sConfig.dayMinutes = atoi(val.c_str());
            if (sConfig.dayMinutes < 0 || sConfig.dayMinutes > 120) sConfig.dayMinutes = 0;
        }
        // Clave antigua: 10 era el falso "original", asi que pasa al original real.
        else if (key == "dayMinutes") {
            sConfig.dayMinutes = atoi(val.c_str());
            if (sConfig.dayMinutes == 10 || sConfig.dayMinutes < 1 || sConfig.dayMinutes > 120) sConfig.dayMinutes = 0;
        }
        else if (key == "coopPlayers") {
            sConfig.coopPlayers = atoi(val.c_str()) == 2 ? 2 : 1;
        }
        else if (key == "coopSplit") {
            sConfig.coopSplit = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "coopMergeCamera") {
            sConfig.coopMergeCamera = atoi(val.c_str()) ? 1 : 0;
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
        else if (key == "hdModelsDisabled") {
            sConfig.hdModelsDisabled = atoi(val.c_str()) & 0x3F;
        }
        else if (key == "texturePackEnabled") {
            sConfig.texturePackEnabled = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "stickInvert") sConfig.stickInvert = atoi(val.c_str()) & 3;
        else if (key == "cStickInvert") sConfig.cStickInvert = atoi(val.c_str()) & 3;
        else if (key.rfind("key2_", 0) == 0) {
            int idx = atoi(key.substr(5).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int scancode = atoi(val.c_str());
                if (pc_bind_is_valid(scancode)) {
                    sConfig.keyboardBindings2[idx] = scancode;
                }
            }
        }
        else if (key.rfind("key_", 0) == 0) {
            int idx = atoi(key.substr(4).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int scancode = atoi(val.c_str());
                if (pc_bind_is_valid(scancode)) {
                    sConfig.keyboardBindings[idx] = scancode;
                }
            }
        }
        else if (key.rfind("gp_", 0) == 0 || key.rfind("gp2_", 0) == 0) {
            const bool p2 = key[2] == '2';
            int idx = atoi(key.substr(p2 ? 4 : 3).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int button = atoi(val.c_str());
                const bool isButton = button >= -1 && button < SDL_CONTROLLER_BUTTON_MAX;
                const int axis = (button - PC_GP_AXIS_BIND) / 2;
                const bool isAxis = button >= PC_GP_AXIS_BIND && axis >= 0 && axis < SDL_CONTROLLER_AXIS_MAX;
                if (isButton || isAxis) {
                    (p2 ? sConfig.gamepadBindingsP2 : sConfig.gamepadBindings)[idx] = button;
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
void pcErasedNoticeInput();
void pcSpeedrunIntroInput();
void pcPlayerCountPromptInput();
void pcDevAssignPromptInput();

static bool sToggleRequested = false;
static bool sTouchTapPending = false;
static float sTouchTapX = 0.0f, sTouchTapY = 0.0f;
static float sTouchDragY = 0.0f; // acumulado entre lecturas, normalizado

// Entradas de la columna izquierda de F1: cabecera de sección (-1 - fila a la
// que precede) o fila (>= 0).
int f1Items(int group, int* out, int max) {
    int k = 0;
    const int n = pc_settings_rows_count(group);
    for (int r = 0; r < n && k < max; r++) {
        if (rowSection(group, r) && k < max) out[k++] = -1 - r;
        if (k < max) out[k++] = r;
    }
    return k;
}

// Ajusta sF1Scroll para que la fila seleccionada (y su cabecera, si la
// encabeza) quede a la vista.
void f1ClampScroll() {
    int items[96];
    const int n = f1Items(sOpenGroup, items, 96);
    int at = 0;
    for (int k = 0; k < n; k++) if (items[k] == sGroupSel) { at = k; break; }
    const int top = (at > 0 && items[at - 1] == -1 - sGroupSel) ? at - 1 : at;
    if (top < sF1Scroll) sF1Scroll = top;
    if (at >= sF1Scroll + kF1Visible) sF1Scroll = at - kF1Visible + 1;
    if (sF1Scroll > n - kF1Visible) sF1Scroll = n - kF1Visible;
    if (sF1Scroll < 0) sF1Scroll = 0;
}

// Opciones de una fila para la columna derecha: se recorren sus valores con
// la misma lógica de izquierda/derecha sobre sPending y luego se restaura.
// Se guardan por fila para que la lista no se reordene al cambiar de valor.
struct F1Options {
    int group = -1, row = -1;
    std::vector<std::string> values;
};
F1Options sF1Opts;
constexpr int kF1MaxOptions = 64;

void f1ProbeOptions(int group, int row) {
    sF1Opts.group = group;
    sF1Opts.row = row;
    sF1Opts.values.clear();
    if (!rowProbeable(group, row) || !pc_settings_row_enabled(group, row)) return;
    const PcConfig saved = sPending;
    const int savedRes = sResolutionIdx;
    const unsigned char savedLanguage = pc_settings_get_language();
    sProbing = true;
    char cur[128], prev[128], v[128];
    pc_settings_row_value(group, row, cur, sizeof(cur));
    // Hacia la izquierda hasta dar la vuelta (lista cíclica) o hacer tope.
    std::vector<std::string> left, right;
    bool cyclic = false;
    snprintf(prev, sizeof(prev), "%s", cur);
    for (int i = 0; i < kF1MaxOptions; i++) {
        pc_settings_row_change(group, row, -1, false);
        pc_settings_row_value(group, row, v, sizeof(v));
        if (!strcmp(v, prev)) break;
        if (!strcmp(v, cur)) { cyclic = true; break; }
        left.push_back(v);
        snprintf(prev, sizeof(prev), "%s", v);
    }
    sPending = saved;
    sResolutionIdx = savedRes;
    pc_settings_set_language(savedLanguage);
    if (!cyclic) {
        snprintf(prev, sizeof(prev), "%s", cur);
        for (int i = 0; i < kF1MaxOptions; i++) {
            pc_settings_row_change(group, row, 1, false);
            pc_settings_row_value(group, row, v, sizeof(v));
            if (!strcmp(v, prev) || !strcmp(v, cur)) break;
            right.push_back(v);
            snprintf(prev, sizeof(prev), "%s", v);
        }
        sPending = saved;
        sResolutionIdx = savedRes;
        pc_settings_set_language(savedLanguage);
    }
    sProbing = false;
    if (cyclic) {
        // Orden de "derecha" empezando por el valor actual.
        sF1Opts.values.push_back(cur);
        for (auto it = left.rbegin(); it != left.rend(); ++it) sF1Opts.values.push_back(*it);
    } else {
        for (auto it = left.rbegin(); it != left.rend(); ++it) sF1Opts.values.push_back(*it);
        sF1Opts.values.push_back(cur);
        sF1Opts.values.insert(sF1Opts.values.end(), right.begin(), right.end());
    }
}

// Opciones de una fila y cuál es la actual (-1 si la fila no se puede
// listar: acciones, selectores, filas desactivadas).
const std::vector<std::string>& f1Options(int group, int row, int* current) {
    if (rowIsResolution(group, row)) {
        // Resolución: todas las del selector (dependen del modo pendiente, así
        // que se rehacen cada vez). La actual lleva "  <" al final del valor.
        sF1Opts.group = group;
        sF1Opts.row = row;
        sF1Opts.values.clear();
        int at = -1;
        const int n = pc_settings_rows_count(PC_SET_PICKER_RESOLUTION);
        for (int k = 0; k < n; k++) {
            char v[128];
            pc_settings_row_value(PC_SET_PICKER_RESOLUTION, k, v, sizeof(v));
            const size_t len = strlen(v);
            if (len >= 3 && !strcmp(v + len - 3, "  <")) { v[len - 3] = '\0'; at = k; }
            sF1Opts.values.push_back(std::string(pc_settings_row_label(PC_SET_PICKER_RESOLUTION, k)) + "  " + v);
        }
        if (at < 0 && n > 0) at = pc_settings_picker_current(PC_SET_PICKER_RESOLUTION);
        *current = pc_settings_row_enabled(group, row) ? at : -1;
        return sF1Opts.values;
    }
    char cur[128];
    pc_settings_row_value(group, row, cur, sizeof(cur));
    auto find = [&] {
        for (size_t k = 0; k < sF1Opts.values.size(); k++)
            if (sF1Opts.values[k] == cur) return (int)k;
        return -1;
    };
    int at = -1;
    if (sF1Opts.group == group && sF1Opts.row == row) at = find();
    // Fila nueva, o el valor cambió a uno que no estaba (p. ej. la lista
    // depende de otro ajuste): volver a sondear.
    if (sF1Opts.group != group || sF1Opts.row != row || (at < 0 && !sF1Opts.values.empty())) {
        f1ProbeOptions(group, row);
        at = find();
    }
    // Una fila desactivada solo enseña su valor, no una lista que no cambia.
    *current = pc_settings_row_enabled(group, row) ? at : -1;
    return sF1Opts.values;
}

// Elige la opción `target` pulsada en la columna derecha: se avanza en
// sondeo hasta la anterior y el último paso es real, para que el vídeo se
// aplique (y se confirme) una sola vez.
void f1PickOption(int group, int row, int target) {
    int at = -1;
    const std::vector<std::string> values = f1Options(group, row, &at);
    if (at < 0 || target < 0 || target >= (int)values.size() || target == at) return;
    if (rowIsResolution(group, row)) {
        pc_settings_row_change(PC_SET_PICKER_RESOLUTION, target, 0, true);
        return;
    }
    const int dir = target > at ? 1 : -1;
    const int steps = target > at ? target - at : at - target;
    sProbing = true;
    for (int i = 0; i < steps - 1; i++) pc_settings_row_change(group, row, dir, false);
    sProbing = false;
    pc_settings_row_change(group, row, dir, false);
}

// Ventana de opciones visible en la columna derecha, centrada en `center`.
void f1OptWindow(int count, int center, int* first, int* shown) {
    *shown = count < kF1OptVisible ? count : kF1OptVisible;
    *first = center - *shown / 2;
    if (*first > count - *shown) *first = count - *shown;
    if (*first < 0) *first = 0;
}

void f1SwitchTab(int dir) {
    sOpenGroup = (sOpenGroup + dir + PC_SET_GROUP_COUNT) % PC_SET_GROUP_COUNT;
    sGroupSel = 0;
    sF1Scroll = 0;
    sF1OptFocus = false;
}

// VS (definidos más abajo, junto a su dibujo).
void pcVsRulesInput();
void pcVsEndScreenInput();
bool vsEndScreenShown();

void pollMenuInput() {
    sTouchFrameButtons = sTouchButtons;
    sTouchButtons = 0;
    // Coop: el Select del mando de J2 también abre el menú, y entonces lo
    // maneja su mando y edita sus bindings.
    SDL_GameController* ctl1 = pc_window_get_controller();
    SDL_GameController* ctl2 = pc_window_get_controller_p2();
    if (ctl2 == ctl1) ctl2 = nullptr;
    SDL_GameController* ctl = sMenuOpen ? menuController() : ctl1;
    const bool toggleRequested = sToggleRequested;
    sToggleRequested = false;
    const bool menuToggleHeld = ctl1 && SDL_GameControllerGetButton(
        ctl1, SDL_CONTROLLER_BUTTON_BACK) != 0;
    const bool menuToggleHeldP2 = ctl2 && SDL_GameControllerGetButton(
        ctl2, SDL_CONTROLLER_BUTTON_BACK) != 0;
    const bool togglePressedP1 = menuToggleHeld && !sPrevMenuToggleHeld;
    const bool togglePressedP2 = menuToggleHeldP2 && !sPrevMenuToggleHeldP2;
    sPrevMenuToggleHeldP2 = menuToggleHeldP2;
    // Abierto, solo lo cierra el Select de quien lo abrió.
    const bool menuTogglePressed = sMenuOpen ? (sMenuPlayer == 1 && ctl2 ? togglePressedP2 : togglePressedP1)
                                             : (togglePressedP1 || togglePressedP2);
    // Latch before every modal early return.  A Select press used while the
    // new-game/video/capture modal owns input must not become a fresh press
    // when that modal exits while the button is still held.
    sPrevMenuToggleHeld = menuToggleHeld;

    if (pc_erased_notice_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcErasedNoticeInput();
        return;
    }
    if (pc_speedrun_intro_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcSpeedrunIntroInput();
        return;
    }
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
    if (pc_playercount_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcPlayerCountPromptInput();
        return;
    }
    if (pc_devassign_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcDevAssignPromptInput();
        return;
    }
    if (pc_captain_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcCaptainPromptInput();
        return;
    }
    if (pc_vsrules_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcVsRulesInput();
        return;
    }
    if (vsEndScreenShown()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcVsEndScreenInput();
        return;
    }
    // Menú de cristal del título (Advanced Options): es dueño de la entrada
    // mientras está abierto; F1 no lo pisa.
    if (pc_glass_menu_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_port_menu();
#endif
        pc_glass_menu_input();
        return;
    }

    auto openMenu = [] {
        sPending = sConfig;
        sPending.controlMode = pc_window_get_control_mode();
        sMenuOpen = true;
        pc_window_set_settings_menu_open(true);
        sOpenGroup = PC_SET_GROUP_DISPLAY;
        sGroupSel = 0;
        sF1Scroll = 0;
        sF1OptFocus = false;
        sVideoConfirmActive = false;
        rebuildResolutionList();
        const int idx = resolutionIndexFor(pc_window_get_width(), pc_window_get_height());
        sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
    };

    // F1 always toggles. Select/View can open the menu while it is closed;
    // closing is handled after video confirmation has had first refusal.
    if (keyWentDown(SDL_SCANCODE_F1) || toggleRequested) {
        if (sMenuOpen) closeMenu();
        else {
            sMenuPlayer = 0;
            openMenu();
        }
        return;
    }
    if (menuTogglePressed && !sMenuOpen) {
        sMenuPlayer = togglePressedP2 && !togglePressedP1 ? 1 : 0;
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
            pollKeyCapture(ctl);
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
        if (ok || right) {
            // A: tecla principal. Derecha: segunda tecla (issue #68).
            startKeyCapture(sControlSelection, right && !ok);
            return;
        }
        if (left) {
            // Izquierda: las dos vuelven a su valor por defecto.
            sPending.keyboardBindings[sControlSelection] = kDefaultKeyBindings[sControlSelection];
            sPending.keyboardBindings2[sControlSelection] = pc_window_default_key_binding2(sControlSelection);
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
        if (pollButtonCapture(ctl)) return;

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
            pendingPadBinds()[sGamepadSelection] = -1;
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

    // HD model restart prompt.
    if (sHdModelRestartPrompt) {
        bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);
        if (ctl || sTouchFrameButtons) {
            if (padNavA(ctl)) accept = true;
            if (padNavB(ctl)) cancel = true;
        }
        if (accept && !cancel) {
            sHdModelRestartPrompt = false;
#ifdef __ANDROID__
            pc_texpack_android_restart();
#else
            texturePackNotice(false, "HD model installed. Restart the game to apply it.");
#endif
        } else if (cancel) {
            sHdModelRestartPrompt = false;
        }
        return;
    }

    if (sInHdModelsSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);
        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl)) up = true;
            if (padNavDown(ctl)) down = true;
            if (padNavA(ctl)) ok = true;
            if (padNavB(ctl)) cancel = true;
        }
        const int rowCount = 6;
        if (up) { sHdModelsSelection = (sHdModelsSelection + rowCount - 1) % rowCount; return; }
        if (down) { sHdModelsSelection = (sHdModelsSelection + 1) % rowCount; return; }
        if (cancel) { sInHdModelsSubmenu = false; return; }
        if (ok) hdModelsRowAction(sHdModelsSelection);
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

        if (ok || left || right) texturePacksRowAction(sTexturePacksSelection, packs);
        return;
    }

    // Página de pestañas: la lista de la pestaña usa pc_settings_rows, la
    // misma que el menú de cristal del título. Los selectores (resolución,
    // packs, modelos, teclas) siguen siendo los submenús propios de arriba.
    if (sOpenGroup < 0) sOpenGroup = PC_SET_GROUP_DISPLAY;
    f1Layout();
    bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                  keyWentDown(SDL_SCANCODE_B);
    bool tabPrev = keyWentDown(SDL_SCANCODE_Q) || keyWentDown(SDL_SCANCODE_PAGEUP);
    bool tabNext = keyWentDown(SDL_SCANCODE_E) || keyWentDown(SDL_SCANCODE_PAGEDOWN) ||
                   keyWentDown(SDL_SCANCODE_TAB);
    // Ratón: rueda = arriba/abajo, botón derecho = atrás (el izquierdo llega
    // como toque desde pc_window).
    if (const int wheel = pc_window_take_wheel_steps()) (wheel > 0 ? up : down) = true;
    if (pc_window_take_mouse_pressed() & SDL_BUTTON(SDL_BUTTON_RIGHT)) cancel = true;
    if (ctl || sTouchFrameButtons) {
        if (padNavUp(ctl)) up = true;
        if (padNavDown(ctl)) down = true;
        if (padNavLeft(ctl)) left = true;
        if (padNavRight(ctl)) right = true;
        if (padNavA(ctl)) ok = true;
        if (padNavB(ctl)) cancel = true;
        if (padEdge((ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
                    || (sTouchFrameButtons & PAD_TRIGGER_L), 10)) tabPrev = true;
        if (padEdge((ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
                    || (sTouchFrameButtons & PAD_TRIGGER_R), 11)) tabNext = true;
    }

    if (sTouchTapPending) {
        sTouchTapPending = false;
        int tx = 0, ty = 0;
        f1TapToCanvas(sTouchTapX, sTouchTapY, &tx, &ty);
        const int closeX = kF1PanelX + kF1PanelW - 18 - kF1CloseW;
        if (ty >= kF1TabY && ty < kF1TabY + kF1TabH) {
            if (tx >= closeX && tx < closeX + kF1CloseW) { closeMenu(); return; }
            const int tab = tx >= kF1TabX ? (tx - kF1TabX) / kF1TabW : -1;
            if (tab >= 0 && tab < PC_SET_GROUP_COUNT && tab != sOpenGroup) {
                sOpenGroup = tab;
                sGroupSel = 0;
                sF1Scroll = 0;
                sF1OptFocus = false;
            }
            return;
        }
        if (tx >= kF1LeftX && tx < kF1LeftX + kF1LeftW && ty >= kF1ColY + 8) {
            // Tocar una fila la selecciona; tocar la ya seleccionada es A.
            int items[96];
            const int n = f1Items(sOpenGroup, items, 96);
            const int k = sF1Scroll + (ty - kF1ColY - 8) / kF1ItemH;
            if (k < n && k < sF1Scroll + kF1Visible && items[k] >= 0) {
                if (items[k] == sGroupSel && !sF1OptFocus) ok = true;
                else { sGroupSel = items[k]; sF1OptFocus = false; return; }
            }
        } else if (tx >= kF1RightX && tx < kF1RightX + kF1RightW && ty >= kF1ColY + 8) {
            int at = -1;
            const int count = (int)f1Options(sOpenGroup, sGroupSel, &at).size();
            if (at >= 0) {
                int first = 0, shown = 0;
                f1OptWindow(count, sF1OptFocus ? sF1OptSel : at, &first, &shown);
                const int k = (ty - kF1ColY - 8) / kF1OptH;
                if (k < shown) f1PickOption(sOpenGroup, sGroupSel, first + k);
                sF1OptFocus = false;
                return;
            }
            ok = true; // filas sin lista: la placa de valor hace de botón
        }
    }

    if (sF1OptFocus) {
        int at = -1;
        const int count = (int)f1Options(sOpenGroup, sGroupSel, &at).size();
        if (at < 0 || count <= 0) sF1OptFocus = false;
        else if (tabPrev || tabNext) sF1OptFocus = false; // sigue abajo: cambia de pestaña
        else {
            if (cancel || left) sF1OptFocus = false;
            else if (up) sF1OptSel = (sF1OptSel + count - 1) % count;
            else if (down) sF1OptSel = (sF1OptSel + 1) % count;
            else if (ok) {
                f1PickOption(sOpenGroup, sGroupSel, sF1OptSel);
                sF1OptFocus = false;
            }
            return;
        }
    }

    // B / Esc cierra el menú y guarda (un cambio de vídeo sin confirmar se revierte).
    if (cancel) { closeMenu(); return; }
    if (tabPrev || tabNext) { f1SwitchTab(tabNext ? 1 : -1); return; }

    const int n = pc_settings_rows_count(sOpenGroup);
    if (n <= 0) return;
    if (sGroupSel >= n) sGroupSel = n - 1;
    if (up) { sGroupSel = (sGroupSel + n - 1) % n; f1ClampScroll(); return; }
    if (down) { sGroupSel = (sGroupSel + 1) % n; f1ClampScroll(); return; }
    if (!left && !right && !ok) return;
    if (!pc_settings_row_enabled(sOpenGroup, sGroupSel)) return;
    {
        // Filas con lista de opciones: derecha (o A) lleva el cursor a la
        // columna derecha; izquierda no hace nada aquí, es la que vuelve.
        int at = -1;
        if (!f1Options(sOpenGroup, sGroupSel, &at).empty() && at >= 0) {
            if (ok || right) {
                sF1OptFocus = true;
                sF1OptSel = at;
            }
            return;
        }
    }

    const int picker = pc_settings_row_opens_picker(sOpenGroup, sGroupSel);
    // La resolución abre la lista con A y con izquierda/derecha salta a la
    // contigua; los demás selectores se abren con A o derecha.
    if (picker == PC_SET_PICKER_RESOLUTION && !ok) {
        pc_settings_row_change(sOpenGroup, sGroupSel, left ? -1 : 1, false);
        return;
    }
    if (picker) {
        if (!ok && !right) return;
        switch (picker) {
        case PC_SET_PICKER_RESOLUTION: openResolutionSubmenu(); break;
        case PC_SET_PICKER_TEXPACKS: graphicsRowChange(10, false, false, true); break;
        case PC_SET_PICKER_HDMODELS: graphicsRowChange(11, false, false, true); break;
        case PC_SET_PICKER_KEYBOARD:
            sInControlsSubmenu = true;
            sControlSelection = pc_settings_picker_current(picker);
            sWaitingForKey = false;
            sCaptureWaitRelease = false;
            break;
        case PC_SET_PICKER_GAMEPAD:
            sInGamepadSubmenu = true;
            sGamepadSelection = pc_settings_picker_current(picker);
            sWaitingForButton = false;
            sCaptureWaitRelease = false;
            break;
        default: break;
        }
        return;
    }
    if (pc_settings_row_is_action(sOpenGroup, sGroupSel)) {
        if (ok) pc_settings_row_change(sOpenGroup, sGroupSel, 0, true);
        return;
    }
    if (left) pc_settings_row_change(sOpenGroup, sGroupSel, -1, false);
    else if (right) pc_settings_row_change(sOpenGroup, sGroupSel, 1, false);
    else pc_settings_row_change(sOpenGroup, sGroupSel, 0, true);
}

// Captura de tecla/botón de ratón para la fila seleccionada (Controls).
void pollKeyCapture(SDL_GameController* ctl) {
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
    // Issue #68: Shift, Ctrl y Alt también se pueden asignar, y Supr o
    // Retroceso dejan la tecla vacía (acción desactivada en esa ranura).
    int* slot = sCaptureSecond ? sPending.keyboardBindings2 : sPending.keyboardBindings;
    for (int sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
        if (!keyWentDown(static_cast<SDL_Scancode>(sc)))
            continue;
        if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE)
            sc = SDL_SCANCODE_UNKNOWN;
        slot[sControlSelection] = sc;
        sWaitingForKey = false;
        break;
    }
    // Mouse buttons are bindable too (issue #42). Capture starts from
    // Enter/Space/pad A, never from a click, so every button is fair
    // game; buttons already held when capture opened are ignored.
    // Use the event edge mask so a click shorter than a frame counts.
    if (sWaitingForKey) {
        const Uint32 mouseWent = pc_window_take_mouse_pressed() & ~sCapturePrevMouse;
        sCapturePrevMouse = SDL_GetMouseState(NULL, NULL);
        for (int b = SDL_BUTTON_LEFT; b <= PC_BIND_MOUSE_LAST - PC_BIND_MOUSE_BASE; b++) {
            if (!(mouseWent & SDL_BUTTON(b))) continue;
            slot[sControlSelection] = PC_BIND_MOUSE_BASE + b;
            sWaitingForKey = false;
            break;
        }
    }
}

// Captura de botón de mando (Gamepad); true mientras la captura o la
// espera de soltar consumen la entrada.
bool pollButtonCapture(SDL_GameController* ctl) {
    if (sWaitingForButton) {
        // B/Circle is a bindable face button. Only Esc cancels capture.
        if (keyWentDown(SDL_SCANCODE_ESCAPE)) {
            sWaitingForButton = false;
            sCaptureWaitRelease = false;
            return true;
        }
        if (sCaptureWaitRelease) {
            if (!captureConfirmHeld(ctl))
                sCaptureWaitRelease = false;
            return true;
        }
        if (ctl || sTouchFrameButtons) {
            const int bind = pc_window_gamepad_first_held_binding(ctl);
            if (bind >= 0) {
                pendingPadBinds()[sGamepadSelection] = bind;
                sWaitingForButton = false;
                sCaptureWaitRelease = true;
            }
        }
        return true;
    }

    // The button just bound is still held; do not treat it as Back.
    if (sCaptureWaitRelease) {
        if (!pc_window_gamepad_any_held(ctl) && !captureConfirmHeld(ctl))
            sCaptureWaitRelease = false;
        return true;
    }

    return false;
}

void advancedRowChange(int row, bool left, bool right) {
    // Sensitivity (0.1 - 5.0, step 0.1)
    if (row == 0) {
        float step = 0.1f;
        if (left) sPending.mouseSensitivity = fmaxf(0.1f, sPending.mouseSensitivity - step);
        if (right) sPending.mouseSensitivity = fminf(5.0f, sPending.mouseSensitivity + step);
    }
    // Stick dead zone (0 - 127, step 4)
    else if (row == 1) {
        int step = 4;
        if (left) sPending.stickDeadZone = std::max(0, sPending.stickDeadZone - step);
        if (right) sPending.stickDeadZone = std::min(127, sPending.stickDeadZone + step);
    }
    // Stick invert (bitmask)
    else if (row == 2) {
        if (left || right) {
            sPending.stickInvert ^= 3; // toggle X and Y bits
        }
    }
    // C-stick invert (bitmask)
    else if (row == 3) {
        if (left || right) {
            sPending.cStickInvert ^= 3; // toggle X and Y bits
        }
    }
    // Gyro on/off
    else if (row == 4) {
        if (left || right) sPending.gyroEnabled = sPending.gyroEnabled ? 0 : 1;
    }
    // Gyro sensitivity (0.1 - 5.0, step 0.1)
    else if (row == 5) {
        if (left) sPending.gyroSensitivity = fmaxf(0.1f, sPending.gyroSensitivity - 0.1f);
        if (right) sPending.gyroSensitivity = fminf(5.0f, sPending.gyroSensitivity + 0.1f);
    }
    // Gyro invert: cycles None, X, Y, X+Y so each axis can be set alone.
    else if (row == 6) {
        if (right) sPending.gyroInvert = (sPending.gyroInvert + 1) & 3;
        if (left) sPending.gyroInvert = (sPending.gyroInvert + 3) & 3;
    }
    // Gyro calibrate: measures the resting drift of the active sensor.
    else if (row == 7) {
        if (left || right) pc_gyro_calibrate_start();
    }
}

void graphicsRowChange(int row, bool left, bool right, bool ok) {
    // Texture packs lives in the Graphics page: it is a rendering choice,
    // it needs restarting to take effect, and it is opened rather than
    // cycled so the install entry has room next to the pack list.
    if (row == 10) {
        if (ok) {
#if !defined(__ANDROID__)
            // Create the folder the manual-install instruction names, so a
            // user who goes looking for it finds it (issue #35).
            std::error_code shareEc;
            std::filesystem::create_directories(
                std::filesystem::path("Load") / "Textures", shareEc);
#endif
            sInTexturePacksSubmenu = true;
            sTexturePacksSelection = 0;
        }
        return;
    }
    if (row == 11) {
        if (ok) {
            sInHdModelsSubmenu = true;
            sHdModelsSelection = 0;
            // Zips dropped straight into Load/Models are converted here
            // too, so the rows below reflect them without a restart.
            pc_hd_models_convert_sources();
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

    if (row == 12) {
        if (left || right) sPending.perPixelLighting = sPending.perPixelLighting ? 0 : 1;
    } else if (row == 13) {
        if (left) sPending.shadows = (sPending.shadows + 3) % 4;
        else if (right) sPending.shadows = (sPending.shadows + 1) % 4;
    } else if (row == 0) {
        if (left || right) sPending.antialiasing = sPending.antialiasing ? 0 : 1;
    } else if (row == 1) {
        if (left || right) sPending.fog = sPending.fog ? 0 : 1;
    } else if (row == 2) {
        if (left) sPending.bloom = (sPending.bloom + 3) % 4;
        else if (right) sPending.bloom = (sPending.bloom + 1) % 4;
    } else if (row == 3) {
        if (left) sPending.ssao = (sPending.ssao + 3) % 4;
        else if (right) sPending.ssao = (sPending.ssao + 1) % 4;
    } else if (row == 4) {
        if (left) sPending.dof = (sPending.dof + 3) % 4;
        else if (right) sPending.dof = (sPending.dof + 1) % 4;
    } else if (row == 5) {
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
    } else if (row == 6) {
        if (left || right) sPending.colourGrading = sPending.colourGrading ? 0 : 1;
    } else if (row == 7) {
        if (left || right) sPending.gamma = step(sPending.gamma, kGammaStops, kGammaStopCount, left);
    } else if (row == 8) {
        if (left || right) sPending.brightness = step(sPending.brightness, kBrightnessStops, kBrightnessStopCount, left);
    } else if (row == 9) {
        if (left || right) sPending.saturation = step(sPending.saturation, kSaturationStops, kSaturationStopCount, left);
    }
    // Applied as you move, so the effect can be judged against the scene
    // behind the menu instead of by reading numbers.
    applyGraphics(sPending);
}

void modsRowChange(int row, bool left, bool right) {
    // Control scheme: Classic (GameCube) or Mouse Cursor.
    if (row == 0) {
        if (left) sPending.controlMode = (sPending.controlMode - 1 + 2) % 2;
        else if (right) sPending.controlMode = (sPending.controlMode + 1) % 2;
    }
    // Chain Pikmin actions.
    else if (row == 1) {
        if (left || right) sPending.chainActions = sPending.chainActions ? 0 : 1;
    }
    // Hold Extract to keep plucking after the first sprout.
    else if (row == 2) {
        if (left || right) sPending.holdToPluck = sPending.holdToPluck ? 0 : 1;
    }
    // What the mouse wheel controls.
    else if (row == 3) {
        if (left || right) sPending.mouseWheelAction = sPending.mouseWheelAction ? 0 : 1;
    }
    // Pikmin field limit. Stepped through meaningful values rather than one
    // at a time: the menu has no key repeat, so a fine slider would take
    // hundreds of presses to cross the range.
    else if (row == 4) {
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
    else if (row == 5) {
        if (pc_hardmode_active())
            return;
        // Una parada más tras la lista: Infinite (el antiguo Infinite Day).
        const int stops = kDayMinutesCount + 1;
        int idx = 0;
        if (sPending.infiniteDay) idx = kDayMinutesCount;
        else for (int i = 0; i < kDayMinutesCount; i++) {
            if (kDayMinutes[i] == sPending.dayMinutes) { idx = i; break; }
        }
        if (left) idx = (idx + stops - 1) % stops;
        else if (right) idx = (idx + 1) % stops;
        sPending.infiniteDay = idx == kDayMinutesCount ? 1 : 0;
        if (!sPending.infiniteDay) sPending.dayMinutes = kDayMinutes[idx];
    }
    // Pantalla partida cooperativa: vertical u horizontal.
    else if (row == 6) {
        if (left || right) sPending.coopSplit = sPending.coopSplit ? 0 : 1;
    }
    // Cámara cooperativa dinámica.
    else if (row == 7) {
        if (left || right) sPending.coopMergeCamera = sPending.coopMergeCamera ? 0 : 1;
    }
    // Desatascar Pikmin que dejan de avanzar por su ruta.
    else if (row == 8) {
        if (left || right) sPending.betterPathfinding = sPending.betterPathfinding ? 0 : 1;
    }
    // Solo los azules entran al agua por su cuenta.
    else if (row == 9) {
        if (left || right) sPending.bluesOnlyWater = sPending.bluesOnlyWater ? 0 : 1;
    }
    // Contador de Pikmin ociosos en el HUD.
    else if (row == 10) {
        if (left || right) sPending.idleCounter = sPending.idleCounter ? 0 : 1;
    }
    // Vida de Olimar, en porcentaje de la original.
    else if (row == 11) {
        if (pc_hardmode_active())
            return;
        if (left || right) sPending.naviHealthPct = stepHealthPct(sPending.naviHealthPct, left);
    }
    // Vida de los enemigos, en porcentaje de la original.
    else if (row == 12) {
        if (pc_hardmode_active())
            return;
        if (left || right) sPending.tekiHealthPct = stepHealthPct(sPending.tekiHealthPct, left);
    }
    // El dia no avanza.
    else if (row == 13) {
        if (pc_hardmode_active())
            return;
        if (left || right) sPending.infiniteDay = sPending.infiniteDay ? 0 : 1;
    }
    // Camara libre.
    else if (row == 14) {
        if (left || right) sPending.freeCamera = sPending.freeCamera ? 0 : 1;
    }
    // Fijar objetivo.
    else if (row == 15) {
        if (right) sPending.lockOn = (sPending.lockOn + 1) % 3;
        if (left) sPending.lockOn = (sPending.lockOn + 2) % 3;
    }
    // Mandar el escuadrón contra el objetivo fijado.
    else if (row == 16) {
        if (left || right) sPending.charge = sPending.charge ? 0 : 1;
    }
    // Relevo del lanzamiento con el capitán en marcha.
    else if (row == 17) {
        if (left || right) sPending.throwWhileMoving = sPending.throwWhileMoving ? 0 : 1;
    }
    // Vista en primera persona.
    else if (row == 18) {
        if (left || right) sPending.firstPerson = sPending.firstPerson ? 0 : 1;
    }
    // Debug shortcuts.
    else if (row == 19) {
        if (left || right) sPending.debugKeys = sPending.debugKeys ? 0 : 1;
    }
    else if (row == 20) {
        if (left || right) sPending.whistleRadiusPct = stepPct(sPending.whistleRadiusPct, kWhistlePcts, kWhistlePctCount, left);
    }
    else if (row == 21) {
        if (left || right) sPending.throwSpeedPct = stepPct(sPending.throwSpeedPct, kThrowSpeedPcts, kThrowSpeedCount, left);
    }
    else if (row == 22) {
        if (left || right) sPending.throwCancelB = sPending.throwCancelB ? 0 : 1;
    }
    else if (row == 33) {
        if (left || right) sPending.quickGrab = sPending.quickGrab ? 0 : 1;
    }
    else if (row == 23) {
        if (left || right) sPending.noTrip = sPending.noTrip ? 0 : 1;
    }
    else if (row == 24) {
        if (left || right) sPending.onionStep10 = sPending.onionStep10 ? 0 : 1;
    }
    else if (row == 25) {
        if (left || right) sPending.instantWhistle = sPending.instantWhistle ? 0 : 1;
    }
    else if (row == 34) {
        if (left || right) sPending.whistlePluck = sPending.whistlePluck ? 0 : 1;
    }
    else if (row == 35) {
        if (left || right) sPending.bombControl = sPending.bombControl ? 0 : 1;
    }
    else if (row == 36) {
        if (left || right) sPending.hideOlimarText = sPending.hideOlimarText ? 0 : 1;
    }
    else if (row == 37) {
        if (!pc_hardmode_active() && (left || right)) sPending.breakableGates = sPending.breakableGates ? 0 : 1;
    }
    else if (row == 40) {
        if (left || right) sPending.eternalNight = sPending.eternalNight ? 0 : 1;
    }
    else if (row == 39) {
        if (left || right) sPending.p2Selection = sPending.p2Selection ? 0 : 1;
    }
    else if (row == 38) {
        if (left || right) sPending.freeCamPadPct = stepPct(sPending.freeCamPadPct, kFreeCamPadPcts, kFreeCamPadPctCount, left);
    }
    // Cheats (26-32). Hard los anula, como la vida y el día.
    else if (row >= 26 && row <= 32) {
        if (pc_hardmode_active() || !(left || right))
            return;
        switch (row) {
        case 26: sPending.pikiInvincible = !sPending.pikiInvincible; break;
        case 27: sPending.allFlowers = !sPending.allFlowers; break;
        case 28: sPending.carrySpeedPct = stepPct(sPending.carrySpeedPct, kSpeedPcts, kSpeedPctCount, left); break;
        case 29: sPending.naviSpeedPct = stepPct(sPending.naviSpeedPct, kSpeedPcts, kSpeedPctCount, left); break;
        case 30: sPending.unlockZones = !sPending.unlockZones; break;
        case 31: sPending.noDayAdvance = !sPending.noDayAdvance; break;
        case 32: sPending.allOnions = !sPending.allOnions; break;
        }
    }
}

void saveDataRowAction(int row) {
#ifdef __ANDROID__
    if (sSaveTransferActive) {
        texturePackNotice(true, "Wait: a transfer is already running.");
    } else {
        sSaveTransferActive = true;
        if (row == 0) pc_save_android_open_backup();
        else pc_save_android_open_restore();
    }
#else
    texturePackNotice(false,
        "Desktop saves live in the game's 'save' folder (card0 / card1). "
        "Copy that folder to transfer.");
#endif
}

void texturePacksRowAction(int row, const std::vector<std::string>& packs) {
    if (row == kTexturePackInstallRow) {
        if (!sTexturePackPickerActive) {
#ifdef __ANDROID__
            sTexturePackPickerActive = true;
            pc_texpack_android_open_picker();
#else
            // Desktop: native picker, then the zip is extracted into
            // Load/Textures with the same path rule as Android.
            char chosen[4096];
            if (pc_file_dialog_open("Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP", chosen, sizeof(chosen))) {
                char msg[192];
                const int written = pc_texpack_install_zip(chosen, msg, sizeof(msg));
                texturePackNotice(written <= 0, msg);
            } else if (chosen[0] != '\0') {
                texturePackNotice(true, chosen); // no dialog available: says why
            }
#endif
        }
        return;
    }

    const int packIndex = row - kTexturePackInstallRow - 1;
    if (packIndex >= 0 && packIndex < static_cast<int>(packs.size())) {
        const std::string folder = packs[packIndex];
        const bool active = sConfig.texturePackEnabled && folder == sConfig.texturePack;
        if (sTexturePackPickerActive) {
            // Con el zip a medio extraer, activar y reiniciar mataría la
            // instalación: es justo lo que dejaba packs con 126 ficheros
            // de 3000 y el menú diciendo "Active".
            texturePackNotice(true, "Wait: the pack is still being installed.");
        } else {
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
}

void hdModelsRowAction(int row) {
    if (sTexturePackPickerActive) return;
#ifdef __ANDROID__
    sTexturePackPickerActive = true;
    sHdModelInstallActive = true;
    pc_modelpack_android_open_picker(row);
#else
    // Desktop: native picker, then convert the chosen rip in place.
    static const char* kTitles[6] = {
        "Choose the Pikmin 3 Olimar zip", "Choose the Pikmin 2 Louie zip", "Choose the Pikmin 3 Louie zip",
        "Choose the Pikmin 3 Pikmin zip", "Choose the Pikmin 3 Bulborb zip", "Choose the Pikmin 3 Dwarf Bulborb zip",
    };
    char chosen[4096];
    if (pc_file_dialog_open(kTitles[row], "Model zip", "*.zip *.ZIP", chosen, sizeof(chosen))) {
        char msg[192];
        const int written = pc_hd_models_convert_file(chosen, row, msg, sizeof(msg));
        texturePackNotice(written <= 0, msg);
        if (written > 0) sHdModelRestartPrompt = true;
    } else if (chosen[0] != '\0') {
        texturePackNotice(true, chosen); // no dialog available: says why
    }
#endif
        
}

// Texto del valor de una fila de la página principal (compartido con
// pc_settings_rows). Calcula la tabla completa y devuelve la fila pedida.
void mainRowValue(int row, char* out, size_t n) {
    if (row < 0 || row >= ROW_DISPLAY_COUNT) { if (n) out[0] = '\0'; return; }
    const char* modeNames[3] = { "Windowed", "Fullscreen", "Borderless" };
    const char* aspectNames[5] = { "Auto", "4:3", "16:10", "16:9", "21:9" };
    char aspectBuf[32];
    snprintf(aspectBuf, sizeof(aspectBuf), "%s", aspectNames[sPending.aspectRatioMode >= 0 && sPending.aspectRatioMode < 5 ? sPending.aspectRatioMode : 0]);

    const char* fpsModeNames[3] = { "30 FPS (stable)", "60 FPS (experimental)", "120 FPS (experimental)" };
    char fpsModeBuf[32];
    snprintf(fpsModeBuf, sizeof(fpsModeBuf), "%s", fpsModeNames[sPending.fpsMode >= 0 && sPending.fpsMode < 3 ? sPending.fpsMode : 0]);

    char valueBuf[ROW_DISPLAY_COUNT][128];
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
                 (sHeadless || language == pc_settings_startup_language()) ? "" : "  (on restart)");
    }
#endif

    snprintf(out, n, "%s", valueBuf[row]);
}

// Cambio de una fila de la página principal (compartido con pc_settings_rows).
void mainRowChange(int row, bool left, bool right, bool ok) {
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

    switch (row) {
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
            if (!sProbing && !sHeadless) pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
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
    default:
        break;
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

// Menús con estilo burbuja (prompts de inicio de partida): el texto P2D va
// más grande, como la letra del juego, y con la sombra fantasma de sus
// menús. Solo mientras dura el ámbito, para no cambiar el F1 ni los HUD.
bool sGlassText = false;
struct GlassTextScope {
    GlassTextScope() { sGlassText = true; }
    ~GlassTextScope() { sGlassText = false; }
};
constexpr int kGlassFontW = 18, kGlassFontH = 26;

int menuTextWidth(const char* text) {
    if (!pc_settings_p2d_active()) return sFont->stringWidth(text);
    return pc_settings_p2d_text_width(text, sGlassText ? kGlassFontW : 12);
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
void fillRoundRectGradAlways(DGXGraphics* gfx, int x, int y, int w, int h, int r,
                             Colour top, Colour bottom);

void fillRoundRectGrad(DGXGraphics* gfx, int x, int y, int w, int h, int r,
                       Colour top, Colour bottom) {
    if (w <= 0 || h <= 0) return;
    if (pc_settings_p2d_active()) {
        // Native selection is a game cursor plus yellow text.
        return;
    }
    fillRoundRectGradAlways(gfx, x, y, w, h, r, top, bottom);
}

// Igual, pero también con el marco P2D activo (velo de los menús burbuja).
void fillRoundRectGradAlways(DGXGraphics* gfx, int x, int y, int w, int h, int r,
                             Colour top, Colour bottom) {
    if (w <= 0 || h <= 0) return;
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
        if (sGlassText) pc_settings_p2d_text_styled(x, y, buf, main, main, kGlassFontW, kGlassFontH);
        else pc_settings_p2d_text(x, y, buf, main);
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
        // Burbuja de los menús del juego: el cristal (w08_160) con un velo
        // negro muy ligero dentro, para que el texto se lea sobre las
        // estrellas sin apagar el fondo fuera del recuadro. El velo va con
        // gfx y las placas P2D se pintan al final, así que queda debajo.
        const Colour veil(0, 0, 0, 120);
        fillRoundRectGradAlways(gfx, x + 8, y + 8, w - 16, h - 16, 26, veil, veil);
        pc_settings_p2d_plate(x, y, w, h, 3);
        // El texto más ancho que el recuadro se reduce para caber dentro.
        pc_settings_p2d_set_content(x + 22, x + w - 22);
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
        // Cápsula de cristal propia, separada encima de la burbuja cuando hay
        // sitio (como "Choose a Game Mode"); si no, montada sobre el borde.
        const int nativeTitleW = pc_settings_p2d_text_width(title, 22);
        const int capW = std::min(panelW, std::max(270, nativeTitleW + 120));
        const int capH = 68;
        const int capX = panelX + (panelW - capW) / 2;
        const int capY = panelY - capH - 14 >= 8 ? panelY - capH - 14 : panelY - 18;
        const Colour veil(0, 0, 0, 120);
        fillRoundRectGradAlways(gfx, capX + 8, capY + 8, capW - 16, capH - 16, 20, veil, veil);
        pc_settings_p2d_plate(capX, capY, capW, capH, 3);
        pc_settings_p2d_text_styled(capX + (capW - nativeTitleW) / 2, capY + 16, title, Colour(225, 255, 255, 255),
                                    Colour(170, 225, 235, 255), 22, 32);
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

// Opción de un selector con el estilo burbuja: la elegida en naranja entre
// dos bolitas de cristal, las demás apagadas. Sin placa: solo texto.
// Devuelve false si el marco P2D no está activo (el llamador dibuja lo suyo).
bool drawGlassOption(int boxX, int boxY, int boxW, int boxH, const char* label, bool sel, int fw = 14, int fh = 21) {
    if (!pc_settings_p2d_active()) return false;
    const int tw = pc_settings_p2d_text_width(label, fw);
    const int tx = boxX + (boxW - tw) / 2;
    const int ty = boxY + (boxH - fh) / 2 - 2;
    if (sel) {
        // Cursor del juego a cada lado: gira como el de los menús originales
        // y se desliza hasta la nueva opción. Si hace un rato que no se dibuja
        // (otro menú), aparece directamente en su sitio.
        const int orb = 24;
        const float targetL = float(tx - orb / 2 - 8), targetR = float(tx + tw + orb / 2 + 8);
        const float targetY = float(ty + fh / 2 + 1);
        static float sL = 0.0f, sR = 0.0f, sY = 0.0f;
        static Uint32 sLast = 0;
        const Uint32 now = SDL_GetTicks();
        const float dt = (now - sLast) / 1000.0f;
        if (sLast == 0 || now - sLast > 250) {
            sL = targetL; sR = targetR; sY = targetY;
        } else {
            const float k = std::min(1.0f, dt * 14.0f);
            sL += (targetL - sL) * k; sR += (targetR - sR) * k; sY += (targetY - sY) * k;
        }
        sLast = now;
        const float angle = std::fmod(now / 1000.0f * 10.0f, 6.2831853f);
        pc_settings_p2d_cursor(int(sL), int(sY), orb, angle);
        pc_settings_p2d_cursor(int(sR), int(sY), orb, angle);
    }
    if (sel) pc_settings_p2d_text_styled(tx, ty, label, Colour(255, 225, 70, 255), Colour(255, 135, 0, 255), fw, fh);
    else pc_settings_p2d_text_styled(tx, ty, label, Colour(150, 170, 195, 255), Colour(150, 170, 195, 255), fw, fh);
    return true;
}

// Línea de ayuda centrada en cx. Con el marco P2D, cada letra de botón suelta
// (A, B, X, Y, Z seguida de espacio, "/" o ":") se dibuja con el icono del
// botón del juego; el resto del texto no cambia.
void drawHelpLine(int cx, int y, const char* text, Colour c, int maxWidth = 570) {
    if (!pc_settings_p2d_active()) {
        drawTextOutline(cx - menuTextWidth(text) / 2, y, "%s", c, Colour(8, 12, 28, 255), text);
        return;
    }
    // Tan grande como quepa (14 → 10) en el ancho del recuadro.
    int fw = 14, fh = 20, icon = 26;
    auto isButton = [&](const char* p) {
        const bool startOk = p == text || p[-1] == ' ';
        const bool endOk   = p[1] == ' ' || p[1] == '/' || p[1] == ':';
        return startOk && endOk && std::strchr("ABXYZ", *p) != nullptr;
    };
    // Medir: trozos de texto + iconos.
    char chunk[256];
    int n = 0;
    auto measure = [&]() {
        int total = 0;
        n = 0;
        for (const char* p = text;; ++p) {
            if (*p == '\0' || isButton(p)) {
                chunk[n] = '\0';
                total += pc_settings_p2d_text_width(chunk, fw);
                n = 0;
                if (*p == '\0') break;
                total += icon;
                continue;
            }
            if (n < int(sizeof(chunk)) - 1) chunk[n++] = *p;
        }
        return total;
    };
    int total = measure();
    while (total > maxWidth && fw > 10) {
        --fw;
        fh = fw * 20 / 14;
        icon = fw * 26 / 14;
        total = measure();
    }
    int x = cx - total / 2;
    n = 0;
    for (const char* p = text;; ++p) {
        if (*p == '\0' || isButton(p)) {
            chunk[n] = '\0';
            if (n) {
                pc_settings_p2d_text_styled(x, y, chunk, c, c, fw, fh, sGlassText);
                x += pc_settings_p2d_text_width(chunk, fw);
            }
            n = 0;
            if (*p == '\0') break;
            if (!pc_settings_p2d_button(x, y + (fh - icon) / 2 - 1, icon, *p)) {
                const char letter[2] = { *p, '\0' };
                pc_settings_p2d_text(x + (icon - pc_settings_p2d_text_width(letter, fw)) / 2, y, letter, c, fw, fh);
            }
            x += icon;
            continue;
        }
        if (n < int(sizeof(chunk)) - 1) chunk[n++] = *p;
    }
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
                    const char* label, const char* value, bool selected, bool enabled = true) {
    if (selected) {
        fillRoundRectGrad(gfx, x, y - 3, w, 22, 8,
                          Colour(58, 51, 31, 235), Colour(7, 7, 8, 245));
        drawTextOutline(x + 10, y, ">", Colour(255, 229, 120, 255),
                        Colour(48, 18, 0, 255));
    }
    Colour main = selected ? Colour(255, 190, 28, 255) : Colour(185, 237, 255, 255);
    Colour shadow = selected ? Colour(62, 25, 0, 255) : Colour(0, 9, 15, 255);
    if (!enabled) main = selected ? Colour(170, 140, 90, 255) : Colour(95, 115, 135, 255);
    const int split = x + w / 2;
    drawTextOutline(split - 14 - menuTextWidth(label), y, "%s",
                    main, shadow, label);
    drawTextOutline(split + 14, y, "%s", main, shadow, value);
}

// Aviso temporal de la última acción (instalación de packs, transferencia de
// guardado) centrado en (centerX, y), con el color según el resultado.
void drawTimedNotice(int centerX, int y) {
    std::lock_guard<std::mutex> lock(sTexturePackNoticeMutex);
    const bool fresh = SDL_GetTicks() - sTexturePackNoticeMs < kTexturePackNoticeTimeoutMs;
    if (!fresh || !sTexturePackNotice[0]) return;
    char msg[sizeof(sTexturePackNotice)];
    snprintf(msg, sizeof(msg), "%s", sTexturePackNotice);
    const Colour colour = sTexturePackNoticeError ? Colour(255, 140, 140, 255)
                                                  : Colour(150, 235, 170, 255);
    drawTextOutline(centerX - menuTextWidth(msg) / 2, y, "%s",
                    colour, Colour(10, 16, 36, 255), msg);
}


// ---------------------------------------------------------------------------
// Página de pestañas de F1
// ---------------------------------------------------------------------------

// Mismos tonos que el menú de cristal del título (pc_glass_menu).
const Colour kF1Text(205, 240, 255, 255);
const Colour kF1Dim(150, 175, 200, 255);
const Colour kF1Help(160, 185, 215, 255);
const Colour kF1Off(95, 115, 135, 255);
const Colour kF1SelDark(255, 150, 0, 255);
const Colour kF1SelOff(170, 120, 60, 255);
const Colour kF1Section(255, 207, 75, 255);

// Placa y texto con la capa nativa (P2D) o, sin ella, con los rellenos y la
// fuente de consola. Estilo 1 = cristal, 2 = selección.
void f1Plate(DGXGraphics* gfx, int x, int y, int w, int h, int style) {
    if (pc_settings_p2d_active()) { pc_settings_p2d_plate(x, y, w, h, style); return; }
    if (style == 2)
        fillRoundRectGrad(gfx, x, y, w, h, 8, Colour(58, 51, 31, 235), Colour(7, 7, 8, 245));
    else
        fillRoundRectGrad(gfx, x, y, w, h, 10, Colour(24, 30, 52, 235), Colour(8, 11, 24, 245));
}

int f1TextW(const char* t, int fw) {
    return pc_settings_p2d_active() ? pc_settings_p2d_text_width(t, fw) : menuTextWidth(t);
}

void f1Text(int x, int y, const char* t, Colour c, int fw, int fh) {
    if (pc_settings_p2d_active()) pc_settings_p2d_text(x, y, t, c, fw, fh);
    else drawTextOutline(x, y, "%s", c, Colour(0, 9, 15, 255), t);
}

// Texto partido por palabras en el ancho w; para en maxY. Devuelve la y final.
int f1TextWrapped(int x, int y, int w, const char* text, Colour c, int fw, int fh, int maxY) {
    char line[256];
    size_t len = 0;
    const char* p = text ? text : "";
    while (*p && y + fh <= maxY) {
        // '\n' corta la línea; una línea vacía deja medio renglón de hueco.
        if (*p == '\n') {
            if (len) {
                f1Text(x, y, line, c, fw, fh);
                y += fh + 2;
                len = 0;
            } else {
                y += (fh + 2) / 2;
            }
            p++;
            continue;
        }
        const char* end = p;
        while (*end && *end != ' ' && *end != '\n') end++;
        char trial[256];
        snprintf(trial, sizeof(trial), "%.*s%s%.*s", (int)len, line, len ? " " : "", (int)(end - p), p);
        if (len && f1TextW(trial, fw) > w) {
            f1Text(x, y, line, c, fw, fh);
            y += fh + 2;
            len = 0;
            continue; // la misma palabra abre la línea siguiente
        }
        snprintf(line, sizeof(line), "%s", trial);
        len = strlen(line);
        p = *end == ' ' ? end + 1 : end;
    }
    if (len && y + fh <= maxY) {
        f1Text(x, y, line, c, fw, fh);
        y += fh + 2;
    }
    return y;
}

int achievementAtRow(int row); // logro de esa fila de la pestaña, o -1

// Textura del icono de un logro (del juego), cargada una vez.
Texture* achievementIcon(int id) {
    static std::map<std::string, Texture*> sIcons;
    const char* path = pc_achievement_icon(id);
    auto it = sIcons.find(path);
    return it != sIcons.end() ? it->second : (sIcons[path] = zen::loadTexExp(path, true, true));
}

void drawAchievementIcon(DGXGraphics* gfx, Texture* icon, int x, int y, int maxW, int maxH, const Colour& tint) {
    if (!icon || icon->mWidth <= 0 || icon->mHeight <= 0) return;
    int ih = maxH, iw = icon->mWidth * ih / icon->mHeight;
    if (iw > maxW) { iw = maxW; ih = icon->mHeight * iw / icon->mWidth; }
    const int ix = x + (maxW - iw) / 2, iy = y + (maxH - ih) / 2;
    if (pc_settings_p2d_active()) {
        pc_settings_p2d_image(ix, iy, iw, ih, icon, 1.0f, 1.0f, tint, 0.0f);
    } else {
        gfx->setColour(tint, true);
        gfx->setAuxColour(tint);
        gfx->useTexture(icon, GX_TEXMAP0);
        gfx->drawRectangle(RectArea(ix, iy, ix + iw, iy + ih), RectArea(0, 0, icon->mWidth, icon->mHeight), nullptr);
        gfx->useTexture(nullptr, GX_TEXMAP0);
    }
}

void drawF1Page(DGXGraphics* gfx) {
    // Pestañas de grupo y la X de cerrar.
    for (int g = 0; g < PC_SET_GROUP_COUNT; g++) {
        const int x = kF1TabX + g * kF1TabW;
        const char* name = pc_settings_group_name(g);
        const bool on = g == sOpenGroup;
        if (on) f1Plate(gfx, x + 2, kF1TabY - 1, kF1TabW - 4, kF1TabH + 2, 2);
        int tfw = 10; // nombres largos ("Achievements") se encogen para caber
        while (tfw > 7 && f1TextW(name, tfw) > kF1TabW - 6) tfw--;
        f1Text(x + (kF1TabW - f1TextW(name, tfw)) / 2, kF1TabY + 3 + (10 - tfw), name, on ? kF1SelDark : kF1Dim, tfw, 15 * tfw / 10);
    }
    const int closeX = kF1PanelX + kF1PanelW - 18 - kF1CloseW;
    f1Text(closeX + (kF1CloseW - f1TextW("X", 11)) / 2, kF1TabY + 3, "X", kF1Dim, 11, 16);

    f1Plate(gfx, kF1LeftX, kF1ColY, kF1LeftW, kF1ColH, 1);
    f1Plate(gfx, kF1RightX, kF1ColY, kF1RightW, kF1ColH, 1);

    // Columna izquierda: cabeceras de sección y filas de la pestaña.
    int items[96];
    const int n = f1Items(sOpenGroup, items, 96);
    f1ClampScroll();
    const bool scroll = n > kF1Visible;
    const int rowX = kF1LeftX + 8;
    const int rowW = kF1LeftW - (scroll ? 30 : 16);
    for (int v = 0; v < kF1Visible && sF1Scroll + v < n; v++) {
        const int item = items[sF1Scroll + v];
        const int y = kF1ColY + 8 + v * kF1ItemH;
        if (item < 0) {
            f1Text(rowX + 2, y + 6, rowSection(sOpenGroup, -1 - item), kF1Section, 9, 13);
            continue;
        }
        const bool sel = item == sGroupSel;
        const bool on = pc_settings_row_enabled(sOpenGroup, item);
        if (sel) f1Plate(gfx, rowX - 2, y, rowW + 4, kF1ItemH - 2, sF1OptFocus ? 1 : 2);
        char value[128];
        pc_settings_row_value(sOpenGroup, item, value, sizeof(value));
        const Colour label = !on ? (sel ? kF1SelOff : kF1Off) : (sel ? kF1SelDark : kF1Text);
        const Colour val = !on ? (sel ? kF1SelOff : kF1Off) : (sel ? kF1SelDark : kF1Dim);
        // Etiquetas largas (títulos de logros) se recortan con "..." y dejan
        // sitio al valor.
        char name[128];
        snprintf(name, sizeof(name), "%s", pc_settings_row_label(sOpenGroup, item));
        const int nameRoom = rowW - 12 - (value[0] ? f1TextW(value, 10) + 12 : 0);
        while (f1TextW(name, 10) > nameRoom && strlen(name) > 4) {
            name[strlen(name) - 4] = '\0';
            strcat(name, "...");
        }
        f1Text(rowX + 6, y + 3, name, label, 10, 15);
        // Valores largos (p. ej. la resolución) se encogen para no pisar la etiqueta.
        const int room = rowW - 12 - f1TextW(name, 10) - 12;
        int fw = 10;
        while (fw > 7 && f1TextW(value, fw) > room) fw--;
        f1Text(rowX + rowW - 6 - f1TextW(value, fw), y + 3 + (10 - fw), value, val, fw, 15 * fw / 10);
    }
    if (scroll) {
        // Carril + pulgar proporcional, como en el menú de cristal.
        const int tx = kF1LeftX + kF1LeftW - 16, ty = kF1ColY + 8, th = kF1Visible * kF1ItemH;
        f1Plate(gfx, tx, ty, 8, th, 1);
        const int thumbH = th * kF1Visible / n < 16 ? 16 : th * kF1Visible / n;
        f1Plate(gfx, tx, ty + (th - thumbH) * sF1Scroll / (n - kF1Visible), 8, thumbH, 2);
    }

    // Columna derecha: opciones de la fila y su explicación.
    const int rx = kF1RightX + 8, rw = kF1RightW - 16;
    int y = kF1ColY + 8;
    int at = -1;
    const std::vector<std::string>& opts = f1Options(sOpenGroup, sGroupSel, &at);
    if (at >= 0) {
        // Sin cursor aquí se marca la opción actual; con cursor, la placa lo
        // sigue y la actual queda en dorado.
        const int count = (int)opts.size();
        if (sF1OptFocus && sF1OptSel >= count) sF1OptSel = count - 1;
        const int mark = sF1OptFocus ? sF1OptSel : at;
        int first = 0, shown = 0;
        f1OptWindow(count, mark, &first, &shown);
        for (int k = 0; k < shown; k++) {
            const bool sel = first + k == mark;
            const bool cur = first + k == at;
            const char* t = opts[first + k].c_str();
            if (sel) f1Plate(gfx, rx - 2, y, rw + 4, kF1OptH - 2, 2);
            f1Text(rx + (rw - f1TextW(t, 10)) / 2, y + 3, t, sel ? kF1SelDark : cur ? kF1Section : kF1Text, 10, 15);
            y += kF1OptH;
        }
        if (count > shown) {
            char pos[32];
            snprintf(pos, sizeof(pos), "%d / %d", mark + 1, count);
            f1Text(rx + (rw - f1TextW(pos, 9)) / 2, y + 2, pos, kF1Dim, 9, 13);
            y += 16;
        }
    } else {
        // Acción, selector o fila desactivada: solo su valor, que hace de botón.
        char value[128];
        pc_settings_row_value(sOpenGroup, sGroupSel, value, sizeof(value));
        const bool on = pc_settings_row_enabled(sOpenGroup, sGroupSel);
        f1Plate(gfx, rx - 2, y, rw + 4, kF1OptH - 2, on ? 2 : 1);
        f1Text(rx + (rw - f1TextW(value, 10)) / 2, y + 3, value, on ? kF1SelDark : kF1Off, 10, 15);
        y += kF1OptH;
        // Logros: su icono, a color si está conseguido y oscurecido si no.
        if (sOpenGroup == PC_SET_GROUP_ACHIEVEMENTS) {
            const int achId = achievementAtRow(sGroupSel);
            if (achId >= 0) {
                drawAchievementIcon(gfx, achievementIcon(achId), rx, y + 8, rw, 56,
                                    on ? Colour(255, 255, 255, 255) : Colour(70, 70, 80, 255));
                y += 64;
            }
        }
    }
    f1TextWrapped(rx + 2, y + 12, rw - 4, pc_settings_row_help(sOpenGroup, sGroupSel), kF1Help, 10, 15,
                  kF1ColY + kF1ColH - 6);

    // Pie: aviso de la última acción, error de vídeo o controles.
    const int footY = kF1ColY + kF1ColH + 9;
    const int footCx = kF1PanelX + kF1PanelW / 2;
    char foot[256];
    bool isError = false;
    Colour footColour = kF1Help;
    if (pc_settings_notice(foot, sizeof(foot), &isError)) {
        footColour = isError ? Colour(255, 150, 140, 255) : Colour(160, 240, 180, 255);
    } else if (pc_window_get_last_error()[0]) {
        snprintf(foot, sizeof(foot), "Video error: %s", pc_window_get_last_error());
        footColour = Colour(255, 150, 140, 255);
    } else {
        snprintf(foot, sizeof(foot), "%s", sF1OptFocus
                     ? "Up/Down: choose   A: apply   Left/B: back"
                     : pc_settings_row_is_action(sOpenGroup, sGroupSel)
                     ? "L/R: tab   Up/Down: move   A: select   B/Esc: save and close"
                     : "L/R: tab   Right: options   Up/Down: move   B/Esc: save and close");
    }
    f1Text(footCx - f1TextW(foot, 10) / 2, footY, foot, footColour, 10, 14);
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
    const bool promptWasOpen = pc_erased_notice_active() || pc_speedrun_intro_active() || pc_newgame_prompt_active() || pc_playercount_prompt_active() || pc_devassign_prompt_active()
                            || pc_captain_prompt_active() || pc_glass_menu_active();
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
    if (sHdModelInstallActive.exchange(false) && ok) sHdModelRestartPrompt = true;
    texturePackNotice(!ok, message ? message : (ok ? "Pack instalado." : "No se pudo instalar el pack."));
}

// Llega desde el hilo Java que exportó/importó la partida (submenú F1 → Android,
// SaveTransfer.java). Cierra la transferencia en curso y deja el mensaje pintado
// unos segundos por drawTimedNotice.
void pc_save_transfer_finished(bool ok, const char* message) {
    sSaveTransferActive = false;
    texturePackNotice(!ok, message ? message : (ok ? "Save transfer complete." : "Save transfer failed."));
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
bool sSpeedrunIntroOpen   = false;
int  sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_PENDING;
int  sSrPage = 0;      // SrPage
int  sSrSel  = 0;      // opción marcada en el menú del modo
bool sErasedNoticeQueued = false;
bool sErasedNoticeOpen   = false;
bool sNewGamePromptOpen = false;
int  sNewGamePromptStep = 0;     // 0 = normal/permadeath, 1 = difficulty
int  sNewGamePromptChoice = 0;   // current step: 0 = left option, 1 = right
int  sNewGamePromptRules = 0;    // 0 = normal file, 1 = permadeath
int  sNewGamePromptResult = PC_NEWGAME_PENDING;
bool sNewGamePromptHard = false;
}

// Menú del modo Speedrun: Start Run / Best Times / How It Works. La
// explicación se abre sola la primera vez (speedrunIntroHidden = ya vista).
enum SrPage { SR_Menu, SR_How, SR_Times, SR_ConfirmReset };

bool pc_speedrun_intro_open_if_needed(void) {
    sSpeedrunIntroOpen   = true;
    sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_PENDING;
    sSrSel  = 0;
    sSrPage = sConfig.speedrunIntroHidden ? SR_Menu : SR_How;
    if (!sConfig.speedrunIntroHidden) {
        sConfig.speedrunIntroHidden  = 1;
        sPending.speedrunIntroHidden = 1;
        saveConfig();
    }
    pc_menu_edge_reset();
    return true;
}

bool pc_speedrun_intro_active(void) { return sSpeedrunIntroOpen; }

int pc_speedrun_intro_result(void) { return sSpeedrunIntroResult; }

namespace {
constexpr int kSrMenuItems = 3;
const char* const kSrMenuLabels[kSrMenuItems] = { "Start Run", "Best Times", "How It Works" };

void pcSpeedrunIntroInput() {
    if (!sSpeedrunIntroOpen) return;
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE)
               || (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    bool back   = keyWentDown(SDL_SCANCODE_ESCAPE) || (sTouchFrameButtons & PAD_BUTTON_B) != 0;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
    bool xPress = keyWentDown(SDL_SCANCODE_X) || (sTouchFrameButtons & PAD_BUTTON_X) != 0;
    if (sTouchTapPending) {
        sTouchTapPending = false;
        // En el menú, un toque sobre una opción la elige; en las páginas, vuelve.
        if (sSrPage == SR_Menu) {
            const float y = sTouchTapY * 480.0f;
            const int row = int((y - (240.0f - 70.0f)) / 44.0f);
            if (row >= 0 && row < kSrMenuItems) { sSrSel = row; accept = true; }
        } else {
            accept = true;
        }
    }
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (promptPadA(ctl)) accept = true;
        if (promptPadB(ctl)) back = true;
        if (padNavUp(ctl)) up = true;
        if (padNavDown(ctl)) down = true;
        static bool sPadXWas = false;
        const bool padX = pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_X));
        if (padX && !sPadXWas) xPress = true;
        sPadXWas = padX;
    }

    switch (sSrPage) {
    case SR_Menu:
        if (up)   sSrSel = (sSrSel + kSrMenuItems - 1) % kSrMenuItems;
        if (down) sSrSel = (sSrSel + 1) % kSrMenuItems;
        if (accept) {
            if (sSrSel == 0) {
                sSpeedrunIntroOpen   = false;
                sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_CONTINUE;
            } else {
                sSrPage = sSrSel == 1 ? SR_Times : SR_How;
            }
        } else if (back) {
            sSpeedrunIntroOpen   = false;
            sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_BACK;
        }
        break;
    case SR_How:
        if (accept || back) sSrPage = SR_Menu;
        break;
    case SR_Times:
        if (xPress && pc_speedrun_has_pb()) sSrPage = SR_ConfirmReset;
        else if (accept || back) sSrPage = SR_Menu;
        break;
    case SR_ConfirmReset:
        if (accept) { pc_speedrun_reset_records(); sSrPage = SR_Times; }
        else if (back) sSrPage = SR_Times;
        break;
    }
}

// Texto P2D a tamaño de lista (más pequeño que el general de la burbuja).
void srText(int x, int y, const char* t, Colour c, int fw = 12, int fh = 18) {
    pc_settings_p2d_text_styled(x, y, t, c, c, fw, fh);
}
void srTextRight(int right, int y, const char* t, Colour c, int fw = 12, int fh = 18) {
    srText(right - pc_settings_p2d_text_width(t, fw), y, t, c, fw, fh);
}

void drawSrHow(DGXGraphics* gfx, int screenW, int screenH) {
    const int panelW = 600, panelH = 262;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2 + 10;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "How It Works");
    const char* lines[6] = { "Pikmin exactly as it shipped on GameCube.",
                             "No gameplay changes and no control upgrades:",
                             "every mod, cheat and port option that alters play is off.",
                             "Start Run begins a new game right away, with no file",
                             "select. The timer starts then and stops when the",
                             "Secret Safe is collected. One split per day." };
    for (int i = 0; i < 6; i++) {
        const Colour c = i < 3 ? Colour(214, 224, 245, 255) : Colour(170, 190, 215, 255);
        drawTextOutline(panelX + panelW / 2 - menuTextWidth(lines[i]) / 2, panelY + 34 + i * 30 + (i >= 3 ? 8 : 0),
                        "%s", c, Colour(8, 12, 28, 255), lines[i]);
    }
    drawHelpLine(panelX + panelW / 2, panelY + 226, "A / Enter: OK    B / Esc: back", Colour(150, 165, 195, 255));
}

void drawSrTimes(DGXGraphics* gfx, int screenW, int screenH, bool confirm) {
    const int panelW = 600, panelH = 372;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2 + 30;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Best Times");
    const Colour kTitle(255, 205, 60, 255), kBody(214, 224, 245, 255), kDim(150, 165, 195, 255);
    const int lx = panelX + 36, rx = panelX + panelW - 36;
    char buf[64], t[32];

    if (!pc_speedrun_has_pb()) {
        const char* none = "No finished runs yet.";
        srText(panelX + panelW / 2 - pc_settings_p2d_text_width(none, 15) / 2, panelY + 150, none, kBody, 15, 22);
    } else {
        // Mejor run y Sum of Best.
        pc_speedrun_format_time(pc_speedrun_pb_ms(), t, sizeof(t));
        srText(lx, panelY + 26, "Personal Best", kTitle, 14, 21);
        srTextRight(rx, panelY + 26, t, kTitle, 14, 21);
        snprintf(buf, sizeof(buf), "%d days   %s", pc_speedrun_pb_days(), pc_speedrun_pb_date());
        srText(lx, panelY + 50, buf, kDim, 10, 15);
        pc_speedrun_format_time(pc_speedrun_sum_of_best(), t, sizeof(t));
        snprintf(buf, sizeof(buf), "Sum of Best  %s", t);
        srTextRight(rx, panelY + 50, buf, kDim, 10, 15);

        // Splits de la mejor run (dos columnas si no caben en una).
        const int n = pc_speedrun_pb_split_count();
        const int perCol = 9, colW = (rx - lx) / 2;
        for (int i = 0; i < n && i < perCol * 2; i++) {
            const int cx = lx + (i / perCol) * colW, y = panelY + 76 + (i % perCol) * 19;
            srText(cx, y, pc_speedrun_pb_split_name(i), kBody, 9, 14);
            pc_speedrun_format_time(pc_speedrun_pb_split_ms(i), t, sizeof(t));
            srTextRight(cx + colW - 14, y, t, kBody, 9, 14);
        }

        // Últimas runs.
        srText(lx, panelY + 252, "Recent Runs", kTitle, 11, 16);
        for (int i = 0; i < pc_speedrun_recent_count(); i++) {
            u64 ms = 0; int days = 0; const char* date = "";
            pc_speedrun_recent(i, &ms, &days, &date);
            pc_speedrun_format_time(ms, t, sizeof(t));
            snprintf(buf, sizeof(buf), "%s   %dd   %s", t, days, date);
            srText(lx + (i % 2) * colW, panelY + 274 + (i / 2) * 18, buf, kBody, 9, 14);
        }
    }

    if (confirm) {
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 36, "Erase all records?    A: erase    B: keep",
                     Colour(255, 160, 120, 255));
    } else {
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 36,
                     pc_speedrun_has_pb() ? "B / Esc: back    X: erase records" : "B / Esc: back",
                     Colour(150, 165, 195, 255));
    }
}
}

void pc_speedrun_intro_draw(void) {
    if (!sSpeedrunIntroOpen || !gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    if (sSrPage == SR_How) { drawSrHow(gfx, screenW, screenH); return; }
    if (sSrPage == SR_Times || sSrPage == SR_ConfirmReset) {
        drawSrTimes(gfx, screenW, screenH, sSrPage == SR_ConfirmReset);
        return;
    }

    const int panelW = 420, panelH = 200;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Speedrun");
    for (int i = 0; i < kSrMenuItems; i++) {
        drawGlassOption(panelX + 40, panelY + 22 + i * 44, panelW - 80, 40, kSrMenuLabels[i], i == sSrSel, 18, 26);
    }
    drawHelpLine(panelX + panelW / 2, panelY + 160, "A / Enter: select    B / Esc: back", Colour(150, 165, 195, 255));
}

void pc_erased_notice_queue(void) { sErasedNoticeQueued = true; }

bool pc_erased_notice_open_if_queued(void) {
    if (!sErasedNoticeQueued) return false;
    sErasedNoticeQueued = false;
    sErasedNoticeOpen   = true;
    pc_menu_edge_reset();
    return true;
}

bool pc_erased_notice_active(void) { return sErasedNoticeOpen; }

namespace {
void pcErasedNoticeInput() {
    if (!sErasedNoticeOpen) return;
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE)
               || (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    if (sTouchTapPending) {
        sTouchTapPending = false;
        accept = true; // un toque en cualquier sitio cierra el aviso
    }
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl && promptPadA(ctl)) accept = true;
    if (accept) sErasedNoticeOpen = false;
}
}

void pc_erased_notice_draw(void) {
    if (!sErasedNoticeOpen || !gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const int panelW = 520, panelH = 190;
    const int panelX = screenW / 2 - panelW / 2;
    const int panelY = screenH / 2 - panelH / 2 + 20;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Expedition Lost");

    const char* lines[3] = { "Olimar fell during a Permadeath run.",
                             "That expedition's log is gone from the", "memory card for good." };
    for (int i = 0; i < 3; i++) {
        drawTextOutline(panelX + panelW / 2 - menuTextWidth(lines[i]) / 2, panelY + 44 + i * 30, "%s",
                        Colour(214, 224, 245, 255), Colour(8, 12, 28, 255), lines[i]);
    }
    const char* help = "A / Enter: continue";
    drawHelpLine(panelX + panelW / 2, panelY + 148, help, Colour(150, 165, 195, 255));
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
            // El reset olvida que A/B siguen pulsados y el frame siguiente
            // los tomaría como pulsación nueva (autoaceptaba la dificultad).
            if (ctl) { promptPadA(ctl); promptPadB(ctl); }
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
            if (ctl) { promptPadA(ctl); promptPadB(ctl); }
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
    GlassTextScope glassText;

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));


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
        if (drawGlassOption(boxX, optY, boxW, 44, options[i], sel, 20, 29)) continue;
        gfx->setColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220), true);
        gfx->setAuxColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220));
        gfx->fillRectangle(RectArea(boxX, optY, boxX + boxW, optY + 40));
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
    drawHelpLine(panelX + panelW / 2, panelY + 212, help, Colour(150, 165, 195, 255));
}


// ─── Selector 1P / 2P ───
//
// Se abre al entrar en la selección de slot desde "Empezar". Con 2 jugadores
// y sin segundo mando el prompt pasa a un estado de espera hasta que
// pc_window ve el SDL_CONTROLLERDEVICEADDED y lo abre en la ranura 1.

namespace {
bool sPlayerCountOpen    = false;
int  sPlayerCountChoice  = 0;   // 0 = 1 jugador, 1 = 2 jugadores
int  sPlayerCountResult  = PC_PLAYERCOUNT_PENDING;

bool playerCountSpanish() { return pc_settings_get_language() == 3; } // OS_LANG_SPANISH
}

void pc_playercount_prompt_open(void) {
    sPlayerCountOpen    = true;
    sPlayerCountChoice  = sConfig.coopPlayers == 2 ? 1 : 0;
    sPlayerCountResult  = PC_PLAYERCOUNT_PENDING;
    pc_menu_edge_reset();
}

bool pc_playercount_prompt_active(void) { return sPlayerCountOpen; }

int pc_playercount_prompt_result(void) { return sPlayerCountResult; }

namespace {
void pcPlayerCountPromptInput() {
    if (!sPlayerCountOpen) return;

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
                sPlayerCountChoice = i;
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

    if (left || right) sPlayerCountChoice = sPlayerCountChoice ? 0 : 1;

    if (accept) {
        sConfig.coopPlayers = sPlayerCountChoice ? 2 : 1;
        saveConfig();
        sPlayerCountResult = sPlayerCountChoice ? PC_PLAYERCOUNT_TWO : PC_PLAYERCOUNT_ONE;
        sPlayerCountOpen   = false;
    } else if (cancel) {
        sPlayerCountResult = PC_PLAYERCOUNT_CANCELLED;
        sPlayerCountOpen   = false;
    }
}
} // namespace

void pc_playercount_prompt_draw(void) {
    if (!sPlayerCountOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const bool es = playerCountSpanish();

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
    drawPikminHeader(gfx, panelX, panelY, panelW, es ? "Jugadores" : "Players");

    const char* line1 = es ? "¿Cuántos van a jugar?" : "How many are playing?";
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(line1) / 2, panelY + 62,
                    "%s", Colour(214, 224, 245, 255), Colour(8, 12, 28, 255), line1);

    const char* options[2] = { es ? "1 jugador" : "1 player", es ? "2 jugadores" : "2 players" };
    const int optY = panelY + 108;
    for (int i = 0; i < 2; i++) {
        const bool sel = (i == sPlayerCountChoice);
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

    const char* detail = sPlayerCountChoice
        ? (es ? "Cooperativo a pantalla partida. Luego se asignan los mandos."
              : "Split-screen co-op. Controllers are assigned next.")
        : (es ? "La aventura original, un solo Olimar."
              : "The original adventure, a single Olimar.");
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(detail) / 2, panelY + 172,
                    "%s", Colour(190, 200, 220, 255), Colour(8, 12, 28, 255), detail);

    const char* help = es ? "Izq/Der: elegir    A / Intro: seguir    B / Esc: volver"
                          : "Left/Right: choose    A / Enter: next    B / Esc: back";
    drawHelpLine(panelX + panelW / 2, panelY + 212, help, Colour(150, 165, 195, 255));
}


// ─── Asignación de mandos por jugador ───
//
// Tras elegir 2 jugadores. Primero "Player 1, press a button", luego P2, y
// al final una pantalla de confirmación con lo asignado. Un dispositivo no
// puede ir a los dos: si P2 pulsa el de P1 se ignora. Tras cada asignación se
// descartan las pulsaciones durante un rato para que un mando que el sistema
// expone dos veces no se cuele como P2.

namespace {
// Cada jugador reclama su mando y, acto seguido, elige capitán (Olimar o
// Louie) con ese mismo mando; al final la confirmación.
enum { DEVASSIGN_WaitP1 = 0, DEVASSIGN_PickP1 = 1, DEVASSIGN_WaitP2 = 2, DEVASSIGN_PickP2 = 3, DEVASSIGN_Confirm = 4 };
bool   sDevAssignOpen   = false;
int    sDevAssignStep   = DEVASSIGN_WaitP1;
int    sDevAssignCaptain[2] = { PC_CAPTAIN_OLIMAR, PC_CAPTAIN_LOUIE }; // cursor/elección por jugador
int    sDevAssignChoice = 0; // confirm: 0 = Start, 1 = Reassign
int    sDevAssignResult = PC_DEVASSIGN_PENDING;
Uint32 sDevAssignIgnoreUntil = 0;
int    sDevAssignKind[2] = { PC_INPUT_DEV_NONE, PC_INPUT_DEV_NONE };
int    sDevAssignId[2]   = { -1, -1 };

int devAssignPlayer() { return sDevAssignStep <= DEVASSIGN_PickP1 ? 0 : 1; }

void devAssignRestart() {
    sDevAssignStep = DEVASSIGN_WaitP1;
    for (int p = 0; p < 2; p++) { sDevAssignKind[p] = PC_INPUT_DEV_NONE; sDevAssignId[p] = -1; }
    pc_window_input_reset_assignment();
    pc_window_discard_button_presses();
    sDevAssignIgnoreUntil = SDL_GetTicks() + 300;
    pc_menu_edge_reset();
}

// Tarjetas del selector de capitán, en orden (índice = PcCaptain).
const char* const kCaptainNames[PC_CAPTAIN_COUNT] = { "Olimar", "Louie", "Red", "Yellow", "Blue" };
// Sprites al doble (Olimar/Louie de Pikmin 2-e; los Pikmin, de Pikmin Puzzle
// Cards de GBA, de pie con la hoja).
const char* const kCaptainArt[PC_CAPTAIN_COUNT] = { "coop_olimar", "coop_louie", "coop_piki_red", "coop_piki_yellow",
                                                    "coop_piki_blue" };
// Si falta el PNG de un Pikmin, sale de la textura del juego piki3 (116x64):
// rojo, amarillo y azul tumbados con su hoja. Columnas de cada uno.
const int kCaptainPikiCol[PC_CAPTAIN_COUNT][2] = { { 0, 0 }, { 0, 0 }, { 0, 38 }, { 34, 78 }, { 76, 116 } };
constexpr int kCaptainBoxW = 104, kCaptainBoxH = 120, kCaptainBoxGap = 9;
constexpr int kCaptainBoxLeft = (620 - (PC_CAPTAIN_COUNT * kCaptainBoxW + (PC_CAPTAIN_COUNT - 1) * kCaptainBoxGap)) / 2;
const char* devAssignCaptainName(int captain) {
    return (captain >= 0 && captain < PC_CAPTAIN_COUNT) ? kCaptainNames[captain] : kCaptainNames[0];
}

bool devAssignLouieInstalled() {
    std::error_code ec;
    return std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE), ec)
        || std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE_HD), ec);
}
Uint32 sDevAssignLouieNoticeUntil = 0;

const char* devAssignName(int player) {
    if (sDevAssignKind[player] == PC_INPUT_DEV_KEYBOARD) return "Keyboard";
    if (sDevAssignKind[player] == PC_INPUT_DEV_GAMEPAD) return pc_window_gamepad_name(sDevAssignId[player]);
    return "-";
}

// Selector de capitán (Olimar, Louie o un Pikmin) compartido por el prompt de mandos del
// coop y el de 1 jugador. Devuelve 0 nada, 1 aceptado, 2 atrás.
int captainPickInput(int player, bool keyboardOk, SDL_GameController* ctl, int* captain, bool ignoreWindow) {
    bool left = false, right = false, accept = false, back = false;
    if (keyboardOk) {
        left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
        right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        back   = keyWentDown(SDL_SCANCODE_ESCAPE);
    }
    if (player == 0) {
        left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
        right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
        accept |= (sTouchFrameButtons & PAD_BUTTON_A) != 0;
        back |= (sTouchFrameButtons & PAD_BUTTON_B) != 0;
        if (sTouchTapPending) {
            // Toque directo sobre una de las cajas (misma geometría que el dibujo).
            sTouchTapPending = false;
            int dw = 0, dh = 0;
            pc_gfx_get_drawable_size(&dw, &dh);
            const float aspect = dh > 0 ? float(dw) / float(dh) : 4.0f / 3.0f;
            const float screenW = aspect * 480.0f;
            const float x = sTouchTapX * screenW, y = sTouchTapY * 480.0f;
            const float panelX = screenW * 0.5f - 310.0f, boxY = 110.0f + 84.0f;
            for (int i = 0; i < PC_CAPTAIN_COUNT; ++i) {
                const float boxX = panelX + kCaptainBoxLeft + i * (kCaptainBoxW + kCaptainBoxGap);
                if (x >= boxX && x <= boxX + kCaptainBoxW && y >= boxY && y <= boxY + kCaptainBoxH) {
                    *captain = i;
                    accept = true;
                }
            }
        }
    }
    if (ctl) {
        const bool hLeft  = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || menuStickHorizontal(ctl, -1);
        const bool hRight = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || menuStickHorizontal(ctl, 1);
        const bool hA     = pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_A));
        const bool hB     = pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_B));
        if (padEdge(hLeft, 2))  left   = true;
        if (padEdge(hRight, 3)) right  = true;
        if (padEdge(hA, 4))     accept = true;
        if (padEdge(hB, 5))     back   = true;
    }
    if (ignoreWindow) return 0;
    if (left)  *captain = (*captain + PC_CAPTAIN_COUNT - 1) % PC_CAPTAIN_COUNT;
    if (right) *captain = (*captain + 1) % PC_CAPTAIN_COUNT;
    if (accept && *captain == PC_CAPTAIN_LOUIE && !devAssignLouieInstalled()) {
        // Sin el modelo no se puede jugar con Louie: aviso y se queda.
        sDevAssignLouieNoticeUntil = SDL_GetTicks() + 3000;
        accept = false;
    }
    if (accept) return 1;
    if (back) return 2;
    return 0;
}

// Dibujo del selector dentro de un panel (panelX/Y/W del prompt).
void captainPickDraw(DGXGraphics* gfx, int panelX, int panelY, int panelW, int player, int captain, const char* helpDefault) {
    char line1[64];
    snprintf(line1, sizeof(line1), "Player %d, choose your captain", player + 1);
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(line1) / 2, panelY + 56,
                    "%s", Colour(255, 229, 120, 255), Colour(8, 12, 28, 255), line1);
    const int boxW = kCaptainBoxW, boxH = kCaptainBoxH;
    const int boxY = panelY + 84;
    for (int i = 0; i < PC_CAPTAIN_COUNT; i++) {
        const bool sel = captain == i;
        const int boxX = panelX + kCaptainBoxLeft + i * (boxW + kCaptainBoxGap);
        if (pc_settings_p2d_active()) {
            // Cada capitán en su burbuja de cristal.
            pc_settings_p2d_plate(boxX, boxY, boxW, boxH, 1);
        } else {
            gfx->setColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220), true);
            gfx->setAuxColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220));
            gfx->fillRectangle(RectArea(boxX, boxY, boxX + boxW, boxY + boxH));
        }
        // Sprite al doble de tamaño, centrado sobre el nombre y apoyado en la
        // misma línea que Olimar (32 px de alto). Sin PNG, un Pikmin usa su
        // trozo de piki3 a tamaño real.
        int ax = 0, aw = 0, ah = 0, scale = 2;
        Texture* art = pc_art_texture(kCaptainArt[i]);
        if (art && !pc_art_size(kCaptainArt[i], &aw, &ah)) art = nullptr;
        if (!art && i >= PC_CAPTAIN_PIKMIN_RED) {
            static Texture* sPiki3 = nullptr;
            if (!sPiki3) sPiki3 = zen::loadTexExp("screen/tex/piki3.bti", true, true);
            art   = sPiki3;
            ax    = kCaptainPikiCol[i][0];
            aw    = kCaptainPikiCol[i][1] - ax;
            ah    = art ? art->mHeight : 0;
            scale = 1;
        }
        if (art) {
            const int dw = aw * scale, dh = ah * scale;
            const int dx = boxX + boxW / 2 - dw / 2, dy = boxY + 10 + (scale == 2 && dh < 64 ? 64 - dh : 0);
            const Colour tint(255, 255, 255, sel ? 255 : 190);
            if (pc_settings_p2d_active()) {
                pc_settings_p2d_image(dx, dy, dw, dh, art, float(ax + aw) / art->mWidth, float(ah) / art->mHeight, tint,
                                      float(ax) / art->mWidth);
            } else {
                gfx->setColour(tint, true);
                gfx->setAuxColour(tint);
                gfx->useTexture(art, GX_TEXMAP0);
                gfx->drawRectangle(RectArea(dx, dy, dx + dw, dy + dh), RectArea(ax, 0, ax + aw, ah), nullptr);
                gfx->useTexture(nullptr, GX_TEXMAP0);
            }
        }
        if (drawGlassOption(boxX, boxY + boxH - 38, boxW, 30, kCaptainNames[i], sel)) continue;
        const int tw = menuTextWidth(kCaptainNames[i]);
        drawTextOutline(boxX + boxW / 2 - tw / 2, boxY + boxH - 30, "%s",
                        sel ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                        Colour(8, 12, 28, 255), kCaptainNames[i]);
    }
    const bool louieMissing = !devAssignLouieInstalled();
    const bool noticing = louieMissing && SDL_GetTicks() < sDevAssignLouieNoticeUntil;
    const char* help = noticing ? "Louie model not installed: Advanced Options > HD Models (Louie zip)."
                     : (louieMissing ? "Louie: model not installed (Advanced Options > HD Models)." : helpDefault);
    drawHelpLine(panelX + panelW / 2, panelY + 220, help, noticing ? Colour(255, 160, 120, 255) : Colour(150, 165, 195, 255));
}
}

// ── Selector de capitán de 1 jugador ──────────────────────────────────────
namespace {
bool sCaptainPromptOpen = false;
int  sCaptainPromptChoice = PC_CAPTAIN_OLIMAR;
int  sCaptainPromptResult = PC_DEVASSIGN_PENDING;
Uint32 sCaptainPromptIgnoreUntil = 0;

void pcCaptainPromptInput() {
    if (!sCaptainPromptOpen) return;
    const int r = captainPickInput(0, true, pc_window_get_controller(), &sCaptainPromptChoice,
                                   SDL_GetTicks() < sCaptainPromptIgnoreUntil);
    if (r == 1) {
        pc_coop_set_captain(0, sCaptainPromptChoice);
        sCaptainPromptResult = PC_DEVASSIGN_OK;
        sCaptainPromptOpen   = false;
    } else if (r == 2) {
        sCaptainPromptResult = PC_DEVASSIGN_CANCELLED;
        sCaptainPromptOpen   = false;
    }
}
}

void pc_captain_prompt_open(void) {
    sCaptainPromptOpen   = true;
    sCaptainPromptChoice = PC_CAPTAIN_OLIMAR;
    sCaptainPromptResult = PC_DEVASSIGN_PENDING;
    sCaptainPromptIgnoreUntil = SDL_GetTicks() + 250; // el A del título sigue pulsado
    pc_menu_edge_reset();
}
bool pc_captain_prompt_active(void) { return sCaptainPromptOpen; }
int pc_captain_prompt_result(void) { return sCaptainPromptResult; }

void pc_captain_prompt_draw(void) {
    if (!sCaptainPromptOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;
    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
    const int panelW = 620, panelH = 260;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Captain");
    captainPickDraw(gfx, panelX, panelY, panelW, 0, sCaptainPromptChoice,
                    "Left/Right: choose    A / Enter: confirm    B / Esc: back");
}

void pc_devassign_prompt_open(void) {
    sDevAssignOpen   = true;
    sDevAssignChoice = 0;
    sDevAssignResult = PC_DEVASSIGN_PENDING;
    // Capitanes por defecto (no se guardan): P1 Olimar, P2 Louie.
    sDevAssignCaptain[0] = PC_CAPTAIN_OLIMAR;
    sDevAssignCaptain[1] = PC_CAPTAIN_LOUIE;
    pc_coop_set_captain(0, sDevAssignCaptain[0]);
    pc_coop_set_captain(1, sDevAssignCaptain[1]);
    devAssignRestart();
}

bool pc_devassign_prompt_active(void) { return sDevAssignOpen; }

int pc_devassign_prompt_result(void) { return sDevAssignResult; }

namespace {
void pcDevAssignPromptInput() {
    if (!sDevAssignOpen) return;

    // Esc / B cancelan en cualquier paso (vuelven al selector 1P/2P). B se
    // acepta desde cualquier mando abierto, aún sin asignar.
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE);
    cancel |= (sTouchFrameButtons & PAD_BUTTON_B) != 0;

    if (sDevAssignStep == DEVASSIGN_WaitP1 || sDevAssignStep == DEVASSIGN_WaitP2) {
        const int player = devAssignPlayer();
        int kind = PC_INPUT_DEV_NONE, id = -1;
        bool pressed = pc_window_take_button_press(&kind, &id);
#if PIKI_PC_TOUCH
        // Pantalla táctil: un toque cuenta como "P1 = pantalla" (la capa
        // táctil siempre va al pad 0). Para P2 no vale: necesita un mando.
        if (sTouchTapPending) {
            sTouchTapPending = false;
            if (player == 0 && !pressed) {
                pressed = true;
                kind    = PC_INPUT_DEV_KEYBOARD;
                id      = -1;
            }
        }
#endif
        if (cancel) {
            sDevAssignResult = PC_DEVASSIGN_CANCELLED;
            sDevAssignOpen   = false;
            return;
        }
        if (!pressed || SDL_GetTicks() < sDevAssignIgnoreUntil) return;
        // El dispositivo de P1 no puede repetirse en P2.
        if (player == 1 && kind == sDevAssignKind[0] && (kind != PC_INPUT_DEV_GAMEPAD || id == sDevAssignId[0])) return;
        sDevAssignKind[player] = kind;
        sDevAssignId[player]   = id;
        pc_window_input_assign(player, kind, id);
        pc_window_discard_button_presses();
        sDevAssignIgnoreUntil = SDL_GetTicks() + 400;
        sDevAssignStep = player == 0 ? DEVASSIGN_PickP1 : DEVASSIGN_PickP2;
        sDevAssignChoice = 0;
        pc_menu_edge_reset();
        return;
    }

    if (sDevAssignStep == DEVASSIGN_PickP1 || sDevAssignStep == DEVASSIGN_PickP2) {
        // Elección de capitán con el dispositivo del propio jugador (teclado
        // si es el suyo, o para P1 siempre; táctil para P1).
        const int player = devAssignPlayer();
        const bool keyboardOk = player == 0 || sDevAssignKind[player] == PC_INPUT_DEV_KEYBOARD;
        SDL_GameController* ctl = player == 0 ? pc_window_get_controller() : pc_window_get_controller_p2();
        const bool padOk = ctl && sDevAssignKind[player] == PC_INPUT_DEV_GAMEPAD;
        pc_window_discard_button_presses();
        if (cancel) {
            sDevAssignResult = PC_DEVASSIGN_CANCELLED;
            sDevAssignOpen   = false;
            return;
        }
        // El botón con el que se acaba de reclamar el mando sigue pulsado.
        const int r = captainPickInput(player, keyboardOk, padOk ? ctl : nullptr, &sDevAssignCaptain[player],
                                       SDL_GetTicks() < sDevAssignIgnoreUntil);
        if (r == 1) {
            pc_coop_set_captain(player, sDevAssignCaptain[player]);
            sDevAssignStep   = player == 0 ? DEVASSIGN_WaitP2 : DEVASSIGN_Confirm;
            sDevAssignChoice = 0;
            pc_menu_edge_reset();
            if (ctl) { promptPadA(ctl); promptPadB(ctl); }
            sDevAssignIgnoreUntil = SDL_GetTicks() + 300;
        } else if (r == 2) {
            devAssignRestart();
        }
        return;
    }

    // Confirmación: navegable con teclado y con los dos mandos asignados.
    bool left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    accept |= (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    // Los flancos (padEdge) se guardan por acción, no por mando: hay que
    // combinar primero el estado de los dos mandos y detectar el flanco una
    // sola vez, o el mando que no pulsa "suelta" el slot cada frame y el otro
    // dispara un flanco nuevo en cada tick.
    SDL_GameController* pads[2] = { pc_window_get_controller(), pc_window_get_controller_p2() };
    bool hLeft = false, hRight = false, hA = false, hB = false;
    for (int i = 0; i < 2; i++) {
        SDL_GameController* ctl = pads[i];
        if (!ctl) continue;
        hLeft  |= SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || menuStickHorizontal(ctl, -1);
        hRight |= SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || menuStickHorizontal(ctl, 1);
        hA     |= pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_A));
        hB     |= pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_B));
    }
    if (padEdge(hLeft, 2))  left   = true;
    if (padEdge(hRight, 3)) right  = true;
    if (padEdge(hA, 4))     accept = true;
    if (padEdge(hB, 5))     cancel = true;
    pc_window_discard_button_presses();
    // El botón con el que P2 se acaba de asignar sigue pulsado: los flancos
    // ya se han latcheado arriba, pero no se actúa hasta pasar la ventana.
    if (SDL_GetTicks() < sDevAssignIgnoreUntil) return;

    if (left || right) sDevAssignChoice = sDevAssignChoice ? 0 : 1;
    if (accept) {
        if (sDevAssignChoice == 1) { devAssignRestart(); return; }
        sDevAssignResult = PC_DEVASSIGN_OK;
        sDevAssignOpen   = false;
    } else if (cancel) {
        sDevAssignResult = PC_DEVASSIGN_CANCELLED;
        sDevAssignOpen   = false;
    }
}
} // namespace

void pc_devassign_prompt_draw(void) {
    if (!sDevAssignOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));


    const int panelW = 620;
    const int panelH = 260;
    const int panelX = screenW / 2 - panelW / 2;
    const int panelY = screenH / 2 - panelH / 2;

    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Controllers");

    if (sDevAssignStep == DEVASSIGN_PickP1 || sDevAssignStep == DEVASSIGN_PickP2) {
        const int player = devAssignPlayer();
        captainPickDraw(gfx, panelX, panelY, panelW, player, sDevAssignCaptain[player],
                        "Left/Right: choose    A / Enter: confirm    B: reassign    Esc: back");
        return;
    }

    if (sDevAssignStep != DEVASSIGN_Confirm) {
        const int player = devAssignPlayer();
        char line1[64];
        snprintf(line1, sizeof(line1), "Player %d, press a button on your controller", player + 1);
#if PIKI_PC_TOUCH
        const char* line2 = player == 0 ? "Any gamepad button, or tap the screen." : "Any gamepad button.";
#else
        const char* line2 = "Any gamepad button, or a keyboard key.";
#endif
        drawTextOutline(panelX + panelW / 2 - menuTextWidth(line1) / 2, panelY + 90,
                        "%s", Colour(255, 229, 120, 255), Colour(8, 12, 28, 255), line1);
        drawTextOutline(panelX + panelW / 2 - menuTextWidth(line2) / 2, panelY + 130,
                        "%s", Colour(190, 200, 220, 255), Colour(8, 12, 28, 255), line2);
        if (player == 1) {
            char p1[96];
            snprintf(p1, sizeof(p1), "Player 1: %s  (%s)", devAssignName(0), devAssignCaptainName(sDevAssignCaptain[0]));
            drawTextOutline(panelX + panelW / 2 - menuTextWidth(p1) / 2, panelY + 166,
                            "%s", Colour(150, 165, 195, 255), Colour(8, 12, 28, 255), p1);
        }
        const char* help = "Esc: back";
        drawHelpLine(panelX + panelW / 2, panelY + 212, help, Colour(150, 165, 195, 255));
        return;
    }

    for (int p = 0; p < 2; p++) {
        char row[96];
        snprintf(row, sizeof(row), "Player %d:  %s  -  %s", p + 1, devAssignName(p), devAssignCaptainName(sDevAssignCaptain[p]));
        drawTextOutline(panelX + 40, panelY + 62 + p * 28, "%s",
                        Colour(214, 224, 245, 255), Colour(8, 12, 28, 255), row);
    }

    const char* options[2] = { "Start", "Reassign" };
    const int optY = panelY + 130;
    for (int i = 0; i < 2; i++) {
        const bool sel = (i == sDevAssignChoice);
        const int boxW = 230;
        const int boxX = panelX + 40 + i * (boxW + 40);
        if (drawGlassOption(boxX, optY, boxW, 44, options[i], sel, 20, 29)) continue;
        gfx->setColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220), true);
        gfx->setAuxColour(sel ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220));
        gfx->fillRectangle(RectArea(boxX, optY, boxX + boxW, optY + 40));
        const int tw = menuTextWidth(options[i]);
        drawTextOutline(boxX + boxW / 2 - tw / 2, optY + 12, "%s",
                        sel ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                        Colour(8, 12, 28, 255), options[i]);
    }

    const char* detail = sDevAssignChoice ? "Pick the controllers again." : "Continue to file select.";
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(detail) / 2, panelY + 186,
                    "%s", Colour(190, 200, 220, 255), Colour(8, 12, 28, 255), detail);
    const char* help = "Left/Right: choose    A / Enter: confirm    B / Esc: back";
    drawHelpLine(panelX + panelW / 2, panelY + 212, help, Colour(150, 165, 195, 255));
}

// Contador de Pikmin ociosos. GameStat::freePikis ya es exactamente eso: lo
// lleva ActFree al entrar y salir, así que no hace falta recorrer nada. Solo
// aparece cuando hay alguno, como en Pikmin 3 y 4: un cero permanente se
// vuelve ruido y se deja de mirar.
static Uint32 sLastGameplayFrameMs = 0;
static int sLockOnActive = 0;

void pc_settings_note_lock_on(int hasTarget) {
    sLockOnActive = hasTarget;
}

// True mientras el juego avisa de que hay partida en marcha (no menús, pausa
// ni pantallas). Free Camera solo se queda la tecla B en ese caso.
int pc_settings_in_gameplay(void) {
    return SDL_GetTicks() - sLastGameplayFrameMs < 250 ? 1 : 0;
}

void pc_settings_note_gameplay_frame(void) {
    sLastGameplayFrameMs = SDL_GetTicks();
}

// El objetivo fijado se marca en el mundo (anillo y marcador, en navi.cpp);
// aquí ya no se dibuja nada.
void pc_settings_draw_lock_on(void) {
}

void pc_settings_draw_idle_counter(void) {
    if (!pc_settings_get_idle_counter()) return;
    if (sMenuOpen || pc_glass_menu_active()) return;
    // Solo mientras el mundo se está simulando. 250 ms de margen cubre una
    // pausa breve sin dejar el contador colgado en el menú o en el título.
    if (sLastGameplayFrameMs == 0 || SDL_GetTicks() - sLastGameplayFrameMs > 250) return;
    if (!gsys || !gsys->mDGXGfx) return;

    const int idle = GameStat::freePikis;
    if (idle <= 0) return;

    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    char buf[32];
    snprintf(buf, sizeof(buf), "IDLE %d", idle);
    // Justo encima del total del HUD (la tercera cifra del contador), para
    // que se lea como una cifra más del bloque y no como un aviso suelto.
    if (pc_settings_p2d_active()) {
        // Misma fuente y proporción que el texto de los menús burbuja (14x21
        // en el espacio de 480 de alto); aquí se dibuja en píxeles reales,
        // así que se escala con la altura de la pantalla.
        const int fw = std::max(8, 14 * screenH / 480);
        const int fh = std::max(12, 21 * screenH / 480);
        const int x  = (int)(screenW * 0.917f) - pc_settings_p2d_text_width(buf, fw) / 2;
        const int y  = (int)(screenH * 0.790f);
        pc_settings_p2d_text(x, y, buf, Colour(255, 170, 30, 255), fw, fh);
        return;
    }
    const int x = (int)(screenW * 0.917f) - menuTextWidth(buf) / 2;
    const int y = (int)(screenH * 0.790f);
    drawTextOutline(x, y, "%s", Colour(255, 190, 28, 255), Colour(24, 12, 0, 255), buf);
}

// ─── Modo VS: reglas, menú previo, marcador, cuenta atrás y pantalla final ──
namespace {
const f32 kVsDurations[5]  = { 300.0f, 600.0f, 900.0f, 1500.0f, 1800.0f };
const f32 kVsRocketHps[3]  = { 60.0f, 100.0f, 150.0f };
const f32 kVsBigPiece[4]   = { 180.0f, 420.0f, 0.0f, -1.0f };
const int kVsPikiLimits[3] = { 25, 40, 50 };
const f32 kVsPellets[4]    = { 30.0f, 45.0f, 60.0f, 0.0f };
constexpr int kVsRuleRows  = 6;

bool vsSpanish() { return pc_settings_get_language() == 3; } // OS_LANG_SPANISH

int* vsRuleField(int row)
{
    switch (row) {
    case 0: return &sConfig.vsDuration;
    case 1: return &sConfig.vsRocketWin;
    case 2: return &sConfig.vsRocketHp;
    case 3: return &sConfig.vsBigPiece;
    case 4: return &sConfig.vsPikiLimit;
    default: return &sConfig.vsPellets;
    }
}
int vsRuleCount(int row)
{
    static const int counts[kVsRuleRows] = { 5, 2, 3, 4, 3, 4 };
    return counts[row];
}
void vsRuleText(int row, const char** label, char* value, size_t n)
{
    const bool es = vsSpanish();
    const int v   = *vsRuleField(row);
    switch (row) {
    case 0:
        *label = es ? "Duración" : "Duration";
        snprintf(value, n, "%d min", int(kVsDurations[v] / 60.0f));
        break;
    case 1:
        *label = es ? "Asedio al cohete" : "Rocket siege";
        snprintf(value, n, "%s", v ? (es ? "Sí" : "On") : (es ? "No" : "Off"));
        break;
    case 2: {
        static const char* es3[3] = { "Baja", "Normal", "Alta" };
        static const char* en3[3] = { "Low", "Normal", "High" };
        *label = es ? "Vida del cohete" : "Rocket health";
        snprintf(value, n, "%s", es ? es3[v] : en3[v]);
        break;
    }
    case 3: {
        static const char* es4[4] = { "Minuto 3", "Minuto 7", "Desde el inicio", "Sin pieza gorda" };
        static const char* en4[4] = { "Minute 3", "Minute 7", "From the start", "No big part" };
        *label = es ? "Pieza gorda" : "Big part";
        snprintf(value, n, "%s", es ? es4[v] : en4[v]);
        break;
    }
    case 4:
        *label = es ? "Pikmin por jugador" : "Pikmin per player";
        snprintf(value, n, "%d", kVsPikiLimits[v]);
        break;
    default: {
        static const char* es4[4] = { "Cada 30 s", "Cada 45 s", "Cada 60 s", "Ninguna" };
        static const char* en4[4] = { "Every 30 s", "Every 45 s", "Every 60 s", "None" };
        *label = es ? "Pastillas" : "Pellets";
        snprintf(value, n, "%s", es ? es4[v] : en4[v]);
        break;
    }
    }
}

bool sVsRulesOpen   = false;
int sVsRulesRow     = 0;
int sVsRulesResult  = PC_DEVASSIGN_PENDING;
int sVsEndChoice    = 0; // 0 revancha, 1 título
Uint32 sVsOverSince = 0; // cuándo acabó la partida (la pantalla final sale un poco después)
constexpr Uint32 kVsEndScreenDelayMs = 2500;

bool vsEndScreenShown()
{
    return pc_vs_active() && pc_vs_match_over() && sVsOverSince && SDL_GetTicks() - sVsOverSince >= kVsEndScreenDelayMs;
}

void pcVsRulesInput()
{
    if (!sVsRulesOpen) return;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE);
    up |= (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
    down |= (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
    left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    accept |= (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    cancel |= (sTouchFrameButtons & PAD_BUTTON_B) != 0;
    sTouchTapPending = false;
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (padNavUp(ctl)) up = true;
        if (padNavDown(ctl)) down = true;
        if (padNavLeft(ctl)) left = true;
        if (padNavRight(ctl)) right = true;
        if (promptPadA(ctl)) accept = true;
        if (promptPadB(ctl)) cancel = true;
    }
    if (up) sVsRulesRow = (sVsRulesRow + kVsRuleRows - 1) % kVsRuleRows;
    if (down) sVsRulesRow = (sVsRulesRow + 1) % kVsRuleRows;
    if (left || right) {
        int* field  = vsRuleField(sVsRulesRow);
        const int n = vsRuleCount(sVsRulesRow);
        *field      = (*field + (left ? n - 1 : 1)) % n;
    }
    if (accept) {
        saveConfig();
        sVsRulesResult = PC_DEVASSIGN_OK;
        sVsRulesOpen   = false;
    } else if (cancel) {
        saveConfig();
        sVsRulesResult = PC_DEVASSIGN_CANCELLED;
        sVsRulesOpen   = false;
    }
}

void pcVsEndScreenInput()
{
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A) || keyWentDown(SDL_SCANCODE_UP);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D) || keyWentDown(SDL_SCANCODE_DOWN);
    bool accept = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    left |= (sTouchFrameButtons & (PAD_BUTTON_LEFT | PAD_BUTTON_UP)) != 0;
    right |= (sTouchFrameButtons & (PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN)) != 0;
    accept |= (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    sTouchTapPending = false;
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (padNavLeft(ctl) || padNavUp(ctl)) left = true;
        if (padNavRight(ctl) || padNavDown(ctl)) right = true;
        if (promptPadA(ctl)) accept = true;
    }
    if (left || right) sVsEndChoice = sVsEndChoice ? 0 : 1;
    if (accept) {
        SeSystem::playSysSe(SYSSE_DECIDE1);
        pc_vs_request_exit(sVsEndChoice == 0 ? PC_VS_EXIT_REMATCH : PC_VS_EXIT_TITLE);
        sVsOverSince = 0;
    }
}
} // namespace

void pc_settings_apply_vs_rules(void) {
    PcVsRules r;
    r.matchSeconds    = kVsDurations[std::clamp(sConfig.vsDuration, 0, 4)];
    r.rocketWin       = sConfig.vsRocketWin != 0;
    r.rocketHp        = kVsRocketHps[std::clamp(sConfig.vsRocketHp, 0, 2)];
    r.bigPieceSeconds = kVsBigPiece[std::clamp(sConfig.vsBigPiece, 0, 3)];
    // Que la gorda salga antes del final (5 min con "a los 7 min" no saldría nunca).
    if (r.bigPieceSeconds >= r.matchSeconds) r.bigPieceSeconds = r.matchSeconds * 0.5f;
    // Entre los dos no pueden pasar del límite general del campo.
    r.fieldLimit    = std::min(kVsPikiLimits[std::clamp(sConfig.vsPikiLimit, 0, 2)], pc_settings_get_piki_limit() / 2);
    r.pelletSeconds = kVsPellets[std::clamp(sConfig.vsPellets, 0, 3)];
    pc_vs_set_rules(r);
}

void pc_vsrules_prompt_open(void) {
    sVsRulesOpen   = true;
    sVsRulesRow    = 0;
    sVsRulesResult = PC_DEVASSIGN_PENDING;
    pc_menu_edge_reset();
}
bool pc_vsrules_prompt_active(void) { return sVsRulesOpen; }
int pc_vsrules_prompt_result(void) { return sVsRulesResult; }

namespace {
// Texto del menú VS con tamaño (el de la interfaz del juego si está activa).
void vsText(int x, int y, const char* s, Colour c, int fw = 12, int fh = 18)
{
    if (pc_settings_p2d_active()) pc_settings_p2d_text_styled(x, y, s, c, c, fw, fh, sGlassText);
    else drawTextOutline(x, y, "%s", c, Colour(8, 12, 28, 255), s);
}
int vsTextW(const char* s, int fw = 12) { return pc_settings_p2d_active() ? pc_settings_p2d_text_width(s, fw) : menuTextWidth(s); }

Texture* vsIcon(const char* name)
{
    static std::map<std::string, Texture*> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    char path[64];
    snprintf(path, sizeof(path), "screen/tex/%s.bti", name);
    Texture* t  = zen::loadTexExp(path, true, true);
    cache[name] = t;
    return t;
}
void vsDrawIcon(const char* name, int x, int y, int w, int h)
{
    Texture* t = vsIcon(name);
    if (t && pc_settings_p2d_active()) pc_settings_p2d_image(x, y, w, h, t, 1.0f, 1.0f, Colour(255, 255, 255, 255));
}
void vsCard(DGXGraphics* gfx, int x, int y, int w, int h)
{
    if (pc_settings_p2d_active()) {
        pc_settings_p2d_plate(x, y, w, h, 1);
    } else {
        gfx->setColour(Colour(26, 30, 48, 220), true);
        gfx->setAuxColour(Colour(26, 30, 48, 220));
        gfx->fillRectangle(RectArea(x, y, x + w, y + h));
    }
}

void vsRuleHelp(int row, const char** l1, const char** l2)
{
    const bool es = vsSpanish();
    *l2           = "";
    switch (row) {
    case 0: *l1 = es ? "Cuánto dura la partida." : "How long the match lasts."; break;
    case 1:
        if (sConfig.vsRocketWin) {
            *l1 = es ? "Pikmin libres junto al cohete rival" : "Free Pikmin by the rival rocket";
            *l2 = es ? "lo dañan. Destruirlo gana." : "damage it. Destroying it wins.";
        } else {
            *l1 = es ? "Los cohetes no se pueden atacar." : "Rockets can't be attacked.";
        }
        break;
    case 2:
        *l1 = es ? "Cuánto aguanta el cohete asediado." : "How long a rocket lasts under siege.";
        *l2 = es ? "Con 20 Pikmin: 24 s, 40 s o 60 s." : "With 20 Pikmin: 24 s, 40 s or 60 s.";
        break;
    case 3: *l1 = es ? "Vale 5 puntos y sale en el cráter." : "Worth 5 points, drops in the crater."; break;
    case 4:
        *l1 = es ? "Máximo de Pikmin de cada jugador" : "Most Pikmin each player can";
        *l2 = es ? "en el campo a la vez." : "have on the field at once.";
        break;
    default:
        *l1 = es ? "Reaparecen detrás de cada base" : "They respawn behind each base";
        *l2 = es ? "y junto al cráter." : "and next to the crater.";
        break;
    }
}
} // namespace

void pc_vsrules_prompt_draw(void) {
    if (!sVsRulesOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;
    const bool es = vsSpanish();

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const Colour title(255, 229, 120, 255), body(214, 224, 245, 255), dim(150, 165, 195, 255);
    const int panelW = std::min(800, screenW - 24), panelH = 452;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, es ? "VS: Carrera de piezas" : "VS: Part Race");

    const int colW = (panelW - 72) / 2;
    const int lx   = panelX + 24;
    const int rx   = lx + colW + 24;

    // ── Izquierda: cómo se juega, en tres tarjetas ─────────────────────────
    int y = panelY + 52;
    vsCard(gfx, lx, y, colW, 104);
    vsDrawIcon("c_rocket", lx + 14, y + 16, 40, 56);
    vsText(lx + 66, y + 12, es ? "OBJETIVO" : "GOAL", title, 14, 21);
    vsText(lx + 66, y + 40, es ? "Lleva piezas de la nave" : "Carry ship parts", body, 10, 15);
    vsText(lx + 66, y + 58, es ? "a tu cohete." : "to your rocket.", body, 10, 15);
    vsDrawIcon("parts32", lx + 66, y + 78, 18, 18);
    vsText(lx + 90, y + 80, es ? "Pequeña 1  Mediana 2  Gorda 5" : "Small 1  Medium 2  Big 5", title, 10, 15);

    y += 114;
    vsCard(gfx, lx, y, colW, 84);
    vsText(lx + 18, y + 12, es ? "CÓMO SE GANA" : "HOW TO WIN", title, 14, 21);
    vsText(lx + 18, y + 40, es ? "Más puntos al acabar el tiempo," : "Most points when time is up,", body, 10, 15);
    vsText(lx + 18, y + 58, es ? "o destruyendo el cohete rival." : "or destroy the rival rocket.", body, 10, 15);

    y += 94;
    vsCard(gfx, lx, y, colW, 146);
    vsDrawIcon("rp_l64", lx + 10, y + 10, 48, 48);
    vsText(lx + 66, y + 12, es ? "CLAVES" : "TIPS", title, 14, 21);
    static const char* tipsEs[5] = { "Roba piezas atacando a quien carga.", "Tus Pikmin junto al cohete rival",
                                     "lo dañan. Lanza uno al capitán", "rival para tumbarlo.",
                                     "Charcas: azules. Roca-bomba: atajo." };
    static const char* tipsEn[5] = { "Attack carriers to steal parts.", "Your Pikmin by the rival rocket",
                                     "damage it. Throw one at the", "rival captain to knock them down.",
                                     "Ponds: Blues. Bomb-rock: shortcut." };
    for (int i = 0; i < 5; i++) {
        vsText(lx + (i == 0 ? 66 : 18), y + (i == 0 ? 40 : 44 + i * 19), (es ? tipsEs : tipsEn)[i], body, 10, 15);
    }

    // ── Derecha: reglas ─────────────────────────────────────────────────────
    vsText(rx + 8, panelY + 56, es ? "REGLAS" : "RULES", title, 14, 21);
    const int rowsY = panelY + 88;
    for (int row = 0; row < kVsRuleRows; row++) {
        const char* label = "";
        char value[48];
        vsRuleText(row, &label, value, sizeof(value));
        const bool sel = row == sVsRulesRow;
        const int ry   = rowsY + row * 38;
        // Fila elegida: bolita de cristal delante y valor en naranja (estilo
        // burbuja), en vez de la placa amarilla.
        if (sel && pc_settings_p2d_active())
            pc_settings_p2d_cursor(rx - 8, ry + 8, 18, std::fmod(SDL_GetTicks() / 1000.0f * 10.0f, 6.2831853f));
        const Colour c = sel ? Colour(255, 255, 255, 255) : Colour(190, 200, 220, 255);
        vsText(rx + 10, ry, label, c, 11, 17);
        char shown[64];
        snprintf(shown, sizeof(shown), sel ? "< %s >" : "%s", value);
        vsText(rx + colW - 10 - vsTextW(shown, 11), ry, shown, sel ? Colour(255, 170, 30, 255) : c, 11, 17);
    }
    const char* h1;
    const char* h2;
    vsRuleHelp(sVsRulesRow, &h1, &h2);
    const int descY = rowsY + kVsRuleRows * 38 + 6;
    vsCard(gfx, rx - 4, descY, colW + 8, 60);
    vsText(rx + 10, descY + 12, h1, body, 10, 15);
    vsText(rx + 10, descY + 32, h2, body, 10, 15);

    const char* help = es ? "Arriba/Abajo: opción   Izq/Der: cambiar   A: jugar   B: salir"
                          : "Up/Down: option   Left/Right: change   A: play   B: back";
    drawHelpLine(panelX + panelW / 2, panelY + panelH - 38, help, dim);
}

bool pc_vs_end_screen_active(void) { return vsEndScreenShown(); }

// Marcador, reloj, cuenta atrás (con el mundo en pausa) y pantalla final.
// Se dibuja cada fotograma, así que también lleva la pausa y los sonidos.
void pc_settings_draw_vs_hud(void) {
    if (!pc_vs_active()) return;

    static int sSerial      = -1;
    static int sLastPhase   = -2;
    static bool sPausedByVs = false;
    static bool sWasOver    = false;
    if (sSerial != pc_vs_match_serial()) {
        sSerial      = pc_vs_match_serial();
        sLastPhase   = -2;
        sPausedByVs  = false;
        sWasOver     = false;
        sVsOverSince = 0;
        sVsEndChoice = 0;
    }
    // Con un menú del port encima, la cuenta atrás espera (va con reloj real).
    const bool menuOver = sMenuOpen || pc_glass_menu_active();
    pc_vs_countdown_set_frozen(menuOver);
    if (menuOver) return;
    const bool live = sLastGameplayFrameMs != 0 && SDL_GetTicks() - sLastGameplayFrameMs <= 250;
    if (!live && !sPausedByVs) return;
    if (!gsys || !gsys->mDGXGfx) return;

    // Pausa: durante la cuenta atrás y al acabar la partida.
    const int phase    = pc_vs_countdown_phase();
    const bool over    = pc_vs_match_over();
    const bool wantPause = phase > 0 || over;
    if (wantPause != sPausedByVs) {
        gameflow.mPauseAll = wantPause ? TRUE : FALSE;
        sPausedByVs        = wantPause;
    }
    if (phase != sLastPhase) {
        if (phase > 0) SeSystem::playSysSe(SYSSE_COUNTDOWN);
        else if (phase == 0) SeSystem::playSysSe(SYSSE_TIME_SIGNAL);
        sLastPhase = phase;
    }
    if (over && !sWasOver) {
        SeSystem::playSysSe(SYSSE_WORK_FINISH);
        sVsOverSince = SDL_GetTicks();
    }
    sWasOver = over;

    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;
    const bool es     = vsSpanish();
    const int screenW = gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
    const Colour shadow(8, 12, 28, 255);
    const int cx = screenW / 2;

    // Pantalla final.
    if (vsEndScreenShown()) {
        gfx->setColour(Colour(0, 0, 0, 150), true);
        gfx->setAuxColour(Colour(0, 0, 0, 150));
        gfx->fillRectangle(RectArea(0, 0, screenW, screenH));
        const int panelW = 520, panelH = 330;
        const int panelX = cx - panelW / 2, panelY = screenH / 2 - panelH / 2;
        drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
        drawPikminHeader(gfx, panelX, panelY, panelW, es ? "Resultado" : "Result");
        const int w = pc_vs_winner();
        char title[48];
        if (w == 2) snprintf(title, sizeof(title), "%s", es ? "EMPATE" : "DRAW");
        else snprintf(title, sizeof(title), es ? "GANA EL JUGADOR %d" : "PLAYER %d WINS", w + 1);
        const Colour winCol = w == 0 ? Colour(120, 175, 255, 255) : (w == 1 ? Colour(205, 130, 255, 255) : Colour(255, 229, 120, 255));
        pc_settings_p2d_text(cx - pc_settings_p2d_text_width(title, 20) / 2, panelY + 48, title, winCol, 20, 30);
        const char* why = pc_vs_won_by_rocket() ? (es ? "Cohete destruido" : "Rocket destroyed") : (es ? "Fin del tiempo" : "Time up");
        drawTextOutline(cx - menuTextWidth(why) / 2, panelY + 86, "%s", Colour(214, 224, 245, 255), shadow, why);

        const char* rowNames[4] = { es ? "Puntos" : "Points", es ? "Piezas" : "Parts", es ? "Cohete" : "Rocket",
                                    es ? "Pikmin vivos" : "Pikmin alive" };
        drawTextOutline(panelX + 250, panelY + 118, "%s", Colour(120, 175, 255, 255), shadow, "P1");
        drawTextOutline(panelX + 380, panelY + 118, "%s", Colour(205, 130, 255, 255), shadow, "P2");
        for (int r = 0; r < 4; r++) {
            const int y = panelY + 146 + r * 25;
            drawTextOutline(panelX + 50, y, "%s", Colour(170, 180, 200, 255), shadow, rowNames[r]);
            for (int p = 0; p < 2; p++) {
                char v[16];
                if (r == 0) snprintf(v, sizeof(v), "%d", pc_vs_score(p));
                else if (r == 1) snprintf(v, sizeof(v), "%d", pc_vs_pieces(p));
                else if (r == 2) snprintf(v, sizeof(v), "%d%%", pc_vs_rocket_percent(p));
                else snprintf(v, sizeof(v), "%d", pc_vs_alive(p));
                drawTextOutline(panelX + (p ? 380 : 250), y, "%s", Colour(255, 255, 255, 255), shadow, v);
            }
        }
        const char* opts[2] = { es ? "Revancha" : "Rematch", es ? "Volver al título" : "Back to title" };
        for (int i = 0; i < 2; i++) {
            const bool sel = i == sVsEndChoice;
            char o[40];
            snprintf(o, sizeof(o), sel ? "> %s <" : "%s", opts[i]);
            const int ox = i == 0 ? panelX + 130 : panelX + panelW - 130;
            drawTextOutline(ox - menuTextWidth(o) / 2, panelY + panelH - 52, "%s",
                            sel ? Colour(255, 229, 120, 255) : Colour(150, 165, 195, 255), shadow, o);
        }
        return;
    }

    // Marcador y reloj arriba en el centro (encima de la división).
    char left[16], right[16], clock[16];
    snprintf(left, sizeof(left), "P1  %d", pc_vs_score(0));
    snprintf(right, sizeof(right), "%d  P2", pc_vs_score(1));
    const int secs = (int)(pc_vs_match_time_left() + 0.999f);
    snprintf(clock, sizeof(clock), "%d:%02d", secs / 60, secs % 60);
    const int y = 10;
    const int clockW = menuTextWidth(clock);
    drawTextOutline(cx - clockW / 2, y, "%s", secs <= 30 ? Colour(255, 120, 90, 255) : Colour(255, 255, 255, 255), shadow, clock);
    drawTextOutline(cx - clockW / 2 - 24 - menuTextWidth(left), y, "%s", Colour(120, 175, 255, 255), shadow, left);
    drawTextOutline(cx + clockW / 2 + 24, y, "%s", Colour(205, 130, 255, 255), shadow, right);
    if (pc_vs_rules().rocketWin) {
        char hp[2][24];
        for (int p = 0; p < 2; p++) snprintf(hp[p], sizeof(hp[p]), es ? "COHETE %d%%" : "ROCKET %d%%", pc_vs_rocket_percent(p));
        auto hpColour = [](int v) { return v > 50 ? Colour(170, 230, 170, 255) : (v > 25 ? Colour(255, 210, 90, 255) : Colour(255, 100, 80, 255)); };
        drawTextOutline(cx - clockW / 2 - 24 - menuTextWidth(hp[0]), y + 26, "%s", hpColour(pc_vs_rocket_percent(0)), shadow, hp[0]);
        drawTextOutline(cx + clockW / 2 + 24, y + 26, "%s", hpColour(pc_vs_rocket_percent(1)), shadow, hp[1]);
    }

    // Cuenta atrás: 3, 2, 1, START.
    if (phase >= 0) {
        char big[16];
        snprintf(big, sizeof(big), "%s", phase > 0 ? (phase == 3 ? "3" : phase == 2 ? "2" : "1") : "START!");
        const int fw = phase > 0 ? 64 : 48, fh = phase > 0 ? 96 : 72;
        pc_settings_p2d_text(cx - pc_settings_p2d_text_width(big, fw) / 2, screenH / 2 - fh / 2, big,
                             phase > 0 ? Colour(255, 255, 255, 255) : Colour(255, 229, 120, 255), fw, fh);
        return;
    }

    if (const char* msg = pc_vs_announcement()) {
        drawTextOutline(cx - menuTextWidth(msg) / 2, (int)(screenH * 0.30f), "%s", Colour(255, 229, 120, 255), shadow, msg);
    }
}

// Aviso de logro (o de logros desactivados por trucos): placa arriba a la
// derecha, entra y sale con un fundido corto.
void pc_settings_draw_achievement_toast(void) {
    if (!gsys || !gsys->mDGXGfx) return;
    float age = 0.0f;
    const int id = pc_achievements_toast(&age);
    const char* blocked = id < 0 ? pc_achievements_blocked_toast(&age) : nullptr;
    if (id < 0 && !blocked) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;
    const int screenW = gfx->mScreenWidth, screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const float fade = std::min(1.0f, std::min(age / 0.25f, (4.5f - age) / 0.4f));
    const u8 a       = u8(255.0f * std::max(0.0f, fade));
    const Colour shadow(8, 12, 28, a);
    char head[48], title[96];
    if (id >= 0) {
        const PcAchievementInfo& info = pc_achievement_info(id);
        snprintf(head, sizeof(head), "Achievement unlocked  (%d pts)", info.points);
        snprintf(title, sizeof(title), "%s", info.title);
    } else {
        snprintf(head, sizeof(head), "Achievements");
        snprintf(title, sizeof(title), "%s", blocked);
    }
    // Títulos largos: se recortan con "..." para no salirse de la pantalla
    // (el icono ocupa hasta ~120 a la izquierda).
    const int maxTextW = screenW - 80 - 120;
    while (menuTextWidth(title) > maxTextW && strlen(title) > 4) {
        title[strlen(title) - 4] = '\0';
        strcat(title, "...");
    }
    // Icono (textura del juego) a la izquierda, ajustado a 48 de alto.
    Texture* icon = id >= 0 ? achievementIcon(id) : nullptr;
    int iw = 0, ih = 0;
    if (icon && icon->mWidth > 0 && icon->mHeight > 0) {
        ih = 48;
        iw = std::min(84, icon->mWidth * ih / icon->mHeight);
        ih = icon->mHeight * iw / icon->mWidth;
    }
    const int textX = iw ? iw + 34 : 20;
    const int w = std::min(screenW - 40, std::max(menuTextWidth(head), menuTextWidth(title)) + textX + 20);
    const int h = 66, x = screenW - w - 20, y = 20;
    if (a > 0) {
        drawPikminPanel(gfx, x, y, w, h, 14);
        if (iw) {
            const int ix = x + 16, iy = y + (h - ih) / 2;
            const Colour tint(255, 255, 255, a);
            if (pc_settings_p2d_active()) {
                pc_settings_p2d_image(ix, iy, iw, ih, icon, 1.0f, 1.0f, tint, 0.0f);
            } else {
                gfx->setColour(tint, true);
                gfx->setAuxColour(tint);
                gfx->useTexture(icon, GX_TEXMAP0);
                gfx->drawRectangle(RectArea(ix, iy, ix + iw, iy + ih), RectArea(0, 0, icon->mWidth, icon->mHeight), nullptr);
                gfx->useTexture(nullptr, GX_TEXMAP0);
            }
        }
        drawTextOutline(x + textX, y + 12, "%s", Colour(255, 229, 120, a), shadow, head);
        drawTextOutline(x + textX, y + 36, "%s", Colour(255, 255, 255, a), shadow, title);
    }
    (void)screenH;
}

void pc_settings_draw(void) {
    if (!sMenuOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    // The panel uses a uniform, centred mapping without stretching and
    // without fill_ui_43_bars: 4:3 640x480 on desktop, and on mobile a
    // shorter virtual canvas as wide as the screen (bigger on small screens).
    // Mapping stays live through the P2D destructor, including submenu returns.
    f1Layout();
    struct F1Map {
        F1Map()
        {
            pc_gfx_set_menu_clip_43(0);
            pc_gfx_set_hud_wide(0);
            pc_gfx_set_ui_43_no_bars(0);
        }
        void bindPanel()
        {
#ifdef __ANDROID__
            pc_gfx_set_hud_virtual_size(kF1CanvasW, kF1CanvasH);
            pc_gfx_set_hud_wide(1);
#else
            pc_gfx_set_ui_43_no_bars(1);
#endif
        }
        ~F1Map()
        {
            pc_gfx_set_ui_43_no_bars(0);
            pc_gfx_set_hud_wide(0);
            pc_gfx_set_hud_virtual_size(0, 0);
        }
    } f1Map;

    const int screenW = kF1CanvasW;
    const int screenH = kF1CanvasH;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);

    f1Map.bindPanel();
    // Sin oscurecer la pantalla: solo se desenfoca lo que queda detrás del
    // panel (margen para las esquinas redondeadas del cristal).
    pc_gfx_blur_gx_rect(kF1PanelX + 6, kF1PanelY + 6, kF1PanelW - 12, kF1PanelH - 12, 8);

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
#ifdef __ANDROID__
    gfx->setViewport(RectArea(0, 0, screenW, screenH));
    gfx->setScissor(RectArea(0, 0, screenW, screenH));
#endif

    const int panelX = kF1PanelX;
    const int panelY = kF1PanelY;
    const int panelW = kF1PanelW;
    const int panelH = kF1PanelH;
    const int px1 = panelX, py1 = panelY;
    const int px2 = panelX + panelW, py2 = panelY + panelH;
    const int radius = 26;
    const int headerH = 34;

    drawPikminPanel(gfx, px1, py1, panelW, panelH, radius);

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
            char value[96];
            if (waiting) {
                snprintf(value, sizeof(value), "%s", sCaptureSecond ? "[2nd: press a key / Del: none]" : "[Press a key / Del: none]");
            } else {
                keyBindingPairName(i, value, sizeof(value));
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
            int boundBtn = pendingPadBinds()[i];
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

    // Dedicated HD model submenu. Models use their own package path and are
    // intentionally not mixed with texture packs.
    if (sInHdModelsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "HD Models",
                           "Up/Down: model   A: install",
                           "Esc/B: back   Restart required");

        std::error_code modelEc;
        const bool olimarInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_OLIMAR), modelEc);
        const bool louieInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE), modelEc);
        const bool louieHdInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE_HD), modelEc);
        const bool pikminInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_PIKI_RED), modelEc)
            && std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_PIKI_YELLOW), modelEc)
            && std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_PIKI_BLUE), modelEc);
        const bool bulborbInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_BULBORB), modelEc);
        const bool dwarfInstalled = std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_BULBORB_DWARF), modelEc);
        // Una fila por modelo: cada una abre el selector para su propio zip
        // (los rips originales de Pikmin 3 o un pack .nhm ya convertido).
        const char* labels[6] = { "Olimar HD", "Louie (Pikmin 2 zip)", "Louie HD (Pikmin 3 zip)", "Pikmin HD (red/yellow/blue)", "Bulborb HD", "Dwarf Bulborb HD" };
        const bool installed[6] = { olimarInstalled, louieInstalled, louieHdInstalled, pikminInstalled, bulborbInstalled, dwarfInstalled };
        for (int row = 0; row < 6; row++) {
            char value[96];
            const bool busy = sTexturePackPickerActive && row == sHdModelsSelection;
            if (busy && sTexturePackInstallFiles.load() > 0)
                snprintf(value, sizeof(value), "Installing... %d files", sTexturePackInstallFiles.load());
            else if (busy)
                snprintf(value, sizeof(value), "Selecting file...");
            else
                snprintf(value, sizeof(value), "%s", installed[row] ? "Installed" : "Not installed");
            drawSubmenuRow(gfx, subX + 20, subY + 70 + row * 28, subW - 40,
                           labels[row], value, row == sHdModelsSelection);
        }
        // The installer converts the public Pikmin 3 rips (Collada + PNG zips
        // from The Models Resource) on the device, so no external tool is needed.
        const char* hint = "Pick the original Pikmin 3 (or Pikmin 2 Louie) model zip from The Models Resource.";
        drawTextOutline(subX + subW / 2 - menuTextWidth(hint) / 2, subY + 70 + 6 * 28 + 8, "%s",
                        Colour(150, 160, 190, 255), Colour(10, 16, 36, 255), hint);
        drawTimedNotice(subX + subW / 2, subY + subH - 8);

        if (sHdModelRestartPrompt) {
            const int boxW = 560, boxH = 150;
            const int boxX = screenW / 2 - boxW / 2;
            const int boxY = screenH / 2 - boxH / 2;
            drawPikminPanel(gfx, boxX, boxY, boxW, boxH, 18);
            const char* line1 = "HD model installed.";
            const char* line2 = "Restart now so the HD models are loaded.";
            drawTextOutline(boxX + boxW / 2 - menuTextWidth(line1) / 2, boxY + 42, "%s",
                            Colour(255, 240, 180, 255), Colour(18, 26, 56, 255), line1);
            drawTextOutline(boxX + boxW / 2 - menuTextWidth(line2) / 2, boxY + 68, "%s",
                            Colour(255, 240, 180, 255), Colour(18, 26, 56, 255), line2);
            const char* prompt = "A: Restart now   B: Not yet";
            drawHelpLine(boxX + boxW / 2, boxY + 106, prompt, Colour(255, 255, 255, 255));
        }
        return;
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
        drawTimedNotice(subX + subW / 2, subY + subH - 8);

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
            drawHelpLine(boxX + boxW / 2, ty + 64, prompt, Colour(255, 255, 255, 255));
        }

        return; // Don't draw footer when texture packs submenu is open.
    }

    // Página de pestañas: grupos arriba, filas a la izquierda y opciones de
    // la fila seleccionada a la derecha.
    drawF1Page(gfx);
}

int pc_settings_get_fps_mode(void) { if (pc_speedrun_active()) return 0;
    return sConfig.fpsMode;
}

int pc_settings_get_chain_actions(void) { if (pc_speedrun_active()) return 0;
    return sConfig.chainActions;
}

int pc_settings_get_better_pathfinding(void) { if (pc_speedrun_active()) return 0;
    return sConfig.betterPathfinding;
}

int pc_settings_get_blues_only_water(void) { if (pc_speedrun_active()) return 0;
    return sConfig.bluesOnlyWater;
}

int pc_settings_get_idle_counter(void) { if (pc_speedrun_active()) return 0;
    return sConfig.idleCounter;
}

int pc_settings_get_navi_health_pct(void) { if (pc_speedrun_active()) return 100;
    return pc_hardmode_active() ? 100 : sConfig.naviHealthPct;
}

int pc_settings_get_teki_health_pct(void) { if (pc_speedrun_active()) return 100;
    return pc_hardmode_active() ? 100 : sConfig.tekiHealthPct;
}

int pc_settings_get_infinite_day(void) { if (pc_speedrun_active()) return 0;
    // VS: el día no avanza; la partida la cierra su propio reloj.
    if (pc_vs_active()) return 1;
    return pc_hardmode_active() ? 0 : sConfig.infiniteDay;
}

int pc_settings_get_free_camera(void) { if (pc_speedrun_active()) return 0;
    return sConfig.freeCamera;
}

int pc_settings_get_whistle_radius_pct(void) { if (pc_speedrun_active()) return 100;
    return sConfig.whistleRadiusPct;
}

float pc_settings_get_throw_speed_scale(void) { if (pc_speedrun_active()) return 1.0f;
    return sConfig.throwSpeedPct / 100.0f;
}

int pc_settings_get_throw_cancel_b(void) { if (pc_speedrun_active()) return 0;
    return sConfig.throwCancelB;
}

int pc_settings_get_quick_grab(void) { if (pc_speedrun_active()) return 0;
    return sConfig.quickGrab;
}

int pc_settings_get_no_trip(void) { if (pc_speedrun_active()) return 0;
    return sConfig.noTrip;
}

int pc_settings_get_whistle_pluck(void) { if (pc_speedrun_active()) return 0;
    return sConfig.whistlePluck;
}

int pc_settings_get_hide_olimar_text(void) { if (pc_speedrun_active()) return 0; return sConfig.hideOlimarText; }

int pc_settings_get_breakable_gates(void) { if (pc_speedrun_active()) return 0;
    return pc_hardmode_active() ? 0 : sConfig.breakableGates;
}
int pc_settings_get_p2_selection(void) { return sConfig.p2Selection; }
int pc_settings_get_eternal_night(void) { return sConfig.eternalNight; }
float pc_settings_get_free_camera_pad_scale(void) { return sConfig.freeCamPadPct / 100.0f; }
int pc_settings_get_bomb_control(void) { if (pc_speedrun_active()) return 0;
    return sConfig.bombControl;
}

int pc_settings_get_onion_step10(void) { if (pc_speedrun_active()) return 0;
    return sConfig.onionStep10;
}

int pc_settings_get_piki_invincible(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.pikiInvincible; }
int pc_settings_get_all_flowers(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.allFlowers; }
float pc_settings_get_carry_speed_scale(void) { if (pc_speedrun_active()) return 1.0f; return pc_hardmode_active() ? 1.0f : sConfig.carrySpeedPct / 100.0f; }
float pc_settings_get_navi_speed_scale(void) { if (pc_speedrun_active()) return 1.0f; return pc_hardmode_active() ? 1.0f : sConfig.naviSpeedPct / 100.0f; }
int pc_settings_get_unlock_zones(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.unlockZones; }
int pc_settings_get_no_day_advance(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.noDayAdvance; }
int pc_settings_get_all_onions(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.allOnions; }

int pc_settings_get_instant_whistle(void) { if (pc_speedrun_active()) return 0;
    return sConfig.instantWhistle;
}

int pc_settings_get_gyro_enabled(void) { if (pc_speedrun_active()) return 0;
    return sConfig.gyroEnabled;
}

float pc_settings_get_gyro_sensitivity(void) {
    return sConfig.gyroSensitivity;
}

int pc_settings_get_gyro_invert(void) {
    return sConfig.gyroInvert;
}

void pc_settings_get_gyro_bias(float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = sConfig.gyroBias[i];
}

void pc_settings_set_gyro_bias(const float bias[3]) {
    // Both copies: calibration runs from the menu, which saves the pending one.
    for (int i = 0; i < 3; i++) {
        sConfig.gyroBias[i]  = bias[i];
        sPending.gyroBias[i] = bias[i];
    }
}

int pc_settings_get_lock_on(void) { if (pc_speedrun_active()) return 0;
    return sConfig.lockOn;
}

int pc_settings_get_throw_while_moving(void) { if (pc_speedrun_active()) return 0;
    return sConfig.throwWhileMoving;
}

int pc_settings_get_first_person(void) { if (pc_speedrun_active()) return 0;
    return sConfig.firstPerson;
}

// La fila de Mods habilita el modo; la tecla entra y sale de él en marcha. Se
// apaga sola al desactivar el mod, para no dejar la cámara dentro de Olimar.
// Coop/VS: cada jugador tiene la suya.
static int sFirstPersonActive[2] = { 0, 0 };

int pc_first_person_active_for(int player) {
    if (!sConfig.firstPerson) sFirstPersonActive[0] = sFirstPersonActive[1] = 0;
    return player == 1 ? sFirstPersonActive[1] : sFirstPersonActive[0];
}

void pc_first_person_toggle_for(int player) {
    if (!sConfig.firstPerson) return;
    int& fp = sFirstPersonActive[player == 1 ? 1 : 0];
    fp = !fp;
}

int pc_first_person_active(void) { return pc_first_person_active_for(0); }
void pc_first_person_toggle(void) { pc_first_person_toggle_for(0); }

int pc_settings_get_charge(void) { if (pc_speedrun_active()) return 0;
    // El charge no significa nada sin un objetivo fijado.
    return sConfig.lockOn ? sConfig.charge : 0;
}

float pc_mods_teki_damage(float damage) {
    const int pct = pc_settings_get_teki_health_pct();
    if (pct < 0) return 1.0e6f; // Insta Kill
    if (pct == 100 || pct == 0) return damage;
    return damage * 100.0f / (float)pct;
}

int pc_settings_get_hold_to_pluck(void) { if (pc_speedrun_active()) return 0;
    return sConfig.holdToPluck;
}

int pc_settings_get_mouse_wheel_action(void) { if (pc_speedrun_active()) return 1;
    return sConfig.mouseWheelAction;
}

int pc_settings_get_piki_limit(void) { if (pc_speedrun_active()) return 100;
    if (pc_hardmode_active() && sConfig.pikiLimit > PC_HARDMODE_PIKI_LIMIT)
        return PC_HARDMODE_PIKI_LIMIT;
    return sConfig.pikiLimit;
}

int pc_settings_get_day_minutes(void) { if (pc_speedrun_active()) return 0;
    if (pc_hardmode_active() && (sConfig.dayMinutes == 0 || sConfig.dayMinutes > PC_HARDMODE_DAY_MINUTES))
        return PC_HARDMODE_DAY_MINUTES;
    return sConfig.dayMinutes;
}

int pc_settings_get_coop_split(void) { return sConfig.coopSplit; }
int pc_settings_get_shadows(void) { return sConfig.shadows; }
int pc_settings_get_coop_merge_camera(void) {
    // VS: pantalla siempre partida; cada uno ve solo su lado.
    if (pc_vs_active()) return 0;
    return sConfig.coopMergeCamera;
}

int pcGameLanguageFromOs(unsigned char osLanguage) {
    static const int kGame[] = { 0, 2, 1, 3, 4 }; // en de fr es it -> en fr de es it
    return osLanguage < 5 ? kGame[osLanguage] : 0;
}

unsigned char pcOsLanguageFromGame(int gameLanguage) {
    static const unsigned char kOs[] = { 0, 2, 1, 3, 4 }; // en fr de es it -> en de fr es it
    return gameLanguage >= 0 && gameLanguage < 5 ? kOs[gameLanguage] : 0;
}

void pc_settings_store_language(unsigned char osLanguage) {
    if (osLanguage == pc_settings_get_language()) return;
    pc_settings_set_language(osLanguage);
    saveConfig();
}

int pc_settings_get_hd_model_enabled(int row) {
    return row < 0 || row >= 6 || !(sConfig.hdModelsDisabled & (1 << row));
}

int pc_settings_get_debug_keys(void) { if (pc_speedrun_active()) return 0;
    return sConfig.debugKeys;
}

// ---------------------------------------------------------------------------
// Modelo de filas para otras interfaces (pc_settings_rows.h)
// ---------------------------------------------------------------------------

const char* pc_settings_group_name(int group) {
    switch (group) {
    case PC_SET_GROUP_DISPLAY: return "Display";
    case PC_SET_GROUP_GRAPHICS: return "Graphics";
    case PC_SET_GROUP_CONTROLS: return "Controls";
    case PC_SET_GROUP_CAMERA: return "Camera";
    case PC_SET_GROUP_GAMEPLAY: return "Gameplay";
    case PC_SET_GROUP_CHEATS: return "Cheats";
    case PC_SET_GROUP_DATA: return "Data";
    case PC_SET_GROUP_ACHIEVEMENTS: return "Achievements";
    case PC_SET_PICKER_RESOLUTION: return "Resolution";
    case PC_SET_PICKER_TEXPACKS: return "Texture Packs";
    case PC_SET_PICKER_HDMODELS: return "HD Models";
    case PC_SET_PICKER_KEYBOARD: return "Keyboard";
    case PC_SET_PICKER_GAMEPAD: return "Gamepad";
    default: return "";
    }
}

const char* pc_settings_group_summary(int group) {
    switch (group) {
    case PC_SET_GROUP_DISPLAY: return "Window, resolution, frame rate";
    case PC_SET_GROUP_GRAPHICS: return "Effects, colour, texture packs";
    case PC_SET_GROUP_CONTROLS: return "Mouse, sticks, gyro, bindings";
    case PC_SET_GROUP_CAMERA: return "Free camera, first person, lock-on";
    case PC_SET_GROUP_GAMEPLAY: return "Pikmin behaviour, co-op";
    case PC_SET_GROUP_CHEATS: return "Day, health, Pikmin limit, whistle";
    case PC_SET_GROUP_DATA: return "Save transfer, reset settings";
    case PC_SET_GROUP_ACHIEVEMENTS: return "Unlocked achievements and how to get the rest";
    default: return "";
    }
}

namespace {
constexpr int kSaveDataRows = 3; // export, import, reset defaults
const char* kHdModelLabels[6] = { "Olimar HD", "Louie (Pikmin 2 zip)", "Louie HD (Pikmin 3 zip)", "Pikmin HD (red/yellow/blue)", "Bulborb HD", "Dwarf Bulborb HD" };

void graphicsRowValue(int i, char* value, size_t n) {
    const bool gradingOn = sPending.colourGrading != 0;
    const char* levels[4] = { "Off", "Subtle", "Normal", "Strong" };
    auto level = [&](int v) { return levels[(v >= 0 && v <= 3) ? v : 0]; };
    switch (i) {
    case 0: snprintf(value, n, "%s", sPending.antialiasing ? "FXAA" : "Off"); break;
    case 1: snprintf(value, n, "%s", sPending.fog ? "On  (original)" : "Off"); break;
    case 2: snprintf(value, n, "%s", level(sPending.bloom)); break;
    case 3: snprintf(value, n, "%s", level(sPending.ssao)); break;
    case 4: snprintf(value, n, "%s", level(sPending.dof)); break;
    case 5:
        if (sPending.anisotropy <= 1) snprintf(value, n, "Trilinear");
        else snprintf(value, n, "Anisotropic %dx", sPending.anisotropy);
        break;
    case 6: snprintf(value, n, "%s", gradingOn ? "On" : "Off"); break;
    case 7: if (!gradingOn) snprintf(value, n, "--"); else snprintf(value, n, sPending.gamma == 1.0f ? "%.2f  (neutral)" : "%.2f", sPending.gamma); break;
    case 8: if (!gradingOn) snprintf(value, n, "--"); else snprintf(value, n, sPending.brightness == 0.0f ? "%+.2f  (neutral)" : "%+.2f", sPending.brightness); break;
    case 9: if (!gradingOn) snprintf(value, n, "--"); else snprintf(value, n, sPending.saturation == 1.0f ? "%.2f  (neutral)" : "%.2f", sPending.saturation); break;
    case 10: {
        std::vector<std::string> packs = pc_texpack_list_packs();
        if (sConfig.texturePackEnabled && !sConfig.texturePack.empty()) snprintf(value, n, "%s  >", sConfig.texturePack.c_str());
        else snprintf(value, n, "%d installed  >", (int)packs.size());
        break;
    }
    case 11: {
        std::error_code ec;
        int installed = 0;
        for (int id = 0; id < PC_HD_MODEL_COUNT; id++)
            if (std::filesystem::is_regular_file(pc_hd_model_path((PcHdModelId)id), ec)) installed++;
        snprintf(value, n, "%d / %d files  >", installed, (int)PC_HD_MODEL_COUNT);
        break;
    }
    case 12: snprintf(value, n, "%s", sPending.perPixelLighting ? "Per-pixel" : "Per-vertex (original)"); break;
    case 13: {
        const char* names[4] = { "Off (original)", "Soft", "Normal", "Strong" };
        snprintf(value, n, "%s", names[(sPending.shadows >= 0 && sPending.shadows <= 3) ? sPending.shadows : 0]);
        break;
    }

    default: value[0] = '\0';
    }
}

void modsRowValue(int i, char* value, size_t n) {
    switch (i) {
    case 0: snprintf(value, n, "%s", sPending.controlMode == PC_CONTROL_CLASSIC ? "Classic (original)" : "Mouse Cursor"); break;
    case 1: snprintf(value, n, "%s", sPending.chainActions ? "On" : "Off (original)"); break;
    case 2: snprintf(value, n, "%s", sPending.holdToPluck ? "On" : "Off (original)"); break;
    case 3: snprintf(value, n, "%s", sPending.mouseWheelAction ? "Camera Zoom" : "Pikmin Colour"); break;
    case 4:
        if (pc_hardmode_active()) snprintf(value, n, "%d (Hard)", PC_HARDMODE_PIKI_LIMIT);
        else if (sPending.pikiLimit == 100) snprintf(value, n, "100 (original)");
        else if (sPending.pikiLimit > 200) snprintf(value, n, "%d  (may cost performance)", sPending.pikiLimit);
        else snprintf(value, n, "%d", sPending.pikiLimit);
        break;
    case 5:
        if (pc_hardmode_active()) snprintf(value, n, "%d min (Hard)", PC_HARDMODE_DAY_MINUTES);
        else if (sPending.infiniteDay) snprintf(value, n, "Infinite");
        else if (sPending.dayMinutes == 0) snprintf(value, n, "13.5 min (original)");
        else snprintf(value, n, "%d min", sPending.dayMinutes);
        break;
    case 6: snprintf(value, n, "%s", sPending.coopSplit ? "Horizontal (top/bottom)" : "Vertical (left/right)"); break;
    case 7: snprintf(value, n, "%s", sPending.coopMergeCamera ? "On (dynamic)" : "Off (static split)"); break;
    case 8: snprintf(value, n, "%s", sPending.betterPathfinding ? "On" : "Off (original)"); break;
    case 9: snprintf(value, n, "%s", sPending.bluesOnlyWater ? "On" : "Off (original)"); break;
    case 10: snprintf(value, n, "%s", sPending.idleCounter ? "On" : "Off (original)"); break;
    case 11: healthPctLabel(sPending.naviHealthPct, "Infinite", value, n); break;
    case 12: healthPctLabel(sPending.tekiHealthPct, "Insta Kill", value, n); break;
    case 13:
        if (pc_hardmode_active()) snprintf(value, n, "Off (Hard)");
        else snprintf(value, n, "%s", sPending.infiniteDay ? "On" : "Off (original)");
        break;
    case 14: snprintf(value, n, "%s", sPending.freeCamera ? "On" : "Off (original)"); break;
    case 15: snprintf(value, n, "%s", sPending.lockOn == 2 ? "Automatic" : sPending.lockOn == 1 ? "Manual" : "Off (original)"); break;
    case 16:
        if (!sPending.lockOn) snprintf(value, n, "Needs Lock-On");
        else snprintf(value, n, "%s", sPending.charge ? "On" : "Off (original)");
        break;
    case 17: snprintf(value, n, "%s", sPending.throwWhileMoving ? "On" : "Off (original)"); break;
    case 18: snprintf(value, n, "%s", sPending.firstPerson ? "On" : "Off (original)"); break;
    case 19: snprintf(value, n, "%s", sPending.debugKeys ? "On" : "Off"); break;
    case 20: snprintf(value, n, sPending.whistleRadiusPct == 100 ? "%d%%  (original)" : "%d%%", sPending.whistleRadiusPct); break;
    case 21: snprintf(value, n, sPending.throwSpeedPct == 100 ? "%d%%  (original)" : "%d%%", sPending.throwSpeedPct); break;
    case 22: snprintf(value, n, "%s", sPending.throwCancelB ? "On" : "Off (original)"); break;
    case 33: snprintf(value, n, "%s", sPending.quickGrab ? "On" : "Off (original)"); break;
    case 23: snprintf(value, n, "%s", sPending.noTrip ? "On" : "Off (original)"); break;
    case 24: snprintf(value, n, "%s", sPending.onionStep10 ? "On" : "Off (original)"); break;
    case 25: snprintf(value, n, "%s", sPending.instantWhistle ? "On" : "Off (original)"); break;
    case 34: snprintf(value, n, "%s", sPending.whistlePluck ? "On" : "Off (original)"); break;
    case 35: snprintf(value, n, "%s", sPending.bombControl ? "On" : "Off (original)"); break;
    case 36: snprintf(value, n, "%s", sPending.hideOlimarText ? "On" : "Off (original)"); break;
    case 37:
        if (pc_hardmode_active()) snprintf(value, n, "Off (Hard)");
        else snprintf(value, n, "%s", sPending.breakableGates ? "On" : "Off (original)");
        break;
    case 39: snprintf(value, n, "%s", sPending.p2Selection ? "On" : "Off"); break;
    case 40: snprintf(value, n, "%s", sPending.eternalNight ? "On" : "Off (original)"); break;
    case 38: snprintf(value, n, sPending.freeCamPadPct == 100 ? "%d%%  (original)" : "%d%%", sPending.freeCamPadPct); break;
    case 28: speedPctLabel(sPending.carrySpeedPct, value, n); break;
    case 29: speedPctLabel(sPending.naviSpeedPct, value, n); break;
    case 26: case 27: case 30: case 31: case 32: {
        const int on = i == 26 ? sPending.pikiInvincible : i == 27 ? sPending.allFlowers
                     : i == 30 ? sPending.unlockZones : i == 31 ? sPending.noDayAdvance : sPending.allOnions;
        if (pc_hardmode_active()) snprintf(value, n, "Off (Hard)");
        else snprintf(value, n, "%s", on ? "On" : "Off (original)");
        break;
    }
    default: value[0] = '\0';
    }
}

void advancedRowValue(int i, char* value, size_t n) {
    switch (i) {
    case 0: snprintf(value, n, "%.2f", sPending.mouseSensitivity); break;
    case 1: snprintf(value, n, "%d", sPending.stickDeadZone); break;
    case 2: snprintf(value, n, "%s / %s", (sPending.stickInvert & 1) ? "InvX" : "NorX", (sPending.stickInvert & 2) ? "InvY" : "NorY"); break;
    case 3: snprintf(value, n, "%s / %s", (sPending.cStickInvert & 1) ? "InvX" : "NorX", (sPending.cStickInvert & 2) ? "InvY" : "NorY"); break;
    case 4: snprintf(value, n, "%s", !sPending.gyroEnabled ? "Off" : (pc_gyro_available() ? "On" : "On (no sensor)")); break;
    case 5: snprintf(value, n, "%.2f", sPending.gyroSensitivity); break;
    case 6: snprintf(value, n, "%s / %s", (sPending.gyroInvert & 1) ? "InvX" : "NorX", (sPending.gyroInvert & 2) ? "InvY" : "NorY"); break;
    case 7: {
        const float left = pc_gyro_calibration_seconds_left();
        if (left > 0.0f) snprintf(value, n, "Hold still... %.1f", left);
        else if (!pc_gyro_available()) snprintf(value, n, "No sensor");
        else snprintf(value, n, "Press A (keep still)");
        break;
    }
    default: value[0] = '\0';
    }
}

bool hdModelInstalled(int row) {
    std::error_code ec;
    auto is = [&](PcHdModelId id) { return std::filesystem::is_regular_file(pc_hd_model_path(id), ec); };
    switch (row) {
    case 0: return is(PC_HD_MODEL_OLIMAR);
    case 1: return is(PC_HD_MODEL_LOUIE);
    case 2: return is(PC_HD_MODEL_LOUIE_HD);
    case 3: return is(PC_HD_MODEL_PIKI_RED) && is(PC_HD_MODEL_PIKI_YELLOW) && is(PC_HD_MODEL_PIKI_BLUE);
    case 4: return is(PC_HD_MODEL_BULBORB);
    case 5: return is(PC_HD_MODEL_BULBORB_DWARF);
    default: return false;
    }
}
} // namespace

namespace {
// Resoluciones elegibles para el modo de vídeo pendiente (mismo criterio que
// el submenú del overlay F1).
std::vector<int> pickerResolutionChoices() {
    std::vector<int> out;
    for (size_t i = 0; i < sResolutions.size(); i++)
        if (resolutionSelectable(sResolutions[i], sPending.displayMode)) out.push_back((int)i);
    return out;
}
}

namespace {
// ---------------------------------------------------------------------------
// Grupos del menú. Cada fila apunta a la lógica que ya existía (fila de la
// página de vídeo, de Advanced, de Graphics, de Mods...), así que reordenar
// aquí no toca qué hace cada ajuste ni cómo se guarda: el .conf va por nombre.
// ---------------------------------------------------------------------------
enum RowSrc { SRC_MAIN, SRC_ADV, SRC_GFX, SRC_MODS, SRC_DATA, SRC_KEYS, SRC_PADS, SRC_RECENTER, SRC_ACH };
struct GroupRow {
    RowSrc src;
    int idx;
    const char* label;
    const char* help;
};

const GroupRow kDisplayRows[] = {
    { SRC_MAIN, ROW_DISPLAY_MODE, "Display Mode", "Windowed, exclusive fullscreen, or a borderless window at desktop size." },
    { SRC_MAIN, ROW_RESOLUTION, "Resolution", "Window or fullscreen size. A opens the full list; Left/Right steps through it." },
    { SRC_MAIN, ROW_ASPECT_RATIO, "Aspect Ratio", "Auto fills the window. The others keep that shape and add bars." },
    { SRC_MAIN, ROW_RENDER_SCALE, "3D Resolution", "Detail of the 3D scene. Higher is sharper, lower is faster." },
    { SRC_MAIN, ROW_REFRESH_RATE, "Refresh Rate", "Fullscreen refresh rate. Auto keeps the monitor's current rate." },
    { SRC_MAIN, ROW_VSYNC, "Frame Sync (VSync)", "Waits for the monitor between frames. Stops tearing, adds a little lag." },
    { SRC_MAIN, ROW_FPS_MODE, "FPS Mode", "30 is the original and the most stable. 60 and 120 are smoother but experimental." },
#if defined(VERSION_GPIP01)
    { SRC_MAIN, ROW_LANGUAGE, "Language", "Language of the game's text. Takes effect after a restart." },
#endif
};

const GroupRow kGraphicsRows[] = {
    { SRC_GFX, 0, "Antialiasing", "Smooths jagged edges (FXAA). Small performance cost." },
    { SRC_GFX, 5, "Texture Filtering", "Keeps textures sharp at steep angles. Anisotropic costs a little GPU." },
    { SRC_GFX, 12, "Lighting", "Per-pixel gives smoother light on models than the original per-vertex." },
    { SRC_GFX, 13, "Shadows", "Real-time shadows cast by characters and objects." },
    { SRC_GFX, 1, "Fog", "The game's own distance fog. On is how the original looks." },
    { SRC_GFX, 2, "Bloom", "Soft glow around bright areas." },
    { SRC_GFX, 3, "Ambient Occlusion", "Contact shadows in corners and under objects." },
    { SRC_GFX, 4, "Depth of Field", "Blurs the far distance so the action stands out." },
    { SRC_GFX, 6, "Colour Grading", "Turns on the Gamma, Brightness and Saturation controls below." },
    { SRC_GFX, 7, "Gamma", "Brightness of the mid-tones. 1.00 is neutral." },
    { SRC_GFX, 8, "Brightness", "Lifts or darkens the whole image. 0.00 is neutral." },
    { SRC_GFX, 9, "Saturation", "Colour intensity. 1.00 is neutral, 0.00 is black and white." },
    { SRC_GFX, 10, "Texture Packs", "Install or switch replacement texture packs. Needs a restart." },
    { SRC_GFX, 11, "HD Models", "Install HD character models from their zips. Needs a restart." },
};

const GroupRow kControlsRows[] = {
    { SRC_MODS, 0, "Control Scheme", "Classic: the stick moves the cursor, as on GameCube. Mouse Cursor: aim with the mouse." },
    { SRC_ADV, 0, "Mouse Sensitivity", "How far the cursor moves for each movement of the mouse." },
    { SRC_MODS, 3, "Mouse Wheel", "What the wheel does: change the Pikmin colour to throw, or zoom the camera." },
    { SRC_MODS, 2, "Hold to Pluck", "Keep the button held to pluck sprouts one after another." },
    { SRC_MODS, 34, "Whistle Pluck", "Hold the whistle over sprouts to pluck them one after another." },
    { SRC_MODS, 17, "Throw While Moving", "Throw Pikmin while running, instead of Olimar stopping first." },
    { SRC_MODS, 22, "Cancel Throw With B", "While holding a Pikmin with A, press B to put it back in the squad." },
    { SRC_MODS, 35, "Bomb Control", "Bomb button (B / assign on a pad): a Yellow with a bomb rock throws it at the cursor, or drops it lit at its feet if the cursor is too close. Ones already thrown go first." },
    { SRC_MODS, 39, "Pikmin 2 Selection", "D-pad as in Pikmin 2: Left/Right keeps the chosen colour for every throw, and with A held Up/Down picks leaf, bud, flower or a Yellow with a bomb rock. Off: Left/Right only picks the next throw." },
    { SRC_MODS, 33, "Quick Grab", "The Pikmin to throw appears in Olimar's hand at once, so throwing is just as fast with the squad behind him." },
    { SRC_MODS, 24, "Onion: Y for Steps of 10", "In the Onion menu, hold Y while moving up or down to move 10 Pikmin at a time." },
    { SRC_ADV, 1, "Stick Dead Zone", "Ignores small stick movements. Raise it if a worn stick drifts." },
    { SRC_ADV, 2, "Stick Invert (X/Y)", "Inverts the movement stick." },
    { SRC_ADV, 3, "C-Stick Invert (X/Y)", "Inverts the right stick (C-Stick)." },
    { SRC_ADV, 4, "Gyro Aiming", "Aim the cursor by turning a gyro pad or the phone. In first person it looks around." },
    { SRC_ADV, 5, "Gyro Sensitivity", "How far the cursor moves when you turn the pad." },
    { SRC_ADV, 6, "Gyro Invert (X/Y)", "Inverts gyro aiming: none, horizontal, vertical or both." },
    { SRC_ADV, 7, "Gyro Calibrate", "Put the pad or phone down, keep it still and press A. Fixes a drifting cursor." },
    { SRC_RECENTER, 0, "Gyro Recenter Button", "Button that brings the cursor back in front of Olimar. A: assign it." },
    { SRC_KEYS, 0, "Keyboard Bindings", "Two keys or mouse buttons per action. A: main key, Right: second key, Left: defaults. While waiting, Del clears the slot. Shift, Ctrl and Alt can be bound." },
    { SRC_PADS, 0, "Gamepad Bindings", "Choose the pad button for each action." },
};

const GroupRow kCameraRows[] = {
    { SRC_MODS, 14, "Free Camera", "Turn the camera as in Pikmin 3: hold Left Shift and move the mouse, or use the right stick on a controller. Swarm gets its own button." },
    { SRC_MODS, 38, "Free Camera Pad Sensitivity", "How fast the right stick turns the free camera on a controller. 100% is the default." },
    { SRC_MODS, 18, "First Person", "Allows a view from Olimar's helmet. Switch in game with its button (V / L3)." },
    { SRC_MODS, 15, "Lock-On", "Automatic: locks onto the nearest enemy or object as you approach. Manual: lock with the Lock-On button (bindable in Controls)." },
    { SRC_MODS, 16, "Charge", "With a target locked, send the whole squad at it." },
};

const GroupRow kGameplayRows[] = {
    { SRC_MODS, 25, "Instant Whistle Response", "Whistled Pikmin join the squad at once, without stopping to turn and look first." },
    { SRC_MODS, 1, "Chain Pikmin Actions", "Pikmin that finish a task go on to the next one nearby." },
    { SRC_MODS, 8, "Better Pathfinding", "Gets Pikmin moving again when they stall on their route." },
    { SRC_MODS, 9, "Blues Only In Water", "Only blue Pikmin walk into water on their own." },
    { SRC_MODS, 10, "Idle Pikmin Counter", "Shows how many Pikmin are standing idle." },
    { SRC_MODS, 36, "Hide Olimar's Texts", "Skip the text boxes Olimar shows while you play: first Pikmin, ship parts, tips. The ending texts stay." },
    { SRC_MODS, 23, "No Tripping", "Pikmin running in the squad never trip and fall behind." },
    { SRC_MODS, 6, "Co-op Split Screen", "How the screen divides in two-player co-op." },
    { SRC_MODS, 7, "Co-op Merged Camera", "Joins both halves into one view while the captains are close." },
};

// Infinite Day (fila 13) vive dentro de Day Length como su última opción.
const GroupRow kCheatsRows[] = {
    { SRC_MODS, 5, "Day Length", "Minutes of daylight per day. 13.5 is the original; Infinite stops the sun." },
    { SRC_MODS, 40, "Eternal Night", "Always night, whatever the day length: night lighting, and the moon crosses the day bar instead of the sun." },
    { SRC_MODS, 11, "Olimar Health", "Olimar's toughness, as a share of the original. Infinite takes no damage." },
    { SRC_MODS, 12, "Enemy Health", "Enemy toughness, as a share of the original. Insta Kill drops them in one hit." },
    { SRC_MODS, 4, "Pikmin Limit", "Most Pikmin on the field at once. 100 is the original; more costs performance." },
    { SRC_MODS, 21, "Throw Speed", "Speed of Olimar's grab and throw, so how fast you can throw. 100% is the original." },
    { SRC_MODS, 20, "Whistle Radius", "Size of the whistle circle at full charge. 100% is the original." },
    { SRC_MODS, 26, "Invincible Pikmin", "Pikmin never die: no attacks, fire, water, gas or crushing." },
    { SRC_MODS, 27, "All Flowers", "Every Pikmin grows a flower as soon as it is plucked or born." },
    { SRC_MODS, 28, "Carry Speed", "How fast Pikmin carry pellets, parts and bodies." },
    { SRC_MODS, 29, "Olimar Speed", "How fast Olimar walks and runs." },
    { SRC_MODS, 30, "Unlock All Zones", "Opens every area on the map. Saved into your game." },
    { SRC_MODS, 31, "No Day Limit", "The day counter never advances, so the 30-day limit never comes." },
    { SRC_MODS, 32, "All Onions", "Red, Yellow and Blue Onions from the start. Saved into your game." },
    { SRC_MODS, 37, "Breakable Reinforced Gates", "Pikmin can break the black reinforced gates by hitting them, without bomb rocks." },
#if PIKI_DEBUG_KEYS
    { SRC_MODS, 19, "Debug Keys (F5-F8)",
      "F5: add up to 20 red leaf Pikmin to the red Onion (requires an Onion and respects the Pikmin limit).\n"
      "F6: advance the clock by one in-game hour, capped just before sunset (with Infinite Day it only moves the lighting).\n"
      "F7: grant the Main Engine, top red Onion stock up to 20, and trigger the normal end-of-day sequence.\n"
      "F8 (VS only): append both captains' XYZ coordinates to vs_positions.txt." },
#endif
};

const GroupRow kDataRows[] = {
#ifdef __ANDROID__
    { SRC_DATA, 0, "Export save to ZIP", "Copies your memory card to a ZIP file you choose." },
    { SRC_DATA, 1, "Import save from ZIP", "Replaces your memory card with one from a ZIP file." },
#else
    { SRC_DATA, 0, "Export save to ZIP", "Desktop saves are plain files in the 'save' folder. Copy it to back up." },
    { SRC_DATA, 1, "Import save from ZIP", "Desktop saves are plain files in the 'save' folder. Replace it to restore." },
#endif
    { SRC_DATA, 2, "Reset all settings to defaults", "Puts every setting back to its default. Saves are not touched." },
};

template <size_t N> constexpr int countOf(const GroupRow (&)[N]) { return (int)N; }

// Pestaña de logros: una fila por logro, en orden de lectura (piezas, colores,
// historia, desafío). idx = PcAchievement.
const int kAchOrder[PC_ACH_COUNT] = {
    PC_ACH_PART_MAIN_ENGINE, PC_ACH_PART_POSITRON_GENERATOR, PC_ACH_PART_ETERNAL_FUEL_DYNAMO,
    PC_ACH_PART_WHIMSICAL_RADAR, PC_ACH_PART_EXTRAORDINARY_BOLT, PC_ACH_PART_NOVA_BLASTER, PC_ACH_PART_SHOCK_ABSORBER,
    PC_ACH_PART_RADIATION_CANOPY, PC_ACH_PART_SAGITTARIUS, PC_ACH_PART_GEIGER_COUNTER, PC_ACH_PART_SPACE_FLOAT,
    PC_ACH_PART_IONIUM_JET_1, PC_ACH_PART_AUTOMATIC_GEAR, PC_ACH_PART_OMEGA_STABILIZER, PC_ACH_PART_LIBRA,
    PC_ACH_PART_GRAVITY_JUMPER, PC_ACH_PART_ANALOG_COMPUTER, PC_ACH_PART_ANTI_DIOXIN_FILTER, PC_ACH_PART_GUARD_SATELLITE,
    PC_ACH_PART_IONIUM_JET_2, PC_ACH_PART_INTERSTELLAR_RADIO, PC_ACH_PART_CHRONOS_REACTOR, PC_ACH_PART_PILOT_SEAT,
    PC_ACH_PART_ZIRCONIUM_ROTOR, PC_ACH_PART_REPAIR_TYPE_BOLT, PC_ACH_PART_MASSAGE_MACHINE, PC_ACH_PART_UV_LAMP,
    PC_ACH_PART_BOWSPRIT, PC_ACH_PART_GLUON_DRIVE, PC_ACH_PART_SECRET_SAFE,
    PC_ACH_PIKMIN_RED, PC_ACH_PIKMIN_YELLOW, PC_ACH_PIKMIN_BLUE,
    PC_ACH_GOOLIX, PC_ACH_ALLERGIC_TO_BLUE, PC_ACH_BAD_ENDING, PC_ACH_NORMAL_ENDING, PC_ACH_BEST_ENDING, PC_ACH_SPEED_DEMON,
    PC_ACH_CHALLENGE_IMPACT_SITE, PC_ACH_CHALLENGE_FOREST_OF_HOPE, PC_ACH_CHALLENGE_FOREST_NAVEL,
    PC_ACH_CHALLENGE_DISTANT_SPRING, PC_ACH_CHALLENGE_FINAL_TRIAL,
};

const GroupRow* achievementRows() {
    static GroupRow rows[PC_ACH_COUNT];
    static bool built = false;
    if (!built) {
        built = true;
        for (int i = 0; i < PC_ACH_COUNT; i++) {
            const PcAchievementInfo& info = pc_achievement_info(kAchOrder[i]);
            rows[i] = { SRC_ACH, kAchOrder[i], info.title, info.description };
        }
    }
    return rows;
}

int achievementAtRow(int row) { return row >= 0 && row < PC_ACH_COUNT ? kAchOrder[row] : -1; }

const GroupRow* groupRows(int group, int* count) {
    switch (group) {
    case PC_SET_GROUP_ACHIEVEMENTS: *count = PC_ACH_COUNT; return achievementRows();
    case PC_SET_GROUP_DISPLAY: *count = countOf(kDisplayRows); return kDisplayRows;
    case PC_SET_GROUP_GRAPHICS: *count = countOf(kGraphicsRows); return kGraphicsRows;
    case PC_SET_GROUP_CONTROLS: *count = countOf(kControlsRows); return kControlsRows;
    case PC_SET_GROUP_CAMERA: *count = countOf(kCameraRows); return kCameraRows;
    case PC_SET_GROUP_GAMEPLAY: *count = countOf(kGameplayRows); return kGameplayRows;
    case PC_SET_GROUP_CHEATS: *count = countOf(kCheatsRows); return kCheatsRows;
    case PC_SET_GROUP_DATA: *count = countOf(kDataRows); return kDataRows;
    default: *count = 0; return nullptr;
    }
}

const GroupRow* groupRow(int group, int row) {
    int n = 0;
    const GroupRow* rows = groupRows(group, &n);
    return (rows && row >= 0 && row < n) ? &rows[row] : nullptr;
}

// Cabeceras de sección de la página F1: fila en la que empieza cada una.
struct GroupSection {
    int group, row;
    const char* title;
};

const GroupSection kSections[] = {
    { PC_SET_GROUP_ACHIEVEMENTS, 0, "SHIP PARTS" },
    { PC_SET_GROUP_ACHIEVEMENTS, 30, "PIKMIN" },
    { PC_SET_GROUP_ACHIEVEMENTS, 33, "STORY" },
    { PC_SET_GROUP_ACHIEVEMENTS, 39, "CHALLENGE MODE" },
    { PC_SET_GROUP_DISPLAY, 0, "DISPLAY" },
    { PC_SET_GROUP_DISPLAY, 3, "RENDERING" },
#if defined(VERSION_GPIP01)
    { PC_SET_GROUP_DISPLAY, 7, "LANGUAGE" },
#endif
    { PC_SET_GROUP_GRAPHICS, 0, "IMAGE" },
    { PC_SET_GROUP_GRAPHICS, 4, "POST-PROCESSING" },
    { PC_SET_GROUP_GRAPHICS, 8, "COLOUR" },
    { PC_SET_GROUP_GRAPHICS, 12, "CONTENT" },
    { PC_SET_GROUP_CONTROLS, 0, "SCHEME" },
    { PC_SET_GROUP_CONTROLS, 3, "ACTIONS" },
    { PC_SET_GROUP_CONTROLS, 7, "STICKS" },
    { PC_SET_GROUP_CONTROLS, 10, "GYRO" },
    { PC_SET_GROUP_CONTROLS, 15, "BINDINGS" },
    { PC_SET_GROUP_CAMERA, 0, "CAMERA" },
    { PC_SET_GROUP_CAMERA, 2, "TARGETING" },
    { PC_SET_GROUP_GAMEPLAY, 0, "PIKMIN" },
    { PC_SET_GROUP_GAMEPLAY, 6, "CO-OP" },
    { PC_SET_GROUP_CHEATS, 0, "DAY & HEALTH" },
    { PC_SET_GROUP_CHEATS, 3, "PIKMIN" },
    { PC_SET_GROUP_CHEATS, 9, "OLIMAR" },
    { PC_SET_GROUP_CHEATS, 10, "PROGRESS" },
#if PIKI_DEBUG_KEYS
    { PC_SET_GROUP_CHEATS, 13, "DEBUG" },
#endif
    { PC_SET_GROUP_DATA, 0, "SAVE FILE" },
    { PC_SET_GROUP_DATA, 2, "SETTINGS" },
};

const char* rowSection(int group, int row) {
    for (const GroupSection& s : kSections)
        if (s.group == group && s.row == row) return s.title;
    return nullptr;
}

bool rowIsResolution(int group, int row) {
    const GroupRow* r = groupRow(group, row);
    return r && r->src == SRC_MAIN && r->idx == ROW_RESOLUTION;
}

// Filas cuyas opciones se pueden listar recorriéndolas en sondeo: las de
// valor cuyo cambio solo toca sPending (el vídeo y el render se saltan con
// sProbing). Quedan fuera acciones, selectores, resolución e idioma.
bool rowProbeable(int group, int row) {
    const GroupRow* r = groupRow(group, row);
    if (!r) return false;
    switch (r->src) {
    case SRC_MAIN:
        return r->idx == ROW_DISPLAY_MODE || r->idx == ROW_ASPECT_RATIO || r->idx == ROW_RENDER_SCALE
            || r->idx == ROW_REFRESH_RATE || r->idx == ROW_VSYNC || r->idx == ROW_FPS_MODE
#if defined(VERSION_GPIP01)
            // El idioma se guarda aparte de sPending (sLanguage): el sondeo lo
            // restaura por su cuenta. Solo sin ventana, para el launcher.
            || (sHeadless && r->idx == ROW_LANGUAGE)
#endif
            ;
    case SRC_ADV: return r->idx != 7;
    case SRC_GFX: return r->idx != 10 && r->idx != 11;
    case SRC_MODS: return true;
    default: return false;
    }
}

bool hardLocked(const GroupRow& r) {
    return r.src == SRC_MODS && pc_hardmode_active()
        && (r.idx == 4 || r.idx == 5 || r.idx == 11 || r.idx == 12 || r.idx == 13);
}

// Motivo por el que una fila no se puede cambiar ahora, o nullptr si se puede.
const char* disabledReason(const GroupRow& r) {
    if (hardLocked(r)) return "Locked by Hard mode for this save file.";
    switch (r.src) {
    case SRC_MAIN:
        if (r.idx == ROW_RESOLUTION && sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS)
            return "Borderless always uses the desktop resolution.";
        break;
    case SRC_ADV:
        if (r.idx == 0 && sPending.controlMode == PC_CONTROL_CLASSIC)
            return "Only used with the Mouse Cursor control scheme.";
        if ((r.idx == 5 || r.idx == 6) && !sPending.gyroEnabled) return "Turn on Gyro Aiming first.";
        if (r.idx == 7 && !pc_gyro_available()) return "No gyro found. Connect a pad with a gyro.";
        break;
    case SRC_RECENTER:
        if (!sPending.gyroEnabled) return "Turn on Gyro Aiming first.";
        break;
    case SRC_GFX:
        if (r.idx >= 7 && r.idx <= 9 && !sPending.colourGrading) return "Turn on Colour Grading first.";
        break;
    case SRC_MODS:
        if (r.idx == 16 && !sPending.lockOn) return "Turn on Lock-On first.";
        break;
    default:
        break;
    }
    return nullptr;
}

int sPickerFocusRow = 0; // fila en la que abre el selector de teclas/botones

void keyBindingPairName(int action, char* out, size_t n) {
    const int second = sPending.keyboardBindings2[action];
    if (second == SDL_SCANCODE_UNKNOWN) {
        snprintf(out, n, "%s", pc_window_binding_name(sPending.keyboardBindings[action]));
    } else {
        snprintf(out, n, "%s / %s", pc_window_binding_name(sPending.keyboardBindings[action]),
                 pc_window_binding_name(second));
    }
}

void startKeyCapture(int action, bool second) {
    sControlSelection = action;
    sCaptureSecond = second;
    sWaitingForKey = true;
    sCapturePrevMouse = SDL_GetMouseState(NULL, NULL);
    pc_window_take_mouse_pressed(); // descartar clics de antes de la captura
    sCaptureWaitRelease = true;
}

void startButtonCapture(int action) {
    sGamepadSelection = action;
    sWaitingForButton = true;
    sCaptureWaitRelease = true;
}

void gamepadBindingName(int action, char* out, size_t n) {
    int bound = pendingPadBinds()[action];
    if (bound < 0) bound = kDefaultGamepadBindings[action];
    const char* name = bound >= 0 ? pc_window_get_gamepad_button_name(bound) : nullptr;
    snprintf(out, n, "%s", name ? name : "None");
}
} // namespace

const char* pc_settings_row_section(int group, int row) { return rowSection(group, row); }

int pc_settings_row_options(int group, int row, int* current) {
    int at = -1;
    const int n = (int)f1Options(group, row, &at).size();
    if (current) *current = at;
    return at >= 0 ? n : 0;
}

const char* pc_settings_row_option(int index) {
    return index >= 0 && index < (int)sF1Opts.values.size() ? sF1Opts.values[index].c_str() : "";
}

void pc_settings_row_pick_option(int group, int row, int index) { f1PickOption(group, row, index); }

int pc_settings_rows_count(int group) {
    int n = 0;
    if (groupRows(group, &n)) return n;
    switch (group) {
    case PC_SET_PICKER_RESOLUTION: return (int)pickerResolutionChoices().size();
    case PC_SET_PICKER_TEXPACKS: return 1 + (int)pc_texpack_list_packs().size();
    case PC_SET_PICKER_HDMODELS: return 6;
    case PC_SET_PICKER_KEYBOARD:
    case PC_SET_PICKER_GAMEPAD: return PC_KEY_ACT_COUNT;
    default: return 0;
    }
}

bool pc_settings_row_enabled(int group, int row) {
    const GroupRow* r = groupRow(group, row);
    if (r && r->src == SRC_ACH) return pc_achievement_unlocked(r->idx); // bloqueados en gris
    return !r || !disabledReason(*r);
}

bool pc_settings_row_is_action(int group, int row) {
    if (group == PC_SET_PICKER_TEXPACKS || group == PC_SET_PICKER_HDMODELS) return true;
    const GroupRow* r = groupRow(group, row);
    return r && (r->src == SRC_DATA || (r->src == SRC_ADV && r->idx == 7));
}

const char* pc_settings_row_help(int group, int row) {
    if (const GroupRow* r = groupRow(group, row); r && r->src == SRC_ACH) {
        static char help[320];
        const PcAchievementInfo& info = pc_achievement_info(r->idx);
        const char* blocked = pc_achievements_blocked_reason();
        snprintf(help, sizeof(help), "%s\n\n%s  %d pts.  Total: %d / %d  (%d / %d pts)%s%s", info.description,
                 pc_achievement_unlocked(r->idx) ? "Unlocked." : "Locked.", info.points, pc_achievements_unlocked_count(),
                 (int)PC_ACH_COUNT, pc_achievements_points(), pc_achievements_total_points(), blocked ? "\n\n" : "",
                 blocked ? blocked : "");
        return help;
    }
    if (const GroupRow* r = groupRow(group, row)) {
        const char* reason = disabledReason(*r);
        return reason ? reason : r->help;
    }
    switch (group) {
    case PC_SET_PICKER_RESOLUTION: return "A: use this size. You then get a few seconds to keep or revert it.";
    case PC_SET_PICKER_TEXPACKS: return "A: install, activate or deactivate. Changes need a restart.";
    case PC_SET_PICKER_HDMODELS: return "A: pick the model's zip to install it. Needs a restart.";
    case PC_SET_PICKER_KEYBOARD: return "A: press the new key or mouse button. Left/Right: back to default.";
    case PC_SET_PICKER_GAMEPAD: return "A: press the new button. Left/Right: back to default.";
    default: return "";
    }
}

int pc_settings_row_opens_picker(int group, int row) {
    const GroupRow* r = groupRow(group, row);
    if (!r || disabledReason(*r)) return 0;
    switch (r->src) {
    case SRC_MAIN: return r->idx == ROW_RESOLUTION ? PC_SET_PICKER_RESOLUTION : 0;
    case SRC_GFX:
        if (r->idx == 10) return PC_SET_PICKER_TEXPACKS;
        if (r->idx == 11) return PC_SET_PICKER_HDMODELS;
        return 0;
    // Consultar la fila fija también dónde abrirá el selector: la del botón
    // de recentrado abre la lista del mando ya encima de esa acción.
    case SRC_KEYS: sPickerFocusRow = 0; return PC_SET_PICKER_KEYBOARD;
    case SRC_PADS: sPickerFocusRow = 0; return PC_SET_PICKER_GAMEPAD;
    case SRC_RECENTER: sPickerFocusRow = PC_KEY_ACT_GYRO_RECENTER; return PC_SET_PICKER_GAMEPAD;
    default: return 0;
    }
}

int pc_settings_picker_current(int picker) {
    if (picker == PC_SET_PICKER_RESOLUTION) {
        std::vector<int> c = pickerResolutionChoices();
        for (size_t k = 0; k < c.size(); k++)
            if (sResolutions[c[k]].w == sPending.windowWidth && sResolutions[c[k]].h == sPending.windowHeight) return (int)k;
    }
    if (picker == PC_SET_PICKER_KEYBOARD || picker == PC_SET_PICKER_GAMEPAD) return sPickerFocusRow;
    return 0;
}

const char* pc_settings_row_label(int group, int row) {
    static char label[96];
    if (const GroupRow* r = groupRow(group, row)) return r->label;
    if (group == PC_SET_PICKER_KEYBOARD || group == PC_SET_PICKER_GAMEPAD)
        return (row >= 0 && row < PC_KEY_ACT_COUNT) ? pc_window_get_key_action_name(row) : "";
    if (group == PC_SET_PICKER_TEXPACKS) {
        if (row == 0) return "Install pack...";
        std::vector<std::string> packs = pc_texpack_list_packs();
        if (row - 1 < 0 || row - 1 >= (int)packs.size()) return "";
        snprintf(label, sizeof(label), "%s", packs[row - 1].c_str());
        return label;
    }
    if (group == PC_SET_PICKER_HDMODELS) return (row >= 0 && row < 6) ? kHdModelLabels[row] : "";
    if (group == PC_SET_PICKER_RESOLUTION) {
        std::vector<int> c = pickerResolutionChoices();
        if (row < 0 || row >= (int)c.size()) return "";
        snprintf(label, sizeof(label), "%dx%d", sResolutions[c[row]].w, sResolutions[c[row]].h);
        return label;
    }
    return "";
}

void pc_settings_row_value(int group, int row, char* out, unsigned long n) {
    if (!out || !n) return;
    out[0] = '\0';
    if (const GroupRow* r = groupRow(group, row)) {
        switch (r->src) {
        case SRC_MAIN: mainRowValue(r->idx, out, (size_t)n); break;
        case SRC_ADV: advancedRowValue(r->idx, out, (size_t)n); break;
        case SRC_GFX: graphicsRowValue(r->idx, out, (size_t)n); break;
        case SRC_MODS: modsRowValue(r->idx, out, (size_t)n); break;
        case SRC_DATA:
#ifdef __ANDROID__
            if (r->idx < 2) snprintf(out, n, "%s", sSaveTransferActive ? "Opening picker..." : "A: choose file");
#else
            if (r->idx < 2) snprintf(out, n, "save / card0, card1");
#endif
            else snprintf(out, n, "A: reset");
            break;
        case SRC_ACH: snprintf(out, n, "%d pts", pc_achievement_info(r->idx).points); break;
        case SRC_KEYS:
        case SRC_PADS: snprintf(out, n, "Open  >"); break;
        case SRC_RECENTER: {
            char name[64];
            gamepadBindingName(PC_KEY_ACT_GYRO_RECENTER, name, sizeof(name));
            snprintf(out, n, "%s  >", name);
            break;
        }
        }
        return;
    }
    if (group == PC_SET_PICKER_KEYBOARD && row >= 0 && row < PC_KEY_ACT_COUNT) {
        if (sWaitingForKey && sControlSelection == row) {
            snprintf(out, n, "%s", sCaptureSecond ? "[2nd: press a key / Del: none]" : "[Press a key / Del: none]");
            return;
        }
        keyBindingPairName(row, out, (size_t)n);
        return;
    }
    if (group == PC_SET_PICKER_GAMEPAD && row >= 0 && row < PC_KEY_ACT_COUNT) {
        if (sWaitingForButton && sGamepadSelection == row) { snprintf(out, n, "[Press a button...]"); return; }
        gamepadBindingName(row, out, (size_t)n);
        return;
    }
    if (group == PC_SET_PICKER_TEXPACKS) {
        if (row == 0) snprintf(out, n, "%s", sTexturePackPickerActive ? "Installing..." : "A: choose zip");
        else {
            std::vector<std::string> packs = pc_texpack_list_packs();
            if (row - 1 >= 0 && row - 1 < (int)packs.size()) {
                const bool active = sConfig.texturePackEnabled && packs[row - 1] == sConfig.texturePack;
                snprintf(out, n, "%s", active ? "Active" : "");
            }
        }
    }
    if (group == PC_SET_PICKER_HDMODELS) {
        if (sTexturePackPickerActive && sHdModelsSelection == row) snprintf(out, n, "Installing...");
        else snprintf(out, n, "%s", hdModelInstalled(row) ? "Installed" : "Not installed");
    }
    if (group == PC_SET_PICKER_RESOLUTION) {
        std::vector<int> c = pickerResolutionChoices();
        if (row < 0 || row >= (int)c.size()) return;
        const Resolution& r = sResolutions[c[row]];
        char aspectTag[16];
        aspectLabel(r.w, r.h, aspectTag, sizeof(aspectTag));
        const bool current = r.w == sPending.windowWidth && r.h == sPending.windowHeight;
        snprintf(out, n, "%s%s%s", aspectTag, r.isNative ? "  (native)" : (r.isDerived ? "  (window)" : ""),
                 current ? "  <" : "");
    }
}

void pc_settings_row_change(int group, int row, int dir, bool ok) {
    if (const GroupRow* r = groupRow(group, row)) {
        if (disabledReason(*r)) return;
        // Filas de valor: A/Enter equivale a "siguiente".
        const bool left = dir < 0, right = dir > 0 || ok;
        switch (r->src) {
        case SRC_MAIN: mainRowChange(r->idx, left, right, false); break;
        case SRC_ADV:
            if (r->idx == 7) { if (ok) advancedRowChange(r->idx, false, true); }
            else advancedRowChange(r->idx, left, right);
            break;
        case SRC_GFX:
            if (r->idx == 10 || r->idx == 11) return; // selectores: pc_settings_row_opens_picker
            graphicsRowChange(r->idx, left, right, false);
            break;
        case SRC_MODS: modsRowChange(r->idx, left, right); break;
        case SRC_DATA:
            if (!ok) return;
            if (r->idx < 2) saveDataRowAction(r->idx);
            else resetToDefaults();
            break;
        default: break; // selectores
        }
        return;
    }
    if (group == PC_SET_PICKER_RESOLUTION && ok) {
        std::vector<int> c = pickerResolutionChoices();
        if (row < 0 || row >= (int)c.size()) return;
        sResolutionIdx = c[row];
        sPending.windowWidth = sResolutions[c[row]].w;
        sPending.windowHeight = sResolutions[c[row]].h;
        applyVideo();
        startVideoConfirm();
    } else if (group == PC_SET_PICKER_KEYBOARD && row >= 0 && row < PC_KEY_ACT_COUNT) {
        // A: tecla principal; derecha: segunda tecla; izquierda: por defecto (issue #68).
        if (ok || dir > 0) startKeyCapture(row, dir > 0 && !ok);
        else if (dir < 0) {
            sPending.keyboardBindings[row] = kDefaultKeyBindings[row];
            sPending.keyboardBindings2[row] = pc_window_default_key_binding2(row);
        }
    } else if (group == PC_SET_PICKER_GAMEPAD && row >= 0 && row < PC_KEY_ACT_COUNT) {
        if (ok) startButtonCapture(row);
        else if (dir) pendingPadBinds()[row] = -1;
    } else if (group == PC_SET_PICKER_TEXPACKS && ok) {
        texturePacksRowAction(row, pc_texpack_list_packs());
    } else if (group == PC_SET_PICKER_HDMODELS && ok) {
        sHdModelsSelection = row;
        hdModelsRowAction(row);
    }
}

bool pc_settings_capture_active(void) { return sWaitingForKey || sWaitingForButton || sCaptureWaitRelease; }

void pc_settings_capture_poll(void) {
    SDL_GameController* ctl = menuController();
    if (sWaitingForKey) { pollKeyCapture(ctl); return; }
    if (sWaitingForButton || sCaptureWaitRelease) pollButtonCapture(ctl);
}

bool pc_settings_notice(char* out, unsigned long n, bool* isError) {
    std::lock_guard<std::mutex> lock(sTexturePackNoticeMutex);
    const bool fresh = SDL_GetTicks() - sTexturePackNoticeMs < kTexturePackNoticeTimeoutMs;
    if (!fresh || !sTexturePackNotice[0]) return false;
    if (out && n) snprintf(out, n, "%s", sTexturePackNotice);
    if (isError) *isError = sTexturePackNoticeError;
    return true;
}

bool pc_settings_restart_prompt_active(void) { return sTexturePackRestartPrompt || sHdModelRestartPrompt; }

void pc_settings_restart_prompt_answer(bool restart) {
    const bool hd = sHdModelRestartPrompt;
    sTexturePackRestartPrompt = false;
    sHdModelRestartPrompt = false;
    if (!restart) return;
#ifdef __ANDROID__
    pc_texpack_android_restart();
#else
    texturePackNotice(!hd, hd ? "HD model installed. Restart the game to apply it."
                              : "Texture pack active. Restart the game to apply it.");
#endif
}

void pc_settings_rows_begin(void) {
    sPending = sConfig;
    sPending.controlMode = pc_window_get_control_mode();
    sVideoConfirmActive = false;
    rebuildResolutionList();
    const int idx = resolutionIndexFor(pc_window_get_width(), pc_window_get_height());
    sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
}

void pc_settings_rows_end(bool save) {
    if (save) {
        commitPendingOnExit();
        return;
    }
    if (sVideoConfirmActive) {
        revertVideoSettings();
    } else if (isVideoSettingChanged()) {
        sPending = sConfig;
        applyVideo();
        syncResolutionIndex();
    }
}

bool pc_settings_video_confirm_active(void) { return sVideoConfirmActive; }

int pc_settings_video_confirm_seconds_left(void) {
    if (!sVideoConfirmActive) return 0;
    const Uint32 elapsed = SDL_GetTicks() - sVideoConfirmStartMs;
    if (elapsed >= kVideoConfirmDurationMs) return 0;
    return (int)((kVideoConfirmDurationMs - elapsed + 999) / 1000);
}

void pc_settings_video_confirm(bool keep) {
    if (!sVideoConfirmActive) return;
    if (keep) confirmVideoSettings();
    else revertVideoSettings();
}

PcNavEdges pc_settings_read_nav_edges(void) {
    PcNavEdges e = {};
    // Teclado con repetición al mantener (mismo retardo/cadencia que el
    // mando): en listas largas como la de resoluciones se baja solo.
    int numKeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numKeys);
    auto held = [&](SDL_Scancode a, SDL_Scancode b) { return (a < numKeys && keys[a]) || (b < numKeys && keys[b]); };
    e.up     = padEdge(held(SDL_SCANCODE_UP, SDL_SCANCODE_W), 6);
    e.down   = padEdge(held(SDL_SCANCODE_DOWN, SDL_SCANCODE_S), 7);
    e.left   = padEdge(held(SDL_SCANCODE_LEFT, SDL_SCANCODE_A), 8);
    e.right  = padEdge(held(SDL_SCANCODE_RIGHT, SDL_SCANCODE_D), 9);
    e.ok     = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    e.cancel = keyWentDown(SDL_SCANCODE_ESCAPE);
    e.tabPrev = keyWentDown(SDL_SCANCODE_Q) || keyWentDown(SDL_SCANCODE_PAGEUP);
    e.tabNext = keyWentDown(SDL_SCANCODE_E) || keyWentDown(SDL_SCANCODE_PAGEDOWN) || keyWentDown(SDL_SCANCODE_TAB);
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl || sTouchFrameButtons) {
        if (padEdge((ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
                    || (sTouchFrameButtons & PAD_TRIGGER_L), 10)) e.tabPrev = true;
        if (padEdge((ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
                    || (sTouchFrameButtons & PAD_TRIGGER_R), 11)) e.tabNext = true;
        if (padNavUp(ctl)) e.up = true;
        if (padNavDown(ctl)) e.down = true;
        if (padNavLeft(ctl)) e.left = true;
        if (padNavRight(ctl)) e.right = true;
        if (padNavA(ctl)) e.ok = true;
        if (padNavB(ctl)) e.cancel = true;
    }
    if (sTouchTapPending) {
        sTouchTapPending = false;
        e.tap  = true;
        e.tapX = sTouchTapX;
        e.tapY = sTouchTapY;
    }
    e.dragY = sTouchDragY * 480.0f;
    sTouchDragY = 0.0f;
    return e;
}

void pc_settings_touch_drag(float dy) { sTouchDragY += dy; }

// ─── Ajustes sin ventana, para el launcher ───────────────────────────────────
// El launcher no enlaza el juego: le pide las filas del F1 a este ejecutable y
// le manda los cambios. Así la lógica de cada ajuste existe una sola vez.
//   --settings-dump                 grupos y filas en JSON por stdout
//   --settings-set G R I [G R I..]  elige la opción I de la fila R del grupo G,
//                                   guarda pikmin_settings.conf y vuelca el JSON
//   --settings-reset                valores por defecto, guarda y vuelca
//   --texpack-install ZIP           instala un pack de texturas desde su zip
//   --texpack-activate NOMBRE       activa ese pack ("" = ninguno)
//   --hdmodel-install FILA ZIP      convierte e instala un modelo HD
//   --hdmodel-enable FILA 0|1       usar o no ese modelo HD instalado
// Trabaja sobre pikmin_settings.conf del directorio actual, como el juego.
namespace {

void jsonString(std::string& out, const char* text) {
    out += '"';
    for (const char* c = text ? text : ""; *c; ++c) {
        const unsigned char ch = (unsigned char)*c;
        if (ch == '"' || ch == '\\') { out += '\\'; out += (char)ch; }
        else if (ch == '\n') out += "\\n";
        else if (ch < 0x20) { char esc[8]; snprintf(esc, sizeof(esc), "\\u%04x", ch); out += esc; }
        else out += (char)ch;
    }
    out += '"';
}

void headlessDump(const std::string& message = std::string(), bool messageIsError = false) {
    std::string out = "{\"groups\":[";
    bool firstGroup = true;
    for (int g = 0; g < PC_SET_GROUP_COUNT; g++) {
        // Logros: solo lectura y dependen de la partida. Datos: acciones con
        // diálogos del juego (el reset tiene su propia orden).
        if (g == PC_SET_GROUP_ACHIEVEMENTS || g == PC_SET_GROUP_DATA) continue;
        if (!firstGroup) out += ',';
        firstGroup = false;
        out += "{\"id\":" + std::to_string(g) + ",\"name\":";
        jsonString(out, pc_settings_group_name(g));
        out += ",\"rows\":[";
        const int rows = pc_settings_rows_count(g);
        for (int r = 0; r < rows; r++) {
            if (r) out += ',';
            char value[256];
            pc_settings_row_value(g, r, value, sizeof(value));
            out += "{\"row\":" + std::to_string(r) + ",\"label\":";
            jsonString(out, pc_settings_row_label(g, r));
            out += ",\"help\":";
            jsonString(out, pc_settings_row_help(g, r));
            out += ",\"section\":";
            if (const char* section = pc_settings_row_section(g, r)) jsonString(out, section);
            else out += "null";
            out += ",\"enabled\":";
            out += pc_settings_row_enabled(g, r) ? "true" : "false";
            out += ",\"action\":";
            out += pc_settings_row_is_action(g, r) ? "true" : "false";
            out += ",\"picker\":" + std::to_string(pc_settings_row_opens_picker(g, r));
            out += ",\"value\":";
            jsonString(out, value);
            int current = -1;
            const int count = pc_settings_row_options(g, r, &current);
            out += ",\"current\":" + std::to_string(count > 0 ? current : -1) + ",\"options\":[";
            for (int k = 0; k < count; k++) {
                if (k) out += ',';
                jsonString(out, pc_settings_row_option(k));
            }
            out += "]}";
        }
        out += "]}";
    }
    out += "]";

    // Packs de texturas instalados (Load/Textures) y el activo.
    out += ",\"texturePacks\":{\"active\":";
    jsonString(out, sConfig.texturePackEnabled ? sConfig.texturePack.c_str() : "");
    out += ",\"installed\":[";
    const std::vector<std::string> packs = pc_texpack_list_packs();
    for (size_t i = 0; i < packs.size(); i++) {
        if (i) out += ',';
        jsonString(out, packs[i].c_str());
    }
    out += "]}";

    // Modelos HD: una fila por modelo, como en su selector del F1.
    out += ",\"hdModels\":[";
    const int models = pc_settings_rows_count(PC_SET_PICKER_HDMODELS);
    for (int m = 0; m < models; m++) {
        if (m) out += ',';
        out += "{\"row\":" + std::to_string(m) + ",\"name\":";
        jsonString(out, pc_settings_row_label(PC_SET_PICKER_HDMODELS, m));
        out += ",\"installed\":";
        out += hdModelInstalled(m) ? "true" : "false";
        out += ",\"enabled\":";
        out += pc_settings_get_hd_model_enabled(m) ? "true" : "false";
        out += '}';
    }
    out += "]";

    out += ",\"message\":";
    jsonString(out, message.c_str());
    out += ",\"messageError\":";
    out += messageIsError ? "true" : "false";
    out += "}\n";
    fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
}

} // namespace

int pc_settings_cli(int argc, char** argv) {
    int at = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--settings-dump") || !strcmp(argv[i], "--settings-set") || !strcmp(argv[i], "--settings-reset")
            || !strcmp(argv[i], "--texpack-install") || !strcmp(argv[i], "--texpack-activate")
            || !strcmp(argv[i], "--hdmodel-install") || !strcmp(argv[i], "--hdmodel-enable")) {
            at = i;
            break;
        }
    }
    if (at < 0) return -1;

    // Solo vídeo, para la lista de resoluciones; sin pantalla sigue valiendo.
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    sHeadless = true;
    loadConfig();
    sPending = sConfig;
    rebuildResolutionList();
    int idx = sHadConfigFile ? resolutionIndexFor(sPending.windowWidth, sPending.windowHeight) : -1;
    if (idx < 0) idx = defaultResolutionIndex();
    sResolutionIdx = idx;

    int result = 0;
    std::string message;
    bool messageIsError = false;
    if (!strcmp(argv[at], "--texpack-install") && at + 1 < argc) {
        char msg[192] = {};
        const int written = pc_texpack_install_zip(argv[at + 1], msg, sizeof(msg));
        message = msg;
        messageIsError = written <= 0;
    } else if (!strcmp(argv[at], "--texpack-activate") && at + 1 < argc) {
        sConfig.texturePack = argv[at + 1];
        sConfig.texturePackEnabled = sConfig.texturePack.empty() ? 0 : 1;
        sPending = sConfig;
        saveConfig();
    } else if (!strcmp(argv[at], "--hdmodel-enable") && at + 2 < argc) {
        const int row = atoi(argv[at + 1]);
        if (row >= 0 && row < 6) {
            if (atoi(argv[at + 2])) sConfig.hdModelsDisabled &= ~(1 << row);
            else sConfig.hdModelsDisabled |= 1 << row;
        }
        sPending = sConfig;
        saveConfig();
    } else if (!strcmp(argv[at], "--hdmodel-install") && at + 2 < argc) {
        char msg[192] = {};
        const int written = pc_hd_models_convert_file(argv[at + 2], atoi(argv[at + 1]), msg, sizeof(msg));
        message = msg;
        messageIsError = written <= 0;
    } else if (!strcmp(argv[at], "--settings-set")) {
        for (int i = at + 1; i + 2 < argc; i += 3) {
            pc_settings_row_pick_option(atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i + 2]));
        }
        sConfig = sPending;
        saveConfig();
    } else if (!strcmp(argv[at], "--settings-reset")) {
        sConfig.applyDefaults();
        sPending = sConfig;
        saveConfig();
    }
    headlessDump(message, messageIsError);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}
