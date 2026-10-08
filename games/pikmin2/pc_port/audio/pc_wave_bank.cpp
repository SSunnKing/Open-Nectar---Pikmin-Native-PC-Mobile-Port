#include <cstdlib>
#include "audio/pc_wave_bank.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace {
u16 be16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
u32 be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16)
         | (static_cast<u32>(p[2]) << 8) | p[3];
}
s16 bes16(const u8* p) { return static_cast<s16>(be16(p)); }
float bef32(const u8* p) {
    const u32 bits = be32(p);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
bool range(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
std::string parent(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}
std::string fixedString(const u8* data, size_t length) {
    size_t count = 0;
    while (count < length && data[count]) ++count;
    return std::string(reinterpret_cast<const char*>(data), count);
}
s16 clamp16(int value) {
    return static_cast<s16>(std::clamp(value, -32768, 32767));
}
}

bool PCWaveBank::load(const char* bxPath) {
    mSystems.clear();
    mVirtualToPhysical.assign(256, -1);
    mError.clear();
    if (!bxPath) {
        mError = "null BX path";
        return false;
    }
    std::ifstream input(bxPath, std::ios::binary | std::ios::ate);
    if (!input) {
        mError = "could not open BX";
        return false;
    }
    const std::streamsize fileSize = input.tellg();
    if (fileSize < 16) {
        mError = "BX header is truncated";
        return false;
    }
    input.seekg(0);
    std::vector<u8> file(static_cast<size_t>(fileSize));
    if (!input.read(reinterpret_cast<char*>(file.data()), fileSize)) {
        mError = "could not read BX";
        return false;
    }

    const u32 wsysTable = be32(file.data());
    const u32 wsysCount = be32(file.data() + 4);
    if (wsysCount > 256 || !range(wsysTable, static_cast<size_t>(wsysCount) * 8, file.size())) {
        mError = "invalid WSYS table";
        return false;
    }
    mBankDirectory = parent(bxPath);
    mSystems.resize(wsysCount);

    for (u32 systemIndex = 0; systemIndex < wsysCount; ++systemIndex) {
        const u8* pair = file.data() + wsysTable + systemIndex * 8;
        const u32 base = be32(pair);
        const u32 size = be32(pair + 4);
        if (!size) continue;
        if (!range(base, size, file.size()) || size < 24 || be32(file.data() + base) != 0x57535953) {
            mError = "invalid WSYS block " + std::to_string(systemIndex);
            return false;
        }
        const u32 winfOffset = be32(file.data() + base + 16);
        if (!range(winfOffset, 8, size)) {
            mError = "invalid WINF offset";
            return false;
        }
        const size_t winf = static_cast<size_t>(base) + winfOffset;
        if (be32(file.data() + winf) != 0x57494e46) {
            mError = "missing WINF";
            return false;
        }
        const u32 archiveCount = be32(file.data() + winf + 4);
        if (archiveCount > 256 || !range(winf + 8, static_cast<size_t>(archiveCount) * 4, file.size())) {
            mError = "invalid WINF archive table";
            return false;
        }
        auto& system = mSystems[systemIndex];
        system.present = true;
        system.virtualId = be32(file.data() + base + 8);
        if (getenv("PIKMIN_AUDIO_STATS")) {
            // Which physical system carries which id, and whether two share
            // one: the mapping below keeps only the first claimant, so a
            // duplicate id would make the later system unreachable by lookup.
            fprintf(stderr, "[PC Audio] wsys physical %u has virtual id %u%s\n",
                    systemIndex, system.virtualId,
                    (system.virtualId < mVirtualToPhysical.size()
                     && mVirtualToPhysical[system.virtualId] != -1)
                        ? "   <-- DUPLICATE, unreachable" : "");
        }
        if (system.virtualId < mVirtualToPhysical.size()
            && mVirtualToPhysical[system.virtualId] == -1) {
            mVirtualToPhysical[system.virtualId] = static_cast<int>(systemIndex);
        }
        system.archives.resize(archiveCount);
        for (u32 archiveIndex = 0; archiveIndex < archiveCount; ++archiveIndex) {
            const u32 archiveOffset = be32(file.data() + winf + 8 + archiveIndex * 4);
            if (!range(archiveOffset, 0x74, size)) {
                mError = "invalid wave archive offset";
                return false;
            }
            const size_t archivePos = static_cast<size_t>(base) + archiveOffset;
            const std::string filename = fixedString(file.data() + archivePos, 0x40);
            const u32 count = be32(file.data() + archivePos + 0x70);
            // Some WSYS files contain an explicit empty slot (notably demo_0).
            if (filename.empty() && count == 0) continue;
            if (filename.empty() || count > 65536
                || !range(archivePos + 0x74, static_cast<size_t>(count) * 4, file.size())) {
                mError = "invalid wave archive metadata at WSYS "
                    + std::to_string(systemIndex) + ", archive " + std::to_string(archiveIndex);
                return false;
            }
            auto& archive = system.archives[archiveIndex];
            archive.reserve(count);
            for (u32 waveIndex = 0; waveIndex < count; ++waveIndex) {
                const u32 waveOffset = be32(file.data() + archivePos + 0x74 + waveIndex * 4);
                if (!range(waveOffset, 0x24, size)) {
                    mError = "invalid wave metadata offset";
                    return false;
                }
                const u8* src = file.data() + base + waveOffset;
                PCWaveInfo info;
                info.archivePath = mBankDirectory + "/" + filename;
                info.format = src[1];
                info.key = src[2];
                info.sampleRate = bef32(src + 4);
                info.dataOffset = be32(src + 8);
                info.dataLength = be32(src + 12);
                info.looping = be32(src + 16) != 0;
                info.loopDataOffset = be32(src + 20);
                info.loopEndSample = be32(src + 24);
                // Wave_ +0x14 is the decoded loop sample address.  The field
                // at +0x18 is the playback end position (DSP_SetWaveInfo also
                // uses it as the end for non-looping waves), despite the old
                // bx.h name `loopStartPosition`.  Treating +0x18 as the loop
                // point reduced sustained instruments to 65-118 samples and
                // turned them into the long periodic beeps heard in menus.
                info.loopSample = info.loopDataOffset;
                info.sampleCount = be32(src + 28);
                info.loopHistory1 = bes16(src + 32);
                info.loopHistory2 = bes16(src + 34);
                if (!std::isfinite(info.sampleRate) || info.sampleRate < 1000.0f
                    || info.sampleRate > 192000.0f || info.sampleCount > 100000000) {
                    mError = "invalid wave format metadata";
                    return false;
                }
                if (!info.looping || info.loopEndSample <= info.loopSample
                    || info.loopEndSample > info.sampleCount) {
                    info.loopEndSample = info.sampleCount;
                }
                archive.push_back(std::move(info));
            }
        }

        const u32 wbctOffset = be32(file.data() + base + 20);
        if (!range(wbctOffset, 12, size)) {
            mError = "invalid WBCT offset";
            return false;
        }
        const size_t wbct = static_cast<size_t>(base) + wbctOffset;
        if (be32(file.data() + wbct) != 0x57424354) {
            mError = "missing WBCT";
            return false;
        }
        const u32 sceneCount = be32(file.data() + wbct + 8);
        if (sceneCount != archiveCount || sceneCount > 256
            || !range(wbct + 12, static_cast<size_t>(sceneCount) * 4, file.size())) {
            mError = "invalid WBCT scene table";
            return false;
        }
        system.scenes.resize(sceneCount);
        for (u32 sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
            const u32 sceneOffset = be32(file.data() + wbct + 12 + sceneIndex * 4);
            if (!range(sceneOffset, 24, size)) {
                mError = "invalid SCNE offset";
                return false;
            }
            const size_t scenePos = static_cast<size_t>(base) + sceneOffset;
            if (be32(file.data() + scenePos) != 0x53434E45) {
                mError = "missing SCNE";
                return false;
            }
            const u32 externalMode = be32(file.data() + scenePos + 4);
            const u32 dependencyCount = be32(file.data() + scenePos + 8);
            if (dependencyCount > sceneCount
                || !range(sceneOffset + 24, static_cast<size_t>(dependencyCount) * 4, size)) {
                mError = "invalid SCNE dependencies";
                return false;
            }
            Scene& scene = system.scenes[sceneIndex];
            for (u32 i = 0; i < dependencyCount; ++i) {
                const u32 dependency = be32(file.data() + scenePos + 24 + i * 4);
                if (dependency >= sceneCount) {
                    mError = "SCNE dependency outside group";
                    return false;
                }
                scene.dependencies.push_back(dependency);
            }

            const auto parseControl = [&](u32 controlOffset, u32 expectedMagic,
                                          bool external, u32 firstWave,
                                          std::vector<WaveRef>& output) -> bool {
                if (!controlOffset) return true;
                if (!range(controlOffset, 8, size)) return false;
                const size_t control = static_cast<size_t>(base) + controlOffset;
                if (be32(file.data() + control) != expectedMagic) return false;
                const u32 count = be32(file.data() + control + 4);
                if (count > 65536
                    || !range(controlOffset + 8, static_cast<size_t>(count) * 4, size)) return false;
                output.reserve(count);
                for (u32 i = 0; i < count; ++i) {
                    const u32 idOffset = be32(file.data() + control + 8 + i * 4);
                    if (!range(idOffset, 4, size)) return false;
                    const u32 id = be32(file.data() + base + idOffset);
                    WaveRef ref;
                    ref.id = static_cast<u16>(id);
                    ref.sourceScene = static_cast<u16>(id >> 16);
                    ref.archive = sceneIndex;
                    ref.wave = firstWave + i;
                    ref.external = external && externalMode != 0;
                    output.push_back(ref);
                }
                return true;
            };
            const u32 cdfOffset = be32(file.data() + scenePos + 12);
            const u32 cexOffset = be32(file.data() + scenePos + 16);
            if (!parseControl(cdfOffset, 0x432D4446, false, 0, scene.direct)) {
                mError = "invalid C-DF control";
                return false;
            }
            if (!parseControl(cexOffset, 0x432D4558, true,
                              static_cast<u32>(scene.direct.size()), scene.external)) {
                mError = "invalid C-EX control";
                return false;
            }
            if (externalMode == 0
                && scene.direct.size() + scene.external.size() > system.archives[sceneIndex].size()) {
                mError = "SCNE controls exceed wave archive";
                return false;
            }
        }
    }
    return true;
}

const PCWaveInfo* PCWaveBank::wave(u32 system, u32 archive, u32 index) const {
    if (system >= mSystems.size() || archive >= mSystems[system].archives.size()
        || index >= mSystems[system].archives[archive].size()) return nullptr;
    return &mSystems[system].archives[archive][index];
}

int PCWaveBank::physicalWaveSystem(u32 virtualSystem) const {
    return virtualSystem < mVirtualToPhysical.size() ? mVirtualToPhysical[virtualSystem] : -1;
}

const PCWaveInfo* PCWaveBank::resolveInScene(
    u32 systemIndex, u16 waveId, u32 sceneIndex, size_t depth) const {
    if (systemIndex >= mSystems.size()) return nullptr;
    const WaveSystem& system = mSystems[systemIndex];
    if (sceneIndex >= system.scenes.size() || depth > system.scenes.size()) return nullptr;
    const Scene& scene = system.scenes[sceneIndex];
    for (const WaveRef& ref : scene.direct) {
        if (ref.id == waveId) return wave(systemIndex, ref.archive, ref.wave);
    }
    for (const WaveRef& ref : scene.external) {
        if (ref.id != waveId) continue;
        if (!ref.external) return wave(systemIndex, ref.archive, ref.wave);
        if (ref.sourceScene >= system.scenes.size()) return nullptr;
        const Scene& source = system.scenes[ref.sourceScene];
        for (const WaveRef& sourceRef : source.direct) {
            if (sourceRef.id == waveId)
                return wave(systemIndex, sourceRef.archive, sourceRef.wave);
        }
    }
    for (u32 dependency : scene.dependencies) {
        if (const PCWaveInfo* found = resolveInScene(
                systemIndex, waveId, dependency, depth + 1)) return found;
    }
    return nullptr;
}

const PCWaveInfo* PCWaveBank::waveById(
    u32 virtualSystem, u16 waveId, u32 scene) const {
    const int system = physicalWaveSystem(virtualSystem);
    return system < 0 ? nullptr : resolveInScene(static_cast<u32>(system), waveId, scene, 0);
}

const PCWaveInfo* PCWaveBank::waveByIdPhysical(
    u32 system, u16 waveId, u32 scene) const {
    return resolveInScene(system, waveId, scene, 0);
}

const PCWaveInfo* PCWaveBank::waveByIdAnyScene(u32 system, u16 waveId) const {
    if (system >= mSystems.size()) return nullptr;
    for (u32 scene = 0; scene < mSystems[system].scenes.size(); ++scene) {
        if (const PCWaveInfo* found = resolveInScene(system, waveId, scene, 0)) return found;
    }
    return nullptr;
}

size_t PCWaveBank::waveCount() const {
    size_t result = 0;
    for (const auto& system : mSystems)
        for (const auto& archive : system.archives) result += archive.size();
    return result;
}

bool pc_decode_wave(const PCWaveInfo& wave, std::vector<s16>& pcm) {
    pcm.clear();
    std::ifstream input(wave.archivePath, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const std::streamsize fileSize = input.tellg();
    if (fileSize < 0 || !range(wave.dataOffset, wave.dataLength, static_cast<size_t>(fileSize))) return false;
    input.seekg(wave.dataOffset);
    std::vector<u8> encoded(wave.dataLength);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), wave.dataLength)) return false;
    pcm.resize(wave.sampleCount);

    if (wave.format == 0 || wave.format == 1) {
        // Nintendo AFC: both encodings produce 16 samples per frame.  Format
        // 0 stores sixteen signed 4-bit residuals in 9 bytes; the rarer format
        // 1 stores sixteen signed 2-bit residuals in 5 bytes.  Pikmin's bank
        // contains both, so rejecting format 1 silently removed 21 original
        // waves and the instruments/layers which depend on them.
        static const s16 c0[16] = {0, 0x800, 0, 0x400, 0x1000, 0xe00, 0xc00, 0x1200,
                                  0x1068, 0x12c0, 0x1400, 0x800, 0x400, -0x400, -0x400, -0x800};
        static const s16 c1[16] = {0, 0, 0x800, 0x400, -0x800, -0x600, -0x400, -0xa00,
                                  -0x8c8, -0x8fc, -0xc00, -0x800, -0x400, 0x400, 0, 0};
        const size_t frameBytes = wave.format == 0 ? 9 : 5;
        s16 older = 0, newer = 0;
        size_t out = 0;
        for (size_t pos = 0; pos + frameBytes <= encoded.size() && out < pcm.size();
             pos += frameBytes) {
            const u8 header = encoded[pos];
            const int scale = 1 << (header >> 4);
            const int predictor = header & 15;
            for (int n = 0; n < 16 && out < pcm.size(); ++n) {
                int residual;
                if (wave.format == 0) {
                    residual = (n & 1) ? (encoded[pos + 1 + n / 2] & 15)
                                       : (encoded[pos + 1 + n / 2] >> 4);
                    if (residual >= 8) residual -= 16;
                } else {
                    const int shift = 6 - (n & 3) * 2;
                    residual = (encoded[pos + 1 + n / 4] >> shift) & 3;
                    if (residual >= 2) residual -= 4;
                }
                const s16 sample = clamp16(residual * scale
                    + ((c0[predictor] * newer + c1[predictor] * older) >> 11));
                pcm[out++] = sample;
                older = newer;
                newer = sample;
            }
        }
        pcm.resize(out);
    } else if (wave.format == 2) { // Signed PCM8.
        const size_t count = std::min(pcm.size(), encoded.size());
        for (size_t i = 0; i < count; ++i)
            pcm[i] = static_cast<s16>(static_cast<s8>(encoded[i])) * 256;
        pcm.resize(count);
    } else if (wave.format == 3) { // Big-endian signed PCM16.
        const size_t count = std::min(pcm.size(), encoded.size() / 2);
        for (size_t i = 0; i < count; ++i) pcm[i] = bes16(encoded.data() + i * 2);
        pcm.resize(count);
    } else {
        pcm.clear();
        return false;
    }
    return !pcm.empty();
}
