// Runs through the game's actual JAudio entry points, without a window/GPU.
#include "jaudio/verysimple.h"
#include "jaudio/aictrl.h"
#include "jaudio/interface.h"
#include "jaudio/file_seq.h"
#include "jaudio/audiostruct.h"
#include "jaudio/jammain_2.h"
#include "jaudio/piki_player.h"
#include "jaudio/piki_bgm.h"
#include "jaudio/pikiinter.h"
#include "jaudio/piki_scene.h"
#include "port/jaudio_host.h"
#include "port/audio_sink.h"
#include "audio/pc_aram.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

namespace {
u64 frames = 0, nonzero = 0;
int peak = 0;
void observe(s16* samples, s32 count)
{
    frames += count;
    for (s32 i = 0; i < count * 2; ++i) {
        nonzero += samples[i] != 0;
        peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    }
}
void pump(u32 milliseconds)
{
    const auto start = SDL_GetTicks();
    do { Jac_Gsync(); PikiJAudioTick(); SDL_Delay(2); } while (SDL_GetTicks() - start < milliseconds);
}
}
int pc_jaudio_integration_test()
{
    // Public source-only builds cannot exercise proprietary audio content.
    // Keep the test enabled for developer checkouts with extracted assets.
    if (!std::filesystem::is_directory("assets/dataDir/SndData")) {
        std::puts("[jaudio-test] SKIP: extracted assets/dataDir/SndData is required");
        return 77;
    }
    SDL_setenv("SDL_AUDIODRIVER", std::getenv("PIKMIN_AUDIO_TEST_NO_DEVICE") ? "unavailable-test-driver" : "dummy", 1);
    // Native pointer graphs are larger than the console's packed bank image.
    constexpr u32 heapSize = 0x80000;
    void* heap = std::malloc(heapSize);
    if (!heap) return 1;
    Jac_Start(heap, heapSize, PC_ARAM_SIZE, "/dataDir/SndData/");
    Jac_RegisterDacCallback(observe);
    Jac_OutputMode(1);
    Jac_SetBGMVolume(8);
    Jac_SetSEVolume(8);
    int failures = 0;
    for (u32 scene : {u32(SCENE_Title), u32(SCENE_FileSelect), u32(SCENE_WorldMap), u32(SCENE_Course)}) {
        nonzero = frames = 0; peak = 0;
        Jac_SceneSetup(scene, 0);
        pump(750);
        std::printf("[jaudio-test] scene=%u frames=%llu nonzero=%llu peak=%d\n", scene,
                    static_cast<unsigned long long>(frames), static_cast<unsigned long long>(nonzero), peak);
        if (!frames || !nonzero) ++failures;
    }
    // The first stage has no boss layer, but proximity can request a boss
    // crossfade on a return visit. It must preserve the normal music instead
    // of resolving the uninitialised boss handle (-1) or fading into silence.
    Jac_EnterBossMode();
    pump(2500);
    seqp_* practiceMusic = Jaf_HandleToSeq(3);
    if (practiceMusic->isMuted || !practiceMusic->outerParams
        || practiceMusic->outerParams->volume <= 0.0f) ++failures;
    Jac_ExitBossMode();
    pump(150);
    std::printf("[jaudio-test] practice boss request preserved normal music\n");

    // Exercise preloading through SceneExit, boss layers and every stage bank.
    for (u32 stage = 1; stage < 5; ++stage) {
        Jac_SceneSetup(SCENE_Course, stage);
        Jac_EnterBossMode(); pump(150);
        Jac_ExitBossMode(); pump(150);
        nonzero = frames = 0;
        pump(300);
        if (!frames || !nonzero) ++failures;
        std::printf("[jaudio-test] stage=%u nonzero=%llu\n", stage, (unsigned long long)nonzero);
    }
    Jac_SceneExit(SCENE_WorldMap, 0);
    Jac_SceneSetup(SCENE_WorldMap, 0);
    pump(300);
    if (Jac_GetCurrentScene() != SCENE_WorldMap) ++failures;

    Jac_SetBGMVolume(0); Jac_SetSEVolume(0); pump(700);
    nonzero = frames = 0; pump(250);
    std::printf("[jaudio-test] muted nonzero=%llu\n", (unsigned long long)nonzero);
    if (!frames || nonzero) ++failures;

    Jac_SetSEVolume(8); pump(100);
    nonzero = 0;
    Jac_PlayOrimaSe(JACORIMA_Gather); pump(500);
    std::printf("[jaudio-test] whistle nonzero=%llu\n", (unsigned long long)nonzero);
    if (!nonzero) ++failures;
    Jac_StopOrimaSe(JACORIMA_Gather);

    Jac_SetBGMVolume(8); pump(100);
    Jac_StopBgm(0); Jac_StopBgm(1); pump(300);
    nonzero = frames = 0;
    Jac_PrepareDemoSound(8); Jac_StartDemoSound(8); pump(1600);
    std::printf("[jaudio-test] opening.stx frames=%llu nonzero=%llu\n",
        (unsigned long long)frames, (unsigned long long)nonzero);
    if (!frames || !nonzero) ++failures;
    Jac_StopDemoSound(8); pump(300);
    StopAudioThread();
    std::free(heap);
    pc_aram_shutdown();
    std::printf("[jaudio-test] failures=%d\n", failures);
    return failures ? 1 : 0;
}
