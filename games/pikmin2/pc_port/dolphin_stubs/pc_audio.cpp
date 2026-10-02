#include "pc_audio.h"
#include <SDL2/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
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
static u8 sDMAVolume = 255;

static u16 read_be16(const u8* data) {
    return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
}

static u32 read_be32(const u8* data) {
    return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16)
         | (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

static s16 clamp_s16(int value) {
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
    const size_t sampleCount = static_cast<size_t>(byteCount) / sizeof(s16);
    for (size_t i = 0; i < sampleCount; ++i) {
        int mixed = 0;
        if (sStreamVoice.active && sStreamVoice.cursor < sStreamVoice.samples.size()) {
            mixed += (static_cast<int>(sStreamVoice.samples[sStreamVoice.cursor++]) * sStreamVolume) / 255;
            if (sStreamVoice.cursor == sStreamVoice.samples.size()) {
                sStreamVoice.active = false;
            }
        }
        if (sDMAActive && sDMAReadCursor < sDMAQueue.size()) {
            mixed += (static_cast<int>(sDMAQueue[sDMAReadCursor++]) * sDMAVolume) / 255;
        }
        dst[i] = clamp_s16(mixed);
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
        SDL_CloseAudioDevice(sAudioDevice);
        sAudioDevice = 0;
    }
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
        SDL_UnlockAudioDevice(sAudioDevice);
    }
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
    if (!sDMAActive.load(std::memory_order_relaxed)) return;

    // Request the next GameCube AI block before the software queue underruns.
    if (sDMABytesLeft.load(std::memory_order_relaxed) < 4096 && sAIDMACallback != nullptr) {
        sAIDMACallback();
    }
}
