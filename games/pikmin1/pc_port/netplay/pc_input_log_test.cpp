// Host-run unit test for the PKNI v1-v4 encode/decode API.
// Round-trip (v4, v3, v2 and v1), back-compat, float bit patterns, truncated
// records, bad magic, and the per-tick yaw and local slots.
// Engine-free: links pc_input_log.cpp and the deterministic-mode switch.

#include "netplay/pc_input_log.h"

#include "Dolphin/pad.h"
#include "netplay/pc_state_hash.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

// Link stubs: this test exercises only the pure encode/decode API, but it
// links pc_input_log.cpp whole, so the engine hooks it calls need symbols.
// They are never invoked below.
static PADStatus sStubPads[4];
PADStatus* pc_netplay_pad_status(void) { return sStubPads; }
void pc_state_hash_before_first_tick(void) {}

// Hook probe: counts pre-sim capture calls and stores a fixed yaw.
static int sHookCalls = 0;
static void sTestCaptureHook(void)
{
	++sHookCalls;
	pc_input_log_yaw_set(0, 0x1234, 0);
}

// Probe: mirrors the production Navi hook (navi.cpp), which leaves slots
// already marked valid alone and fills the rest from the live camera.
static void sTestCaptureHookChecked(void)
{
	++sHookCalls;
	if (pc_input_log_yaw_valid(0)) return;
	pc_input_log_yaw_set(0, 0x1234, 0);
}

// Local capture probe: reports a fixed Mouse Cursor input on pad 1, plus a
// bit no version knows yet.
static int sLocalHookCalls = 0;
static void sTestLocalHook(PcLocalInput out[4])
{
	++sLocalHookCalls;
	for (int p = 0; p < 4; ++p) {
		if (out[p].flags != 0 || out[p].wheelSteps != 0 || out[p].cursorDx != 0.0f || out[p].cursorDy != 0.0f) {
			std::printf("FAIL: local hook out[] not zeroed\n");
		}
	}
	out[1].flags      = pc_input_log::kLocalMouseCursor | 0x80;
	out[1].wheelSteps = -3;
	out[1].cursorDx   = 12.5f;
	out[1].cursorDy   = -0.75f;
}

static uint32_t bitsOf(float f)
{
	uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	return u;
}

static float floatOf(uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

namespace {
int sFailures = 0;

void check(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

void fillPads(PcInputPad pads[4])
{
	// Cover edges: full button word, stick extremes, trigger extremes,
	// both err signs, yaw extremes, flags.
	const uint16_t buttons[4] = { 0x0000, 0xFFFF, 0x0100 | 0x0040, 0x1000 };
	const int8_t sticks[4][4] = { { 0, 0, 0, 0 }, { 127, 127, -128, -128 }, { -1, 1, 100, -100 }, { 32, -33, 0, 7 } };
	const uint16_t yaws[4]    = { 0x0000, 0xFFFF, 0x1234, 0x8000 };
	for (int p = 0; p < 4; ++p) {
		pads[p].button       = buttons[p];
		pads[p].stickX       = sticks[p][0];
		pads[p].stickY       = sticks[p][1];
		pads[p].substickX    = sticks[p][2];
		pads[p].substickY    = sticks[p][3];
		pads[p].triggerLeft  = (uint8_t)(p * 85);
		pads[p].triggerRight = (uint8_t)(255 - p * 85);
		pads[p].analogA      = (uint8_t)(17 + p);
		pads[p].analogB      = (uint8_t)(200 - p);
		pads[p].err          = (p == 0) ? (int8_t)0 : (int8_t)-1;
		pads[p].controlYaw   = yaws[p];
		pads[p].flags        = (uint8_t)(p & 1);
		pads[p].cursorRotate = (int16_t)(p * 1000 - 1500);
		pads[p].local        = (uint8_t)(p == 3 ? 0xFF : p);
		pads[p].wheelSteps   = (int8_t)(p == 2 ? -128 : p * 40 - 1);
		pads[p].cursorDx     = 1.5f * (float)p - 2.0f;
		pads[p].cursorDy     = -1000.25f + (float)p;
	}
}

bool padsEqual(const PcInputPad a[4], const PcInputPad b[4], bool withYaw)
{
	// Field-wise: PcInputPad may carry padding bytes that encode/decode
	// never touches.
	for (int p = 0; p < 4; ++p) {
		if (a[p].button != b[p].button || a[p].stickX != b[p].stickX || a[p].stickY != b[p].stickY
		    || a[p].substickX != b[p].substickX || a[p].substickY != b[p].substickY
		    || a[p].triggerLeft != b[p].triggerLeft || a[p].triggerRight != b[p].triggerRight
		    || a[p].analogA != b[p].analogA || a[p].analogB != b[p].analogB || a[p].err != b[p].err) {
			return false;
		}
		if (withYaw && (a[p].controlYaw != b[p].controlYaw || a[p].flags != b[p].flags)) return false;
	}
	return true;
}
} // namespace

int main()
{
	// v2 round-trip through encode/decode.
	PcInputPad pads[4];
	fillPads(pads);
	uint8_t rec[56];
	check(pc_input_log_encode_tick(pads, rec) == 56, "v2 encode returns 56 bytes");
	PcInputPad back[4];
	std::memset(back, 0xAA, sizeof(back));
	check(pc_input_log_decode_tick(rec, sizeof(rec), back), "v2 decode of full record succeeds");
	check(padsEqual(pads, back, true), "v2 round-trip preserves every field incl yaw/flags");

	// Byte layout is explicit little-endian, not a struct dump.
	check(rec[0] == 0x00 && rec[1] == 0x00, "pad0 button LE");
	PcInputPad hi[4];
	std::memset(hi, 0, sizeof(hi));
	hi[1].button     = 0x0100;
	hi[1].controlYaw = 0x0201;
	hi[1].flags      = 0x03;
	uint8_t recHi[56];
	pc_input_log_encode_tick(hi, recHi);
	check(recHi[14] == 0x00 && recHi[15] == 0x01, "button serialised LE, not host order dependent");
	check(recHi[16] == 0 && recHi[14 + 14] == 0, "sticks at fixed offsets");
	// Pad 1 starts at byte 14: 11 PAD bytes, then yaw LE, then flags.
	check(recHi[14 + 11] == 0x01 && recHi[14 + 12] == 0x02, "yaw serialised LE after the 11 PAD bytes");
	check(recHi[14 + 13] == 0x03, "flags byte follows yaw");

	// Truncated file: decode must fail for every short length.
	for (size_t len = 0; len < 56; ++len) {
		PcInputPad tmp[4];
		char what[64];
		std::snprintf(what, sizeof(what), "v2 decode rejects %llu bytes", (unsigned long long)len);
		check(!pc_input_log_decode_tick(rec, len, tmp), what);
	}
	check(!pc_input_log_decode_tick(nullptr, 56, back), "decode rejects null data");
	check(!pc_input_log_decode_tick(rec, 56, nullptr), "decode rejects null pads");

	// v1 compat: encode ignores yaw/flags, decode zeroes them.
	uint8_t recV1[44];
	check(pc_input_log_encode_tick_v1(pads, recV1) == 44, "v1 encode returns 44 bytes");
	PcInputPad backV1[4];
	std::memset(backV1, 0xAA, sizeof(backV1));
	check(pc_input_log_decode_tick_v1(recV1, sizeof(recV1), backV1), "v1 decode succeeds");
	check(padsEqual(pads, backV1, false), "v1 round-trip preserves the 11 PAD fields");
	for (int p = 0; p < 4; ++p) {
		char what[64];
		std::snprintf(what, sizeof(what), "v1 decode zeroes yaw/flags pad %d", p);
		check(backV1[p].controlYaw == 0 && backV1[p].flags == 0, what);
	}
	for (size_t len = 0; len < 44; ++len) {
		PcInputPad tmp[4];
		check(!pc_input_log_decode_tick_v1(recV1, len, tmp), "v1 decode rejects short");
	}

	// v3: the v2 14 bytes per pad plus the cursor rotate s16 LE.
	uint8_t recV3[64];
	check(pc_input_log_encode_tick_v3(pads, recV3) == 64, "v3 encode returns 64 bytes");
	check(std::memcmp(recV3, rec, 14) == 0, "v3 pad 0 starts with the v2 pad bytes");
	check(recV3[14] == (uint8_t)(-1500 & 0xFF) && recV3[15] == (uint8_t)((-1500 >> 8) & 0xFF), "v3 rotate LE");
	PcInputPad backV3[4];
	std::memset(backV3, 0xAA, sizeof(backV3));
	check(pc_input_log_decode_tick_v3(recV3, sizeof(recV3), backV3), "v3 decode succeeds");
	check(padsEqual(pads, backV3, true), "v3 round-trip preserves the v2 fields");
	for (int p = 0; p < 4; ++p) check(backV3[p].cursorRotate == pads[p].cursorRotate, "v3 round-trip keeps the rotate");
	check(!pc_input_log_decode_tick_v3(recV3, 63, backV3), "v3 decode rejects short");
	PcInputPad backV2[4];
	pc_input_log_decode_tick(rec, sizeof(rec), backV2);
	check(backV2[3].cursorRotate == 0, "v2 decode zeroes the rotate");
	std::vector<uint8_t> headerV3;
	pc_input_log_write_header_v3(headerV3);
	uint16_t v3v = 0, v3p = 0, v3r = 0;
	check(pc_input_log_read_header(headerV3.data(), headerV3.size(), v3v, v3p, v3r) == PC_INPUT_HEADER_OK && v3v == 3
	          && v3p == 4 && v3r == 64,
	      "v3 header reads back v3/4/64");

	// v4: the v3 16 bytes per pad, then local u8, wheel s8, dx and dy as
	// binary32 bits u32 LE (26 bytes per pad).
	{
		uint8_t recV4[104];
		std::memset(recV4, 0xCC, sizeof(recV4));
		check(pc_input_log_encode_tick_v4(pads, recV4) == 104, "v4 encode returns 104 bytes");
		for (int p = 0; p < 4; ++p) {
			check(std::memcmp(recV4 + p * 26, recV3 + p * 16, 16) == 0, "v4 pad starts with the v3 pad bytes");
			const uint8_t* tail = recV4 + p * 26 + 16;
			check(tail[0] == pads[p].local, "v4 local byte");
			check(tail[1] == (uint8_t)pads[p].wheelSteps, "v4 wheel byte");
			const uint32_t dx = bitsOf(pads[p].cursorDx);
			const uint32_t dy = bitsOf(pads[p].cursorDy);
			check(tail[2] == (dx & 0xFF) && tail[3] == ((dx >> 8) & 0xFF) && tail[4] == ((dx >> 16) & 0xFF)
			          && tail[5] == (dx >> 24),
			      "v4 dx bits LE");
			check(tail[6] == (dy & 0xFF) && tail[7] == ((dy >> 8) & 0xFF) && tail[8] == ((dy >> 16) & 0xFF)
			          && tail[9] == (dy >> 24),
			      "v4 dy bits LE");
		}
		// A known value pins the byte order: 1.0f is 0x3F800000.
		PcInputPad one[4];
		std::memset(one, 0, sizeof(one));
		one[0].cursorDx = 1.0f;
		uint8_t recOne[104];
		pc_input_log_encode_tick_v4(one, recOne);
		check(recOne[18] == 0x00 && recOne[19] == 0x00 && recOne[20] == 0x80 && recOne[21] == 0x3F,
		      "v4 1.0f serialises as 00 00 80 3F");

		PcInputPad backV4[4];
		std::memset(backV4, 0xAA, sizeof(backV4));
		check(pc_input_log_decode_tick_v4(recV4, sizeof(recV4), backV4), "v4 decode succeeds");
		check(padsEqual(pads, backV4, true), "v4 round-trip preserves the v2 fields");
		for (int p = 0; p < 4; ++p) {
			check(backV4[p].cursorRotate == pads[p].cursorRotate, "v4 round-trip keeps the rotate");
			check(backV4[p].local == pads[p].local && backV4[p].wheelSteps == pads[p].wheelSteps,
			      "v4 round-trip keeps local and wheel");
			check(bitsOf(backV4[p].cursorDx) == bitsOf(pads[p].cursorDx)
			          && bitsOf(backV4[p].cursorDy) == bitsOf(pads[p].cursorDy),
			      "v4 round-trip keeps the delta bits");
		}

		// Exact bit patterns: signed zero, subnormals, extremes, infinities
		// and NaNs with payloads all come back bit for bit.
		const uint32_t patterns[] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x00800000u,
			                          0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u,
			                          0x7FA12345u, 0xFFC0BEEFu, 0x3EAAAAABu, 0xC5800000u, 0x45800000u };
		const size_t count = sizeof(patterns) / sizeof(patterns[0]);
		for (size_t i = 0; i < count; ++i) {
			PcInputPad in[4];
			std::memset(in, 0, sizeof(in));
			for (int p = 0; p < 4; ++p) {
				in[p].cursorDx = floatOf(patterns[(i + (size_t)p) % count]);
				in[p].cursorDy = floatOf(patterns[(i + 7 + (size_t)p) % count]);
			}
			uint8_t buf[104];
			pc_input_log_encode_tick_v4(in, buf);
			PcInputPad out[4];
			pc_input_log_decode_tick_v4(buf, sizeof(buf), out);
			bool same = true;
			for (int p = 0; p < 4; ++p) {
				same = same && bitsOf(out[p].cursorDx) == patterns[(i + (size_t)p) % count]
				    && bitsOf(out[p].cursorDy) == patterns[(i + 7 + (size_t)p) % count];
				const uint8_t* tail = buf + p * 26 + 16;
				const uint32_t onDisk = (uint32_t)tail[2] | ((uint32_t)tail[3] << 8) | ((uint32_t)tail[4] << 16)
				                      | ((uint32_t)tail[5] << 24);
				same = same && onDisk == patterns[(i + (size_t)p) % count];
			}
			char what[64];
			std::snprintf(what, sizeof(what), "v4 float bit pattern set %u round-trips exactly", (unsigned)i);
			check(same, what);
		}

		// Truncated: every length short of a full record is rejected.
		for (size_t len = 0; len < 104; ++len) {
			PcInputPad tmp[4];
			char what[64];
			std::snprintf(what, sizeof(what), "v4 decode rejects %llu bytes", (unsigned long long)len);
			check(!pc_input_log_decode_tick_v4(recV4, len, tmp), what);
		}
		check(!pc_input_log_decode_tick_v4(nullptr, 104, backV4), "v4 decode rejects null data");
		check(!pc_input_log_decode_tick_v4(recV4, 104, nullptr), "v4 decode rejects null pads");

		// The older decoders zero the v4 fields.
		PcInputPad old[4];
		std::memset(old, 0x5A, sizeof(old));
		pc_input_log_decode_tick(rec, sizeof(rec), old);
		bool zeroed = true;
		for (int p = 0; p < 4; ++p) {
			zeroed = zeroed && old[p].local == 0 && old[p].wheelSteps == 0 && bitsOf(old[p].cursorDx) == 0
			      && bitsOf(old[p].cursorDy) == 0;
		}
		check(zeroed, "v2 decode zeroes the v4 fields");
		std::memset(old, 0x5A, sizeof(old));
		pc_input_log_decode_tick_v1(recV1, sizeof(recV1), old);
		zeroed = true;
		for (int p = 0; p < 4; ++p) {
			zeroed = zeroed && old[p].local == 0 && old[p].wheelSteps == 0 && bitsOf(old[p].cursorDx) == 0
			      && bitsOf(old[p].cursorDy) == 0;
		}
		check(zeroed, "v1 decode zeroes the v4 fields");
		std::memset(old, 0x5A, sizeof(old));
		pc_input_log_decode_tick_v3(recV3, sizeof(recV3), old);
		zeroed = true;
		for (int p = 0; p < 4; ++p) {
			zeroed = zeroed && old[p].local == 0 && old[p].wheelSteps == 0 && bitsOf(old[p].cursorDx) == 0
			      && bitsOf(old[p].cursorDy) == 0;
		}
		check(zeroed, "v3 decode zeroes the v4 fields");

		std::vector<uint8_t> headerV4;
		pc_input_log_write_header_v4(headerV4);
		uint16_t v4v = 0, v4p = 0, v4r = 0;
		check(headerV4.size() == 10, "v4 header is 10 bytes");
		check(pc_input_log_read_header(headerV4.data(), headerV4.size(), v4v, v4p, v4r) == PC_INPUT_HEADER_OK
		          && v4v == 4 && v4p == 4 && v4r == 104,
		      "v4 header reads back v4/4/104");
		std::vector<uint8_t> v4wrongSize = headerV4;
		v4wrongSize[8] = 64; // v4 version with the v3 size
		check(pc_input_log_read_header(v4wrongSize.data(), v4wrongSize.size(), v4v, v4p, v4r)
		          == PC_INPUT_HEADER_UNSUPPORTED,
		      "v4 version with a v3 record size rejected");
		std::vector<uint8_t> v3bigSize = headerV3;
		v3bigSize[8] = 104; // v3 version with the v4 size
		check(pc_input_log_read_header(v3bigSize.data(), v3bigSize.size(), v4v, v4p, v4r)
		          == PC_INPUT_HEADER_UNSUPPORTED,
		      "v3 version with a v4 record size rejected");
	}

	// Ext slots and the local rotate accumulator.
	pc_input_log_ext_set(1, pc_input_log::kExtBomb | pc_input_log::kExtSwarm, -42);
	check(pc_netplay_ext_bits(1) == 0x09 && pc_netplay_ext_cursor_rotate(1) == -42 && pc_netplay_ext_bits(0) == 0,
	      "ext slots per pad");
	pc_input_log_ext_set(1, 0, 0);
	const float quarterLsb = pc_input_log::kYawTurnsToRad * 0.25f;
	pc_input_log_local_rotate_add(quarterLsb);
	check(pc_input_log_local_rotate_take() == 0, "a quarter LSB rounds to 0");
	pc_input_log_local_rotate_add(quarterLsb);
	pc_input_log_local_rotate_add(quarterLsb);
	check(pc_input_log_local_rotate_take() == 1, "the remainder carries to the next take");
	pc_input_log_local_rotate_add(-1.0f);
	const int16_t q = pc_input_log_local_rotate_take();
	check(q <= -10429 && q >= -10431, "one radian is about 10430 LSB");

	// Header round-trip (v2 default).
	std::vector<uint8_t> header;
	pc_input_log_write_header(header);
	check(header.size() == 10, "header is 10 bytes");
	check(header[0] == 'P' && header[1] == 'K' && header[2] == 'N' && header[3] == 'I', "header magic");
	uint16_t version = 0, padCount = 0, recordSize = 0;
	check(pc_input_log_read_header(header.data(), header.size(), version, padCount, recordSize)
	              == PC_INPUT_HEADER_OK
	      && version == 2 && padCount == 4 && recordSize == 56, "header reads back v2/4/56");

	// v1 header still reads.
	std::vector<uint8_t> headerV1;
	pc_input_log_write_header_v1(headerV1);
	check(pc_input_log_read_header(headerV1.data(), headerV1.size(), version, padCount, recordSize)
	              == PC_INPUT_HEADER_OK
	      && version == 1 && padCount == 4 && recordSize == 44, "v1 header reads back v1/4/44");

	// Bad magic.
	std::vector<uint8_t> bad = header;
	bad[0]                 = 'X';
	check(pc_input_log_read_header(bad.data(), bad.size(), version, padCount, recordSize)
	          == PC_INPUT_HEADER_BAD_MAGIC,
	      "bad magic rejected");

	// Truncated header.
	for (size_t len = 0; len < 10; ++len) {
		check(pc_input_log_read_header(header.data(), len, version, padCount, recordSize)
		              == PC_INPUT_HEADER_TRUNCATED,
		      "short header rejected");
	}

	// Unsupported version / pad count / record size.
	std::vector<uint8_t> wrong = header;
	wrong[4]                   = 4; // version 4 with the v2 size
	check(pc_input_log_read_header(wrong.data(), wrong.size(), version, padCount, recordSize)
	          == PC_INPUT_HEADER_UNSUPPORTED,
	      "v4 version with the v2 size rejected");
	wrong[4] = 5; // version 5
	wrong[8] = 104;
	check(pc_input_log_read_header(wrong.data(), wrong.size(), version, padCount, recordSize)
	          == PC_INPUT_HEADER_UNSUPPORTED,
	      "future version rejected");
	wrong = header;
	wrong[6] = 2; // pad count 2
	check(pc_input_log_read_header(wrong.data(), wrong.size(), version, padCount, recordSize)
	          == PC_INPUT_HEADER_UNSUPPORTED,
	      "wrong pad count rejected");
	wrong = header;
	wrong[8] = 44; // v2 version with v1 size
	check(pc_input_log_read_header(wrong.data(), wrong.size(), version, padCount, recordSize)
	          == PC_INPUT_HEADER_UNSUPPORTED,
	      "version/size mismatch rejected");

	// Yaw quantisation: round-trip through angle is within half an LSB,
	// and known angles map to known codes.
	check(pc_input_log_yaw_quantise(0.0f) == 0, "yaw 0 -> 0");
	check(pc_input_log_yaw_quantise(6.28318530717958647692f) == 0, "yaw 2pi wraps to 0");
	// Non-finite camera input maps to neutral 0 instead of UB (m7).
	check(pc_input_log_yaw_quantise(std::numeric_limits<float>::quiet_NaN()) == 0, "yaw NaN -> 0");
	check(pc_input_log_yaw_quantise(std::numeric_limits<float>::infinity()) == 0, "yaw inf -> 0");
	check(pc_input_log_yaw_quantise(-std::numeric_limits<float>::infinity()) == 0, "yaw -inf -> 0");

	// The pre-sim capture hook runs and the yaw slots reset every
	// tick even with no record or replay file (det mode without files must
	// get a fresh yaw each tick, never a stale one). Needs
	// PIKMIN_INPUT_RECORD/PIKMIN_INPUT_REPLAY unset in the test env.
	{
		float s = 0.0f, c = 0.0f;
		check(!pc_netplay_control_yaw(0, &s, &c), "no yaw before the first tick");
		pc_input_log_set_yaw_capture_fn(&sTestCaptureHook);
		pc_input_log_tick();
		check(sHookCalls == 1, "capture hook runs without record/replay");
		check(pc_input_log_yaw_valid(0), "hook yaw stored for pad 0");
		check(pc_netplay_control_yaw(0, &s, &c), "stored yaw consumable after tick");
		// With the hook removed the slots must still clear every tick.
		pc_input_log_set_yaw_capture_fn(nullptr);
		pc_input_log_tick();
		check(sHookCalls == 1, "removed hook is not called");
		check(!pc_input_log_yaw_valid(0), "yaw cleared every tick without record/replay");
	}
	// The fresh capture clears stale injected yaw before running the
	// hook, so the lockstep session gets the live camera yaw every submit.
	{
		pc_input_log_set_yaw_capture_fn(&sTestCaptureHookChecked);
		pc_input_log_yaw_set(0, 0x5555, 0);
		check(pc_input_log_yaw_raw(0) == 0x5555, "injected yaw stored");
		pc_input_log_capture_yaw(); // Navi-style hook skips valid slots
		check(pc_input_log_yaw_raw(0) == 0x5555, "plain capture keeps stale injected yaw");
		pc_input_log_capture_yaw_fresh();
		check(pc_input_log_yaw_raw(0) == 0x1234, "fresh capture refreshes from the hook");
		check(sHookCalls == 3, "fresh capture runs the hook");
		pc_input_log_set_yaw_capture_fn(nullptr);
		pc_input_log_tick();
	}
	// Local slots without a record or replay file: the hook's values reach
	// the slots (unknown bits masked), the delta and the notches go to the
	// first reader only, the flags hold for the tick, and tick_end clears all.
	{
		check(pc_netplay_local_flags(1) == 0, "no local flags before a capture");
		pc_input_log_set_local_capture_fn(&sTestLocalHook);
		pc_input_log_tick();
		check(sLocalHookCalls == 1, "local hook runs without record/replay");
		check(pc_netplay_local_flags(1) == pc_input_log::kLocalMouseCursor, "local flags stored, unknown bit masked");
		check(pc_netplay_local_flags(0) == 0 && pc_netplay_local_flags(-1) == 0 && pc_netplay_local_flags(4) == 0,
		      "other and out-of-range pads read no flags");
		float dx = 1.0f, dy = 1.0f;
		pc_netplay_take_cursor_delta(0, &dx, &dy);
		check(dx == 0.0f && dy == 0.0f, "pad 0 has no delta");
		pc_netplay_take_cursor_delta(1, &dx, &dy);
		check(dx == 12.5f && dy == -0.75f, "pad 1 delta taken");
		pc_netplay_take_cursor_delta(1, &dx, &dy);
		check(dx == 0.0f && dy == 0.0f, "second take of the tick reads zero");
		check(pc_netplay_take_wheel_steps(1) == -3, "pad 1 wheel taken");
		check(pc_netplay_take_wheel_steps(1) == 0, "second wheel take reads zero");
		check(pc_netplay_local_flags(1) == pc_input_log::kLocalMouseCursor, "flags survive the takes");
		pc_netplay_take_cursor_delta(9, nullptr, nullptr); // out of range, null outputs: no crash
		check(pc_netplay_take_wheel_steps(9) == 0, "out-of-range wheel reads zero");
		pc_input_log_tick_end();
		check(pc_netplay_local_flags(1) == 0, "tick_end clears the local flags");
		// Untaken input does not carry over: tick_end drops it.
		pc_input_log_tick();
		pc_input_log_tick_end();
		pc_netplay_take_cursor_delta(1, &dx, &dy);
		check(dx == 0.0f && dy == 0.0f && pc_netplay_take_wheel_steps(1) == 0, "untaken input dropped at tick_end");
		pc_input_log_set_local_capture_fn(nullptr);
		pc_input_log_tick();
		check(sLocalHookCalls == 2, "removed local hook is not called");
		check(pc_netplay_local_flags(1) == 0, "local slots clear every tick without a hook");
		pc_input_log_tick_end();
	}
	// Per-pad camera rotation queued during a tick: the next tick's capture
	// quantises it into that pad's cursor rotate and keeps the remainder;
	// out-of-range pads are ignored and a huge drag clamps to the s16 range.
	{
		const float lsb = pc_input_log::kYawTurnsToRad;
		check(!pc_netplay_local_slots_filled(), "slots not filled outside a tick");
		pc_input_log_pad_rotate_add(3, lsb * 0.75f);
		pc_input_log_pad_rotate_add(-1, 1.0f);
		pc_input_log_pad_rotate_add(4, 1.0f);
		pc_input_log_tick();
		check(pc_netplay_local_slots_filled(), "slots filled during a tick");
		check(pc_netplay_ext_cursor_rotate(3) == 1, "0.75 LSB rounds to 1");
		check(pc_netplay_ext_cursor_rotate(0) == 0 && pc_netplay_ext_cursor_rotate(1) == 0
		          && pc_netplay_ext_cursor_rotate(2) == 0,
		      "other pads take no rotation");
		pc_input_log_tick_end();
		check(!pc_netplay_local_slots_filled(), "tick_end spends the slots");
		pc_input_log_pad_rotate_add(3, lsb * 0.25f);
		pc_input_log_tick();
		check(pc_netplay_ext_cursor_rotate(3) == 0, "the -0.25 remainder cancels the next 0.25");
		pc_input_log_tick_end();
		pc_input_log_tick();
		check(pc_netplay_ext_cursor_rotate(3) == 0, "a tick without a drag reads no rotation");
		pc_input_log_tick_end();
		pc_input_log_pad_rotate_add(3, 100.0f);
		pc_input_log_pad_rotate_add(3, std::numeric_limits<float>::quiet_NaN());
		pc_input_log_tick();
		check(pc_netplay_ext_cursor_rotate(3) == 32767, "a huge drag clamps; NaN is ignored");
		pc_input_log_tick_end();
	}
	{
		const float pi = 3.14159265358979323846f;
		check(pc_input_log_yaw_quantise(pi) == 0x8000, "yaw pi -> 0x8000");
		float s = 0.0f, c = 0.0f;
		pc_input_log_yaw_sincos(0x4000, &s, &c);
		check(std::fabs(s - 1.0f) < 1e-5f && std::fabs(c) < 1e-4f, "yaw 0x4000 is +90deg");
		pc_input_log_yaw_sincos(0, &s, &c);
		check(std::fabs(s) < 1e-4f && std::fabs(c - 1.0f) < 1e-5f, "yaw 0 is 0deg");
		// Negative angles wrap: -pi/2 == 3pi/2.
		check(pc_input_log_yaw_quantise(-pi / 2.0f) == 0xC000, "yaw -pi/2 wraps to 0xC000");
	}

	if (sFailures == 0) {
		std::printf("pc_input_log_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_input_log_test: %d failure(s)\n", sFailures);
	return 1;
}
