// Real software-DSP controls: looped PCM/AFC, fractional pitch, frame boundaries.
#include "pc_aram.h"
#include "pc_dsp_host.h"
#include "jaudio/dspinterface.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static thread_local bool hostAlloc;
bool pc_host_alloc_active() { return hostAlloc; }
void pc_host_alloc_set(bool active) { hostAlloc = active; }

static DSPchannel_ voice(unsigned type, unsigned ratio) {
    DSPchannel_ c = {};
    c.enabled = DSP_TRUE;
    c.samplesSourceType = type;
    c.baseAddress = 0x10000;
    c.remainingLength = 1024;
    c.loopAddress = 128;
    c.isLooping = DSP_TRUE;
    c.resamplingRatio = ratio;
    c.mixChannels[0].id = 0x0d00;
    c.mixChannels[1].id = 0x0d60;
    for (unsigned i = 0; i < 2; ++i)
        c.mixChannels[i].currentVolume = c.mixChannels[i].targetVolume = 0x7fff;
    return c;
}

int main() {
    if (!pc_aram_init()) return 1;
    pc_dsp_host_init();
    pc_dsp_host_set_pitch_unity(0x1000); // Actual P2 ratio scale.
    const unsigned count = 560 * 120;
    std::vector<s16> full(count * 2), split(count * 2);
    // Predictor history is nonzero, exercising sequential AFC decoding and
    // restoration of the engine-provided history on every loop.
    u16 coefficients[32] = {};
    coefficients[2] = 1536;
    coefficients[3] = 256;
    for (unsigned type : {16u, 9u}) {
        auto* bytes = pc_aram_write(0x10000, 2048);
        if (!bytes) return 2;
        if (type == 16) {
            for (unsigned i = 0; i < 1024; ++i) {
                const s16 value = static_cast<s16>((i % 128) * 128 - 8192);
                bytes[i * 2] = static_cast<u16>(value) >> 8;
                bytes[i * 2 + 1] = static_cast<u16>(value);
            }
            pc_dsp_host_set_tables(nullptr, nullptr);
        } else {
            for (unsigned block = 0; block < 64; ++block) {
                bytes[block * 9] = 0x91;
                for (unsigned i = 1; i < 9; ++i)
                    bytes[block * 9 + i] = block % 2 ? 0x12 : 0xef;
            }
            pc_dsp_host_set_tables(nullptr, reinterpret_cast<u32*>(coefficients));
        }
        for (unsigned ratio : {0x800u, 0x1000u, 0x1800u, 0x2000u}) {
            pc_dsp_host_shutdown();
            pc_dsp_host_init();
            DSPchannel_ c = voice(type, ratio);
            c.loopYN1 = 1000;
            c.loopYN2 = 800;
            pc_dsp_host_render_frame(&c, 1, full.data(), count);
            if (c.done || c.currentPosition < 128 || c.currentPosition >= 1024) return 3;
            pc_dsp_host_shutdown();
            pc_dsp_host_init();
            DSPchannel_ chunk = voice(type, ratio);
            chunk.loopYN1 = 1000;
            chunk.loopYN2 = 800;
            for (unsigned at = 0; at < count; at += 560)
                pc_dsp_host_render_frame(&chunk, 1, split.data() + at * 2, 560);
            if (full != split || c.currentPosition != chunk.currentPosition
                || c.currentPosFrac != chunk.currentPosFrac) {
                std::printf("FAIL loop/frame continuity type=%u ratio=%x\n", type, ratio);
                return 4;
            }
            if (std::all_of(split.begin(), split.end(), [](s16 s) { return s == 0; })) return 5;
            std::printf("PASS sustained type=%u ratio=%x samples=%u\n", type, ratio, count);
        }
    }
    pc_dsp_host_shutdown();
    pc_aram_shutdown();
}
