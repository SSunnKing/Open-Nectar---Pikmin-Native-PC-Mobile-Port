#include "audio/pc_jam.h"

#include <algorithm>
#include <cmath>

bool PCJamPlayer::start(const std::vector<u8>& sequence, u8 bank) {
    mSequence = sequence;
    mTracks = {};
    mUnsupportedOpcode = 0;
    mLastOpcodeAddress = 0;
    mNaturalWaitCount = 0;
    mGateNoteCount = 0;
    mPitchBendCount = 0;
    mDurationRegisterCount = 0;
    mOpcodeCounts.fill(0);
    mChildVolumes.fill(1.0f);
    if (mSequence.empty()) return fail(PCJamResult::Truncated) == PCJamResult::Ok;
    initializeTrack(mTracks[0]);
    mTracks[0].active = true;
    mTracks[0].bankProgram = static_cast<u16>((bank << 8) | 0xF0);
    mResult = PCJamResult::Ok;
    return true;
}

void PCJamPlayer::initializeTrack(Track& track) {
    track = {};
    track.children.fill(-1);
    track.parameters[1].current = track.parameters[1].target = 0.0f;
    track.parameters[2].current = track.parameters[2].target = 0.0f;
    track.parameters[3].current = track.parameters[3].target = 0.5f;
    track.parameters[4].current = track.parameters[4].target = 0.0f;
    track.parameters[5].current = track.parameters[5].target = 0.0f;
    for (size_t i = 6; i <= 15; ++i)
        track.parameters[i].current = track.parameters[i].target = 0.0f;
    track.parameters[16].current = track.parameters[16].target = 0.5f;
    track.parameters[17].current = track.parameters[17].target = 0.0f;
}

void PCJamPlayer::updateParameters(Track& track) {
    for (Track::Parameter& parameter : track.parameters) {
        if (parameter.duration == 0) continue;
        parameter.current += parameter.step;
        if (--parameter.duration == 0) parameter.current = parameter.target;
    }
}

void PCJamPlayer::eventMix(size_t index, PCJamEvent& event) const {
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.5f;
    float fxMix = 0.0f;
    float dolby = 0.0f;
    std::array<size_t, kPCJamTrackCount> lineage {};
    size_t count = 0;
    for (int current = static_cast<int>(index);
         current >= 0 && count < lineage.size(); current = mTracks[current].parent)
        lineage[count++] = static_cast<size_t>(current);
    for (size_t i = count; i-- > 0;) {
        const Track& node = mTracks[lineage[i]];
        const float localPitch = std::pow(2.0f, 4.0f * node.parameters[1].current);
        if (i == count - 1 || (node.flags & 1)) {
            volume = node.parameters[0].current;
            pitch = localPitch;
            pan = node.parameters[3].current;
            fxMix = node.parameters[2].current;
            dolby = node.parameters[4].current;
        } else {
            volume *= node.parameters[0].current;
            pitch *= localPitch;
            const float localPan = node.parameters[3].current;
            switch (node.parentPanCalcTypes[0]) {
            case 0: pan = localPan; break;
            case 1: break; // Preserve the parent value.
            default: pan = (pan + localPan) * 0.5f; break;
            }
            const auto inherit = [](float parent, float local, u8 mode) {
                switch (mode) {
                case 0: return local;
                case 1: return parent;
                default: return (parent + local) * 0.5f;
                }
            };
            fxMix = inherit(fxMix, node.parameters[2].current,
                            node.parentPanCalcTypes[1]);
            dolby = inherit(dolby, node.parameters[4].current,
                            node.parentPanCalcTypes[2]);
        }
    }
    if (count >= 2) {
        const int eventTrack = static_cast<int>(lineage[count - 2]);
        for (u8 child = 0; child < 16; ++child) {
            if (mTracks[0].children[child] == eventTrack) {
                event.source = child;
                break;
            }
        }
    }
    if (event.source < mChildVolumes.size()) volume *= mChildVolumes[event.source];
    event.volume = std::clamp(volume, 0.0f, 1.0f);
    event.pitch = std::clamp(pitch, 0.125f, 8.0f);
    event.pan = std::clamp(pan * 2.0f - 1.0f, -1.0f, 1.0f);
    event.fxMix = std::clamp(fxMix, 0.0f, 1.0f);
    event.dolby = std::clamp(dolby, 0.0f, 1.0f);
    event.cutoff = mTracks[index].cutoff;
    event.envelope = mTracks[index].adsr;
}

void PCJamPlayer::setChildVolume(u8 child, float volume) {
    if (child < mChildVolumes.size())
        mChildVolumes[child] = std::clamp(volume, 0.0f, 2.0f);
}

bool PCJamPlayer::setChildPaused(u8 child, bool paused) {
    if (child >= mTracks[0].children.size()) return false;
    const int root = mTracks[0].children[child];
    if (root < 0) return false;
    std::array<int, kPCJamTrackCount> pending {};
    size_t head = 0;
    size_t tail = 0;
    pending[tail++] = root;
    while (head < tail) {
        Track& track = mTracks[static_cast<size_t>(pending[head++])];
        track.paused = paused;
        for (int descendant : track.children) {
            if (descendant >= 0 && tail < pending.size()) pending[tail++] = descendant;
        }
    }
    return true;
}

int PCJamPlayer::allocateTrack(size_t parent, u8 child,
                               std::vector<PCJamEvent>& events) {
    Track& owner = mTracks[parent];
    const int previous = owner.children[child];
    if (previous >= 0) deactivateTrack(static_cast<size_t>(previous), events);
    for (size_t i = 1; i < mTracks.size(); ++i) {
        if (mTracks[i].active) continue;
        Track created;
        initializeTrack(created);
        created.active = true;
        created.parent = static_cast<int>(parent);
        created.children.fill(-1);
        mTracks[i] = created;
        owner.children[child] = static_cast<int>(i);
        return static_cast<int>(i);
    }
    return -1;
}

void PCJamPlayer::deactivateTrack(size_t index,
                                  std::vector<PCJamEvent>& events) {
    if (index >= mTracks.size() || !mTracks[index].active) return;
    for (int child : mTracks[index].children)
        if (child >= 0) deactivateTrack(static_cast<size_t>(child), events);
    // Jaq_CloseTrack calls __AllNoteOff before releasing a JAudio track.  The
    // native sequencer used to discard only its logical active-note mask here,
    // leaving the corresponding looping sample voices alive in pc_audio.  A
    // later child-track replacement or Finish therefore produced permanent
    // sustained tones (most visibly in FileSelect).
    for (u8 voice = 0; voice < 8; ++voice) {
        PCJamEvent event;
        event.type = PCJamEventType::NoteOff;
        event.track = static_cast<u8>(index);
        event.voice = voice;
        // The original gives root-track shutdown a short forced release;
        // children use their instrument's normal release curve.
        event.release = index == 0 ? 10 : 0;
        events.push_back(event);
    }
    const int parent = mTracks[index].parent;
    if (parent >= 0) {
        for (int& child : mTracks[static_cast<size_t>(parent)].children)
            if (child == static_cast<int>(index)) child = -1;
    }
    mTracks[index].active = false;
}

bool PCJamPlayer::active() const {
    for (const Track& track : mTracks) if (track.active) return true;
    return false;
}

bool PCJamPlayer::takeFinishedAction(u8& event, u8& slot) {
    if (mFinishedActions.empty()) return false;
    event = mFinishedActions.front().first;
    slot = mFinishedActions.front().second;
    mFinishedActions.erase(mFinishedActions.begin());
    return true;
}

void PCJamPlayer::noteFinished(u8 track, u8 voice) {
    if (track >= mTracks.size() || voice >= 8) return;
    mTracks[track].activeNotes &= static_cast<u8>(~(1u << voice));
    mTracks[track].noteTimers[voice] = 0;
}

bool PCJamPlayer::writeChildPort(u8 child, u8 port, u16 value) {
    if (child >= 16 || port >= 16) return false;
    const int slot = mTracks[0].children[child];
    if (slot < 0 || !mTracks[slot].active) return false;
    Track& track = mTracks[slot];
    track.ports[port].value = value;
    track.ports[port].imported = true;
    if (port < 2) track.pendingInterrupts |= static_cast<u16>(1 << (port + 3));
    return true;
}

bool PCJamPlayer::writeRootPort(u8 port, u16 value) {
    if (port >= 16 || !mTracks[0].active) return false;
    Track& track = mTracks[0];
    track.ports[port].value = value;
    track.ports[port].imported = true;
    if (port < 2) track.pendingInterrupts |= static_cast<u16>(1 << (port + 3));
    return true;
}

bool PCJamPlayer::childPortReady(u8 child, u8 port) const {
    if (child >= 16 || port >= 2) return false;
    const int slot = mTracks[0].children[child];
    return slot >= 0 && mTracks[slot].active
        && (mTracks[slot].interruptEnable & static_cast<u16>(1 << (port + 3))) != 0;
}

bool PCJamPlayer::read8(Track& track, u8& value) {
    if (track.pc >= mSequence.size()) return false;
    value = mSequence[track.pc++];
    return true;
}

bool PCJamPlayer::read16(Track& track, u16& value) {
    u8 hi, lo;
    if (!read8(track, hi) || !read8(track, lo)) return false;
    value = static_cast<u16>((hi << 8) | lo);
    return true;
}

bool PCJamPlayer::read24(Track& track, u32& value) {
    u8 a, b, c;
    if (!read8(track, a) || !read8(track, b) || !read8(track, c)) return false;
    value = (static_cast<u32>(a) << 16) | (static_cast<u32>(b) << 8) | c;
    return true;
}

// Arglist from src/jaudio/jammain_2.c: how many operands each 0xC0..0xFF
// command takes, and two bits per operand saying how wide each one is
// (0 = byte, 1 = word, 2 = 24-bit, 3 = register index).  A register-wrapped
// command (the 0xB0 family) ORs 0b11 over the entries it overrides.
struct CommandArguments {
    u8 count;
    u16 types;
};

static const CommandArguments kCommandArguments[64] = {
    { 0, 0x0000 }, { 2, 0x0008 }, { 2, 0x0008 }, { 1, 0x0002 }, // C0 OpenTrack OpenTrackBros Call
    { 0, 0x0000 }, { 0, 0x0000 }, { 1, 0x0000 }, { 1, 0x0002 }, // CallF Ret RetF Jmp
    { 0, 0x0000 }, { 1, 0x0001 }, { 0, 0x0000 }, { 2, 0x0000 }, // JmpF LoopS LoopE ReadPort
    { 2, 0x000C }, { 1, 0x0000 }, { 1, 0x0000 }, { 1, 0x0003 }, // WritePort CheckPortImport/Export WaitReg
    { 2, 0x0005 }, { 2, 0x000C }, { 2, 0x000C }, { 2, 0x000F }, // ConnectName Parent/ChildWritePort D3
    { 1, 0x0000 }, { 1, 0x0000 }, { 1, 0x0000 }, { 2, 0x0008 }, // SetLastNote TimeRelate SimpleOsc SimpleEnv
    { 5, 0x0155 }, { 1, 0x0000 }, { 1, 0x0000 }, { 1, 0x0000 }, // SimpleADSR Transpose CloseTrack OutSwitch
    { 1, 0x0001 }, { 2, 0x0004 }, { 1, 0x0000 }, { 2, 0x0008 }, // UpdateSync BusConnect PauseStatus SetInterrupt
    { 1, 0x0000 }, { 0, 0x0000 }, { 0, 0x0000 }, { 0, 0x0000 }, // DisInterrupt ClrI SetI RetI
    { 2, 0x0004 }, { 0, 0x0000 }, { 0, 0x0000 }, { 1, 0x0001 }, // IntTimer ConnectOpen ConnectClose SyncCPU
    { 0, 0x0000 }, { 0, 0x0000 }, { 1, 0x0002 }, { 5, 0x0000 }, // FlushAll FlushRelease Wait3 PanPowSet
    { 4, 0x0055 }, { 1, 0x0002 }, { 1, 0x0002 }, { 3, 0x0000 }, // IIRSet FIRSet EXTSet PanSwSet
    { 1, 0x0000 }, { 1, 0x0000 }, { 3, 0x0028 }, { 0, 0x0000 }, // OscRoute IIRCutOff OscFull F3
    { 0, 0x0000 }, { 0, 0x0000 }, { 0, 0x0000 }, { 0, 0x0000 }, // F4 F5 F6 F7
    { 0, 0x0000 }, { 0, 0x0000 }, { 1, 0x0001 }, { 0, 0x0000 }, // F8 F9 CheckWave Printf
    { 0, 0x0000 }, { 1, 0x0001 }, { 1, 0x0001 }, { 0, 0x0000 }, // Nop Tempo TimeBase Finish
};

// A note whose duration field is zero waits for its own voice to stop.
static constexpr u32 kWaitForNote = 0xFFFFFFFFu;

// PowerPC shift semantics, which the sequences rely on: a count of 32 or more
// clears the result (or fills with the sign bit for an arithmetic shift).
static u16 shiftLeft16(u16 value, int count) {
    return count >= 32 ? 0 : static_cast<u16>(static_cast<u32>(value) << count);
}

static u16 shiftRight16(u16 value, int count) {
    return count >= 32 ? 0 : static_cast<u16>(static_cast<u32>(value) >> count);
}

static s16 shiftRightSigned16(s16 value, int count) {
    if (count >= 32) return value < 0 ? -1 : 0;
    return static_cast<s16>(static_cast<s32>(value) >> count);
}

// GetRandom_s32 from src/jaudio/random.c.  Sequences pick between variants of
// an effect with this, so keeping the original generator keeps the variety the
// game was authored with.
static u32 nextSequenceRandom() {
    static s32 v0 = 0x0001000;
    static s32 v1 = 0x0005555;
    const s32 scaled = static_cast<s32>(static_cast<u32>(v0) * 0x13579BDEu) >> 4;
    s32 next = static_cast<s32>(static_cast<u32>(v1) * 0x98765432u
                                + static_cast<u32>(scaled));
    v0 = v1;
    v1 = ++next;
    return static_cast<u32>(next);
}

// The track running this is the per-action track: its parent is the event
// track, and that track's parent is the root. The slot is which child of the
// event track we are, and the event is which child of the root that track is.
void PCJamPlayer::noteActionFinished(size_t index, u16 message) {
    if (!(message & 0x8000)) return;
    const Track& action = mTracks[index];
    if (action.parent < 0) return;
    const Track& eventTrack = mTracks[static_cast<size_t>(action.parent)];
    if (eventTrack.parent < 0) return;
    const Track& root = mTracks[static_cast<size_t>(eventTrack.parent)];
    int slot = -1;
    for (u8 child = 0; child < 16; ++child)
        if (eventTrack.children[child] == static_cast<int>(index)) { slot = child; break; }
    int event = -1;
    for (u8 child = 0; child < 16; ++child)
        if (root.children[child] == action.parent) { event = child; break; }
    if (slot < 0 || event < 0 || mFinishedActions.size() >= 64) return;
    mFinishedActions.push_back({ static_cast<u8>(event), static_cast<u8>(slot) });
}

bool PCJamPlayer::validAddress(u32 address) const { return address < mSequence.size(); }

PCJamResult PCJamPlayer::fail(PCJamResult result, u8 opcode, u32 address) {
    mResult = result;
    mUnsupportedOpcode = opcode;
    mFailedAddress = address;
    for (Track& track : mTracks) track.active = false;
    return result;
}

PCJamResult PCJamPlayer::runTrack(size_t index, std::vector<PCJamEvent>& events,
                                  size_t& operations, size_t limit) {
    Track& track = mTracks[index];
    if (!track.active) return PCJamResult::Ok;
    if (track.paused) return PCJamResult::Ok;
    // The register file is what the sound-effect sequences actually compute
    // with, and it is not a plain array: several indices are computed views
    // over the track's live state.  These three follow Jam_WriteRegDirect,
    // Jam_ReadRegDirect and __ExchangeRegisterValue in src/jaudio/jammain_2.c
    // exactly; guessing at them is what silently broke the effect dispatch.
    const auto readRegDirect = [&](u8 reg) -> u16 {
        s16 result;
        switch (reg) {
        // bankNumber IS reg[6] in the original's union.  In this port the
        // authority is `bankProgram`, which a child track inherits at creation
        // without its register file being copied, so reading the raw register
        // here loses the bank the parent handed down.
        case 0x06:
        case 0x20:
        case 0x21:
            result = static_cast<s16>(track.bankProgram);
            break;
        case 0x22:
            result = static_cast<s16>(((track.registers[0] & 0xFF) << 8)
                                      | (track.registers[1] & 0xFF));
            break;
        case 0x2C: {
            // Which child track slots are alive.  Sequences read this to find
            // a free slot before opening a track on it.
            u16 alive = 0;
            for (int child = 15; child >= 0; --child) {
                alive = static_cast<u16>(alive << 1);
                const int slot = track.children[child];
                if (slot >= 0 && mTracks[static_cast<size_t>(slot)].active) alive |= 1;
            }
            result = static_cast<s16>(alive);
            break;
        }
        case 0x2D: {
            // Which voice slots have *stopped*: CheckNoteStop is true when the
            // slot holds no live note.
            u16 stopped = 0;
            for (int voice = 7; voice >= 0; --voice) {
                stopped = static_cast<u16>(stopped << 1);
                if (!(track.activeNotes & static_cast<u8>(1u << voice))) stopped |= 1;
            }
            result = static_cast<s16>(stopped);
            break;
        }
        case 0x30:
            result = track.stackDepth == 0
                ? 0 : static_cast<s16>(track.stack[track.stackDepth - 1].loop);
            break;
        default:
            result = reg < track.registers.size()
                ? static_cast<s16>(track.registers[reg]) : 0;
            break;
        }
        switch (reg) {
        case 0x00: case 0x01: case 0x02: case 0x21:
            result = static_cast<s16>(result & 0xFF);
            break;
        case 0x20:
            result = static_cast<s16>(result >> 8);
            break;
        }
        return static_cast<u16>(result);
    };
    const auto readReg32 = [&](u8 reg) -> u32 {
        if (reg >= 0x28 && reg <= 0x2B) return track.extendedRegisters[reg - 0x28];
        if (reg == 0x23)
            return (static_cast<u32>(readRegDirect(4)) << 16) | readRegDirect(5);
        return readRegDirect(reg);
    };
    // Registers 64 and up address the track's ports, not its register file.
    const auto readRegisterValue = [&](u8 reg) -> u32 {
        if (reg < 64) return readReg32(reg);
        const u8 port = static_cast<u8>(reg - 64);
        return port < track.ports.size() ? track.ports[port].value : 0;
    };
    // extendedRegs[] is a union view over reg[0x10..0x17] in the original, so
    // writing either half has to be visible through the other.
    const auto syncExtendedFromRegisters = [&](u8 reg) {
        if (reg < 0x10 || reg > 0x17) return;
        const size_t extended = (reg - 0x10) / 2;
        track.extendedRegisters[extended] =
            (static_cast<u32>(track.registers[0x10 + extended * 2]) << 16)
            | track.registers[0x11 + extended * 2];
    };
    const auto writeExtendedRegister = [&](size_t extended, u32 value) {
        track.extendedRegisters[extended] = value;
        track.registers[0x10 + extended * 2] = static_cast<u16>(value >> 16);
        track.registers[0x11 + extended * 2] = static_cast<u16>(value);
    };
    auto writeRegister = [&](u8 reg, u16 value) {
        u16 condition = value;
        if (reg <= 2) {
            value &= 0x00FF;
            condition = static_cast<u16>(static_cast<s16>(static_cast<s8>(value)));
        } else if (reg == 0x20 || reg == 0x21) {
            return;
        } else if (reg == 0x22) {
            track.registers[0] = static_cast<u16>((value >> 8) & 0xFF);
            reg = 1;
            value &= 0x00FF;
        }
        track.registers[3] = condition;
        if (reg < track.registers.size()) {
            track.registers[reg] = value;
            syncExtendedFromRegisters(reg);
            if (reg == 6) track.bankProgram = value;
        }
    };
    for (u8 voice = 0; voice < track.noteTimers.size(); ++voice) {
        if (track.noteTimers[voice] == 0 || --track.noteTimers[voice] != 0) continue;
        PCJamEvent event;
        event.type = PCJamEventType::NoteOff;
        event.track = static_cast<u8>(index);
        event.voice = voice;
        events.push_back(event);
        track.activeNotes &= static_cast<u8>(~(1u << voice));
    }
    updateParameters(track);
    if (!track.interruptActive) {
        for (u8 interrupt = 0; interrupt < 8; ++interrupt) {
            const u16 mask = static_cast<u16>(1 << interrupt);
            if (!(track.interruptEnable & mask) || !(track.pendingInterrupts & mask)) continue;
            if (!validAddress(track.interruptAddresses[interrupt]))
                return fail(PCJamResult::InvalidAddress, 0, track.interruptAddresses[interrupt]);
            track.savedPc = track.pc;
            track.savedWait = track.wait;
            track.pc = track.interruptAddresses[interrupt];
            track.wait = 0;
            track.interruptActive = mask;
            track.pendingInterrupts &= static_cast<u16>(~mask);
            break;
        }
    }
    // waitTimer == -1 in the original does not mean "wait forever": it means
    // wait until voice 0 has stopped sounding, which is how a one-shot effect
    // times itself before telling the game it has finished. Treating it as an
    // endless wait left every effect track parked on its note, holding its
    // event slot, and the sounds stopped coming.
    if (track.wait == kWaitForNote) {
        if (track.activeNotes & 1) return PCJamResult::Ok;
        track.wait = 0;
    }
    if (track.wait != 0) {
        --track.wait;
        if (track.wait != 0) return PCJamResult::Ok;
    }

    while (track.active) {
        if (++operations > limit) return fail(PCJamResult::OperationLimit);
        mLastOpcodeAddress = track.pc;
        if (track.pc < mSequence.size()) {
            mTrace[mTraceNext] = { track.pc, mSequence[track.pc],
                                   track.registers.size() > 1 ? track.registers[1] : u16(0) };
            mTraceNext = (mTraceNext + 1) % 16;
            ++mTraceCount;
        }
        u8 opcode;
        if (!read8(track, opcode)) return fail(PCJamResult::Truncated);
        ++mOpcodeCounts[opcode];
        if (opcode < 0x80) {
            u8 flags, velocity;
            if (!read8(track, flags) || !read8(track, velocity))
                return fail(PCJamResult::Truncated);
            u8 key = opcode;
            if (flags & 0x80) key = static_cast<u8>(track.registers[key & 31]);
            if (velocity & 0x80) velocity = static_cast<u8>(
                track.registers[(velocity - 0x80) & 31]);
            u8 voice = flags & 7;
            const bool fullFormat = voice == 0;
            u8 gate = 100;
            u32 duration = 0;
            const bool naturalWait = voice == 0 && ((flags >> 3) & 3) == 0;
            if (voice == 0) {
                if (!read8(track, gate)) return fail(PCJamResult::Truncated);
                const u8 bytes = (flags >> 3) & 3;
                for (u8 i = 0; i < bytes; ++i) {
                    u8 part;
                    if (!read8(track, part)) return fail(PCJamResult::Truncated);
                    duration = (duration << 8) | part;
                }
                if (bytes == 1 && duration >= 0x80) {
                    duration = track.registers[(duration - 0x80) & 31];
                    ++mDurationRegisterCount;
                }
            } else {
                const u8 durationBytes = (flags >> 3) & 3;
                if (durationBytes != 0)
                    voice = static_cast<u8>(track.registers[voice - 1]);
                if (voice >= 8) return fail(PCJamResult::UnsupportedOpcode, opcode);
            }
            PCJamEvent event;
            // JAudio's GateON retunes/retriggers the channel already assigned
            // to this slot.  It does not allocate a new sample voice or rewind
            // it to sample zero.  Treating every gated note as NoteOn caused
            // sustained instruments to be repeatedly restarted and heard as
            // the long, stuck tones present in several BGM sequences.
            const bool gateUpdate = track.gateMode
                && (track.activeNotes & static_cast<u8>(1u << voice));
            event.type = gateUpdate ? PCJamEventType::GateUpdate
                                    : PCJamEventType::NoteOn;
            event.track = static_cast<u8>(index);
            event.voice = voice;
            event.key = static_cast<u8>(std::clamp<int>(key + track.transpose, 0, 127));
            event.velocity = velocity;
            event.bank = static_cast<u8>(track.bankProgram >> 8);
            event.program = static_cast<u8>(track.bankProgram);
            eventMix(index, event);
            events.push_back(event);
            track.activeNotes |= static_cast<u8>(1u << voice);
            if (flags & 0x20) ++mGateNoteCount;
            if (flags & 0x40) ++mPitchBendCount;
            track.noteTimers[voice] = duration == 0 || (flags & 0x20) ? 0
                : std::max<u32>(1, (duration * gate + 99) / 100);
            track.gateMode = (flags & 0x20) != 0;
            if (naturalWait) ++mNaturalWaitCount;
            // Jam_SeqmainNote yields after *every* note in the full format --
            // the compact one leaves noteDurationTicks at -1 and falls through
            // -- and a duration of zero there means "wait indefinitely", not
            // "do not wait". Treating zero as no wait let a sound-effect
            // handler that loops on such a note spin the interpreter until it
            // hit the operation limit, which killed the whole event sequence.
            if (fullFormat) {
                track.wait = duration == 0 ? kWaitForNote : duration;
                return PCJamResult::Ok;
            }
            continue;
        }
        if ((opcode & 0xF0) == 0x80) {
            u8 voice = opcode & 0x0F;
            if (voice == 0 || voice == 8) {
                u16 wait;
                if (voice == 0) {
                    u8 value;
                    if (!read8(track, value)) return fail(PCJamResult::Truncated);
                    wait = value;
                } else if (!read16(track, wait)) {
                    return fail(PCJamResult::Truncated);
                }
                if (wait != 0) {
                    track.wait = wait;
                    return PCJamResult::Ok;
                }
                continue;
            }
            PCJamEvent event;
            event.type = PCJamEventType::NoteOff;
            event.track = static_cast<u8>(index);
            if (voice > 8) {
                voice = static_cast<u8>(voice - 8);
                u8 release;
                if (!read8(track, release)) return fail(PCJamResult::Truncated);
                event.release = release > 100 ? static_cast<u16>((release - 98) * 20) : release;
            }
            event.voice = voice;
            events.push_back(event);
            track.activeNotes &= static_cast<u8>(~(1u << voice));
            track.noteTimers[voice] = 0;
            continue;
        }
        if ((opcode & 0xF0) == 0xA0) {
            // Jam_WriteRegParam, src/jaudio/jammain_2.c.  Followed literally,
            // including the original's reuse of `control` across the three
            // prefix tests: the sound-effect sequences depend on the exact
            // operand lengths, and one wrong byte desynchronises the stream.
            u8 control = static_cast<u8>(opcode & 0x0F);
            u32 sourceMode = static_cast<u32>(control & 0x0C);
            u32 operation = static_cast<u32>(control & 0x03);
            u32 tableType = 0;
            u32 wide = 0;
            if ((control & 0x0F) == 0x0B) {
                sourceMode = 0;
                operation = 0x0B;
            }
            if ((control & 0x0F) == 0x0A) {
                if (!read8(track, control)) return fail(PCJamResult::Truncated);
                sourceMode = static_cast<u32>(control & 0x0C);
                operation = 0x0A;
                tableType = static_cast<u32>(control >> 4) + 4;
            }
            if ((control & 0x0F) == 0x09) {
                if (!read8(track, control)) return fail(PCJamResult::Truncated);
                sourceMode = static_cast<u32>(control & 0x0C);
                operation = static_cast<u32>(control & 0xF0);
                // Source form 8 under an extended operation means the constant
                // -1 and consumes no operand byte.  Reading one here is what
                // shifted the stream by a byte and left the table-indexed call
                // jumping on a sound id instead of a slot number.
                if (sourceMode == 8) sourceMode = 0x10;
            }
            u8 regIdx;
            if (!read8(track, regIdx)) return fail(PCJamResult::Truncated);
            if (operation == 0x0A) {
                u8 baseRegister;
                if (!read8(track, baseRegister)) return fail(PCJamResult::Truncated);
                wide = readReg32(baseRegister);
            }
            s16 newValue = 0;
            {
                u8 byteOperand;
                u16 wordOperand;
                switch (sourceMode) {
                case 0:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    newValue = static_cast<s16>(readRegDirect(byteOperand));
                    break;
                case 4:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    newValue = byteOperand;
                    break;
                case 8:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    newValue = static_cast<s16>(byteOperand << 8);
                    break;
                case 12:
                    if (!read16(track, wordOperand)) return fail(PCJamResult::Truncated);
                    newValue = static_cast<s16>(wordOperand);
                    break;
                case 16:
                    newValue = -1;
                    break;
                }
            }
            const s16 oldValue = static_cast<s16>(readRegDirect(regIdx));
            switch (operation) {
            case 0x00:
                break;
            case 0x01:
                if (sourceMode == 4) newValue = static_cast<s8>(newValue);
                newValue = static_cast<s16>(oldValue + newValue);
                break;
            case 0x02: {
                // The product is 32 bits wide and lands in the register pair
                // 4:5, not in the named destination register.
                const u32 product = static_cast<u32>(oldValue * newValue);
                track.registers[4] = static_cast<u16>(product >> 16);
                track.registers[5] = static_cast<u16>(product);
                continue;
            }
            case 0x03:
                track.registers[3] = static_cast<u16>(oldValue - newValue);
                continue;
            case 0x0B:
                newValue = static_cast<s16>(oldValue - newValue);
                break;
            case 0x10:
                if (sourceMode == 4) newValue = static_cast<s8>(newValue);
                newValue = newValue < 0
                    ? static_cast<s16>(shiftRight16(static_cast<u16>(oldValue), -newValue))
                    : static_cast<s16>(shiftLeft16(static_cast<u16>(oldValue), newValue));
                break;
            case 0x20:
                if (sourceMode == 4) newValue = static_cast<s8>(newValue);
                newValue = newValue < 0
                    ? static_cast<s16>(shiftRightSigned16(oldValue, -newValue))
                    : static_cast<s16>(shiftLeft16(static_cast<u16>(oldValue), newValue));
                break;
            case 0x30: newValue = static_cast<s16>(oldValue & newValue); break;
            case 0x40: newValue = static_cast<s16>(oldValue | newValue); break;
            case 0x50: newValue = static_cast<s16>(oldValue ^ newValue); break;
            case 0x60: newValue = static_cast<s16>(-oldValue); break;
            case 0x90: {
                const u16 limit = static_cast<u16>(newValue);
                newValue = limit == 0 ? 0
                    : static_cast<s16>(nextSequenceRandom() % limit);
                break;
            }
            case 0x0A: {
                // LoadTbl: the entry width also scales the index, except for
                // type 8, which reads four bytes at an unscaled offset.
                const u32 index = static_cast<u16>(newValue);
                u32 offset = wide;
                u32 width = 0;
                switch (tableType) {
                case 4: width = 1; offset += index; break;
                case 5: width = 2; offset += index * 2; break;
                case 6: width = 3; offset += index * 3; break;
                case 7: width = 4; offset += index * 4; break;
                case 8: width = 4; offset += index; break;
                default: break;
                }
                if (width == 0) { wide = 0; break; }
                if (offset > mSequence.size() || mSequence.size() - offset < width)
                    return fail(PCJamResult::InvalidAddress, 0, offset);
                wide = 0;
                for (u32 i = 0; i < width; ++i)
                    wide = (wide << 8) | mSequence[offset + i];
                newValue = static_cast<s16>(wide);
                break;
            }
            default:
                break;
            }
            u16 condition = static_cast<u16>(newValue);
            switch (regIdx) {
            case 0x00: case 0x01: case 0x02:
                newValue = static_cast<s16>(newValue & 0xFF);
                condition = static_cast<u16>(static_cast<s16>(
                    static_cast<s8>(static_cast<u8>(newValue))));
                break;
            case 0x21:
                newValue = static_cast<s16>((track.bankProgram & 0xFF00)
                                            | (newValue & 0x00FF));
                regIdx = 6;
                break;
            case 0x20:
                newValue = static_cast<s16>((track.bankProgram & 0x00FF)
                                            | (newValue << 8));
                regIdx = 6;
                break;
            case 0x2E:
                newValue = static_cast<s16>((track.registers[0x0D] & 0xFF00)
                                            | (newValue & 0x00FF));
                regIdx = 0x0D;
                break;
            case 0x2F:
                newValue = static_cast<s16>((track.registers[0x0D] & 0x00FF)
                                            | (newValue << 8));
                regIdx = 0x0D;
                break;
            case 0x22:
                track.registers[0] = static_cast<u16>((newValue >> 8) & 0xFF);
                newValue = static_cast<s16>(newValue & 0xFF);
                condition = static_cast<u16>(newValue);
                regIdx = 1;
                break;
            case 0x28: case 0x29: case 0x2A: case 0x2B:
                writeExtendedRegister(regIdx - 0x28, wide);
                continue;
            default:
                break;
            }
            if (regIdx < track.registers.size()) {
                track.registers[regIdx] = static_cast<u16>(newValue);
                syncExtendedFromRegisters(regIdx);
                if (regIdx == 6) track.bankProgram = static_cast<u16>(newValue);
            }
            track.registers[3] = condition;
            continue;
        }
        if ((opcode & 0xF0) == 0x90) {
            u8 parameter;
            if (!read8(track, parameter) || parameter >= track.parameters.size())
                return fail(PCJamResult::Truncated);
            s16 target = 0;
            u8 byte;
            u16 word;
            switch (opcode & 0x0C) {
            case 0:
                if (!read8(track, byte)) return fail(PCJamResult::Truncated);
                target = static_cast<s16>(track.registers[byte & 31]);
                break;
            case 4:
                if (!read8(track, byte)) return fail(PCJamResult::Truncated);
                target = byte;
                break;
            case 8:
                if (!read8(track, byte)) return fail(PCJamResult::Truncated);
                target = static_cast<s16>(byte << 8);
                break;
            default:
                if (!read16(track, word)) return fail(PCJamResult::Truncated);
                target = static_cast<s16>(word);
                break;
            }
            u32 duration = 0;
            switch (opcode & 3) {
            case 1:
                if (!read8(track, byte)) return fail(PCJamResult::Truncated);
                duration = track.registers[byte & 31];
                break;
            case 2:
                if (!read8(track, byte)) return fail(PCJamResult::Truncated);
                duration = byte;
                break;
            case 3:
                if (!read16(track, word)) return fail(PCJamResult::Truncated);
                duration = word;
                break;
            }
            Track::Parameter& move = track.parameters[parameter];
            move.target = target / 32768.0f;
            if (duration == 0) {
                move.current = move.target;
                move.step = 0.0f;
                move.duration = 0;
            } else {
                move.duration = duration;
                move.step = (move.target - move.current) / duration;
            }
            continue;
        }

        if ((opcode & 0xF0) == 0xB0) {
            // RegCmd_Process and Cmd_Process, src/jaudio/jammain_2.c.  Every
            // command's operand layout comes from one table, so a command this
            // port does not act on still consumes exactly the right bytes and
            // can be skipped instead of killing the sequence.  Decoding this
            // per command by hand is what made "tempo from a register" (0xFD,
            // wrapped in 0xB0) stop Olimar's effect track dead.
            const u8 argumentTypeCount = static_cast<u8>(opcode & 7);
            const bool commandFromRegister = (opcode & 8) != 0;
            u8 command;
            if (!read8(track, command)) return fail(PCJamResult::Truncated);
            if (commandFromRegister)
                command = static_cast<u8>(readRegisterValue(command));
            u16 argumentTypes = 0;
            if (!commandFromRegister || argumentTypeCount != 0) {
                u8 maskBits;
                if (!read8(track, maskBits)) return fail(PCJamResult::Truncated);
                u16 pair = 0x3;
                for (u8 i = 0; i <= argumentTypeCount; ++i) {
                    if (maskBits & 0x80) argumentTypes |= pair;
                    maskBits = static_cast<u8>(maskBits << 1);
                    pair = static_cast<u16>(pair << 2);
                }
            }
            const CommandArguments& layout = kCommandArguments[command - 0xC0];
            argumentTypes |= layout.types;
            u32 arguments[8] = {};
            for (u8 i = 0; i < layout.count && i < 8; ++i) {
                u8 byteOperand;
                u16 wordOperand;
                switch (argumentTypes & 3) {
                case 0:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    arguments[i] = byteOperand;
                    break;
                case 1:
                    if (!read16(track, wordOperand)) return fail(PCJamResult::Truncated);
                    arguments[i] = wordOperand;
                    break;
                case 2:
                    if (!read24(track, arguments[i])) return fail(PCJamResult::Truncated);
                    break;
                default:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    arguments[i] = readRegisterValue(byteOperand);
                    break;
                }
                argumentTypes = static_cast<u16>(argumentTypes >> 2);
            }
            switch (command) {
            case 0xC1:
            case 0xC2: {
                const size_t owner = command == 0xC2 && track.parent >= 0
                    ? static_cast<size_t>(track.parent) : index;
                u8 child = static_cast<u8>(arguments[0]);
                const u8 trackFlags = (child & 0x20) ? 4
                    : static_cast<u8>((child & 0xC0) >> 6);
                child &= 0x0F;
                if (!validAddress(arguments[1]))
                    return fail(PCJamResult::InvalidAddress, 0, arguments[1]);
                const int slot = allocateTrack(owner, child, events);
                if (slot < 0) return fail(PCJamResult::OperationLimit);
                Track& created = mTracks[static_cast<size_t>(slot)];
                created.pc = arguments[1];
                created.tempo = track.tempo;
                created.timeBase = track.timeBase;
                created.transpose = track.transpose;
                created.bankProgram = track.bankProgram;
                created.flags = trackFlags;
                break;
            }
            case 0xC3:
                if (!validAddress(arguments[0]))
                    return fail(PCJamResult::InvalidAddress, 0, arguments[0]);
                if (track.stackDepth >= track.stack.size())
                    return fail(PCJamResult::StackOverflow);
                track.stack[track.stackDepth++] = { track.pc, 0, false };
                track.pc = arguments[0];
                break;
            case 0xC7:
                if (!validAddress(arguments[0]))
                    return fail(PCJamResult::InvalidAddress, 0, arguments[0]);
                track.pc = arguments[0];
                break;
            case 0xCB: {
                const u8 port = static_cast<u8>(arguments[0]);
                const u8 reg = static_cast<u8>(arguments[1]);
                if (port < track.ports.size() && reg < track.registers.size()) {
                    writeRegister(reg, track.ports[port].value);
                    track.ports[port].imported = false;
                }
                break;
            }
            case 0xCC: {
                const u8 port = static_cast<u8>(arguments[0]);
                if (port < track.ports.size()) {
                    track.ports[port].value = static_cast<u16>(arguments[1]);
                    track.ports[port].exported = true;
                }
                break;
            }
            case 0xD1:
            case 0xD2: {
                const u8 destination = static_cast<u8>(arguments[0]);
                const u8 port = static_cast<u8>(destination & 0x0F);
                const int target = command == 0xD1 ? track.parent
                    : track.children[(destination >> 4) & 0x0F];
                if (target >= 0 && port < 16) {
                    Track& receiver = mTracks[static_cast<size_t>(target)];
                    receiver.ports[port].value = static_cast<u16>(arguments[1]);
                    receiver.ports[port].imported = true;
                    if (port < 2)
                        receiver.pendingInterrupts |= static_cast<u16>(1 << (port + 3));
                }
                break;
            }
            case 0xD9:
                track.transpose = static_cast<s8>(arguments[0]);
                break;
            case 0xDA: {
                const int slot = track.children[arguments[0] & 0x0F];
                if (slot >= 0) deactivateTrack(static_cast<size_t>(slot), events);
                break;
            }
            case 0xE7:
                noteActionFinished(index, static_cast<u16>(arguments[0]));
                track.registers[3] = 0;
                break;
            case 0xFD:
                track.tempo = static_cast<u16>(arguments[0]);
                if (track.parent >= 0) track.needsTempoSync = true;
                break;
            case 0xFE:
                track.timeBase = static_cast<u16>(arguments[0]);
                break;
            case 0xFF:
                deactivateTrack(index, events);
                break;
            default:
                // Operands are already consumed, so the stream stays aligned.
                break;
            }
            continue;
        }

        u32 address;
        u16 value;
        switch (opcode) {
        case 0xC1: {
            u8 child;
            if (!read8(track, child) || !read24(track, address))
                return fail(PCJamResult::Truncated);
            const u8 trackFlags = (child & 0x20) ? 4 : static_cast<u8>((child & 0xC0) >> 6);
            child &= 0x0F;
            if (!validAddress(address)) return fail(PCJamResult::InvalidAddress, 0, address);
            const int slot = allocateTrack(index, child, events);
            if (slot < 0) return fail(PCJamResult::OperationLimit);
            Track& created = mTracks[static_cast<size_t>(slot)];
            created.pc = address;
            created.tempo = track.tempo;
            created.timeBase = track.timeBase;
            created.transpose = track.transpose;
            created.bankProgram = track.bankProgram;
            created.flags = trackFlags;
            break;
        }
        case 0xC3:
            if (!read24(track, address)) return fail(PCJamResult::Truncated);
            if (!validAddress(address)) return fail(PCJamResult::InvalidAddress, 0, address);
            if (track.stackDepth >= track.stack.size()) return fail(PCJamResult::StackOverflow);
            track.stack[track.stackDepth++] = { track.pc, 0, false };
            track.pc = address;
            break;
        case 0xC4:
        case 0xC8: {
            u8 flags;
            if (!read8(track, flags)) return fail(PCJamResult::Truncated);
            if (flags & 0x80) {
                u8 reg;
                if (!read8(track, reg)) return fail(PCJamResult::Truncated);
                address = readRegDirect(reg);
                if (flags & 0x40) {
                    u32 tableBase;
                    if (flags & 0x20) {
                        if (!read8(track, reg)) return fail(PCJamResult::Truncated);
                        tableBase = readRegDirect(reg);
                    } else if (!read24(track, tableBase)) {
                        return fail(PCJamResult::Truncated);
                    }
                    const u32 tableEntry = tableBase + address * 3;
                    mFailedIndex = address;
                    mFailedTableEntry = tableEntry;
                    if (tableEntry > mSequence.size()
                        || mSequence.size() - tableEntry < 3)
                        return fail(PCJamResult::InvalidAddress);
                    address = (static_cast<u32>(mSequence[tableEntry]) << 16)
                            | (static_cast<u32>(mSequence[tableEntry + 1]) << 8)
                            | mSequence[tableEntry + 2];
                }
            } else if (!read24(track, address)) {
                return fail(PCJamResult::Truncated);
            }
            const u16 conditionValue = track.registers[3];
            bool take = false;
            switch (flags & 0x0F) {
            case 0: take = true; break;
            case 1: take = conditionValue == 0; break;
            case 2: take = conditionValue != 0; break;
            case 3: take = conditionValue == 1; break;
            case 4: take = conditionValue >= 0x8000; break;
            case 5: take = conditionValue < 0x8000; break;
            }
            if (take) {
                if (!validAddress(address)) return fail(PCJamResult::InvalidAddress, 0, address);
                if (opcode == 0xC4) {
                    if (track.stackDepth >= track.stack.size())
                        return fail(PCJamResult::StackOverflow);
                    track.stack[track.stackDepth++] = { track.pc, 0, false };
                }
                track.pc = address;
            }
            break;
        }
        case 0xC5:
            if (track.stackDepth == 0 || track.stack[track.stackDepth - 1].isLoop)
                return fail(PCJamResult::StackUnderflow);
            track.pc = track.stack[--track.stackDepth].address;
            break;
        case 0xC6: {
            u8 condition;
            if (!read8(track, condition)) return fail(PCJamResult::Truncated);
            const u16 v = track.registers[3];
            const bool take = (condition & 0x0F) == 0
                           || ((condition & 0x0F) == 1 && v == 0)
                           || ((condition & 0x0F) == 2 && v != 0);
            if (take) {
                if (track.stackDepth == 0 || track.stack[track.stackDepth - 1].isLoop)
                    return fail(PCJamResult::StackUnderflow);
                track.pc = track.stack[--track.stackDepth].address;
            }
            break;
        }
        case 0xC7:
            if (!read24(track, address)) return fail(PCJamResult::Truncated);
            if (!validAddress(address)) return fail(PCJamResult::InvalidAddress, 0, address);
            track.pc = address;
            break;
        case 0xC9:
            if (!read16(track, value)) return fail(PCJamResult::Truncated);
            if (track.stackDepth >= track.stack.size()) return fail(PCJamResult::StackOverflow);
            track.stack[track.stackDepth++] = { track.pc, value, true };
            break;
        case 0xCB: { // ReadPort
            u8 port, reg;
            if (!read8(track, port) || !read8(track, reg)) return fail(PCJamResult::Truncated);
            if (port < track.ports.size() && reg < track.registers.size()) {
                writeRegister(reg, track.ports[port].value);
                track.ports[port].imported = false;
            }
            break;
        }
        case 0xCC: { // WritePort: value is read from a register.
            u8 port, reg;
            if (!read8(track, port) || !read8(track, reg)) return fail(PCJamResult::Truncated);
            if (port < track.ports.size()) {
                track.ports[port].value = reg < track.registers.size() ? track.registers[reg] : 0;
                track.ports[port].exported = true;
            }
            break;
        }
        case 0xCD:
        case 0xCE: {
            u8 port;
            if (!read8(track, port)) return fail(PCJamResult::Truncated);
            track.registers[3] = port < track.ports.size()
                ? ((opcode == 0xCD) ? track.ports[port].imported : track.ports[port].exported) : 0;
            break;
        }
        case 0xCF: {
            u8 reg;
            if (!read8(track, reg)) return fail(PCJamResult::Truncated);
            track.wait = reg < track.registers.size() ? track.registers[reg] : 0;
            if (track.wait) return PCJamResult::Ok;
            break;
        }
        case 0xD0: { // ConnectName
            u16 group, name;
            if (!read16(track, group) || !read16(track, name))
                return fail(PCJamResult::Truncated);
            break;
        }
        case 0xD1:
        case 0xD2: {
            u8 destination, reg;
            if (!read8(track, destination) || !read8(track, reg))
                return fail(PCJamResult::Truncated);
            int target = -1;
            u8 port = destination & 0x0F;
            if (opcode == 0xD1) target = track.parent;
            else target = track.children[(destination >> 4) & 0x0F];
            if (target >= 0 && port < 16) {
                Track& receiver = mTracks[static_cast<size_t>(target)];
                receiver.ports[port].value = reg < track.registers.size()
                    ? track.registers[reg] : 0;
                receiver.ports[port].imported = true;
                if (port < 2) receiver.pendingInterrupts |= static_cast<u16>(1 << (port + 3));
            }
            break;
        }
        case 0xD3: { // Register-routed port/state command.
            u8 first, second;
            if (!read8(track, first) || !read8(track, second))
                return fail(PCJamResult::Truncated);
            break;
        }
        case 0xD4: { // SetLastNote
            u8 ignored;
            if (!read8(track, ignored)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xD5: { // TimeRelate
            u8 ignored;
            if (!read8(track, ignored)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xD6: {
            u8 oscillator;
            if (!read8(track, oscillator)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xD7:
            { u8 oscillator; if (!read8(track, oscillator) || !read24(track, address))
                return fail(PCJamResult::Truncated); }
            break;
        case 0xD8: { // SimpleADSR
            // Osc_Setup_ADSR (jamosc.c) patches five values into two fixed
            // envelope templates. The tables are triples of (curve, time,
            // value), the same shape the bank's own curves use.
            s16 args[5];
            for (int i = 0; i < 5; ++i) {
                if (!read16(track, value)) return fail(PCJamResult::Truncated);
                args[i] = static_cast<s16>(value);
            }
            s16 ads[12] = { 0, 0, 0x7FFF, 0, 0, 0x7FFF, 0, 0, 0, 14, 0, 0 };
            s16 rel[6] = { 0, 10, 0, 15, 1, 0 };
            ads[1] = args[0];
            ads[4] = args[1];
            ads[7] = args[2];
            ads[8] = args[3];
            rel[1] = args[4];
            PCInstrumentOscillator envelope;
            for (int i = 0; i < 12; i += 3)
                envelope.attack.push_back({ ads[i], ads[i + 1], ads[i + 2] });
            for (int i = 0; i < 6; i += 3)
                envelope.release.push_back({ rel[i], rel[i + 1], rel[i + 2] });
            // Shared and immutable: a voice in the mixer outlives the track
            // that started it, and the audio thread must never be left holding
            // a pointer into a track's own storage.
            auto table = std::make_shared<std::vector<PCInstrumentOscillator>>();
            table->push_back(std::move(envelope));
            track.adsr = std::move(table);
            break;
        }
        case 0xCA: {
            if (track.stackDepth == 0 || !track.stack[track.stackDepth - 1].isLoop)
                return fail(PCJamResult::StackUnderflow);
            StackEntry& loop = track.stack[track.stackDepth - 1];
            if (loop.loop != 0) --loop.loop;
            if (loop.loop == 0) --track.stackDepth;
            else track.pc = loop.address;
            break;
        }
        case 0xD9: {
            u8 transpose;
            if (!read8(track, transpose)) return fail(PCJamResult::Truncated);
            track.transpose = static_cast<s8>(transpose);
            break;
        }
        case 0xDA: { // CloseTrack
            u8 child;
            if (!read8(track, child)) return fail(PCJamResult::Truncated);
            const int slot = track.children[child & 0x0F];
            if (slot >= 0) deactivateTrack(static_cast<size_t>(slot), events);
            break;
        }
        case 0xDB: // Output switch
        case 0xDE: // Pause status
        case 0xF0: { // Oscillator routing
            u8 ignored;
            if (!read8(track, ignored)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xF1: { // IIR cutoff table index.
            u8 cutoff;
            if (!read8(track, cutoff)) return fail(PCJamResult::Truncated);
            track.cutoff = std::min<u8>(cutoff, 127);
            break;
        }
        case 0xDF: { // SetInterrupt
            u8 interrupt;
            if (!read8(track, interrupt) || !read24(track, address))
                return fail(PCJamResult::Truncated);
            if (interrupt < 8) {
                track.interruptEnable |= static_cast<u16>(1 << interrupt);
                track.interruptAddresses[interrupt] = address;
            }
            break;
        }
        case 0xE0: {
            u8 interrupt;
            if (!read8(track, interrupt)) return fail(PCJamResult::Truncated);
            if (interrupt < 8) track.interruptEnable &= static_cast<u16>(~(1 << interrupt));
            break;
        }
        case 0xE1: track.interruptActive = 0; break;
        case 0xE2: track.interruptActive = 1; break;
        case 0xE3:
            track.pc = track.savedPc;
            track.wait = track.savedWait;
            track.interruptActive = 0;
            break;
        case 0xE4: {
            u8 count;
            if (!read8(track, count) || !read16(track, value))
                return fail(PCJamResult::Truncated);
            break;
        }
        case 0xE5: // ConnectOpen
        case 0xE6: // ConnectClose
            break;
        case 0xDC: { // Update sync
            if (!read16(track, value)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xED: // FIR table offset
        case 0xEE: // External parameter offset
            if (!read24(track, address)) return fail(PCJamResult::Truncated);
            break;
        case 0xDD: { // Bus connect
            u8 bus;
            if (!read8(track, bus) || !read16(track, value))
                return fail(PCJamResult::Truncated);
            break;
        }
        case 0xEF: { // Pan calculation modes
            static constexpr u8 parentTypes[] = { 0, 1, 2, 0, 2, 0, 2, 2 };
            for (size_t i = 0; i < track.parentPanCalcTypes.size(); ++i) {
                u8 mode;
                if (!read8(track, mode)) return fail(PCJamResult::Truncated);
                track.parentPanCalcTypes[i] = parentTypes[std::min<u8>(mode >> 5, 7)];
            }
            break;
        }
        case 0xE7: // SyncCPU -> TrackReceive
            if (!read16(track, value)) return fail(PCJamResult::Truncated);
            noteActionFinished(index, value);
            track.registers[3] = 0;
            break;
        case 0xE8: // FlushAll
        case 0xE9: { // FlushRelease
            for (u8 voice = 0; voice < 8; ++voice) {
                if (!(track.activeNotes & static_cast<u8>(1u << voice))) continue;
                PCJamEvent event;
                event.type = PCJamEventType::NoteOff;
                event.track = static_cast<u8>(index);
                event.voice = voice;
                event.release = opcode == 0xE9 ? 20 : 0;
                events.push_back(event);
            }
            track.activeNotes = 0;
            track.noteTimers.fill(0);
            break;
        }
        case 0xEA: // Wait3
            if (!read16(track, value)) return fail(PCJamResult::Truncated);
            track.wait = value;
            if (track.wait) return PCJamResult::Ok;
            break;
        case 0xEB: { // PanPowSet
            u8 ignored;
            for (int i = 0; i < 5; ++i)
                if (!read8(track, ignored)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xEC: { // IIRSet
            for (int i = 0; i < 4; ++i)
                if (!read16(track, value)) return fail(PCJamResult::Truncated);
            break;
        }
        case 0xFA: // CheckWave: zero means the native wave bank is ready.
            if (!read16(track, value)) return fail(PCJamResult::Truncated);
            track.registers[3] = 0;
            break;
        case 0xFB: { // Debug printf: zero-terminated format, then one byte per conversion.
            size_t arguments = 0;
            bool escaped = false;
            for (size_t i = 0; i < 0x80; ++i) {
                u8 character;
                if (!read8(track, character)) return fail(PCJamResult::Truncated);
                if (character == 0) break;
                if (escaped) {
                    escaped = false;
                } else if (character == '\\') {
                    escaped = true;
                } else if (character == '%') {
                    u8 conversion;
                    if (!read8(track, conversion)) return fail(PCJamResult::Truncated);
                    if (conversion == 0) break;
                    if (arguments < 4) ++arguments;
                    ++i;
                }
            }
            for (size_t i = 0; i < arguments; ++i) {
                u8 ignored;
                if (!read8(track, ignored)) return fail(PCJamResult::Truncated);
            }
            break;
        }
        case 0xFC: // Nop
            break;
        case 0xFD:
            if (!read16(track, track.tempo)) return fail(PCJamResult::Truncated);
            if (track.parent >= 0) track.needsTempoSync = true;
            break;
        case 0xFE:
            if (!read16(track, track.timeBase)) return fail(PCJamResult::Truncated);
            break;
        case 0xFF:
            deactivateTrack(index, events);
            break;
        default: {
            // Same reasoning as the 0xB0 family: consume the operands the
            // Arglist table says this command has, so a command the port does
            // not implement is skipped rather than killing the sequence. Two
            // sound-effect handlers open with OscFull (0xF2), which used to
            // stop them dead.
            const CommandArguments& layout = kCommandArguments[opcode - 0xC0];
            u16 argumentTypes = layout.types;
            for (u8 i = 0; i < layout.count; ++i) {
                u8 byteOperand;
                u16 wordOperand;
                u32 wideOperand;
                switch (argumentTypes & 3) {
                case 0:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    break;
                case 1:
                    if (!read16(track, wordOperand)) return fail(PCJamResult::Truncated);
                    break;
                case 2:
                    if (!read24(track, wideOperand)) return fail(PCJamResult::Truncated);
                    break;
                default:
                    if (!read8(track, byteOperand)) return fail(PCJamResult::Truncated);
                    break;
                }
                argumentTypes = static_cast<u16>(argumentTypes >> 2);
            }
            break;
        }
        }
    }
    return PCJamResult::Ok;
}

PCJamResult PCJamPlayer::tick(std::vector<PCJamEvent>& events, size_t maxOperations) {
    events.clear();
    if (mResult != PCJamResult::Ok) return mResult;
    size_t operations = 0;
    for (size_t i = 0; i < mTracks.size(); ++i) {
        Track& track = mTracks[i];
        if (track.active && track.parent >= 0 && track.needsTempoSync) {
            const Track& parent = mTracks[static_cast<size_t>(track.parent)];
            const float ratio = parent.tempo
                ? std::min(1.0f, static_cast<float>(track.tempo) / parent.tempo)
                : 1.0f;
            track.tempoAccumulator += ratio;
            if (track.tempoAccumulator < 1.0f) continue;
            track.tempoAccumulator -= 1.0f;
        }
        const PCJamResult result = runTrack(i, events, operations, maxOperations);
        if (result != PCJamResult::Ok) return result;
    }
    // JAudio applies timed track parameters to channels which are already
    // sounding.  NoteOn-only snapshots leave sustained instruments frozen at
    // their initial volume/pan/pitch, which is especially audible as fixed
    // tones in select.jam.  Publish the current mix for every live voice after
    // all tracks have advanced this tick.
    for (size_t i = 0; i < mTracks.size(); ++i) {
        const Track& track = mTracks[i];
        if (!track.active) continue;
        for (u8 voice = 0; voice < 8; ++voice) {
            if (!(track.activeNotes & static_cast<u8>(1u << voice))) continue;
            PCJamEvent event;
            event.type = PCJamEventType::VoiceUpdate;
            event.track = static_cast<u8>(i);
            event.voice = voice;
            eventMix(i, event);
            events.push_back(event);
        }
    }
    if (!active()) mResult = PCJamResult::Finished;
    return mResult;
}
