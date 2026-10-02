#ifndef PC_INSTRUMENT_BANK_H
#define PC_INSTRUMENT_BANK_H

#include "types.h"

#include <string>
#include <vector>

struct PCEnvelopePoint {
    s16 curve = 0;
    s16 time = 0;
    s16 value = 0;
};

struct PCInstrumentOscillator {
    u8 mode = 0;
    float rate = 1.0f;
    float width = 1.0f;
    float vertex = 0.0f;
    std::vector<PCEnvelopePoint> attack;
    std::vector<PCEnvelopePoint> release;
};

struct PCInstrumentEffect {
    u8 id = 0;
    u8 type = 0;
    u8 threshold = 0;
    float value = 1.0f;
    float range = 0.0f;
    float maximum = 1.0f;
    bool sensor = false;
};

struct PCInstrumentRegion {
    // Which key region this came from. Regions are stored flat, but the
    // original commits to one key region and then searches only that region's
    // velocity list: falling through to the next key region on a velocity miss
    // plays a sample from the wrong part of the keyboard.
    u8 keyGroup = 0;
    u8 maximumKey = 127;
    u8 maximumVelocity = 127;
    s16 waveSystem = -1;
    s16 waveId = -1;
    float volume = 1.0f;
    float pitch = 1.0f;
    u16 release = 0;
};

struct PCInstrument {
    bool percussion = false;
    float pitch = 1.0f;
    float volume = 1.0f;
    std::vector<PCInstrumentOscillator> oscillators;
    std::vector<PCInstrumentEffect> effects;
    std::vector<PCInstrumentRegion> regions;
};

struct PCInstrumentSelection {
    u32 physicalBank = 0;
    // Percussion regions are parsed with the melodic layout; if that layout is
    // wrong for PER2 the wave system read out of it will be wrong too, so a
    // failed lookup needs to say which kind of instrument it came from.
    bool percussion = false;
    u32 program = 0;
    PCInstrumentRegion region;
    float instrumentPitch = 1.0f;
    float instrumentVolume = 1.0f;
    // Instruments are immutable after the bank is loaded. Selections are
    // short-lived views so NoteON does not copy nested envelope/effect vectors.
    const std::vector<PCInstrumentOscillator>* oscillators = nullptr;
    const std::vector<PCInstrumentEffect>* effects = nullptr;
};

class PCInstrumentBank {
public:
    bool load(const char* bxPath);
    const PCInstrument* instrument(u32 physicalBank, u32 program) const;
    const PCInstrumentSelection* select(u32 virtualBank, u32 program, u8 key,
                                        u8 velocity, PCInstrumentSelection& result) const;
    int physicalBank(u32 virtualBank) const;
    size_t bankSlotCount() const { return mBanks.size(); }
    size_t bankCount() const;
    size_t instrumentCount() const;
    size_t regionCount() const;
    size_t oscillatorCount() const;
    size_t unsupportedPercussionCount() const { return mUnsupportedPercussion; }
    const std::string& error() const { return mError; }

private:
    struct Bank {
        bool present = false;
        u32 virtualId = 0xFFFFFFFF;
        std::vector<PCInstrument> programs;
    };
    std::vector<Bank> mBanks;
    std::vector<int> mVirtualToPhysical;
    size_t mUnsupportedPercussion = 0;
    std::string mError;
};

#endif
