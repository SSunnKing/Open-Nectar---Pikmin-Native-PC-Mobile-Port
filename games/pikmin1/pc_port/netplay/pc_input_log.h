#pragma once
// Input record/replay for the netplay determinism harness.
//
// File format (little-endian binary, "PKNI" v1/v2/v3/v4):
//   offset  size  field
//   0       4     magic: 'P' 'K' 'N' 'I'
//   4       2     version: u16, 1, 2, 3 or 4
//   6       2     pad count: u16, currently 4
//   8       2     record size: u16, bytes per tick (44 for v1, 56 for v2,
//                 64 for v3, 104 for v4)
//   10      N*rec tick records, one per tick, tick index = file order
//
// A v1 record holds the 4 pads in channel order, 11 PADStatus bytes each. A
// v2 record holds 14 bytes per pad: the same 11 bytes followed by
//   11      2     controlYaw: u16 LE, camera control yaw in 1/65536 turns
//   13      1     flags: u8, always 0 in v2; the ext bits in v3 and v4
// A v3 record holds 16 bytes per pad: the v2 14 followed by
//   14      2     cursorRotate: s16 LE, cursor rotation in 1/65536 turns
// A v4 record holds 26 bytes per pad: the v3 16 followed by the local
// inputs of the Mouse Cursor path, which live outside PADStatus:
//   16      1     local: u8, kLocal* bits
//   17      1     wheelSteps: s8, colour-pick wheel notches this tick
//   18      4     cursorDx: u32 LE, IEEE-754 binary32 bits of the Mouse
//                 Cursor delta this tick (window units, SDL x)
//   22      4     cursorDy: u32 LE, the same for SDL y (down is positive)
// Every field is serialised explicitly, never as a raw struct. The deltas
// travel as their bit patterns, so a replay feeds the sim the exact floats
// the live run fed it.
//
// Ext bits and cursor rotate (v3, and the netplay input bytes 10-12): the
// Bomb, Lock-On and Charge presses, the held Swarm button and the cursor
// rotation of a free-camera drag live outside PADStatus. In deterministic
// mode the sim reads them from these per-pad slots only: a session fills
// them, a v3 or v4 replay fills them from the file, and an offline live tick
// fills the ext bits through the local capture hook. Outside deterministic
// mode they are never read.
//
// Control yaw: in deterministic mode the camera yaw each player saw when
// they pushed the stick is part of that player's per-tick input, so the sim
// never reads a camera. Quantisation:
//   yaw = round(wrap(angle / 2pi) * 65536) & 0xFFFF
//   angle = yaw * 2pi / 65536
// The sim builds its basis from the quantised value only, so record and
// replay compute bit-identical values.
//
// Local inputs (v4): in deterministic mode the sim reads the Mouse Cursor
// path from these per-pad slots only: whether the pad's cursor follows the
// mouse this tick, the cursor delta, the wheel's colour-pick notches and
// whether First Person pins the cursor. It also reads from them whether the
// D-pad Down in the pad came from a gamepad, the Gyro Recenter press and
// (pad 0) the photo mode toggle (kLocal* bits). A live tick fills them through the
// local capture hook (the engine takes what the window gathered since the
// last tick); a v4 replay fills them from the file. Either way the sim
// runs the same code on the same values. The sim takes the delta and the
// notches once per tick; what no reader takes is dropped at the tick's end.
// Outside deterministic mode the hook takes nothing and the sim never reads
// the slots.
//
// Runtime behaviour:
//   PIKMIN_INPUT_RECORD=<file> (or --input-record <file>): before each tick's
//     simulation, append the 4 pads plus yaw/flags. The record has the
//     version of the file being replayed (v2 for a v1 replay); without a
//     replay it is v4 in deterministic mode and v2 outside it.
//   PIKMIN_INPUT_REPLAY=<file> (or --input-replay <file>): each tick,
//     overwrite the 4 polled pads with the recorded ones and feed the
//     recorded yaw (v2+), ext slots (v3+) and local slots (v4) to the sim.
//     Past the end of the file the pads and local slots are neutral (pad 0
//     connected, pads 1-3 no controller). A replay that cannot be loaded
//     exits the process with code 3, so a harness can never mistake a live
//     run for a replay.
//   Record and replay may be combined, even on the same path: the replay is
//     loaded into memory first, and recording a replayed v2, v3 or v4 run
//     reproduces the replay file byte for byte.
//   Covered: PADStatus, yaw, (v3+) the ext slots and (v4) the local slots,
//     which offline also carry the touch colour taps (as wheel notches), the
//     gamepad's D-pad Down, the gyro recenter and the photo mode toggle.
//     Inputs outside them (the absolute-mode virtual cursor) are neutralised
//     in deterministic mode rather than recorded. A netplay session carries
//     only the pads, the yaw and the ext slots.
//   With neither switch set no file is touched and nothing is printed. The
//     yaw and local slots are still cleared and the capture hooks still run
//     each tick, so deterministic mode without a file gets fresh input every
//     tick.
//
// Tick split: pc_input_log_tick() runs after PADRead and before the sim; it
// clears the slots, applies the replay, runs the capture hooks and writes
// the record. pc_input_log_tick_end() runs after the sim, clears the local
// slots and advances the tick index.

#include <cstddef>
#include <cstdint>
#include <vector>

struct PcInputPad {
	uint16_t button;
	int8_t stickX;
	int8_t stickY;
	int8_t substickX;
	int8_t substickY;
	uint8_t triggerLeft;
	uint8_t triggerRight;
	uint8_t analogA;
	uint8_t analogB;
	int8_t err;
	// v2 and v3; zero for v1 decodes.
	uint16_t controlYaw;
	uint8_t flags;
	// v3 and v4; zero for v1 and v2 decodes.
	int16_t cursorRotate;
	// v4 only; zero for v1 to v3 decodes.
	uint8_t local;
	int8_t wheelSteps;
	float cursorDx;
	float cursorDy;
};

// One pad's local inputs for one tick, as the local capture hook reports
// them (the v4 fields of PcInputPad, plus the ext bits of the v3 flags).
struct PcLocalInput {
	uint8_t flags;      // pc_input_log::kLocal* bits
	int8_t wheelSteps;  // colour-pick wheel notches since the last tick
	float cursorDx;     // Mouse Cursor delta since the last tick
	float cursorDy;
	uint8_t ext;        // pc_input_log::kExt* bits: presses since the last tick, Swarm held now
};

namespace pc_input_log {
constexpr uint8_t kMagic[4]       = { 'P', 'K', 'N', 'I' };
constexpr uint16_t kVersion       = 2;
constexpr uint16_t kPadCount      = 4;
constexpr size_t kPadBytes        = 14;
constexpr size_t kRecordBytes     = kPadCount * kPadBytes; // 56
constexpr size_t kHeaderBytes     = 10;
constexpr int8_t kErrConnected    = 0;
constexpr int8_t kErrNoController = -1;
constexpr uint16_t kVersionV1     = 1;
constexpr size_t kPadBytesV1      = 11;
constexpr size_t kRecordBytesV1   = kPadCount * kPadBytesV1; // 44
constexpr float kYawTurnsToRad    = 6.28318530717958647692f / 65536.0f;
constexpr uint8_t kFlagsNone      = 0;
constexpr uint16_t kVersionV3     = 3;
constexpr size_t kPadBytesV3      = 16;
constexpr size_t kRecordBytesV3   = kPadCount * kPadBytesV3; // 64
constexpr uint8_t kExtBomb        = 0x01; // Bomb press
constexpr uint8_t kExtLockOn      = 0x02; // Lock-On press
constexpr uint8_t kExtCharge      = 0x04; // Charge press (the Swarm button's press)
constexpr uint8_t kExtSwarm       = 0x08; // Swarm button held
constexpr uint8_t kExtKnown       = 0x0F;
constexpr uint16_t kVersionV4     = 4;
constexpr size_t kPadBytesV4      = 26;
constexpr size_t kRecordBytesV4   = kPadCount * kPadBytesV4; // 104
constexpr uint8_t kLocalMouseCursor = 0x01; // the pad's cursor follows the mouse delta this tick
constexpr uint8_t kLocalFirstPerson = 0x02; // First Person is on: the cursor is pinned ahead of the view
constexpr uint8_t kLocalPadDpadDown = 0x04; // D-pad Down held on this player's gamepad (its swarm button, not a lie-down)
constexpr uint8_t kLocalGyroRecenter = 0x08; // Gyro Recenter pressed: the cursor goes back in front of the captain
constexpr uint8_t kLocalPhotoToggle = 0x10; // photo mode toggled (pad 0 only): it freezes the sim
constexpr uint8_t kLocalKnown       = 0x1F;
} // namespace pc_input_log

// Pure encode/decode API (no globals, no files).
size_t pc_input_log_encode_tick(const PcInputPad pads[4], uint8_t out[56]);
bool pc_input_log_decode_tick(const uint8_t* data, size_t avail, PcInputPad pads[4]);
// v1: 44-byte records; encode ignores controlYaw/flags, decode zeroes them.
size_t pc_input_log_encode_tick_v1(const PcInputPad pads[4], uint8_t out[44]);
bool pc_input_log_decode_tick_v1(const uint8_t* data, size_t avail, PcInputPad pads[4]);

// v3: 64-byte records with the ext bits (flags) and the cursor rotate.
size_t pc_input_log_encode_tick_v3(const PcInputPad pads[4], uint8_t out[64]);
bool pc_input_log_decode_tick_v3(const uint8_t* data, size_t avail, PcInputPad pads[4]);

// v4: 104-byte records, the v3 fields plus the local inputs.
size_t pc_input_log_encode_tick_v4(const PcInputPad pads[4], uint8_t out[104]);
bool pc_input_log_decode_tick_v4(const uint8_t* data, size_t avail, PcInputPad pads[4]);

// Appends the 10-byte header to out.
void pc_input_log_write_header(std::vector<uint8_t>& out);
void pc_input_log_write_header_v1(std::vector<uint8_t>& out);
void pc_input_log_write_header_v3(std::vector<uint8_t>& out);
void pc_input_log_write_header_v4(std::vector<uint8_t>& out);

enum PcInputHeaderResult {
	PC_INPUT_HEADER_OK          = 0,
	PC_INPUT_HEADER_TRUNCATED   = 1,
	PC_INPUT_HEADER_BAD_MAGIC   = 2,
	PC_INPUT_HEADER_UNSUPPORTED = 3, // version, pad count or record size mismatch
};

// Accepts v1 (1/4/44), v2 (2/4/56), v3 (3/4/64) and v4 (4/4/104); the
// outputs are set only on OK.
PcInputHeaderResult pc_input_log_read_header(const uint8_t* data, size_t len, uint16_t& version,
                                             uint16_t& padCount, uint16_t& recordSize);

// Yaw helpers. sincos uses the same sinf/cosf as Matrix4f::makeRotate, so
// the netplay libm covers both.
uint16_t pc_input_log_yaw_quantise(float angleRad);
float pc_input_log_yaw_to_angle(uint16_t yaw);
void pc_input_log_yaw_sincos(uint16_t yaw, float* sinYaw, float* cosYaw);

// Runtime API. argv must be captured before the first tick; env vars are
// read lazily on the first tick.
void pc_input_log_notify_argv(int argc, char** argv);

// Per-tick hook: after PADRead, before the sim.
void pc_input_log_tick(void);

// Pre-sim yaw capture hook, registered once by the engine (Navi) before the
// first tick. It fills every slot without a replayed value from that pad's
// live control camera.
typedef void (*PcYawCaptureFn)(void);
void pc_input_log_set_yaw_capture_fn(PcYawCaptureFn fn);

// Runs the capture hook now, without any record/replay logic. The _fresh
// variant clears the slots first so the hook refills them from the camera.
void pc_input_log_capture_yaw(void);
void pc_input_log_capture_yaw_fresh(void);

// Post-tick hook: after app->idle() returns.
void pc_input_log_tick_end(void);

// Per-pad yaw for the current tick, for the sim. False when no yaw is set
// (switch off, or no Navi/camera at capture time); in deterministic mode the
// sim then uses the neutral basis (yaw 0), never the camera.
bool pc_netplay_control_yaw(int pad, float* sinYaw, float* cosYaw);

// Slot access for the capture hook (never called from the sim).
void pc_input_log_yaw_set(int pad, uint16_t yaw, uint8_t flags);
bool pc_input_log_yaw_valid(int pad);
uint16_t pc_input_log_yaw_raw(int pad);
uint8_t pc_input_log_yaw_flags(int pad);

// Per-pad ext bits and cursor rotate for the current tick, for the sim in
// deterministic mode. Cleared with the yaw every tick; set by a v3 replay or
// by the session's input injection.
void pc_input_log_ext_set(int pad, uint8_t ext, int16_t cursorRotate);
uint8_t pc_netplay_ext_bits(int pad);
int16_t pc_netplay_ext_cursor_rotate(int pad);
// PIKMIN_NETPLAY_DEBUG_EXT=1: the sim logs every applied ext event. Cached.
bool pc_netplay_ext_trace(void);

// Local capture hook, registered once by the engine (the window layer)
// before the first tick. pc_input_log_tick runs it every tick with out[]
// zeroed; it takes what the window gathered since the last tick (nothing
// outside deterministic mode). Its values fill the local slots and the ext
// bits only on a live tick, not while a replay feeds the tick or after it
// ends.
typedef void (*PcLocalCaptureFn)(PcLocalInput out[4]);
void pc_input_log_set_local_capture_fn(PcLocalCaptureFn fn);

// Per-pad local inputs for the current tick, for the sim in deterministic
// mode. The flags hold for the whole tick; the delta and the notches go to
// the first reader of the tick and read as zero afterwards, as the window
// accumulators they mirror do.
uint8_t pc_netplay_local_flags(int pad);
void pc_netplay_take_cursor_delta(int pad, float* dx, float* dy);
int pc_netplay_take_wheel_steps(int pad);

// True from pc_input_log_tick to pc_input_log_tick_end: this tick's local
// slots and ext bits come from the input log (an offline tick, live or
// replayed). A netplay session's tick never runs pc_input_log_tick, so the
// sim keeps the session's own path for inputs the session does not carry.
bool pc_netplay_local_slots_filled(void);

// Local cursor rotation (this peer's own captain's free-camera drag, in
// radians) waiting for the session's next input. take() quantises to
// 1/65536 turns and keeps the remainder for the next take.
void pc_input_log_local_rotate_add(float angleRad);
int16_t pc_input_log_local_rotate_take(void);

// Offline deterministic mode: a captain's free-camera drag rotation (in
// radians), queued by the camera during a tick. The next live tick's
// capture quantises it like take() above into that pad's cursor rotate (so
// a v4 record carries it); a replayed tick drains it unused.
void pc_input_log_pad_rotate_add(int pad, float angleRad);

bool pc_input_log_is_record_active(void);
bool pc_input_log_is_replay_active(void);

// Flush (every 300 ticks and on exit) / close the record file, if any.
void pc_input_log_flush(void);
void pc_input_log_close(void);
