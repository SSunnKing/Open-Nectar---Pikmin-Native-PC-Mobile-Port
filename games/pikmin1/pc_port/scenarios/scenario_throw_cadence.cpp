// throw_cadence: how fast one captain throws ten Pikmin, measured the same way on
// the stand-in tree and on a disc install (PIKMIN_SCENARIO_DATA_DIR), so the per-throw
// tick lists of the two can be diffed (KYL-67: disc parity, one behaviour at a time).
//
// Setup (as throw_release): the stage is held to the disc's 45 intro ticks, the
// captain walks north for 4 s (away from the ship, where A enters it) and then
// stands still with neutral input for 1 s. Ten blue Pikmin are born in Formation
// on a 10-unit ring around him (inside the 15-unit hand-over range,
// NaviMgr mPluckGrabRange) and settle into the plate for 3 s. Tick T0 starts the case.
//
// Cases (A pattern from T0; the name suffix):
//   hold  A held; released for exactly one tick on the tick after each grab
//         completes (mIsHoldingThrowPiki), then held again. The fastest
//         "hold to grab, let go" rhythm.
//   tapN  a square wave: A down for N ticks, up for N ticks (tap1 is a fresh
//         press every 2 ticks, the fastest the pad can produce).
// The _qg variants turn on the port's Quick Grab setting, so the chosen Pikmin
// appears in the hand and nobody walks at all.
//
// Logged per throw: the Pikmin, its distance when ThrowWait starts, the tick the
// captain enters ThrowWait, the tick the Pikmin is within hand-over range
// (mHeldThrowPiki; the same tick when it already is), the grab keyframe
// (KEY_Action0 in ThrowWait: mIsHoldingThrowPiki), the tick the captain enters
// Throw, and the release keyframe (KEY_Action0 in Throw). The release is the first
// tick the Throw state has mHasThrownPiki, Throw is left straight for ThrowWait
// (a queued re-click, which NaviThrowState only honours once thrown), or the
// Pikmin is Flying. The cursor sits at the still captain's feet, so the flight is
// shorter than a tick and Flying is almost never seen (as throw_release).
// Then throws per second over the releases. The scenario passes once the ten
// throws are logged (or 30 s pass: it prints what it has and fails).
//
// Stage: on the stand-in tree run it on stages/stage1.ini (PIKMIN_SCENARIO_STAGE),
// not the sandbox. The sandbox gate pulls Pikmin 0 and 1 into BreakwallMode
// while they settle. Plate geometry still differs between the stand-in and
// disc stage1, so walk-in (GoHang) ticks for the vanilla cases differ. Compare
// the in-hand intervals (held->grab, grab->throw, throw->release) at equal
// input phase, and the _qg cases as whole lists.
//
// PIKMIN_TC_SPOT="x,z" moves the still captain to a flat, wall-free spot (facing +z, no
// speed) before the Pikmin are born on their fixed ring, as throw_flight's PIKMIN_TF_SPOT; with
// it the vanilla cases no longer depend on where the walk north ends.
//
// Diagnostics (env, off by default): PIKMIN_TC_TRACE=N prints a TRACE line per
// tick for the first N ticks from T0 (and SETTLE lines each second); with it,
// PIKMIN_TC_WATCH=k adds a WATCH line for Pikmin k (distance, state, mode).
// PIKMIN_TC_SETTLE=s overrides the 3 s settle (long settles let the captain go
// Idle); PIKMIN_TC_UP=n sets how many ticks the hold case lets go of A.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CPlate.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "MoviePlayer.h"
#include "gameflow.h"
#include "KeyConfig.h"
#include "Kontroller.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "settings/pc_settings.h"

namespace {

constexpr int kCount               = 10;
constexpr int kBlue                = 0;
constexpr unsigned kWalkStartTicks = 45;
constexpr unsigned kAwaySeconds    = 4;

struct ThrowRec {
	int piki        = -1;
	float dist      = 0.0f;
	bool pending    = false;
	unsigned wait   = 0;
	int held        = -1;
	int grab        = -1;
	int throwT      = -1;
	int release     = -1;
	bool aborted    = false;
};

class ThrowCadence : public PcScenario {
public:
	ThrowCadence(const char* name, int tap, bool quick)
	    : mName(name)
	    , mTap(tap)
	    , mQuick(quick)
	{
	}
	const char* name() const override { return mName; }

	void onStageReady() override
	{
		KeyConfig* k = KeyConfig::_instance;
		if (k == nullptr) {
			scenarioFailf(0, "KeyConfig", "none");
			return;
		}
		k->mThrowKey.mBind     = KBBTN_A;
		k->mSetCursorKey.mBind = KBBTN_B;
		pc_settings_scenario_set_quick_grab(mQuick ? 1 : 0);
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		const unsigned intro = pc_scenario_intro_ticks();
		mHold                = intro < kWalkStartTicks ? kWalkStartTicks - intro : 0;
		std::printf("[scenario] %s: intro %u ticks, hold %u, grab range %.1f, quick grab %d\n", mName, intro, mHold,
		            C_NAVI_PARM(navi, mPluckGrabRange), pc_settings_get_quick_grab());
		mTrace     = std::getenv("PIKMIN_TC_TRACE") ? std::atoi(std::getenv("PIKMIN_TC_TRACE")) : 0;
		mPhase     = mHold == 0 ? Away : Hold;
		mPhaseTick = 0;
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		pad.button = 0;
		if (mPhase == Away) pad.stickY = 100;
		if (mPhase != Run) return;
		const unsigned r = t - mT0;
		if (mTap == 0) {
			const unsigned up = std::getenv("PIKMIN_TC_UP") ? unsigned(std::atoi(std::getenv("PIKMIN_TC_UP"))) : 1u;
			pad.button = (mReleaseAt != ~0u && t >= mReleaseAt && t < mReleaseAt + up) ? 0 : PAD_BUTTON_A;
		} else {
			pad.button = ((r / unsigned(mTap)) % 2 == 0) ? PAD_BUTTON_A : 0;
		}
	}

	void onTick(unsigned t) override
	{
		Navi* navi     = naviMgr->getNavi();
		const int rate = pc_scenario_ticks_per_second();
		switch (mPhase) {
		case Hold:
			if (t + 1 >= mHold) {
				mPhase     = Away;
				mPhaseTick = t + 1;
			}
			return;
		case Away:
			if (t + 1 >= mPhaseTick + kAwaySeconds * unsigned(rate)) {
				mPhase     = Still;
				mPhaseTick = t + 1;
			}
			return;
		case Still:
			// A cutscene still running after the intro (the stand-in plays demo 45 after
			// its intro is reported done; the disc before) restarts the still second, so
			// the Pikmin are never born under a movie.
			if (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive) {
				mPhaseTick = t + 1;
				return;
			}
			if (t + 1 >= mPhaseTick + unsigned(rate)) {
				// PIKMIN_TC_SPOT="x,z": put the still captain on a flat, wall-free spot
				// (the one throw_flight uses), facing +z with no speed, before the Pikmin
				// are born round him, so the stage under the plate is the same on both trees.
				if (const char* spot = std::getenv("PIKMIN_TC_SPOT")) {
					Vector3f p;
					if (std::sscanf(spot, "%f,%f", &p.x, &p.z) == 2) {
						p.y           = mapMgr->getMinY(p.x, p.z, true);
						navi->mSRT.t  = p;
						navi->mVelocity.set(0.0f, 0.0f, 0.0f);
						navi->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
						navi->mFaceDirection = 0.0f;
						mSpot                = p;
						mSpotSet             = true;
						std::printf("[scenario] %s: captain put on the spot (%.2f, %.2f, %.2f)\n", mName, p.x, p.y, p.z);
					}
				}
				spawn(navi);
				mPhase     = Settle;
				mPhaseTick = t + 1;
			}
			return;
		case Settle:
			// With PIKMIN_TC_SPOT the whole settle pins the captain on the spot and each
			// Pikmin on its ring point (no speed, facing +z): T0 starts from the same
			// fixed offsets on both trees instead of wherever the formation AI drifted.
			if (mSpotSet) {
				navi->mSRT.t = mSpot;
				navi->mVelocity.set(0.0f, 0.0f, 0.0f);
				navi->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
				navi->mFaceDirection = 0.0f;
				for (int i = 0; i < kCount; ++i) {
					mPiki[i]->mSRT.t = mRing[i];
					mPiki[i]->mVelocity.set(0.0f, 0.0f, 0.0f);
					mPiki[i]->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
					mPiki[i]->mFaceDirection = 0.0f;
				}
			}
			if (mSettle == 0) mSettle = std::getenv("PIKMIN_TC_SETTLE") ? unsigned(std::atoi(std::getenv("PIKMIN_TC_SETTLE"))) : 3u;
			if (mTrace > 0 && (t - mPhaseTick) % unsigned(rate) == 0) {
				for (int i = 0; i < 2; ++i) {
					const Vector3f d = mPiki[i]->mSRT.t - navi->mSRT.t;
					std::printf("[scenario] %s: SETTLE %u Pikmin %d state %d mode %d d (%.1f, %.1f, %.1f)\n", mName, t - mPhaseTick, i, mPiki[i]->getState(), int(mPiki[i]->mMode), d.x, d.y, d.z);
				}
			}
			if (t + 1 >= mPhaseTick + mSettle * unsigned(rate)) {
				reportGround(navi);
				mPhase = Run;
				mT0    = t + 1;
				mPrev  = navi->getCurrState()->getID();
				std::printf("[scenario] %s: T0 = tick %u, captain at (%.1f, %.1f, %.1f) state %d\n", mName, mT0, navi->mSRT.t.x,
				            navi->mSRT.t.y, navi->mSRT.t.z, mPrev);
			}
			return;
		case Run: track(t, navi, rate); return;
		case Done: return;
		}
	}

private:
	enum Phase { Hold, Away, Still, Settle, Run, Done };

	void spawn(Navi* navi)
	{
		for (int i = 0; i < kCount; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (piki == nullptr) {
				scenarioFailf(0, "pikmin spawned", "birth returned null at %d", i);
				mPhase = Done;
				return;
			}
			GameStat::workPikis.inc(kBlue);
			piki->init(navi);
			const float a = 6.2831853f * (float)i / (float)kCount;
			Vector3f at   = navi->mSRT.t;
			at.x += 10.0f * std::cos(a);
			at.z += 10.0f * std::sin(a);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			mRing[i] = at;
			piki->initColor(kBlue);
			piki->changeMode(PikiMode::FormationMode, navi);
			mPiki[i] = piki;
		}
		GameStat::update();
	}

	// The ground under the plate: height range over a 120 x 120 grid around the captain.
	void reportGround(Navi* navi)
	{
		float lo = 1e9f, hi = -1e9f;
		for (int ix = -6; ix <= 6; ++ix) {
			for (int iz = -6; iz <= 6; ++iz) {
				const float y = mapMgr->getMinY(navi->mSRT.t.x + 10.0f * ix, navi->mSRT.t.z + 10.0f * iz, true);
				lo            = y < lo ? y : lo;
				hi            = y > hi ? y : hi;
			}
		}
		std::printf("[scenario] %s: ground within 60 units: %.2f..%.2f (range %.2f)\n", mName, lo, hi, hi - lo);
		for (int i = 0; i < kCount; ++i) {
			const Vector3f d = mPiki[i]->mSRT.t - navi->mSRT.t;
			std::printf("[scenario] %s: Pikmin %d at T0: dist %.1f state %d mode %d d (%.1f, %.1f, %.1f)\n", mName, i, d.length(), mPiki[i]->getState(),
			            int(mPiki[i]->mMode), d.x, d.y, d.z);
		}
	}

	int index(Piki* p) const
	{
		for (int i = 0; i < kCount; ++i)
			if (mPiki[i] == p) return i;
		return -1;
	}

	void track(unsigned t, Navi* navi, int rate)
	{
		const int cap = navi->getCurrState()->getID();
		ThrowRec* cur = mN > 0 ? &mRec[mN - 1] : nullptr;
		if (mTrace < 0) mTrace = std::getenv("PIKMIN_TC_TRACE") ? std::atoi(std::getenv("PIKMIN_TC_TRACE")) : 0;
		if (int(t - mT0) < mTrace) {
			char buf[64] = {};
			for (int i = 0; i < kCount; ++i) buf[i] = char('a' + mPiki[i]->getState());
			int tgt = -2, thrown = -1, held = -2, holding = -1;
			if (cap == NAVISTATE_Throw) {
				NaviThrowState* ts = static_cast<NaviThrowState*>(navi->getCurrState());
				tgt                = index(ts->mTargetPiki);
				thrown             = ts->mHasThrownPiki;
			}
			if (cap == NAVISTATE_ThrowWait) {
				NaviThrowWaitState* ws = static_cast<NaviThrowWaitState*>(navi->getCurrState());
				held                   = index(ws->mHeldThrowPiki);
				holding                = ws->mIsHoldingThrowPiki;
			}
			std::printf("[scenario] %s: TRACE %u cap %d A %d click %d pikis %s held %d holding %d tgt %d thrown %d\n", mName, t - mT0, cap,
			            int(navi->mKontroller->keyDown(KeyConfig::_instance->mThrowKey.mBind)),
			            int(navi->mKontroller->keyClick(KeyConfig::_instance->mThrowKey.mBind)), buf, held, holding, tgt, thrown);
			if (const char* w = std::getenv("PIKMIN_TC_WATCH")) {
				const int k = std::atoi(w);
				if (k >= 0 && k < kCount) {
					const Vector3f d = mPiki[k]->mSRT.t - navi->mSRT.t;
					std::printf("[scenario] %s: WATCH %u Pikmin %d dist %.2f state %d mode %d d (%.2f, %.2f, %.2f)\n", mName, t - mT0, k, d.length(),
					            mPiki[k]->getState(), int(mPiki[k]->mMode), d.x, d.y, d.z);
				}
			}
		}
		// Release (KEY_Action0 of the throw): the Throw state's mHasThrownPiki, or Throw
		// left straight for ThrowWait (NaviThrowState::exec only does that once thrown).
		// The cursor sits at the still captain's feet, so the flight is shorter than a
		// tick and Flying is usually never seen at a tick boundary (as throw_release).
		if (cur && cur->throwT >= 0 && cur->release < 0) {
			const bool flag = cap == NAVISTATE_Throw && static_cast<NaviThrowState*>(navi->getCurrState())->mHasThrownPiki;
			const bool rethrow = mPrev == NAVISTATE_Throw && cap == NAVISTATE_ThrowWait;
			const bool flying  = cur->piki >= 0 && mPiki[cur->piki]->getState() == PIKISTATE_Flying;
			if (flag || rethrow || flying) {
				cur->release = int(t - mT0);
				++mReleased;
			} else if (mPrev == NAVISTATE_Throw && cap != NAVISTATE_Throw) {
				cur->aborted = true; // Throw ended without a release
			}
		}
		if (cap == NAVISTATE_ThrowWait && mPrev != NAVISTATE_ThrowWait && mN < kMaxRec) {
			NaviThrowWaitState* w = static_cast<NaviThrowWaitState*>(navi->getCurrState());
			Piki* p               = w->mHeldThrowPiki ? w->mHeldThrowPiki : w->mPendingThrowPiki;
			ThrowRec& r           = mRec[mN++];
			r.wait                = t - mT0;
			r.piki                = index(p);
			r.pending             = w->mHeldThrowPiki == nullptr;
			r.dist                = p ? (p->mSRT.t - navi->mSRT.t).length() : -1.0f;
			cur                   = &r;
		}
		if (cur && cap == NAVISTATE_ThrowWait) {
			NaviThrowWaitState* w = static_cast<NaviThrowWaitState*>(navi->getCurrState());
			if (cur->held < 0 && w->mHeldThrowPiki) cur->held = int(t - mT0);
			if (cur->grab < 0 && w->mIsHoldingThrowPiki) {
				cur->grab = int(t - mT0);
				if (mTap == 0) mReleaseAt = t + 1;
			}
		}
		if (cur && cap == NAVISTATE_Throw && mPrev != NAVISTATE_Throw) {
			cur->throwT = int(t - mT0);
			if (cur->held < 0) cur->held = cur->throwT;
			if (cur->grab < 0) cur->grab = cur->throwT; // grab and throw in one tick
			Piki* p = static_cast<NaviThrowState*>(navi->getCurrState())->mTargetPiki;
			if (index(p) != cur->piki) std::printf("[scenario] %s: note: Throw target %d != waited %d\n", mName, index(p), cur->piki);
		}
		if (cur && mPrev == NAVISTATE_ThrowWait && cap == NAVISTATE_Walk && cur->throwT < 0) cur->aborted = true;
		mPrev = cap;
		if (mReleased >= kCount || t - mT0 > 30u * unsigned(rate)) finish(t, rate);
	}

	void finish(unsigned t, int rate)
	{
		mPhase = Done;
		std::printf("[scenario] %s: rate %d, throws:\n", mName, rate);
		std::printf("[scenario] %s: #  piki dist  pend wait held grab throw release\n", mName);
		int first = -1, last = -1, n = 0;
		for (int i = 0; i < mN; ++i) {
			const ThrowRec& r = mRec[i];
			std::printf("[scenario] %s: THROW %d %d %.1f %d %u %d %d %d %d%s\n", mName, i, r.piki, r.dist, int(r.pending), r.wait, r.held, r.grab,
			            r.throwT, r.release, r.aborted ? " aborted" : "");
			if (r.release >= 0) {
				if (first < 0) first = r.release;
				last = r.release;
				++n;
			}
		}
		const double tps = (n > 1 && last > first) ? double(n - 1) * rate / double(last - first) : 0.0;
		std::printf("[scenario] %s: CADENCE rate %d released %d first %d last %d throws_per_second %.4f\n", mName, rate, n, first, last, tps);
		if (mReleased >= kCount) scenarioPass();
		else scenarioFailf(t, "ten throws within 30 s", "%d released", mReleased);
	}

	static constexpr int kMaxRec = 64;
	const char* mName;
	int mTap;
	bool mQuick;
	Phase mPhase        = Hold;
	unsigned mHold      = 0;
	unsigned mPhaseTick = 0;
	unsigned mT0        = 0;
	unsigned mReleaseAt = ~0u;
	int mPrev           = -1;
	Piki* mPiki[kCount] = {};
	ThrowRec mRec[kMaxRec];
	int mN        = 0;
	int mReleased = 0;
	int mTrace    = -1;
	unsigned mSettle = 0;
	Vector3f mRing[kCount];
	Vector3f mSpot;
	bool mSpotSet = false;
};

} // namespace

#define THROW_CADENCE(fn, nm, tap, quick) \
	PcScenario* fn(void)                  \
	{                                     \
		static ThrowCadence s(nm, tap, quick); \
		return &s;                        \
	}
THROW_CADENCE(pc_scenario_make_throw_cadence_hold, "throw_cadence_hold", 0, false)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap1, "throw_cadence_tap1", 1, false)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap2, "throw_cadence_tap2", 2, false)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap3, "throw_cadence_tap3", 3, false)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap5, "throw_cadence_tap5", 5, false)
THROW_CADENCE(pc_scenario_make_throw_cadence_hold_qg, "throw_cadence_hold_qg", 0, true)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap1_qg, "throw_cadence_tap1_qg", 1, true)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap2_qg, "throw_cadence_tap2_qg", 2, true)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap3_qg, "throw_cadence_tap3_qg", 3, true)
THROW_CADENCE(pc_scenario_make_throw_cadence_tap5_qg, "throw_cadence_tap5_qg", 5, true)

#endif
