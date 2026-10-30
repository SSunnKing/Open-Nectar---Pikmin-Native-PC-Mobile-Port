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
#include "netplay/pc_online_switch.h"
#include "settings/pc_settings_rows.h"
#include "settings/pc_glass_menu.h"
#include "GlobalGameOptions.h"
#include "mods/pc_hd_models.h"
#include "mods/pc_pikmin_models.h"
#include "mods/pc_hd_model_convert.h"
#include "pc_file_dialog.h"
#include "settings/pc_settings_p2d.h"
#include "pc_menu_repeat.h"
#include "pc_menu_click.h"
#include "settings/pc_prompt_pointer.h"
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
#include <sstream>
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
#include "mods/pc_vs_map.h"
#include "mods/pc_map_catalog.h"
#include "mods/pc_mission.h"
#include "mods/pc_campaign.h"
#include "nectar_mapcore/pack.h"
#include "netplay/pc_netplay_det.h"
#include "pc_achievements.h"
#include "pc_speedrun.h"
#include "pc_livesplit.h"
#include "pc_captain_preview.h"
#include "mods/pc_hd_model_convert.h"
#include "randomizer/pc_randomizer.h"
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

std::string pc_card_save_dir(); // card_stubs.cpp

namespace {

constexpr const char* kConfigFilename = "pikmin_settings.conf";

struct PcConfig {
    int windowWidth = 1280;
    int windowHeight = 720;
    int displayMode = PC_WINDOW_FULLSCREEN_WINDOWED; // 0 windowed, 1 fullscreen, 2 borderless
    double refreshRate = 0.0;                        // 0 = auto (detect)
    bool vsync = true;                               // presentation pacing on/off
    float renderScale = 0.0f; // 0 = original de GameCube (480 líneas)                 // internal 3D resolution multiplier
    int aspectRatioMode = 0;                         // 0=auto, 1=4:3, 2=16:10, 3=16:9, 4=21:9

    // FPS mode (0=30fps, 1=60fps, 2=120fps experimental, 3=monitor refresh rate)
    int fpsMode = 0;

    // Control mode (0=Classic, 1=Mouse Cursor)
    int controlMode = PC_CONTROL_CLASSIC;
    // Botones del mando por defecto: GameCube o Switch (PC_PAD_SCHEME_*).
    int padScheme = PC_PAD_SCHEME_GAMECUBE;
    // Apuntar con el stick derecho (PC_RS_AIM_*) y su sensibilidad.
    int rsAimMode = PC_RS_AIM_OFF;
    float rsAimSens = 1.0f;

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
    // Free Camera: L mira hacia el cursor (1) o al frente del capitán (0).
    int faceCursor = 0;
    int whistleRadiusPct = 100; // radio máximo del silbato, % del original
    int throwSpeedPct = 100;    // velocidad de las animaciones de coger y lanzar
    int throwCancelB = 0;       // B con un Pikmin en la mano lo devuelve al grupo
    int quickGrab = 0;          // el Pikmin elegido aparece en la mano (sin andar hasta ella)
    int noTrip = 0;             // los Pikmin no tropiezan al correr
    // Whistling over sprouts plucks them one at a time (0=off/faithful, 1=on).
    int whistlePluck = 0;
    int bombControl = 0;
    int bombRespawnSec = 0;     // mod Bomb Rock Respawn: segundos por roca (0 = original, al día siguiente)
    int hideOlimarText = 0;     // sin los textos de Olimar (primer Pikmin, piezas, avisos)
    int speedrunIntroHidden = 0; // explicación del modo Speedrun ya vista (se abre sola solo la primera vez)        // botón Bomb: el amarillo con bomba la lanza al cursor o la suelta
    int speedrunCategory = 0;    // categoría del modo Speedrun (PC_SR_CAT_*)
    int speedrunSplitsShown = 4;
    // LiveSplit (servidor TCP): opcional, apagado por defecto. Host y puerto
    // solo en el archivo de ajustes (otro PC de la red, p. ej. desde Android).
    int speedrunLiveSplit = 0;
    int speedrunLsHideClock = 0; // con LiveSplit conectado, sin reloj ni splits propios
    char speedrunLsHost[64] = "127.0.0.1";
    int speedrunLsPort = 16834; // splits visibles bajo el reloj del modo Speedrun (1-10)
    int onlineStun = 1;         // online: servidor STUN público para jugar entre redes distintas
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
    int fireflies = 0;          // luciérnagas por el escenario (de día o de noche)
    int p2Selection = 0;        // issue #67: cruceta como en Pikmin 2 (tipo fijo, arriba/abajo hoja/capullo/flor/bomba)
    // Reglas del modo VS (índices de las opciones del menú previo).
    int vsDuration = 2;  // 5 / 10 / 15 / 25 / 30 min
    int vsRocketWin = 1; // cohete asediable y destruirlo gana
    int vsRocketHp = 1;  // baja / normal / alta
    int vsBigPiece = 1;  // minuto 3 / minuto 7 / desde el inicio / sin gorda
    int vsPikiLimit = 2; // 25 / 40 / 50 por jugador
    int vsPellets = 1;   // cada 30 / 45 / 60 s / sin pastillas
    std::string vsMap;   // carpeta en user/maps/vs/; vacío = la arena de serie
    std::string missionMap; // Custom Maps: la última misión elegida (carpeta en user/maps/)
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
    // Modelos de Pikmin propios (Load/Pikmin) por color (PikiColor: azul,
    // rojo, amarillo): id del pack, vacío = el del juego.
    std::string pikminModel[3];

    void applyDefaults() {
        windowWidth = 1280;
        windowHeight = 720;
        displayMode = PC_WINDOW_FULLSCREEN_WINDOWED;
        refreshRate = 0.0;
        vsync = true;
        renderScale = 0.0f;
        aspectRatioMode = 0;
        fpsMode = 0;
        controlMode = PC_CONTROL_CLASSIC;
        padScheme = PC_PAD_SCHEME_GAMECUBE;
        rsAimMode = PC_RS_AIM_OFF;
        rsAimSens = 1.0f;
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
        faceCursor = 0;
        whistleRadiusPct = 100;
        throwSpeedPct = 100;
        throwCancelB = 0;
        quickGrab = 0;
        noTrip = 0;
        whistlePluck = 0;
        bombControl = 0;
        bombRespawnSec = 0;
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
        fireflies = 0;
        allOnions = 0;
        vsDuration = 2;
        vsRocketWin = 1;
        vsRocketHp = 1;
        vsBigPiece = 1;
        vsPikiLimit = 2;
        vsPellets = 1;
        vsMap.clear();
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
        for (std::string& m : pikminModel) m.clear();
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
// Controller Guide: qué hace cada botón con el esquema, los botones y los
// mods de ahora mismo (se rehace cada vez que se pinta).
bool sInPadGuide = false;
int sPadGuideSel = 0;
int padGuideCount();
const char* padGuideLabel(int i);
const char* padGuideValue(int i);
bool padGuideIsHeader(int i);
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
#ifndef __ANDROID__
// Tags of the desktop file picker requests (pc_file_dialog). The picker stays
// open across frames; pollFilePicker() handles its answer.
enum : int { kPickTexturePack = 1, kPickNectarPack = 2, kPickHdModel = 100 /* + row */ };
#endif
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
    pc_window_set_pad_scheme(config.padScheme);
    pc_window_set_rs_aim(config.rsAimMode, config.rsAimSens);
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
void hdModelsToggle(int row);
bool hdModelInstalled(int row);
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
    sInPadGuide = false;
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

#if PIKI_NETPLAY_BUILD
// Netplay: every setting that changes the simulation. Both peers hash these
// in the handshake and they are locked for the session. Window, graphics,
// audio, bindings, camera feel and other local-only rows are not here.
#define PC_NETPLAY_SESSION_FIELDS(X)                                                          \
    X(fpsMode) X(chainActions) X(holdToPluck) X(betterPathfinding) X(bluesOnlyWater)         \
    X(idleCounter) X(naviHealthPct) X(tekiHealthPct) X(infiniteDay) X(freeCamera)            \
    X(whistleRadiusPct) X(throwSpeedPct) X(throwCancelB) X(quickGrab) X(noTrip)              \
    X(whistlePluck) X(bombControl) X(bombRespawnSec) X(hideOlimarText) X(onionStep10) X(instantWhistle)        \
    X(pikiInvincible) X(allFlowers) X(carrySpeedPct) X(naviSpeedPct) X(unlockZones)          \
    X(noDayAdvance) X(allOnions) X(breakableGates) X(eternalNight) X(fireflies)              \
    X(p2Selection) X(vsDuration) X(vsRocketWin) X(vsRocketHp) X(vsBigPiece) X(vsPikiLimit)   \
    X(vsPellets) X(lockOn) X(charge) X(throwWhileMoving) X(firstPerson) X(pikiLimit)         \
    X(dayMinutes) X(coopSplit) X(coopMergeCamera) X(debugKeys) X(gyroEnabled)

bool sNetplaySessionLocked = false;
PcConfig sNetplaySessionConfig;
// The joiner of a launcher session adopted the host's values: the file keeps
// this player's own (sNetplayDiskConfig), never the adopted ones.
bool sNetplayAdopted = false;
PcConfig sNetplayDiskConfig;

void netplayRelockSessionFields() {
    if (!sNetplaySessionLocked) return;
#define PC_NETPLAY_RELOCK(f) sConfig.f = sNetplaySessionConfig.f; sPending.f = sNetplaySessionConfig.f;
    PC_NETPLAY_SESSION_FIELDS(PC_NETPLAY_RELOCK)
#undef PC_NETPLAY_RELOCK
    sConfig.vsMap  = sNetplaySessionConfig.vsMap;
    sPending.vsMap = sNetplaySessionConfig.vsMap;
}

// While saveConfig writes, the session fields hold this player's own values.
struct NetplayDiskValuesScope {
    bool active = false;
    PcConfig live;
    NetplayDiskValuesScope() {
        if (!sNetplaySessionLocked || !sNetplayAdopted) return;
        active = true;
        live = sConfig;
#define PC_NETPLAY_DISK(f) sConfig.f = sNetplayDiskConfig.f;
        PC_NETPLAY_SESSION_FIELDS(PC_NETPLAY_DISK)
#undef PC_NETPLAY_DISK
        sConfig.vsMap = sNetplayDiskConfig.vsMap;
    }
    ~NetplayDiskValuesScope() {
        if (active) sConfig = live;
    }
};

// Inverse of pc_settings_netplay_config_text: sets every session field the
// text names. Unknown or malformed pairs are ignored.
int netplayApplyConfigText(const char* text) {
    int applied = 0;
    std::string s(text != nullptr ? text : "");
    size_t pos = 0;
    while (pos < s.size()) {
        size_t semi = s.find(';', pos);
        const std::string pair = s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        pos = semi == std::string::npos ? s.size() : semi + 1;
        const size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = pair.substr(0, eq);
        const std::string val = pair.substr(eq + 1);
        if (key == "vsMap") {
            sConfig.vsMap = val;
            ++applied;
            continue;
        }
        char* end = nullptr;
        const long v = std::strtol(val.c_str(), &end, 10);
        if (val.empty() || end == nullptr || *end != '\0') continue;
#define PC_NETPLAY_ADOPT(f) if (key == #f) { sConfig.f = (int)v; ++applied; continue; }
        PC_NETPLAY_SESSION_FIELDS(PC_NETPLAY_ADOPT)
#undef PC_NETPLAY_ADOPT
    }
    return applied;
}
#endif

void saveConfig() {
#if PIKI_NETPLAY_BUILD
    netplayRelockSessionFields();
    NetplayDiskValuesScope netplayDiskValues;
#endif
    std::string path = std::string(kConfigFilename);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        printf("[PC Settings] Failed to write %s\n", path.c_str());
        return;
    }
    out << "# Open Nectar settings (F1 or backtick in-game to change)\n";
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
    out << "faceCursor = " << sConfig.faceCursor << "\n";
    out << "whistleRadiusPct = " << sConfig.whistleRadiusPct << "\n";
    out << "throwSpeedPct = " << sConfig.throwSpeedPct << "\n";
    out << "throwCancelB = " << sConfig.throwCancelB << "\n";
    out << "quickGrab = " << sConfig.quickGrab << "\n";
    out << "noTrip = " << sConfig.noTrip << "\n";
    out << "whistlePluck = " << sConfig.whistlePluck << "\n";
    out << "bombControl = " << sConfig.bombControl << "\n";
    out << "bombRespawnSec = " << sConfig.bombRespawnSec << "\n";
    out << "breakableGates = " << sConfig.breakableGates << "\n";
    out << "freeCamPadPct = " << sConfig.freeCamPadPct << "\n";
    out << "p2Selection = " << sConfig.p2Selection << "\n";
    out << "eternalNight = " << sConfig.eternalNight << "\n";
    out << "fireflies = " << sConfig.fireflies << "\n";
    out << "hideOlimarText = " << sConfig.hideOlimarText << "\n";
    out << "speedrunIntroHidden = " << sConfig.speedrunIntroHidden << "\n";
    out << "speedrunCategory = " << sConfig.speedrunCategory << "\n";
    out << "speedrunSplitsShown = " << sConfig.speedrunSplitsShown << "\n";
    out << "speedrunLiveSplit = " << sConfig.speedrunLiveSplit << "\n";
    out << "speedrunLsHideClock = " << sConfig.speedrunLsHideClock << "\n";
    out << "speedrunLsHost = " << sConfig.speedrunLsHost << "\n";
    out << "speedrunLsPort = " << sConfig.speedrunLsPort << "\n";
    out << "onlineStun = " << sConfig.onlineStun << "\n";
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
    out << "vsMap = " << sConfig.vsMap << "\n";
    out << "missionMap = " << sConfig.missionMap << "\n";
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
    out << "pikminModelBlue = " << sConfig.pikminModel[Blue] << "\n";
    out << "pikminModelRed = " << sConfig.pikminModel[Red] << "\n";
    out << "pikminModelYellow = " << sConfig.pikminModel[Yellow] << "\n";
    out << "controlMode = " << sConfig.controlMode << "\n";
    out << "padScheme = " << sConfig.padScheme << "\n";
    out << "rsAimMode = " << sConfig.rsAimMode << "\n";
    out << "rsAimSens = " << sConfig.rsAimSens << "\n";
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
            if (sConfig.renderScale != 0.0f && (sConfig.renderScale < 0.25f || sConfig.renderScale > 4.0f)) sConfig.renderScale = 0.0f;
        }
        else if (key == "rsAimMode") {
            sConfig.rsAimMode = atoi(val.c_str());
            if (sConfig.rsAimMode < PC_RS_AIM_OFF || sConfig.rsAimMode > PC_RS_AIM_ALWAYS) sConfig.rsAimMode = PC_RS_AIM_OFF;
        }
        else if (key == "rsAimSens") {
            sConfig.rsAimSens = (float)atof(val.c_str());
            if (!(sConfig.rsAimSens >= 0.25f && sConfig.rsAimSens <= 3.0f)) sConfig.rsAimSens = 1.0f;
        }
        else if (key == "padScheme") {
            sConfig.padScheme = atoi(val.c_str());
            if (sConfig.padScheme < 0 || sConfig.padScheme >= PC_PAD_SCHEME_COUNT) sConfig.padScheme = PC_PAD_SCHEME_GAMECUBE;
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
            if (sConfig.fpsMode > 3) sConfig.fpsMode = 3;
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
        else if (key == "faceCursor") sConfig.faceCursor = atoi(val.c_str()) ? 1 : 0;
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
        else if (key == "speedrunCategory") {
            sConfig.speedrunCategory = std::clamp(atoi(val.c_str()), 0, PC_SR_CAT_COUNT - 1);
        }
        else if (key == "speedrunLiveSplit") {
            sConfig.speedrunLiveSplit = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "speedrunLsHideClock") {
            sConfig.speedrunLsHideClock = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "speedrunLsHost") {
            if (!val.empty()) snprintf(sConfig.speedrunLsHost, sizeof(sConfig.speedrunLsHost), "%s", val.c_str());
        }
        else if (key == "speedrunLsPort") {
            sConfig.speedrunLsPort = std::clamp(atoi(val.c_str()), 1, 65535);
        }
        else if (key == "onlineStun") {
            sConfig.onlineStun = atoi(val.c_str()) != 0 ? 1 : 0;
        }
        else if (key == "speedrunSplitsShown") {
            sConfig.speedrunSplitsShown = std::clamp(atoi(val.c_str()), 1, 10);
        }
        else if (key == "hideOlimarText") {
            sConfig.hideOlimarText = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "breakableGates") {
            sConfig.breakableGates = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "eternalNight") sConfig.eternalNight = atoi(val.c_str()) ? 1 : 0;
        else if (key == "fireflies") sConfig.fireflies = atoi(val.c_str()) ? 1 : 0;
        else if (key == "p2Selection") {
            sConfig.p2Selection = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "freeCamPadPct") {
            sConfig.freeCamPadPct = clampFreeCamPadPct(atoi(val.c_str()));
        }
        else if (key == "bombControl") {
            sConfig.bombControl = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "bombRespawnSec") {
            const int v = atoi(val.c_str());
            sConfig.bombRespawnSec = (v == 30 || v == 60 || v == 120) ? v : 0;
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
        else if (key == "vsMap") {
            // Solo un nombre de carpeta, nunca una ruta.
            const bool safe = val.size() < 64 && val.find('/') == std::string::npos
                && val.find('\\') == std::string::npos && val.find("..") == std::string::npos;
            sConfig.vsMap = safe ? val : std::string();
        }
        else if (key == "missionMap") {
            const bool safe = val.size() < 64 && val.find('/') == std::string::npos
                && val.find('\\') == std::string::npos && val.find("..") == std::string::npos;
            sConfig.missionMap = safe ? val : std::string();
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
        else if (key == "pikminModelBlue" || key == "pikminModelRed" || key == "pikminModelYellow") {
            // Nombre de una carpeta de Load/Pikmin: solo letras, números y guiones.
            bool safe = val.size() <= 64;
            for (char c : val) safe = safe && (isalnum((unsigned char)c) || c == '-');
            const int color = key == "pikminModelBlue" ? Blue : key == "pikminModelRed" ? Red : Yellow;
            sConfig.pikminModel[color] = safe ? val : std::string();
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
                const bool isButton = button >= PC_GP_BIND_NONE && button < SDL_CONTROLLER_BUTTON_MAX;
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
// A y B del menú siguen los bindings del mando, como el juego: con A y B
// intercambiados el menú aceptaba con uno y el juego con el otro. Una acción
// sin botón asignado vuelve al físico para que el menú nunca quede inservible.
bool menuPadHeld(SDL_GameController* c, int action, SDL_GameControllerButton physical)
{
	if (!c)
		return false;
	const int bind = pc_window_get_gamepad_binding(action);
	if (bind < 0)
		return SDL_GameControllerGetButton(c, physical) != 0;
	return pc_window_gamepad_bind_held(c, bind);
}
bool padNavA(SDL_GameController* c) { return padEdge(menuPadHeld(c, PC_KEY_ACT_A, SDL_CONTROLLER_BUTTON_A) || (sTouchFrameButtons & PAD_BUTTON_A), 4); }
bool padNavB(SDL_GameController* c) { return padEdge(menuPadHeld(c, PC_KEY_ACT_B, SDL_CONTROLLER_BUTTON_B) || (sTouchFrameButtons & PAD_BUTTON_B), 5); }

// The new-game prompt is a game-facing dialog rather than the F1 settings
// menu. Its accept/cancel actions follow the configured A/B bindings, which
// may be either SDL buttons or the axis encodings used by the remapping page.
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
	return menuPadHeld(ctl, PC_KEY_ACT_A, SDL_CONTROLLER_BUTTON_A);
}

} // namespace

bool keyWentDown(SDL_Scancode sc) {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    bool now = (int)sc < numKeys && state[sc] != 0;
    bool prev = (int)sc < (int)gPrevKeys.size() && gPrevKeys[sc] != 0;
    return now && !prev;
}

// Left click sampled once per poll. pc_window_take_mouse_pressed is destructive.
static Uint32 sMenuMouseEdge = 0;

static bool menuBindingWentDown(int bind) {
    if (bind <= 0 || bind >= SDL_NUM_SCANCODES) return false;
    return keyWentDown(static_cast<SDL_Scancode>(bind));
}

bool menuAcceptClick() {
    return (sMenuMouseEdge & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
}

// Accept: Enter, numpad Enter, Space, left click, and a non-mouse A binding.
// Cancel: Escape, Backspace, right click, and a B binding that is not Shift.
// A text field keeps Backspace for delete and does not call menuCancelKey.
// The accept keys without the left click, for a panel whose rows take the click themselves.
bool menuOkKeyNoClick() {
    return keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_KP_ENTER)
        || keyWentDown(SDL_SCANCODE_SPACE)
        || menuBindingWentDown(pc_window_get_key_binding(PC_KEY_ACT_A))
        || menuBindingWentDown(pc_window_get_key_binding2(PC_KEY_ACT_A));
}
bool menuOkKey() {
    return menuOkKeyNoClick() || menuAcceptClick();
}
static bool menuCancelBinding(int bind) {
    if (bind == SDL_SCANCODE_LSHIFT || bind == SDL_SCANCODE_RSHIFT) return false;
    return menuBindingWentDown(bind);
}
bool menuCancelKey() {
    return keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_BACKSPACE)
        || (sMenuMouseEdge & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0
        || menuCancelBinding(pc_window_get_key_binding(PC_KEY_ACT_B))
        || menuCancelBinding(pc_window_get_key_binding2(PC_KEY_ACT_B));
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
void pcRandomizerIntroInput();
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
void pcMissionPromptInput();
void pcCampaignMapInput();
void pcMissionEndInput();
void pcVsEndScreenInput();
bool vsEndScreenShown();

void pollMenuInput() {
    sMenuMouseEdge = pc_window_take_mouse_pressed();
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
    if (pc_randomizer_intro_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcRandomizerIntroInput();
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
    if (pc_mission_prompt_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcMissionPromptInput();
        return;
    }
    if (pc_campaign_map_active()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcCampaignMapInput();
        return;
    }
    if (pc_mission_end_screen()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        pcMissionEndInput();
        return;
    }
    if (pc_mission_textbox()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        // Cuadro de texto de un disparador: A (o B) lo cierra.
        bool close = menuOkKey() || menuCancelKey()
                  || (sTouchFrameButtons & (PAD_BUTTON_A | PAD_BUTTON_B)) != 0;
        sTouchTapPending = false;
        if (SDL_GameController* ctl = pc_window_get_controller()) close |= promptPadA(ctl) || promptPadB(ctl);
        if (close) {
            SeSystem::playSysSe(SYSSE_DECIDE1);
            pc_mission_close_textbox();
        }
        return;
    }
    if (vsEndScreenShown()) {
#if PIKI_PC_TOUCH
        pc_touch_claim_game_menu();
#endif
        // Netplay: the choice comes from both players' synced pads, in the
        // sim (pc_vs_end_screen_sim_input); the local devices only feed them.
        if (!pc_netplay_deterministic()) pcVsEndScreenInput();
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
        pc_pikmin_models_refresh(); // modelos instalados con el juego abierto
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

    // F1 or the backtick key (top left on a Mac keyboard, no Fn needed) always
    // toggles. Select/View can open the menu while it is closed; closing is
    // handled after video confirmation has had first refusal.
    if (keyWentDown(SDL_SCANCODE_F1) || keyWentDown(SDL_SCANCODE_GRAVE) || toggleRequested) {
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
        if (menuOkKey() ||
            keyWentDown(SDL_SCANCODE_J) ||
            padNavA(ctl)) {
            confirmVideoSettings();
        } else if (menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
        bool ok = menuOkKey();

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
        if (menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
            keyWentDown(SDL_SCANCODE_B) || padNavB(ctl)) {
            sInControlsSubmenu = false;
            sWaitingForKey = false;
            sCaptureWaitRelease = false;
        }
        return;
    }

    // Controller Guide: solo lectura; arriba/abajo recorre, B/Esc vuelve.
    if (sInPadGuide) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl)) up = true;
            if (padNavDown(ctl)) down = true;
        }
        const int count = padGuideCount();
        if (up && sPadGuideSel > 0) sPadGuideSel--;
        if (down && sPadGuideSel < count - 1) sPadGuideSel++;
        if (menuCancelKey() || keyWentDown(SDL_SCANCODE_B) || padNavB(ctl) || padNavA(ctl) || menuOkKey())
            sInPadGuide = false;
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
        bool ok = menuOkKey();

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
        // Izquierda/derecha: por defecto <-> ninguno; Supr/Retroceso lo vacía.
        if (keyWentDown(SDL_SCANCODE_DELETE) || keyWentDown(SDL_SCANCODE_BACKSPACE)) {
            pendingPadBinds()[sGamepadSelection] = PC_GP_BIND_NONE;
            return;
        }
        if (left || right) {
            int& b = pendingPadBinds()[sGamepadSelection];
            b = b == -1 ? PC_GP_BIND_NONE : -1;
            return;
        }
        if (menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
        bool ok = menuOkKey();
        bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
        bool accept = menuOkKey();
        bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
        bool ok = menuOkKey();
        bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);
        if (ctl || sTouchFrameButtons) {
            if (padNavUp(ctl)) up = true;
            if (padNavDown(ctl)) down = true;
            if (padNavA(ctl)) ok = true;
            if (padNavB(ctl)) cancel = true;
        }
        bool toggle = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_A)
                   || keyWentDown(SDL_SCANCODE_D);
        // The touch stick reaches the menu as digital directions too.
        if ((ctl || sTouchFrameButtons) && (padNavLeft(ctl) || padNavRight(ctl))) toggle = true;
        const int rowCount = 6;
        if (up) { sHdModelsSelection = (sHdModelsSelection + rowCount - 1) % rowCount; return; }
        if (down) { sHdModelsSelection = (sHdModelsSelection + 1) % rowCount; return; }
        if (cancel) { sInHdModelsSubmenu = false; return; }
        if (toggle) { hdModelsToggle(sHdModelsSelection); return; }
        if (ok) hdModelsRowAction(sHdModelsSelection);
        return;
    }

    // Texture packs submenu. The restart prompt owns input while it is up:
    // activating a pack asks for a restart, and the choice must not leak into
    // the pack list underneath as a stray press.
    if (sTexturePackRestartPrompt) {
        bool accept = menuOkKey();
        bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
        bool ok = menuOkKey();
        bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
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
    bool ok = menuOkKey();
    bool cancel = menuCancelKey() || keyWentDown(SDL_SCANCODE_K) ||
                  keyWentDown(SDL_SCANCODE_B);
    bool tabPrev = keyWentDown(SDL_SCANCODE_Q) || keyWentDown(SDL_SCANCODE_PAGEUP);
    bool tabNext = keyWentDown(SDL_SCANCODE_E) || keyWentDown(SDL_SCANCODE_PAGEDOWN) ||
                   keyWentDown(SDL_SCANCODE_TAB);
    // Ratón: rueda = arriba/abajo. El clic derecho es B y el izquierdo es A
    // (menuCancelKey / menuOkKey). El izquierdo también llega como toque.
    if (const int wheel = pc_window_take_wheel_steps()) (wheel > 0 ? up : down) = true;
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
        case PC_SET_PICKER_PADGUIDE:
            sInPadGuide = true;
            sPadGuideSel = 0;
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
        const Uint32 mouseWent = sMenuMouseEdge & ~sCapturePrevMouse;
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
        // Supr o Retroceso dejan la acción sin botón, como en el teclado.
        if (keyWentDown(SDL_SCANCODE_DELETE) || keyWentDown(SDL_SCANCODE_BACKSPACE)) {
            pendingPadBinds()[sGamepadSelection] = PC_GP_BIND_NONE;
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
    // Controller layout: GameCube or Switch button defaults.
    else if (row == 8) {
        if (left || right) sPending.padScheme = sPending.padScheme == PC_PAD_SCHEME_SWITCH ? PC_PAD_SCHEME_GAMECUBE : PC_PAD_SCHEME_SWITCH;
    }
    // Right stick aim: Off / Hold button / Always.
    else if (row == 9) {
        if (left) sPending.rsAimMode = (sPending.rsAimMode + 2) % 3;
        else if (right) sPending.rsAimMode = (sPending.rsAimMode + 1) % 3;
    }
    // Right stick aim sensitivity (0.25 - 3.00, step 0.25).
    else if (row == 10) {
        if (left) sPending.rsAimSens = std::max(0.25f, sPending.rsAimSens - 0.25f);
        if (right) sPending.rsAimSens = std::min(3.0f, sPending.rsAimSens + 0.25f);
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
    // Modelos de Pikmin: el del juego y los de Load/Pikmin para ese color.
    if (row >= 14 && row <= 16) {
        const int color = row == 14 ? Red : row == 15 ? Yellow : Blue;
        const int count = pc_pikmin_models_count(color);
        int at          = -1; // -1 = el del juego
        for (int k = 0; k < count; k++)
            if (sPending.pikminModel[color] == pc_pikmin_models_id(color, k)) at = k;
        at = left ? (at + 1 + count) % (count + 1) - 1 : (at + 2) % (count + 1) - 1;
        sPending.pikminModel[color] = at < 0 ? std::string() : std::string(pc_pikmin_models_id(color, at));
        // Se ve al momento (no mientras F1 sondea la lista de opciones).
        if (!sProbing) sConfig.pikminModel[color] = sPending.pikminModel[color];
        (void)right;
        (void)ok;
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
    // Free Camera: hacia dónde mira la cámara al pulsar L.
    else if (row == 43) {
        if (left || right) sPending.faceCursor = sPending.faceCursor ? 0 : 1;
    }
    else if (row == 42) {
        static const int kSteps[] = { 0, 30, 60, 120 };
        int i = 0;
        while (i < 3 && kSteps[i] != sPending.bombRespawnSec) i++;
        if (right) i = (i + 1) % 4;
        if (left) i = (i + 3) % 4;
        sPending.bombRespawnSec = kSteps[i];
    }
    else if (row == 41) {
        if (left || right) sPending.fireflies = sPending.fireflies ? 0 : 1;
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
    (void)row;
    const std::string dir = pc_card_save_dir();
    if (SDL_OpenURL(("file://" + dir).c_str()) != 0) {
        texturePackNotice(true, ("Could not open the folder: " + dir).c_str());
    }
#endif
}

void texturePacksRowAction(int row, const std::vector<std::string>& packs) {
    if (row == kTexturePackInstallRow) {
        if (!sTexturePackPickerActive) {
#ifdef __ANDROID__
            sTexturePackPickerActive = true;
            pc_texpack_android_open_picker();
#else
            // Desktop: native picker; the game keeps drawing while it is
            // open. pollFilePicker() then extracts the zip into
            // Load/Textures with the same path rule as Android.
            if (pc_file_dialog_begin(kPickTexturePack, "Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP"))
                sTexturePackPickerActive = true;
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
    // Desktop: native picker; pollFilePicker() converts the chosen rip.
    static const char* kTitles[6] = {
        "Choose the Pikmin 3 Olimar zip", "Choose the Pikmin 2 Louie zip", "Choose the Pikmin 3 Louie zip",
        "Choose the Pikmin 3 Pikmin zip", "Choose the Pikmin 3 Bulborb zip", "Choose the Pikmin 3 Dwarf Bulborb zip",
    };
    if (row >= 0 && row < 6 && pc_file_dialog_begin(kPickHdModel + row, kTitles[row], "Model zip", "*.zip *.ZIP"))
        sTexturePackPickerActive = true;
#endif
        
}

// Activa o desactiva un modelo HD instalado (lo mismo que el launcher). Se
// aplica al momento: el juego consulta el ajuste en cada dibujo.
void hdModelsToggle(int row) {
    if (row < 0 || row >= 6 || !hdModelInstalled(row)) return;
    sConfig.hdModelsDisabled ^= 1 << row;
    sPending.hdModelsDisabled = sConfig.hdModelsDisabled;
    saveConfig();
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
    if (sPending.fpsMode == 3) {
        snprintf(fpsModeBuf, sizeof(fpsModeBuf), "Monitor (%.0f Hz)", pc_window_get_refresh_rate());
    } else {
        snprintf(fpsModeBuf, sizeof(fpsModeBuf), "%s", fpsModeNames[sPending.fpsMode >= 0 && sPending.fpsMode < 3 ? sPending.fpsMode : 0]);
    }

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
        if (rs <= 0.0f) snprintf(valueBuf[3], sizeof(valueBuf[3]), "Original (GameCube, 480p)");
        else if (fabsf(rs - 2.0f / 3.0f) < 0.01f) snprintf(valueBuf[3], sizeof(valueBuf[3]), "Auto (native)");
        else snprintf(valueBuf[3], sizeof(valueBuf[3]), "%.2fx", rs);
    }
    if (sPending.refreshRate <= 0.0) snprintf(valueBuf[4], sizeof(valueBuf[4]), "Auto");
    else snprintf(valueBuf[4], sizeof(valueBuf[4]), "%.0f Hz", sPending.refreshRate);
    snprintf(valueBuf[5], sizeof(valueBuf[5]), "%s", sPending.vsync ? "On" : "Off");
    snprintf(valueBuf[ROW_FPS_MODE], sizeof(valueBuf[0]), "%s", fpsModeBuf);

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
        static const float kScales[] = { 0.0f, 0.5f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f };
        constexpr int kScaleCount = 6;
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
            sPending.fpsMode = (sPending.fpsMode - 1 + 4) % 4;
        } else if (right) {
            sPending.fpsMode = (sPending.fpsMode + 1) % 4;
        }
        break;
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

// The button icons of the help line drawn last, and where each one starts. The VS lobby's
// "A: play" hint takes a click over its span, so it reads these after the line is drawn.
struct HelpLineIcon {
    char letter;
    int x;
};
constexpr int kHelpLineIcons = 8;
HelpLineIcon sHelpIcons[kHelpLineIcons];
int sHelpIconCount = 0;
int sHelpLineY = 0, sHelpLineH = 0, sHelpLineEnd = 0;

// Línea de ayuda centrada en cx. Con el marco P2D, cada letra de botón suelta
// (A, B, X, Y, Z seguida de espacio, "/" o ":") se dibuja con el icono del
// botón del juego; el resto del texto no cambia.
void drawHelpLine(int cx, int y, const char* text, Colour c, int maxWidth = 570) {
    sHelpIconCount = 0;
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
            if (sHelpIconCount < kHelpLineIcons) sHelpIcons[sHelpIconCount++] = HelpLineIcon{ *p, x };
            if (!pc_settings_p2d_button(x, y + (fh - icon) / 2 - 1, icon, *p)) {
                const char letter[2] = { *p, '\0' };
                pc_settings_p2d_text(x + (icon - pc_settings_p2d_text_width(letter, fw)) / 2, y, letter, c, fw, fh);
            }
            x += icon;
            continue;
        }
        if (n < int(sizeof(chunk)) - 1) chunk[n++] = *p;
    }
    sHelpLineY = y;
    sHelpLineH = fh;
    sHelpLineEnd = x;
}

// Span of the button icon for letter in the help line drawn last: from its icon to the next
// icon, or to the end of the line. False when that line drew no such icon.
bool helpLineIconSpan(char letter, int* x0, int* x1, int* y0, int* y1)
{
    for (int i = 0; i < sHelpIconCount; i++) {
        if (sHelpIcons[i].letter != letter) continue;
        *x0 = sHelpIcons[i].x;
        *x1 = i + 1 < sHelpIconCount ? sHelpIcons[i + 1].x : sHelpLineEnd;
        *y0 = sHelpLineY;
        *y1 = sHelpLineY + sHelpLineH;
        return true;
    }
    return false;
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
    // Sin caché propia: el registro de gsys ya la hace y se invalida cuando el
    // heap del nivel se resetea (un Texture* guardado aquí quedaba con la textura
    // GL liberada y los píxeles reutilizados).
    return zen::loadTexExp(pc_achievement_icon(id), true, true);
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
        if (sOpenGroup == PC_SET_GROUP_ACHIEVEMENTS) {
            // Progreso global de logros. La placa amarilla funciona como el
            // relleno y deja visible de un vistazo la proporción conseguida.
            const int unlocked = pc_achievements_unlocked_count();
            const int total = (int)PC_ACH_COUNT;
            const int barW = rw + 4;
            f1Plate(gfx, rx - 2, y, barW, kF1OptH - 2, 1);
            const int fillW = total > 0 ? barW * unlocked / total : 0;
            if (fillW > 0) f1Plate(gfx, rx - 2, y, fillW, kF1OptH - 2, 2);
            char progress[32];
            snprintf(progress, sizeof(progress), "%d/%d", unlocked, total);
            f1Text(rx + (rw - f1TextW(progress, 10)) / 2, y + 3, progress, kF1Section, 10, 15);
        } else {
            f1Plate(gfx, rx - 2, y, rw + 4, kF1OptH - 2, on ? 2 : 1);
            f1Text(rx + (rw - f1TextW(value, 10)) / 2, y + 3, value, on ? kF1SelDark : kF1Off, 10, 15);
        }
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

// El launcher deja Load/Textures/.activate al descargar un pack: se
// activa ese pack (su carpeta) y se borra el aviso. Al arrancar el juego y
// en las órdenes del launcher (--settings-dump...).
static void applyLauncherPackActivation() {
    const std::filesystem::path activate = std::filesystem::path("Load") / "Textures" / ".activate";
    std::string name;
    {
        std::ifstream in(activate);
        if (in) std::getline(in, name);
    }
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    if (!name.empty()) {
        sConfig.texturePack = name;
        sConfig.texturePackEnabled = 1;
        sPending = sConfig;
        saveConfig();
    }
    std::error_code activateEc;
    std::filesystem::remove(activate, activateEc);
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
    applyLauncherPackActivation();
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

int pc_settings_menu_is_open(void) { return sMenuOpen ? 1 : 0; }

bool pc_settings_consume_game_input(void) {
    const bool promptWasOpen = pc_erased_notice_active() || pc_speedrun_intro_active() || pc_randomizer_intro_active() || pc_newgame_prompt_active() || pc_playercount_prompt_active() || pc_devassign_prompt_active()
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
int  sNewGamePromptRow = 0;      // 0 = Difficulty, 1 = Game mode, 2 = Skip tutorial, 3 = Start
int  sNewGamePromptCol = 0;      // opción bajo el cursor en la fila con foco
int  sNewGamePromptRules = 0;    // 0 = standard file, 1 = permadeath
int  sNewGamePromptResult = PC_NEWGAME_PENDING;
bool sNewGamePromptHard = false;
bool sNewGamePromptSkipTutorial = false;
bool sTutorialSkipPending = false;
}

// Menú del modo Speedrun: Start Run / Best Times / How It Works. La
// explicación se abre sola la primera vez (speedrunIntroHidden = ya vista).
enum SrPage { SR_Menu, SR_How, SR_Times, SR_ConfirmReset, SR_Settings };

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
enum SrMenuRow {
    SRM_Start, SRM_Category, SRM_Practice, SRM_Times, SRM_How, SRM_Settings, SRM_Splits, SRM_ResetKey, SRM_ResetPad,
    SRM_LiveSplit, SRM_LsHideClock, kSrMenuItems
};
const char* const kSrMenuLabels[kSrMenuItems] = { "Start Run", "Category", "Practice", "Best Times", "How It Works",
                                                  "Settings", "Splits shown", "Reset key", "Reset button",
                                                  "LiveSplit", "Hide game clock" };
// Filas de cada página: la principal y Settings (ajustes del modo).
const int kSrMainRows[]     = { SRM_Start, SRM_Category, SRM_Practice, SRM_Times, SRM_How, SRM_Settings };
const int kSrSettingsRows[] = { SRM_Splits, SRM_ResetKey, SRM_ResetPad, SRM_LiveSplit, SRM_LsHideClock };
constexpr int kSrMenuRowH   = 44;

const int* srRows(int& count) {
    if (sSrPage == SR_Settings) {
        count = int(sizeof(kSrSettingsRows) / sizeof(kSrSettingsRows[0]));
        return kSrSettingsRows;
    }
    count = int(sizeof(kSrMainRows) / sizeof(kSrMainRows[0]));
    return kSrMainRows;
}
int srPanelH(int count) { return 22 + count * kSrMenuRowH + 60; }

// Captura de la tecla o el botón de reset (lo mismo que en Controls).
char   sSrExportMsg[96] = "";
Uint32 sSrExportMsgAt   = 0; // el aviso de la exportación dura unos segundos
int  sSrCapture     = 0;     // 0 = no, SRM_ResetKey o SRM_ResetPad
bool sSrCaptureWait = false; // esperar a soltar antes y después de capturar

void srSetResetKey(int sc) {
    sConfig.keyboardBindings[PC_KEY_ACT_SPEEDRUN_RESET] = sPending.keyboardBindings[PC_KEY_ACT_SPEEDRUN_RESET] = sc;
    pc_window_set_key_binding(PC_KEY_ACT_SPEEDRUN_RESET, static_cast<SDL_Scancode>(sc));
    saveConfig();
}
void srSetResetPad(int bind) {
    sConfig.gamepadBindings[PC_KEY_ACT_SPEEDRUN_RESET] = sPending.gamepadBindings[PC_KEY_ACT_SPEEDRUN_RESET] = bind;
    pc_window_set_gamepad_binding(PC_KEY_ACT_SPEEDRUN_RESET, bind);
    saveConfig();
}

// true mientras la captura consume la entrada. Esc cancela; Supr o
// Retroceso dejan la acción sin tecla o botón.
bool srPollCapture() {
    if (!sSrCapture && !sSrCaptureWait) return false;
    SDL_GameController* ctl = pc_window_get_controller();
    if (sSrCaptureWait) {
        if (!captureConfirmHeld(ctl) && !pc_window_gamepad_any_held(ctl)) sSrCaptureWait = false;
        return true;
    }
    if (keyWentDown(SDL_SCANCODE_ESCAPE)) {
        sSrCapture = 0;
        return true;
    }
    const bool clear = keyWentDown(SDL_SCANCODE_DELETE) || keyWentDown(SDL_SCANCODE_BACKSPACE);
    bool done = false;
    if (sSrCapture == SRM_ResetKey) {
        if (clear) {
            srSetResetKey(SDL_SCANCODE_UNKNOWN);
            done = true;
        }
        for (int sc = 1; sc < SDL_NUM_SCANCODES && !done; sc++) {
            if (!keyWentDown(static_cast<SDL_Scancode>(sc))) continue;
            srSetResetKey(sc);
            done = true;
        }
    } else {
        const int bind = clear ? -1 : pc_window_gamepad_first_held_binding(ctl);
        if (clear || bind >= 0) {
            srSetResetPad(bind);
            done = true;
        }
    }
    if (done) {
        sSrCapture     = 0;
        sSrCaptureWait = true;
    }
    return true;
}

void pcSpeedrunIntroInput() {
    if (!sSpeedrunIntroOpen) return;
    if (srPollCapture()) return;
    bool accept = menuOkKey()
               || (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    bool back   = menuCancelKey() || (sTouchFrameButtons & PAD_BUTTON_B) != 0;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
    bool xPress = keyWentDown(SDL_SCANCODE_X) || (sTouchFrameButtons & PAD_BUTTON_X) != 0;
    bool yPress = keyWentDown(SDL_SCANCODE_E) || (sTouchFrameButtons & PAD_BUTTON_Y) != 0;
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    if (sTouchTapPending) {
        sTouchTapPending = false;
        // En el menú, un toque sobre una opción la elige; en las páginas, vuelve.
        if (sSrPage == SR_Menu || sSrPage == SR_Settings) {
            int count = 0;
            srRows(count);
            const float y = sTouchTapY * 480.0f;
            const int row = int((y - (240.0f - srPanelH(count) / 2 + 22)) / kSrMenuRowH);
            if (row >= 0 && row < count) { sSrSel = row; accept = true; }
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
        if (padNavLeft(ctl)) left = true;
        if (padNavRight(ctl)) right = true;
        static bool sPadXWas = false;
        const bool padX = pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_X));
        if (padX && !sPadXWas) xPress = true;
        sPadXWas = padX;
        static bool sPadYWas = false;
        const bool padY = pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(PC_KEY_ACT_Y));
        if (padY && !sPadYWas) yPress = true;
        sPadYWas = padY;
    }

    switch (sSrPage) {
    case SR_Menu:
    case SR_Settings: {
        int count = 0;
        const int* rows = srRows(count);
        if (up)   sSrSel = (sSrSel + count - 1) % count;
        if (down) sSrSel = (sSrSel + 1) % count;
        const int row = rows[sSrSel];
        if (row == SRM_Category && (left || right || accept)) {
            // Categoría: izquierda/derecha o A la cambian, en círculo.
            int c = sConfig.speedrunCategory;
            c = (c + (left ? PC_SR_CAT_COUNT - 1 : 1)) % PC_SR_CAT_COUNT;
            sConfig.speedrunCategory = sPending.speedrunCategory = c;
            saveConfig();
        } else if (row == SRM_Practice && (left || right || accept)) {
            // Práctica: Off, día 1 y los días con punto de práctica.
            if (pc_speedrun_category_uses_days())
                pc_speedrun_set_practice_day(pc_speedrun_next_practice_day(pc_speedrun_practice_day(), left ? -1 : 1));
        } else if (row == SRM_Splits && (left || right || accept)) {
            // Splits visibles: izquierda/derecha cambian, A avanza (de 10 vuelve a 1).
            int n = sConfig.speedrunSplitsShown;
            n = left ? std::max(1, n - 1) : right ? std::min(10, n + 1) : n % 10 + 1;
            sConfig.speedrunSplitsShown = sPending.speedrunSplitsShown = n;
            saveConfig();
        } else if ((row == SRM_LiveSplit || row == SRM_LsHideClock) && (left || right || accept)) {
            int& v = row == SRM_LiveSplit ? sConfig.speedrunLiveSplit : sConfig.speedrunLsHideClock;
            v = !v;
            (row == SRM_LiveSplit ? sPending.speedrunLiveSplit : sPending.speedrunLsHideClock) = v;
            saveConfig();
        } else if ((row == SRM_ResetKey || row == SRM_ResetPad) && accept) {
            sSrCapture     = row;
            sSrCaptureWait = true;
        } else if (accept) {
            if (row == SRM_Start) {
                sSpeedrunIntroOpen   = false;
                sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_CONTINUE;
            } else if (row == SRM_Settings) {
                sSrPage = SR_Settings;
                sSrSel  = 0;
            } else {
                sSrPage = row == SRM_Times ? SR_Times : SR_How;
            }
        } else if (back && sSrPage == SR_Settings) {
            // Settings vuelve a la página principal, con Settings marcado.
            sSrPage = SR_Menu;
            sSrSel  = 5;
        } else if (back) {
            sSpeedrunIntroOpen   = false;
            sSpeedrunIntroResult = PC_SPEEDRUN_INTRO_BACK;
        }
        break;
    }
    case SR_How:
        if (accept || back) sSrPage = SR_Menu;
        break;
    case SR_Times:
        if (yPress) {
            // Exportar los splits a LiveSplit; el resultado sale en la página.
            char name[64];
            if (pc_speedrun_export_lss(name, sizeof(name)))
                snprintf(sSrExportMsg, sizeof(sSrExportMsg), "Saved save/speedrun/%s", name);
            else
                snprintf(sSrExportMsg, sizeof(sSrExportMsg), "Nothing to export: finish a run first");
            sSrExportMsgAt = SDL_GetTicks();
        } else if (xPress && pc_speedrun_has_pb()) sSrPage = SR_ConfirmReset;
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
    // Reglas de speedrun.com de la categoría elegida.
    static const char* const kRules[PC_SR_CAT_COUNT][3] = {
        { "All Parts: Start Run begins a new game. The timer",
          "starts then and stops when the Secret Safe is",
          "collected. One split per day." },
        { "Low-Pikmin: All Parts, but the Onions may produce",
          "at most " PC_SR_SPROUT_LIMIT_TEXT " sprouts. Going over voids the run.",
          "One split per day." },
        { "5 Parts: play Day 1 and save (25 Red Leaf Pikmin).",
          "Later runs load that race file. The timer starts on",
          "entering Forest of Hope; one split per part." },
        { "200 Pikmin Challenge: all five stages are open. The",
          "timer starts on the first stage and stops leaving the",
          "last one with 200 in all five. One split per stage." },
    };
    const char* const* rules = kRules[pc_speedrun_category()];
    const char* lines[6] = { "Pikmin exactly as it shipped on GameCube.",
                             "No gameplay changes and no control upgrades:",
                             "every mod, cheat and port option that alters play is off.",
                             rules[0], rules[1], rules[2] };
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
    char header[48];
    snprintf(header, sizeof(header), "Best Times: %s", pc_speedrun_category_name(pc_speedrun_category()));
    drawPikminHeader(gfx, panelX, panelY, panelW, header);
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
        if (pc_speedrun_category_uses_days()) snprintf(buf, sizeof(buf), "%d days   %s", pc_speedrun_pb_days(), pc_speedrun_pb_date());
        else snprintf(buf, sizeof(buf), "%s", pc_speedrun_pb_date());
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
            if (pc_speedrun_category_uses_days()) snprintf(buf, sizeof(buf), "%s   %dd   %s", t, days, date);
            else snprintf(buf, sizeof(buf), "%s   %s", t, date);
            srText(lx + (i % 2) * colW, panelY + 274 + (i / 2) * 18, buf, kBody, 9, 14);
        }
    }

    if (confirm) {
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 36, "Erase all records?    A: erase    B: keep",
                     Colour(255, 160, 120, 255));
    } else if (sSrExportMsg[0] && SDL_GetTicks() - sSrExportMsgAt < 4000) {
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 36, sSrExportMsg, Colour(255, 205, 60, 255));
    } else {
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 36,
                     pc_speedrun_has_pb() ? "B: back    X: erase records    Y / E: export to LiveSplit"
                                          : "B: back    Y / E: export to LiveSplit",
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

    int count = 0;
    const int* rows = srRows(count);
    const int panelW = 560, panelH = srPanelH(count);
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, sSrPage == SR_Settings ? "Speedrun Settings" : "Speedrun");
    for (int r = 0; r < count; r++) {
        const int i = rows[r];
        char label[64];
        if (i == SRM_Start && pc_speedrun_practice_day() > 0)
            snprintf(label, sizeof(label), "Start Practice");
        else if (i == SRM_Practice && !pc_speedrun_category_uses_days())
            snprintf(label, sizeof(label), "%s: n/a", kSrMenuLabels[i]);
        else if (i == SRM_Practice && pc_speedrun_practice_day() == 0)
            snprintf(label, sizeof(label), "%s: < Off >", kSrMenuLabels[i]);
        else if (i == SRM_Practice)
            snprintf(label, sizeof(label), "%s: < Day %d >", kSrMenuLabels[i], pc_speedrun_practice_day());
        else if (i == SRM_Category)
            snprintf(label, sizeof(label), "%s: < %s >", kSrMenuLabels[i], pc_speedrun_category_name(pc_speedrun_category()));
        else if (i == SRM_Splits)
            snprintf(label, sizeof(label), "%s: < %d >", kSrMenuLabels[i], sConfig.speedrunSplitsShown);
        else if (i == sSrCapture)
            snprintf(label, sizeof(label), "%s", i == SRM_ResetKey ? "Press a key..." : "Press a button...");
        else if (i == SRM_ResetKey)
            snprintf(label, sizeof(label), "%s: %s", kSrMenuLabels[i],
                     pc_window_binding_name(sConfig.keyboardBindings[PC_KEY_ACT_SPEEDRUN_RESET]));
        else if (i == SRM_ResetPad)
            snprintf(label, sizeof(label), "%s: %s", kSrMenuLabels[i],
                     pc_window_get_gamepad_button_name(sConfig.gamepadBindings[PC_KEY_ACT_SPEEDRUN_RESET]));
        else if (i == SRM_LiveSplit)
            snprintf(label, sizeof(label), "%s: < %s >", kSrMenuLabels[i],
                     !sConfig.speedrunLiveSplit ? "Off" : pc_livesplit_connected() ? "Connected" : "Waiting");
        else if (i == SRM_LsHideClock)
            snprintf(label, sizeof(label), "%s: < %s >", kSrMenuLabels[i], sConfig.speedrunLsHideClock ? "On" : "Off");
        else snprintf(label, sizeof(label), "%s", kSrMenuLabels[i]);
        drawGlassOption(panelX + 40, panelY + 22 + r * kSrMenuRowH, panelW - 80, kSrMenuRowH - 4, label, r == sSrSel, 18, 26);
    }
    const char* help = sSrCapture ? "Hold it 1 s in game to reset    Esc: cancel    Del: none"
                                  : "A / Enter: select    B / Esc: back";
    drawHelpLine(panelX + panelW / 2, panelY + panelH - 40, help, Colour(150, 165, 195, 255));
}

// ─── Randomizer: explicación y ajustes ───
// Se abre al elegir Randomizer en el título, antes del capitán y los slots.
// Dos páginas: la explicación (A sigue, B vuelve al título) y "Randomizer
// Settings" (flechas para cambiar, Start sigue, B vuelve a la explicación).

namespace {
bool sRandomizerIntroOpen   = false;
int  sRandomizerIntroResult = PC_RANDOMIZER_INTRO_PENDING;
int  sRndPage = 0; // 0 = explicación, 1 = ajustes
int  sRndRow  = 0;

// Filas de ajustes. Cada una tiene sus valores y una línea de ayuda por valor.
enum RndRow {
    RND_Parts, RND_Enemies, RND_Bosses, RND_Mix, RND_Wisps, RND_Final, RND_Daily, RND_Share, RND_NewSeed,
    RND_Start, RND_RowCount
};
const char* const kRndRowTitles[RND_RowCount] = {
    "Ship parts", "Enemies", "Bosses", "Bosses <-> enemies", "Honeywisps & Flint Beetles",
    "Final boss", "Daily layout", "Seed code", "", "",
};

bool sRndTextWasOn = false;

// Código compartido (semilla + ajustes): campo de texto libre aparte, porque
// lleva letras que el editor de la semilla no acepta.
bool sShareEditing = false;
Uint32 sShareErrorUntil = 0; ///< aviso de código no válido, tras salir de la edición
char sShareEdit[32];
int  sShareLen     = 0;

/// Si el texto es un código compartido válido, aplica semilla y ajustes.
bool rndApplyShareCode(const char* text) {
    uint64_t seed  = 0;
    uint32_t flags = 0;
    if (!pc_randomizer_parse_share_code(text, &seed, &flags)) return false;
    pc_randomizer_set_pending_seed(seed);
    pc_randomizer_set_pending_flags(flags);
    return true;
}

void rndCopyShareCode() {
    char code[PC_RANDOMIZER_SHARE_CODE_LEN];
    pc_randomizer_format_share_code(pc_randomizer_pending_seed(), pc_randomizer_pending_flags(), code, sizeof(code));
    SDL_SetClipboardText(code);
}

void rndTextInputOn() {
    // En Android abre el teclado virtual; en escritorio solo activa los
    // eventos de texto. Se deja como estaba al salir.
    sRndTextWasOn = SDL_IsTextInputActive() == SDL_TRUE;
    if (!sRndTextWasOn) SDL_StartTextInput();
}

void rndShareBegin() {
    sShareLen     = 0;
    sShareEdit[0] = '\0';
    sShareEditing = true;
    rndTextInputOn();
}

void rndShareEnd() {
    sShareEditing = false;
    if (!sRndTextWasOn) SDL_StopTextInput();
}

void rndShareType(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 32);
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '-';
    if (!ok || sShareLen >= int(sizeof(sShareEdit)) - 1) return;
    sShareEdit[sShareLen++] = c;
    sShareEdit[sShareLen]   = '\0';
}

/// Confirma. Un código no válido no bloquea: se sale de la edición, vuelve
/// el código que había y se avisa unos segundos.
void rndShareCommit() {
    if (sShareLen > 0 && !rndApplyShareCode(sShareEdit)) sShareErrorUntil = SDL_GetTicks() + 3000;
    rndShareEnd();
}

bool rndShareErrorShown() { return SDL_GetTicks() < sShareErrorUntil; }

int rndValueCount(int row) {
    return (row == RND_Enemies || row == RND_Bosses) ? 3 : 2;
}

int rndValue(int row) {
    const uint32_t f = pc_randomizer_pending_flags();
    switch (row) {
    case RND_Parts:   return (f & PC_RND_ShuffleParts) ? 1 : 0;
    case RND_Enemies: return int(pc_rnd_enemy_mode(f));
    case RND_Bosses:  return int(pc_rnd_boss_mode(f));
    case RND_Mix:     return (f & PC_RND_BossEnemyMix) ? 1 : 0;
    case RND_Wisps:   return (f & PC_RND_IncludeWispsFlint) ? 1 : 0;
    case RND_Final:   return (f & PC_RND_RandomFinalBoss) ? 1 : 0;
    case RND_Daily:   return (f & PC_RND_DailyLayout) ? 1 : 0;
    default:          return 0;
    }
}

void rndSetValue(int row, int v) {
    uint32_t f = pc_randomizer_pending_flags();
    auto bit = [&](uint32_t b) { f = v ? (f | b) : (f & ~b); };
    switch (row) {
    case RND_Parts:   bit(PC_RND_ShuffleParts); break;
    case RND_Enemies: f = pc_rnd_with_enemy_mode(f, uint32_t(v)); break;
    case RND_Bosses:  f = pc_rnd_with_boss_mode(f, uint32_t(v)); break;
    case RND_Mix:     bit(PC_RND_BossEnemyMix); break;
    case RND_Wisps:   bit(PC_RND_IncludeWispsFlint); break;
    case RND_Final:   bit(PC_RND_RandomFinalBoss); break;
    case RND_Daily:   bit(PC_RND_DailyLayout); break;
    default: break;
    }
    pc_randomizer_set_pending_flags(f);
}

const char* rndValueLabel(int row, int v) {
    switch (row) {
    case RND_Parts:   return v ? "Shuffled" : "Vanilla";
    case RND_Enemies:
    case RND_Bosses:  return v == 2 ? "Chaos" : v == 1 ? "Shuffled" : "Vanilla";
    case RND_Mix:     return v ? "Yes" : "No";
    case RND_Wisps:   return v ? "Randomized" : "Vanilla";
    case RND_Final:   return v ? "Randomized" : "Fixed";
    case RND_Daily:   return v ? "Changes each day" : "Same every day";
    default:          return "";
    }
}

const char* rndHelp(int row, int v) {
    switch (row) {
    case RND_Parts:
        return v ? "The 29 remaining ship parts swap places. Each zone keeps its count."
                 : "Every ship part is where it always was.";
    case RND_Enemies:
        return v == 2 ? "Any enemy can appear anywhere. Expect the unexpected."
             : v == 1 ? "Enemies are replaced by others of the same kind."
                      : "Regular enemies are left as they are.";
    case RND_Bosses:
        return v == 2 ? "Any boss can appear in any boss arena."
             : v == 1 ? "Bosses trade arenas with each other."
                      : "Bosses are left as they are.";
    case RND_Mix:
        return v ? "Bosses may replace regular enemies, and enemies may replace bosses."
                 : "Bosses and regular enemies are shuffled separately.";
    case RND_Wisps:
        return v ? "Include the many Honeywisps and Flint Beetles. Much harder."
                 : "Honeywisps and Flint Beetles stay as they are.";
    case RND_Final:
        return v ? "The Final Trial boss is randomized too. Experimental."
                 : "The Emperor Bulblax keeps the Final Trial.";
    case RND_Daily:
        return v ? "Respawning enemies are re-rolled each day. Parts never move."
                 : "Each spot keeps the same replacement for the whole run.";
    case RND_Share:
        return "This world and its settings in one code. A: enter a friend's. Ctrl+C: copy.";
    case RND_NewSeed:
        return "Roll a new random seed.";
    default:
        return "Next: difficulty and game mode. The tutorial is always skipped.";
    }
}

// Geometría compartida por el dibujo y los toques.
const int kRndPanelW = 640;
const int kRndPanelH = 466;
const int kRndRowH   = 32;
const int kRndValueW = 250;
int rndRowY(int panelY, int row) { return panelY + 30 + row * kRndRowH + (row == RND_Start ? 6 : 0); }
int rndValueX(int panelX) { return panelX + kRndPanelW - 40 - kRndValueW; }
}

void pc_settings_text_input(const char* text) {
    if (!sRandomizerIntroOpen || !text) return;
    if (sShareEditing) {
        for (const char* p = text; *p; ++p) rndShareType(*p);
    }
}

// Ajustes de una partida nueva: se abren al elegir una ranura vacía desde el
// menú Randomizer. Las partidas ya creadas usan los suyos, guardados en el
// archivo. Cada partida nueva empieza con una semilla nueva.
void pc_randomizer_settings_open(void) {
    sRandomizerIntroOpen   = true;
    sRandomizerIntroResult = PC_RANDOMIZER_INTRO_PENDING;
    sRndPage               = 1;
    sRndRow                = RND_Start;
    if (sShareEditing) rndShareEnd();
    pc_randomizer_reroll_pending_seed();
    pc_menu_edge_reset();
}

void pc_randomizer_intro_open(void) {
    sRandomizerIntroOpen   = true;
    sRandomizerIntroResult = PC_RANDOMIZER_INTRO_PENDING;
    sRndPage               = 0;
    sRndRow                = 0;
    pc_menu_edge_reset();
}

bool pc_randomizer_intro_active(void) { return sRandomizerIntroOpen; }

int pc_randomizer_intro_result(void) { return sRandomizerIntroResult; }

namespace {
void pcRandomizerIntroInput() {
    if (!sRandomizerIntroOpen) return;
    if (sRndPage == 1 && sShareEditing) {
        // Igual que la semilla: Enter/Esc en el teclado, A/B en el mando.
        bool ok     = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_KP_ENTER)
                   || keyWentDown(SDL_SCANCODE_SPACE) || menuAcceptClick();
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE);
        SDL_GameController* ctl = pc_window_get_controller();
        if (sTouchTapPending) {
            sTouchTapPending = false;
            ok = true;
        }
        if (ctl) {
            if (promptPadA(ctl)) ok = true;
            if (promptPadB(ctl)) cancel = true;
        }
        if ((sTouchFrameButtons & PAD_BUTTON_A) != 0) ok = true;
        if ((sTouchFrameButtons & PAD_BUTTON_B) != 0) cancel = true;
        if ((SDL_GetModState() & KMOD_CTRL) && keyWentDown(SDL_SCANCODE_V)) {
            char* clip = SDL_GetClipboardText();
            sShareLen  = 0;
            sShareEdit[0] = '\0';
            for (const char* q = clip ? clip : ""; *q; ++q) rndShareType(*q);
            SDL_free(clip);
            rndShareCommit(); // pegar un código completo lo aplica al momento
        } else if (keyWentDown(SDL_SCANCODE_BACKSPACE)) {
            if (sShareLen > 0) sShareEdit[--sShareLen] = '\0';
        }
        if (ok) rndShareCommit();
        else if (cancel) rndShareEnd();
        return;
    }
    bool accept = menuOkKey() || (sTouchFrameButtons & PAD_BUTTON_A) != 0;
    bool back   = menuCancelKey() || (sTouchFrameButtons & PAD_BUTTON_B) != 0;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W) || (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S) || (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A) || (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D) || (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    if (sTouchTapPending) {
        sTouchTapPending = false;
        if (sRndPage == 0) {
            accept = true; // un toque en la explicación sigue adelante
        } else {
            int dw = 0, dh = 0;
            pc_gfx_get_drawable_size(&dw, &dh);
            const float aspect  = dh > 0 ? float(dw) / float(dh) : 4.0f / 3.0f;
            const float screenW = aspect * 480.0f;
            const float x = sTouchTapX * screenW, y = sTouchTapY * 480.0f;
            const int panelX = int(screenW * 0.5f) - kRndPanelW / 2, panelY = 240 - kRndPanelH / 2;
            for (int row = 0; row < RND_RowCount; row++) {
                const float ry = float(rndRowY(panelY, row));
                if (y < ry || y > ry + kRndRowH - 4) continue;
                sRndRow = row;
                if (row == RND_Start || row == RND_Share || row == RND_NewSeed) {
                    accept = true;
                } else {
                    // Mitad izquierda del valor: anterior; derecha: siguiente.
                    const float vx = float(rndValueX(panelX));
                    if (x >= vx && x < vx + kRndValueW / 2) left = true;
                    else if (x >= vx + kRndValueW / 2) right = true;
                }
            }
        }
    }
    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (promptPadA(ctl)) accept = true;
        if (promptPadB(ctl)) back = true;
        if (padNavUp(ctl)) up = true;
        if (padNavDown(ctl)) down = true;
        if (padNavLeft(ctl)) left = true;
        if (padNavRight(ctl)) right = true;
    }

    if (sRndPage == 0) {
        if (accept) {
            sRandomizerIntroOpen   = false;
            sRandomizerIntroResult = PC_RANDOMIZER_INTRO_CONTINUE;
        } else if (back) {
            sRandomizerIntroOpen   = false;
            sRandomizerIntroResult = PC_RANDOMIZER_INTRO_BACK;
        }
        return;
    }

    if (up)   sRndRow = (sRndRow + RND_RowCount - 1) % RND_RowCount;
    if (down) sRndRow = (sRndRow + 1) % RND_RowCount;
    if ((left || right) && sRndRow < RND_Share) {
        const int n = rndValueCount(sRndRow);
        rndSetValue(sRndRow, (rndValue(sRndRow) + (right ? 1 : n - 1)) % n);
    }
    // Ctrl+C copia el código compartido (semilla + ajustes); Ctrl+V pega un
    // código compartido (aplica ambos) o, si no lo es, una semilla.
    const bool seedRows = sRndRow == RND_Share;
    if (seedRows && (SDL_GetModState() & KMOD_CTRL) && keyWentDown(SDL_SCANCODE_C)) {
        rndCopyShareCode();
    }
    if (seedRows && (SDL_GetModState() & KMOD_CTRL) && keyWentDown(SDL_SCANCODE_V)) {
        char* clip = SDL_GetClipboardText();
        if (clip && *clip && !rndApplyShareCode(clip)) sShareErrorUntil = SDL_GetTicks() + 3000;
        SDL_free(clip);
        return;
    }
    if (accept) {
        if (sRndRow == RND_Share) {
            rndShareBegin();
        } else if (sRndRow == RND_NewSeed) {
            pc_randomizer_reroll_pending_seed();
        } else if (sRndRow == RND_Start) {
            sRandomizerIntroOpen   = false;
            sRandomizerIntroResult = PC_RANDOMIZER_INTRO_CONTINUE;
        } else {
            const int n = rndValueCount(sRndRow);
            rndSetValue(sRndRow, (rndValue(sRndRow) + 1) % n);
        }
    } else if (back) {
        // Vuelve a la selección de ranura sin crear nada.
        sRandomizerIntroOpen   = false;
        sRandomizerIntroResult = PC_RANDOMIZER_INTRO_BACK;
    }
}

// Texto P2D que se encoge hasta caber en maxW (las descripciones largas no
// deben salirse del panel). center: x es el centro.
void rndText(int x, int y, const char* t, Colour c, int fw, int maxW, bool center) {
    while (fw > 9 && pc_settings_p2d_text_width(t, fw) > maxW) fw--;
    const int fh = fw * 3 / 2;
    if (center) x -= pc_settings_p2d_text_width(t, fw) / 2;
    srText(x, y, t, c, fw, fh);
}

void drawRndIntro(DGXGraphics* gfx, int screenW, int screenH) {
    const int panelW = 620, panelH = 360;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2 + 10;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Randomizer");

    const Colour kGold(255, 214, 90, 255), kBody(232, 238, 252, 255), kDim(176, 192, 222, 255);
    rndText(panelX + panelW / 2, panelY + 34, "Every run is a brand new expedition.", kGold, 16, panelW - 60, true);

    // Una idea por línea, con viñeta: qué se baraja, cómo se elige, la
    // semilla, cómo empieza y dónde se guarda.
    struct Bullet { const char* text; const char* sub; };
    const Bullet bullets[] = {
        { "Enemies, bosses and ship parts get shuffled.", "Choose how wild it gets when you start a new file." },
        { "Same seed and settings, same world.", "Share your seed to race a friend." },
        { "No tutorial: start on Day 2.", "With the Main Engine and 20 red Pikmin." },
        { "Separate saves.", "Randomizer files never touch your normal ones." },
    };
    const int textX = panelX + 64, textW = panelW - 64 - 36;
    for (int i = 0; i < 4; i++) {
        const int y = panelY + 82 + i * 58;
        fillRoundRectGrad(gfx, panelX + 40, y + 5, 10, 10, 5, Colour(255, 236, 140, 255), Colour(220, 160, 40, 255));
        rndText(textX, y, bullets[i].text, kBody, 13, textW, false);
        rndText(textX, y + 23, bullets[i].sub, kDim, 11, textW, false);
    }
    drawHelpLine(panelX + panelW / 2, panelY + panelH - 34, "A / Enter: continue    B / Esc: back",
                 Colour(150, 165, 195, 255));
}

void drawRndSettings(DGXGraphics* gfx, int screenW, int screenH) {
    const int panelX = screenW / 2 - kRndPanelW / 2, panelY = screenH / 2 - kRndPanelH / 2;
    drawPikminPanel(gfx, panelX, panelY, kRndPanelW, kRndPanelH, 22);
    drawPikminHeader(gfx, panelX, panelY, kRndPanelW, "Randomizer Settings");

    const Colour kFocus(255, 229, 120, 255), kLabel(190, 200, 220, 255), kEdge(8, 12, 28, 255);
    const int vx = rndValueX(panelX);
    for (int row = 0; row < RND_RowCount; row++) {
        const bool focus = row == sRndRow;
        const int y      = rndRowY(panelY, row);
        if (row == RND_Start) {
            const int bw = 230, bx = panelX + kRndPanelW / 2 - bw / 2;
            if (focus) drawTextOutline(bx - 18, y + 8, ">", kFocus, kEdge);
            drawGlassOption(bx, y, bw, kRndRowH - 4, "Continue", focus, 18, 26);
            continue;
        }
        if (row == RND_Share) {
            char share[40];
            if (sShareEditing) {
                std::snprintf(share, sizeof(share), "%s|", sShareEdit);
            } else {
                pc_randomizer_format_share_code(pc_randomizer_pending_seed(), pc_randomizer_pending_flags(), share,
                                                sizeof(share));
            }
            if (focus) drawTextOutline(panelX + 22, y + 8, ">", kFocus, kEdge);
            rndText(panelX + 40, y + 7, kRndRowTitles[row], focus ? kFocus : kLabel, 13, vx - panelX - 50, false);
            drawGlassOption(vx, y, kRndValueW, kRndRowH - 4, share, focus, 11, 17);
            continue;
        }
        if (row == RND_NewSeed) {
            if (focus) drawTextOutline(vx - 18, y + 8, ">", kFocus, kEdge);
            drawGlassOption(vx, y, kRndValueW, kRndRowH - 4, "New random seed", focus, 13, 20);
            continue;
        }
        if (focus) drawTextOutline(panelX + 22, y + 8, ">", kFocus, kEdge);
        rndText(panelX + 40, y + 7, kRndRowTitles[row], focus ? kFocus : kLabel, 13, vx - panelX - 50, false);
        char value[48];
        // "< valor >" muestra que se cambia con las flechas.
        std::snprintf(value, sizeof(value), "<  %s  >", rndValueLabel(row, rndValue(row)));
        drawGlassOption(vx, y, kRndValueW, kRndRowH - 4, value, focus, 15, 22);
    }

    const char* help = sShareEditing         ? "Type or paste (Ctrl+V) a friend's code: RND-XXXXXX-XXXXXX-XXXXXX"
                     : rndShareErrorShown() ? "That code is not valid, so your code was kept."
                                            : rndHelp(sRndRow, sRndRow < RND_Share ? rndValue(sRndRow) : 0);
    rndText(panelX + kRndPanelW / 2, panelY + kRndPanelH - 64, help, Colour(232, 238, 252, 255), 12, kRndPanelW - 60, true);
    drawHelpLine(panelX + kRndPanelW / 2, panelY + kRndPanelH - 34,
                 sShareEditing ? "Enter / A: confirm    Esc / B: cancel"
                             : "Arrows: change    A / Enter: select    B / Esc: back",
                 Colour(150, 165, 195, 255));
}
}

void pc_randomizer_intro_draw(void) {
    if (!sRandomizerIntroOpen || !gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    if (sRndPage == 0) drawRndIntro(gfx, screenW, screenH);
    else drawRndSettings(gfx, screenW, screenH);
}

// ─── Semilla en el menú de pausa ───
// En una partida Randomizer, bajo el recuadro de la pausa: la semilla y un
// resumen de los ajustes, que es lo que hace falta para compartir el mundo.

void pc_randomizer_pause_update(void) {
    if (!pc_randomizer_active()) return;
    // Ctrl+C copia el código. Flanco propio: la pausa no pasa por el
    // latch de teclas de los prompts.
    static bool sWasDown = false;
    const Uint8* keys    = SDL_GetKeyboardState(nullptr);
    const bool down      = keys && keys[SDL_SCANCODE_C] && (SDL_GetModState() & KMOD_CTRL);
    if (down && !sWasDown) {
        char code[PC_RANDOMIZER_SHARE_CODE_LEN];
        pc_randomizer_format_share_code(pc_randomizer_rules().seed, pc_randomizer_rules().flags, code, sizeof(code));
        SDL_SetClipboardText(code);
    }
    sWasDown = down;
}

void pc_randomizer_pause_draw(void) {
    if (!pc_randomizer_active() || !gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const PcRandomizerRules& rules = pc_randomizer_rules();
    char line[96], summary[128];
    char share[PC_RANDOMIZER_SHARE_CODE_LEN];
    pc_randomizer_format_share_code(rules.seed, rules.flags, share, sizeof(share));
    std::snprintf(line, sizeof(line), "%s", share);

    static const char* const kModes[] = { "Vanilla", "Shuffled", "Chaos", "?" };
    std::snprintf(summary, sizeof(summary), "Parts %s  -  Enemies %s  -  Bosses %s%s%s%s%s",
                  (rules.flags & PC_RND_ShuffleParts) ? "Shuffled" : "Vanilla", kModes[pc_rnd_enemy_mode(rules.flags)],
                  kModes[pc_rnd_boss_mode(rules.flags)], (rules.flags & PC_RND_BossEnemyMix) ? "  +Mix" : "",
                  (rules.flags & PC_RND_IncludeWispsFlint) ? "  +Wisps" : "",
                  (rules.flags & PC_RND_RandomFinalBoss) ? "  +Final" : "",
                  (rules.flags & PC_RND_DailyLayout) ? "  +Daily" : "");

    const int panelW = 460, panelH = 74;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH - panelH - 18;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 16);
    rndText(panelX + panelW / 2, panelY + 10, line, Colour(255, 214, 90, 255), 15, panelW - 30, true);
    rndText(panelX + panelW / 2, panelY + 36, summary, Colour(214, 224, 245, 255), 10, panelW - 30, true);
    rndText(panelX + panelW / 2, panelY + 54, "Ctrl+C: copy code", Colour(150, 165, 195, 255), 9, panelW - 30, true);
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
    bool accept = menuOkKey()
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
    sNewGamePromptRow    = 0;
    sNewGamePromptCol    = 0;
    sNewGamePromptRules  = 0;
    sNewGamePromptResult = PC_NEWGAME_PENDING;
    sNewGamePromptHard   = false;
    // Randomizer: el tutorial se salta siempre (la fila queda bloqueada).
    sNewGamePromptSkipTutorial = pc_randomizer_menu_pending();
    pc_menu_edge_reset();
}

bool pc_newgame_prompt_active(void) { return sNewGamePromptOpen; }

int pc_newgame_prompt_result(void) { return sNewGamePromptResult; }

bool pc_newgame_prompt_chose_hard(void) { return sNewGamePromptHard; }

bool pc_newgame_prompt_chose_skip_tutorial(void) { return sNewGamePromptSkipTutorial; }

void pc_tutorial_skip_set_pending(bool on) { sTutorialSkipPending = on; }
bool pc_tutorial_skip_pending(void) { return sTutorialSkipPending; }
void pc_tutorial_skip_clear_pending(void) { sTutorialSkipPending = false; }

namespace {
// Geometría compartida por el dibujo y los toques: tres filas de dos
// opciones (Difficulty / Game mode / Skip tutorial) y, debajo, el botón Start.
// Las flechas mueven el cursor, A elige la opción bajo él y solo Start empieza.
// The numbers live in pc_prompt_pointer so the mouse hit test uses the same boxes.
const int kNgPanelW = kPcNewGamePanelW;
const int kNgPanelH = kPcNewGamePanelH;
const int kNgBoxW   = kPcNewGameBoxW;
const int kNgBoxH   = kPcNewGameBoxH;
const int kNgRowCount = kPcNewGameRows;
const int kNgStartRow = kNgRowCount; // fila del botón Start
int ngRowY(int panelY, int row) { return pc_newgame_row_y(panelY, row); }
int ngStartX(int panelX) { return pc_newgame_start_x(panelX); }
int ngBoxX(int panelX, int i) { return pc_newgame_box_x(panelX, i); }

// En una partida Randomizer la fila 2 (Skip tutorial) queda fija en Yes: el
// tutorial depende de generadores y una pieza que el barajado cambia.
bool ngSeedRow(int row) { return row == 2 && pc_randomizer_menu_pending(); }

int ngRowValue(int row) {
    if (row == 0) return sNewGamePromptHard ? 1 : 0;
    if (row == 1) return sNewGamePromptRules;
    if (ngSeedRow(row)) return 1;
    return sNewGamePromptSkipTutorial ? 1 : 0;
}

void ngSetRowValue(int row, int v) {
    if (row == 0) sNewGamePromptHard = v != 0;
    else if (row == 1) sNewGamePromptRules = v;
    else if (ngSeedRow(row)) { (void)v; }
    else               sNewGamePromptSkipTutorial = v != 0;
}

// Mouse on the New Game panel. Motion over an option or Start moves the cursor there. A click
// on an option chooses it and returns 1; a click on Start returns 2. Empty space and the
// locked Randomizer row do nothing. The claim keeps the click from also being A.
int ngPointer() {
    pc_menu_click_claim_pointer_menu();
    DGXGraphics* gfx = (gsys && gsys->mDGXGfx) ? static_cast<DGXGraphics*>(gsys->mDGXGfx) : nullptr;
    if (!gfx) return 0;
    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    PcMenuClickRect rects[PC_NEWGAME_PTR_COUNT];
    pc_newgame_pointer_rects(screenW, screenH, rects);
    int active[PC_NEWGAME_PTR_COUNT];
    for (int i = 0; i < PC_NEWGAME_PTR_COUNT; ++i) active[i] = (i < PC_NEWGAME_PTR_START && ngSeedRow(i / 2)) ? 0 : 1;
    float x = 0.0f, y = 0.0f;
    if (pc_gfx_menu_graph_point(false, &x, &y)) {
        const PcMenuClickResult hover
            = pc_menu_click_hover(PC_MENU_CLICK_MAIN, x, y, rects, active, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
        if (hover.index == PC_NEWGAME_PTR_START) {
            sNewGamePromptRow = kNgStartRow;
        } else if (hover.index >= 0) {
            sNewGamePromptRow = hover.index / 2;
            sNewGamePromptCol = hover.index % 2;
        }
    }
    if (pc_gfx_menu_graph_point(true, &x, &y)) {
        const PcMenuClickResult hit
            = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, x, y, rects, active, PC_NEWGAME_PTR_COUNT, kPcMenuClickMargin);
        if (hit.action == PC_MENU_CLICK_ACTIVATE) {
            if (hit.index == PC_NEWGAME_PTR_START) {
                sNewGamePromptRow = kNgStartRow;
                return 2;
            }
            sNewGamePromptRow = hit.index / 2;
            sNewGamePromptCol = hit.index % 2;
            ngSetRowValue(sNewGamePromptRow, sNewGamePromptCol);
            return 1;
        }
    }
    return 0;
}

void pcNewGamePromptInput() {
    if (!sNewGamePromptOpen) return;

    bool left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool up     = keyWentDown(SDL_SCANCODE_UP)    || keyWentDown(SDL_SCANCODE_W);
    bool down   = keyWentDown(SDL_SCANCODE_DOWN)  || keyWentDown(SDL_SCANCODE_S);
    // The boxes hit-test the left click (ngPointer), so a click is not also A on the cursor.
    bool accept = menuOkKeyNoClick();
    bool cancel = menuCancelKey();
    cancel |= pc_menu_click_take_back(); // right click: back

    left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
    right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
    up |= (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
    down |= (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
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
        const int panelX = int(screenW * 0.5f) - kNgPanelW / 2;
        const int panelY = 240 - kNgPanelH / 2;
        // Un toque elige la opción; el de Start empieza.
        for (int row = 0; row < kNgRowCount; ++row) {
            const float optY = float(ngRowY(panelY, row));
            for (int i = 0; i < 2; ++i) {
                const float boxX = float(ngBoxX(panelX, i));
                if (x >= boxX && x <= boxX + kNgBoxW && y >= optY - 8.0f && y <= optY + kNgBoxH + 8.0f) {
                    sNewGamePromptRow = row;
                    sNewGamePromptCol = i;
                    ngSetRowValue(row, i);
                }
            }
        }
        const float startY = float(ngRowY(panelY, kNgStartRow));
        const float startX = float(ngStartX(panelX));
        if (x >= startX && x <= startX + kNgBoxW && y >= startY - 8.0f && y <= startY + kNgBoxH + 8.0f) {
            sNewGamePromptRow = kNgStartRow;
            accept            = true;
        }
    }

    SDL_GameController* ctl = pc_window_get_controller();
    if (ctl) {
        if (padNavLeft(ctl))  left   = true;
        if (padNavRight(ctl)) right  = true;
        if (padNavUp(ctl))    up     = true;
        if (padNavDown(ctl))  down   = true;
        if (promptPadA(ctl))  accept = true;
        if (promptPadB(ctl))  cancel = true;
    }
    if (ngPointer() == 2) accept = true; // Start was clicked; an option click already chose it

    const int rows = kNgRowCount + 1; // opciones + Start
    if (up || down) {
        sNewGamePromptRow = (sNewGamePromptRow + (up ? rows - 1 : 1)) % rows;
        // El cursor entra en la fila sobre la opción ya elegida.
        if (sNewGamePromptRow < kNgRowCount) sNewGamePromptCol = ngRowValue(sNewGamePromptRow);
    }
    if ((left || right) && sNewGamePromptRow < kNgRowCount) sNewGamePromptCol = sNewGamePromptCol ? 0 : 1;

    if (accept) {
        if (sNewGamePromptRow < kNgRowCount) {
            ngSetRowValue(sNewGamePromptRow, sNewGamePromptCol); // A elige
        } else {
            sNewGamePromptResult = sNewGamePromptRules ? PC_NEWGAME_PERMADEATH
                                                       : PC_NEWGAME_NORMAL;
            sNewGamePromptOpen   = false;
        }
    } else if (cancel) {
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

    const int panelW = kNgPanelW;
    const int panelH = kNgPanelH;
    const int panelX = screenW / 2 - panelW / 2;
    const int panelY = screenH / 2 - panelH / 2;

    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    const bool randomizer = pc_randomizer_menu_pending();
    drawPikminHeader(gfx, panelX, panelY, panelW, randomizer ? "New Randomizer Game" : "New Game");

    const char* titles[kNgRowCount] = { "Difficulty", "Game mode", "Skip tutorial" };
    const char* options[kNgRowCount][2] = {
        { "Normal", "Hard" },
        { "Standard", "Permadeath" },
        { "No", "Yes" },
    };
    if (randomizer) {
        options[2][0] = "-";
        options[2][1] = "Yes (required)";
    }
    for (int row = 0; row < kNgRowCount; row++) {
        const bool rowFocus = (row == sNewGamePromptRow);
        const int optY = ngRowY(panelY, row);
        drawTextOutline(ngBoxX(panelX, 0), optY - 24, "%s",
                        rowFocus ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                        Colour(8, 12, 28, 255), titles[row]);
        for (int i = 0; i < 2; i++) {
            const bool chosen = (i == ngRowValue(row));
            const bool cursor = rowFocus && i == sNewGamePromptCol;
            const bool sel    = chosen;
            const int boxX = ngBoxX(panelX, i);
            // Cursor: marcador a la izquierda de la opción (la elegida va resaltada).
            if (cursor)
                drawTextOutline(boxX - 18, optY + 12, ">", Colour(255, 229, 120, 255), Colour(8, 12, 28, 255));
            if (drawGlassOption(boxX, optY, kNgBoxW, kNgBoxH, options[row][i], sel, 20, 29)) continue;
            const Colour fill = sel      ? Colour(70, 92, 150, 240)
                              : chosen   ? Colour(48, 60, 96, 230)
                                         : Colour(26, 30, 48, 220);
            gfx->setColour(fill, true);
            gfx->setAuxColour(fill);
            gfx->fillRectangle(RectArea(boxX, optY, boxX + kNgBoxW, optY + 40));
            const int tw = menuTextWidth(options[row][i]);
            drawTextOutline(boxX + kNgBoxW / 2 - tw / 2, optY + 12, "%s",
                            chosen ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                            Colour(8, 12, 28, 255), options[row][i]);
        }
    }

    // Botón Start: el único que empieza la partida.
    {
        const bool focus = sNewGamePromptRow == kNgStartRow;
        const int bx = ngStartX(panelX), by = ngRowY(panelY, kNgStartRow);
        if (focus)
            drawTextOutline(bx - 18, by + 12, ">", Colour(255, 229, 120, 255), Colour(8, 12, 28, 255));
        if (!drawGlassOption(bx, by, kNgBoxW, kNgBoxH, "Start", focus, 20, 29)) {
            const Colour fill = focus ? Colour(70, 92, 150, 240) : Colour(26, 30, 48, 220);
            gfx->setColour(fill, true);
            gfx->setAuxColour(fill);
            gfx->fillRectangle(RectArea(bx, by, bx + kNgBoxW, by + 40));
            drawTextOutline(bx + kNgBoxW / 2 - menuTextWidth("Start") / 2, by + 12, "%s",
                            focus ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255), Colour(8, 12, 28, 255),
                            "Start");
        }
    }

    // Describe la opción bajo el cursor: es el único sitio donde se explican
    // Hard y Permadeath.
    const int value = sNewGamePromptRow < kNgRowCount && !ngSeedRow(sNewGamePromptRow) ? sNewGamePromptCol : 0;
    const char* detail;
    if (sNewGamePromptRow == kNgStartRow) {
        detail = randomizer ? "Begin the randomized expedition."
                            : "Begin the expedition with these settings.";
    } else if (ngSeedRow(sNewGamePromptRow)) {
        detail = "Required in Randomizer: Day 2, Main Engine and 20 red Pikmin.";
    } else if (sNewGamePromptRow == 0) {
        detail = value ? "Tougher enemies, Olimar takes more damage. 8-minute days, 80 Pikmin."
                       : "Original enemy health, day length and field limit.";
    } else if (sNewGamePromptRow == 1) {
        detail = value ? "If Olimar loses all his health, this file is erased."
                       : "Losing Olimar ends the day. The original rules.";
    } else {
        detail = value ? "Begin on Day 2 with the Main Engine and 20 red Pikmin."
                       : "Play The Impact Site tutorial normally.";
    }
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(detail) / 2, panelY + 344,
                    "%s",
                    value ? Colour(255, 150, 150, 255) : Colour(190, 200, 220, 255),
                    Colour(8, 12, 28, 255), detail);

    const char* help = "Arrows: move    A / Enter: select    B / Esc: back";
    drawHelpLine(panelX + panelW / 2, panelY + 382, help, Colour(150, 165, 195, 255));
    pc_gfx_note_menu_tap_space(screenW, screenH); // ngPointer maps the mouse through this projection
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
// Modo Desafío: tercera opción, 1 vs 1, que aún no se puede elegir.
bool sPlayerCountChallenge = false;
constexpr bool kChallengeVsReady = false;
int playerCountOptions() { return sPlayerCountChallenge ? 3 : 2; }
bool playerCountEnabled(int i) { return i < 2 || kChallengeVsReady; }

bool playerCountSpanish() { return pc_settings_get_language() == 3; } // OS_LANG_SPANISH
}

void pc_playercount_prompt_open(void) {
    sPlayerCountOpen    = true;
    sPlayerCountChoice  = sConfig.coopPlayers == 2 ? 1 : 0;
    sPlayerCountResult  = PC_PLAYERCOUNT_PENDING;
    sPlayerCountChallenge = false;
    pc_menu_edge_reset();
}

void pc_playercount_prompt_open_challenge(void) {
    pc_playercount_prompt_open();
    sPlayerCountChallenge = true;
}

bool pc_playercount_prompt_active(void) { return sPlayerCountOpen; }

int pc_playercount_prompt_result(void) { return sPlayerCountResult; }

namespace {
void pcPlayerCountPromptInput() {
    if (!sPlayerCountOpen) return;

    bool left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = menuOkKey();
    bool cancel = menuCancelKey();

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
        const int n = playerCountOptions();
        const float boxW = n == 3 ? 170.0f : 230.0f, gap = n == 3 ? 20.0f : 40.0f;
        const float left0 = panelX + (620.0f - (n * boxW + (n - 1) * gap)) * 0.5f;
        for (int i = 0; i < n; ++i) {
            const float boxX = left0 + i * (boxW + gap);
            if (!playerCountEnabled(i)) continue;
            if (x >= boxX && x <= boxX + boxW && y >= optY - 8.0f && y <= optY + 52.0f) {
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

    if (left || right) {
        const int n = playerCountOptions();
        do {
            sPlayerCountChoice = (sPlayerCountChoice + (left ? n - 1 : 1)) % n;
        } while (!playerCountEnabled(sPlayerCountChoice));
    }

    if (accept) {
        sConfig.coopPlayers = sPlayerCountChoice ? 2 : 1;
        saveConfig();
        sPlayerCountResult = sPlayerCountChoice == 2 ? PC_PLAYERCOUNT_VS
                           : sPlayerCountChoice      ? PC_PLAYERCOUNT_TWO
                                                     : PC_PLAYERCOUNT_ONE;
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

    const char* options[3] = { es ? "1 jugador" : "1 player", es ? "2 jugadores" : "2 players", "1 vs 1" };
    if (sPlayerCountChallenge) options[1] = es ? "Cooperativo" : "Co-op";
    const int optY = panelY + 108;
    const int n    = playerCountOptions();
    const int boxW = n == 3 ? 170 : 230, gap = n == 3 ? 20 : 40;
    const int left0 = panelX + (panelW - (n * boxW + (n - 1) * gap)) / 2;
    for (int i = 0; i < n; i++) {
        const bool sel = (i == sPlayerCountChoice);
        const bool on  = playerCountEnabled(i);
        const int boxX = left0 + i * (boxW + gap);
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
                        sel ? Colour(255, 229, 120, 255) : on ? Colour(170, 180, 200, 255) : Colour(90, 96, 112, 255),
                        Colour(8, 12, 28, 255), options[i]);
    }

    const char* detail = sPlayerCountChallenge
        ? (sPlayerCountChoice == 2 ? (es ? "Cada uno con sus Pikmin: gana quien tenga más." : "Each with their own Pikmin: most Pikmin wins.")
           : sPlayerCountChoice    ? (es ? "Los dos juntos, un solo marcador. Luego se asignan los mandos."
                                         : "Both together, one shared score. Controllers are assigned next.")
                                   : (es ? "El desafío original, un solo Olimar." : "The original challenge, a single Olimar."))
        : sPlayerCountChoice
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

// Tarjetas del selector de capitán, en orden (índice = PcCaptain). Los
// propios (Load/Characters) no están en estas tablas: ver captainName/Art.
const char* const kCaptainNames[PC_CAPTAIN_CUSTOM_FIRST] = { "Olimar", "Louie", "Red", "Yellow", "Blue",
                                                             "President", "White", "Purple", "Bulbmin", "Puffmin" };
// Sprites al doble (Olimar/Louie de Pikmin 2-e; los Pikmin, de Pikmin Puzzle
// Cards de GBA, de pie con la hoja).
const char* const kCaptainArt[PC_CAPTAIN_CUSTOM_FIRST] = { "coop_olimar", "coop_louie", "coop_piki_red", "coop_piki_yellow",
                                                           "coop_piki_blue", "coop_president", "coop_piki_white",
                                                           "coop_piki_purple", "coop_bulbmin", "coop_puffmin" };
const char* captainName(int captain) {
    if (pc_captain_is_custom(captain)) return pc_captain_custom_name(captain);
    return (captain >= 0 && captain < PC_CAPTAIN_CUSTOM_FIRST) ? kCaptainNames[captain] : kCaptainNames[0];
}
// Tarjeta de un propio: su portrait.png ("@ruta"); sin ella, la de Olimar.
const char* captainArt(int captain) {
    if (pc_captain_is_custom(captain)) {
        const char* p = pc_captain_custom_portrait(captain);
        return p ? p : kCaptainArt[0];
    }
    return (captain >= 0 && captain < PC_CAPTAIN_CUSTOM_FIRST) ? kCaptainArt[captain] : kCaptainArt[0];
}
// Si falta el PNG de un Pikmin, sale de la textura del juego piki3 (116x64):
// rojo, amarillo y azul tumbados con su hoja. Columnas de cada uno.
const int kCaptainPikiCol[PC_CAPTAIN_COUNT][2] = { { 0, 0 }, { 0, 0 }, { 0, 38 }, { 34, 78 }, { 76, 116 } };
constexpr int kCaptainBoxW = kPcCaptainBoxW, kCaptainBoxGap = kPcCaptainBoxGap;
// Filas: 0 los de Pikmin 1; 1 los de Pikmin 2 instalados y el Puffmin, que
// siempre está; 2 los personajes propios. Con las tres a la vista las
// tarjetas son algo más bajas para caber en 480 de alto.
constexpr int kCaptainBoxTall = 120, kCaptainBoxShort = 104;
// Tarjetas a la vista por fila; los propios van por páginas de cinco.
constexpr int kCaptainRowSlots = kPcCaptainRowSlots;
int sCaptainCustomPage = 0;
int captainRowLeft(int count) { return pc_captain_row_left(count); }
int captainRowOf(int captain) {
    if (captain < PC_CAPTAIN_FIRST_PIKMIN2) return 0;
    return pc_captain_is_custom(captain) ? 2 : 1;
}
void captainRowRange(int row, int* first, int* end) {
    *first = row == 0 ? 0 : row == 1 ? PC_CAPTAIN_FIRST_PIKMIN2 : PC_CAPTAIN_CUSTOM_FIRST;
    *end   = row == 0 ? PC_CAPTAIN_FIRST_PIKMIN2 : row == 1 ? PC_CAPTAIN_CUSTOM_FIRST : PC_CAPTAIN_COUNT;
}
// Tarjetas de una fila (las que se pueden elegir), y la de la columna col (-1 si no hay).
int captainRowCount(int row) {
    if (row == 0) return PC_CAPTAIN_FIRST_PIKMIN2;
    int first = 0, end = 0, n = 0;
    captainRowRange(row, &first, &end);
    for (int i = first; i < end; i++) n += pc_captain_available(i) ? 1 : 0;
    return n;
}
int captainRowAt(int row, int col) {
    if (row == 0) return (col >= 0 && col < PC_CAPTAIN_FIRST_PIKMIN2) ? col : -1;
    int first = 0, end = 0;
    captainRowRange(row, &first, &end);
    for (int i = first; i < end; i++) {
        if (pc_captain_available(i) && col-- == 0) return i;
    }
    return -1;
}
int captainColOf(int captain) {
    const int row = captainRowOf(captain);
    if (row == 0) return captain;
    int first = 0, end = 0, col = 0;
    captainRowRange(row, &first, &end);
    for (int i = first; i < captain; i++) col += pc_captain_available(i) ? 1 : 0;
    return col;
}
bool captainRowShown(int row) { return row == 0 || captainRowCount(row) > 0; }
int captainRowsShown() { return 1 + (captainRowShown(1) ? 1 : 0) + (captainRowShown(2) ? 1 : 0); }
// Puesto en pantalla de una fila (salta las vacías).
int captainScreenRow(int row) {
    int r = 0;
    for (int k = 0; k < row; k++) r += captainRowShown(k) ? 1 : 0;
    return r;
}
int captainBoxH() { return captainRowsShown() >= 3 ? kCaptainBoxShort : kCaptainBoxTall; }
bool captainSecondRow() { return captainRowsShown() > 1; }
// Alto extra del panel (el de 260 trae una fila de las altas).
int captainExtraH() {
    return (captainRowsShown() - 1) * (captainBoxH() + kCaptainBoxGap) - (kCaptainBoxTall - captainBoxH());
}
int captainCustomPages() {
    const int n = captainRowCount(2);
    return n > 0 ? (n + kCaptainRowSlots - 1) / kCaptainRowSlots : 1;
}
// Columna a la vista dentro de su fila (la de los propios, en su página).
int captainViewCol(int captain) {
    const int col = captainColOf(captain);
    return captainRowOf(captain) == 2 ? col - sCaptainCustomPage * kCaptainRowSlots : col;
}
// La página de los propios es la del elegido (si está en esa fila).
void captainKeepVisible(int captain) {
    if (captainRowOf(captain) == 2) sCaptainCustomPage = captainColOf(captain) / kCaptainRowSlots;
    const int pages = captainCustomPages();
    if (sCaptainCustomPage >= pages) sCaptainCustomPage = pages - 1;
    if (sCaptainCustomPage < 0) sCaptainCustomPage = 0;
}
// Página siguiente/anterior (da la vuelta). Con un propio elegido, pasa al de
// la misma columna en la página nueva (o al último si es más corta).
void captainTurnPage(int* captain, int dir) {
    const int pages = captainCustomPages();
    if (pages <= 1) return;
    const int col       = captainRowOf(*captain) == 2 ? captainViewCol(*captain) : -1;
    sCaptainCustomPage  = (sCaptainCustomPage + dir + pages) % pages;
    if (col >= 0) {
        const int count = captainRowCount(2);
        const int at    = std::min(sCaptainCustomPage * kCaptainRowSlots + col, count - 1);
        if (const int c = captainRowAt(2, at); c >= 0) *captain = c;
    }
}
// Flechas de página a los lados de la fila de los propios (táctil y dibujo).
bool captainPageArrows(int panelX, int panelY, int* leftX, int* rightX, int* y, int* h) {
    if (captainCustomPages() <= 1 || !captainRowShown(2)) return false;
    const int shown = std::min(captainRowCount(2) - sCaptainCustomPage * kCaptainRowSlots, kCaptainRowSlots);
    const int left  = panelX + captainRowLeft(std::min(captainRowCount(2), kCaptainRowSlots));
    const int width = std::min(captainRowCount(2), kCaptainRowSlots) * (kCaptainBoxW + kCaptainBoxGap) - kCaptainBoxGap;
    (void)shown;
    *leftX  = left - 30;
    *rightX = left + width + 6;
    *y      = panelY + 84 + captainScreenRow(2) * (captainBoxH() + kCaptainBoxGap);
    *h      = captainBoxH();
    return true;
}
// Esquina de la tarjeta de un capitán dentro del panel; false si no se muestra.
bool captainCardPos(int captain, int panelX, int panelY, int* x, int* y) {
    if (captain < 0 || captain >= PC_CAPTAIN_COUNT) return false;
    const int row = captainRowOf(captain);
    if (row != 0 && !pc_captain_available(captain)) return false;
    const int col = captainViewCol(captain);
    if (col < 0 || col >= kCaptainRowSlots) return false;
    const int shown = std::min(captainRowCount(row), kCaptainRowSlots);
    const PcMenuClickRect r = pc_captain_card_rect(panelX, panelY, captainScreenRow(row), col, shown, captainBoxH());
    *x = r.minX;
    *y = r.minY;
    return true;
}
const char* devAssignCaptainName(int captain) { return captainName(captain); }

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

// Mouse on the captain cards, in the graph the prompt draws (pc_captain_prompt_draw and the
// co-op picker note it). Motion over a card highlights it. A click on a card picks it and
// returns 1, so one click confirms; a click on a page arrow returns 2 (previous) or 3 (next).
// Empty space does nothing. ignore drops the click and the hover during the opening window.
int captainPickPointer(int* captain, bool ignore) {
    pc_menu_click_claim_pointer_menu();
    DGXGraphics* gfx = (gsys && gsys->mDGXGfx) ? static_cast<DGXGraphics*>(gsys->mDGXGfx) : nullptr;
    if (!gfx) return 0;
    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    int panelX = 0, panelY = 0;
    pc_captain_panel_origin(screenW, screenH, captainExtraH(), &panelX, &panelY);
    constexpr int kPagePrev = -2, kPageNext = -3;
    PcMenuClickRect rects[PC_CAPTAIN_COUNT + 2];
    int ids[PC_CAPTAIN_COUNT + 2];
    int count = 0;
    for (int i = 0; i < PC_CAPTAIN_COUNT; ++i) {
        int boxX = 0, boxY = 0;
        if (!captainCardPos(i, panelX, panelY, &boxX, &boxY) || !pc_captain_available(i)) continue;
        rects[count] = { boxX, boxY, boxX + kCaptainBoxW, boxY + captainBoxH() };
        ids[count++] = i;
    }
    int arrowL = 0, arrowR = 0, arrowY = 0, arrowH = 0;
    if (captainPageArrows(panelX, panelY, &arrowL, &arrowR, &arrowY, &arrowH)) {
        rects[count] = { arrowL, arrowY, arrowL + kPcCaptainArrowW, arrowY + arrowH };
        ids[count++] = kPagePrev;
        rects[count] = { arrowR, arrowY, arrowR + kPcCaptainArrowW, arrowY + arrowH };
        ids[count++] = kPageNext;
    }
    float x = 0.0f, y = 0.0f;
    if (!ignore && pc_gfx_menu_graph_point(false, &x, &y)) {
        const PcMenuClickResult hover
            = pc_menu_click_hover(PC_MENU_CLICK_MAIN, x, y, rects, nullptr, count, kPcMenuClickMargin);
        if (hover.index >= 0 && ids[hover.index] >= 0) *captain = ids[hover.index];
    }
    if (pc_gfx_menu_graph_point(true, &x, &y) && !ignore) {
        const PcMenuClickResult hit
            = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, x, y, rects, nullptr, count, kPcMenuClickMargin);
        if (hit.action == PC_MENU_CLICK_ACTIVATE && hit.index >= 0) {
            const int id = ids[hit.index];
            if (id == kPagePrev) return 2;
            if (id == kPageNext) return 3;
            *captain = id;
            return 1;
        }
    }
    return 0;
}

// Selector de capitán (Olimar, Louie o un Pikmin) compartido por el prompt de mandos del
// coop y el de 1 jugador. Devuelve 0 nada, 1 aceptado, 2 atrás.
int captainPickInput(int player, bool keyboardOk, SDL_GameController* ctl, int* captain, bool ignoreWindow) {
    bool left = false, right = false, up = false, down = false, accept = false, back = false;
    bool pagePrev = false, pageNext = false; // páginas de los personajes propios
    if (keyboardOk) {
        pagePrev = keyWentDown(SDL_SCANCODE_Q);
        pageNext = keyWentDown(SDL_SCANCODE_E);
        left   = keyWentDown(SDL_SCANCODE_LEFT)  || keyWentDown(SDL_SCANCODE_A);
        right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        up     = keyWentDown(SDL_SCANCODE_UP)    || keyWentDown(SDL_SCANCODE_W);
        down   = keyWentDown(SDL_SCANCODE_DOWN)  || keyWentDown(SDL_SCANCODE_S);
        // The cards hit-test the left click (captainPickPointer), so a click is not also A.
        accept = menuOkKeyNoClick();
        back   = menuCancelKey();
        back |= pc_menu_click_take_back(); // right click: back
    }
    if (player == 0) {
        left |= (sTouchFrameButtons & PAD_BUTTON_LEFT) != 0;
        right |= (sTouchFrameButtons & PAD_BUTTON_RIGHT) != 0;
        up |= (sTouchFrameButtons & PAD_BUTTON_UP) != 0;
        down |= (sTouchFrameButtons & PAD_BUTTON_DOWN) != 0;
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
            const int panelX = int(screenW * 0.5f) - 310, panelY = 240 - (260 + captainExtraH()) / 2;
            int arrowL = 0, arrowR = 0, arrowY = 0, arrowH = 0;
            if (captainPageArrows(panelX, panelY, &arrowL, &arrowR, &arrowY, &arrowH) && y >= arrowY && y <= arrowY + arrowH) {
                if (x >= arrowL - 8 && x <= arrowL + 32) pagePrev = true;
                if (x >= arrowR - 8 && x <= arrowR + 32) pageNext = true;
            }
            for (int i = 0; i < PC_CAPTAIN_COUNT; ++i) {
                int boxX = 0, boxY = 0;
                if (!captainCardPos(i, panelX, panelY, &boxX, &boxY) || !pc_captain_available(i)) continue;
                if (x >= boxX && x <= boxX + kCaptainBoxW && y >= boxY && y <= boxY + captainBoxH()) {
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
        const bool hUp   = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) || menuStickVertical(ctl, -1);
        const bool hDown = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || menuStickVertical(ctl, 1);
        if (padEdge(hUp, 0))   up   = true;
        if (padEdge(hDown, 1)) down = true;
        if (padEdge(hA, 4))     accept = true;
        if (padEdge(hB, 5))     back   = true;
        if (padEdge(SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0, 12)) pagePrev = true;
        if (padEdge(SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0, 13)) pageNext = true;
    }
    // The mouse belongs to whoever has the keyboard. A click inside the opening window is
    // taken and dropped with the rest of the input.
    bool pointerPick = false;
    if (keyboardOk) {
        const int pointer = captainPickPointer(captain, ignoreWindow);
        if (pointer == 1) pointerPick = true;
        if (pointer == 2) pagePrev = true;
        if (pointer == 3) pageNext = true;
    }
    if (ignoreWindow) return 0;
    // Izquierda/derecha dan la vuelta dentro de la fila; arriba/abajo cambian
    // de fila (la segunda solo con Pikmin 2), a la misma columna o la última.
    if (!pc_captain_available(*captain)) *captain = PC_CAPTAIN_OLIMAR;
    {
        const int row   = captainRowOf(*captain);
        const int count = captainRowCount(row);
        if ((left || right) && count > 0) {
            *captain = captainRowAt(row, (captainColOf(*captain) + (left ? count - 1 : 1)) % count);
        }
        if (up || down) {
            // A la fila de al lado que se vea, a la misma columna en pantalla
            // (o la última).
            int target = -1;
            for (int r = row + (down ? 1 : -1); r >= 0 && r <= 2 && target < 0; r += down ? 1 : -1) {
                if (!captainRowShown(r)) continue;
                const int col    = std::max(0, captainViewCol(*captain)) + (r == 2 ? sCaptainCustomPage * kCaptainRowSlots : 0);
                const int oCount = captainRowCount(r);
                target           = captainRowAt(r, col < oCount ? col : oCount - 1);
            }
            if (target >= 0) *captain = target;
        }
        captainKeepVisible(*captain);
        if (pagePrev || pageNext) {
            captainTurnPage(captain, pageNext ? 1 : -1);
            captainKeepVisible(*captain);
        }
    }
    // One click on a card picks it and confirms. Navigation above ran with no key held, so it
    // left the clicked card alone.
    if (pointerPick) accept = true;
    if (accept && *captain == PC_CAPTAIN_LOUIE && !devAssignLouieInstalled()) {
        // Sin el modelo no se puede jugar con Louie: aviso y se queda.
        sDevAssignLouieNoticeUntil = SDL_GetTicks() + 3000;
        accept = false;
    }
    if (accept) return 1;
    if (back) return 2;
    return 0;
}

// Prueba de tarjetas con el modelo 3D del juego (pc_captain_preview): lo
// activa el selector de 1 jugador; las tarjetas con modelo no pintan su PNG.
bool sCaptainCards3D = false;

// Dibujo del selector dentro de un panel (panelX/Y/W del prompt).
void captainPickDraw(DGXGraphics* gfx, int panelX, int panelY, int panelW, int player, int captain, const char* helpDefault) {
    char line1[64];
    snprintf(line1, sizeof(line1), "Player %d, choose your captain", player + 1);
    drawTextOutline(panelX + panelW / 2 - menuTextWidth(line1) / 2, panelY + 56,
                    "%s", Colour(255, 229, 120, 255), Colour(8, 12, 28, 255), line1);
    const int boxW = kCaptainBoxW, boxH = captainBoxH();
    captainKeepVisible(captain);
    for (int i = 0; i < PC_CAPTAIN_COUNT; i++) {
        const bool sel = captain == i;
        int boxX = 0, boxY = 0;
        if (!captainCardPos(i, panelX, panelY, &boxX, &boxY)) continue;
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
        Texture* art = (sCaptainCards3D && pc_captain_preview_ready(i)) ? nullptr : pc_art_texture(captainArt(i));
        if (art && !pc_art_size(captainArt(i), &aw, &ah)) art = nullptr;
        // La tarjeta de un propio es grande (256): se dibuja a 64 de alto, como Olimar.
        const bool fitArt = art && pc_captain_is_custom(i) && ah > 32;
        if (!art && i >= PC_CAPTAIN_PIKMIN_RED && i <= PC_CAPTAIN_PIKMIN_BLUE
            && !(sCaptainCards3D && pc_captain_preview_ready(i))) {
            static Texture* sPiki3 = nullptr;
            if (!sPiki3) sPiki3 = zen::loadTexExp("screen/tex/piki3.bti", true, true);
            art   = sPiki3;
            ax    = kCaptainPikiCol[i][0];
            aw    = kCaptainPikiCol[i][1] - ax;
            ah    = art ? art->mHeight : 0;
            scale = 1;
        }
        if (art) {
            const int dh = fitArt ? 64 : ah * scale, dw = fitArt ? aw * 64 / ah : aw * scale;
            const int dx = boxX + boxW / 2 - dw / 2, dy = boxY + (boxH < kCaptainBoxTall ? 4 : 10) + (scale == 2 && dh < 64 ? 64 - dh : 0);
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
        // Metido en la tarjeta: a lo ancho entero, los bordes del brillo de
        // selección asomaban por fuera como paréntesis.
        if (drawGlassOption(boxX + 8, boxY + boxH - 38, boxW - 16, 30, captainName(i), sel)) continue;
        const int tw = menuTextWidth(captainName(i));
        drawTextOutline(boxX + boxW / 2 - tw / 2, boxY + boxH - 30, "%s",
                        sel ? Colour(255, 229, 120, 255) : Colour(170, 180, 200, 255),
                        Colour(8, 12, 28, 255), captainName(i));
    }
    // Flechas de página de los propios (también se tocan en el móvil).
    int arrowL = 0, arrowR = 0, arrowY = 0, arrowH = 0;
    char pageHelp[96] = "";
    if (captainPageArrows(panelX, panelY, &arrowL, &arrowR, &arrowY, &arrowH)) {
        const Colour gold(255, 229, 120, 255), ink(8, 12, 28, 255);
        drawTextOutline(arrowL + 8, arrowY + arrowH / 2 - 10, "%s", gold, ink, "<");
        drawTextOutline(arrowR + 8, arrowY + arrowH / 2 - 10, "%s", gold, ink, ">");
        snprintf(pageHelp, sizeof(pageHelp), "Q/E or LB/RB: page %d/%d    A: confirm    B: back", sCaptainCustomPage + 1,
                 captainCustomPages());
    }
    const bool louieMissing = !devAssignLouieInstalled();
    const bool noticing = louieMissing && SDL_GetTicks() < sDevAssignLouieNoticeUntil;
    const char* help = noticing ? "Louie model not installed: Advanced Options > HD Models (Louie zip)."
                     : pageHelp[0] ? pageHelp
                     : (louieMissing ? "Louie: model not installed (Advanced Options > HD Models)." : helpDefault);
    drawHelpLine(panelX + panelW / 2, panelY + 220 + captainExtraH(), help, noticing ? Colour(255, 160, 120, 255) : Colour(150, 165, 195, 255));
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
        pc_captain_preview_release();
    } else if (r == 2) {
        sCaptainPromptResult = PC_DEVASSIGN_CANCELLED;
        sCaptainPromptOpen   = false;
        pc_captain_preview_release();
    }
}
}

void pc_captain_prompt_open(void) {
    pc_captain_custom_refresh(); // personajes instalados con el juego abierto
    sCaptainCustomPage = 0;
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
    const int panelW = 620, panelH = 260 + captainExtraH();
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    // Prueba: modelos 3D en las tarjetas (solo con el menú a pantalla
    // ancha, cuyo espacio virtual cubre toda la ventana).
    sCaptainCards3D = pc_gfx_menu_wide() != 0;
    {
        PcSettingsP2DFrame nativeFrame(screenW, screenH);
        GlassTextScope glassText;
        Matrix4f ortho;
        gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
        drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
        drawPikminHeader(gfx, panelX, panelY, panelW, "Captain");
        captainPickDraw(gfx, panelX, panelY, panelW, 0, sCaptainPromptChoice,
                        captainSecondRow() ? "Arrows: choose    A / Enter: confirm    B / Esc: back"
                                            : "Left/Right: choose    A / Enter: confirm    B / Esc: back");
        pc_gfx_note_menu_tap_space(screenW, screenH); // captainPickPointer maps the mouse through this
    }
    if (sCaptainCards3D) {
        // Mismo hueco que el sprite: encima del nombre de la tarjeta.
        for (int i = 0; i < PC_CAPTAIN_COUNT; i++) {
            int boxX = 0, boxY = 0;
            if (!captainCardPos(i, panelX, panelY, &boxX, &boxY)) continue;
            pc_captain_preview_draw(i, boxX + 4, boxY + 4, kCaptainBoxW - 8, captainBoxH() - 44, screenW, screenH,
                                    i == sCaptainPromptChoice);
        }
        Matrix4f ortho;
        gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
    }
}

void pc_devassign_prompt_open(void) {
    pc_captain_custom_refresh();
    sCaptainCustomPage = 0;
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

    // Escape, Backspace, right click and a non-Shift B binding cancel every
    // step, including the "press a button" wait. That wait is checked first,
    // so those keys are not taken as "this player is the keyboard".
    bool cancel = menuCancelKey();
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
            pc_captain_preview_release();
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
            pc_captain_preview_release();
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
    bool accept = menuOkKey();
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
        pc_captain_preview_release();
    } else if (cancel) {
        sDevAssignResult = PC_DEVASSIGN_CANCELLED;
        sDevAssignOpen   = false;
        pc_captain_preview_release();
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
    const int panelW = 620;

    if (sDevAssignStep == DEVASSIGN_PickP1 || sDevAssignStep == DEVASSIGN_PickP2) {
        // El mismo selector que el de 1 jugador (pc_captain_prompt_draw):
        // con Pikmin 2 cabe la segunda fila y, a pantalla ancha, las
        // tarjetas llevan el modelo 3D.
        const int player = devAssignPlayer();
        const int panelH = 260 + captainExtraH();
        const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
        sCaptainCards3D  = pc_gfx_menu_wide() != 0;
        {
            PcSettingsP2DFrame nativeFrame(screenW, screenH);
            GlassTextScope glassText;
            Matrix4f ortho;
            gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
            drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
            drawPikminHeader(gfx, panelX, panelY, panelW, "Captain");
            captainPickDraw(gfx, panelX, panelY, panelW, player, sDevAssignCaptain[player],
                            captainSecondRow() ? "Arrows: choose    A / Enter: confirm    B: reassign    Esc: back"
                                               : "Left/Right: choose    A / Enter: confirm    B: reassign    Esc: back");
            pc_gfx_note_menu_tap_space(screenW, screenH);
        }
        if (sCaptainCards3D) {
            // Mismo hueco que el sprite: encima del nombre de la tarjeta.
            for (int i = 0; i < PC_CAPTAIN_COUNT; i++) {
                int boxX = 0, boxY = 0;
                if (!captainCardPos(i, panelX, panelY, &boxX, &boxY)) continue;
                pc_captain_preview_draw(i, boxX + 4, boxY + 4, kCaptainBoxW - 8, captainBoxH() - 44, screenW, screenH,
                                        i == sDevAssignCaptain[player]);
            }
            Matrix4f ortho;
            gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
        }
        return;
    }

    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    const int panelH = 260;
    const int panelX = screenW / 2 - panelW / 2;
    const int panelY = screenH / 2 - panelH / 2;

    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, "Controllers");

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
    pc_vs_note_gameplay_tick();
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
int sVsRulesRow     = 0; // 0 = mapa, 1.. = reglas (vsRuleField(row - 1))
// The "A: play" span of the lobby's help line, from the last frame the lobby drew.
bool sVsPlaySpanOk = false;
int sVsPlayX0 = 0, sVsPlayX1 = 0, sVsPlayY0 = 0, sVsPlayY1 = 0;
int sVsRulesResult  = PC_DEVASSIGN_PENDING;
constexpr int kVsMenuRows = kVsRuleRows + 1;

// The rules panel. The draw and the mouse hit test both read this layout, so a click lands on
// the row that is drawn.
struct VsRulesLayout {
    int panelX, panelY, panelW, panelH;
    int rx, rowsY, colW, rowH;
};

VsRulesLayout vsRulesLayout(int screenW, int screenH)
{
    VsRulesLayout l;
    l.panelW = std::min(800, screenW - 24);
    l.panelH = 452;
    l.panelX = screenW / 2 - l.panelW / 2;
    l.panelY = screenH / 2 - l.panelH / 2;
    l.colW   = (l.panelW - 72) / 2;
    l.rx     = l.panelX + 24 + l.colW + 24;
    l.rowsY  = l.panelY + 88;
    l.rowH   = 34;
    return l;
}

// Selector de mapa: posición en pc_vs_map_list y, si al pulsar A no se pudo
// montar, por qué (se enseña en la tarjeta de abajo y no se empieza).
int sVsMapIndex = 0;
std::string sVsMapError;

const PcVsMapEntry& vsMapEntry()
{
    const std::vector<PcVsMapEntry>& list = pc_vs_map_list(false);
    return list[std::clamp(sVsMapIndex, 0, int(list.size()) - 1)];
}
int sVsEndChoice    = 0; // 0 revancha, 1 título
Uint32 sVsOverSince = 0; // cuándo acabó la partida (la pantalla final sale un poco después)
constexpr Uint32 kVsEndScreenDelayMs = 2500;

bool vsEndScreenShown()
{
    // Netplay: the end screen is sim state (pc_vs_det_end_screen_shown).
    if (pc_netplay_deterministic()) return pc_vs_det_end_screen_shown();
    return pc_vs_active() && pc_vs_match_over() && sVsOverSince && SDL_GetTicks() - sVsOverSince >= kVsEndScreenDelayMs;
}

// Mouse on the rules panel. Motion over a row highlights it. A click highlights its row and
// returns 1, so the caller cycles that row forward, as Right does. A click on the drawn
// "A: play" hint returns 2, which is A. The claim keeps a left click in Mouse Cursor mode
// from also being A.
int pcVsRulesPointer()
{
    pc_menu_click_claim_pointer_menu();
    DGXGraphics* gfx = (gsys && gsys->mDGXGfx) ? static_cast<DGXGraphics*>(gsys->mDGXGfx) : nullptr;
    if (!gfx) return 0;
    const int screenW       = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH       = gfx->mScreenHeight;
    const VsRulesLayout lay = vsRulesLayout(screenW, screenH);
    PcMenuClickRect rects[kVsMenuRows + 1];
    for (int row = 0; row < kVsMenuRows; row++) {
        const int ry = lay.rowsY + row * lay.rowH;
        rects[row]   = { lay.rx, ry, lay.rx + lay.colW, ry + lay.rowH };
    }
    int count = kVsMenuRows;
    if (sVsPlaySpanOk) {
        rects[kVsMenuRows] = { sVsPlayX0, sVsPlayY0, sVsPlayX1, sVsPlayY1 };
        count              = kVsMenuRows + 1;
    }
    float x = 0.0f, y = 0.0f;
    if (pc_gfx_menu_graph_point(false, &x, &y)) {
        const PcMenuClickResult hover
            = pc_menu_click_hover(PC_MENU_CLICK_MAIN, x, y, rects, nullptr, count, kPcMenuClickMargin);
        if (hover.index >= 0 && hover.index < kVsMenuRows) sVsRulesRow = hover.index;
    }
    if (pc_gfx_menu_graph_point(true, &x, &y)) {
        const PcMenuClickResult hit
            = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, x, y, rects, nullptr, count, kPcMenuClickMargin);
        if (hit.action == PC_MENU_CLICK_ACTIVATE) {
            if (hit.index == kVsMenuRows) return 2;
            sVsRulesRow = hit.index;
            return 1;
        }
    }
    return 0;
}

void pcVsRulesInput()
{
    if (!sVsRulesOpen) return;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    // The rows hit-test the left click, so a click on a row cycles it and is not also A.
    bool accept = menuOkKeyNoClick();
    bool cancel = menuCancelKey();
    cancel |= pc_menu_click_take_back(); // a right click backs out; a mouse press only
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
    const int pointer = pcVsRulesPointer();
    if (pointer == 1) right = true; // a click cycles its row forward, as Right does
    if (pointer == 2) accept = true; // a click on the drawn "A: play" hint is A
    if (up) sVsRulesRow = (sVsRulesRow + kVsMenuRows - 1) % kVsMenuRows;
    if (down) sVsRulesRow = (sVsRulesRow + 1) % kVsMenuRows;
    if ((left || right) && sVsRulesRow == 0) {
        const int n   = int(pc_vs_map_list(false).size());
        sVsMapIndex   = (sVsMapIndex + (left ? n - 1 : 1)) % n;
        sConfig.vsMap = vsMapEntry().id;
        sVsMapError.clear();
    } else if (left || right) {
        int* field  = vsRuleField(sVsRulesRow - 1);
        const int n = vsRuleCount(sVsRulesRow - 1);
        *field      = (*field + (left ? n - 1 : 1)) % n;
    }
    if (accept) {
        // Se monta ya: si el mapa no vale, se dice aquí y no se empieza.
        pc_vs_map_select(sConfig.vsMap);
        if (const char* why = pc_vs_arena_prepare()) {
            sVsMapError = why;
            sVsRulesRow = 0;
            return;
        }
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
    bool accept = menuOkKey();
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

const char* pc_settings_get_vs_map(void) { return sConfig.vsMap.c_str(); }

void pc_vsrules_prompt_open(void) {
    sVsRulesOpen   = true;
    sVsRulesRow    = 0;
    sVsRulesResult = PC_DEVASSIGN_PENDING;
    // La lista se vuelve a leer cada vez (mapas copiados con el juego abierto).
    const std::vector<PcVsMapEntry>& maps = pc_vs_map_list(true);
    sVsMapIndex = 0;
    for (size_t i = 0; i < maps.size(); i++) {
        if (maps[i].id == sConfig.vsMap) sVsMapIndex = int(i);
    }
    sConfig.vsMap = vsMapEntry().id;
    sVsMapError.clear();
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

// Copia `s` en `out` recortada con "..." para que quepa en maxW (sin partir
// letras UTF-8: los mapas pueden traer tildes).
void vsFitText(const char* s, char* out, size_t n, int maxW, int fw)
{
    snprintf(out, n, "%s", s);
    if (vsTextW(out, fw) <= maxW) return;
    std::string cut(out);
    while (!cut.empty()) {
        cut.pop_back();
        while (!cut.empty() && (u8(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (!cut.empty() && (u8(cut.back()) & 0x80)) cut.pop_back(); // primer byte de la letra partida
        snprintf(out, n, "%s...", cut.c_str());
        if (vsTextW(out, fw) <= maxW) return;
    }
}

Texture* vsIcon(const char* name)
{
    // Sin caché propia: el registro de gsys ya la hace y se invalida cuando el
    // heap del nivel se resetea (un Texture* guardado aquí quedaba con la textura
    // GL liberada y los píxeles reutilizados).
    char path[64];
    snprintf(path, sizeof(path), "screen/tex/%s.bti", name);
    return zen::loadTexExp(path, true, true);
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
    const VsRulesLayout lay = vsRulesLayout(screenW, screenH);
    const int panelW = lay.panelW, panelH = lay.panelH;
    const int panelX = lay.panelX, panelY = lay.panelY;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    drawPikminHeader(gfx, panelX, panelY, panelW, es ? "VS: Carrera de piezas" : "VS: Part Race");

    const int colW = lay.colW;
    const int lx   = panelX + 24;
    const int rx   = lay.rx;

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
    const int rowsY = lay.rowsY;
    const int kRowH = lay.rowH;
    const PcVsMapEntry& map = vsMapEntry();
    const bool mapBad       = !map.error.empty() || !sVsMapError.empty();
    for (int row = 0; row < kVsMenuRows; row++) {
        const char* label = "";
        char value[48];
        if (row == 0) {
            label = es ? "Mapa" : "Map";
            vsFitText(map.name.c_str(), value, sizeof(value), colW / 2, 11);
        } else {
            vsRuleText(row - 1, &label, value, sizeof(value));
        }
        const bool sel = row == sVsRulesRow;
        const int ry   = rowsY + row * kRowH;
        // Fila elegida: bolita de cristal delante y valor en naranja (estilo
        // burbuja), en vez de la placa amarilla.
        if (sel && pc_settings_p2d_active())
            pc_settings_p2d_cursor(rx - 8, ry + 8, 18, std::fmod(SDL_GetTicks() / 1000.0f * 10.0f, 6.2831853f));
        const Colour c = sel ? Colour(255, 255, 255, 255) : Colour(190, 200, 220, 255);
        vsText(rx + 10, ry, label, c, 11, 17);
        char shown[64];
        snprintf(shown, sizeof(shown), sel ? "< %s >" : "%s", value);
        const Colour valueC = row == 0 && mapBad ? Colour(255, 110, 90, 255) : (sel ? Colour(255, 170, 30, 255) : c);
        vsText(rx + colW - 10 - vsTextW(shown, 11), ry, shown, valueC, 11, 17);
    }
    const int descY = rowsY + kVsMenuRows * kRowH + 6;
    vsCard(gfx, rx - 4, descY, colW + 8, 60);
    if (sVsRulesRow == 0 || !sVsMapError.empty()) {
        // Mapa: miniatura (si trae thumb.png) y autor, o por qué no vale.
        int tx = rx + 10;
        int aw = 0, ah = 0;
        const std::string thumbName = map.thumb.empty() ? std::string() : "@" + map.thumb;
        Texture* thumb = thumbName.empty() ? nullptr : pc_art_texture(thumbName.c_str());
        if (thumb && pc_art_size(thumbName.c_str(), &aw, &ah) && aw > 0 && ah > 0) {
            const int th = 48, tw = std::min(80, aw * th / ah);
            const Colour tint(255, 255, 255, 255);
            const float u1 = float(aw) / thumb->mWidth, v1 = float(ah) / thumb->mHeight;
            if (pc_settings_p2d_active()) {
                pc_settings_p2d_image(rx + 2, descY + 6, tw, th, thumb, u1, v1, tint, 0.0f);
            } else {
                gfx->setColour(tint, true);
                gfx->setAuxColour(tint);
                gfx->useTexture(thumb, GX_TEXMAP0);
                gfx->drawRectangle(RectArea(rx + 2, descY + 6, rx + 2 + tw, descY + 6 + th), RectArea(0, 0, aw, ah), nullptr);
                gfx->useTexture(nullptr, GX_TEXMAP0);
            }
            tx = rx + 2 + tw + 10;
        }
        const int textW = rx + colW - 6 - tx;
        char l1[96], l2[128];
        const std::string& why = !sVsMapError.empty() ? sVsMapError : map.error;
        if (!why.empty()) {
            snprintf(l1, sizeof(l1), "%s", es ? "No se puede jugar:" : "Can't be played:");
            vsFitText(why.c_str(), l2, sizeof(l2), textW, 10);
        } else {
            if (map.id.empty()) snprintf(l1, sizeof(l1), "%s", es ? "Arena de serie" : "Built-in arena");
            else snprintf(l1, sizeof(l1), "%s", es ? "Mapa de la comunidad" : "Community map");
            char by[96];
            snprintf(by, sizeof(by), "%s %s", es ? "De" : "By", map.author.empty() ? "?" : map.author.c_str());
            vsFitText(by, l2, sizeof(l2), textW, 10);
        }
        vsText(tx, descY + 12, l1, why.empty() ? body : Colour(255, 110, 90, 255), 10, 15);
        vsText(tx, descY + 32, l2, body, 10, 15);
    } else {
        const char* h1;
        const char* h2;
        vsRuleHelp(sVsRulesRow - 1, &h1, &h2);
        vsText(rx + 10, descY + 12, h1, body, 10, 15);
        vsText(rx + 10, descY + 32, h2, body, 10, 15);
    }

    const char* help = es ? "Arriba/Abajo: opción   Izq/Der: cambiar   A: jugar   B: salir"
                          : "Up/Down: option   Left/Right: change   A: play   B: back";
    drawHelpLine(panelX + panelW / 2, panelY + panelH - 38, help, dim);
    sVsPlaySpanOk = helpLineIconSpan('A', &sVsPlayX0, &sVsPlayX1, &sVsPlayY0, &sVsPlayY1);
    pc_gfx_note_menu_tap_space(screenW, screenH);
}


// ── Custom Maps: campañas y misiones sueltas ────────────────────────────────
// Pantallas: la lista (campañas arriba, misiones sueltas debajo) y las tres
// ranuras de una campaña. Sus zonas se eligen en el mapa de zonas (MapSelect).
namespace {
enum MissionScreen { MS_List, MS_Slots, MS_Mods };
bool sMissionOpen    = false;
int sMissionResult   = PC_DEVASSIGN_PENDING;
int sMissionScreen   = MS_List;
int sMissionIndex    = 0;
int sMissionPlayers  = 1; // misiones sueltas: 1 solo, 2 coop (izquierda/derecha)
int sMissionSavedDay = 0; // > 0: preguntando si se sigue esa partida guardada
int sMissionAskRow   = 0; // 0 continuar, 1 empezar de nuevo
std::string sMissionError; // al pulsar A, si no se pudo montar
std::string sMissionInfo;  // lo instalado (paquetes importados)
std::vector<PcMapCatalogEntry> sMissionList;
std::vector<PcMissionRecord> sMissionRecords; // leídos con la lista
// Campañas: la elegida, su ranura y la zona.
int sPackIndex     = 0;
int sSlotRow       = 0;
int sSlotPlayers   = 1;  // partida nueva en una ranura vacía
int sSlotConfirm   = -1; // ranura pendiente de confirmar al borrar
PcCampaignSlotInfo sSlotInfo[PC_CAMPAIGN_SLOTS];
bool sPrevX = false, sPrevY = false;
// Modificadores: para una misión suelta (0) o una partida nueva de campaña (1).
int sModsFor = 0, sModsRow = 0;
PcMissionMods sModsPick;
constexpr int kModTeki[]  = { 50, 75, 100, 150, 200 };
constexpr int kModCarry[] = { 100, 150, 200 };

void missionDone();
const std::vector<PcCampaignPack>& packs();

int modsRows() { return sModsFor == 0 ? 4 : 3; } // «sin bajas» solo en misiones sueltas

void openMods(int forWhat)
{
    sModsFor       = forWhat;
    sModsRow       = 0;
    sModsPick      = PcMissionMods();
    sMissionScreen = MS_Mods;
    sMissionError.clear();
}

template <size_t N> int cycle(const int (&values)[N], int current, bool left)
{
    int i = 0;
    for (size_t k = 0; k < N; k++) {
        if (values[k] == current) i = int(k);
    }
    i = (i + (left ? int(N) - 1 : 1)) % int(N);
    return values[i];
}

void modsInput(bool up, bool down, bool left, bool right, bool accept, bool cancel)
{
    const int n = modsRows();
    if (up || down) sModsRow = (sModsRow + (up ? n - 1 : 1)) % n;
    if (left || right) {
        switch (sModsRow) {
        case 0: sModsPick.tekiHealth = cycle(kModTeki, sModsPick.tekiHealth, left); break;
        case 1: sModsPick.extraDays = (sModsPick.extraDays + (left ? 5 : 1)) % 6; break;
        case 2: sModsPick.carry = cycle(kModCarry, sModsPick.carry, left); break;
        default: sModsPick.noLosses = !sModsPick.noLosses; break;
        }
    }
    if (accept) {
        if (sModsFor == 0) {
            pc_mission_start(false);
            pc_mission_set_mods(sModsPick);
            missionDone();
        } else {
            const PcCampaignPack& pack = packs()[size_t(sPackIndex)];
            if (!pc_campaign_open(pack, sSlotRow, sSlotPlayers, sModsPick)) {
                sMissionError = pack.error.empty() ? "no se puede abrir la ranura" : pack.error;
                return;
            }
            missionDone();
        }
    } else if (cancel) {
        sMissionScreen = sModsFor == 0 ? MS_List : MS_Slots;
    }
}

const std::vector<PcCampaignPack>& packs() { return pc_campaign_packs(false); }
int missionRows() { return int(packs().size() + sMissionList.size()); }
bool missionRowIsPack(int row) { return row < int(packs().size()); }
const PcMapCatalogEntry& missionRowMap(int row) { return sMissionList[size_t(row - int(packs().size()))]; }

void missionRescan(bool rescan)
{
    pc_campaign_packs(rescan);
    sMissionList.clear();
    // Los del modo elegido y los que no se pueden leer fuera de la carpeta de
    // VS (para enseñar el motivo).
    const unsigned mode = sMissionPlayers == 2 ? nectar::MAPMODE_COOP : nectar::MAPMODE_SOLO;
    for (const PcMapCatalogEntry& e : pc_map_catalog(rescan)) {
        if ((e.modes & mode) || (e.modes == 0 && !e.legacy)) sMissionList.push_back(e);
    }
    sMissionRecords.clear();
    for (const PcMapCatalogEntry& e : sMissionList) sMissionRecords.push_back(pc_mission_record(e.id, sMissionPlayers));
}

// Instala un .nectarpack (o .zip) en user/: campaña o mapa suelto.
bool importPack(const std::string& file, std::string& msg)
{
    std::string installed, err;
    if (!nectar::installPack(file, "user", installed, err)) {
        msg = std::filesystem::path(file).filename().string() + ": " + err;
        return false;
    }
    msg = std::filesystem::path(installed).filename().string();
    fprintf(stderr, "[packs] %s -> %s\n", file.c_str(), installed.c_str());
    return true;
}

// Los paquetes dejados en user/import/ se instalan solos (así también en
// Android): el archivo queda como .imported (o .failed, con el motivo).
void autoImport()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string done, failed;
    for (fs::directory_iterator it("user/import", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string ext = it->path().extension().string();
        if (!it->is_regular_file(ec) || (ext != ".nectarpack" && ext != ".zip")) continue;
        std::string msg;
        const bool ok = importPack(it->path().string(), msg);
        (ok ? done : failed) += (ok ? done : failed).empty() ? msg : ", " + msg;
        fs::rename(it->path(), it->path().string() + (ok ? ".imported" : ".failed"), ec);
    }
    const bool es = pc_settings_get_language() == 3;
    if (!done.empty()) sMissionInfo = (es ? "Instalado: " : "Installed: ") + done;
    if (!failed.empty()) sMissionError = (es ? "No se pudo instalar: " : "Couldn't install: ") + failed;
}

void readSlots()
{
    for (int s = 0; s < PC_CAMPAIGN_SLOTS; s++) sSlotInfo[s] = pc_campaign_slot_info(packs()[size_t(sPackIndex)], s);
}

const char* missionGoalText(const nectar::MapData& m, bool es)
{
    switch (m.story.objective) {
    case nectar::MAPGOAL_Parts: return es ? "Recupera piezas de la nave" : "Recover ship parts";
    case nectar::MAPGOAL_Pellets: return es ? "Lleva pastillas a las cebollas" : "Carry pellets to the Onions";
    case nectar::MAPGOAL_Pikmin: return es ? "Consigue Pikmin" : "Grow your Pikmin";
    case nectar::MAPGOAL_Boss: return es ? "Derrota a los jefes" : "Defeat the bosses";
    default: return es ? "Aguanta hasta el anochecer" : "Survive until sunset";
    }
}

void missionDone()
{
    sMissionResult = PC_DEVASSIGN_OK;
    sMissionOpen   = false;
}

// Ranuras: A continúa o empieza (izquierda/derecha: jugadores de la nueva),
// X copia en la primera vacía, Y borra (pulsando dos veces).
void slotsInput(bool up, bool down, bool side, bool accept, bool cancel, bool copy, bool erase)
{
    const PcCampaignPack& pack = packs()[size_t(sPackIndex)];
    if (up || down) {
        sSlotRow     = (sSlotRow + (up ? PC_CAMPAIGN_SLOTS - 1 : 1)) % PC_CAMPAIGN_SLOTS;
        sSlotConfirm = -1;
        sMissionError.clear();
    }
    const PcCampaignSlotInfo& info = sSlotInfo[sSlotRow];
    if (side && !info.used && pack.coop) sSlotPlayers = sSlotPlayers == 2 ? 1 : 2;
    if (copy && info.used) {
        for (int s = 0; s < PC_CAMPAIGN_SLOTS; s++) {
            if (!sSlotInfo[s].used) {
                pc_campaign_slot_copy(pack, sSlotRow, s);
                SeSystem::playSysSe(SYSSE_DECIDE1);
                break;
            }
        }
        readSlots();
    }
    if (erase && info.used) {
        if (sSlotConfirm == sSlotRow) {
            pc_campaign_slot_delete(pack, sSlotRow);
            sSlotConfirm = -1;
            readSlots();
        } else {
            sSlotConfirm = sSlotRow;
        }
        return;
    }
    if (accept && !info.used) {
        openMods(1);
        return;
    }
    if (accept) {
        if (!pc_campaign_open(pack, sSlotRow, 0)) {
            sMissionError = pack.error.empty() ? "no se puede abrir la ranura" : pack.error;
            return;
        }
        // Luego, mandos y capitanes; después, el mapa de zonas (MapSelect).
        sMissionError.clear();
        missionDone();
    } else if (cancel) {
        sMissionScreen = MS_List;
        sSlotConfirm   = -1;
        sMissionError.clear();
    }
}

void pcMissionPromptInput()
{
    if (!sMissionOpen) return;
    bool up     = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = menuOkKey();
    bool cancel = menuCancelKey();
    bool copy   = keyWentDown(SDL_SCANCODE_C);
    bool erase  = keyWentDown(SDL_SCANCODE_DELETE);
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
        const bool x = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_X) != 0;
        const bool y = SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_Y) != 0;
        copy |= x && !sPrevX;
        erase |= y && !sPrevY;
        sPrevX = x;
        sPrevY = y;
    }
    const bool side = left || right;
    if (sMissionScreen == MS_Slots) {
        slotsInput(up, down, side, accept, cancel, copy, erase);
        return;
    }
    if (sMissionScreen == MS_List && sMissionSavedDay == 0 && (erase || keyWentDown(SDL_SCANCODE_I))) {
        // Importar un paquete (.nectarpack) con el diálogo del sistema; la
        // respuesta llega por pollFilePicker() sin parar el dibujo.
#ifndef __ANDROID__
        if (!pc_file_dialog_begin(kPickNectarPack, "Import a .nectarpack", "Nectar pack", "*.nectarpack *.zip"))
            sMissionError = "A file picker is already open.";
#endif
        return;
    }
    if (sMissionScreen == MS_Mods) {
        modsInput(up, down, left, right, accept, cancel);
        return;
    }
    // Partida guardada de una misión suelta: continuar o empezar de nuevo.
    if (sMissionSavedDay > 0) {
        if (up || down) sMissionAskRow ^= 1;
        if (accept) {
            sMissionSavedDay = 0;
            if (sMissionAskRow == 0) {
                pc_mission_start(true); // con los modificadores con los que se empezó
                missionDone();
            } else {
                openMods(0);
            }
        } else if (cancel) {
            sMissionSavedDay = 0;
        }
        return;
    }
    if (side) {
        // 1 jugador <-> 2 jugadores (misiones sueltas): otra lista; se queda en
        // la misma fila si sigue estando.
        const bool wasPack   = missionRows() > 0 && missionRowIsPack(sMissionIndex);
        const std::string id = missionRows() == 0 || wasPack ? std::string() : missionRowMap(sMissionIndex).id;
        sMissionPlayers = sMissionPlayers == 2 ? 1 : 2;
        missionRescan(false);
        if (!wasPack) {
            sMissionIndex = 0;
            for (size_t i = 0; i < sMissionList.size(); i++) {
                if (sMissionList[i].id == id) sMissionIndex = int(packs().size() + i);
            }
        }
        sMissionError.clear();
    }
    const int n = missionRows();
    if (n > 0 && (up || down)) {
        sMissionIndex = (sMissionIndex + (up ? n - 1 : 1)) % n;
        sMissionError.clear();
    }
    if (accept && n > 0 && missionRowIsPack(sMissionIndex)) {
        // Campaña: sus ranuras.
        const PcCampaignPack& pack = packs()[size_t(sMissionIndex)];
        if (!pack.error.empty()) {
            sMissionError = pack.error;
            return;
        }
        sPackIndex     = sMissionIndex;
        sSlotRow       = 0;
        sSlotPlayers   = 1;
        sSlotConfirm   = -1;
        sMissionScreen = MS_Slots;
        readSlots();
        sMissionError.clear();
    } else if (accept && n > 0) {
        // Se monta ya: si el mapa no vale, se dice aquí y no se empieza.
        const PcMapCatalogEntry& e = missionRowMap(sMissionIndex);
        pc_mission_select(e.id, sMissionPlayers);
        const char* why = pc_mission_map() ? pc_vs_arena_prepare() : pc_mission_error().c_str();
        if (why) {
            sMissionError = why;
            return;
        }
        sConfig.missionMap = e.id;
        saveConfig();
        if (const int day = pc_mission_saved_day()) {
            sMissionSavedDay = day;
            sMissionAskRow   = 0;
            return;
        }
        openMods(0);
    } else if (cancel) {
        sMissionResult = PC_DEVASSIGN_CANCELLED;
        sMissionOpen   = false;
    }
}

// Miniatura en (x, y) de alto `th`; devuelve el ancho (0 si no hay).
int missionThumb(DGXGraphics* gfx, const std::string& file, int x, int y, int th, int maxW)
{
    if (file.empty()) return 0;
    const std::string name = "@" + file;
    int aw = 0, ah = 0;
    Texture* thumb = pc_art_texture(name.c_str());
    if (!thumb || !pc_art_size(name.c_str(), &aw, &ah) || aw <= 0 || ah <= 0) return 0;
    const int tw = std::min(maxW, aw * th / ah);
    const Colour tint(255, 255, 255, 255);
    const float u1 = float(aw) / thumb->mWidth, v1 = float(ah) / thumb->mHeight;
    if (pc_settings_p2d_active()) {
        pc_settings_p2d_image(x, y, tw, th, thumb, u1, v1, tint, 0.0f);
    } else {
        gfx->setColour(tint, true);
        gfx->setAuxColour(tint);
        gfx->useTexture(thumb, GX_TEXMAP0);
        gfx->drawRectangle(RectArea(x, y, x + tw, y + th), RectArea(0, 0, aw, ah), nullptr);
        gfx->useTexture(nullptr, GX_TEXMAP0);
    }
    return tw;
}

void missionCursor(int x, int y)
{
    if (pc_settings_p2d_active()) pc_settings_p2d_cursor(x, y, 18, std::fmod(SDL_GetTicks() / 1000.0f * 10.0f, 6.2831853f));
}
} // namespace

void pc_mission_prompt_open(void) {
    sMissionOpen   = true;
    sMissionResult = PC_DEVASSIGN_PENDING;
    sMissionScreen = MS_List;
    pc_campaign_close();
    sMissionError.clear();
    sMissionInfo.clear();
    autoImport();
    // La lista se vuelve a leer cada vez (mapas copiados con el juego abierto).
    missionRescan(true);
    sMissionIndex = 0;
    for (size_t i = 0; i < sMissionList.size(); i++) {
        if (sMissionList[i].id == sConfig.missionMap) sMissionIndex = int(packs().size() + i);
    }
    sMissionSavedDay = 0;
    pc_menu_edge_reset();
}
bool pc_mission_prompt_active(void) { return sMissionOpen; }
int pc_mission_prompt_players(void) { return sMissionPlayers; }
int pc_mission_prompt_result(void) { return sMissionResult; }

void pc_mission_prompt_draw(void) {
    if (!sMissionOpen) return;
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

    const Colour title(255, 229, 120, 255), body(214, 224, 245, 255), dim(150, 165, 195, 255), bad(255, 110, 90, 255);
    const Colour selC(255, 170, 30, 255), rowC(190, 200, 220, 255), packC(150, 220, 255, 255);
    const int panelW = std::min(800, screenW - 24), panelH = 452;
    const int panelX = screenW / 2 - panelW / 2, panelY = screenH / 2 - panelH / 2;
    drawPikminPanel(gfx, panelX, panelY, panelW, panelH, 22);
    const int colW = (panelW - 72) / 2;
    const int lx   = panelX + 24;
    const int rx   = lx + colW + 24;
    constexpr int kRowH = 30, kRows = 9;
    const int listY = panelY + 88;
    char line[160];

    // ── Modificadores ───────────────────────────────────────────────────────
    if (sMissionScreen == MS_Mods) {
        drawPikminHeader(gfx, panelX, panelY, panelW, es ? "Modificadores" : "Modifiers");
        vsText(lx + 8, panelY + 56, es ? "ANTES DE EMPEZAR" : "BEFORE YOU START", title, 14, 21);
        for (int r = 0; r < modsRows(); r++) {
            const bool sel = r == sModsRow;
            const int ry   = listY + r * 40;
            if (sel) missionCursor(lx - 8, ry + 8);
            const char* label = "";
            char value[48];
            switch (r) {
            case 0:
                label = es ? "Vida de los enemigos" : "Enemy health";
                snprintf(value, sizeof(value), "%d%%", sModsPick.tekiHealth);
                break;
            case 1:
                label = es ? "Días extra" : "Extra days";
                snprintf(value, sizeof(value), "+%d", sModsPick.extraDays);
                break;
            case 2:
                label = es ? "Velocidad de carga" : "Carry speed";
                snprintf(value, sizeof(value), "%d%%", sModsPick.carry);
                break;
            default:
                label = es ? "Sin bajas (pierdes si cae uno)" : "No losses (lose if one falls)";
                snprintf(value, sizeof(value), "%s", sModsPick.noLosses ? (es ? "Sí" : "Yes") : "No");
                break;
            }
            vsText(lx + 10, ry, label, sel ? selC : rowC, 11, 17);
            snprintf(line, sizeof(line), sel ? "< %s >" : "%s", value);
            vsText(lx + panelW - 90 - vsTextW(line, 11), ry, line, sel ? selC : body, 11, 17);
        }
        vsText(lx + 10, listY + modsRows() * 40 + 10,
               sModsPick.any() ? (es ? "Con modificadores, los récords van aparte." : "With modifiers, records are kept apart.")
                               : (es ? "Partida normal." : "Normal game."),
               dim, 10, 15);
        if (!sMissionError.empty()) {
            vsFitText(sMissionError.c_str(), line, sizeof(line), panelW - 60, 10);
            vsText(lx, panelY + panelH - 66, line, bad, 10, 15);
        }
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 38,
                     es ? "Arriba/Abajo: opción   Izq/Der: cambiar   A: empezar   B: atrás" : "Up/Down: option   Left/Right: change   A: start   B: back", dim);
        return;
    }

    // ── Ranuras de una campaña ──────────────────────────────────────────────
    if (sMissionScreen == MS_Slots) {
        const PcCampaignPack& pack = packs()[size_t(sPackIndex)];
        drawPikminHeader(gfx, panelX, panelY, panelW, pc_campaign_text(pack.doc.name, pack.doc.nameEn).c_str());
        vsText(lx + 8, panelY + 56, es ? "PARTIDAS" : "SAVE FILES", title, 14, 21);
        for (int s = 0; s < PC_CAMPAIGN_SLOTS; s++) {
            const PcCampaignSlotInfo& info = sSlotInfo[s];
            const bool sel                 = s == sSlotRow;
            const int y                    = listY + s * 92;
            vsCard(gfx, lx - 4, y - 6, panelW - 40, 84);
            if (sel) missionCursor(lx - 8, y + 8);
            snprintf(line, sizeof(line), "%s %d", es ? "Ranura" : "Slot", s + 1);
            vsText(lx + 12, y, line, sel ? selC : rowC, 12, 18);
            if (!info.used) {
                if (pack.coop)
                    snprintf(line, sizeof(line), es ? "Nueva partida   < %s >" : "New game   < %s >",
                             sSlotPlayers == 2 ? (es ? "2 jugadores" : "2 players") : (es ? "1 jugador" : "1 player"));
                else snprintf(line, sizeof(line), "%s", es ? "Nueva partida" : "New game");
                vsText(lx + 12, y + 28, line, body, 10, 15);
                continue;
            }
            snprintf(line, sizeof(line), es ? "Día %d   Piezas %d/%d   Pikmin %d   Zonas %d (%d completadas)%s"
                                            : "Day %d   Parts %d/%d   Pikmin %d   Zones %d (%d complete)%s",
                     info.day, info.parts, pack.partTotal, info.pikmin, info.zonesOpen, info.zonesDone,
                     info.players == 2 ? (es ? "   Coop" : "   Co-op") : "");
            vsText(lx + 12, y + 28, line, body, 10, 15);
            if (info.ending >= 0)
                vsText(lx + 12, y + 50, info.ending == 1 ? (es ? "Campaña completada" : "Campaign complete") : (es ? "Se acabaron los días" : "Out of days"),
                       title, 10, 15);
            if (sSlotConfirm == s) vsText(lx + 300, y, es ? "Pulsa Y / Supr otra vez para borrar" : "Press Y / Del again to erase", bad, 10, 15);
        }
        if (!sMissionError.empty()) {
            vsFitText(sMissionError.c_str(), line, sizeof(line), panelW - 60, 10);
            vsText(lx, panelY + panelH - 66, line, bad, 10, 15);
        }
        const char* help = es ? "A: jugar   X / C: copiar   Y / Supr: borrar   B: atrás" : "A: play   X / C: copy   Y / Del: erase   B: back";
        drawHelpLine(panelX + panelW / 2, panelY + panelH - 38, help, dim);
        return;
    }

    // ── La lista ────────────────────────────────────────────────────────────
    drawPikminHeader(gfx, panelX, panelY, panelW, es ? "Mapas personalizados" : "Custom Maps");
    const bool two = sMissionPlayers == 2;
    vsText(lx + 8, panelY + 56, two ? (es ? "< MISIONES: 2 JUGADORES >" : "< MISSIONS: 2 PLAYERS >") : (es ? "< MISIONES: 1 JUGADOR >" : "< MISSIONS: 1 PLAYER >"),
           title, 14, 21);
    const int n = missionRows();
    if (n == 0) {
        vsText(lx + 10, listY, es ? "No hay campañas ni misiones." : "No campaigns or missions yet.", body, 11, 17);
        vsText(lx + 10, listY + 26, es ? "Campañas: user/campaigns/<nombre>/" : "Campaigns: user/campaigns/<name>/", dim, 10, 15);
        vsText(lx + 10, listY + 46, es ? "Misiones: user/maps/<nombre>/" : "Missions: user/maps/<name>/", dim, 10, 15);
    }
    const int first = std::clamp(sMissionIndex - kRows / 2, 0, std::max(0, n - kRows));
    for (int i = first; i < n && i < first + kRows; i++) {
        const bool pack = missionRowIsPack(i);
        const bool sel  = i == sMissionIndex;
        const int ry    = listY + (i - first) * kRowH;
        if (sel) missionCursor(lx - 8, ry + 8);
        char name[96];
        const std::string& error = pack ? packs()[size_t(i)].error : missionRowMap(i).error;
        if (pack) {
            snprintf(line, sizeof(line), "%s %s", es ? "Campaña:" : "Campaign:", pc_campaign_text(packs()[size_t(i)].doc.name, packs()[size_t(i)].doc.nameEn).c_str());
        } else {
            const PcMapCatalogEntry& e = missionRowMap(i);
            const PcMissionRecord& rec = sMissionRecords[size_t(i) - packs().size()];
            const char* medal          = pc_mission_medal_name(rec.medal, es);
            snprintf(line, sizeof(line), "%s%s%s%s", e.name.c_str(), rec.wins ? "  [" : "", rec.wins ? (*medal ? medal : "OK") : "", rec.wins ? "]" : "");
        }
        vsFitText(line, name, sizeof(name), colW - 20, 11);
        const Colour c = !error.empty() ? bad : sel ? selC : pack ? packC : rowC;
        vsText(lx + 10, ry, name, c, 11, 17);
    }

    // ── Derecha: la ficha de la campaña elegida ─────────────────────────────
    if (n > 0 && missionRowIsPack(std::clamp(sMissionIndex, 0, n - 1))) {
        const PcCampaignPack& c = packs()[size_t(std::clamp(sMissionIndex, 0, n - 1))];
        int y = panelY + 52;
        const int tx = rx + 10;
        vsCard(gfx, rx - 4, y, colW + 8, 300);
        if (missionThumb(gfx, c.thumb, tx, y + 10, 96, colW - 16)) y += 106;
        vsFitText(pc_campaign_text(c.doc.name, c.doc.nameEn).c_str(), line, sizeof(line), colW - 20, 14);
        vsText(tx, y + 12, line, title, 14, 21);
        char by[96];
        snprintf(by, sizeof(by), "%s %s", es ? "De" : "By", c.doc.author.empty() ? "?" : c.doc.author.c_str());
        vsFitText(by, line, sizeof(line), colW - 20, 10);
        vsText(tx, y + 40, line, body, 10, 15);
        const std::string& why = !sMissionError.empty() ? sMissionError : c.error;
        if (!why.empty()) {
            vsText(tx, y + 70, es ? "No se puede jugar:" : "Can't be played:", bad, 10, 15);
            vsFitText(why.c_str(), line, sizeof(line), colW - 20, 10);
            vsText(tx, y + 90, line, body, 10, 15);
        } else {
            snprintf(line, sizeof(line), es ? "%zu zonas, %d piezas" : "%zu zones, %d parts", c.doc.zones.size(), c.partTotal);
            vsText(tx, y + 70, line, body, 10, 15);
            vsText(tx, y + 92, c.coop ? (es ? "1 o 2 jugadores" : "1 or 2 players") : (es ? "1 jugador" : "1 player"), body, 10, 15);
            if (c.doc.ending.dayLimit > 0) {
                snprintf(line, sizeof(line), es ? "Límite: %d días" : "Limit: %d days", c.doc.ending.dayLimit);
                vsText(tx, y + 114, line, body, 10, 15);
            }
        }
    }
    // ── Derecha: la ficha del mapa elegido ──────────────────────────────────
    else if (n > 0) {
        const PcMapCatalogEntry& e = missionRowMap(std::clamp(sMissionIndex, 0, n - 1));
        int y = panelY + 52;
        const int tx = rx + 10;
        vsCard(gfx, rx - 4, y, colW + 8, 300);
        if (missionThumb(gfx, e.thumb, tx, y + 10, 96, colW - 16)) y += 106;
        vsFitText(e.name.c_str(), line, sizeof(line), colW - 20, 14);
        vsText(tx, y + 12, line, title, 14, 21);
        char by[96];
        snprintf(by, sizeof(by), "%s %s", es ? "De" : "By", e.author.empty() ? "?" : e.author.c_str());
        vsFitText(by, line, sizeof(line), colW - 20, 10);
        vsText(tx, y + 40, line, body, 10, 15);
        const std::string& why = !sMissionError.empty() ? sMissionError : e.error;
        if (!why.empty()) {
            vsText(tx, y + 70, es ? "No se puede jugar:" : "Can't be played:", bad, 10, 15);
            vsFitText(why.c_str(), line, sizeof(line), colW - 20, 10);
            vsText(tx, y + 90, line, body, 10, 15);
        } else {
            // La ficha lee el mapa una vez.
            nectar::MapData m;
            std::string err;
            static std::string sInfoId;
            static nectar::MapData sInfo;
            if (sInfoId != e.id && pc_map_catalog_load(e.file, m, err)) {
                sInfo   = m;
                sInfoId = e.id;
            }
            if (sInfoId == e.id) {
                vsText(tx, y + 70, es ? "OBJETIVO" : "GOAL", title, 12, 18);
                vsText(tx, y + 92, missionGoalText(sInfo, es), body, 10, 15);
                const int* sp = sInfo.story.startPikmin;
                snprintf(line, sizeof(line), es ? "Pikmin al empezar: %d" : "Starting Pikmin: %d", sp[0] + sp[1] + sp[2]);
                vsText(tx, y + 114, line, body, 10, 15);
                snprintf(line, sizeof(line), es ? "Días: %d" : "Days: %d", sInfo.story.days);
                vsText(tx, y + 134, line, body, 10, 15);
                const int* md = sInfo.story.medals;
                if (md[0] || md[1] || md[2]) {
                    snprintf(line, sizeof(line), es ? "Medallas: %d / %d / %d Pikmin" : "Medals: %d / %d / %d Pikmin", md[0], md[1], md[2]);
                    vsText(tx, y + 154, line, body, 10, 15);
                }
                const PcMissionRecord& rec = sMissionRecords[size_t(std::clamp(sMissionIndex, 0, n - 1)) - packs().size()];
                if (rec.wins) {
                    const char* medal = pc_mission_medal_name(rec.medal, es);
                    snprintf(line, sizeof(line), es ? "Récord: %s%s%d Pikmin, %d día(s)" : "Best: %s%s%d Pikmin, %d day(s)", medal, *medal ? ", " : "",
                             rec.pikmin, rec.days);
                } else {
                    snprintf(line, sizeof(line), "%s", es ? "Aún sin cumplir" : "Not completed yet");
                }
                vsText(tx, y + 176, line, title, 10, 15);
            }
        }
    }

    // Partida guardada: la pregunta, encima de la ficha.
    if (sMissionSavedDay > 0) {
        const int bx = rx - 4, by = panelY + 250, bw = colW + 8;
        vsCard(gfx, bx, by, bw, 112);
        char head[64];
        snprintf(head, sizeof(head), es ? "Partida guardada: día %d" : "Saved game: day %d", sMissionSavedDay);
        vsText(bx + 14, by + 10, head, title, 12, 18);
        const char* rows[2] = { es ? "Continuar" : "Continue", es ? "Empezar de nuevo" : "Start over" };
        for (int r = 0; r < 2; r++) {
            const bool sel = r == sMissionAskRow;
            const int ry   = by + 42 + r * 30;
            if (sel) missionCursor(bx + 6, ry + 8);
            vsText(bx + 26, ry, rows[r], sel ? selC : body, 11, 17);
        }
    }

    // Paquetes instalados (o no) y otros avisos.
    if (!sMissionInfo.empty()) {
        vsFitText(sMissionInfo.c_str(), line, sizeof(line), panelW - 60, 10);
        vsText(lx, panelY + panelH - 84, line, Colour(140, 230, 120, 255), 10, 15);
    }
    if (!sMissionError.empty() && n == 0) {
        vsFitText(sMissionError.c_str(), line, sizeof(line), panelW - 60, 10);
        vsText(lx, panelY + panelH - 64, line, bad, 10, 15);
    }
    const char* help = es ? "Arriba/Abajo: elegir   Izq/Der: jugadores   A: jugar   Y / I: importar   B: salir"
                          : "Up/Down: choose   Left/Right: players   A: play   Y / I: import   B: back";
    drawHelpLine(panelX + panelW / 2, panelY + panelH - 38, help, dim);
}

// ── Campañas: el mapa de zonas ──────────────────────────────────────────────
// Sustituye al mapa del mundo del juego (MapSelect) cuando hay una campaña: la
// imagen (la del juego o la del paquete), los caminos, las zonas y la nave,
// que vuela a la zona elegida. Las zonas recién abiertas aparecen una a una.
namespace {
bool sCmapOpen    = false;
int sCmapResult   = PC_DEVASSIGN_PENDING;
int sCmapSel      = 0;     // zona bajo el cursor
int sCmapShip     = 0;     // zona donde está la nave
int sCmapTarget   = -1;    // volando hacia esta zona
Uint32 sCmapFlyAt = 0;     // cuándo empezó el vuelo
bool sCmapQuit    = false; // preguntando si se vuelve al título
bool sCmapIntro   = false; // enseñando la introducción de la campaña
std::string sCmapError;
std::vector<int> sCmapReveal; // zonas nuevas por enseñar
Uint32 sCmapRevealAt = 0;
constexpr Uint32 kCmapFlyMs = 1300, kCmapRevealMs = 900;

// Rectángulo de la imagen en pantalla (la que se dibujó por última vez).
int sCmapX = 0, sCmapY = 0, sCmapW = 1, sCmapH = 1;
int sCmapScreenW = 640, sCmapScreenH = 480; // el lienzo virtual del último dibujo

const PcCampaignPack* cmapPack() { return pc_campaign_pack(); }

bool cmapRevealing(int zone, float* t)
{
    if (sCmapReveal.empty()) return false;
    const float k = float(SDL_GetTicks() - sCmapRevealAt) / float(kCmapRevealMs);
    for (size_t i = 0; i < sCmapReveal.size(); i++) {
        if (sCmapReveal[i] != zone) continue;
        if (t) *t = std::clamp(k - float(i), 0.0f, 1.0f);
        return k < float(i) + 1.0f;
    }
    return false;
}

// Abierta y ya enseñada (o enseñándose).
bool cmapShown(int zone)
{
    if (!pc_campaign_zone_open(zone)) return false;
    float t = 1.0f;
    if (cmapRevealing(zone, &t)) return t > 0.0f;
    return true;
}

// Índices de más allá de las zonas: los puntos de interés.
int cmapZones() { return int(cmapPack()->doc.zones.size()); }
int cmapNodes() { return cmapZones() + int(cmapPack()->doc.pois.size()); }

void cmapPoint(int zone, float& x, float& y)
{
    const PcCampaignPack* pack = cmapPack();
    double px, py;
    if (zone < cmapZones()) {
        px = pack->doc.zones[size_t(zone)].x;
        py = pack->doc.zones[size_t(zone)].y;
    } else {
        px = pack->doc.pois[size_t(zone - cmapZones())].x;
        py = pack->doc.pois[size_t(zone - cmapZones())].y;
    }
    x = float(sCmapX) + float(px) * float(sCmapW);
    y = float(sCmapY) + float(py) * float(sCmapH);
}

// Texto partido en líneas de `w` de ancho desde (x, y); devuelve la y de debajo.
int drawWrapped(int x, int y, int w, const std::string& text, Colour c, int maxY, int fw = 10, int fh = 15)
{
    std::istringstream words(text);
    std::string word, cur;
    while (words >> word && y < maxY) {
        const std::string next = cur.empty() ? word : cur + " " + word;
        if (vsTextW(next.c_str(), fw) > w && !cur.empty()) {
            vsText(x, y, cur.c_str(), c, fw, fh);
            y += fh + 5;
            cur = word;
        } else {
            cur = next;
        }
    }
    if (!cur.empty() && y < maxY) {
        vsText(x, y, cur.c_str(), c, fw, fh);
        y += fh + 5;
    }
    return y;
}

// Una PNG del paquete, encajada en (maxW, maxH); devuelve el alto dibujado.
int drawPackImage(const std::string& dir, const std::string& file, int x, int y, int maxW, int maxH)
{
    if (file.empty()) return 0;
    const std::string name = "@" + dir + "/" + file;
    int iw = 0, ih = 0;
    Texture* t = pc_art_texture(name.c_str());
    if (!t || !pc_art_size(name.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return 0;
    int w = maxW, h = maxW * ih / iw;
    if (h > maxH) {
        h = maxH;
        w = maxH * iw / ih;
    }
    pc_settings_p2d_image(x + (maxW - w) / 2, y, w, h, t, float(iw) / t->mWidth, float(ih) / t->mHeight, Colour(255, 255, 255, 255));
    return h;
}

// La zona más cercana en esa dirección (dx, dy), dentro de un cono de 60°.
int cmapNeighbour(int from, float dx, float dy)
{
    const PcCampaignPack* pack = cmapPack();
    float fx, fy;
    cmapPoint(from, fx, fy);
    int best      = -1;
    float bestCost = 1e9f;
    (void)pack;
    for (int z = 0; z < cmapNodes(); z++) {
        if (z == from) continue;
        float x, y;
        cmapPoint(z, x, y);
        const float vx = x - fx, vy = y - fy, len = std::sqrt(vx * vx + vy * vy);
        if (len < 1.0f) continue;
        const float cosA = (vx * dx + vy * dy) / len;
        if (cosA < 0.5f) continue;
        const float cost = len * (2.0f - cosA) * (z >= cmapZones() ? 1.3f : pc_campaign_zone_open(z) ? 1.0f : 1.6f);
        if (cost < bestCost) {
            bestCost = cost;
            best     = int(z);
        }
    }
    return best;
}

void cmapEnter(int zone)
{
    std::string why;
    if (!pc_campaign_enter(zone, why)) {
        sCmapError  = why;
        sCmapTarget = -1;
        return;
    }
    sCmapResult = PC_DEVASSIGN_OK;
    sCmapOpen   = false;
}

void pcCampaignMapInput()
{
    if (!sCmapOpen || !cmapPack()) return;
    // Ratón o toque: el punto del lienzo (encajado en la ventana, centrado).
    int tapNode = -1;
    if (sTouchTapPending && !sCmapIntro && !sCmapQuit && sCmapTarget < 0) {
        int dw = 0, dh = 0;
        pc_gfx_get_drawable_size(&dw, &dh);
        if (dw > 0 && dh > 0) {
            const float scale = std::min(float(dw) / sCmapScreenW, float(dh) / sCmapScreenH);
            const float ox = (dw - sCmapScreenW * scale) * 0.5f, oy = (dh - sCmapScreenH * scale) * 0.5f;
            const float tx = (sTouchTapX * dw - ox) / scale, ty = (sTouchTapY * dh - oy) / scale;
            float best = 24.0f * 24.0f;
            for (int n = 0; n < cmapNodes(); n++) {
                float x, y;
                cmapPoint(n, x, y);
                const float d = (x - tx) * (x - tx) + (y - ty) * (y - ty);
                if (d < best) {
                    best    = d;
                    tapNode = n;
                }
            }
        }
    }
    bool up     = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down   = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool accept = menuOkKey();
    bool cancel = menuCancelKey();
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
    if (sCmapIntro) {
        if (accept || cancel) sCmapIntro = false;
        return;
    }
    // Mientras vuela la nave o aparecen zonas, nada.
    if (sCmapTarget >= 0) return;
    if (!sCmapReveal.empty()) {
        if (SDL_GetTicks() - sCmapRevealAt < kCmapRevealMs * sCmapReveal.size()) {
            if (accept) sCmapRevealAt = SDL_GetTicks() - kCmapRevealMs * Uint32(sCmapReveal.size()); // A lo salta
            return;
        }
        sCmapReveal.clear();
        pc_campaign_clear_new_zones();
    }
    if (sCmapQuit) {
        if (accept) {
            sCmapResult = PC_DEVASSIGN_CANCELLED;
            sCmapOpen   = false;
        } else if (cancel) {
            sCmapQuit = false;
        }
        return;
    }
    // Tocar una zona la elige; tocar la ya elegida es A.
    if (tapNode >= 0) {
        if (tapNode == sCmapSel) {
            accept = true;
        } else {
            sCmapSel = tapNode;
            sCmapError.clear();
            SeSystem::playSysSe(SYSSE_MOVE1);
        }
    }
    const float dirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    const bool pressed[4]  = { up, down, left, right };
    for (int d = 0; d < 4; d++) {
        if (!pressed[d]) continue;
        const int next = cmapNeighbour(sCmapSel, dirs[d][0], dirs[d][1]);
        if (next >= 0) {
            sCmapSel = next;
            sCmapError.clear();
            SeSystem::playSysSe(SYSSE_MOVE1);
        }
    }
    if (accept && sCmapSel >= cmapZones()) return; // un punto de interés: solo se lee
    if (accept) {
        if (!pc_campaign_zone_open(sCmapSel)) {
            SeSystem::playSysSe(SYSSE_CANCEL);
            sCmapError = pc_campaign_lock_text(sCmapSel, vsSpanish());
            return;
        }
        SeSystem::playSysSe(SYSSE_DECIDE1);
        sCmapError.clear();
        if (sCmapSel == sCmapShip) {
            cmapEnter(sCmapSel);
        } else {
            sCmapTarget = sCmapSel;
            sCmapFlyAt  = SDL_GetTicks();
        }
    } else if (cancel) {
        sCmapQuit = true;
    }
}
} // namespace

void pc_campaign_map_open(void)
{
    const PcCampaignPack* pack = cmapPack();
    sCmapOpen   = pack != nullptr;
    sCmapResult = PC_DEVASSIGN_PENDING;
    sCmapQuit   = false;
    sCmapTarget = -1;
    sCmapError.clear();
    if (!pack) return;
    sCmapShip = std::max(0, pc_campaign_current_zone());
    sCmapSel  = sCmapShip;
    sCmapReveal    = pc_campaign_new_zones();
    sCmapRevealAt  = SDL_GetTicks() + 400;
    // Partida nueva: la introducción, si la hay.
    sCmapIntro = (!pack->doc.intro.empty() || !pack->doc.introImage.empty()) && pc_campaign_progress().visited.empty() && pc_campaign_day() == 1;
    pc_menu_edge_reset();
}
bool pc_campaign_map_active(void) { return sCmapOpen; }
int pc_campaign_map_result(void)
{
    // El vuelo acaba aquí (cada fotograma, aunque no haya entrada).
    if (sCmapOpen && sCmapTarget >= 0 && SDL_GetTicks() - sCmapFlyAt >= kCmapFlyMs) {
        sCmapShip = sCmapTarget;
        cmapEnter(sCmapTarget);
    }
    return sCmapResult;
}

void pc_campaign_map_draw(void)
{
    const PcCampaignPack* pack = cmapPack();
    if (!sCmapOpen || !pack) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;
    const bool es = vsSpanish();

    const int screenW = pc_gfx_menu_wide() ? pc_gfx_menu_virt_width() : gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;
    sCmapScreenW      = screenW;
    sCmapScreenH      = screenH;
    PcSettingsP2DFrame nativeFrame(screenW, screenH);
    GlassTextScope glassText;
    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));
    // Solo dentro del marco (PcSettingsP2DFrame) se sabe si hay capa 2D.
    if (!pc_settings_p2d_active()) return;
    const Colour title(255, 229, 120, 255), body(214, 224, 245, 255), dim(150, 165, 195, 255), bad(255, 110, 90, 255);
    const Colour white(255, 255, 255, 255);
    const float now = SDL_GetTicks() / 1000.0f;
    char line[200];

    // ── La imagen del mapa, a la izquierda; el panel, a la derecha ──────────
    const int panelW = std::min(270, screenW / 3);
    const int areaX = 16, areaY = 16, areaW = screenW - panelW - 40, areaH = screenH - 64;
    Texture* map = nullptr;
    int imgW = 4, imgH = 3;
    float u1 = 1.0f, v1 = 1.0f;
    if (pack->doc.worldmap == "game") {
        map = vsIcon("w_map");
        if (map) imgW = map->mWidth, imgH = map->mHeight;
    } else {
        const std::string name = "@" + pack->dir + "/" + pack->doc.worldmap;
        map                    = pc_art_texture(name.c_str());
        if (map && pc_art_size(name.c_str(), &imgW, &imgH) && imgW > 0 && imgH > 0) {
            u1 = float(imgW) / map->mWidth;
            v1 = float(imgH) / map->mHeight;
        }
    }
    sCmapW = areaW;
    sCmapH = areaW * imgH / std::max(1, imgW);
    if (sCmapH > areaH) {
        sCmapH = areaH;
        sCmapW = areaH * imgW / std::max(1, imgH);
    }
    sCmapX = areaX + (areaW - sCmapW) / 2;
    sCmapY = areaY + (areaH - sCmapH) / 2;
    if (map) pc_settings_p2d_image(sCmapX, sCmapY, sCmapW, sCmapH, map, u1, v1, white);
    else vsCard(gfx, sCmapX, sCmapY, sCmapW, sCmapH);

    // ── Caminos: de puntos, entre zonas abiertas ────────────────────────────
    Texture* dot = vsIcon("dot_24");
    for (const auto& p : pack->doc.paths) {
        const int a = pack->doc.find(p.first), b = pack->doc.find(p.second);
        if (a < 0 || b < 0 || !cmapShown(a) || !cmapShown(b)) continue;
        // Si una de las dos está apareciendo, el camino se dibuja poco a poco.
        float ta = 1.0f, tb = 1.0f;
        cmapRevealing(a, &ta);
        cmapRevealing(b, &tb);
        const float t = std::min(ta, tb);
        float ax, ay, bx, by;
        cmapPoint(a, ax, ay);
        cmapPoint(b, bx, by);
        const float len = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        // La capa 2D tiene 256 elementos por fotograma: pocos puntos por camino.
        const int dots  = std::clamp(int(len / 18.0f), 2, 16);
        for (int k = 1; k < dots && float(k) / float(dots) <= t; k++) {
            const float f = float(k) / float(dots);
            const int x = int(ax + (bx - ax) * f), y = int(ay + (by - ay) * f);
            if (dot) pc_settings_p2d_image(x - 3, y - 3, 6, 6, dot, 1.0f, 1.0f, Colour(255, 240, 200, 220));
        }
    }

    // ── Zonas ───────────────────────────────────────────────────────────────
    Texture* point = vsIcon("dot_32");
    for (size_t z = 0; z < pack->doc.zones.size(); z++) {
        float x, y;
        cmapPoint(int(z), x, y);
        float t        = 1.0f;
        const bool rev = cmapRevealing(int(z), &t);
        const bool shown = cmapShown(int(z));
        Colour c = !shown ? Colour(110, 110, 120, 200) : pc_campaign_zone_completed(int(z)) ? Colour(255, 215, 60, 255) : Colour(255, 255, 255, 255);
        int size = shown ? 22 : 12;
        if (rev) size = int(12 + 10 * t + std::sin(t * 3.14159f) * 14.0f); // aparece con un pequeño salto
        if (point) pc_settings_p2d_image(int(x) - size / 2, int(y) - size / 2, size, size, point, 1.0f, 1.0f, c);
        if (int(z) == sCmapSel) pc_settings_p2d_cursor(int(x), int(y), 30, std::fmod(now * 4.0f, 6.2831853f));
        if (shown) {
            const std::string& name = pc_campaign_text(pack->doc.zones[z].name, pack->doc.zones[z].nameEn);
            const int w             = vsTextW(name.c_str(), 9);
            vsText(int(x) - w / 2, int(y) + 14, name.c_str(), int(z) == sCmapSel ? title : body, 9, 13);
        }
    }

    // ── Puntos de interés ───────────────────────────────────────────────────
    Texture* star = vsIcon("star2");
    for (size_t k = 0; k < pack->doc.pois.size(); k++) {
        const nectar::CampaignPoi& poi = pack->doc.pois[k];
        const int node                 = cmapZones() + int(k);
        float x, y;
        cmapPoint(node, x, y);
        if (Texture* t = star ? star : point) pc_settings_p2d_image(int(x) - 8, int(y) - 8, 16, 16, t, 1.0f, 1.0f, Colour(140, 220, 255, 255));
        if (node == sCmapSel) pc_settings_p2d_cursor(int(x), int(y), 24, std::fmod(now * 4.0f, 6.2831853f));
        const std::string& name = pc_campaign_text(poi.name, poi.nameEn);
        const int w             = vsTextW(name.c_str(), 9);
        vsText(int(x) - w / 2, int(y) + 10, name.c_str(), node == sCmapSel ? title : Colour(170, 220, 255, 255), 9, 13);
    }

    // ── La nave: flota sobre su zona o vuela a la elegida ───────────────────
    if (Texture* ship = vsIcon("i_rocket2")) {
        float sx, sy;
        cmapPoint(sCmapShip, sx, sy);
        if (sCmapTarget >= 0) {
            float tx, ty;
            cmapPoint(sCmapTarget, tx, ty);
            const float k = std::clamp(float(SDL_GetTicks() - sCmapFlyAt) / float(kCmapFlyMs), 0.0f, 1.0f);
            const float e = k * k * (3.0f - 2.0f * k); // suave al salir y al llegar
            sx += (tx - sx) * e;
            sy += (ty - sy) * e - std::sin(k * 3.14159f) * 46.0f; // en arco
        }
        sy += std::sin(now * 2.5f) * 3.0f - 30.0f;
        pc_settings_p2d_image(int(sx) - 16, int(sy) - 20, 32, 40, ship, 1.0f, 1.0f, white);
    }

    // ── Panel ───────────────────────────────────────────────────────────────
    const int px = screenW - panelW - 16, pw = panelW;
    vsCard(gfx, px, 16, pw, screenH - 64);
    int y = 28;
    vsFitText(pc_campaign_text(pack->doc.name, pack->doc.nameEn).c_str(), line, sizeof(line), pw - 24, 12);
    vsText(px + 12, y, line, title, 12, 18);
    y += 28;
    const nectar::CampaignProgress& prog = pc_campaign_progress();
    snprintf(line, sizeof(line), es ? "Día %d%s" : "Day %d%s", prog.day,
             pc_campaign_day_limit() > 0 ? (std::string(" / ") + std::to_string(pc_campaign_day_limit())).c_str() : "");
    vsText(px + 12, y, line, body, 10, 15);
    y += 20;
    snprintf(line, sizeof(line), es ? "Piezas %zu / %d" : "Parts %zu / %d", prog.parts.size(), pack->partTotal);
    vsText(px + 12, y, line, body, 10, 15);
    y += 20;
    {
        char raw[96];
        snprintf(raw, sizeof(raw), es ? "Pikmin: %d rojos, %d amarillos, %d azules" : "Pikmin: %d red, %d yellow, %d blue", prog.pikmin[1], prog.pikmin[2],
                 prog.pikmin[0]);
        vsFitText(raw, line, sizeof(line), pw - 24, 10);
        vsText(px + 12, y, line, body, 10, 15);
    }
    y += 32;

    // Un punto de interés: su nombre y su texto.
    if (sCmapSel >= cmapZones()) {
        const nectar::CampaignPoi& poi = pack->doc.pois[size_t(sCmapSel - cmapZones())];
        vsFitText(pc_campaign_text(poi.name, poi.nameEn).c_str(), line, sizeof(line), pw - 24, 12);
        vsText(px + 12, y, line, title, 12, 18);
        drawWrapped(px + 12, y + 28, pw - 24, pc_campaign_text(poi.text, poi.textEn), body, screenH - 130);
    } else {
    // La zona elegida.
    const nectar::CampaignZone& zone   = pack->doc.zones[size_t(sCmapSel)];
    const PcCampaignZoneInfo& info      = pack->zones[size_t(sCmapSel)];
    const bool open                     = pc_campaign_zone_open(sCmapSel);
    Texture* photo                      = nullptr;
    float pu = 1.0f, pv = 1.0f;
    int phW = 4, phH = 3;
    if (zone.photo.rfind("game:", 0) == 0) {
        photo = vsIcon(zone.photo.substr(5).c_str());
        if (photo) phW = photo->mWidth, phH = photo->mHeight;
    } else {
        const std::string file = zone.photo.empty() ? info.thumb : pack->dir + "/" + zone.photo;
        if (!file.empty()) {
            const std::string name = "@" + file;
            photo                  = pc_art_texture(name.c_str());
            if (photo && pc_art_size(name.c_str(), &phW, &phH) && phW > 0 && phH > 0) {
                pu = float(phW) / photo->mWidth;
                pv = float(phH) / photo->mHeight;
            }
        }
    }
    if (photo && open) {
        const int w = pw - 24, h = std::min(140, w * phH / std::max(1, phW));
        pc_settings_p2d_image(px + 12, y, w, h, photo, pu, pv, white);
        y += h + 10;
    }
    vsFitText(open ? pc_campaign_text(zone.name, zone.nameEn).c_str() : "???", line, sizeof(line), pw - 24, 12);
    vsText(px + 12, y, line, title, 12, 18);
    y += 26;
    if (!open) {
        vsText(px + 12, y, es ? "Cerrada. Falta:" : "Locked. Needs:", bad, 10, 15);
        y += 20;
        const std::string lock = pc_campaign_lock_text(sCmapSel, es);
        vsFitText(lock.c_str(), line, sizeof(line), pw - 24, 10);
        vsText(px + 12, y, line, body, 10, 15);
        y += 20;
    } else {
        snprintf(line, sizeof(line), es ? "Piezas: %d / %zu" : "Parts: %d / %zu", pc_campaign_zone_parts_found(sCmapSel), info.parts.size());
        vsText(px + 12, y, line, body, 10, 15);
        y += 20;
        const char* medal = pc_mission_medal_name(pc_campaign_zone_medal(sCmapSel), es);
        if (pc_campaign_zone_completed(sCmapSel)) snprintf(line, sizeof(line), "%s%s%s", es ? "Completada" : "Complete", *medal ? " - " : "", medal);
        else snprintf(line, sizeof(line), "%s", info.hasGoal ? (es ? "Con un objetivo por cumplir" : "Has a goal to complete") : (es ? "Para explorar" : "To explore"));
        vsText(px + 12, y, line, body, 10, 15);
        y += 20;
    }
    }
    if (!sCmapError.empty()) {
        vsFitText(sCmapError.c_str(), line, sizeof(line), pw - 24, 10);
        vsText(px + 12, y + 6, line, bad, 10, 15);
    }
    if (pc_campaign_ending() == 1) vsText(px + 12, screenH - 112, es ? "¡Campaña completada!" : "Campaign complete!", title, 11, 17);
    else if (pc_campaign_ending() == 0) vsText(px + 12, screenH - 112, es ? "Se acabaron los días" : "Out of days", bad, 11, 17);

    // Aviso de zonas nuevas.
    if (!sCmapReveal.empty()) {
        const int k = std::clamp(int((SDL_GetTicks() - sCmapRevealAt) / kCmapRevealMs), 0, int(sCmapReveal.size()) - 1);
        const nectar::CampaignZone& nz = pack->doc.zones[size_t(sCmapReveal[size_t(k)])];
        snprintf(line, sizeof(line), es ? "¡Nueva zona: %s!" : "New zone: %s!", pc_campaign_text(nz.name, nz.nameEn).c_str());
        const int w = vsTextW(line, 14);
        vsCard(gfx, sCmapX + sCmapW / 2 - w / 2 - 16, sCmapY + sCmapH - 60, w + 32, 40);
        vsText(sCmapX + sCmapW / 2 - w / 2, sCmapY + sCmapH - 50, line, title, 14, 21);
    }

    // Preguntas a pantalla completa.
    if (sCmapIntro || sCmapQuit) {
        const bool introImage = sCmapIntro && !pack->doc.introImage.empty();
        const int bw = std::min(560, screenW - 40), bh = sCmapIntro ? (introImage ? 400 : 220) : 120;
        const int bx = screenW / 2 - bw / 2, by = screenH / 2 - bh / 2;
        drawPikminPanel(gfx, bx, by, bw, bh, 18);
        if (sCmapIntro) {
            vsText(bx + 20, by + 18, pc_campaign_text(pack->doc.name, pack->doc.nameEn).c_str(), title, 14, 21);
            int ly = by + 52;
            if (introImage) ly += drawPackImage(pack->dir, pack->doc.introImage, bx + 20, ly, bw - 40, 180) + 10;
            drawWrapped(bx + 20, ly, bw - 40, pc_campaign_text(pack->doc.intro, pack->doc.introEn), body, by + bh - 50);
            drawHelpLine(screenW / 2, by + bh - 34, es ? "A: empezar" : "A: start", dim);
        } else {
            vsText(bx + 20, by + 20, es ? "¿Volver al título?" : "Back to the title?", title, 13, 19);
            vsText(bx + 20, by + 50, es ? "La partida se guarda al final de cada día." : "The game is saved at the end of each day.", body, 10, 15);
            drawHelpLine(screenW / 2, by + bh - 34, es ? "A: sí   B: no" : "A: yes   B: no", dim);
        }
        return;
    }
    drawHelpLine(screenW / 2, screenH - 36, sCmapSel >= cmapZones() ? (es ? "Flechas: elegir   B: título" : "Arrows: choose   B: title")
                                                                     : (es ? "Flechas: zona   A: ir   B: título" : "Arrows: zone   A: go   B: title"), dim);
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
    // Netplay (deterministic mode): the countdown, the pause and the end
    // screen are sim state (pc_vs_det_tick_end); this only draws and plays
    // the local sounds. A local menu never holds the shared countdown.
    const bool det = pc_netplay_deterministic();
    // Con un menú del port encima, la cuenta atrás espera (va con reloj real).
    const bool menuOver = sMenuOpen || pc_glass_menu_active();
    if (!det) pc_vs_countdown_set_frozen(menuOver);
    if (menuOver) return;
    const bool live = det ? pc_vs_det_live() : sLastGameplayFrameMs != 0 && SDL_GetTicks() - sLastGameplayFrameMs <= 250;
    if (!live && !sPausedByVs) return;
    if (!gsys || !gsys->mDGXGfx) return;

    // Pausa: durante la cuenta atrás y al acabar la partida.
    const int phase    = pc_vs_countdown_phase();
    const bool over    = pc_vs_match_over();
    const bool wantPause = phase > 0 || over;
    if (!det && wantPause != sPausedByVs) {
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
            const bool sel = i == (pc_netplay_deterministic() ? pc_vs_end_choice() : sVsEndChoice);
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


// Custom Maps: la pantalla de resultados al acabar (la misión o el día).
namespace {
int sMissionEndRow = 0;

const char* missionExitLabel(int kind, bool es)
{
    switch (kind) {
    case PC_MISSION_EXIT_NEXT_DAY: return es ? "Día siguiente" : "Next day";
    case PC_MISSION_EXIT_MAP: return es ? "Al mapa de zonas" : "To the zone map";
    case PC_MISSION_EXIT_RETRY: return es ? "Reintentar" : "Retry";
    default: return es ? "Salir al título" : "Back to title";
    }
}

void pcMissionEndInput()
{
    bool left   = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A) || keyWentDown(SDL_SCANCODE_UP);
    bool right  = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D) || keyWentDown(SDL_SCANCODE_DOWN);
    bool accept = menuOkKey();
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
    int kinds[4];
    const int n = pc_mission_end_options(kinds, 4);
    if (n <= 0) return;
    if (left || right) {
        sMissionEndRow = (sMissionEndRow + (left ? n - 1 : 1)) % n;
        SeSystem::playSysSe(SYSSE_MOVE1);
    }
    if (accept) {
        SeSystem::playSysSe(SYSSE_DECIDE1);
        pc_mission_choose_exit(kinds[std::clamp(sMissionEndRow, 0, n - 1)]);
        sMissionEndRow = 0;
    }
}

void drawMissionEndScreen(DGXGraphics* gfx, int screenW, int screenH, bool es)
{
    const Colour title(255, 229, 120, 255), body(214, 224, 245, 255), dim(150, 165, 195, 255), selC(255, 170, 30, 255);
    const PcMissionStats st = pc_mission_stats();
    const int result        = pc_mission_result();
    // Campaña: el diario de esta noche y, al acabar, la imagen del final.
    const bool zone          = pc_mission_zone_mode() && pc_campaign_active();
    const std::string& diary = zone ? pc_campaign_diary() : std::string();
    const std::string endImg = zone && pc_campaign_ending() >= 0 ? pc_campaign_pack()->doc.ending.image : std::string();
    const int pw = std::min(600, screenW - 40), ph = std::min(screenH - 40, 330 + (diary.empty() ? 0 : 110) + (endImg.empty() ? 0 : 170));
    const int px = screenW / 2 - pw / 2, py = screenH / 2 - ph / 2;
    drawPikminPanel(gfx, px, py, pw, ph, 20);
    const char* head = pc_mission_announcement(es);
    char fit[200];
    vsFitText(head, fit, sizeof(fit), pw - 40, 13);
    vsText(px + 20, py + 18, fit, title, 13, 20);
    char line[160];
    int y = py + 56;
    auto row = [&](const char* label, const char* value) {
        vsText(px + 30, y, label, body, 10, 15);
        vsText(px + pw - 30 - vsTextW(value, 10), y, value, body, 10, 15);
        y += 22;
    };
    char v[64];
    const int secs = int(st.seconds);
    snprintf(v, sizeof(v), "%d:%02d", secs / 60, secs % 60);
    row(es ? "Tiempo de juego" : "Play time", v);
    if (st.days > 1) {
        snprintf(v, sizeof(v), "%d / %d", st.day, st.days);
        row(es ? "Día" : "Day", v);
    } else if (pc_mission_zone_mode()) {
        snprintf(v, sizeof(v), "%d", st.day);
        row(es ? "Día" : "Day", v);
    }
    snprintf(v, sizeof(v), "%d", st.born);
    row(es ? "Pikmin nacidos" : "Pikmin born", v);
    snprintf(v, sizeof(v), "%d", st.lost);
    row(es ? "Pikmin perdidos" : "Pikmin lost", v);
    snprintf(v, sizeof(v), "%d", st.kills);
    row(es ? "Enemigos derrotados" : "Enemies defeated", v);
    if (result == 2 || pc_mission_zone_mode()) {
        // Fin del día: los que pasan la noche en cada cebolla.
        snprintf(v, sizeof(v), es ? "%d rojos, %d amarillos, %d azules" : "%d red, %d yellow, %d blue", st.night[1], st.night[2], st.night[0]);
        row(es ? "A salvo esta noche" : "Safe tonight", v);
        snprintf(v, sizeof(v), "%d", st.lostAtNight);
        row(es ? "Perdidos al anochecer" : "Lost at sunset", v);
    } else {
        snprintf(v, sizeof(v), "%d", st.pikmin);
        row(es ? "Pikmin al acabar" : "Pikmin at the end", v);
        if (st.medal >= 0) row(es ? "Medalla" : "Medal", pc_mission_medal_name(st.medal, es));
        if (st.newRecord) row(es ? "¡Nuevo récord!" : "New record!", "");
    }
    if (pc_mission_mods().any()) {
        vsText(px + 30, y, es ? "Con modificadores (récords aparte)" : "With modifiers (separate records)", dim, 9, 13);
        y += 20;
    }
    if (!endImg.empty()) y += drawPackImage(pc_campaign_pack()->dir, endImg, px + 30, y + 4, pw - 60, 160) + 10;
    if (!diary.empty()) {
        vsText(px + 30, y + 4, es ? "Diario" : "Diary", title, 11, 17);
        drawWrapped(px + 30, y + 28, pw - 60, diary, body, py + ph - 60);
    }
    // Las salidas, en fila.
    int kinds[4];
    const int n   = pc_mission_end_options(kinds, 4);
    const int bw  = (pw - 40) / std::max(1, n);
    for (int i = 0; i < n; i++) {
        const bool sel = i == sMissionEndRow;
        const int bx   = px + 20 + i * bw;
        snprintf(line, sizeof(line), sel ? "> %s <" : "%s", missionExitLabel(kinds[i], es));
        vsText(bx + bw / 2 - vsTextW(line, 11) / 2, py + ph - 44, line, sel ? selC : body, 11, 17);
    }
}
} // namespace

// Custom Maps: el objetivo arriba en el centro y, al acabar, el resultado.
void pc_settings_draw_mission_hud(void) {
    if (!pc_mission_active()) return;
    if (sMenuOpen || pc_glass_menu_active()) return;
    const bool live = sLastGameplayFrameMs != 0 && SDL_GetTicks() - sLastGameplayFrameMs <= 250;
    if (!live && !pc_mission_over() && !pc_mission_textbox()) return; // con el cuadro abierto, el mundo está parado
    if (!gsys || !gsys->mDGXGfx) return;
    static bool sWasOver = false;
    const bool over      = pc_mission_over();
    if (over && !sWasOver) SeSystem::playSysSe(pc_mission_result() > 0 ? SYSSE_WORK_FINISH : SYSSE_TIME_SIGNAL);
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
    char status[64];
    pc_mission_status(status, sizeof(status), es);
    drawTextOutline(cx - menuTextWidth(status) / 2, 18, "%s", Colour(255, 255, 255, 255), shadow, status);
    // Cuenta atrás (de un disparador o del límite de tiempo).
    const f32 left = pc_mission_countdown();
    if (left >= 0.0f) {
        char clock[32];
        const int secs = int(left + 0.999f);
        snprintf(clock, sizeof(clock), "%d:%02d", secs / 60, secs % 60);
        drawTextOutline(cx - menuTextWidth(clock) / 2, 44, "%s", secs <= 10 ? Colour(255, 110, 90, 255) : Colour(255, 229, 120, 255), shadow, clock);
    }
    // Cuadro de texto: el mundo se para mientras está abierto.
    static bool sPausedByBox = false;
    const char* box          = pc_mission_textbox();
    if ((box != nullptr) != sPausedByBox) {
        sPausedByBox       = box != nullptr;
        gameflow.mPauseAll = sPausedByBox ? TRUE : FALSE;
    }
    if (box) {
        const int bw = std::min(620, screenW - 40), bh = 150;
        const int bx = cx - bw / 2, by = screenH - bh - 30;
        drawPikminPanel(gfx, bx, by, bw, bh, 18);
        std::istringstream words(box);
        std::string word, cur;
        int ly = by + 20;
        while (words >> word && ly < by + bh - 40) {
            const std::string next = cur.empty() ? word : cur + " " + word;
            if (vsTextW(next.c_str(), 11) > bw - 40 && !cur.empty()) {
                vsText(bx + 20, ly, cur.c_str(), Colour(255, 255, 255, 255), 11, 17);
                ly += 22;
                cur = word;
            } else {
                cur = next;
            }
        }
        if (!cur.empty()) vsText(bx + 20, ly, cur.c_str(), Colour(255, 255, 255, 255), 11, 17);
        drawHelpLine(cx, by + bh - 30, es ? "A: seguir" : "A: continue", Colour(150, 165, 195, 255));
        return;
    }
    if (pc_mission_end_screen()) {
        drawMissionEndScreen(gfx, screenW, screenH, es);
    } else if (over) {
        const char* msg = pc_mission_announcement(es);
        drawTextOutline(cx - menuTextWidth(msg) / 2, (int)(screenH * 0.30f), "%s", Colour(255, 229, 120, 255), shadow, msg);
    } else if (const char* msg = pc_mission_message(); *msg) {
        // Mensaje de un disparador del mapa.
        drawTextOutline(cx - menuTextWidth(msg) / 2, (int)(screenH * 0.22f), "%s", Colour(255, 255, 255, 255), shadow, msg);
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
        snprintf(head, sizeof(head), "Achievement unlocked");
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

#ifndef __ANDROID__
// Answer of the desktop file picker (pc_file_dialog.h). Runs every frame,
// menu open or not, so an answer is never lost when the menu was closed.
static void pollFilePicker()
{
    int tag = 0;
    bool chosen = false;
    char path[4096];
    if (pc_file_dialog_poll(&tag, &chosen, path, sizeof(path)) != PC_FILE_DIALOG_DONE) return;
    if (tag == kPickNectarPack) {
        if (chosen) {
            std::string msg;
            sMissionInfo.clear();
            sMissionError.clear();
            if (importPack(path, msg)) sMissionInfo = (pc_settings_get_language() == 3 ? "Instalado: " : "Installed: ") + msg;
            else sMissionError = msg;
            missionRescan(true);
        } else if (path[0]) {
            sMissionError = path; // sin diálogo disponible: el motivo
        }
        return;
    }
    sTexturePackPickerActive = false;
    if (!chosen) {
        if (path[0]) texturePackNotice(true, path); // no dialog available: says why
        return;
    }
    char msg[192];
    if (tag == kPickTexturePack) {
        const int written = pc_texpack_install_zip(path, msg, sizeof(msg));
        texturePackNotice(written <= 0, msg);
    } else if (tag >= kPickHdModel && tag < kPickHdModel + 6) {
        const int written = pc_hd_models_convert_file(path, tag - kPickHdModel, msg, sizeof(msg));
        texturePackNotice(written <= 0, msg);
        if (written > 0) sHdModelRestartPrompt = true;
    }
}
#endif

void pc_settings_draw(void) {
#ifndef __ANDROID__
    pollFilePicker();
#endif
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

    // Controller Guide overlay.
    if (sInPadGuide) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Controller Guide",
                           "What each button does now", "Up/Down: scroll   Esc/B: back");
        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = std::max(1, (subH - 70) / itemH);
        const int count = padGuideCount();
        int startIdx = 0;
        if (sPadGuideSel >= visibleItems) startIdx = sPadGuideSel - visibleItems + 1;
        const int endIdx = std::min(count, startIdx + visibleItems);
        for (int i = startIdx; i < endIdx; i++) {
            const int itemY = listStartY + (i - startIdx) * itemH;
            if (padGuideIsHeader(i)) {
                drawTextOutline(subX + 24, itemY + 4, "%s", Colour(255, 214, 120, 255), Colour(10, 16, 36, 255),
                                padGuideLabel(i));
            } else {
                drawSubmenuRow(gfx, subX + 20, itemY, subW - 40, padGuideLabel(i), padGuideValue(i), i == sPadGuideSel);
            }
        }
        if (count > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sPadGuideSel + 1, count);
            drawTextOutline(subX + subW - 12 - menuTextWidth(hint), subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }
        return;
    }

    // Gamepad submenu overlay.
    if (sInGamepadSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Gamepad Controls",
                           sWaitingForButton ? "Press a button, trigger or stick   Esc: cancel"
                                            : "Enter: capture   Left/Right: default/none   Del: none",
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
            if (boundBtn == PC_GP_BIND_NONE) boundBtn = -1;
            else if (boundBtn < 0) boundBtn = pc_window_scheme_default_gamepad_binding(sPending.padScheme, i);
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
                           "Up/Down: model   Left/Right: on/off   A: install zip",
                           "Esc/B: back   A new install needs a restart");

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
                snprintf(value, sizeof(value), "%s",
                         !installed[row] ? "Not installed" : (pc_settings_get_hd_model_enabled(row) ? "On" : "Off"));
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

int pc_settings_get_speedrun_splits_shown(void) { return sConfig.speedrunSplitsShown; }
int pc_settings_get_speedrun_category(void) { return sConfig.speedrunCategory; }
int pc_settings_get_speedrun_livesplit(void) { return sConfig.speedrunLiveSplit; }
int pc_settings_get_speedrun_ls_hide_clock(void) { return sConfig.speedrunLsHideClock; }
const char* pc_settings_get_speedrun_ls_host(void) { return sConfig.speedrunLsHost; }
int pc_settings_get_speedrun_ls_port(void) { return sConfig.speedrunLsPort; }
int pc_settings_get_online_stun(void) { return sConfig.onlineStun; }
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
    // Misiones y campañas: el modificador elegido al empezar.
    if (pc_mission_active()) return pc_mission_mods().tekiHealth;
    return pc_hardmode_active() ? 100 : sConfig.tekiHealthPct;
}

int pc_settings_get_infinite_day(void) { if (pc_speedrun_active()) return 0;
    // VS: el día no avanza; la partida la cierra su propio reloj.
    if (pc_vs_active()) return 1;
    // Misión: el día acaba con el reloj (decide el resultado).
    if (pc_mission_active()) return 0;
    return pc_hardmode_active() ? 0 : sConfig.infiniteDay;
}

int pc_settings_get_face_cursor(void) { return sConfig.faceCursor; }
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
int pc_settings_get_fireflies(void) { return sConfig.fireflies; }
float pc_settings_get_free_camera_pad_scale(void) { return sConfig.freeCamPadPct / 100.0f; }
int pc_settings_get_bomb_control(void) { if (pc_speedrun_active()) return 0;
    return sConfig.bombControl;
}
int pc_settings_get_bomb_respawn(void) { if (pc_speedrun_active()) return 0;
    return sConfig.bombRespawnSec;
}

int pc_settings_get_onion_step10(void) { if (pc_speedrun_active()) return 0;
    return sConfig.onionStep10;
}

int pc_settings_get_piki_invincible(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.pikiInvincible; }
int pc_settings_get_all_flowers(void) { if (pc_speedrun_active()) return 0; return pc_hardmode_active() ? 0 : sConfig.allFlowers; }
float pc_settings_get_carry_speed_scale(void) { if (pc_speedrun_active()) return 1.0f;
    if (pc_mission_active()) return pc_mission_mods().carry / 100.0f;
    return pc_hardmode_active() ? 1.0f : sConfig.carrySpeedPct / 100.0f; }
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

const char* pc_settings_get_pikmin_model(int color) {
    return (color >= 0 && color < 3) ? sConfig.pikminModel[color].c_str() : "";
}

int pc_settings_get_hd_model_enabled(int row) {
    return row < 0 || row >= 6 || !(sConfig.hdModelsDisabled & (1 << row));
}

int pc_settings_get_debug_keys(void) { if (pc_speedrun_active()) return 0;
    return sConfig.debugKeys;
}

void pc_settings_record_original_look(int original) {
    // Copia de lo que había antes de pasar al aspecto original; se repone tal cual.
    static PcConfig sBefore;
    static bool sInOriginal = false;
    if (original && !sInOriginal) {
        sBefore = sConfig;
        sConfig.shadows = 0;
        sConfig.ssao = 0;
        sConfig.bloom = 0;
        sConfig.dof = 0;
        sConfig.antialiasing = 0;
        sConfig.perPixelLighting = 0;
        sConfig.anisotropy = 0;
        sConfig.colourGrading = 0;
        sConfig.fog = 1;
        sConfig.hdModelsDisabled = 0x3F; // las seis filas del selector de modelos HD
        sInOriginal = true;
    } else if (!original && sInOriginal) {
        sConfig.shadows = sBefore.shadows;
        sConfig.ssao = sBefore.ssao;
        sConfig.bloom = sBefore.bloom;
        sConfig.dof = sBefore.dof;
        sConfig.antialiasing = sBefore.antialiasing;
        sConfig.perPixelLighting = sBefore.perPixelLighting;
        sConfig.anisotropy = sBefore.anisotropy;
        sConfig.colourGrading = sBefore.colourGrading;
        sConfig.fog = sBefore.fog;
        sConfig.hdModelsDisabled = sBefore.hdModelsDisabled;
        sInOriginal = false;
    }
    applyGraphics(sConfig);
    fprintf(stderr, "[RECORD] graphics: %s\n", sInOriginal ? "original" : "Open Nectar");
    fflush(stderr);
}

#if PIKI_NETPLAY_BUILD
const char* pc_settings_netplay_config_text(void) {
    static std::string text;
    text.clear();
    char cell[96];
#define PC_NETPLAY_TEXT_FIELD(f)                                        \
    snprintf(cell, sizeof(cell), "%s=%d;", #f, (int)sConfig.f);        \
    text += cell;
    PC_NETPLAY_SESSION_FIELDS(PC_NETPLAY_TEXT_FIELD)
#undef PC_NETPLAY_TEXT_FIELD
    text += "vsMap=" + sConfig.vsMap + ";";
    return text.c_str();
}

void pc_settings_netplay_session_begin(const char* adoptConfigText) {
    if (sNetplaySessionLocked) return;
    if (adoptConfigText != nullptr) {
        sNetplayDiskConfig = sConfig;
        const std::string own = pc_settings_netplay_config_text();
        const int applied = netplayApplyConfigText(adoptConfigText);
        const std::string adopted = pc_settings_netplay_config_text();
        sNetplayAdopted = true;
        sPending = sConfig;
        printf("[PC Settings] netplay session: adopted the host's sim settings for this session only "
               "(%d keys, %s); the settings file keeps this player's own values\n",
               applied, own == adopted ? "same as this player's" : "some differ from this player's");
        if (own != adopted) {
            printf("[PC Settings] netplay session: own   %s\n", own.c_str());
            printf("[PC Settings] netplay session: host  %s\n", adopted.c_str());
        }
    }
    // The session ticks at 30 Hz, and each tick advances the game by the
    // frame clamp's period: 60/120 FPS mode would play at half/quarter speed.
    // Both peers force 30 FPS for the session; the settings file keeps the
    // player's own mode (the disk values are restored when saving).
    if (sConfig.fpsMode != 0) {
        if (!sNetplayAdopted) {
            sNetplayDiskConfig = sConfig;
            sNetplayAdopted    = true;
        }
        printf("[PC Settings] netplay session: FPS mode %d -> 30 FPS for the session (the netplay tick is 30 Hz)\n",
               sConfig.fpsMode);
        sConfig.fpsMode  = 0;
        sPending.fpsMode = 0;
    }
    sNetplaySessionConfig = sConfig;
    sNetplaySessionLocked = true;
    printf("[PC Settings] netplay session: sim-relevant settings locked for the session\n");
    fflush(stdout);
}
#endif

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
    case PC_SET_PICKER_PADGUIDE: return "Controller Guide";
    default: return "";
    }
}

const char* pc_settings_group_summary(int group) {
    switch (group) {
    case PC_SET_GROUP_DISPLAY: return "Window, resolution, frame rate";
    case PC_SET_GROUP_GRAPHICS: return "Effects, colour, texture packs, HD models";
    case PC_SET_GROUP_CONTROLS: return "Actions, lock-on, sticks, gyro, bindings";
    case PC_SET_GROUP_CAMERA: return "Free camera, first person, co-op view";
    case PC_SET_GROUP_GAMEPLAY: return "Pikmin behaviour, night, fireflies, HUD";
    case PC_SET_GROUP_CHEATS: return "Day, health, Pikmin limit, whistle";
    case PC_SET_GROUP_DATA: return "Saves, reset settings";
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
    case 0: snprintf(value, n, "%s", sPending.antialiasing ? "On" : "Off"); break;
    case 1: snprintf(value, n, "%s", sPending.fog ? "On  (original)" : "Off"); break;
    case 2: snprintf(value, n, "%s", level(sPending.bloom)); break;
    case 3: snprintf(value, n, "%s", level(sPending.ssao)); break;
    case 4: snprintf(value, n, "%s", level(sPending.dof)); break;
    case 5:
        if (sPending.anisotropy <= 1) snprintf(value, n, "Standard");
        else snprintf(value, n, "Sharp %dx", sPending.anisotropy);
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
        for (int id = 0; id < PC_HD_MODEL_PRESIDENT; id++) // los de Pikmin 2 no son packs
            if (std::filesystem::is_regular_file(pc_hd_model_path((PcHdModelId)id), ec)) installed++;
        snprintf(value, n, "%d / %d files  >", installed, (int)PC_HD_MODEL_PRESIDENT);
        break;
    }
    case 12: snprintf(value, n, "%s", sPending.perPixelLighting ? "Smooth" : "Original"); break;
    case 14:
    case 15:
    case 16: {
        const int color = i == 14 ? Red : i == 15 ? Yellow : Blue;
        const char* name = pc_pikmin_models_name_of(color, sPending.pikminModel[color].c_str());
        snprintf(value, n, "%s", *name ? name : "Game");
        break;
    }
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
    case 41: snprintf(value, n, "%s", sPending.fireflies ? "On" : "Off (original)"); break;
    case 43: snprintf(value, n, "%s", sPending.faceCursor ? "Cursor" : "Captain (default)"); break;
    case 42:
        if (sPending.bombRespawnSec <= 0) snprintf(value, n, "Off (original)");
        else if (sPending.bombRespawnSec < 60) snprintf(value, n, "Every %d s", sPending.bombRespawnSec);
        else snprintf(value, n, "Every %d min", sPending.bombRespawnSec / 60);
        break;
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
    case 8: snprintf(value, n, "%s", sPending.padScheme == PC_PAD_SCHEME_SWITCH ? "Switch" : "GameCube (original)"); break;
    case 9: {
        const char* names[3] = { "Off", "Hold button", "Always" };
        snprintf(value, n, "%s", names[(sPending.rsAimMode >= 0 && sPending.rsAimMode <= 2) ? sPending.rsAimMode : 0]);
        break;
    }
    case 10: snprintf(value, n, "%.2f", sPending.rsAimSens); break;
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
enum RowSrc { SRC_MAIN, SRC_ADV, SRC_GFX, SRC_MODS, SRC_DATA, SRC_KEYS, SRC_PADS, SRC_RECENTER, SRC_ACH, SRC_PADGUIDE, SRC_AIMBTN };
struct GroupRow {
    RowSrc src;
    int idx;
    const char* label;
    const char* help;
};

const GroupRow kDisplayRows[] = {
    { SRC_MAIN, ROW_DISPLAY_MODE, "Display Mode", "Windowed, fullscreen, or a borderless window that fills the screen." },
    { SRC_MAIN, ROW_RESOLUTION, "Resolution", "Size of the window or the fullscreen picture. A: full list. Left/Right: next size." },
    { SRC_MAIN, ROW_ASPECT_RATIO, "Aspect Ratio", "Auto fills the window. The others keep that shape and add black bars." },
    { SRC_MAIN, ROW_RENDER_SCALE, "3D Resolution", "Detail of the 3D world. Original renders at the GameCube's 480 lines, the lightest and the default. Higher looks sharper, lower runs faster." },
    { SRC_MAIN, ROW_REFRESH_RATE, "Refresh Rate", "How often the screen updates in fullscreen. Auto keeps your monitor's setting." },
    { SRC_MAIN, ROW_VSYNC, "Frame Sync (VSync)", "Waits for the screen before showing each frame. Stops tearing (a picture split in two), adds a little input delay." },
    { SRC_MAIN, ROW_FPS_MODE, "FPS Mode", "Frames per second. 30 is the original and the most stable. 60 and 120 are smoother but experimental." },
};

const GroupRow kGraphicsRows[] = {
    { SRC_GFX, 0, "Antialiasing", "Smooths jagged edges on characters and scenery. Small performance cost." },
    { SRC_GFX, 5, "Texture Filtering", "Keeps the ground and walls sharp when seen at an angle. Higher costs a little performance." },
    { SRC_GFX, 12, "Lighting", "Smooth: softer, more detailed light on characters. Original: as on GameCube." },
    { SRC_GFX, 13, "Shadows", "Real-time shadows cast by characters and objects." },
    { SRC_GFX, 1, "Fog", "The game's own distance fog. On is how the original looks." },
    { SRC_GFX, 2, "Bloom", "Soft glow around bright lights and bright areas." },
    { SRC_GFX, 3, "Ambient Occlusion", "Soft shadows in corners and where objects meet the ground." },
    { SRC_GFX, 4, "Depth of Field", "Blurs the far distance so what is close to you stands out." },
    { SRC_GFX, 6, "Colour Adjustments", "Turns on the Gamma, Brightness and Saturation sliders below. Off shows the original colours." },
    { SRC_GFX, 7, "Gamma", "Lightens or darkens the middle shades, leaving the darkest and brightest parts alone. 1.00 is neutral." },
    { SRC_GFX, 8, "Brightness", "Makes the whole picture lighter or darker. 0.00 is neutral." },
    { SRC_GFX, 9, "Saturation", "Colour strength. 1.00 is neutral, 0.00 is black and white." },
    { SRC_GFX, 10, "Texture Packs", "Replacement textures made by the community. Install one from its zip or switch between them. Changes need a restart." },
    { SRC_GFX, 11, "HD Models", "High-detail character models. Install each one from its zip, then turn it On or Off." },
    { SRC_GFX, 14, "Red Pikmin", "Model for the red Pikmin: the game's, or one made with the Model Maker (Load/Pikmin). The leaf, bud and flower stay the game's." },
    { SRC_GFX, 15, "Yellow Pikmin", "Model for the yellow Pikmin: the game's, or one made with the Model Maker (Load/Pikmin). The leaf, bud and flower stay the game's." },
    { SRC_GFX, 16, "Blue Pikmin", "Model for the blue Pikmin: the game's, or one made with the Model Maker (Load/Pikmin). The leaf, bud and flower stay the game's." },
};

const GroupRow kControlsRows[] = {
    { SRC_ADV, 8, "Controller Layout", "What each controller button does. GameCube: the original layout. Switch: as in Pikmin 1 + 2 on Switch (right stick turns the camera, L + right stick swarms, ZL faces forward, ZR zooms). Buttons you reassigned keep your choice." },
    { SRC_ADV, 9, "Right Stick Aim", "Controller only. Move the cursor with the right stick, like a mouse. Hold button: only while you hold the button below (the right stick goes back to the camera when you let go). Always: the right stick always aims; the swarm button still swarms." },
    { SRC_AIMBTN, 0, "Right Stick Aim Button", "Button to hold so the right stick aims the cursor (Hold button mode). A: choose the button." },
    { SRC_ADV, 10, "Right Stick Aim Sensitivity", "How fast the cursor moves with the right stick. 1.00 is the default." },
    { SRC_PADGUIDE, 0, "Controller Guide", "Every controller button and what it does right now, sorted by topic. It follows the layout, your reassigned buttons and the mods you have on." },
    { SRC_KEYS, 0, "Keyboard Bindings", "Two keys or mouse buttons per action. A: main key. Right: second key. Left: back to default. While waiting, Delete clears it. Shift, Ctrl and Alt can be used." },
    { SRC_PADS, 0, "Controller Bindings", "Choose the controller button for each action." },
    { SRC_MODS, 0, "Control Scheme", "Classic: the stick moves the cursor, as on GameCube. Mouse Cursor: aim the cursor with the mouse." },
    { SRC_ADV, 0, "Mouse Sensitivity", "How far the cursor moves when you move the mouse." },
    { SRC_MODS, 3, "Mouse Wheel", "What the mouse wheel does: choose the Pikmin colour to throw, or zoom the camera." },
    { SRC_MODS, 2, "Hold to Pluck", "Hold the pluck button to pull up sprouts one after another." },
    { SRC_MODS, 34, "Whistle Pluck", "Hold the whistle over sprouts to pluck them one after another." },
    { SRC_MODS, 17, "Throw While Moving", "Throw Pikmin while running, instead of Olimar stopping first." },
    { SRC_MODS, 22, "Cancel a Throw", "While holding a Pikmin ready to throw, press B (the cancel button) to put it back in the squad." },
    { SRC_MODS, 33, "Quick Grab", "The next Pikmin jumps straight into Olimar's hand, so throwing stays fast even with the squad behind him." },
    { SRC_MODS, 39, "Pikmin 2 Selection", "D-pad as in Pikmin 2. Left/Right chooses the colour for every throw. Hold A and press Up/Down to pick leaf, bud, flower or a Yellow with a bomb rock. Off: Left/Right only picks the next throw." },
    { SRC_MODS, 24, "Onion: Move 10 at a Time", "In the Onion menu, hold Y while pressing up or down to move 10 Pikmin at a time." },
    { SRC_MODS, 35, "Bomb Control", "Bomb button (B on keyboard, set a controller button in Controls): a Yellow with a bomb rock throws it at the cursor, or drops it at its feet if the cursor is too close. Yellows already thrown go first." },
    { SRC_MODS, 42, "Bomb Rock Respawn", "Used bomb rock piles grow back one rock at a time during the same day, instead of only the next morning." },
    { SRC_MODS, 15, "Lock-On", "Automatic: locks onto the nearest enemy or object as you approach. Manual: press the Lock-On button (set it in Controls)." },
    { SRC_MODS, 16, "Charge", "With a target locked, send the whole squad at it." },
    { SRC_ADV, 1, "Stick Dead Zone", "Ignores tiny stick movements. Raise it if the cursor or Olimar moves on its own (stick drift)." },
    { SRC_ADV, 2, "Stick Invert", "Flips the movement stick: horizontal, vertical or both." },
    { SRC_ADV, 3, "Camera Stick Invert", "Flips the right stick (the GameCube C-Stick): horizontal, vertical or both." },
    { SRC_ADV, 4, "Gyro Aiming", "Aim the cursor by tilting a controller with motion sensors, or your phone. In first person it looks around." },
    { SRC_ADV, 5, "Gyro Sensitivity", "How far the cursor moves when you tilt the controller." },
    { SRC_ADV, 6, "Gyro Invert", "Flips gyro aiming: none, horizontal, vertical or both." },
    { SRC_ADV, 7, "Gyro Calibrate", "Put the controller or phone down, keep it still and press A. Fixes a cursor that drifts on its own." },
    { SRC_RECENTER, 0, "Gyro Recenter Button", "Button that brings the cursor back in front of Olimar. A: choose the button." },
};

const GroupRow kCameraRows[] = {
    { SRC_MODS, 14, "Free Camera", "Turn the camera freely, as in Pikmin 3. Mouse: push the cursor against the edge of its circle. Controller: right stick. L puts the camera back behind Olimar. Swarming moves to the Swarm button (assign it in Controls)." },
    { SRC_MODS, 38, "Free Camera Speed", "How fast the right stick turns the free camera. 100% is the default." },
    { SRC_MODS, 43, "Face Forward Aims At", "With Free Camera, what L (face forward) points the camera at. Captain: behind the captain, and the cursor comes back in front of him. Cursor: towards the cursor, wherever you left it, as in the original game." },
    { SRC_MODS, 18, "First Person", "Look at the world from inside Olimar's helmet. Switch in game with the First Person button (V on keyboard, L3 on a controller)." },
    { SRC_MODS, 6, "Co-op Split Screen", "How the screen divides in two-player co-op." },
    { SRC_MODS, 7, "Co-op Merged Camera", "Joins both halves into one view while the captains are close." },
};

const GroupRow kGameplayRows[] = {
    { SRC_MODS, 25, "Instant Whistle Response", "Whistled Pikmin join the squad at once, without stopping to turn and look first." },
    { SRC_MODS, 1, "Chain Pikmin Actions", "Pikmin that finish a task go on to the next one nearby." },
    { SRC_MODS, 8, "Better Pathfinding", "Smarter Pikmin: they get unstuck on their way, run back to the squad when thrown with nothing to do, and no longer stop to pull nectar grass they walk past (swarm them at it instead)." },
    { SRC_MODS, 9, "Blues Only In Water", "Only blue Pikmin walk into water on their own." },
    { SRC_MODS, 23, "No Tripping", "Pikmin running in the squad never trip and fall behind." },
    { SRC_MODS, 40, "Eternal Night", "Always night, whatever the day length: night lighting, and the moon crosses the day bar instead of the sun." },
    { SRC_MODS, 41, "Fireflies", "Fireflies drift around the field, by day or by night." },
    { SRC_MODS, 10, "Idle Pikmin Counter", "Shows how many Pikmin are standing around with nothing to do." },
    { SRC_MODS, 36, "Hide Olimar's Messages", "Skips the messages Olimar shows while you play (first Pikmin, ship parts, tips). The ending messages still appear." },
};

// Infinite Day (fila 13) vive dentro de Day Length como su última opción.
const GroupRow kCheatsRows[] = {
    { SRC_MODS, 5, "Day Length", "Minutes of daylight per day. 13.5 is the original; Infinite stops the sun, except on the last day." },
    { SRC_MODS, 11, "Olimar Health", "Olimar's toughness, as a share of the original. Infinite takes no damage." },
    { SRC_MODS, 12, "Enemy Health", "Enemy toughness, as a share of the original. Insta Kill drops them in one hit." },
    { SRC_MODS, 4, "Pikmin Limit", "Most Pikmin on the field at once. 100 is the original; more costs performance." },
    { SRC_MODS, 21, "Throw Speed", "How fast Olimar grabs and throws Pikmin. 100% is the original." },
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
      "F6: advance the clock by one in-game hour, capped just before sunset (with Infinite Day it only moves the lighting, except on the last day).\n"
      "F7: grant the Main Engine, top red Onion stock up to 20, and trigger the normal end-of-day sequence.\n"
      "F8 (VS only): append both captains' XYZ coordinates to vs_positions.txt." },
#endif
};

const GroupRow kDataRows[] = {
#ifdef __ANDROID__
    { SRC_DATA, 0, "Export save to ZIP", "Copies your memory card to a ZIP file you choose." },
    { SRC_DATA, 1, "Import save from ZIP", "Replaces your memory card with one from a ZIP file." },
#else
    { SRC_DATA, 0, "Open Save Folder", "Opens the folder with your saves (card0, card1). Copy it to back up your progress or move it to another computer." },
#endif
    { SRC_DATA, 2, "Reset all settings to defaults", "Puts every setting back to its default. Saves are not touched." },
#if PC_ONLINE_ENABLED // online features are off project-wide (pc_online_switch.h)
    { SRC_DATA, 3, "Online co-op: leave / rejoin", "As an online guest: stop playing and watch the host's captain while keeping your place. Choose it again, or press Start, to ask to rejoin; the host lets you back in outside menus and cutscenes." },
    { SRC_DATA, 4, "Online over the Internet", "On: when you create or join an online room, the game asks a free public server (Google or Cloudflare STUN) for this connection's Internet address, so a friend on another network can connect. Only that address is exchanged; the game itself goes directly between the two players. Some networks still block it (often mobile data on both sides). Off: no outside server is contacted, and only players on the same network or a VPN can connect." },
#endif
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

const GroupRow* rawGroupRows(int group, int* count) {
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

// Filas que solo valen con otra encendida: se ocultan en vez de atenuarse,
// para que Controls y Graphics no se llenen de opciones que no aplican.
bool rowHidden(const GroupRow& r) {
    switch (r.src) {
    case SRC_ADV:
        if (r.idx == 0) return sPending.controlMode == PC_CONTROL_CLASSIC;
        if (r.idx >= 5 && r.idx <= 7) return !sPending.gyroEnabled;
        if (r.idx == 10) return sPending.rsAimMode == PC_RS_AIM_OFF;
        return false;
    case SRC_RECENTER: return !sPending.gyroEnabled;
    case SRC_AIMBTN: return sPending.rsAimMode != PC_RS_AIM_HOLD;
    case SRC_GFX: return r.idx >= 7 && r.idx <= 9 && !sPending.colourGrading;
    case SRC_MODS: return (r.idx == 16 && !sPending.lockOn) || (r.idx == 43 && !sPending.freeCamera);
    default: return false;
    }
}
// Fila original de cada fila visible del último groupRows (para las secciones).
int sVisibleRowSource[64];
const GroupRow* groupRows(int group, int* count) {
    int n = 0;
    const GroupRow* raw = rawGroupRows(group, &n);
    if (!raw || group == PC_SET_GROUP_ACHIEVEMENTS) {
        *count = n;
        for (int i = 0; i < n && i < 64; i++) sVisibleRowSource[i] = i;
        return raw;
    }
    static GroupRow visible[64];
    int k = 0;
    for (int i = 0; i < n && k < 64; i++) {
        if (rowHidden(raw[i])) continue;
        sVisibleRowSource[k] = i;
        visible[k++] = raw[i];
    }
    *count = k;
    return visible;
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
    { PC_SET_GROUP_GRAPHICS, 0, "IMAGE" },
    { PC_SET_GROUP_GRAPHICS, 4, "POST-PROCESSING" },
    { PC_SET_GROUP_GRAPHICS, 8, "COLOUR" },
    { PC_SET_GROUP_GRAPHICS, 12, "CONTENT" },
    { PC_SET_GROUP_GRAPHICS, 14, "PIKMIN MODELS" },
    { PC_SET_GROUP_CONTROLS, 0, "BINDINGS" },
    { PC_SET_GROUP_CONTROLS, 2, "SCHEME" },
    { PC_SET_GROUP_CONTROLS, 5, "ACTIONS" },
    { PC_SET_GROUP_CONTROLS, 12, "COMBAT" },
    { PC_SET_GROUP_CONTROLS, 15, "STICKS" },
    { PC_SET_GROUP_CONTROLS, 18, "GYRO" },
    { PC_SET_GROUP_CAMERA, 0, "CAMERA" },
    { PC_SET_GROUP_CAMERA, 3, "CO-OP" },
    { PC_SET_GROUP_GAMEPLAY, 0, "PIKMIN" },
    { PC_SET_GROUP_GAMEPLAY, 5, "WORLD" },
    { PC_SET_GROUP_GAMEPLAY, 7, "HUD" },
    { PC_SET_GROUP_CHEATS, 0, "DAY & HEALTH" },
    { PC_SET_GROUP_CHEATS, 3, "PIKMIN" },
    { PC_SET_GROUP_CHEATS, 9, "OLIMAR" },
    { PC_SET_GROUP_CHEATS, 10, "PROGRESS" },
#if PIKI_DEBUG_KEYS
    { PC_SET_GROUP_CHEATS, 14, "DEBUG" },
#endif
    { PC_SET_GROUP_DATA, 0, "SAVE FILE" },
    { PC_SET_GROUP_DATA, 2, "SETTINGS" },
};

const char* rowSection(int group, int row) {
    // La cabecera va en la primera fila visible de su sección.
    int n = 0;
    groupRows(group, &n);
    if (row < 0 || row >= n) return nullptr;
    const int from = row > 0 ? sVisibleRowSource[row - 1] + 1 : 0;
    const int to   = sVisibleRowSource[row];
    const char* title = nullptr;
    for (const GroupSection& s : kSections)
        if (s.group == group && s.row >= from && s.row <= to) title = s.title;
    return title;
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
            || r->idx == ROW_REFRESH_RATE || r->idx == ROW_VSYNC || r->idx == ROW_FPS_MODE;
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
// Online co-op lives in the netplay session (netplay builds only): weak
// references, so builds without it link and simply never enable the row.
}
bool pc_netplay_online_is_guest(void);
bool pc_netplay_online_spectating(void);
void pc_netplay_online_request_leave(void);
void pc_netplay_online_request_rejoin(void);
namespace {
bool online_guest() { return pc_netplay_online_is_guest != nullptr && pc_netplay_online_is_guest(); }
bool online_spectating() { return pc_netplay_online_spectating != nullptr && pc_netplay_online_spectating(); }

const char* disabledReason(const GroupRow& r) {
    if (hardLocked(r)) return "Locked by Hard mode for this save file.";
    switch (r.src) {
    case SRC_MAIN:
        if (r.idx == ROW_RESOLUTION && sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS)
            return "Borderless always uses the desktop resolution.";
        break;
    case SRC_ADV:
        if ((r.idx == 9 || r.idx == 10) && !pc_window_get_controller()) return "Connect a controller first.";
        if (r.idx == 10 && sPending.rsAimMode == PC_RS_AIM_OFF) return "Turn on Right Stick Aim first.";
        if (r.idx == 0 && sPending.controlMode == PC_CONTROL_CLASSIC)
            return "Only used with the Mouse Cursor control scheme.";
        if ((r.idx == 5 || r.idx == 6) && !sPending.gyroEnabled) return "Turn on Gyro Aiming first.";
        if (r.idx == 7 && !pc_gyro_available()) return "No gyro found. Connect a pad with a gyro.";
        break;
    case SRC_RECENTER:
        if (!sPending.gyroEnabled) return "Turn on Gyro Aiming first.";
        break;
    case SRC_AIMBTN:
        if (!pc_window_get_controller()) return "Connect a controller first.";
        if (sPending.rsAimMode != PC_RS_AIM_HOLD) return "Set Right Stick Aim to Hold button first.";
        break;
    case SRC_GFX:
        if (r.idx >= 7 && r.idx <= 9 && !sPending.colourGrading) return "Turn on Colour Adjustments first.";
        break;
    case SRC_MODS:
        if (r.idx == 0 && pc_window_get_controller())
            return "A controller is connected, so the cursor is aimed with its stick (Classic). Unplug it to use Mouse Cursor.";
        if (r.idx == 16 && !sPending.lockOn) return "Turn on Lock-On first.";
        if (r.idx == 43 && !sPending.freeCamera) return "Turn on Free Camera first.";
        break;
    case SRC_DATA:
        if (r.idx == 3 && !online_guest()) return "Only while you are the guest of an online co-op session.";
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
    sMenuMouseEdge = 0;
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
    if (bound == PC_GP_BIND_NONE) bound = -1;
    else if (bound < 0) bound = pc_window_scheme_default_gamepad_binding(sPending.padScheme, action);
    const char* name = bound >= 0 ? pc_window_get_gamepad_button_name(bound) : nullptr;
    snprintf(out, n, "%s", name ? name : "None");
}

// ── Controller Guide ──
// Una línea por acción, agrupadas por tema. Los botones salen de los bindings
// pendientes (o del esquema si no se tocaron), así que la guía siempre dice lo
// que hará el mando al cerrar el menú.
struct PadGuideLine { std::string label, value; bool header; };
std::vector<PadGuideLine> sPadGuide;

std::string padBtn(int action) {
    int b = pendingPadBinds()[action];
    if (b == PC_GP_BIND_NONE) return "(not assigned)";
    if (b < 0) b = pc_window_scheme_default_gamepad_binding(sPending.padScheme, action);
    const char* name = b >= 0 ? pc_window_get_gamepad_button_name(b) : nullptr;
    return name && b >= 0 ? name : "(not assigned)";
}
bool padBound(int action) {
    int b = pendingPadBinds()[action];
    if (b == PC_GP_BIND_NONE) return false;
    if (b < 0) b = pc_window_scheme_default_gamepad_binding(sPending.padScheme, action);
    return b >= 0;
}

void buildPadGuide() {
    sPadGuide.clear();
    auto H = [](const char* t) { sPadGuide.push_back({ t, "", true }); };
    auto L = [](const std::string& a, const std::string& b) { sPadGuide.push_back({ a, b, false }); };
    const bool sw = sPending.padScheme == PC_PAD_SCHEME_SWITCH;
    // "Right Stick Aim: Always" se queda el stick derecho: ni cámara ni swarm sin el botón.
    const bool aimAlways = sPending.rsAimMode == PC_RS_AIM_ALWAYS;
    const bool rsCamera = (sw || sPending.freeCamera) && !aimAlways;
    const std::string A = padBtn(PC_KEY_ACT_A), B = padBtn(PC_KEY_ACT_B), X = padBtn(PC_KEY_ACT_X);
    const std::string Lt = padBtn(PC_KEY_ACT_L);
    std::string swarm;
    if (!rsCamera && !aimAlways && !sw && !sPending.freeCamera) swarm = "Right Stick";
    else if (padBound(PC_KEY_ACT_SWARM_STICK)) swarm = "Hold " + padBtn(PC_KEY_ACT_SWARM_STICK) + " + Right Stick";
    else swarm = "Assign \"Swarm with right stick\"";

    H(sw ? "Layout: Switch" : "Layout: GameCube (original)");
    H("Moving");
    L("Move the captain and the cursor", "Left Stick");
    if (sPending.rsAimMode == PC_RS_AIM_ALWAYS) L("Aim the cursor", "Right Stick");
    else if (sPending.rsAimMode == PC_RS_AIM_HOLD)
        L("Aim the cursor", padBound(PC_KEY_ACT_AIM_STICK) ? "Hold " + padBtn(PC_KEY_ACT_AIM_STICK) + " + Right Stick"
                                                            : "Assign \"Right Stick Aim Button\"");
    H("Pikmin");
    L("Throw a Pikmin (hold, then release)", A);
    L("Pluck a sprout", sPending.holdToPluck ? A + " (hold to keep going)" : A);
    L("Whistle (hold for a bigger circle)", B);
    if (sPending.throwCancelB) L("Put the Pikmin in hand back", "Hold " + A + ", press " + B);
    L("Swarm the squad", swarm);
    if (padBound(PC_KEY_ACT_SWARM)) L("Swarm toward the cursor", padBtn(PC_KEY_ACT_SWARM));
    L("Dismiss the squad", X);
    L("Lie down", padBound(PC_KEY_ACT_LIEDOWN) ? padBtn(PC_KEY_ACT_LIEDOWN) : padBtn(PC_KEY_ACT_DPAD_DOWN));
    L("Change the colour to throw", sw ? padBtn(PC_KEY_ACT_DPAD_RIGHT)
                                    : padBtn(PC_KEY_ACT_DPAD_LEFT) + " / " + padBtn(PC_KEY_ACT_DPAD_RIGHT));
    if (sPending.p2Selection)
        L("Leaf, bud or flower (Pikmin in hand)", sw ? "Hold " + A + ", press " + X
                                                    : "Hold " + A + ", " + padBtn(PC_KEY_ACT_DPAD_UP) + " / " + padBtn(PC_KEY_ACT_DPAD_DOWN));
    H("Camera");
    L("Turn the camera", rsCamera ? "Right Stick left / right" : "Hold " + Lt + " + Left Stick");
    L("Face forward", "Tap " + Lt);
    L("Camera height", sw ? "Right Stick up / down" : padBtn(PC_KEY_ACT_Z));
    L("Zoom", padBtn(PC_KEY_ACT_R));
    const bool anyMod = sPending.lockOn || sPending.firstPerson || sPending.gyroEnabled || sPending.bombControl;
    if (anyMod) H("Mods you have on");
    if (sPending.lockOn) {
        const bool zlLock = sw && !padBound(PC_KEY_ACT_LOCKON);
        L("Lock-On", zlLock ? Lt + " (also faces forward)" : padBtn(PC_KEY_ACT_LOCKON));
        if (sPending.charge) {
            std::string charge = padBound(PC_KEY_ACT_SWARM) ? padBtn(PC_KEY_ACT_SWARM) : std::string();
            if (padBound(PC_KEY_ACT_SWARM_STICK)) {
                const std::string tap = "Tap " + padBtn(PC_KEY_ACT_SWARM_STICK);
                charge = charge.empty() ? tap : charge + " or " + tap;
            }
            L("Charge at the locked target", charge.empty() ? "Assign \"Swarm to cursor\"" : charge);
        }
    }
    if (sPending.firstPerson) L("First-person view", padBtn(PC_KEY_ACT_FIRSTPERSON));
    if (sPending.gyroEnabled) L("Bring the gyro cursor back", padBtn(PC_KEY_ACT_GYRO_RECENTER));
    if (sPending.bombControl) L("Bomb Control (Yellow with a bomb rock)", padBtn(PC_KEY_ACT_BOMB));
    H("Menus");
    L("Pause", padBtn(PC_KEY_ACT_START));
    L("Accept / Back", A + " / " + B);
}

int padGuideCount() { buildPadGuide(); return (int)sPadGuide.size(); }
const char* padGuideLabel(int i) { return (i >= 0 && i < (int)sPadGuide.size()) ? sPadGuide[i].label.c_str() : ""; }
const char* padGuideValue(int i) { return (i >= 0 && i < (int)sPadGuide.size()) ? sPadGuide[i].value.c_str() : ""; }
bool padGuideIsHeader(int i) { return i >= 0 && i < (int)sPadGuide.size() && sPadGuide[i].header; }
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
    case PC_SET_PICKER_PADGUIDE: return padGuideCount();
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
    return r && ((r->src == SRC_DATA && r->idx != 4) || (r->src == SRC_ADV && r->idx == 7));
}

const char* pc_settings_row_help(int group, int row) {
    if (const GroupRow* r = groupRow(group, row); r && r->src == SRC_ACH) {
        static char help[320];
        const PcAchievementInfo& info = pc_achievement_info(r->idx);
        const char* blocked = pc_achievements_blocked_reason();
        snprintf(help, sizeof(help), "%s\n\n%s%s%s", info.description,
                 pc_achievement_unlocked(r->idx) ? "Unlocked." : "Locked.", blocked ? "\n\n" : "",
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
    case PC_SET_PICKER_GAMEPAD: return "A: press the new button. Left/Right: default or none. Delete: none.";
    case PC_SET_PICKER_PADGUIDE: return "What each controller button does now. Change it in Controller Layout and Controller Bindings.";
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
    case SRC_AIMBTN: sPickerFocusRow = PC_KEY_ACT_AIM_STICK; return PC_SET_PICKER_GAMEPAD;
    case SRC_PADGUIDE: return PC_SET_PICKER_PADGUIDE;
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
    if (group == PC_SET_PICKER_PADGUIDE) return padGuideLabel(row);
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
            if (r->idx < 2) snprintf(out, n, "A: open");
#endif
            else if (r->idx == 2) snprintf(out, n, "A: reset");
            else if (r->idx == 4) snprintf(out, n, "%s", sPending.onlineStun ? "On" : "Off");
            else snprintf(out, n, "%s", online_guest() ? (online_spectating() ? "A: rejoin" : "A: leave") : "-");
            break;
        case SRC_ACH: break;
        case SRC_KEYS:
        case SRC_PADS:
        case SRC_PADGUIDE: snprintf(out, n, "Open  >"); break;
        case SRC_RECENTER: {
            char name[64];
            gamepadBindingName(PC_KEY_ACT_GYRO_RECENTER, name, sizeof(name));
            snprintf(out, n, "%s  >", name);
            break;
        }
        case SRC_AIMBTN: {
            char name[64];
            gamepadBindingName(PC_KEY_ACT_AIM_STICK, name, sizeof(name));
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
    if (group == PC_SET_PICKER_PADGUIDE) snprintf(out, n, "%s", padGuideValue(row));
    if (group == PC_SET_PICKER_HDMODELS) {
        if (sTexturePackPickerActive && sHdModelsSelection == row) snprintf(out, n, "Installing...");
        else snprintf(out, n, "%s", !hdModelInstalled(row) ? "Not installed" : (pc_settings_get_hd_model_enabled(row) ? "On" : "Off"));
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
            if (r->idx == 4) {
                if (ok || left || right) sPending.onlineStun = !sPending.onlineStun;
                return;
            }
            if (!ok) return;
            if (r->idx < 2) saveDataRowAction(r->idx);
            else if (r->idx == 2) resetToDefaults();
            else if (online_guest()) {
                if (online_spectating()) pc_netplay_online_request_rejoin();
                else pc_netplay_online_request_leave();
            }
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
        else if (dir) pendingPadBinds()[row] = pendingPadBinds()[row] == -1 ? PC_GP_BIND_NONE : -1;
    } else if (group == PC_SET_PICKER_TEXPACKS && ok) {
        texturePacksRowAction(row, pc_texpack_list_packs());
    } else if (group == PC_SET_PICKER_HDMODELS && dir) {
        sHdModelsSelection = row;
        hdModelsToggle(row);
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
    pc_pikmin_models_refresh();
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
    e.ok     = menuOkKey();
    e.cancel = menuCancelKey();
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
//   --pikmin-model COLOR ID         modelo de Pikmin de ese color (red, yellow,
//                                   blue) de Load/Pikmin; ID "" = el del juego
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

    // Modelos de Pikmin propios: el elegido de cada color y los que hay.
    out += ",\"pikminModels\":[";
    pc_pikmin_models_refresh();
    const int kOrder[3] = { Red, Yellow, Blue };
    const char* kNames[3] = { "Red Pikmin", "Yellow Pikmin", "Blue Pikmin" };
    for (int k = 0; k < 3; k++) {
        const int color = kOrder[k];
        if (k) out += ',';
        out += "{\"color\":";
        jsonString(out, pc_pikmin_models_colour_key(color));
        out += ",\"name\":";
        jsonString(out, kNames[k]);
        out += ",\"selected\":";
        jsonString(out, sConfig.pikminModel[color].c_str());
        out += ",\"options\":[{\"id\":\"\",\"name\":\"Game\"}";
        for (int m = 0; m < pc_pikmin_models_count(color); m++) {
            out += ",{\"id\":";
            jsonString(out, pc_pikmin_models_id(color, m));
            out += ",\"name\":";
            jsonString(out, pc_pikmin_models_name(color, m));
            out += '}';
        }
        out += "]}";
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
            || !strcmp(argv[i], "--hdmodel-install") || !strcmp(argv[i], "--hdmodel-enable")
            || !strcmp(argv[i], "--pikmin-model")) {
            at = i;
            break;
        }
    }
    if (at < 0) return -1;

    // Solo vídeo, para la lista de resoluciones; sin pantalla sigue valiendo.
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    sHeadless = true;
    loadConfig();
    applyLauncherPackActivation();
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
    } else if (!strcmp(argv[at], "--pikmin-model") && at + 2 < argc) {
        const int color = pc_pikmin_models_colour_from_key(argv[at + 1]);
        pc_pikmin_models_refresh();
        bool known = !argv[at + 2][0];
        for (int m = 0; color >= 0 && m < pc_pikmin_models_count(color); m++)
            known = known || !strcmp(pc_pikmin_models_id(color, m), argv[at + 2]);
        if (color < 0 || !known) {
            message = "That Pikmin model is not installed for that colour.";
            messageIsError = true;
            result = 1;
        } else {
            sConfig.pikminModel[color] = argv[at + 2];
            sPending = sConfig;
            saveConfig();
        }
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
