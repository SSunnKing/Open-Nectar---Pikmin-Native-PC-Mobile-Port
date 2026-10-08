#ifndef PC_JAM_H
#define PC_JAM_H

#include "types.h"
#include "audio/pc_instrument_bank.h"

#include <array>
#include <memory>
#include <utility>
#include <cstddef>
#include <vector>

enum class PCJamEventType : u8 { NoteOn, GateUpdate, NoteOff, VoiceUpdate };

struct PCJamEvent {
    PCJamEventType type = PCJamEventType::NoteOn;
    u8 track = 0;
    u8 voice = 0;
    u8 key = 0;
    u8 velocity = 0;
    u8 bank = 0;
    u8 program = 0;
    u8 source = 0xFF;
    u16 release = 0;
    float volume = 1.0f;
    float pan = 0.0f;
    float pitch = 1.0f;
    float fxMix = 0.0f;
    float dolby = 0.0f;
    u8 cutoff = 127;
    // Envelope the track set up with SimpleADSR. Oscillator programs (0xF0+)
    // have no bank instrument to take one from, so without this they sound at
    // full amplitude for the note's whole length.
    std::shared_ptr<const std::vector<PCInstrumentOscillator>> envelope;
};

constexpr size_t kPCJamTrackCount = 64;

enum class PCJamResult : u8 {
    Ok, Finished, Truncated, InvalidAddress, StackOverflow,
    StackUnderflow, OperationLimit, UnsupportedOpcode
};

class PCJamPlayer {
public:
    bool start(const std::vector<u8>& sequence, u8 bank = 0);
    PCJamResult tick(std::vector<PCJamEvent>& events,
                     size_t maxOperations = 1024);
    bool active() const;
    bool writeChildPort(u8 child, u8 port, u16 value);
    // The mixer tells the player when a voice has finished sounding. A note
    // whose duration field is zero waits for exactly that (CheckNoteStop in
    // the original), so without this the track never resumes and never frees
    // its event slot.
    void noteFinished(u8 track, u8 voice);
    // An event action tells the game it has finished by running SyncCPU with
    // bit 0x8000 set (TrackReceive -> MML_StopEventAction in the original).
    // Draining these is what frees the event slot again.
    bool takeFinishedAction(u8& event, u8& slot);
    bool writeRootPort(u8 port, u16 value);
    bool setChildPaused(u8 child, bool paused);
    bool childPortReady(u8 child, u8 port) const;
    void setChildVolume(u8 child, float volume);
    PCJamResult result() const { return mResult; }
    u8 unsupportedOpcode() const { return mUnsupportedOpcode; }
    u32 lastOpcodeAddress() const { return mLastOpcodeAddress; }
    // A sequence that dies takes every sound it had not yet triggered with it,
    // so a failure needs to say where: which byte of the sequence was running,
    // what it was trying to reach, and how big the sequence is.
    u32 failedAddress() const { return mFailedAddress; }
    // For a table-indexed call: the index register's value and where in the
    // sequence that index landed. An index past the end of the table still sits
    // inside the sequence, so the bounds check passes and arbitrary bytes get
    // read as an address.
    u32 failedIndex() const { return mFailedIndex; }
    // The last opcodes executed before the failure. The index register arrives
    // holding what looks like a sound id rather than a table slot, so what
    // matters is which instruction was supposed to turn one into the other.
    u32 traceCount() const { return mTraceCount < 16 ? mTraceCount : 16; }
    void traceEntry(u32 i, u32& pc, u8& opcode, u16& reg1) const {
        const u32 slot = (mTraceNext + 16 - traceCount() + i) % 16;
        pc = mTrace[slot].pc; opcode = mTrace[slot].opcode; reg1 = mTrace[slot].reg1;
    }
    u32 failedTableEntry() const { return mFailedTableEntry; }
    u32 sequenceSize() const { return static_cast<u32>(mSequence.size()); }
    // The operand layout is what is in doubt, so hand back the raw bytes from
    // the failing opcode onwards and let them be decoded by hand.
    const u8* bytesAt(u32 address, u32& available) const {
        if (address >= mSequence.size()) { available = 0; return nullptr; }
        available = static_cast<u32>(mSequence.size() - address);
        return mSequence.data() + address;
    }
    u8 failedOpcodeByte() const {
        return mLastOpcodeAddress < mSequence.size() ? mSequence[mLastOpcodeAddress] : 0;
    }
    u16 tempo() const { return mTracks[0].tempo ? mTracks[0].tempo : 120; }
    u16 timeBase() const { return mTracks[0].timeBase ? mTracks[0].timeBase : 48; }
    size_t naturalWaitCount() const { return mNaturalWaitCount; }
    size_t gateNoteCount() const { return mGateNoteCount; }
    size_t pitchBendCount() const { return mPitchBendCount; }
    size_t durationRegisterCount() const { return mDurationRegisterCount; }
    size_t opcodeCount(u8 opcode) const { return mOpcodeCounts[opcode]; }

private:
    struct StackEntry { u32 address = 0; u16 loop = 0; bool isLoop = false; };
    struct Track {
        bool active = false;
        u32 pc = 0;
        u32 wait = 0;
        u16 tempo = 120;
        u16 timeBase = 48;
        bool needsTempoSync = false;
        float tempoAccumulator = 0.0f;
        s8 transpose = 0;
        u16 bankProgram = 0x00F0;
        struct Parameter {
            float current = 1.0f;
            float target = 1.0f;
            float step = 0.0f;
            u32 duration = 0;
        };
        std::array<Parameter, 18> parameters {};
        std::array<u16, 32> registers {};
        std::array<u32, 4> extendedRegisters {};
        std::shared_ptr<const std::vector<PCInstrumentOscillator>> adsr;
        std::array<StackEntry, 8> stack {};
        size_t stackDepth = 0;
        int parent = -1;
        std::array<int, 16> children {};
        u8 flags = 0;
        bool gateMode = false;
        bool paused = false;
        u8 cutoff = 127;
        std::array<u8, 3> parentPanCalcTypes { 2, 2, 2 };
        struct Port { u16 value = 0; bool imported = false; bool exported = false; };
        std::array<Port, 16> ports {};
        std::array<u32, 8> interruptAddresses {};
        std::array<u32, 8> noteTimers {};
        u8 activeNotes = 0;
        u16 interruptEnable = 0;
        u16 pendingInterrupts = 0;
        u8 interruptActive = 0;
        u32 savedPc = 0;
        u32 savedWait = 0;
    };

    bool read8(Track& track, u8& value);
    bool read16(Track& track, u16& value);
    bool read24(Track& track, u32& value);
    void noteActionFinished(size_t index, u16 message);
    bool validAddress(u32 address) const;
    PCJamResult runTrack(size_t index, std::vector<PCJamEvent>& events,
                         size_t& operations, size_t limit);
    PCJamResult fail(PCJamResult result, u8 opcode = 0, u32 address = 0);
    int allocateTrack(size_t parent, u8 child, std::vector<PCJamEvent>& events);
    void deactivateTrack(size_t index, std::vector<PCJamEvent>& events);
    void initializeTrack(Track& track);
    void updateParameters(Track& track);
    void eventMix(size_t index, PCJamEvent& event) const;

    std::vector<std::pair<u8, u8>> mFinishedActions;
    std::vector<u8> mSequence;
    std::array<Track, kPCJamTrackCount> mTracks {};
    PCJamResult mResult = PCJamResult::Finished;
    u8 mUnsupportedOpcode = 0;
    u32 mLastOpcodeAddress = 0;
    u32 mFailedAddress = 0;
    u32 mFailedIndex = 0;
    u32 mFailedTableEntry = 0;
    struct TraceEntry { u32 pc; u8 opcode; u16 reg1; };
    TraceEntry mTrace[16] = {};
    u32 mTraceNext = 0;
    u32 mTraceCount = 0;
    size_t mNaturalWaitCount = 0;
    size_t mGateNoteCount = 0;
    size_t mPitchBendCount = 0;
    size_t mDurationRegisterCount = 0;
    std::array<size_t, 256> mOpcodeCounts {};
    std::array<float, 16> mChildVolumes {};
};

#endif
