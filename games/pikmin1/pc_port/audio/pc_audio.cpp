#include <mutex>
#include "pc_audio.h"
#include "audio/pc_envelope.h"
#include "audio/pc_instrument_bank.h"
#include "audio/pc_jam.h"
#include "audio/pc_wave_bank.h"
#include "audio/pc_sequence_archive.h"
#include <SDL2/SDL.h>
#include <atomic>
#include <cstdio>
#include <set>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <limits>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

static SDL_AudioDeviceID sAudioDevice = 0;
static SDL_AudioSpec sAudioSpec;
static AIDCallback sAIDMACallback = nullptr;

static u32 sDMABaseAddr = 0;
static u32 sDMALength = 0;
static std::atomic<u32> sDMABytesLeft { 0 };
static std::atomic<bool> sDMAActive { false };

struct PCMVoice {
    std::vector<s16> samples;
    size_t cursor = 0;
    bool active = false;
};

// The GameCube can play streamed audio and DSP/JAudio output concurrently.
// Keep them as independent voices instead of using SDL's queue API, where
// SDL_ClearQueuedAudio() previously made every new sound silence the others.
static PCMVoice sStreamVoice;
static std::vector<s16> sDMAQueue;
static size_t sDMAReadCursor = 0;
static u8 sStreamVolume = 255;
// Fundido de salida del stream. Jac_DemoFade solo funde la SECUENCIA
// (pc_audio_fade_sequence); el stream no tenia ningun fundido, asi que
// Jac_FinishDemo lo cortaba en seco al acabar una cinematica. La ganancia baja
// por muestra en el mezclador y la voz se apaga al llegar a cero.
static float sStreamFadeGain = 1.0f;
static float sStreamFadeStep = 0.0f;
static u8 sDMAVolume = 255;

struct SampleVoice {
    std::shared_ptr<const std::vector<s16>> samples;
    double cursor = 0.0;
    double step = 1.0;
    double baseStep = 1.0;
    size_t loopSample = 0;
    size_t endSample = 0;
    float left = 1.0f;
    float right = 1.0f;
    float baseVolume = 1.0f;
    float basePan = 0.0f;
    float trackVolume = 1.0f;
    float trackPitch = 1.0f;
    float releaseGain = 1.0f;
    float releaseStep = 0.0f;
    u32 releaseFrames = 0;
    u16 defaultReleaseParam = 0;
    // Stable view into the immutable instrument bank; avoids allocating and
    // copying nested envelope vectors for every JAM NoteON.
    const std::vector<PCInstrumentOscillator>* oscillators = nullptr;
    // Keeps a sequencer-built envelope alive for as long as the voice uses it.
    // Bank instruments leave this empty: the bank outlives every voice.
    std::shared_ptr<const std::vector<PCInstrumentOscillator>> oscillatorsOwned;
    PCEnvelopeState envelopeStates[2];
    float envelopeVolume = 1.0f;
    float envelopePitch = 1.0f;
    float envelopePan = 0.0f;
    float baseFxMix = 0.0f;
    float baseDolby = 0.0f;
    float envelopeFxMix = 0.0f;
    float envelopeDolby = 0.0f;
    float trackFxMix = 0.0f;
    float trackDolby = 0.0f;
    float filterAlpha = 1.0f;
    float filterState = 0.0f;
    u8 envelopeCountdown = 0;
    u64 age = 0;
    u16 generation = 0;
    u8 priority = 0;
    PCAudioBus bus = PC_AUDIO_BUS_SE;
    u8 bgmTrack = 0xFF;
    u8 bgmLayer = 0xFF;
    u8 eventSource = 0xFF;
    u8 jamOwner = 0;
    u8 jamSource = 0xFF;
    // Loudest contribution since the last report, so a noise complaint can be
    // traced to the instrument making it instead of guessed at.
    int peak = 0;
    u8 reportBank = 0;
    u8 reportProgram = 0;
    u8 reportKey = 0;
    bool looping = false;
    bool active = false;
};

static constexpr size_t kSampleVoiceCount = 64;
static constexpr u8 kEnvelopeControlPeriod = 32;
static SampleVoice sSampleVoices[kSampleVoiceCount];
static u16 sVoiceGenerations[kSampleVoiceCount] = {};
static float sBusVolumes[PC_AUDIO_BUS_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f };

// Loudness of each path as actually mixed, not as configured. Gameplay music is
// sequenced (BGM bus) and cinematic music is streamed (STREAM bus), and the two
// do not carry the same chain of gains: the sequenced path is scaled by the BGM
// bus *and* by sBgmTrackGain, while the streamed path is scaled only by the
// STREAM bus. Comparing the multipliers alone would not say how much of the
// difference is audible, so this records the peak sample each path contributes.
// Off unless PIKMIN_AUDIO_STATS=1.
static std::atomic<int> sBusPeak[PC_AUDIO_BUS_COUNT] = {};

// A requested sound can go missing in four different places, and they need
// different fixes: the instrument is not in the bank, the bank points at a wave
// system we do not have, the wave id is absent from that system, or every voice
// is busy with something louder. Counting them separately turns "some sounds do
// not play" into a specific defect.
static std::atomic<int> sMissInstrument{0};
static std::atomic<int> sMissWaveSystem{0};
static std::atomic<int> sMissWave{0};
// Of the waves missing from the requested scene, how many exist in some other
// scene of the same system? If most do, the scene argument is the problem and a
// fallback recovers them; if none do, the sample is genuinely absent and no
// lookup change will help. Also keeps a few distinct misses for identification.
static std::atomic<int> sMissWaveElsewhere{0};
struct WaveMiss { int system; int id; int scene; bool elsewhere; int otherSystem; bool percussion; int program; int count; };
static WaveMiss sWaveMisses[24] = {};
static std::mutex sWaveMissMutex;
static void note_wave_miss(int system, int id, int scene, bool elsewhere, int otherSystem,
                           bool percussion, int program) {
    std::lock_guard<std::mutex> lock(sWaveMissMutex);
    for (auto& miss : sWaveMisses) {
        if (miss.count && miss.system == system && miss.id == id && miss.scene == scene) {
            ++miss.count;
            return;
        }
        if (!miss.count) {
            miss = { system, id, scene, elsewhere, otherSystem, percussion, program, 1 };
            return;
        }
    }
}
static bool pc_audio_stats_enabled() {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_AUDIO_STATS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}
static inline void note_bus_peak(int bus, float sample) {
    const int magnitude = static_cast<int>(std::fabs(sample));
    int previous = sBusPeak[bus].load(std::memory_order_relaxed);
    while (magnitude > previous
           && !sBusPeak[bus].compare_exchange_weak(previous, magnitude,
                                                   std::memory_order_relaxed)) {
    }
}
static bool sStereoOutput = true;
static float sBgmTrackGain = 1.0f;
static float sBgmTrackTarget = 1.0f;
static float sBgmTrackStep = 0.0f;
static u32 sBgmTrackFadeTicks = 0;
static float sBossTrackGain = 0.0f;
static float sBossTrackTarget = 0.0f;
static float sBossTrackStep = 0.0f;
static u32 sBossTrackFadeTicks = 0;
static std::array<float, 16> sBgmLayerGain {};
static std::array<float, 16> sBgmLayerTarget {};
static std::array<float, 16> sBgmLayerStep {};
static u32 sBgmLayerFadeTicks = 0;
static double sBgmMixAccumulator = 0.0;
static u64 sBgmMixLastCounter = 0;
static u64 sVoiceAge = 0;
static std::atomic<u32> sClipCount { 0 };
static std::atomic<u64> sCallbackCount { 0 };
static std::atomic<u64> sMixedFrameCount { 0 };
static std::atomic<u32> sPeakActiveVoices { 0 };
static std::atomic<u32> sVoiceSteals { 0 };
static std::atomic<u32> sVoiceRejects { 0 };
static std::atomic<u32> sDMAUnderruns { 0 };
static std::atomic<u64> sLimitedFrames { 0 };
static std::atomic<u64> sBgmTicks { 0 };
static std::atomic<u64> sBossTicks { 0 };
static std::atomic<u64> sSETicks { 0 };
static std::atomic<u64> sEventTicks { 0 };
static constexpr size_t kFxDelayFrames = 4096;
static constexpr size_t kDolbyDelayFrames = 512;
static std::array<float, kFxDelayFrames> sFxDelayLeft {};
static std::array<float, kFxDelayFrames> sFxDelayRight {};
static std::array<float, kDolbyDelayFrames> sDolbyDelay {};
static size_t sFxDelayCursor = 0;
static size_t sDolbyDelayCursor = 0;
static float sLimiterGain = 1.0f;
static PCWaveBank sWaveBank;
static PCInstrumentBank sInstrumentBank;
static PCSequenceArchive sSequenceArchive;
static PCJamPlayer sJamPlayer;
static PCJamPlayer sBossJamPlayer;
static PCJamPlayer sSEJamPlayer;
static PCJamPlayer sEventJamPlayer;
static constexpr bool kEnablePositionalEventJam = true;
// sysevent.jam: the sixteen per-event tracks the game drives through
// Jac_PlayEventAction.  See restart_event_jam.
static constexpr u32 kEventSequence = 1;
static int sJamVoices[kPCJamTrackCount][8] = {};
static int sBossJamVoices[kPCJamTrackCount][8] = {};
static int sSEJamVoices[kPCJamTrackCount][8] = {};
static int sEventJamVoices[kPCJamTrackCount][8] = {};
static u32 sJamSequence = 0;
static u32 sBossJamSequence = 0;
static double sJamTickAccumulator = 0.0;
static u64 sJamLastCounter = 0;
static double sBossJamTickAccumulator = 0.0;
static u64 sBossJamLastCounter = 0;
static double sSEJamTickAccumulator = 0.0;
static u64 sSEJamLastCounter = 0;
static double sEventJamTickAccumulator = 0.0;
static u64 sEventJamLastCounter = 0;
static u32 sJamUnresolvedNotes = 0;
static std::vector<PCJamEvent> sJamEvents;
static std::vector<PCJamEvent> sBossJamEvents;
static std::vector<PCJamEvent> sSEJamEvents;
static std::vector<PCJamEvent> sEventJamEvents;
static std::array<float, 16> sEventVolumes {};
static std::array<float, 16> sEventPans {};
static std::array<bool, 16> sSETrackPaused {};
static bool sEventsPaused = false;
static std::unordered_map<u64, std::shared_ptr<const std::vector<s16>>> sDecodedWaveCache;
extern "C" u8 HEAD_pikiseq[];
static constexpr size_t kPikiSequenceHeaderSize = 0x20 + 22 * 0x20;

static u16 read_be16(const u8* data) {
    return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
}

static u32 read_be32(const u8* data) {
    return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16)
         | (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

static s16 clamp_s16(int value) {
    if (value < -32768 || value > 32767) {
        sClipCount.fetch_add(1, std::memory_order_relaxed);
    }
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}

static bool convert_pcm(const s16* samples, size_t frameCount, int sourceRate,
                        std::vector<s16>& convertedSamples) {
    if (!sAudioDevice || !samples || !frameCount || sourceRate <= 0) {
        return false;
    }

    SDL_AudioStream* converter = SDL_NewAudioStream(
        AUDIO_S16SYS, 2, sourceRate, sAudioSpec.format, sAudioSpec.channels, sAudioSpec.freq);
    if (!converter) {
        printf("[PC Port Error] SDL_NewAudioStream failed: %s\n", SDL_GetError());
        return false;
    }

    const size_t byteCount = frameCount * 2 * sizeof(s16);
    const bool inputFits = byteCount <= static_cast<size_t>(std::numeric_limits<int>::max());
    bool ok = inputFits && SDL_AudioStreamPut(converter, samples, static_cast<int>(byteCount)) == 0
           && SDL_AudioStreamFlush(converter) == 0;
    const int available = ok ? SDL_AudioStreamAvailable(converter) : -1;
    std::vector<u8> converted(available > 0 ? static_cast<size_t>(available) : 0);
    if (available > 0) {
        ok = SDL_AudioStreamGet(converter, converted.data(), available) == available;
    }
    SDL_FreeAudioStream(converter);
    if (!ok || converted.empty()) {
        printf("[PC Port Error] SDL audio conversion failed: %s\n", SDL_GetError());
        return false;
    }

    convertedSamples.resize(converted.size() / sizeof(s16));
    std::memcpy(convertedSamples.data(), converted.data(),
                convertedSamples.size() * sizeof(s16));
    return true;
}

static void audio_callback(void*, Uint8* output, int byteCount) {
    std::memset(output, 0, static_cast<size_t>(byteCount));
    if (sAudioSpec.format != AUDIO_S16SYS || sAudioSpec.channels != 2) return;

    s16* dst = reinterpret_cast<s16*>(output);
    const size_t frameCount = static_cast<size_t>(byteCount) / (2 * sizeof(s16));
    sCallbackCount.fetch_add(1, std::memory_order_relaxed);
    sMixedFrameCount.fetch_add(frameCount, std::memory_order_relaxed);
    // The callback owns the audio-device lock, so the active set cannot change
    // underneath it. Compacting it once avoids scanning all 64 slots for every
    // single output sample.
    SampleVoice* activeVoices[kSampleVoiceCount];
    size_t activeVoiceCount = 0;
    for (SampleVoice& voice : sSampleVoices) {
        if (voice.active) activeVoices[activeVoiceCount++] = &voice;
    }
    u32 previousPeak = sPeakActiveVoices.load(std::memory_order_relaxed);
    while (activeVoiceCount > previousPeak
           && !sPeakActiveVoices.compare_exchange_weak(
               previousPeak, static_cast<u32>(activeVoiceCount),
               std::memory_order_relaxed)) {}
    if (sDMAActive.load(std::memory_order_relaxed)
        && sDMAReadCursor + 1 >= sDMAQueue.size()) {
        sDMAUnderruns.fetch_add(1, std::memory_order_relaxed);
    }
    const bool statsOn = pc_audio_stats_enabled();
    for (size_t frame = 0; frame < frameCount; ++frame) {
        int mixedLeft = 0;
        int mixedRight = 0;
        float fxSendLeft = 0.0f;
        float fxSendRight = 0.0f;
        float dolbySend = 0.0f;
        if (sStreamVoice.active && sStreamVoice.cursor + 1 < sStreamVoice.samples.size()) {
            const float gain = sStreamVolume / 255.0f * sStreamFadeGain
                             * sBusVolumes[PC_AUDIO_BUS_STREAM];
            const float streamLeft = sStreamVoice.samples[sStreamVoice.cursor++] * gain;
            const float streamRight = sStreamVoice.samples[sStreamVoice.cursor++] * gain;
            if (statsOn) note_bus_peak(PC_AUDIO_BUS_STREAM, streamLeft);
            mixedLeft += static_cast<int>(streamLeft);
            mixedRight += static_cast<int>(streamRight);
            if (sStreamFadeStep > 0.0f) {
                sStreamFadeGain -= sStreamFadeStep;
                if (sStreamFadeGain <= 0.0f) {
                    sStreamFadeGain = 0.0f;
                    sStreamFadeStep = 0.0f;
                    sStreamVoice.active = false;
                    sStreamVoice.samples.clear();
                    sStreamVoice.cursor = 0;
                }
            }
            if (sStreamVoice.cursor >= sStreamVoice.samples.size()) sStreamVoice.active = false;
        }
        if (sDMAActive && sDMAReadCursor + 1 < sDMAQueue.size()) {
            const float gain = sDMAVolume / 255.0f * sBusVolumes[PC_AUDIO_BUS_DMA];
            mixedLeft += static_cast<int>(sDMAQueue[sDMAReadCursor++] * gain);
            mixedRight += static_cast<int>(sDMAQueue[sDMAReadCursor++] * gain);
        }
        for (size_t voiceIndex = 0; voiceIndex < activeVoiceCount; ++voiceIndex) {
            SampleVoice& voice = *activeVoices[voiceIndex];
            if (!voice.active || !voice.samples || voice.samples->empty()) continue;
            if ((voice.jamOwner == 1 && voice.jamSource < sSETrackPaused.size()
                 && sSETrackPaused[voice.jamSource])
                || (voice.jamOwner == 2 && sEventsPaused)) continue;
            size_t sampleIndex = static_cast<size_t>(voice.cursor);
            const size_t playbackEnd = voice.endSample
                ? std::min(voice.endSample, voice.samples->size()) : voice.samples->size();
            if (sampleIndex >= playbackEnd) {
                if (!voice.looping || voice.loopSample >= playbackEnd) {
                    voice.active = false;
                    continue;
                }
                voice.cursor = static_cast<double>(voice.loopSample);
                sampleIndex = voice.loopSample;
            }
            // El punto de bucle esta acotado contra el tamano de la muestra, asi
            // que puede valer exactamente ese tamano: usarlo como vecino de
            // interpolacion lee un elemento pasado el final del vector. Es una
            // muestra de basura por vuelta de bucle, justo en las voces
            // ambientales que no paran nunca.
            size_t nextIndex = sampleIndex + 1 < playbackEnd
                ? sampleIndex + 1
                : (voice.looping ? voice.loopSample : sampleIndex);
            if (nextIndex >= voice.samples->size()) nextIndex = sampleIndex;
            const double fraction = voice.cursor - static_cast<double>(sampleIndex);
            const double rawSample = (*voice.samples)[sampleIndex]
                + fraction * ((*voice.samples)[nextIndex] - (*voice.samples)[sampleIndex]);
            voice.filterState += voice.filterAlpha
                               * (static_cast<float>(rawSample) - voice.filterState);
            const double sample = voice.filterState;
            const float gain = voice.envelopeVolume * voice.releaseGain
                             * sBusVolumes[voice.bus]
                             * (voice.bgmTrack == 0 ? sBgmTrackGain
                                : (voice.bgmTrack == 1 ? sBossTrackGain : 1.0f))
                             * (voice.bgmTrack == 0 && voice.bgmLayer < 16
                                ? sBgmLayerGain[voice.bgmLayer] : 1.0f)
                             * (voice.eventSource < 16
                                ? sEventVolumes[voice.eventSource] : 1.0f);
            float left = voice.left;
            float right = voice.right;
            const float eventPan = voice.eventSource < 16 ? sEventPans[voice.eventSource] : 0.0f;
            const float pan = std::clamp(
                voice.basePan + eventPan + voice.envelopePan, -1.0f, 1.0f);
            const float volume = voice.baseVolume * voice.trackVolume;
            left = volume * (pan <= 0.0f ? 1.0f : 1.0f - pan);
            right = volume * (pan >= 0.0f ? 1.0f : 1.0f + pan);
            if (statsOn) {
                note_bus_peak(voice.bus, sample * left * gain);
                const int contribution = std::abs(static_cast<int>(sample * left * gain));
                if (contribution > voice.peak) voice.peak = contribution;
            }
            mixedLeft += static_cast<int>(sample * left * gain);
            mixedRight += static_cast<int>(sample * right * gain);
            const float fxMix = 1.0f
                - (1.0f - std::clamp(voice.envelopeFxMix, 0.0f, 1.0f))
                * (1.0f - std::clamp(voice.trackFxMix, 0.0f, 1.0f));
            const float dolby = 1.0f
                - (1.0f - std::clamp(voice.envelopeDolby, 0.0f, 1.0f))
                * (1.0f - std::clamp(voice.trackDolby, 0.0f, 1.0f));
            fxSendLeft += static_cast<float>(sample * left * gain) * fxMix;
            fxSendRight += static_cast<float>(sample * right * gain) * fxMix;
            dolbySend += static_cast<float>(sample * (left + right) * gain * 0.5f)
                       * dolby;
            voice.cursor += voice.step * voice.envelopePitch;
            // GameCube envelopes are control signals, not audio-rate DSP. Step
            // them at 1 kHz instead of repeating the same relatively expensive
            // state-machine work for every 32 kHz sample. Passing the matching
            // effective rate preserves attack/release durations.
            if (voice.oscillators && voice.envelopeCountdown == 0) {
                voice.envelopeVolume = 1.0f;
                voice.envelopePitch = 1.0f;
                voice.envelopePan = 0.0f;
                voice.envelopeFxMix = voice.baseFxMix;
                voice.envelopeDolby = voice.baseDolby;
                const size_t count = std::min<size_t>(
                    voice.oscillators->size(),
                    sizeof(voice.envelopeStates) / sizeof(voice.envelopeStates[0]));
                for (size_t oscIndex = 0; oscIndex < count; ++oscIndex) {
                    const PCInstrumentOscillator& osc = (*voice.oscillators)[oscIndex];
                    const float value = pc_envelope_step(
                        &osc, &voice.envelopeStates[oscIndex],
                        static_cast<float>(sAudioSpec.freq) / kEnvelopeControlPeriod);
                    if (voice.envelopeStates[oscIndex].result != PCEnvelopeResult::Ok
                        || !pc_envelope_is_active(&voice.envelopeStates[oscIndex])) {
                        voice.active = false;
                        break;
                    }
                    if (osc.mode == 0) voice.envelopeVolume *= value;
                    else if (osc.mode == 1) voice.envelopePitch *= value;
                    else if (osc.mode == 2) voice.envelopePan += (value - 0.5f) * 2.0f;
                    else if (osc.mode == 3) voice.envelopeFxMix = value;
                    else if (osc.mode == 4) voice.envelopeDolby = value;
                }
                voice.envelopeCountdown = kEnvelopeControlPeriod - 1;
            } else if (voice.envelopeCountdown != 0) {
                --voice.envelopeCountdown;
            }
            if (voice.releaseFrames != 0) {
                voice.releaseGain = std::max(0.0f, voice.releaseGain - voice.releaseStep);
                if (--voice.releaseFrames == 0) voice.active = false;
            }
        }
        const float fxLeft = sFxDelayLeft[sFxDelayCursor];
        const float fxRight = sFxDelayRight[sFxDelayCursor];
        sFxDelayLeft[sFxDelayCursor] = fxSendLeft + fxRight * 0.18f + fxLeft * 0.32f;
        sFxDelayRight[sFxDelayCursor] = fxSendRight + fxLeft * 0.18f + fxRight * 0.32f;
        sFxDelayCursor = (sFxDelayCursor + 1) % kFxDelayFrames;
        mixedLeft += static_cast<int>(fxLeft * 0.35f);
        mixedRight += static_cast<int>(fxRight * 0.35f);

        const float surround = sDolbyDelay[sDolbyDelayCursor];
        sDolbyDelay[sDolbyDelayCursor] = dolbySend;
        sDolbyDelayCursor = (sDolbyDelayCursor + 1) % kDolbyDelayFrames;
        mixedLeft += static_cast<int>(surround * 0.35f);
        mixedRight -= static_cast<int>(surround * 0.35f);
        if (!sStereoOutput) mixedLeft = mixedRight = (mixedLeft + mixedRight) / 2;
        const int peak = std::max(std::abs(mixedLeft), std::abs(mixedRight));
        constexpr float kLimiterCeiling = 30000.0f;
        const float targetGain = peak > kLimiterCeiling
            ? kLimiterCeiling / static_cast<float>(peak) : 1.0f;
        if (targetGain < sLimiterGain) sLimiterGain = targetGain;
        else sLimiterGain += (1.0f - sLimiterGain)
                           / std::max(1.0f, sAudioSpec.freq * 0.1f);
        if (sLimiterGain < 0.9999f)
            sLimitedFrames.fetch_add(1, std::memory_order_relaxed);
        mixedLeft = static_cast<int>(mixedLeft * sLimiterGain);
        mixedRight = static_cast<int>(mixedRight * sLimiterGain);
        dst[frame * 2] = clamp_s16(mixedLeft);
        dst[frame * 2 + 1] = clamp_s16(mixedRight);
    }

    if (sDMAReadCursor >= sDMAQueue.size()) {
        sDMAQueue.clear();
        sDMAReadCursor = 0;
        sDMABytesLeft.store(0, std::memory_order_relaxed);
    } else {
        sDMABytesLeft.store(static_cast<u32>(std::min<size_t>(
            (sDMAQueue.size() - sDMAReadCursor) * sizeof(s16), UINT32_MAX)),
            std::memory_order_relaxed);
    }
}

bool pc_audio_init(void) {
    if (sAudioDevice != 0) {
        return true;
    }
    sBgmLayerGain.fill(1.0f);
    sBgmLayerTarget.fill(1.0f);
    sBgmLayerStep.fill(0.0f);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("[PC Port Error] SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq = 32000;              // GameCube native AI sample rate
    desired.format = AUDIO_S16SYS;      // 16-bit signed PCM
    desired.channels = 2;               // Stereo
    desired.samples = 1024;             // Buffer size in frames
    desired.callback = audio_callback;  // Native mixer: stream + JAudio DMA

    sAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &sAudioSpec, 0);
    if (sAudioDevice == 0) {
        printf("[PC Port Error] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudioDevice(sAudioDevice, 0); // Start audio playback
    printf("[PC Port] SDL2 Audio Subsystem initialized (%d Hz, %u channels)\n",
           sAudioSpec.freq, static_cast<unsigned>(sAudioSpec.channels));
    return true;
}

void pc_audio_shutdown(void) {

    if (sAudioDevice != 0) {
        SDL_LockAudioDevice(sAudioDevice);
        sStreamVoice = {};
        sDMAQueue.clear();
        sDMAReadCursor = 0;
        for (SampleVoice& voice : sSampleVoices) voice = {};
        sFxDelayLeft.fill(0.0f);
        sFxDelayRight.fill(0.0f);
        sDolbyDelay.fill(0.0f);
        sFxDelayCursor = 0;
        sDolbyDelayCursor = 0;
        sLimiterGain = 1.0f;
        SDL_UnlockAudioDevice(sAudioDevice);
        SDL_CloseAudioDevice(sAudioDevice);
        sAudioDevice = 0;
    }
    sDecodedWaveCache.clear();
    pc_audio_reset_metrics();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool pc_audio_play_stx(const char* path) {
    if (!path || !pc_audio_init()) {
        return false;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        printf("[PC Port Warning] Could not open STX stream: %s\n", path);
        return false;
    }
    const std::streamsize fileSize = input.tellg();
    if (fileSize < 32) {
        printf("[PC Port Warning] Invalid STX stream (short header): %s\n", path);
        return false;
    }
    input.seekg(0);
    std::vector<u8> file(static_cast<size_t>(fileSize));
    if (!input.read(reinterpret_cast<char*>(file.data()), fileSize)) {
        printf("[PC Port Warning] Failed reading STX stream: %s\n", path);
        return false;
    }

    const u32 encodedSize = read_be32(file.data());
    const u32 sampleCount = read_be32(file.data() + 4);
    const u16 sampleRate = read_be16(file.data() + 8);
    const u16 format = read_be16(file.data() + 10);
    const size_t bodySize = std::min<size_t>(encodedSize, file.size() - 32);
    if (format != 4 || sampleRate == 0 || bodySize < 18) {
        printf("[PC Port Warning] Unsupported STX format %u in %s\n",
               static_cast<unsigned>(format), path);
        return false;
    }

    static const int filters[16][2] = {
        { 0x0000,  0x0000}, { 0x0800,  0x0000}, { 0x0000,  0x0800}, { 0x0400,  0x0400},
        { 0x1000, -0x0800}, { 0x0e00, -0x0600}, { 0x0c00, -0x0400}, { 0x1200, -0x0a00},
        { 0x1068, -0x08c8}, { 0x12c0, -0x08fc}, { 0x1400, -0x0c00}, { 0x0800, -0x0800},
        { 0x0400, -0x0400}, {-0x0400,  0x0400}, {-0x0400,  0x0000}, {-0x0800,  0x0000},
    };
    const size_t blockCount = bodySize / 18;
    const size_t outputFrames = std::min<size_t>(sampleCount, blockCount * 16);
    std::vector<s16> pcm(outputFrames * 2);
    int history[2][2] = {};
    const u8* src = file.data() + 32;
    size_t frame = 0;
    for (size_t block = 0; block < blockCount && frame < outputFrames; ++block) {
        for (int channel = 0; channel < 2; ++channel) {
            const u8* encoded = src + block * 18 + channel * 9;
            const int scale = (encoded[0] >> 4) & 0x0f;
            const int predictor = encoded[0] & 0x0f;
            int newer = history[channel][0];
            int older = history[channel][1];
            for (int nibbleIndex = 0; nibbleIndex < 16 && frame + nibbleIndex < outputFrames; ++nibbleIndex) {
                const u8 packed = encoded[1 + nibbleIndex / 2];
                int nibble = (nibbleIndex & 1) ? (packed & 0x0f) : (packed >> 4);
                if (nibble >= 8) nibble -= 16;
				const int decoded = nibble * (1 << scale)
                    + ((filters[predictor][0] * newer + filters[predictor][1] * older) >> 11);
                const s16 sample = clamp_s16(decoded);
                pcm[(frame + nibbleIndex) * 2 + channel] = sample;
                older = newer;
                newer = sample;
            }
            history[channel][0] = newer;
            history[channel][1] = older;
        }
        frame += 16;
    }

    std::vector<s16> converted;
    if (!convert_pcm(pcm.data(), outputFrames, sampleRate, converted)) {
        return false;
    }
    SDL_LockAudioDevice(sAudioDevice);
    sStreamVoice.samples = std::move(converted);
    sStreamVoice.cursor = 0;
    sStreamVoice.active = true;
    sStreamFadeGain = 1.0f;
    sStreamFadeStep = 0.0f;
    SDL_UnlockAudioDevice(sAudioDevice);
    printf("[PC Port] Playing STX stream: %s (%u Hz, %zu frames)\n",
           path, static_cast<unsigned>(sampleRate), outputFrames);
    return true;
}

void pc_audio_stop_stream(void) {
    if (sAudioDevice) {
        SDL_LockAudioDevice(sAudioDevice);
        sStreamVoice.active = false;
        sStreamVoice.samples.clear();
        sStreamVoice.cursor = 0;
        sStreamFadeGain = 1.0f;
        sStreamFadeStep = 0.0f;
        SDL_UnlockAudioDevice(sAudioDevice);
    }
}

// `fadeFrames` esta en fotogramas de juego de 60 Hz, la misma unidad que usan
// Jac_DemoFade y pc_audio_fade_sequence_track. Cero corta ya, para que quien
// necesite parada inmediata siga teniendola.
void pc_audio_fade_stream(u32 fadeFrames) {
    if (!sAudioDevice) return;
    if (fadeFrames == 0) {
        pc_audio_stop_stream();
        return;
    }
    SDL_LockAudioDevice(sAudioDevice);
    if (sStreamVoice.active) {
        const double samples = double(fadeFrames) * double(sAudioSpec.freq) / 60.0;
        sStreamFadeStep = samples > 0.0 ? float(1.0 / samples) : 1.0f;
        SDL_UnlockAudioDevice(sAudioDevice);
        return;
    }
    // Sin stream sonando no hay nada que fundir: conservar la limpieza que
    // hacia la parada dura, para no dejar el buffer colgado.
    sStreamVoice.samples.clear();
    sStreamVoice.cursor = 0;
    sStreamFadeGain = 1.0f;
    sStreamFadeStep = 0.0f;
    SDL_UnlockAudioDevice(sAudioDevice);
}

bool pc_audio_load_wave_bank(const char* path) {
    if (sAudioDevice) SDL_LockAudioDevice(sAudioDevice);
    for (SampleVoice& voice : sSampleVoices) voice = {};
    if (sAudioDevice) SDL_UnlockAudioDevice(sAudioDevice);
    sDecodedWaveCache.clear();
    const auto clearHandles = [](auto& voices) {
        for (auto& track : voices)
            std::fill(std::begin(track), std::end(track), -1);
    };
    clearHandles(sJamVoices);
    clearHandles(sBossJamVoices);
    clearHandles(sSEJamVoices);
    clearHandles(sEventJamVoices);
    if (!path || !pc_audio_init()) return false;
    if (!sWaveBank.load(path)) {
        printf("[PC Port Error] Could not load JAudio wave bank %s: %s\n",
               path, sWaveBank.error().c_str());
        return false;
    }
    printf("[PC Port] JAudio wave catalog loaded: %zu WSYS, %zu waves\n",
           sWaveBank.waveSystemCount(), sWaveBank.waveCount());
    if (!sInstrumentBank.load(path)) {
        printf("[PC Port Error] Could not load JAudio instrument banks %s: %s\n",
               path, sInstrumentBank.error().c_str());
        return false;
    }
    printf("[PC Port] JAudio instrument catalog loaded: %zu/%zu IBNK, "
           "%zu instruments, %zu regions, %zu oscillators, %zu legacy percussion\n",
           sInstrumentBank.bankCount(), sInstrumentBank.bankSlotCount(),
           sInstrumentBank.instrumentCount(), sInstrumentBank.regionCount(),
           sInstrumentBank.oscillatorCount(),
           sInstrumentBank.unsupportedPercussionCount());
    if (!sSequenceArchive.load("assets/dataDir/SndData/Seqs/pikiseq.arc",
                               HEAD_pikiseq, kPikiSequenceHeaderSize)) {
        printf("[PC Port Error] Could not load JAudio sequence catalog: %s\n",
               sSequenceArchive.error().c_str());
        return false;
    }
    printf("[PC Port] JAudio sequence catalog loaded: %zu sequences\n",
           sSequenceArchive.size());
    std::vector<u8> soundSequence;
    if (sSequenceArchive.read(0, soundSequence)) {
        sSEJamPlayer.start(soundSequence, 0);
        sSEJamEvents.reserve(128);
        sJamEvents.reserve(128);
        sBossJamEvents.reserve(128);
        sSEJamPlayer.tick(sSEJamEvents, 4096);
        sSEJamTickAccumulator = 0.0;
        sSEJamLastCounter = SDL_GetPerformanceCounter();
    }
    std::vector<u8> eventSequence;
    // The positional event system lives in sysevent.jam, not pikise.jam.
    // Jac_InitEventSystem asks for track handle 0x20000 and takes its sixteen
    // children as the per-event tracks; 0x20000 is "ConnectName 2, 0", which
    // only sysevent.jam declares -- and its root opens exactly those sixteen
    // children in a loop before declaring it. pikise.jam's root opens six
    // children that are different machines entirely, two of them the sound
    // effect dispatchers, so event commands landed in a sound id's place.
    if (kEnablePositionalEventJam && sSequenceArchive.read(kEventSequence, eventSequence)) {
        sEventJamPlayer.start(eventSequence, 0);
        sEventJamEvents.reserve(128);
        sEventJamPlayer.tick(sEventJamEvents, 4096);
        sEventJamTickAccumulator = 0.0;
        sEventJamLastCounter = SDL_GetPerformanceCounter();
    }
    return true;
}

void pc_audio_stop_sequence(void) {
    pc_audio_stop_sequence_track(0);
}

void pc_audio_stop_sequence_track(u8 sequenceTrack) {
    printf("[DEBUG] pc_audio_stop_sequence_track(%u) called\n", sequenceTrack);
    if (sequenceTrack > 1) return;
    auto& voices = sequenceTrack == 0 ? sJamVoices : sBossJamVoices;
    for (auto& track : voices) {
        for (int& voice : track) {
            if (voice >= 0) pc_audio_stop_wave(voice);
            voice = -1;
        }
    }
    if (sAudioDevice) {
        SDL_LockAudioDevice(sAudioDevice);
        for (SampleVoice& voice : sSampleVoices) {
            if (!voice.active || voice.bgmTrack != sequenceTrack) continue;
            voice.active = false;
            voice.samples.reset();
            voice.cursor = 0.0;
        }
        SDL_UnlockAudioDevice(sAudioDevice);
    }
    if (sequenceTrack == 0) {
        sJamPlayer = {};
        sJamTickAccumulator = 0.0;
        sJamLastCounter = 0;
    } else {
        sBossJamPlayer = {};
        sBossJamTickAccumulator = 0.0;
        sBossJamLastCounter = 0;
        sBossTrackGain = sBossTrackTarget = 0.0f;
        sBossTrackStep = 0.0f;
        sBossTrackFadeTicks = 0;
        return;
    }
    sBgmTrackGain = sBgmTrackTarget = 1.0f;
    sBgmTrackStep = 0.0f;
    sBgmTrackFadeTicks = 0;
    sBgmMixAccumulator = 0.0;
    sBgmMixLastCounter = 0;
}

bool pc_audio_play_sequence(u32 sequence) {
    return pc_audio_play_sequence_track(0, sequence);
}

bool pc_audio_play_sequence_track(u8 sequenceTrack, u32 sequence) {
    if (sequenceTrack > 1) return false;
    std::vector<u8> data;
    if (!sSequenceArchive.read(sequence, data)) return false;
    pc_audio_stop_sequence_track(sequenceTrack);
    PCJamPlayer& player = sequenceTrack == 0 ? sJamPlayer : sBossJamPlayer;
    if (!player.start(data, static_cast<u8>(sequence))) return false;
    if (sequenceTrack == 0) sJamSequence = sequence;
    else sBossJamSequence = sequence;
    sJamUnresolvedNotes = 0;
    const u64 now = SDL_GetPerformanceCounter();
    if (sequenceTrack == 0) sJamLastCounter = now;
    else sBossJamLastCounter = now;
    sBgmMixLastCounter = now;
    const PCSequenceEntry* entry = sSequenceArchive.entry(sequence);
    printf("[PC Port] Native JAM BGM track %u started: %u (%s)\n",
           sequenceTrack, sequence, entry ? entry->name.c_str() : "unknown");
    return true;
}

bool pc_audio_sequence_track_active(u8 sequenceTrack) {
    if (sequenceTrack == 0)
        return sJamPlayer.result() == PCJamResult::Ok && sJamPlayer.active();
    if (sequenceTrack == 1)
        return sBossJamPlayer.result() == PCJamResult::Ok && sBossJamPlayer.active();
    return false;
}

bool pc_audio_write_sequence_port(u8 sequenceTrack, u8 port, u16 value) {
    if (sequenceTrack == 0) return sJamPlayer.writeRootPort(port, value);
    if (sequenceTrack == 1) return sBossJamPlayer.writeRootPort(port, value);
    return false;
}

void pc_audio_set_sequence_layers(u16 enabledMask, float volume, u32 fadeFrames) {
    const float enabledVolume = std::clamp(volume, 0.0f, 2.0f);
    sBgmLayerFadeTicks = fadeFrames;
    for (u8 layer = 0; layer < 16; ++layer) {
        const float target = (enabledMask & static_cast<u16>(1u << layer))
            ? enabledVolume : 0.0f;
        sBgmLayerTarget[layer] = target;
        sBgmLayerStep[layer] = fadeFrames
            ? (target - sBgmLayerGain[layer]) / static_cast<float>(fadeFrames) : 0.0f;
        if (!fadeFrames) sBgmLayerGain[layer] = target;
    }
}

void pc_audio_fade_sequence(float volume, u32 fadeFrames) {
    pc_audio_fade_sequence_track(0, volume, fadeFrames);
}

void pc_audio_fade_sequence_track(u8 sequenceTrack, float volume, u32 fadeFrames) {
    if (sequenceTrack == 1) {
        sBossTrackTarget = std::clamp(volume, 0.0f, 1.0f);
        sBossTrackFadeTicks = fadeFrames;
        sBossTrackStep = fadeFrames
            ? (sBossTrackTarget - sBossTrackGain) / static_cast<float>(fadeFrames) : 0.0f;
        if (!fadeFrames) sBossTrackGain = sBossTrackTarget;
        return;
    }
    if (sequenceTrack != 0) return;
    sBgmTrackTarget = std::clamp(volume, 0.0f, 1.0f);
    sBgmTrackFadeTicks = fadeFrames;
    sBgmTrackStep = fadeFrames
        ? (sBgmTrackTarget - sBgmTrackGain) / static_cast<float>(fadeFrames) : 0.0f;
    if (!fadeFrames) sBgmTrackGain = sBgmTrackTarget;
}

// A finished voice has to be reported back to the sequencer: a one-shot effect
// parks on its note until the mixer says the sound has stopped, and only then
// tells the game its action is over and frees the event slot.
static bool wave_still_playing(int voiceHandle) {
    if (voiceHandle < 0) return false;
    const size_t index = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (index >= kSampleVoiceCount) return false;
    const SampleVoice& voice = sSampleVoices[index];
    return voice.active && voice.generation == generation;
}

template <size_t N>
static void reap_finished_voices(PCJamPlayer& player, int (&voices)[N][8]) {
    if (!sAudioDevice) return;
    SDL_LockAudioDevice(sAudioDevice);
    for (size_t track = 0; track < N; ++track) {
        for (u8 voice = 0; voice < 8; ++voice) {
            int& handle = voices[track][voice];
            if (handle < 0 || wave_still_playing(handle)) continue;
            handle = -1;
            player.noteFinished(static_cast<u8>(track), voice);
        }
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

static void tag_bgm_voice(int voiceHandle, u8 sequenceTrack, u8 layer) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t index = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (index >= kSampleVoiceCount) return;
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[index];
    if (voice.active && voice.generation == generation) {
        voice.bgmTrack = sequenceTrack;
        voice.bgmLayer = layer;
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

static void label_voice(int voiceHandle, u8 bank, u8 program, u8 key) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t index = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (index >= kSampleVoiceCount) return;
    SampleVoice& voice = sSampleVoices[index];
    if (voice.active && voice.generation == generation) {
        voice.reportBank = bank;
        voice.reportProgram = program;
        voice.reportKey = key;
        voice.peak = 0;
    }
}

static void tag_event_voice(int voiceHandle, u8 eventSource) {
    if (!sAudioDevice || voiceHandle < 0 || eventSource >= 16) return;
    const size_t index = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (index >= kSampleVoiceCount) return;
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[index];
    if (voice.active && voice.generation == generation) voice.eventSource = eventSource;
    SDL_UnlockAudioDevice(sAudioDevice);
}

static void tag_jam_voice(int voiceHandle, u8 owner, u8 source) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t index = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (index >= kSampleVoiceCount) return;
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[index];
    if (voice.active && voice.generation == generation) {
        voice.jamOwner = owner;
        voice.jamSource = source;
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

static void advance_bgm_mix(void) {
    const u64 now = SDL_GetPerformanceCounter();
    const u64 frequency = SDL_GetPerformanceFrequency();
    if (sBgmMixLastCounter && frequency) {
        const double elapsed = static_cast<double>(now - sBgmMixLastCounter) / frequency;
        sBgmMixAccumulator += elapsed * 60.0;
    }
    sBgmMixLastCounter = now;
    while (sBgmMixAccumulator >= 1.0) {
        sBgmMixAccumulator -= 1.0;
        if (sBgmTrackFadeTicks) {
            sBgmTrackGain += sBgmTrackStep;
            if (--sBgmTrackFadeTicks == 0) {
                sBgmTrackGain = sBgmTrackTarget;
            }
        }
        if (sBossTrackFadeTicks) {
            sBossTrackGain += sBossTrackStep;
            if (--sBossTrackFadeTicks == 0) {
                sBossTrackGain = sBossTrackTarget;
            }
        }
        if (sBgmLayerFadeTicks) {
            for (u8 layer = 0; layer < 16; ++layer) {
                sBgmLayerGain[layer] += sBgmLayerStep[layer];
            }
            if (--sBgmLayerFadeTicks == 0) {
                for (u8 layer = 0; layer < 16; ++layer) {
                    sBgmLayerGain[layer] = sBgmLayerTarget[layer];
                }
            }
        }
    }
}

int pc_audio_play_wave(u32 waveSystem, u32 archive, u32 waveIndex,
                       float volume, float pan, bool looping) {
    return pc_audio_play_wave_ex(waveSystem, archive, waveIndex, volume, pan,
                                 looping, PC_AUDIO_BUS_SE, 64);
}

static int play_wave_info(const PCWaveInfo* info, u64 cacheKey,
                          float pitchScale, float volume, float pan,
                          bool looping, PCAudioBus bus, u8 priority,
                          const std::vector<PCInstrumentOscillator>* oscillators = nullptr,
                          std::shared_ptr<const std::vector<s16>> decodedOverride = {},
                          u16 defaultReleaseParam = 0,
                          float trackVolume = 1.0f,
                          float trackPitch = 1.0f,
                          u8 cutoff = 127,
                          float fxMix = 0.0f,
                          float dolby = 0.0f,
                          float trackFxMix = 0.0f,
                          float trackDolby = 0.0f,
                          std::shared_ptr<const std::vector<PCInstrumentOscillator>>
                              ownedOscillators = {}) {
    if (!pc_audio_init()) return -1;
    if (bus < 0 || bus >= PC_AUDIO_BUS_COUNT) return -1;
    if (!info) return -1;
    std::shared_ptr<const std::vector<s16>> decoded = std::move(decodedOverride);
    const auto cached = sDecodedWaveCache.find(cacheKey);
    if (!decoded && cached != sDecodedWaveCache.end()) {
        decoded = cached->second;
    } else if (!decoded) {
        auto fresh = std::make_shared<std::vector<s16>>();
        if (!pc_decode_wave(*info, *fresh)) return -1;
        decoded = fresh;
        sDecodedWaveCache.emplace(cacheKey, decoded);
    }

    volume = std::clamp(volume, 0.0f, 1.0f);
    pan = std::clamp(pan, -1.0f, 1.0f);
    trackVolume = std::clamp(trackVolume, 0.0f, 2.0f);
    trackPitch = std::clamp(trackPitch, 0.125f, 8.0f);
    SDL_LockAudioDevice(sAudioDevice);
    size_t selected = kSampleVoiceCount;
    for (size_t i = 0; i < kSampleVoiceCount; ++i) {
        if (!sSampleVoices[i].active) {
            selected = i;
            break;
        }
    }
    if (selected == kSampleVoiceCount) {
        // Steal the oldest voice in the lowest priority class. A new sound may
        // not evict a more important voice (for example UI over BGM sustain).
        u8 lowestPriority = std::numeric_limits<u8>::max();
        u64 oldestAge = std::numeric_limits<u64>::max();
        for (size_t i = 0; i < kSampleVoiceCount; ++i) {
            const SampleVoice& candidate = sSampleVoices[i];
            if (candidate.priority < lowestPriority
                || (candidate.priority == lowestPriority && candidate.age < oldestAge)) {
                selected = i;
                lowestPriority = candidate.priority;
                oldestAge = candidate.age;
            }
        }
        if (selected == kSampleVoiceCount || lowestPriority > priority) {
            sVoiceRejects.fetch_add(1, std::memory_order_relaxed);
            SDL_UnlockAudioDevice(sAudioDevice);
            return -1;
        }
        sVoiceSteals.fetch_add(1, std::memory_order_relaxed);
    }
    SampleVoice& voice = sSampleVoices[selected];
    sVoiceGenerations[selected] = static_cast<u16>(sVoiceGenerations[selected] % 0x7FFF + 1);
    voice.generation = sVoiceGenerations[selected];
    voice.samples = decoded;
    voice.cursor = 0.0;
    voice.baseStep = static_cast<double>(info->sampleRate) / sAudioSpec.freq * pitchScale;
    voice.trackPitch = trackPitch;
    voice.step = voice.baseStep * voice.trackPitch;
    voice.loopSample = std::min<size_t>(info->loopSample, voice.samples->size());
    voice.endSample = info->looping && info->loopEndSample
        ? std::min<size_t>(info->loopEndSample, voice.samples->size())
        : voice.samples->size();
    voice.trackVolume = trackVolume;
    const float mixedVolume = volume * voice.trackVolume;
    voice.left = mixedVolume * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    voice.right = mixedVolume * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    voice.baseVolume = volume;
    voice.basePan = pan;
    voice.releaseGain = 1.0f;
    voice.releaseStep = 0.0f;
    voice.releaseFrames = 0;
    voice.defaultReleaseParam = defaultReleaseParam;
    voice.oscillatorsOwned = std::move(ownedOscillators);
    voice.oscillators = oscillators;
    voice.envelopeVolume = 1.0f;
    voice.envelopePitch = 1.0f;
    voice.envelopePan = 0.0f;
    voice.baseFxMix = std::clamp(fxMix, 0.0f, 1.0f);
    voice.baseDolby = std::clamp(dolby, 0.0f, 1.0f);
    voice.envelopeFxMix = voice.baseFxMix;
    voice.envelopeDolby = voice.baseDolby;
    voice.trackFxMix = std::clamp(trackFxMix, 0.0f, 1.0f);
    voice.trackDolby = std::clamp(trackDolby, 0.0f, 1.0f);
    const float cutoffHz = 80.0f * std::pow(
        200.0f, std::min<u8>(cutoff, 127) / 127.0f);
    voice.filterAlpha = cutoff >= 127 ? 1.0f
        : 1.0f - std::exp(-6.28318530718f * cutoffHz / sAudioSpec.freq);
    voice.filterState = 0.0f;
    voice.envelopeCountdown = 0;
    for (PCEnvelopeState& state : voice.envelopeStates) state = {};
    if (voice.oscillators) {
        const size_t count = std::min<size_t>(
            voice.oscillators->size(),
            sizeof(voice.envelopeStates) / sizeof(voice.envelopeStates[0]));
        for (size_t i = 0; i < count; ++i) {
            const PCInstrumentOscillator& osc = (*voice.oscillators)[i];
            pc_envelope_init_attack(&voice.envelopeStates[i], &osc);
            const float value = pc_envelope_step(
                &osc, &voice.envelopeStates[i],
                static_cast<float>(sAudioSpec.freq) / kEnvelopeControlPeriod);
            if (osc.mode == 0) voice.envelopeVolume *= value;
            else if (osc.mode == 1) voice.envelopePitch *= value;
            else if (osc.mode == 2) voice.envelopePan += (value - 0.5f) * 2.0f;
            else if (osc.mode == 3) voice.envelopeFxMix = value;
            else if (osc.mode == 4) voice.envelopeDolby = value;
        }
    }
    voice.age = ++sVoiceAge;
    voice.priority = priority;
    voice.bus = bus;
    voice.bgmTrack = 0xFF;
    voice.bgmLayer = 0xFF;
    voice.eventSource = 0xFF;
    voice.jamOwner = 0;
    voice.jamSource = 0xFF;
    voice.looping = looping && info->looping;
    voice.active = true;
    SDL_UnlockAudioDevice(sAudioDevice);
    return static_cast<int>((static_cast<u32>(voice.generation) << 8) | selected);
}

static int play_jaudio_oscillator(u8 oscillator, u8 key, u8 velocity,
                                  float volume, float pan, PCAudioBus bus,
                                  u8 priority, float trackPitch,
                                  const std::shared_ptr<const std::vector<PCInstrumentOscillator>>& envelope) {
    // Programs F0-FF are DSP-generated sources, not missing IBNK instruments.
    // Recreate their periodic sources so event effects do not disappear merely
    // because there is no wave archive entry to resolve.
    static std::array<std::shared_ptr<const std::vector<s16>>, 16> waves;
    oscillator &= 15;
    if (!waves[oscillator]) {
        // 64 entries, not an arbitrary length: basePitch is 16736.016/32000,
        // so a 64-sample period puts key 60 at 16736.016/64 = 261.5 Hz, middle
        // C, which is what the constant is there for. At 256 every oscillator
        // note came out two octaves low -- the ship's idle landed at 49 Hz with
        // notes only 25 ms long, barely one cycle each, which is a click rather
        // than a tone and read as static.
        auto pcm = std::make_shared<std::vector<s16>>(64);
        for (size_t i = 0; i < pcm->size(); ++i) {
            const float phase = static_cast<float>(i) / pcm->size();
            float sample = 0.0f;
            switch (oscillator) {
            case 0: sample = std::sin(phase * 6.28318530718f); break;
            case 1: sample = phase < 0.5f ? 1.0f : -1.0f; break;
            case 2: sample = phase * 2.0f - 1.0f; break;
            case 3: sample = 1.0f - 4.0f * std::abs(phase - 0.5f); break;
            case 4: sample = phase < 0.25f ? 1.0f : -1.0f; break;
            case 5: sample = phase < 0.125f ? 1.0f : -1.0f; break;
            // No noise sources. Nothing in the decompilation says any of these
            // is noise -- the real shapes live in DSP microcode -- and noise is
            // the one guess that cannot be told apart from a defect: it is what
            // "static" means. The ship's blinking light uses oscillator 7, and
            // as noise it buzzed next to the ship for the whole session.
            case 6: sample = std::sin(phase * 6.28318530718f)
                           + 0.5f * std::sin(phase * 12.5663706144f);
                sample *= 0.667f;
                break;
            case 7: sample = phase < 0.0625f ? 1.0f : -1.0f; break;
            default: {
                const unsigned harmonic = 1u + (oscillator - 8u) / 2u;
                sample = std::sin(phase * 6.28318530718f * harmonic);
                if (oscillator & 1) sample = sample >= 0.0f ? 1.0f : -1.0f;
                break;
            }
            }
            (*pcm)[i] = static_cast<s16>(std::clamp(sample, -1.0f, 1.0f) * 12000.0f);
        }
        waves[oscillator] = pcm;
    }
    PCWaveInfo info;
    info.sampleRate = 16736.016f;
    info.looping = true;
    info.loopSample = 0;
    const float notePitch = std::pow(2.0f, (static_cast<int>(key) - 60) / 12.0f);
    const float velocityGain = velocity / 127.0f;
    return play_wave_info(&info, 0xF000000000000000ULL | oscillator,
                          notePitch, velocityGain * velocityGain,
                          pan, true, bus, priority, envelope ? envelope.get() : nullptr,
                          waves[oscillator], 0, volume, trackPitch, 127, 0.0f, 0.0f,
                          0.0f, 0.0f, envelope);
}

int pc_audio_play_wave_ex(u32 waveSystem, u32 archive, u32 waveIndex,
                          float volume, float pan, bool looping,
                          PCAudioBus bus, u8 priority) {
    const PCWaveInfo* info = sWaveBank.wave(waveSystem, archive, waveIndex);
    const u64 cacheKey = (static_cast<u64>(waveSystem) << 40)
                       | (static_cast<u64>(archive) << 20) | waveIndex;
    return play_wave_info(info, cacheKey, 1.0f, volume, pan, looping, bus, priority);
}

int pc_audio_play_note(u32 virtualBank, u32 program, u8 key, u8 velocity,
                       u32 waveScene, float volume, float pan,
                       PCAudioBus bus, u8 priority, float trackPitch,
                       u8 cutoff, float fxMix, float dolby,
                       std::shared_ptr<const std::vector<PCInstrumentOscillator>> envelope) {
    if (program >= 0xF0) {
        // Programs F0+ are synthesised by the GameCube DSP, whose waveforms
        // live in microcode the decompilation does not contain. Everything
        // else in this port is derived from the original; these shapes cannot
        // be, and three attempts at guessing them all came back as "static"
        // from in-game testing -- the ship's blinking light is two detuned
        // looping voices at 46 Hz, which is a buzz whatever waveform is under
        // it. Silence is the honest default for a sound that cannot be
        // reproduced. PIKMIN_OSC=1 turns the approximation back on.
        static const bool oscillatorsEnabled = [] {
            const char* value = getenv("PIKMIN_OSC");
            return value != nullptr && value[0] == '1';
        }();
        if (!oscillatorsEnabled) return -1;
        return play_jaudio_oscillator(static_cast<u8>(program - 0xF0), key,
                                      velocity, volume, pan, bus, priority,
                                      trackPitch, envelope);
    }
    PCInstrumentSelection selection;
    if (!sInstrumentBank.select(virtualBank, program, key, velocity, selection)) {
        sMissInstrument.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    // A region with waveSystem == -1 defers to its bank's own wave system. That
    // is the bank's global id (IBNK header +8, the same id the virtual-to-
    // physical bank table is keyed by), resolved through the wave catalogue --
    // not the bank's physical slot index, which is just where it happened to be
    // stored. Using the slot sent every such lookup to wave system 0, where the
    // samples are not: whistle-era sounds resolved to ids 16 and 19-22, all of
    // which live in wave system 2, and simply went silent.
    const int waveSystem = selection.region.waveSystem == -1
        ? static_cast<int>(selection.physicalBank)
        : sWaveBank.physicalWaveSystem(static_cast<u16>(selection.region.waveSystem));
    const PCWaveInfo* info = sWaveBank.waveByIdPhysical(
        static_cast<u32>(waveSystem), static_cast<u16>(selection.region.waveId), waveScene);

    if (!info) {
        sMissWave.fetch_add(1, std::memory_order_relaxed);
        const bool elsewhere = sWaveBank.waveByIdAnyScene(
            static_cast<u32>(waveSystem), static_cast<u16>(selection.region.waveId)) != nullptr;
        if (elsewhere) sMissWaveElsewhere.fetch_add(1, std::memory_order_relaxed);
        // The instrument may simply have been looked up in the wrong archive:
        // when a region does not name its wave system the code falls back to
        // the bank index. Searching every system says whether the sample exists
        // under a different one, which is a mapping bug, or nowhere at all,
        // which means the data was never loaded.
        int otherSystem = -1;
        for (u32 sys = 0; sys < sWaveBank.waveSystemCount(); ++sys) {
            if (static_cast<int>(sys) == waveSystem) continue;
            if (sWaveBank.waveByIdAnyScene(sys, static_cast<u16>(selection.region.waveId))) {
                otherSystem = static_cast<int>(sys);
                break;
            }
        }
        note_wave_miss(waveSystem, selection.region.waveId,
                       static_cast<int>(waveScene), elsewhere, otherSystem,
                       selection.percussion, static_cast<int>(program));
        return -1;
    }

    // Play_1shot transposes by the note; Play_1shot_Perc does not -- it sets
    // currentPitch straight from basePitch, because a percussion key selects
    // *which* drum to hit, not what pitch to hit it at. Transposing percussion
    // plays every drum away from the sample's own rate.
    float notePitch = 1.0f;
    if (!selection.percussion) {
        const int pitchKey = std::clamp<int>(static_cast<int>(key) + 60 - info->key, 0, 127);
        notePitch = std::pow(2.0f, (pitchKey - 60) / 12.0f);
    }
    const float pitchScale = selection.region.pitch * selection.instrumentPitch
                           * notePitch;
    const float velocityGain = velocity / 127.0f;
    float noteVolume = selection.region.volume * selection.instrumentVolume
                     * velocityGain * velocityGain;
    float effectPitch = 1.0f;
    float effectPan = 0.0f;
    float effectFxMix = 0.0f;
    float effectDolby = 0.0f;
    static u32 effectRandom = 0x4A415544u;
    if (selection.effects) for (const PCInstrumentEffect& effect : *selection.effects) {
        float effectValue;
        if (effect.sensor) {
            const u8 trigger = effect.type == 1 ? velocity : (effect.type == 2 ? key : 0);
            if (effect.threshold == 0 || effect.threshold == 127) {
                effectValue = effect.value + trigger * (effect.maximum - effect.value) / 127.0f;
            } else if (trigger < effect.threshold) {
                effectValue = effect.value + (1.0f - effect.value)
                            * trigger / effect.threshold;
            } else {
                effectValue = 1.0f + (effect.maximum - 1.0f)
                            * (trigger - effect.threshold) / (127.0f - effect.threshold);
            }
        } else {
            effectRandom = effectRandom * 1664525u + 1013904223u;
            const float random = static_cast<s32>(effectRandom) / 2147483648.0f;
            effectValue = effect.value + random * effect.range;
        }
        if (effect.id == 0) noteVolume *= effectValue;
        else if (effect.id == 1) effectPitch *= effectValue;
        else if (effect.id == 2) effectPan += (effectValue - 0.5f) * 2.0f;
        else if (effect.id == 3) effectFxMix = effectValue;
        else if (effect.id == 4) effectDolby = effectValue;
    }
    const u64 cacheKey = 0x8000000000000000ULL
                       ^ static_cast<u64>(reinterpret_cast<uintptr_t>(info));
    const std::vector<PCInstrumentOscillator>* oscillators = selection.oscillators;
    const int handle = play_wave_info(info, cacheKey, pitchScale * effectPitch, noteVolume,
                          std::clamp(pan + effectPan, -1.0f, 1.0f),
                          info->looping, bus, priority, oscillators, {},
                          selection.region.release, volume, trackPitch, cutoff,
                          effectFxMix, effectDolby, fxMix, dolby);
    if (handle >= 0) label_voice(handle, static_cast<u8>(virtualBank),
                                 static_cast<u8>(program), key);
    return handle;
}

static bool restart_event_jam() {
    if (!kEnablePositionalEventJam) return false;
    for (auto& track : sEventJamVoices) {
        for (int& handle : track) {
            if (handle >= 0) pc_audio_stop_wave(handle);
            handle = -1;
        }
    }
    std::vector<u8> sequence;
    if (!sSequenceArchive.read(kEventSequence, sequence)
        || !sEventJamPlayer.start(sequence, 0))
        return false;
    sEventJamPlayer.tick(sEventJamEvents, 4096);
    if (sEventsPaused) {
        for (u8 child = 0; child < 16; ++child)
            sEventJamPlayer.setChildPaused(child, true);
    }
    sEventJamTickAccumulator = 0.0;
    sEventJamLastCounter = SDL_GetPerformanceCounter();
    return sEventJamPlayer.result() == PCJamResult::Ok;
}

static bool restart_se_jam() {
    for (auto& track : sSEJamVoices) {
        for (int& handle : track) {
            if (handle >= 0) pc_audio_stop_wave(handle);
            handle = -1;
        }
    }
    std::vector<u8> sequence;
    if (!sSequenceArchive.read(0, sequence) || !sSEJamPlayer.start(sequence, 0))
        return false;
    sSEJamPlayer.tick(sSEJamEvents, 4096);
    for (u8 child = 0; child < sSETrackPaused.size(); ++child)
        if (sSETrackPaused[child]) sSEJamPlayer.setChildPaused(child, true);
    sSEJamTickAccumulator = 0.0;
    sSEJamLastCounter = SDL_GetPerformanceCounter();
    return sSEJamPlayer.result() == PCJamResult::Ok;
}

bool pc_audio_send_system_se(u16 id, bool stop) {
    if (sSEJamPlayer.result() != PCJamResult::Ok && !restart_se_jam()) return false;
    return sSEJamPlayer.writeChildPort(9, stop ? 1 : 0, id);
}

bool pc_audio_send_orima_se(u16 id, bool stop, bool pikiSound) {
    if (sSEJamPlayer.result() != PCJamResult::Ok && !restart_se_jam()) return false;
    const u8 port = stop ? 2 : (pikiSound ? 1 : 0);
    return sSEJamPlayer.writeChildPort(10, port, id);
}

bool pc_audio_write_se_port(u8 track, u8 port, u16 value) {
    if (sSEJamPlayer.result() != PCJamResult::Ok && !restart_se_jam()) return false;
    return sSEJamPlayer.writeChildPort(track, port, value);
}

void pc_audio_set_se_track_volume(u8 track, float volume) {
    sSEJamPlayer.setChildVolume(track, volume);
}

void pc_audio_set_se_track_paused(u8 track, bool paused) {
    if (track >= sSETrackPaused.size()) return;
    sSEJamPlayer.setChildPaused(track, paused);
    if (sAudioDevice) SDL_LockAudioDevice(sAudioDevice);
    sSETrackPaused[track] = paused;
    if (sAudioDevice) SDL_UnlockAudioDevice(sAudioDevice);
}

void pc_audio_set_events_paused(bool paused) {
    for (u8 child = 0; child < 16; ++child)
        sEventJamPlayer.setChildPaused(child, paused);
    if (sAudioDevice) SDL_LockAudioDevice(sAudioDevice);
    sEventsPaused = paused;
    if (sAudioDevice) SDL_UnlockAudioDevice(sAudioDevice);
}

static void (*sEventActionFinishedHook)(u8, u8) = nullptr;

void pc_audio_set_event_action_finished_hook(void (*hook)(u8 event, u8 slot)) {
    sEventActionFinishedHook = hook;
}

bool pc_audio_send_event_action(u8 event, u8 slot, u16 command, bool stop) {
    if (!kEnablePositionalEventJam) return false;
    if (event >= 16 || slot >= 16) return false;
    if (sEventJamPlayer.result() != PCJamResult::Ok && !restart_event_jam()) return false;
    // sysevent.jam's per-event handler (byte 98) splits the word into a slot
    // (top nibble) and a command (low 12 bits), and treats command 0 as "close
    // this slot"; 0xFFFF closes the whole event. Command 0 is therefore never a
    // real action -- ACTION_STATUS entry 0 sits below every event type's offset
    // -- so the port's old 0x0FFF stop value was read as a genuine command,
    // opened a track for it and jumped past the end of its dispatch table,
    // killing the event sequence every time a sound was stopped.
    const u16 value = static_cast<u16>((static_cast<u16>(slot) << 12)
                    | (stop ? 0u : (command & 0x0FFFu)));
    return sEventJamPlayer.writeChildPort(event, 0, value);
}

void pc_audio_set_event_mix(u8 event, float volume, float pan) {
    if (!kEnablePositionalEventJam) return;
    if (event >= 16) return;
    sEventVolumes[event] = std::clamp(volume, 0.0f, 1.0f);
    sEventPans[event] = std::clamp(pan, -1.0f, 1.0f);
}

void pc_audio_stop_wave(int voiceHandle) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t voiceIndex = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (voiceIndex >= kSampleVoiceCount) return;
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[voiceIndex];
    if (voice.generation != generation) {
        SDL_UnlockAudioDevice(sAudioDevice);
        return;
    }
    voice.active = false;
    voice.samples.reset();
    voice.cursor = 0.0;
    SDL_UnlockAudioDevice(sAudioDevice);
}

void pc_audio_update_wave(int voiceHandle, float volume, float pan, float pitch,
                          u8 cutoff, float fxMix, float dolby) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t voiceIndex = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (voiceIndex >= kSampleVoiceCount) return;
    volume = std::clamp(volume, 0.0f, 2.0f);
    pan = std::clamp(pan, -1.0f, 1.0f);
    pitch = std::clamp(pitch, 0.125f, 8.0f);
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[voiceIndex];
    if (voice.active && voice.generation == generation) {
        voice.trackVolume = volume;
        voice.trackPitch = pitch;
        voice.trackFxMix = std::clamp(fxMix, 0.0f, 1.0f);
        voice.trackDolby = std::clamp(dolby, 0.0f, 1.0f);
        voice.basePan = pan;
        voice.step = voice.baseStep * voice.trackPitch;
        const float cutoffHz = 80.0f * std::pow(
            200.0f, std::min<u8>(cutoff, 127) / 127.0f);
        voice.filterAlpha = cutoff >= 127 ? 1.0f
            : 1.0f - std::exp(-6.28318530718f * cutoffHz / sAudioSpec.freq);
        const float mixedVolume = voice.baseVolume * voice.trackVolume;
        voice.left = mixedVolume * (pan <= 0.0f ? 1.0f : 1.0f - pan);
        voice.right = mixedVolume * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

void pc_audio_release_wave(int voiceHandle, u32 releaseFrames, u16 releaseParam) {
    if (!sAudioDevice || voiceHandle < 0) return;
    const size_t voiceIndex = static_cast<u32>(voiceHandle) & 0xFF;
    const u16 generation = static_cast<u32>(voiceHandle) >> 8;
    if (voiceIndex >= kSampleVoiceCount) return;
    SDL_LockAudioDevice(sAudioDevice);
    SampleVoice& voice = sSampleVoices[voiceIndex];
    if (voice.active && voice.generation == generation) {
        if (voice.oscillators && !voice.oscillators->empty()) {
            const u16 parameter = releaseParam ? releaseParam : voice.defaultReleaseParam;
            const size_t count = std::min<size_t>(
                voice.oscillators->size(),
                sizeof(voice.envelopeStates) / sizeof(voice.envelopeStates[0]));
            for (size_t i = 0; i < count; ++i) {
                pc_envelope_init_release(
                    &voice.envelopeStates[i], &(*voice.oscillators)[i], parameter);
            }
        } else if (releaseFrames == 0) {
            voice.active = false;
            voice.samples.reset();
            voice.cursor = 0.0;
        } else {
            voice.releaseFrames = releaseFrames;
            voice.releaseStep = voice.releaseGain / static_cast<float>(releaseFrames);
        }
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

// Prints what each path is really contributing, next to the gains that produced
// it. Call once every couple of seconds; peaks reset on each report so the
// figures describe the interval just played, not the loudest thing ever heard.
void pc_audio_report_levels(void) {
    if (!pc_audio_stats_enabled()) return;
    const int stream = sBusPeak[PC_AUDIO_BUS_STREAM].exchange(0, std::memory_order_relaxed);
    const int bgm    = sBusPeak[PC_AUDIO_BUS_BGM].exchange(0, std::memory_order_relaxed);
    const int se     = sBusPeak[PC_AUDIO_BUS_SE].exchange(0, std::memory_order_relaxed);
    const int dma    = sBusPeak[PC_AUDIO_BUS_DMA].exchange(0, std::memory_order_relaxed);
    fprintf(stderr,
            "[PC Audio] peak  stream=%5d  bgm=%5d  se=%5d  dma=%5d   (of 32767)\n",
            stream, bgm, se, dma);
    // The three loudest voices of the interval, by name. A complaint about a
    // noise can then be pinned on the instrument making it.
    if (sAudioDevice) {
        struct Loud { int peak; u8 bank, program, key, bus, event; bool looping; };
        Loud top[3] = {};
        SDL_LockAudioDevice(sAudioDevice);
        for (SampleVoice& voice : sSampleVoices) {
            if (!voice.active) continue;
            Loud entry { voice.peak, voice.reportBank, voice.reportProgram,
                         voice.reportKey, static_cast<u8>(voice.bus),
                         voice.eventSource, voice.looping };
            voice.peak = 0;
            for (Loud& slot : top)
                if (entry.peak > slot.peak) std::swap(entry, slot);
        }
        SDL_UnlockAudioDevice(sAudioDevice);
        for (const Loud& slot : top)
            if (slot.peak > 0)
                fprintf(stderr, "[PC Audio]   voz fuerte: pico %5d  banco %u programa %u "
                                "tecla %u  bus %u evento %u%s\n",
                        slot.peak, slot.bank, slot.program, slot.key, slot.bus,
                        slot.event, slot.looping ? "  EN BUCLE" : "");
    }
    const int missInstrument = sMissInstrument.exchange(0, std::memory_order_relaxed);
    const int missWaveSystem = sMissWaveSystem.exchange(0, std::memory_order_relaxed);
    const int missWave       = sMissWave.exchange(0, std::memory_order_relaxed);
    // These two are cumulative and are also read by pc_audio_get_metrics, so
    // take a delta here rather than resetting them out from under that caller.
    static u32 lastRejects = 0, lastSteals = 0;
    const u32 rejectsNow = sVoiceRejects.load(std::memory_order_relaxed);
    const u32 stealsNow  = sVoiceSteals.load(std::memory_order_relaxed);
    const int rejects = static_cast<int>(rejectsNow - lastRejects);
    const int steals  = static_cast<int>(stealsNow - lastSteals);
    lastRejects = rejectsNow;
    lastSteals  = stealsNow;
    if (missInstrument || missWaveSystem || missWave || rejects) {
        fprintf(stderr,
                "[PC Audio] DROPPED  no-instrument=%d  no-wavesystem=%d  no-wave=%d (of which %d exist in another scene)  no-voice=%d   (steals=%d)\n",
                missInstrument, missWaveSystem, missWave,
                sMissWaveElsewhere.exchange(0, std::memory_order_relaxed), rejects, steals);
        std::lock_guard<std::mutex> lock(sWaveMissMutex);
        for (const auto& miss : sWaveMisses) {
            if (!miss.count) break;
            char where[64];
            if (miss.elsewhere) snprintf(where, sizeof(where), "present in another scene");
            else if (miss.otherSystem >= 0)
                snprintf(where, sizeof(where), "PRESENT IN WSYS %d", miss.otherSystem);
            else snprintf(where, sizeof(where), "absent from every wave system");
            fprintf(stderr, "    wsys=%d wave=%d scene=%d prog=%d %s  x%d  (%s)\n",
                    miss.system, miss.id, miss.scene, miss.program,
                    miss.percussion ? "PERCUSSION" : "melodic", miss.count, where);
        }
    }
    fprintf(stderr,
            "[PC Audio] gains bus: stream=%.3f bgm=%.3f se=%.3f | sequence track=%.3f | streamVol=%.3f\n",
            sBusVolumes[PC_AUDIO_BUS_STREAM], sBusVolumes[PC_AUDIO_BUS_BGM],
            sBusVolumes[PC_AUDIO_BUS_SE], sBgmTrackGain, sStreamVolume / 255.0f);
}

void pc_audio_set_bus_volume(PCAudioBus bus, float volume) {
    if (!sAudioDevice || bus < 0 || bus >= PC_AUDIO_BUS_COUNT) return;
    SDL_LockAudioDevice(sAudioDevice);
    sBusVolumes[bus] = std::clamp(volume, 0.0f, 2.0f);
    SDL_UnlockAudioDevice(sAudioDevice);
}

void pc_audio_stop_bus(PCAudioBus bus) {
    if (!sAudioDevice || bus < 0 || bus >= PC_AUDIO_BUS_COUNT) return;
    SDL_LockAudioDevice(sAudioDevice);
    for (SampleVoice& voice : sSampleVoices) {
        if (!voice.active || voice.bus != bus) continue;
        voice.active = false;
        voice.samples.reset();
        voice.cursor = 0.0;
    }
    SDL_UnlockAudioDevice(sAudioDevice);
}

void pc_audio_set_stereo(bool stereo) {
    if (!sAudioDevice) {
        sStereoOutput = stereo;
        return;
    }
    SDL_LockAudioDevice(sAudioDevice);
    sStereoOutput = stereo;
    SDL_UnlockAudioDevice(sAudioDevice);
}

u32 pc_audio_get_active_voice_count(void) {
    if (!sAudioDevice) return 0;
    SDL_LockAudioDevice(sAudioDevice);
    u32 count = 0;
    for (const SampleVoice& voice : sSampleVoices) {
        if (voice.active) ++count;
    }
    SDL_UnlockAudioDevice(sAudioDevice);
    return count;
}

u32 pc_audio_get_clip_count(void) {
    return sClipCount.load(std::memory_order_relaxed);
}

void pc_audio_get_metrics(PCAudioMetrics* metrics) {
    if (!metrics) return;
    metrics->sampleRate = sAudioSpec.freq > 0 ? static_cast<u32>(sAudioSpec.freq) : 0;
    metrics->deviceBufferFrames = sAudioSpec.samples;
    metrics->callbacks = sCallbackCount.load(std::memory_order_relaxed);
    metrics->mixedFrames = sMixedFrameCount.load(std::memory_order_relaxed);
    metrics->peakActiveVoices = sPeakActiveVoices.load(std::memory_order_relaxed);
    metrics->voiceSteals = sVoiceSteals.load(std::memory_order_relaxed);
    metrics->voiceRejects = sVoiceRejects.load(std::memory_order_relaxed);
    metrics->dmaUnderruns = sDMAUnderruns.load(std::memory_order_relaxed);
    metrics->clips = sClipCount.load(std::memory_order_relaxed);
    metrics->limitedFrames = sLimitedFrames.load(std::memory_order_relaxed);
    metrics->bgmTicks = sBgmTicks.load(std::memory_order_relaxed);
    metrics->bossTicks = sBossTicks.load(std::memory_order_relaxed);
    metrics->seTicks = sSETicks.load(std::memory_order_relaxed);
    metrics->eventTicks = sEventTicks.load(std::memory_order_relaxed);
}

void pc_audio_reset_metrics(void) {
    sCallbackCount.store(0, std::memory_order_relaxed);
    sMixedFrameCount.store(0, std::memory_order_relaxed);
    sPeakActiveVoices.store(0, std::memory_order_relaxed);
    sVoiceSteals.store(0, std::memory_order_relaxed);
    sVoiceRejects.store(0, std::memory_order_relaxed);
    sDMAUnderruns.store(0, std::memory_order_relaxed);
    sClipCount.store(0, std::memory_order_relaxed);
    sLimitedFrames.store(0, std::memory_order_relaxed);
    sBgmTicks.store(0, std::memory_order_relaxed);
    sBossTicks.store(0, std::memory_order_relaxed);
    sSETicks.store(0, std::memory_order_relaxed);
    sEventTicks.store(0, std::memory_order_relaxed);
}

u32 pc_audio_wave_count(void) {
    return static_cast<u32>(std::min<size_t>(sWaveBank.waveCount(), UINT32_MAX));
}

AIDCallback pc_audio_register_dma_callback(AIDCallback callback) {
    AIDCallback old = sAIDMACallback;
    sAIDMACallback = callback;
    return old;
}

void pc_audio_start_dma(u32 start_addr, u32 length) {
    sDMABaseAddr = start_addr;
    sDMALength = length;
    sDMABytesLeft.store(length, std::memory_order_relaxed);
    sDMAActive.store(true, std::memory_order_relaxed);

    if (start_addr != 0 && length > 0) {
        const u8* src = reinterpret_cast<const u8*>(static_cast<uintptr_t>(start_addr));
        SDL_LockAudioDevice(sAudioDevice);
        // DMA buffers are signed big-endian stereo PCM. Append them so a new
        // hardware block never cuts off a block that SDL has not consumed yet.
        if (sDMAReadCursor != 0) {
            sDMAQueue.erase(sDMAQueue.begin(), sDMAQueue.begin() + sDMAReadCursor);
            sDMAReadCursor = 0;
        }
        sDMAQueue.reserve(sDMAQueue.size() + length / 2);
        for (u32 i = 0; i + 1 < length; i += 2) {
            sDMAQueue.push_back(static_cast<s16>(read_be16(src + i)));
        }
        sDMABytesLeft.store(static_cast<u32>((sDMAQueue.size() - sDMAReadCursor) * sizeof(s16)),
                            std::memory_order_relaxed);
        SDL_UnlockAudioDevice(sAudioDevice);
    }
}

void pc_audio_stop_dma(void) {
    sDMAActive.store(false, std::memory_order_relaxed);
    sDMABytesLeft.store(0, std::memory_order_relaxed);
    if (sAudioDevice != 0) {
        SDL_LockAudioDevice(sAudioDevice);
        sDMAQueue.clear();
        sDMAReadCursor = 0;
        SDL_UnlockAudioDevice(sAudioDevice);
    }
}

u32 pc_audio_get_dma_bytes_left(void) {
    return sDMABytesLeft.load(std::memory_order_relaxed);
}

void pc_audio_tick(void) {

    advance_bgm_mix();
    if (sJamPlayer.result() == PCJamResult::Ok) {
        const u64 now = SDL_GetPerformanceCounter();
        const u64 frequency = SDL_GetPerformanceFrequency();
        if (sJamLastCounter != 0 && frequency != 0) {
            const double elapsed = static_cast<double>(now - sJamLastCounter) / frequency;
            sJamTickAccumulator += elapsed * sJamPlayer.tempo()
                                 * sJamPlayer.timeBase() / 60.0;
        }
        sJamLastCounter = now;
        size_t ticks = 0;
        while (sJamTickAccumulator >= 1.0 && ticks++ < 2048) {
            sJamTickAccumulator -= 1.0;
            sBgmTicks.fetch_add(1, std::memory_order_relaxed);
            reap_finished_voices(sJamPlayer, sJamVoices);
            const PCJamResult result = sJamPlayer.tick(sJamEvents, 4096);
            for (const PCJamEvent& event : sJamEvents) {
                int& handle = sJamVoices[event.track % kPCJamTrackCount][event.voice & 7];
                if (event.type == PCJamEventType::NoteOn) {
                    if (handle >= 0) pc_audio_stop_wave(handle);
                    handle = -1;
                    if (event.volume <= 0.0001f) continue;
                    handle = pc_audio_play_note(
                        event.bank, event.program, event.key, event.velocity,
                        0, event.volume, event.pan, PC_AUDIO_BUS_BGM, 64,
                        event.pitch, event.cutoff, event.fxMix, event.dolby,
                        event.envelope);
                    if (handle >= 0) tag_bgm_voice(handle, 0, event.source);
                    if (handle < 0 && sJamUnresolvedNotes++ == 0) {
                        printf("[PC Port Warning] First unresolved JAM note: "
                               "sequence=%u bank=%u program=%u key=%u velocity=%u\n",
                               sJamSequence, event.bank, event.program,
                               event.key, event.velocity);
                    }
                } else if (event.type == PCJamEventType::NoteOff && handle >= 0) {
                    const u32 releaseFrames = event.release == 0 ? 3200
                        : static_cast<u32>(event.release) * sAudioSpec.freq / 600;
                    pc_audio_release_wave(handle, releaseFrames, event.release);
                    handle = -1;
                } else if ((event.type == PCJamEventType::VoiceUpdate
                            || event.type == PCJamEventType::GateUpdate) && handle >= 0) {
                    pc_audio_update_wave(handle, event.volume, event.pan, event.pitch,
                                         event.cutoff, event.fxMix, event.dolby);
                }
            }
            if (result != PCJamResult::Ok) {
                if (result != PCJamResult::Finished) {
                    printf("[PC Port Warning] JAM sequence %u stopped: result %u opcode 0x%02X\n",
                           sJamSequence, static_cast<unsigned>(result),
                           sJamPlayer.unsupportedOpcode());
                }
                break;
            }
        }
    }

    if (sBossJamPlayer.result() == PCJamResult::Ok) {
        const u64 now = SDL_GetPerformanceCounter();
        const u64 frequency = SDL_GetPerformanceFrequency();
        if (sBossJamLastCounter != 0 && frequency != 0) {
            const double elapsed = static_cast<double>(now - sBossJamLastCounter) / frequency;
            sBossJamTickAccumulator += elapsed * sBossJamPlayer.tempo()
                                     * sBossJamPlayer.timeBase() / 60.0;
        }
        sBossJamLastCounter = now;
        size_t ticks = 0;
        while (sBossJamTickAccumulator >= 1.0 && ticks++ < 2048) {
            sBossJamTickAccumulator -= 1.0;
            sBossTicks.fetch_add(1, std::memory_order_relaxed);
            reap_finished_voices(sBossJamPlayer, sBossJamVoices);
            const PCJamResult result = sBossJamPlayer.tick(sBossJamEvents, 4096);
            for (const PCJamEvent& event : sBossJamEvents) {
                int& handle = sBossJamVoices[event.track % kPCJamTrackCount][event.voice & 7];
                if (event.type == PCJamEventType::NoteOn) {
                    if (handle >= 0) pc_audio_stop_wave(handle);
                    handle = -1;
                    if (event.volume <= 0.0001f) continue;
                    handle = pc_audio_play_note(
                        event.bank, event.program, event.key, event.velocity,
                        0, event.volume * 0.5f, event.pan, PC_AUDIO_BUS_BGM, 72,
                        event.pitch, event.cutoff, event.fxMix, event.dolby,
                        event.envelope);
                    if (handle >= 0) tag_bgm_voice(handle, 1, event.source);
                } else if (event.type == PCJamEventType::NoteOff && handle >= 0) {
                    const u32 releaseFrames = event.release == 0 ? 3200
                        : static_cast<u32>(event.release) * sAudioSpec.freq / 600;
                    pc_audio_release_wave(handle, releaseFrames, event.release);
                    handle = -1;
                } else if ((event.type == PCJamEventType::VoiceUpdate
                            || event.type == PCJamEventType::GateUpdate) && handle >= 0) {
                    pc_audio_update_wave(handle, event.volume * 0.5f,
                                         event.pan, event.pitch, event.cutoff,
                                         event.fxMix, event.dolby);
                }
            }
            if (result != PCJamResult::Ok) {
                if (result != PCJamResult::Finished) {
                    printf("[PC Port Warning] Boss JAM sequence %u stopped: result %u opcode 0x%02X\n",
                           sBossJamSequence, static_cast<unsigned>(result),
                           sBossJamPlayer.unsupportedOpcode());
                }
                break;
            }
        }
    }

    // pikise.jam is the persistent sound-effect sequencer. Its clock must not
    // depend on a BGM being active: menus and transitions legitimately run
    // without sJamPlayer, and coupling both clocks also made effects inherit
    // the current song's tempo.
    if (sSEJamPlayer.result() == PCJamResult::Ok) {
        const u64 now = SDL_GetPerformanceCounter();
        const u64 frequency = SDL_GetPerformanceFrequency();
        if (sSEJamLastCounter != 0 && frequency != 0) {
            const double elapsed = static_cast<double>(now - sSEJamLastCounter) / frequency;
            sSEJamTickAccumulator += elapsed * sSEJamPlayer.tempo()
                                   * sSEJamPlayer.timeBase() / 60.0;
        }
        sSEJamLastCounter = now;
        size_t ticks = 0;
        while (sSEJamTickAccumulator >= 1.0 && ticks++ < 2048) {
            sSEJamTickAccumulator -= 1.0;
            sSETicks.fetch_add(1, std::memory_order_relaxed);
            reap_finished_voices(sSEJamPlayer, sSEJamVoices);
            const PCJamResult result = sSEJamPlayer.tick(sSEJamEvents, 4096);
            for (const PCJamEvent& event : sSEJamEvents) {
                int& handle = sSEJamVoices[event.track % kPCJamTrackCount][event.voice & 7];
                if (event.type == PCJamEventType::NoteOn) {
                    if (handle >= 0) pc_audio_stop_wave(handle);
                    handle = pc_audio_play_note(
                        event.bank, event.program, event.key, event.velocity,
                        0, event.volume, event.pan, PC_AUDIO_BUS_SE, 96,
                        event.pitch, event.cutoff, event.fxMix, event.dolby,
                        event.envelope);
                    if (handle >= 0) tag_jam_voice(handle, 1, event.source);
                } else if (event.type == PCJamEventType::NoteOff && handle >= 0) {
                    const u32 releaseFrames = event.release == 0 ? 0
                        : static_cast<u32>(event.release) * sAudioSpec.freq / 600;
                    pc_audio_release_wave(handle, releaseFrames, event.release);
                    handle = -1;
                } else if ((event.type == PCJamEventType::VoiceUpdate
                            || event.type == PCJamEventType::GateUpdate) && handle >= 0) {
                    pc_audio_update_wave(handle, event.volume, event.pan, event.pitch,
                                         event.cutoff, event.fxMix, event.dolby);
                }
            }
            if (result != PCJamResult::Ok) {
                // Rate-limited and off by default: hundreds of these a second
                // through stdio stalls the game badly enough to look like a
                // hang, which a diagnostic has no business causing.
                static int reports = 0;
                if (result != PCJamResult::Finished && pc_audio_stats_enabled()
                    && reports < 8 && ++reports)
                    printf("[PC Port Warning] JAM sound sequence stopped: result %u opcode 0x%02X at byte %u/%u (target %u), opcode byte 0x%02X, index=%u entry=%u\n",
                           static_cast<unsigned>(result), sSEJamPlayer.unsupportedOpcode(),
                           sSEJamPlayer.lastOpcodeAddress(), sSEJamPlayer.sequenceSize(),
                           sSEJamPlayer.failedAddress(), sSEJamPlayer.failedOpcodeByte(),
                           sSEJamPlayer.failedIndex(), sSEJamPlayer.failedTableEntry());
                // Gated with the report itself: an ungated dump here printed
                // sixteen lines per failure, hundreds of times a second.
                if (result != PCJamResult::Finished && pc_audio_stats_enabled()
                    && reports <= 8) {
                    for (u32 i = 0; i < sSEJamPlayer.traceCount(); ++i) {
                        u32 pc; u8 op; u16 reg1;
                        sSEJamPlayer.traceEntry(i, pc, op, reg1);
                        printf("[PC Port Warning]   trace pc=%u op=0x%02X reg1=%u\n", pc, op, reg1);
                    }
                    u32 available = 0;
                    const u8* bytes = sSEJamPlayer.bytesAt(sSEJamPlayer.lastOpcodeAddress(), available);
                    if (bytes) {
                        printf("[PC Port Warning]   bytes:");
                        for (u32 i = 0; i < 16 && i < available; ++i) printf(" %02X", bytes[i]);
                        printf("\n");
                    }
                }
                restart_se_jam();
                break;
            }
        }
    }

    // Positional gameplay events use the game's second persistent pikise.jam
    // instance (original handle 0x20000), with one child track per event.
    if (kEnablePositionalEventJam && sEventJamPlayer.result() == PCJamResult::Ok) {
        const u64 now = SDL_GetPerformanceCounter();
        const u64 frequency = SDL_GetPerformanceFrequency();
        if (sEventJamLastCounter != 0 && frequency != 0) {
            const double elapsed = static_cast<double>(now - sEventJamLastCounter) / frequency;
            sEventJamTickAccumulator += elapsed * sEventJamPlayer.tempo()
                                      * sEventJamPlayer.timeBase() / 60.0;
        }
        sEventJamLastCounter = now;
        size_t ticks = 0;
        while (sEventJamTickAccumulator >= 1.0 && ticks++ < 2048) {
            sEventJamTickAccumulator -= 1.0;
            sEventTicks.fetch_add(1, std::memory_order_relaxed);
            reap_finished_voices(sEventJamPlayer, sEventJamVoices);
            for (u8 event, slot; sEventJamPlayer.takeFinishedAction(event, slot); )
                if (sEventActionFinishedHook) sEventActionFinishedHook(event, slot);
            const PCJamResult result = sEventJamPlayer.tick(sEventJamEvents, 4096);
            for (const PCJamEvent& event : sEventJamEvents) {
                int& handle = sEventJamVoices[event.track % kPCJamTrackCount][event.voice & 7];
                if (event.type == PCJamEventType::NoteOn) {
                    const int displaced = handle;
                    if (handle >= 0) pc_audio_stop_wave(handle);
                    handle = pc_audio_play_note(
                        event.bank, event.program, event.key, event.velocity,
                        0, event.volume,
                        event.pan, PC_AUDIO_BUS_SE, 88,
                        event.pitch, event.cutoff, event.fxMix, event.dolby,
                        event.envelope);
                    // Last stage of a gameplay sound: the note reached the
                    // mixer, or it did not. One line per distinct note.
                    if (pc_audio_stats_enabled()) {
                        static const bool traceAll = [] {
                            const char* value = getenv("PIKMIN_AUDIO_TRACE_ALL");
                            return value != nullptr && value[0] == '1';
                        }();
                        static std::set<u32> seen;
                        const u32 key = (static_cast<u32>(event.bank) << 16)
                                      | (static_cast<u32>(event.program) << 8) | event.key;
                        // "Voice assigned" is not the same as "audible": a voice
                        // can be handed a valid sample and still mix to nothing
                        // if its step walks it off the end or its gains are
                        // zero. Print what the voice was actually given.
                        char detail[160] = "";
                        if (handle >= 0) {
                            const SampleVoice& v = sSampleVoices[handle & 0xFF];
                            std::snprintf(detail, sizeof detail,
                                          "  [paso %.5f base %.5f pistaTono %.3f  "
                                          "izq %.3f der %.3f  pcm %zu fin %zu]",
                                          v.step, v.baseStep, v.trackPitch, v.left, v.right,
                                          v.samples ? v.samples->size() : 0u, v.endSample);
                        }
                        if (traceAll || seen.insert(key).second)
                            printf("[PC Audio]   nota de evento: banco %u programa %u tecla %u "
                                   "vel %u vol %.3f pista %u voz %u -> %s%s%s\n",
                                   event.bank, event.program, event.key, event.velocity,
                                   event.volume, event.track, event.voice,
                                   handle >= 0 ? "voz asignada" : "SIN VOZ",
                                   displaced >= 0 ? " (desplaza una voz viva)" : "",
                                   detail);
                    }
                    if (handle >= 0) tag_event_voice(handle, event.source);
                    if (handle >= 0) tag_jam_voice(handle, 2, event.source);
                } else if (event.type == PCJamEventType::NoteOff && handle >= 0) {
                    const u32 releaseFrames = event.release == 0 ? 0
                        : static_cast<u32>(event.release) * sAudioSpec.freq / 600;
                    pc_audio_release_wave(handle, releaseFrames, event.release);
                    handle = -1;
                } else if ((event.type == PCJamEventType::VoiceUpdate
                            || event.type == PCJamEventType::GateUpdate) && handle >= 0) {
                    pc_audio_update_wave(handle, event.volume, event.pan, event.pitch,
                                         event.cutoff, event.fxMix, event.dolby);
                }
            }
            if (result != PCJamResult::Ok) {
                // Rate-limited and off by default: hundreds of these a second
                // through stdio stalls the game badly enough to look like a
                // hang, which a diagnostic has no business causing.
                static int reports = 0;
                if (result != PCJamResult::Finished && pc_audio_stats_enabled()
                    && reports < 8 && ++reports)
                    printf("[PC Port Warning] JAM event sequence stopped: result %u opcode 0x%02X at byte %u/%u (target %u), opcode byte 0x%02X, index=%u entry=%u\n",
                           static_cast<unsigned>(result), sEventJamPlayer.unsupportedOpcode(),
                           sEventJamPlayer.lastOpcodeAddress(), sEventJamPlayer.sequenceSize(),
                           sEventJamPlayer.failedAddress(), sEventJamPlayer.failedOpcodeByte(),
                           sEventJamPlayer.failedIndex(), sEventJamPlayer.failedTableEntry());
                {
                    u32 available = 0;
                    const u8* bytes = sEventJamPlayer.bytesAt(sEventJamPlayer.lastOpcodeAddress(), available);
                    if (bytes) {
                        printf("[PC Port Warning]   bytes:");
                        for (u32 i = 0; i < 16 && i < available; ++i) printf(" %02X", bytes[i]);
                        printf("\n");
                    }
                }
                restart_event_jam();
                break;
            }
        }
    }

    if (!sDMAActive.load(std::memory_order_relaxed)) return;

    // Request the next GameCube AI block before the software queue underruns.
    if (sDMABytesLeft.load(std::memory_order_relaxed) < 4096 && sAIDMACallback != nullptr) {
        sAIDMACallback();
    }
}
