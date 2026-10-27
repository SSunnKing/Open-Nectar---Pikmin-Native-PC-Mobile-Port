// Host-run test of the PKNI record/replay runtime (pc_input_log_tick) with the
// v4 local slots and the ext bits the local capture hook files offline. The
// input log reads its switches once per process, so each
// mode runs in its own process (ctest chains them with a fixture):
//   record <file>             deterministic mode, live local input -> v4 file
//   replay <file> <record>    deterministic mode, replays <file> while live
//                             input differs, re-records it to <record>
//   record-nondet <file>      deterministic mode off -> v2 file, as before
//   replay-v3 <file> <record> deterministic mode, a v3 replay records v3 and
//                             keeps the local slots empty despite live input
//   memory                    deterministic mode, no file: the record sink and
//                             an in-memory replay that hands back to live input
// Engine-free: links pc_input_log.cpp and the deterministic-mode switch.

#include "netplay/pc_input_log.h"

#include "Dolphin/pad.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_state_hash.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static PADStatus sStubPads[4];
PADStatus* pc_netplay_pad_status(void) { return sStubPads; }
void pc_state_hash_before_first_tick(void) {}

namespace {
int sFailures = 0;

void check(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

constexpr int kTicks = 60;

uint32_t bitsOf(float f)
{
	uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	return u;
}

float floatOf(uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

// The scripted live input of tick t: pad 0 drives the cursor with awkward
// floats (signed zero, a subnormal, values with no short decimal form), the
// wheel turns now and then, and pad 1 sometimes has First Person on. Both
// press Bomb, Lock-On and Charge now and then and hold Swarm for stretches.
PcLocalInput scriptedLocal(int t, int pad)
{
	PcLocalInput in;
	std::memset(&in, 0, sizeof(in));
	if (pad == 0) {
		static const uint32_t dx[] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x3DCCCCCDu, 0xC1A80000u, 0x40490FDBu };
		in.flags      = pc_input_log::kLocalMouseCursor;
		if (t % 13 == 6) in.flags |= pc_input_log::kLocalGyroRecenter;
		if (t == 20 || t == 41) in.flags |= pc_input_log::kLocalPhotoToggle;
		in.cursorDx   = floatOf(dx[t % 6]);
		in.cursorDy   = (float)t * -0.1f + 0.3f;
		in.wheelSteps = (int8_t)((t % 5 == 0) ? (t % 10 == 0 ? 1 : -2) : 0);
		if (t % 4 == 1) in.ext |= pc_input_log::kExtBomb;
		if (t % 6 == 2) in.ext |= pc_input_log::kExtLockOn;
		if (t % 9 == 3) in.ext |= pc_input_log::kExtCharge;
		if ((t / 8) % 2 == 1) in.ext |= pc_input_log::kExtSwarm;
	} else if (pad == 1) {
		if (t % 7 == 0) in.flags = pc_input_log::kLocalFirstPerson;
		if (t % 11 >= 5) in.flags |= pc_input_log::kLocalPadDpadDown;
		if (t % 5 == 4) in.ext = pc_input_log::kExtBomb | pc_input_log::kExtSwarm;
	}
	return in;
}

void scriptedPads(int t)
{
	for (int p = 0; p < 4; ++p) {
		std::memset(&sStubPads[p], 0, sizeof(PADStatus));
		sStubPads[p].err = (p == 0) ? 0 : -1;
	}
	sStubPads[0].button = (uint16_t)(t * 37);
	sStubPads[0].stickX = (int8_t)(t - 30);
}

int sTick      = 0;
int sHookCalls = 0;

void scriptedHook(PcLocalInput out[4])
{
	++sHookCalls;
	for (int p = 0; p < 4; ++p) out[p] = scriptedLocal(sTick, p);
}

// Live input that a replay must never let through.
void junkHook(PcLocalInput out[4])
{
	++sHookCalls;
	for (int p = 0; p < 4; ++p) {
		out[p].flags      = pc_input_log::kLocalKnown;
		out[p].wheelSteps = 7;
		out[p].cursorDx   = 999.0f;
		out[p].cursorDy   = -999.0f;
		out[p].ext        = pc_input_log::kExtKnown;
	}
}

std::vector<uint8_t> readFile(const char* path)
{
	std::vector<uint8_t> bytes;
	FILE* f = std::fopen(path, "rb");
	if (f == nullptr) return bytes;
	uint8_t buf[4096];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
	std::fclose(f);
	return bytes;
}

void start(std::vector<std::string>& args)
{
	std::vector<char*> argv;
	for (std::string& a : args) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	pc_netplay_det_init((int)args.size(), argv.data());
	pc_input_log_notify_argv((int)args.size(), argv.data());
}

// The slots as the sim sees them this tick (taking the delta and notches).
void readSlots(int pad, PcLocalInput& got)
{
	got.flags = pc_netplay_local_flags(pad);
	pc_netplay_take_cursor_delta(pad, &got.cursorDx, &got.cursorDy);
	got.wheelSteps = (int8_t)pc_netplay_take_wheel_steps(pad);
	got.ext        = pc_netplay_ext_bits(pad);
}

bool sameLocal(const PcLocalInput& a, const PcLocalInput& b)
{
	return a.flags == b.flags && a.wheelSteps == b.wheelSteps && bitsOf(a.cursorDx) == bitsOf(b.cursorDx)
	    && bitsOf(a.cursorDy) == bitsOf(b.cursorDy) && a.ext == b.ext;
}

// The free-camera rotation the camera queues for pad 2 during tick t's sim,
// in whole 1/65536 turns, and what the sim therefore reads on tick t (the
// rotation queued on the previous tick).
int16_t scriptedTurn(int t) { return (int16_t)((t % 3 == 0) ? t * 17 - 300 : 0); }
int16_t turnSeenOn(int t) { return t > 0 ? scriptedTurn(t - 1) : 0; }

int runRecord(const char* file)
{
	std::vector<std::string> args = { "test", "--netplay-deterministic", "--input-record", file };
	start(args);
	check(pc_netplay_deterministic(), "record: deterministic mode on");
	pc_input_log_set_local_capture_fn(&scriptedHook);
	for (sTick = 0; sTick < kTicks; ++sTick) {
		scriptedPads(sTick);
		pc_input_log_tick();
		check(pc_netplay_local_slots_filled(), "record: slots filled during the tick");
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			readSlots(p, got);
			check(sameLocal(got, scriptedLocal(sTick, p)), "record: live tick feeds the sim the hook's input");
		}
		check(pc_netplay_ext_cursor_rotate(2) == turnSeenOn(sTick) && pc_netplay_ext_cursor_rotate(0) == 0,
		      "record: last tick's queued camera rotation reaches its pad");
		// The camera queues this tick's drag during the sim.
		pc_input_log_pad_rotate_add(2, (float)scriptedTurn(sTick) * pc_input_log::kYawTurnsToRad);
		pc_input_log_tick_end();
		check(!pc_netplay_local_slots_filled(), "record: slots spent after the tick");
	}
	check(pc_input_log_is_record_active(), "record: record active");
	pc_input_log_close();
	check(sHookCalls == kTicks, "record: hook ran once per tick");

	const std::vector<uint8_t> bytes = readFile(file);
	uint16_t version = 0, pads = 0, rec = 0;
	check(pc_input_log_read_header(bytes.data(), bytes.size(), version, pads, rec) == PC_INPUT_HEADER_OK,
	      "record: header reads");
	check(version == 4 && rec == 104, "record: deterministic mode records v4");
	check(bytes.size() == 10 + (size_t)kTicks * 104, "record: one 104-byte record per tick");
	for (int t = 0; t < kTicks && bytes.size() >= 10 + (size_t)(t + 1) * 104; ++t) {
		PcInputPad decoded[4];
		check(pc_input_log_decode_tick_v4(bytes.data() + 10 + (size_t)t * 104, 104, decoded), "record: decodes");
		scriptedPads(t);
		check(decoded[0].button == sStubPads[0].button && decoded[0].stickX == sStubPads[0].stickX,
		      "record: pads filed");
		for (int p = 0; p < 4; ++p) {
			const PcLocalInput want = scriptedLocal(t, p);
			PcLocalInput got;
			got.flags      = decoded[p].local;
			got.wheelSteps = decoded[p].wheelSteps;
			got.cursorDx   = decoded[p].cursorDx;
			got.cursorDy   = decoded[p].cursorDy;
			got.ext        = decoded[p].flags;
			check(sameLocal(got, want), "record: local input and ext bits filed bit for bit");
			check(decoded[p].cursorRotate == (p == 2 ? turnSeenOn(t) : 0), "record: camera rotation filed");
		}
	}
	return 0;
}

int runReplay(const char* file, const char* record)
{
	std::vector<std::string> args = { "test", "--netplay-deterministic", "--input-replay", file, "--input-record",
		                              record };
	start(args);
	pc_input_log_set_local_capture_fn(&junkHook);
	const int extra = 5;
	for (sTick = 0; sTick < kTicks + extra; ++sTick) {
		// Live pads differ from the file: the replay must overwrite them.
		for (int p = 0; p < 4; ++p) std::memset(&sStubPads[p], 0x11, sizeof(PADStatus));
		pc_input_log_tick();
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			readSlots(p, got);
			if (sTick < kTicks) {
				check(sameLocal(got, scriptedLocal(sTick, p)), "replay: the file's input reaches the sim");
			} else {
				PcLocalInput zero;
				std::memset(&zero, 0, sizeof(zero));
				check(sameLocal(got, zero), "replay: past the end the local slots are neutral");
			}
		}
		if (sTick < kTicks) {
			check(pc_netplay_pad_status()[0].button == (uint16_t)(sTick * 37), "replay: pads come from the file");
		}
		check(pc_netplay_ext_cursor_rotate(2) == (sTick < kTicks ? turnSeenOn(sTick) : 0),
		      "replay: the file's camera rotation reaches the sim, never the live drag");
		// A live drag during the replay is drained, never applied.
		pc_input_log_pad_rotate_add(2, 900.0f * pc_input_log::kYawTurnsToRad);
		pc_input_log_tick_end();
	}
	pc_input_log_close();
	check(sHookCalls == kTicks + extra, "replay: the live hook still runs (drains) every tick");
	const std::vector<uint8_t> src = readFile(file);
	const std::vector<uint8_t> out = readFile(record);
	check(out.size() == 10 + (size_t)(kTicks + extra) * 104, "replay: re-record covers every tick");
	check(out.size() >= src.size() && std::memcmp(out.data(), src.data(), src.size()) == 0,
	      "replay: re-record reproduces the v4 file byte for byte");
	return 0;
}

int runRecordNondet(const char* file)
{
	std::vector<std::string> args = { "test", "--input-record", file };
	start(args);
	check(!pc_netplay_deterministic(), "nondet: deterministic mode off");
	for (sTick = 0; sTick < 10; ++sTick) {
		scriptedPads(sTick);
		pc_input_log_tick();
		pc_input_log_tick_end();
	}
	pc_input_log_close();
	const std::vector<uint8_t> bytes = readFile(file);
	uint16_t version = 0, pads = 0, rec = 0;
	check(pc_input_log_read_header(bytes.data(), bytes.size(), version, pads, rec) == PC_INPUT_HEADER_OK
	          && version == 2 && rec == 56,
	      "nondet: records v2 as before");
	check(bytes.size() == 10 + 10 * 56, "nondet: 56-byte records");
	return 0;
}

int runReplayV3(const char* file, const char* record)
{
	// Write a v3 file with the pure API first.
	{
		std::vector<uint8_t> bytes;
		pc_input_log_write_header_v3(bytes);
		for (int t = 0; t < 20; ++t) {
			PcInputPad pads[4];
			std::memset(pads, 0, sizeof(pads));
			pads[0].button       = (uint16_t)(t * 3);
			pads[0].controlYaw   = (uint16_t)(t * 1000);
			pads[0].flags        = (uint8_t)(t & pc_input_log::kExtKnown);
			pads[0].cursorRotate = (int16_t)(-t);
			for (int p = 1; p < 4; ++p) pads[p].err = -1;
			uint8_t rec[64];
			pc_input_log_encode_tick_v3(pads, rec);
			bytes.insert(bytes.end(), rec, rec + 64);
		}
		FILE* f = std::fopen(file, "wb");
		if (f == nullptr) {
			std::printf("FAIL: cannot write %s\n", file);
			return 1;
		}
		std::fwrite(bytes.data(), 1, bytes.size(), f);
		std::fclose(f);
	}
	std::vector<std::string> args = { "test", "--netplay-deterministic", "--input-replay", file, "--input-record",
		                              record };
	start(args);
	pc_input_log_set_local_capture_fn(&junkHook);
	for (sTick = 0; sTick < 20; ++sTick) {
		pc_input_log_tick();
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			readSlots(p, got);
			PcLocalInput zero;
			std::memset(&zero, 0, sizeof(zero));
			zero.ext = p == 0 ? (uint8_t)(sTick & pc_input_log::kExtKnown) : 0; // from the file
			check(sameLocal(got, zero), "v3 replay: no live local input reaches the sim");
		}
		check(pc_netplay_ext_cursor_rotate(0) == (int16_t)(-sTick), "v3 replay: ext rotate still replays");
		check(pc_netplay_ext_bits(0) == (uint8_t)(sTick & pc_input_log::kExtKnown),
		      "v3 replay: the file's ext bits reach the sim, not the live presses");
		pc_input_log_tick_end();
	}
	pc_input_log_close();
	const std::vector<uint8_t> src = readFile(file);
	const std::vector<uint8_t> out = readFile(record);
	check(out == src, "v3 replay: re-record is v3 and reproduces the file byte for byte");
	return 0;
}
std::vector<uint8_t> sSunk;
int sSinkCalls = 0;

void sink(const uint8_t record[104])
{
	++sSinkCalls;
	sSunk.insert(sSunk.end(), record, record + 104);
}

int runMemory()
{
	std::vector<std::string> args = { "test", "--netplay-deterministic" };
	start(args);
	pc_input_log_set_record_sink(&sink);
	pc_input_log_set_local_capture_fn(&scriptedHook);
	// Live: the sink receives exactly what the sim was fed.
	for (sTick = 0; sTick < kTicks; ++sTick) {
		scriptedPads(sTick);
		pc_input_log_tick();
		pc_input_log_tick_end();
	}
	check(!pc_input_log_is_record_active() && !pc_input_log_is_replay_active(), "memory: no file involved");
	check(sSinkCalls == kTicks && sSunk.size() == (size_t)kTicks * 104, "memory: one sunk record per live tick");
	for (int t = 0; t < kTicks && sSunk.size() >= (size_t)(t + 1) * 104; ++t) {
		PcInputPad decoded[4];
		pc_input_log_decode_tick_v4(sSunk.data() + (size_t)t * 104, 104, decoded);
		scriptedPads(t);
		check(decoded[0].button == sStubPads[0].button, "memory: sunk pads");
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			got.flags      = decoded[p].local;
			got.wheelSteps = decoded[p].wheelSteps;
			got.cursorDx   = decoded[p].cursorDx;
			got.cursorDy   = decoded[p].cursorDy;
			got.ext        = decoded[p].flags;
			check(sameLocal(got, scriptedLocal(t, p)), "memory: sunk local input bit for bit");
		}
	}

	// Replay the first K ticks from memory while the live input is junk. The
	// replay begins in the middle of a tick, as the day start that begins it
	// runs inside the sim: that tick keeps its live input and the next tick
	// takes the first record.
	const int K                     = 40;
	const std::vector<uint8_t> live = sSunk;
	scriptedPads(kTicks);
	pc_input_log_tick();
	check(pc_input_log_replay_memory_begin(live.data(), K), "memory: replay begins");
	pc_input_log_tick_end();
	sSunk.clear();
	sSinkCalls = 0;
	check(pc_input_log_replay_memory_active() && pc_input_log_replay_memory_fed() == 0,
	      "memory: a replay begun mid-tick has fed nothing at that tick's end");
	pc_input_log_set_local_capture_fn(&junkHook);
	for (sTick = 0; sTick < K; ++sTick) {
		for (int p = 0; p < 4; ++p) std::memset(&sStubPads[p], 0x11, sizeof(PADStatus));
		pc_input_log_tick();
		check(pc_netplay_pad_status()[0].button == (uint16_t)(sTick * 37), "memory replay: pads from memory");
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			readSlots(p, got);
			check(sameLocal(got, scriptedLocal(sTick, p)), "memory replay: the recorded input reaches the sim");
		}
		pc_input_log_tick_end();
		check(pc_input_log_replay_memory_fed() == (size_t)sTick + 1, "memory replay: one record consumed per tick");
	}
	check(sSunk.size() == (size_t)K * 104 && std::memcmp(sSunk.data(), live.data(), sSunk.size()) == 0,
	      "memory replay: the sink re-records the replayed ticks byte for byte");
	// Past the last record and before the handoff: neutral, never live.
	for (int p = 0; p < 4; ++p) std::memset(&sStubPads[p], 0x11, sizeof(PADStatus));
	pc_input_log_tick();
	check(pc_netplay_pad_status()[0].button == 0 && pc_netplay_pad_status()[1].err == -1,
	      "memory replay: past its end the pads are neutral");
	for (int p = 0; p < 4; ++p) {
		PcLocalInput got, zero;
		std::memset(&zero, 0, sizeof(zero));
		readSlots(p, got);
		check(sameLocal(got, zero), "memory replay: past its end the local slots are neutral");
	}
	pc_input_log_tick_end();
	check(pc_input_log_replay_memory_fed() == (size_t)K, "memory replay: the count stops at the last record");

	// The handoff: the next tick is live again and recorded.
	pc_input_log_replay_memory_end();
	check(!pc_input_log_replay_memory_active(), "memory: replay over");
	pc_input_log_set_local_capture_fn(&scriptedHook);
	sSunk.clear();
	for (sTick = K; sTick < K + 5; ++sTick) {
		scriptedPads(sTick);
		pc_input_log_tick();
		for (int p = 0; p < 4; ++p) {
			PcLocalInput got;
			readSlots(p, got);
			check(sameLocal(got, scriptedLocal(sTick, p)), "memory handoff: live input reaches the sim again");
		}
		check(pc_netplay_pad_status()[0].button == (uint16_t)(sTick * 37), "memory handoff: live pads");
		pc_input_log_tick_end();
	}
	check(sSunk.size() == 5 * 104, "memory handoff: live ticks are recorded again");

	// The camera rotation remainder can be read and handed over.
	float rem[4] = { 0.25f, -0.5f, 0.0f, 0.125f };
	pc_input_log_pad_rotate_set(rem);
	float back[4] = {};
	pc_input_log_pad_rotate_get(back);
	check(std::memcmp(rem, back, sizeof(rem)) == 0, "memory: rotate remainder round trip");
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc == 2 && std::string(argv[1]) == "memory") {
		runMemory();
		if (sFailures == 0) {
			std::printf("pc_input_log_runtime_test memory: all checks passed\n");
			return 0;
		}
		std::printf("pc_input_log_runtime_test memory: %d failure(s)\n", sFailures);
		return 1;
	}
	if (argc < 3) {
		std::printf("usage: %s record|replay|record-nondet|replay-v3 <file> [<record>] | memory\n", argv[0]);
		return 2;
	}
	const std::string mode = argv[1];
	if (mode == "record") runRecord(argv[2]);
	else if (mode == "replay" && argc >= 4) runReplay(argv[2], argv[3]);
	else if (mode == "record-nondet") runRecordNondet(argv[2]);
	else if (mode == "replay-v3" && argc >= 4) runReplayV3(argv[2], argv[3]);
	else {
		std::printf("unknown mode %s\n", mode.c_str());
		return 2;
	}
	if (sFailures == 0) {
		std::printf("pc_input_log_runtime_test %s: all checks passed\n", mode.c_str());
		return 0;
	}
	std::printf("pc_input_log_runtime_test %s: %d failure(s)\n", mode.c_str(), sFailures);
	return 1;
}
