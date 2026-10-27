// Curated per-tick simulation hash for the netplay harness. See
// pc_state_hash.h for the exact format and field list.

#include "netplay/pc_state_hash.h"

#include "netplay/pc_input_log.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_sim_rng.h"

#include "Boss.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "Navi.h"
#include "Piki.h"
#include "NaviMgr.h"
#include "ObjType.h"
#include "Pellet.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "teki.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime  = 1099511628211ULL;

uint64_t fnvMix(uint64_t h, const void* data, size_t len)
{
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < len; ++i) {
		h ^= (uint64_t)p[i];
		h *= kFnvPrime;
	}
	return h;
}

void mixU64(uint64_t& h, uint64_t v)
{
	uint8_t b[8];
	for (int i = 0; i < 8; ++i) b[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
	h = fnvMix(h, b, 8);
}

void mixU32(uint64_t& h, uint32_t v)
{
	uint8_t b[4];
	for (int i = 0; i < 4; ++i) b[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
	h = fnvMix(h, b, 4);
}

void mixS32(uint64_t& h, int32_t v) { mixU32(h, (uint32_t)v); }

void mixF32(uint64_t& h, float v)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &v, sizeof(bits));
	mixU32(h, bits);
}

void mixVec(uint64_t& h, const Vector3f& v)
{
	mixF32(h, v.x);
	mixF32(h, v.y);
	mixF32(h, v.z);
}

uint64_t sTick          = 0;
bool sInitialised       = false;
bool sLogActive         = false;
FILE* sLogFile          = nullptr;
uint64_t sExitAfter     = 0;
bool sExitAfterSet      = false;
bool sExitRequested     = false;
uint64_t sExitTick      = 0;
bool sPrerollDone       = false;
const char* sArgvLog    = nullptr;
const char* sArgvExit   = nullptr;
// Lockstep capture: the last computed hashes, even with no log file open.
// Written only by pc_state_hash_tick_end().
bool sNetplayCapture    = false;
bool sNetplayHaveHash   = false;
uint64_t sLastTotal     = 0;
uint64_t sLastSubs[7]   = { 0, 0, 0, 0, 0, 0, 0 };
uint64_t sLastTick      = 0;
PcStateHashModeFn sModeFn = nullptr;

uint64_t parseU64(const char* text, bool& ok)
{
	ok = false;
	if (text == nullptr || *text == '\0') return 0;
	char* end = nullptr;
	unsigned long long v = std::strtoull(text, &end, 10);
	if (end == text || *end != '\0') return 0;
	ok = true;
	return (uint64_t)v;
}

const char* argvValue(int argc, char** argv, const char* flag)
{
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
	}
	return nullptr;
}

uint64_t hashNavi(void)
{
	// No live stage: exitStage reliably nulls naviMgr while the other
	// manager globals may still point at the released stage. Hash 0
	// instead of walking stale objects.
	if (naviMgr == nullptr) return 0;
	uint64_t h = kFnvOffset;
	Iterator it(naviMgr);
	CI_LOOP(it)
	{
		Navi* n = static_cast<Navi*>(*it);
		if (n == nullptr) continue;
		mixS32(h, (int32_t)n->mNaviID);
		mixVec(h, n->mSRT.t);
		mixVec(h, n->mSRT.r);
		mixVec(h, n->mVelocity);
		mixVec(h, n->mTargetVelocity);
		mixF32(h, n->mFaceDirection);
		mixF32(h, n->mHealth);
		AState<Navi>* st = n->getCurrState();
		mixS32(h, st != nullptr ? (int32_t)st->getID() : (int32_t)-1);
		// Lock-On / Charge: mixed only while in use, so a session that never
		// locks hashes exactly as before.
		if (n->mPcLockTarget != nullptr || n->mPcChargeTime > 0.0f) {
			mixS32(h, n->mPcLockTarget != nullptr ? (int32_t)n->mPcLockTarget->mObjType : (int32_t)-1);
			if (n->mPcLockTarget != nullptr) mixVec(h, n->mPcLockTarget->mSRT.t);
			mixF32(h, n->mPcChargeTime);
			mixVec(h, n->mPcAimOffset);
		}
	}
	return h;
}

uint64_t hashPiki(void)
{
	if (naviMgr == nullptr || pikiMgr == nullptr) return 0;
	uint64_t h = kFnvOffset;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* p = static_cast<Piki*>(*it);
		if (p == nullptr) continue;
		mixU32(h, (uint32_t)p->mColor);
		mixS32(h, (int32_t)p->mHappa);
		mixVec(h, p->mSRT.t);
		mixVec(h, p->mSRT.r);
		mixVec(h, p->mVelocity);
		mixVec(h, p->mTargetVelocity);
		mixF32(h, p->mFaceDirection);
		mixF32(h, p->mHealth);
		AState<Piki>* st = p->getCurrState();
		mixS32(h, st != nullptr ? (int32_t)st->getID() : (int32_t)-1);
	}
	return h;
}

uint64_t hashTeki(void)
{
	if (naviMgr == nullptr || tekiMgr == nullptr) return 0;
	uint64_t h = kFnvOffset;
	Iterator it(tekiMgr);
	CI_LOOP(it)
	{
		Teki* t = static_cast<Teki*>(*it);
		if (t == nullptr) continue;
		mixS32(h, (int32_t)t->mTekiType);
		mixVec(h, t->mSRT.t);
		mixVec(h, t->mSRT.r);
		mixVec(h, t->mVelocity);
		mixVec(h, t->mTargetVelocity);
		mixF32(h, t->mFaceDirection);
		mixF32(h, t->mHealth);
		mixS32(h, (int32_t)t->mStateID);
	}
	return h;
}

uint64_t hashItem(void)
{
	if (naviMgr == nullptr) return 0;
	if (itemMgr == nullptr && pelletMgr == nullptr && bossMgr == nullptr) return 0;
	uint64_t h = kFnvOffset;
	if (itemMgr != nullptr) {
		Iterator it(itemMgr);
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c == nullptr) continue;
			mixS32(h, (int32_t)c->mObjType);
			mixVec(h, c->mSRT.t);
			mixVec(h, c->mSRT.r);
			mixVec(h, c->mVelocity);
			mixVec(h, c->mTargetVelocity);
			mixF32(h, c->mFaceDirection);
			// Goal check uses the same mObjType test as
			// ItemMgr::getContainer (itemMgr.cpp), then a static_cast:
			// no dynamic_cast, so no vptr chase through stale objects.
			if (c->mObjType == OBJTYPE_Goal) {
				GoalItem* goal = static_cast<GoalItem*>(c);
				mixU32(h, (uint32_t)goal->mOnionColour);
				mixU32(h, goal->mHeldPikis[0]);
				mixU32(h, goal->mHeldPikis[1]);
				mixU32(h, goal->mHeldPikis[2]);
			}
		}
		// Onion/container counts per colour, via the colour-indexed lookup
		// (0 when that colour has no Onion).
		for (int color = PikiMinColor; color <= PikiMaxColor; ++color) {
			GoalItem* onion = itemMgr->getContainer(color);
			if (onion == nullptr) {
				mixU32(h, 0);
				mixU32(h, 0);
				mixU32(h, 0);
			} else {
				mixU32(h, onion->mHeldPikis[0]);
				mixU32(h, onion->mHeldPikis[1]);
				mixU32(h, onion->mHeldPikis[2]);
			}
		}
	}
	if (pelletMgr != nullptr) {
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p == nullptr) continue;
			if (p->mConfig != nullptr) {
				mixU32(h, p->mConfig->mPelletId.mId);
				mixS32(h, (int32_t)p->mConfig->mPelletType());
			} else {
				mixU32(h, 0);
				mixS32(h, 0);
			}
			mixVec(h, p->mSRT.t);
			mixVec(h, p->mSRT.r);
			mixVec(h, p->mVelocity);
			mixVec(h, p->mTargetVelocity);
			mixF32(h, p->mFaceDirection);
			AState<Pellet>* st = p->getCurrState();
			mixS32(h, st != nullptr ? (int32_t)st->getID() : (int32_t)-1);
			mixS32(h, (int32_t)p->getState());
			mixU32(h, (uint32_t)p->mCarrierCount);
		}
	}
	if (bossMgr != nullptr) {
		Iterator it(bossMgr);
		CI_LOOP(it)
		{
			Boss* b = static_cast<Boss*>(*it);
			if (b == nullptr) continue;
			mixS32(h, (int32_t)b->getCurrentState());
			mixS32(h, (int32_t)b->getNextState());
			mixF32(h, b->getCurrentLife());
			mixVec(h, b->mSRT.t);
			mixVec(h, b->mSRT.r);
			mixVec(h, b->mVelocity);
			mixVec(h, b->mTargetVelocity);
			mixF32(h, b->mFaceDirection);
		}
	}
	return h;
}

uint64_t hashWorld(void)
{
	uint64_t h = kFnvOffset;
	mixF32(h, gameflow.mWorldClock.mTimeOfDay);
	mixS32(h, (int32_t)gameflow.mWorldClock.mCurrentDay);
	if (playerState != nullptr) {
		mixS32(h, (int32_t)playerState->mLivingPikiNum);
		mixS32(h, (int32_t)playerState->mTotalBornPikiNum);
		mixS32(h, (int32_t)playerState->mTotalDeadPikiNum);
		mixS32(h, (int32_t)playerState->mTotalPluckedPikiCount);
	} else {
		mixS32(h, 0);
		mixS32(h, 0);
		mixS32(h, 0);
		mixS32(h, 0);
	}
	return h;
}

uint64_t hashRng(void) { return (uint64_t)pc_sim_rng_state(); }

// Mode-specific sim state (VS fills it in); 0 when no source is registered.
uint64_t hashMode(void) { return sModeFn != nullptr ? sModeFn() : 0; }

void initOnce(void)
{
	sInitialised = true;
	const char* logPath = sArgvLog;
	const char* exitText = sArgvExit;
	if (logPath == nullptr) logPath = std::getenv("PIKMIN_STATE_HASH_LOG");
	if (exitText == nullptr) exitText = std::getenv("PIKMIN_NETPLAY_EXIT_AFTER_TICKS");
	if (logPath != nullptr && *logPath != '\0') {
		sLogFile = std::fopen(logPath, "w");
		if (sLogFile != nullptr) {
			std::setvbuf(sLogFile, nullptr, _IOFBF, 64 * 1024);
			sLogActive = true;
			std::printf("[netplay] state hash log: %s\n", logPath);
			std::fflush(stdout);
		} else {
			std::printf("[netplay] state hash log: cannot open %s\n", logPath);
			std::fflush(stdout);
		}
	}
	if (exitText != nullptr && *exitText != '\0') {
		bool ok = false;
		uint64_t n = parseU64(exitText, ok);
		if (ok && n > 0) {
			sExitAfter    = n;
			sExitAfterSet = true;
		}
	}
}
} // namespace

void pc_state_hash_notify_argv(int argc, char** argv)
{
	if (argv == nullptr) return;
	sArgvLog  = argvValue(argc, argv, "--state-hash-log");
	sArgvExit = argvValue(argc, argv, "--netplay-exit-after-ticks");
}

void pc_state_hash_before_first_tick(void)
{
	if (sPrerollDone) return;
	sPrerollDone = true;
	const char* text = std::getenv("PIKMIN_NETPLAY_TEST_PREROLL_RAND");
	if (text == nullptr || *text == '\0') return;
	bool ok     = false;
	uint64_t n  = parseU64(text, ok);
	if (!ok) return;
	for (uint64_t i = 0; i < n; ++i) {
		(void)std::rand();
		if (pc_netplay_deterministic()) (void)pc_sim_rand();
	}
}

uint64_t pc_state_hash_tick(void) { return sTick; }

void pc_state_hash_set_netplay_capture(bool on) { sNetplayCapture = on; }

uint64_t pc_state_hash_compute_total(void)
{
	uint64_t total = kFnvOffset;
	mixU64(total, hashNavi());
	mixU64(total, hashPiki());
	mixU64(total, hashTeki());
	mixU64(total, hashItem());
	mixU64(total, hashWorld());
	mixU64(total, hashRng());
	mixU64(total, hashMode());
	return total;
}

void pc_state_hash_set_tick(uint64_t tick) { sTick = tick; }

void pc_state_hash_set_mode_fn(PcStateHashModeFn fn) { sModeFn = fn; }

void pc_state_hash_print_navi_positions(uint64_t tick)
{
	if (naviMgr == nullptr) return;
	Iterator it(naviMgr);
	CI_LOOP(it)
	{
		Navi* n = static_cast<Navi*>(*it);
		if (n == nullptr) continue;
		std::printf("[netplay-navi] tick=%llu navi=%d pos=(%.2f %.2f %.2f) cursor=(%.2f %.2f)\n",
		            (unsigned long long)tick, n->mNaviID, n->mSRT.t.x, n->mSRT.t.y, n->mSRT.t.z, n->mCursorPosition.x,
		            n->mCursorPosition.z);
	}
	std::fflush(stdout);
}

bool pc_state_hash_current(uint64_t* total, uint64_t subs[7], uint64_t* tick)
{
	if (!sNetplayHaveHash) return false;
	if (total != nullptr) *total = sLastTotal;
	if (subs != nullptr) {
		for (int i = 0; i < 7; ++i) subs[i] = sLastSubs[i];
	}
	if (tick != nullptr) *tick = sLastTick;
	return true;
}

void pc_state_hash_flush(void)
{
	if (sLogFile != nullptr) std::fflush(sLogFile);
}

void pc_state_hash_tick_end(void)
{
	if (!sInitialised) initOnce();
	// With no switch set, return before walking any manager: no hashes, no
	// files, no timing change. A lockstep session opts into per-tick hashing
	// via sNetplayCapture even when no log file is open.
	if (!sLogActive && !sExitAfterSet && !sNetplayCapture) return;
	if (sExitRequested) {
		// The quit event is already queued; the main loop breaks on its
		// next pc_window_should_close() check. Fall back to a direct
		// exit only if the loop is stuck.
		if (sTick >= sExitTick + 600) {
			pc_state_hash_flush();
			pc_input_log_flush();
			pc_input_log_close();
			std::fflush(stdout);
			std::exit(0);
		}
		return;
	}
	++sTick;

	uint64_t navi  = 0;
	uint64_t piki  = 0;
	uint64_t teki  = 0;
	uint64_t item  = 0;
	uint64_t world = 0;
	uint64_t rng   = 0;
	uint64_t mode  = 0;
	uint64_t total = 0;
	if (sLogActive || sNetplayCapture) {
		navi  = hashNavi();
		piki  = hashPiki();
		teki  = hashTeki();
		item  = hashItem();
		world = hashWorld();
		rng   = hashRng();
		mode  = hashMode();
		total = kFnvOffset;
		mixU64(total, navi);
		mixU64(total, piki);
		mixU64(total, teki);
		mixU64(total, item);
		mixU64(total, world);
		mixU64(total, rng);
		mixU64(total, mode);

		sLastTotal    = total;
		sLastSubs[0]  = navi;
		sLastSubs[1]  = piki;
		sLastSubs[2]  = teki;
		sLastSubs[3]  = item;
		sLastSubs[4]  = world;
		sLastSubs[5]  = rng;
		sLastSubs[6]  = mode;
		sLastTick     = sTick;
		sNetplayHaveHash = true;

		if (sLogActive) {
			std::fprintf(sLogFile, "%llu %016llx %016llx %016llx %016llx %016llx %016llx %016llx %016llx\n",
			             (unsigned long long)sTick, (unsigned long long)total, (unsigned long long)navi,
			             (unsigned long long)piki, (unsigned long long)teki, (unsigned long long)item,
			             (unsigned long long)world, (unsigned long long)rng, (unsigned long long)mode);
			if (sTick % 300 == 0) std::fflush(sLogFile);
		}
	}

	if (sExitAfterSet && sTick >= sExitAfter) {
		pc_state_hash_flush();
		pc_input_log_flush();
		pc_input_log_close();
		std::fflush(stdout);
		// Clean quit through the same route as normal play: queue SDL_QUIT
		// so System::run breaks on pc_window_should_close() and main
		// returns through "[PC Port] Game exited normally.". Stop hashing
		// further ticks; the counter above is the fallback if the loop
		// never drains the event.
		sExitRequested = true;
		sExitTick      = sTick;
		SDL_Event ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.type = SDL_QUIT;
		SDL_PushEvent(&ev);
	}
}
