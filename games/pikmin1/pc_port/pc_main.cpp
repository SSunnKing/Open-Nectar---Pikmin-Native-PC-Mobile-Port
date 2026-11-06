/**
 * @file pc_main.cpp
 * @brief Native Linux entry point for the Pikmin PC port.
 *
 * Replaces the original sysBootup.cpp main() which called
 * gsys->Initialise() and gsys->run(new PlugPikiApp()).
 *
 * This file will grow in later stages to include SDL2 window creation,
 * OpenGL context setup, and input polling.
 */
#include <cstdio>
#include "netplay/pc_online_switch.h"
#include <cstdlib>
#include <cstring>
#if PIKI_USE_JAUDIO
int pc_jaudio_integration_test();
#endif

// Game headers
#include "system.h"
#include "App.h"
#include "sysNew.h"

/**
 * @brief Entry point for the Linux PC port.
 *
 * Currently this initializes the game system with stubs and enters
 * the main loop. Since all Dolphin SDK calls are stubbed, this will
 * "run" but produce no visible output yet.
 */
#include <SDL.h>

#ifdef _WIN32
// Laptops with switchable graphics start a process on the integrated GPU unless
// the executable asks otherwise. Both vendors read that request the same way:
// the driver DLL looks up an exported symbol in the process image at load time,
// before any context exists, so this cannot be done from code that runs later.
//
// These have to live in the object that is linked straight into the executable
// -- pc_main.cpp is, see PC_PORT_SOURCES -- because an export from a static
// library reaches the .exe export table only if something already pulled the
// object in. Nothing references either variable, so nothing would.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement                  = 1;
__declspec(dllexport) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include "pc_crash.h"
#include "pc_window.h"
#include "pc_discord.h"
#include "pc_gpu_preference.h"
#include "gl/pc_gfx.h"
#include "gl/pc_texpack.h"
#ifdef __ANDROID__
#include "android/pc_android.h"
#endif
#include "settings/pc_settings.h"
#include "mods/pc_hd_model_convert.h"
#include "mods/pc_vs_map.h"
#include "mods/pc_mission.h"
#include "mods/pc_campaign.h"
#include "pc_vs.h"
#include "settings/pc_settings_p2d.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_input_log.h"
#include "netplay/pc_state_hash.h"
#include "netplay/pc_coop_switch.h"
#if PIKI_SCENARIOS
#include "scenarios/pc_scenario.h"
#endif

// Netplay session switches: strong-defined by pc_netplay_session.cpp in
// netplay builds only, null otherwise.
void pc_netplay_session_notify_argv(int argc, char** argv);
void pc_netplay_launch_preinit(int argc, char** argv);
void pc_netplay_launch_post_settings(void);

int main(int argc, char* argv[])
{
    // Disable stdout buffering so we see logs immediately before any crash
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef __ANDROID__
    // Logcat, carpeta del juego y ruta de guardado: antes de que nada abra un
    // fichero o escriba un mensaje. Sin carpeta no hay assets, y sin assets el
    // juego no arranca: mejor decirlo que caer más adelante sin explicación.
    if (!pc_android_init()) {
        printf("[PC Port Fatal Error] Android storage unavailable\n");
        return 1;
    }
#endif
    // Informe de crash (pikmin_crash.log) y log sin terminal (pikmin.log), en
    // la carpeta del juego: en Android pc_android_init ya entró en ella.
    pc_install_crash_handler();

    // SDL_MAIN_HANDLED is defined for this build, which means the application
    // owns main() and SDL2main is not linked. The other half of that contract
    // is telling SDL so before the first SDL_Init. It is close to a no-op on
    // Linux, which is why its absence went unnoticed there, but Windows needs
    // it to set up the instance handle and command line.
    SDL_SetMainReady();

    // El launcher lee y cambia los ajustes del F1 a través del juego, sin
    // abrir ventana ni arrancar nada más.
    {
        const int settingsResult = pc_settings_cli(argc, argv);
        if (settingsResult >= 0) return settingsResult;
    }

#if PIKI_SCENARIOS
    // --scenario <name>: argument and data checks, and the determinism, settings
    // and input overrides, before anything reads them. No window exists yet.
    {
        const int scenarioResult = pc_scenario_parse_argv(argc, argv);
        if (scenarioResult >= 0) return scenarioResult;
    }
#endif

    // --netplay-host-ice / --netplay-join-ice: the run folder, the private
    // save folder and the joiner's session bundle, before anything reads them.
#if PC_ONLINE_ENABLED // online features are off project-wide (pc_online_switch.h)
    if (pc_netplay_launch_preinit != nullptr) pc_netplay_launch_preinit(argc, argv);
#endif

    // Netplay determinism switches (--netplay-deterministic and the
    // PIKMIN_NETPLAY_* variables), before the game starts. Inert unless set.
    pc_netplay_det_init(argc, argv);

    // Before SDL_Init, and before anything can touch GL: on Linux the vendor
    // is selected by libglvnd the first time it is asked, and by the time a
    // context exists the choice has already been made. No-op elsewhere.
    pc_gpu_preference_apply();

#if PIKI_USE_JAUDIO
    if (argc == 2 && std::strcmp(argv[1], "--audio-self-test") == 0)
        return pc_jaudio_integration_test();
#endif
    // Netplay harness: --input-record/--input-replay and --state-hash-log.
    // No-ops unless those switches (or their env vars) are set.
    pc_input_log_notify_argv(argc, argv);
    pc_state_hash_notify_argv(argc, argv);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-texture-names") == 0)
            pc_gfx_set_dump_texture_names(1);
        // PLAN_TEXTURAS_HD: hasta que exista el ajuste del menú (fase 2), la
        // línea de comandos es la única puerta al pack.
        if (std::strcmp(argv[i], "--texture-pack") == 0)
            pc_texpack_request_enable();
        // Editor de mapas: "Probar" abre el juego directo en una partida VS.
        if (std::strcmp(argv[i], "--vs-map") == 0 && i + 1 < argc) {
            pc_vs_map_select_file(argv[++i]);
            pc_vs_direct_request();
        }
        // Editor de mapas: "Probar" en solo o coop (--mission-players 2).
        if (std::strcmp(argv[i], "--mission-map") == 0 && i + 1 < argc) {
            const char* path = argv[++i];
            int players      = 1;
            for (int k = 1; k + 1 < argc; k++) {
                if (std::strcmp(argv[k], "--mission-players") == 0) players = std::atoi(argv[k + 1]);
            }
            pc_mission_select_file(path, players);
            pc_mission_direct_request();
        }
        // Editor de mapas: "Probar" una campaña desde una zona.
        if (std::strcmp(argv[i], "--campaign") == 0 && i + 1 < argc) {
            const char* dir = argv[++i];
            std::string zone, state;
            for (int k = 1; k + 1 < argc; k++) {
                if (std::strcmp(argv[k], "--zone") == 0) zone = argv[k + 1];
                if (std::strcmp(argv[k], "--campaign-state") == 0) state = argv[k + 1];
            }
            pc_campaign_direct_request(dir, zone, state);
        }
        // Straight into a new two-captain story run (--coop-captains=<p1>,<p2>
        // picks the captains).
        if (std::strcmp(argv[i], "--coop-direct") == 0) {
            PcCoopSwitch sw = pc_coop_switch_parse(argc, argv);
            sw.coop         = true;
            pc_coop_switch_apply(sw);
            pc_coop_direct_request();
        }
    }
    // --netplay-host / --netplay-join. A session also takes the co-op direct
    // boot. No-op unless a netplay switch is set.
#if PC_ONLINE_ENABLED
    if (pc_netplay_session_notify_argv != nullptr) pc_netplay_session_notify_argv(argc, argv);
#endif
    (void)argc;
    (void)argv;

    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Pikmin - Native Linux PC Port          ║\n");
#if PIKI_USE_GLES
    printf("║   OpenGL ES 3.0 Rendering Backend        ║\n");
#else
    printf("║   OpenGL Rendering Backend               ║\n");
#endif
    printf("╚══════════════════════════════════════════╝\n\n");
    fflush(stdout);

    // Initialize SDL2 Window and OpenGL Context FIRST
    printf("[PC Port] Initializing SDL2 window and GL context...\n");
    fflush(stdout);
    if (!pc_window_init("Open Nectar", 1280, 720)) {
        printf("[PC Port Fatal Error] Could not initialize window/OpenGL!\n");
        fflush(stdout);
#if PIKI_SCENARIOS
        // A missing GL context must never read as a failed scenario.
        if (pc_scenario_active()) {
            pc_scenario_on_no_gl();
            return PC_SCENARIO_EXIT_NO_GL;
        }
#endif
        return 1;
    }
    pc_discord_init("Pikmin 1");

    printf("[PC Port] Loading persisted settings...\n");
    fflush(stdout);
    pc_settings_init();
    // A netplay launch locks the sim settings here; the joiner adopts the host's.
    if (pc_netplay_launch_post_settings != nullptr) pc_netplay_launch_post_settings();
    // Con Pikmin 2 instalado, su Louie sirve de capitán sin bajar ningún pack.
    pc_hd_models_import_pikmin2();

    printf("[PC Port] Initializing game system...\n");
    fflush(stdout);
    gsys->Initialise();
    pc_settings_p2d_init();

    printf("[PC Port] Creating node manager...\n");
    nodeMgr = new NodeMgr();

    printf("[PC Port] Starting game application...\n");
#if PIKI_USE_GLES
    printf("[PC Port] Native GX/OpenGL ES, SDL input and persistent save backends active.\n");
#else
    printf("[PC Port] Native GX/OpenGL, SDL input and persistent save backends active.\n");
#endif
    printf("[PC Port] Audio and movie playback are still under development.\n\n");

    gsys->run(new PlugPikiApp());

    printf("[PC Port] Game exited normally.\n");
#if PIKI_SCENARIOS
    if (pc_scenario_active()) return pc_scenario_exit_code();
#endif
    return 0;
}
