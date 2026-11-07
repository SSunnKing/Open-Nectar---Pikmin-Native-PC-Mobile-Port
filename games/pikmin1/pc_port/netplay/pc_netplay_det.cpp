// Deterministic fixed-step mode for netplay.
//
// Engine-free: only the C library, env vars and argv, so the host tests can
// link it.

#include "netplay/pc_netplay_det.h"
#include "netplay/pc_sim_rng.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define PC_DET_HAVE_SSE_CSR 1
#endif
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#define PC_DET_HAVE_X87_CW 1
#endif
#if !defined(PC_DET_HAVE_X87_CW)
#include <cfenv>
#endif

namespace {

bool sDeterministic = false;
bool sUnthrottled   = false;
bool sCatchup       = false;
unsigned sTick      = 0;

bool envIsOne(const char* name)
{
	const char* value = std::getenv(name);
	return value != nullptr && value[0] == '1' && value[1] == '\0';
}

unsigned fnv1aBytes(unsigned hash, const void* data, unsigned size)
{
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (unsigned i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

unsigned readU32Env(const char* name, unsigned fallback)
{
	const char* value = std::getenv(name);
	if (value == nullptr || *value == '\0') return fallback;
	// strtoul accepts a leading '-' (it wraps) and saturates on overflow:
	// reject both, plus trailing garbage, instead of silently using them.
	if (value[0] == '-') {
		std::fprintf(stderr, "[netplay-det] invalid %s='%s', using %u\n", name, value, fallback);
		return fallback;
	}
	errno     = 0;
	char* end = nullptr;
	const unsigned long parsed = std::strtoul(value, &end, 0);
	if (end == value || *end != '\0' || errno == ERANGE || parsed > 0xfffffffful) {
		std::fprintf(stderr, "[netplay-det] invalid %s='%s', using %u\n", name, value, fallback);
		return fallback;
	}
	return static_cast<unsigned>(parsed);
}

} // namespace

void pc_netplay_det_init(int argc, char** argv)
{
	// Re-readable so the host tests can toggle modes in-process.
	sDeterministic = envIsOne("PIKMIN_NETPLAY_DETERMINISTIC");
	for (int i = 1; i < argc; ++i) {
		if (argv[i] != nullptr && std::strcmp(argv[i], "--netplay-deterministic") == 0) {
			sDeterministic = true;
			break;
		}
	}
	sUnthrottled = sDeterministic && envIsOne("PIKMIN_NETPLAY_UNTHROTTLED");
	// Both RNG streams are main-thread only; remember which thread that is.
	pc_sim_rng_note_main_thread();
	if (sDeterministic) {
		std::printf("[netplay-det] deterministic fixed-step mode ON%s\n",
		    sUnthrottled ? " (unthrottled: one tick per loop, no vsync wait)" : "");
		std::fflush(stdout);
	}
}

bool pc_netplay_deterministic(void) { return sDeterministic; }

bool pc_netplay_unthrottled(void) { return sDeterministic && (sUnthrottled || sCatchup); }

void pc_netplay_det_force_on(void)
{
	sDeterministic = true;
	sUnthrottled   = sUnthrottled || envIsOne("PIKMIN_NETPLAY_UNTHROTTLED");
	pc_sim_rng_note_main_thread();
	std::printf("[netplay-det] deterministic fixed-step mode ON (forced by netplay)%s\n",
	    sUnthrottled ? " (unthrottled)" : "");
	std::fflush(stdout);
}

void pc_netplay_det_enable_for_recording(void)
{
	if (sDeterministic) return;
	sDeterministic = true;
	sUnthrottled   = envIsOne("PIKMIN_NETPLAY_UNTHROTTLED");
	pc_sim_rng_note_main_thread();
	std::printf("[netplay-det] deterministic fixed-step mode ON (quick save recording)%s\n",
	    sUnthrottled ? " (unthrottled)" : "");
	std::fflush(stdout);
}

void pc_netplay_det_set_catchup(bool on) { sCatchup = on; }

bool pc_netplay_det_catchup(void) { return sCatchup; }

void pc_netplay_det_set_tick(unsigned tick) { sTick = tick; }

unsigned pc_netplay_tick(void) { return sTick; }

void pc_netplay_on_tick_begin(void)
{
	++sTick;
	if (!sDeterministic) return;
	// Pin the FP environment every tick: third-party DLLs (GL drivers, audio,
	// SDL) may change it on our thread. MXCSR 0x1F80 is the reset value
	// (round-to-nearest, all exceptions masked, no FTZ/DAZ).
#if defined(PC_DET_HAVE_SSE_CSR)
	_mm_setcsr(0x1F80);
#endif
#if defined(PC_DET_HAVE_X87_CW)
	// 0x037F: all exceptions masked, 64-bit precision, round-to-nearest.
	// Set directly: include/Dolphin/float.h shadows the system <float.h>, so
	// _controlfp is not reachable from here.
	{
		const unsigned short cw = 0x037Fu;
		__asm__ volatile("fldcw %0" : : "m"(cw));
	}
#else
	std::fesetround(FE_TONEAREST);
#endif
}

float pc_netplay_fixed_dt(int frameClamp)
{
	// Mirrors PcFrameScheduler::deltaForClamp; kept local so this TU stays
	// engine-free.
	if (frameClamp == 0) return 1.0f / 120.0f;
	if (frameClamp == 1) return 1.0f / 60.0f;
	if (frameClamp == 2) return 1.0f / 30.0f;
	return float(frameClamp) / 60.0f;
}

unsigned pc_netplay_det_session_seed(void) { return readU32Env("PIKMIN_NETPLAY_SEED", 0); }

void pc_netplay_det_reseed_for_new_day(int dayIndex, int stageId)
{
	if (!sDeterministic) return;
	const unsigned sessionSeed = pc_netplay_det_session_seed();
	// FNV-1a/32 over the session seed, day index and stage id as u32 words.
	// Every peer computes the same words from the same save.
	unsigned hash = 2166136261u;
	const unsigned words[3]
	    = { sessionSeed, static_cast<unsigned>(dayIndex), static_cast<unsigned>(stageId) };
	hash = fnv1aBytes(hash, words, sizeof(words));
	pc_sim_srand(hash);
	// The cosmetic stream gets the same hash with a fixed xor so draw-only
	// draws never alias the sim stream.
	pc_cosmetic_srand(hash ^ 0x9E3779B9u);
	std::printf("[netplay-det] reseed day=%d stage=%d sessionSeed=%u hash=0x%08x tick=%u\n", dayIndex,
	    stageId, sessionSeed, hash, sTick);
	std::fflush(stdout);
}

const char* pc_netplay_det_profile_path(void)
{
	static const char* path = [] {
		const char* value = std::getenv("PIKMIN_NETPLAY_PROFILE_LOG");
		return (value != nullptr && *value != '\0') ? value : nullptr;
	}();
	return path;
}
