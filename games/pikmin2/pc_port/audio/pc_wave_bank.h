#ifndef PC_WAVE_BANK_H
#define PC_WAVE_BANK_H

#include "types.h"

#include <string>
#include <vector>

struct PCWaveInfo {
    std::string archivePath;
    u8 format = 0;
    u8 key = 60;
    float sampleRate = 32000.0f;
    u32 dataOffset = 0;
    u32 dataLength = 0;
    bool looping = false;
    u32 loopDataOffset = 0;
    u32 loopSample = 0;
    u32 loopEndSample = 0;
    u32 sampleCount = 0;
    s16 loopHistory1 = 0;
    s16 loopHistory2 = 0;
};

class PCWaveBank {
public:
    bool load(const char* bxPath);
    const PCWaveInfo* wave(u32 waveSystem, u32 archive, u32 waveIndex) const;
    const PCWaveInfo* waveById(u32 virtualWaveSystem, u16 waveId, u32 scene = 0) const;
    const PCWaveInfo* waveByIdPhysical(u32 waveSystem, u16 waveId, u32 scene = 0) const;
    const PCWaveInfo* waveByIdAnyScene(u32 waveSystem, u16 waveId) const;
    int physicalWaveSystem(u32 virtualWaveSystem) const;
    size_t waveSystemCount() const { return mSystems.size(); }
    size_t waveCount() const;
    const std::string& error() const { return mError; }

private:
    struct WaveRef {
        u16 id = 0;
        u16 sourceScene = 0;
        u32 archive = 0;
        u32 wave = 0;
        bool external = false;
    };
    struct Scene {
        std::vector<WaveRef> direct;
        std::vector<WaveRef> external;
        std::vector<u32> dependencies;
    };
    using Archive = std::vector<PCWaveInfo>;
    struct WaveSystem {
        bool present = false;
        u32 virtualId = 0xFFFFFFFF;
        std::vector<Archive> archives;
        std::vector<Scene> scenes;
    };
    std::vector<WaveSystem> mSystems;
    std::vector<int> mVirtualToPhysical;
    std::string mBankDirectory;
    std::string mError;
    const PCWaveInfo* resolveInScene(u32 system, u16 waveId, u32 scene,
                                     size_t depth) const;
};

bool pc_decode_wave(const PCWaveInfo& wave, std::vector<s16>& pcm);

#endif
