#include "audio/pc_instrument_bank.h"

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
bool sane(float value) { return std::isfinite(value) && std::abs(value) <= 64.0f; }
constexpr u32 kIBNK = 0x49424E4B;
constexpr u32 kBANK = 0x42414E4B;
constexpr u32 kINST = 0x494E5354;
constexpr u32 kPER2 = 0x50455232;
constexpr u32 kPERC = 0x50455243;
}

bool PCInstrumentBank::load(const char* bxPath) {
    mBanks.clear();
    mVirtualToPhysical.assign(256, -1);
    mUnsupportedPercussion = 0;
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

    const u32 tableOffset = be32(file.data() + 8);
    const u32 bankSlots = be32(file.data() + 12);
    if (bankSlots > 256 || !range(tableOffset, static_cast<size_t>(bankSlots) * 8, file.size())) {
        mError = "invalid IBNK table";
        return false;
    }
    mBanks.resize(bankSlots);
    for (u32 bankIndex = 0; bankIndex < bankSlots; ++bankIndex) {
        const u8* pair = file.data() + tableOffset + bankIndex * 8;
        const u32 base = be32(pair);
        const u32 size = be32(pair + 4);
        if (!size) continue;
        if (size < 0x3E4 || !range(base, size, file.size())
            || be32(file.data() + base) != kIBNK
            || be32(file.data() + base + 0x20) != kBANK) {
            mError = "invalid IBNK block " + std::to_string(bankIndex);
            return false;
        }
        Bank& bank = mBanks[bankIndex];
        bank.present = true;
        bank.virtualId = be32(file.data() + base + 8);
        if (bank.virtualId < mVirtualToPhysical.size()
            && mVirtualToPhysical[bank.virtualId] == -1) {
            mVirtualToPhysical[bank.virtualId] = static_cast<int>(bankIndex);
        }
        bank.programs.resize(240);

        for (u32 program = 0; program < 240; ++program) {
            const u32 objectOffset = be32(file.data() + base + 0x24 + program * 4);
            if (!objectOffset) continue;
            if (!range(objectOffset, 4, size)) {
                mError = "IBNK object outside block " + std::to_string(bankIndex);
                return false;
            }
            const size_t object = static_cast<size_t>(base) + objectOffset;
            const u32 magic = be32(file.data() + object);
            PCInstrument& dest = bank.programs[program];
            if (magic == kINST) {
                if (!range(objectOffset, 0x2C, size)) {
                    mError = "truncated INST";
                    return false;
                }
                // oneshot.c applies offset 0x08 to volume and 0x0C to pitch;
                // the historical bx.h member names describe them the other way.
                dest.volume = bef32(file.data() + object + 8);
                dest.pitch = bef32(file.data() + object + 12);
                const auto parseCurve = [&](u32 curveOffset,
                                            std::vector<PCEnvelopePoint>& curve) -> bool {
                    if (!curveOffset) return true;
                    // Curves are triples of signed 16-bit values. Commands E/F
                    // terminate playback; D jumps to another table index.
                    for (size_t point = 0; point < 256; ++point) {
                        const size_t offset = static_cast<size_t>(curveOffset) + point * 6;
                        if (!range(offset, 6, size)) return false;
                        const u8* src = file.data() + base + offset;
                        PCEnvelopePoint item { bes16(src), bes16(src + 2), bes16(src + 4) };
                        curve.push_back(item);
                        if (item.curve == 0x0E || item.curve == 0x0F) return true;
                    }
                    return false;
                };
                for (u32 oscillatorIndex = 0; oscillatorIndex < 2; ++oscillatorIndex) {
                    const u32 oscillatorOffset = be32(
                        file.data() + object + 0x10 + oscillatorIndex * 4);
                    if (!oscillatorOffset) continue;
                    if (!range(oscillatorOffset, 0x18, size)) {
                        mError = "invalid INST oscillator";
                        return false;
                    }
                    const u8* oscillator = file.data() + base + oscillatorOffset;
                    PCInstrumentOscillator parsed;
                    parsed.mode = oscillator[0];
                    parsed.rate = bef32(oscillator + 4);
                    parsed.width = bef32(oscillator + 0x10);
                    parsed.vertex = bef32(oscillator + 0x14);
                    const u32 attackOffset = be32(oscillator + 8);
                    const u32 releaseOffset = be32(oscillator + 12);
                    if (!sane(parsed.rate) || !sane(parsed.width) || !sane(parsed.vertex)
                        || !parseCurve(attackOffset, parsed.attack)
                        || (releaseOffset != attackOffset
                            && !parseCurve(releaseOffset, parsed.release))) {
                        mError = "invalid INST oscillator curve";
                        return false;
                    }
                    if (releaseOffset == attackOffset) parsed.release = parsed.attack;
                    dest.oscillators.push_back(std::move(parsed));
                }
                for (u32 effectIndex = 0; effectIndex < 2; ++effectIndex) {
                    const u32 randomOffset = be32(
                        file.data() + object + 0x18 + effectIndex * 4);
                    if (randomOffset) {
                        if (!range(randomOffset, 0x10, size)) {
                            mError = "invalid INST random effect";
                            return false;
                        }
                        const u8* src = file.data() + base + randomOffset;
                        PCInstrumentEffect effect;
                        effect.id = src[0];
                        effect.value = bef32(src + 4);
                        effect.range = bef32(src + 8);
                        if (effect.id > 4 || !sane(effect.value) || !sane(effect.range)) {
                            mError = "invalid INST random effect parameters";
                            return false;
                        }
                        dest.effects.push_back(effect);
                    }
                    const u32 sensorOffset = be32(
                        file.data() + object + 0x20 + effectIndex * 4);
                    if (sensorOffset) {
                        if (!range(sensorOffset, 0x0C, size)) {
                            mError = "invalid INST sensor effect";
                            return false;
                        }
                        const u8* src = file.data() + base + sensorOffset;
                        PCInstrumentEffect effect;
                        effect.id = src[0];
                        effect.type = src[1];
                        effect.threshold = src[2];
                        effect.value = bef32(src + 4);
                        effect.maximum = bef32(src + 8);
                        effect.sensor = true;
                        if (effect.id > 4 || effect.type > 2
                            || !sane(effect.value) || !sane(effect.maximum)) {
                            mError = "invalid INST sensor effect parameters";
                            return false;
                        }
                        dest.effects.push_back(effect);
                    }
                }
                const u32 keyCount = be32(file.data() + object + 0x28);
                if (!sane(dest.pitch) || !sane(dest.volume) || keyCount > 128
                    || !range(objectOffset + 0x2C, static_cast<size_t>(keyCount) * 4, size)) {
                    mError = "invalid INST metadata";
                    return false;
                }
                for (u32 keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
                    const u32 keyOffset = be32(file.data() + object + 0x2C + keyIndex * 4);
                    if (!range(keyOffset, 8, size)) {
                        mError = "invalid INST key region";
                        return false;
                    }
                    const size_t keymap = static_cast<size_t>(base) + keyOffset;
                    const u8 maximumKey = file[keymap];
                    const u32 velocityCount = be32(file.data() + keymap + 4);
                    if (velocityCount > 128
                        || !range(keyOffset + 8, static_cast<size_t>(velocityCount) * 4, size)) {
                        mError = "invalid INST velocity table";
                        return false;
                    }
                    for (u32 velocityIndex = 0; velocityIndex < velocityCount; ++velocityIndex) {
                        const u32 velocityOffset = be32(file.data() + keymap + 8 + velocityIndex * 4);
                        if (!range(velocityOffset, 0x10, size)) {
                            mError = "invalid INST velocity region";
                            return false;
                        }
                        const u8* vmap = file.data() + base + velocityOffset;
                        PCInstrumentRegion region;
                        region.keyGroup = static_cast<u8>(keyIndex);
                        region.maximumKey = maximumKey;
                        region.maximumVelocity = vmap[0];
                        region.waveSystem = bes16(vmap + 4);
                        region.waveId = bes16(vmap + 6);
                        region.volume = bef32(vmap + 8);
                        region.pitch = bef32(vmap + 12);
                        if (!sane(region.volume) || !sane(region.pitch)) {
                            mError = "invalid INST region parameters";
                            return false;
                        }
                        dest.regions.push_back(region);
                    }
                }
            } else if (magic == kPER2 || magic == kPERC) {
                // Both tags use Perc_ in the original Bank_Test path. PERC is
                // the older tag, but its key table starts at the same 0x88
                // offset and uses the same PercKeymap_/Vmap_ structures.
                dest.percussion = true;
                PCInstrumentOscillator envelope;
                envelope.mode = 0;
                envelope.attack.push_back({ 0x0E, 0, 32767 });
                dest.oscillators.push_back(std::move(envelope));
                if (!range(objectOffset, 0x408, size)) {
                    mError = "truncated PER2";
                    return false;
                }
                for (u32 key = 0; key < 128; ++key) {
                    const u32 keyOffset = be32(file.data() + object + 0x88 + key * 4);
                    if (!keyOffset) continue;
                    if (!range(keyOffset, 0x14, size)) {
                        mError = "invalid PER2 key region";
                        return false;
                    }
                    const size_t keymap = static_cast<size_t>(base) + keyOffset;
                    const float keyPitch = bef32(file.data() + keymap);
                    const float keyVolume = bef32(file.data() + keymap + 4);
                    const u32 velocityCount = be32(file.data() + keymap + 0x10);
                    if (!sane(keyPitch) || !sane(keyVolume) || velocityCount > 128
                        || !range(keyOffset + 0x14, static_cast<size_t>(velocityCount) * 4, size)) {
                        mError = "invalid PER2 velocity table";
                        return false;
                    }
                    for (u32 velocityIndex = 0; velocityIndex < velocityCount; ++velocityIndex) {
                        const u32 velocityOffset = be32(file.data() + keymap + 0x14 + velocityIndex * 4);
                        if (!range(velocityOffset, 0x10, size)) {
                            mError = "invalid PER2 velocity region";
                            return false;
                        }
                        const u8* vmap = file.data() + base + velocityOffset;
                        PCInstrumentRegion region;
                        region.keyGroup = static_cast<u8>(key);
                        region.maximumKey = static_cast<u8>(key);
                        region.maximumVelocity = vmap[0];
                        region.waveSystem = bes16(vmap + 4);
                        region.waveId = bes16(vmap + 6);
                        // Play_1shot_Perc applies the key map's first float to
                        // volume and its second float to pitch despite the
                        // misleading historical member names in bx.h.
                        region.volume = bef32(vmap + 8) * keyPitch;
                        region.pitch = bef32(vmap + 12) * keyVolume;
                        region.release = magic == kPER2
                            ? be16(file.data() + object + 0x308 + key * 2) : 1000;
                        if (!sane(region.volume) || !sane(region.pitch)) {
                            mError = "invalid PER2 region parameters";
                            return false;
                        }
                        dest.regions.push_back(region);
                    }
                }
            } else {
                mError = "unsupported IBNK object magic";
                return false;
            }
        }
    }
    return true;
}

const PCInstrument* PCInstrumentBank::instrument(u32 bank, u32 program) const {
    if (bank >= mBanks.size() || !mBanks[bank].present
        || program >= mBanks[bank].programs.size()) return nullptr;
    const PCInstrument& result = mBanks[bank].programs[program];
    return result.regions.empty() ? nullptr : &result;
}

int PCInstrumentBank::physicalBank(u32 virtualBank) const {
    return virtualBank < mVirtualToPhysical.size() ? mVirtualToPhysical[virtualBank] : -1;
}

const PCInstrumentSelection* PCInstrumentBank::select(
    u32 virtualBank, u32 program, u8 key, u8 velocity, PCInstrumentSelection& result) const {
    const int bank = physicalBank(virtualBank);
    if (bank < 0) return nullptr;
    const PCInstrument* inst = instrument(static_cast<u32>(bank), program);
    if (!inst) return nullptr;
    // Bank_GetInstVmap: pick the first key region that covers the key, then
    // search only that region's velocity list. A velocity miss means silence,
    // not the next key region's sample.
    const PCInstrumentRegion* selected = nullptr;
    for (size_t i = 0; i < inst->regions.size(); ++i) {
        const PCInstrumentRegion& start = inst->regions[i];
        const bool keyMatches = inst->percussion
            ? key == start.maximumKey : key <= start.maximumKey;
        if (!keyMatches) continue;
        for (size_t j = i; j < inst->regions.size()
                           && inst->regions[j].keyGroup == start.keyGroup; ++j) {
            if (velocity <= inst->regions[j].maximumVelocity) {
                selected = &inst->regions[j];
                break;
            }
        }
        break;
    }
    if (!selected) return nullptr;
    result.physicalBank = static_cast<u32>(bank);
    result.percussion = inst->percussion;
    result.program = program;
    result.region = *selected;
    result.instrumentPitch = inst->pitch;
    result.instrumentVolume = inst->volume;
    result.oscillators = &inst->oscillators;
    result.effects = &inst->effects;
    return &result;
}

size_t PCInstrumentBank::bankCount() const {
    size_t result = 0;
    for (const Bank& bank : mBanks) if (bank.present) ++result;
    return result;
}

size_t PCInstrumentBank::instrumentCount() const {
    size_t result = 0;
    for (const Bank& bank : mBanks)
        for (const PCInstrument& instrument : bank.programs)
            if (!instrument.regions.empty()) ++result;
    return result;
}

size_t PCInstrumentBank::regionCount() const {
    size_t result = 0;
    for (const Bank& bank : mBanks)
        for (const PCInstrument& instrument : bank.programs)
            result += instrument.regions.size();
    return result;
}

size_t PCInstrumentBank::oscillatorCount() const {
    size_t result = 0;
    for (const Bank& bank : mBanks)
        for (const PCInstrument& instrument : bank.programs)
            result += instrument.oscillators.size();
    return result;
}
