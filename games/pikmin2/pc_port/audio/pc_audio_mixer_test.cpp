#include "audio/pc_audio.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    int failures = 0;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!pc_audio_init()
        || !pc_audio_load_wave_bank("assets/dataDir/SndData/Banks/pikibank.bx")) {
        std::printf("pc_audio_mixer_test: initialization failed\n");
        return 1;
    }
    pc_audio_reset_metrics();

    if (argc == 3 && std::strcmp(argv[1], "--stress-seconds") == 0) {
        const int seconds = std::clamp(std::atoi(argv[2]), 1, 3600);
        if (!pc_audio_play_sequence(18)
            || !pc_audio_play_stx("assets/dataDir/SndData/opening.stx")) ++failures;
        const Uint64 deadline = SDL_GetTicks64() + static_cast<Uint64>(seconds) * 1000;
        size_t iteration = 0;
        while (SDL_GetTicks64() < deadline) {
            const u16 command = static_cast<u16>(iteration % 40 + 1);
            pc_audio_send_system_se(command, false);
            if ((iteration & 7) == 7) pc_audio_send_system_se(command, true);
            const int voice = pc_audio_play_note(
                18, 4 + static_cast<u32>(iteration & 1), 60, 80, 0,
                0.01f, 0.0f, PC_AUDIO_BUS_SE, 80, 1.0f);
            if (voice >= 0) pc_audio_stop_wave(voice);
            pc_audio_tick();
            SDL_Delay(1);
            ++iteration;
        }
        pc_audio_stop_stream();
        pc_audio_stop_sequence();
        pc_audio_stop_bus(PC_AUDIO_BUS_SE);
        PCAudioMetrics stress {};
        pc_audio_get_metrics(&stress);
        if (stress.sampleRate != 32000 || stress.deviceBufferFrames > 1024
            || stress.callbacks < static_cast<u64>(seconds) * 20
            || stress.mixedFrames == 0 || stress.clips != 0
            || pc_audio_get_active_voice_count() != 0) ++failures;
        pc_audio_shutdown();
        std::printf("pc_audio_mixer_test stress: seconds=%d iterations=%zu failures=%d callbacks=%llu frames=%llu peak=%u steals=%u rejects=%u clips=%u limited=%llu bgm-ticks=%llu se-ticks=%llu event-ticks=%llu\n",
                    seconds, iteration, failures,
                    static_cast<unsigned long long>(stress.callbacks),
                    static_cast<unsigned long long>(stress.mixedFrames),
                    stress.peakActiveVoices, stress.voiceSteals, stress.voiceRejects,
                    stress.clips, static_cast<unsigned long long>(stress.limitedFrames),
                    static_cast<unsigned long long>(stress.bgmTicks),
                    static_cast<unsigned long long>(stress.seTicks),
                    static_cast<unsigned long long>(stress.eventTicks));
        return failures == 0 ? 0 : 1;
    }

    // A long video frame must not discard JAM time. At the default 120 BPM /
    // 48 timebase, 350 ms represents about 33 ticks; the historical 250 ms
    // clamp could never report more than 24 here.
    if (!pc_audio_play_sequence(18)) {
        ++failures;
    } else {
        SDL_Delay(350);
        pc_audio_tick();
        PCAudioMetrics clockMetrics {};
        pc_audio_get_metrics(&clockMetrics);
        if (clockMetrics.bgmTicks < 28 || clockMetrics.seTicks < 28
            || clockMetrics.eventTicks < 28) ++failures;
        pc_audio_stop_sequence();
    }
    pc_audio_reset_metrics();

    std::vector<int> voices;
    for (size_t i = 0; i < 64; ++i) {
        const int handle = pc_audio_play_note(
            18, 4, 60, 100, 0, 0.05f, 0.0f,
            PC_AUDIO_BUS_BGM, 64, 1.0f);
        if (handle < 0) ++failures;
        voices.push_back(handle);
    }
    if (pc_audio_get_active_voice_count() != 64) ++failures;

    const int rejected = pc_audio_play_note(
        18, 4, 60, 100, 0, 0.05f, 0.0f,
        PC_AUDIO_BUS_BGM, 63, 1.0f);
    if (rejected >= 0 || pc_audio_get_active_voice_count() != 64) ++failures;

    const int replacement = pc_audio_play_note(
        18, 4, 60, 100, 0, 0.05f, 0.0f,
        PC_AUDIO_BUS_SE, 96, 1.0f);
    if (replacement < 0 || pc_audio_get_active_voice_count() != 64) ++failures;
    SDL_Delay(40);

    // Stopping every stale handle must not stop the higher-priority voice
    // which reused one of their slots with a new generation.
    for (int handle : voices) pc_audio_stop_wave(handle);
    if (pc_audio_get_active_voice_count() != 1) ++failures;
    pc_audio_stop_wave(replacement);
    if (pc_audio_get_active_voice_count() != 0) ++failures;

    const int streamVoice = pc_audio_play_note(
        18, 5, 60, 100, 0, 0.05f, 0.0f,
        PC_AUDIO_BUS_BGM, 64, 1.0f);
    if (streamVoice < 0
        || !pc_audio_play_stx("assets/dataDir/SndData/opening.stx")) ++failures;
    for (size_t iteration = 0; iteration < 512; ++iteration) {
        const int handle = pc_audio_play_note(
            18, 4 + static_cast<u32>(iteration & 1), 60, 100, 0,
            0.03f, 0.0f, PC_AUDIO_BUS_SE, 88, 1.0f);
        if (handle < 0) {
            ++failures;
            continue;
        }
        pc_audio_update_wave(handle, 0.5f,
                             static_cast<float>(iteration % 21) / 10.0f - 1.0f,
                             0.5f + static_cast<float>(iteration % 8) * 0.125f);
        pc_audio_stop_wave(handle);
        if ((iteration & 31) == 31) SDL_Delay(1);
    }
    SDL_Delay(40);
    if (pc_audio_get_active_voice_count() != 1) ++failures;
    pc_audio_stop_stream();
    pc_audio_stop_wave(streamVoice);
    if (pc_audio_get_active_voice_count() != 0) ++failures;

    const int persistentBgm = pc_audio_play_note(
        18, 4, 60, 100, 0, 0.05f, 0.0f,
        PC_AUDIO_BUS_BGM, 64, 1.0f);
    const int oneShotSe = pc_audio_play_note(
        18, 5, 60, 100, 0, 0.05f, 0.0f,
        PC_AUDIO_BUS_SE, 80, 1.0f);
    if (persistentBgm < 0 || oneShotSe < 0
        || pc_audio_get_active_voice_count() != 2) ++failures;
    pc_audio_stop_bus(PC_AUDIO_BUS_SE);
    if (pc_audio_get_active_voice_count() != 1) ++failures;
    pc_audio_stop_wave(oneShotSe); // stale SE handle must remain harmless.
    if (pc_audio_get_active_voice_count() != 1) ++failures;
    pc_audio_stop_wave(persistentBgm);

    // DMA with a declared transfer but no host pointer is a deterministic
    // starvation case. It must be observable once per affected callback.
    pc_audio_start_dma(0, 128);
    SDL_Delay(40);
    pc_audio_stop_dma();

    PCAudioMetrics metrics {};
    pc_audio_get_metrics(&metrics);
    if (metrics.sampleRate != 32000 || metrics.deviceBufferFrames > 1024
        || metrics.callbacks == 0 || metrics.mixedFrames == 0
        || metrics.peakActiveVoices != 64 || metrics.voiceSteals != 1
        || metrics.voiceRejects != 1 || metrics.dmaUnderruns == 0
        || metrics.clips != 0) ++failures;
    pc_audio_shutdown();
    std::printf("pc_audio_mixer_test: failures=%d callbacks=%llu frames=%llu peak=%u steals=%u rejects=%u dma-underruns=%u clips=%u limited=%llu bgm-ticks=%llu se-ticks=%llu event-ticks=%llu\n",
                failures, static_cast<unsigned long long>(metrics.callbacks),
                static_cast<unsigned long long>(metrics.mixedFrames),
                metrics.peakActiveVoices, metrics.voiceSteals,
                metrics.voiceRejects, metrics.dmaUnderruns, metrics.clips,
                static_cast<unsigned long long>(metrics.limitedFrames),
                static_cast<unsigned long long>(metrics.bgmTicks),
                static_cast<unsigned long long>(metrics.seTicks),
                static_cast<unsigned long long>(metrics.eventTicks));
    return failures == 0 ? 0 : 1;
}
