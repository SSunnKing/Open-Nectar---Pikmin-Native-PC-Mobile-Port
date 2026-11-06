// whistle_gather_release: the real whistle and disband paths (coverage/edge-cases.md EC-32, EC-39
// in part; issue #96), on the sandbox stand-in stage, with no state poked.
//
// The stand-in Fue motion (PIKIANIM_Fue, info 74) carries the disc's loop keys (standin_anims.h),
// so NaviGatherState grows its circle after KEY_LoopStart and NaviReleaseState disbands on it.
// The cursor is held at the captain's feet (mCursorWorldPos = mSRT.t), so distances are from the
// captain. R is NAVI_WHISTLE_MAX_RADIUS, min mWhistleMinRadius, expand mWhistleExpandTime.
//
// In order, each within a bound in seconds:
//   Away    The captain walks sideways for 2 s, clear of the ship, and stands in Walk.
//   Full    Free Pikmin at 0.5, 0.9, 1.1 and 1.4 R (blue, red, blue, red). B is held: the captain
//           enters Gather, the motion's KEY_LoopStart sets NaviGatherState::mWhistleAnimPhase to 1
//           by itself within 0.2 s, the circle is full (mode 2, callPikis(R)) within expand + 2
//           ticks of that, and B stays down 0.3 s more (the whistle ends at mWhistleHoldTime).
//           One tick after the circle is full a late Free Pikmin is spawned 40 from the captain.
//           Within 2 s of the release:
//           - the two inside R are in Formation, the two outside stay Free where they stood;
//           - formationPikis +2 and freePikis -2 (GameStat);
//           - the late Pikmin stays Free: mode 2 calls callPikis(mWhistleCallRadius) every tick,
//             but a full-length hold never sets mWhistleCallRadius, and in a fresh process it is
//             0 (the port's allocator zeroes the state; on the GameCube it is whatever the heap
//             held). Characterised, not fixed.
//   Stay    The squad stays in Formation for 2 s of standing (control for the disband).
//   Disband X (the disc's disband key) clicked once: NaviReleaseState within 2 ticks, every squad
//           Pikmin Free within 1 s (releasePikis on KEY_LoopStart), Walk again within 2 s
//           (KEY_Finished after finishMotion), formationPikis back to 0, freePikis +2.
//   Away    2 s sideways again, so every Pikmin left so far is outside R.
//   Partial B held until the circle's timer reaches half of expand, then released: mode 1 released calls
//           callPikis(scale), scale = min + frac (R - min), frac = mWhistleRadiusFrac in 0.3..0.7,
//           and sets mWhistleCallRadius to scale. Pikmin at 0.8 and 1.25 of the expected scale:
//           the first joins, the second stays Free (strict dist < scale on the measured scale).
//   Away    2 s sideways again (the squad follows; the 1.25 one is left behind).
//   Stale   A full hold again. One tick after the circle is full, Free Pikmin at 0.6 S and
//           (S + R) / 2, S the partial's scale: the per-tick calls use the stale S, so the first
//           joins and the second, inside R but outside S, stays Free. mWhistleCallRadius is still S.
//
// Negative controls: the Pikmin outside R or the scale (they would join if the radius were
// ignored), the late Pikmin of Full and the outer one of Stale (they would join if mode 2 called
// at R), and Stay (the squad would not stay if anything but X disbanded it).
// Manual (2026-10-31, local, not committed): with the stand-in Fue motion's info keys removed the
// scenario fails at Full ("KEY_LoopStart sets mWhistleAnimPhase to 1 within 0.2 s").
//
// Not covered: Pikmin in the excluded states (LookAt, Nukare, Drown, Swallowed, Pressed, Absorb,
// Flick; piki_drown covers Drown), the B click that interrupts a Release, the sunset whistle
// (NaviDemoSunsetState::WhistleState is never entered, #96), and two captains.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Dolphin/pad.h"
#include "GameStat.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "scenarios/scenario_tutorial_common.h"
#include "settings/pc_settings.h"

namespace {

constexpr float kStay    = 20.0f;
constexpr float kLate    = 40.0f;
constexpr int kMaxPikis  = 4;

class WhistleGatherRelease : public PcScenario {
public:
	const char* name() const override { return "whistle_gather_release"; }

	void onStageReady() override
	{
		tutorial::applyDiscKeys();
		Navi* navi = naviMgr->getNavi();
		if (!navi || pikiMgr->getSize() != 0 || pc_settings_get_whistle_radius_pct() != 100) {
			fail(0, "a captain, no Pikmin and Whistle Radius 100%", "captain %d, Pikmin %d, pct %d", (int)(navi != nullptr), pikiMgr->getSize(),
			     pc_settings_get_whistle_radius_pct());
			return;
		}
		mR      = NAVI_WHISTLE_MAX_RADIUS(navi);
		mMin    = C_NAVI_PARM(navi, mWhistleMinRadius);
		mExpand = C_NAVI_PARM(navi, mWhistleExpandTime);
		std::printf("[scenario] whistle_gather_release: radius %.1f..%.1f, expand %.2f s, hold %.2f s\n", mMin, mR, mExpand,
		            C_NAVI_PARM(navi, mWhistleHoldTime));
		enter(Away, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Away) pad.stickX = 100;
		if (mPhase == Hold && t < mReleaseTick) pad.button = PAD_BUTTON_B;
		if (mPhase == Disband && mStayed && t == mPhaseTick + 1) pad.button = PAD_BUTTON_X;
		if (mPhase != Away) {
			if (Navi* navi = naviMgr->getNavi()) {
				navi->mCursorPosition.set(0.0f, 0.0f, 0.0f);
				navi->mCursorTargetPosition = navi->mCursorPosition;
				navi->mCursorWorldPos       = navi->mSRT.t;
			}
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate         = unsigned(pc_scenario_ticks_per_second());
		Navi* navi    = naviMgr->getNavi();
		const float s = (t - mPhaseTick) / float(mRate);
		const int id  = navi->getCurrState()->getID();

		switch (mPhase) {
		case Away:
			if (t == mPhaseTick) mAwayFrom = navi->mSRT.t;
			if (s >= 2) enter(Settle, t);
			return;
		case Settle:
			if (id == NAVISTATE_Walk && speed(navi) < 1.0f) {
				mCentre  = navi->mSRT.t;
				mWalkDir = std::atan2(mCentre.z - mAwayFrom.z, mCentre.x - mAwayFrom.x);
				startRound(t, navi);
			} else if (s > 5) {
				fail(t, "the captain standing in Walk within 5 s", "state %d, speed %.1f", id, speed(navi));
			}
			return;
		case Stand:
			if (s >= 1) {
				for (int i = 0; i < mCount; ++i) {
					if (mPiki[i]->mMode != PikiMode::FreeMode) {
						fail(t, "spawned Pikmin stay Free until the whistle", "%s Pikmin %d mode %d", roundName(), i, mPiki[i]->mMode);
						return;
					}
					mFrom[i] = mPiki[i]->mSRT.t;
				}
				mGatherTick = mPhase1Tick = mFullTick = 0;
				mFormation0 = GameStat::formationPikis;
				mFree0      = GameStat::freePikis;
				// Partial: B down until the circle's timer is half of expand; full: until 0.3 s after
				// the circle is full (both set in hold()).
				mReleaseTick = ~0u;
				enter(Hold, t);
			}
			return;
		case Hold: hold(t, navi, s, id); return;
		case Check: check(t, navi, s); return;
		case Disband: disband(t, navi, s, id); return;
		default: return;
		}
	}

private:
	enum Phase { Away, Settle, Stand, Hold, Check, Disband, Done };
	enum Round { Full, Partial, Stale, Rounds };

	const char* roundName() const { return mRound == Full ? "full" : mRound == Partial ? "partial" : "stale"; }

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	void fail(unsigned t, const char* expected, const char* fmt, ...) __attribute__((format(printf, 4, 5)))
	{
		char buf[320];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		mPhase = Done;
		scenarioFailf(t, expected, "%s", buf);
	}

	static float speed(Navi* navi) { return std::sqrt(navi->mVelocity.x * navi->mVelocity.x + navi->mVelocity.z * navi->mVelocity.z); }

	/// A Free Pikmin `dist` from the captain, `angle` radians from the direction he last walked. The squad gathers behind
	/// him, so late spawns go ahead; everything spawned before a whistle goes behind, so the next walk leaves it there.
	Piki* spawnAt(Navi* navi, int colour, float dist, float angle)
	{
		const float a = mWalkDir + angle;
		Piki* p       = tutorial::spawnPiki(navi, colour, PikiMode::FreeMode, dist * std::cos(a), dist * std::sin(a));
		if (!p) fail(0, "Pikmin born", "birth returned null (%s)", roundName());
		GameStat::update();
		return p;
	}

	void startRound(unsigned t, Navi* navi)
	{
		mCount = 0;
		if (mRound == Full) {
			const float at[kMaxPikis]  = { 0.5f, 0.9f, 1.1f, 1.4f };
			const float ang[kMaxPikis] = { 2.4f, -2.4f, 2.9f, -2.9f };
			for (int i = 0; i < kMaxPikis; ++i) {
				if (!(mPiki[mCount++] = spawnAt(navi, i % 2 ? Red : Blue, at[i] * mR, ang[i]))) return;
			}
		} else if (mRound == Partial) {
			const float est = mMin + 0.5f * (mR - mMin);
			if (!(mPiki[mCount++] = spawnAt(navi, Blue, 0.8f * est, 2.5f))) return;
			if (!(mPiki[mCount++] = spawnAt(navi, Red, 1.25f * est, -2.5f))) return;
		}
		// Every Pikmin spawned before (left behind or in the squad) is out of this round's reach.
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			bool mine = false;
			for (int i = 0; i < mCount; ++i) mine |= p == mPiki[i];
			if (!mine && p->mMode == PikiMode::FreeMode && tutorial::flatDistance(p->mSRT.t, mCentre) < mR + 10.0f) {
				fail(t, "Pikmin left behind earlier are outside this round's radius", "%s: a Free Pikmin %.1f from the captain, R %.1f", roundName(),
				     tutorial::flatDistance(p->mSRT.t, mCentre), mR);
				return;
			}
		}
		enter(Stand, t);
	}

	void hold(unsigned t, Navi* navi, float s, int id)
	{
		if (id == NAVISTATE_Gather) {
			NaviGatherState* g = static_cast<NaviGatherState*>(navi->getCurrState());
			mGather            = g;
			if (!mGatherTick) {
				mGatherTick = t;
				if (mRound == Full && g->mWhistleCallRadius != 0.0f) {
					fail(t, "a fresh process starts with mWhistleCallRadius 0", "%.2f", g->mWhistleCallRadius);
					return;
				}
			}
			if (g->mWhistleAnimPhase == 1 && !mPhase1Tick) mPhase1Tick = t;
			if (mRound == Partial && mReleaseTick == ~0u && navi->mWhistleCircleMode == 1 && navi->mWhistleTimer >= 0.5f * mExpand - 1e-4f) {
				mReleaseTick = t + 1;
			}
			if (navi->mWhistleCircleMode == 2 && !mFullTick) {
				mFullTick = t;
			} else if (mFullTick && t == mFullTick + 1 && mRound != Partial) {
				// The circle went full last tick: callPikis(R) has run. Late Pikmin, ahead.
				const float d0 = mRound == Full ? kLate : 0.6f * mScale;
				if (!(mPiki[mCount++] = spawnAt(navi, Blue, d0, 0.3f))) return;
				if (mRound == Stale && !(mPiki[mCount++] = spawnAt(navi, Red, 0.5f * (mScale + mR), -0.5f))) return;
				for (int i = mCount - (mRound == Stale ? 2 : 1); i < mCount; ++i) mFrom[i] = mPiki[i]->mSRT.t;
				mLateFrom = mCount - (mRound == Stale ? 2 : 1);
				mReleaseTick = t + unsigned(std::lround(0.3f * mRate));
			}
		}
		if (mGatherTick && !mPhase1Tick && (t - mGatherTick) > unsigned(0.2f * mRate)) {
			fail(t, "KEY_LoopStart sets mWhistleAnimPhase to 1 within 0.2 s of Gather", "phase %d, %u ticks after Gather",
			     mGather ? mGather->mWhistleAnimPhase : -1, t - mGatherTick);
			return;
		}
		if (mPhase1Tick && mRound != Partial && !mFullTick && (t - mPhase1Tick) > unsigned(std::ceil(mExpand * mRate)) + 2) {
			fail(t, "B held: the circle full (mode 2) within expand + 2 ticks of KEY_LoopStart", "mode %d, %u ticks", navi->mWhistleCircleMode,
			     t - mPhase1Tick);
			return;
		}
		if (t >= mReleaseTick && id != NAVISTATE_Gather) {
			if (!mPhase1Tick) {
				fail(t, "B held: Gather and KEY_LoopStart", "Gather tick %u, phase-1 tick %u, state %d", mGatherTick, mPhase1Tick, id);
				return;
			}
			if (mRound == Partial) {
				const float frac = navi->mWhistleRadiusFrac;
				mScale           = mMin + frac * (mR - mMin);
				if (frac < 0.3f || frac > 0.7f || std::fabs(mGather->mWhistleCallRadius - mScale) > 0.01f) {
					fail(t, "a partial whistle calls at min + frac (R - min) and keeps it in mWhistleCallRadius", "frac %.3f, scale %.2f, mWhistleCallRadius %.2f",
					     frac, mScale, mGather->mWhistleCallRadius);
					return;
				}
				std::printf("[scenario] whistle_gather_release: partial: B down %u ticks, frac %.3f, called at %.1f\n", mReleaseTick - mPhaseTick - 1, frac,
				            mScale);
			} else {
				std::printf("[scenario] whistle_gather_release: %s: KEY_LoopStart %u ticks after Gather, circle full %u ticks after it, mWhistleCallRadius %.2f\n",
				            roundName(), mPhase1Tick - mGatherTick, mFullTick - mPhase1Tick, mGather->mWhistleCallRadius);
			}
			enter(Check, t);
		} else if (s > 5) {
			fail(t, "the whistle over within 5 s", "state %d, mode %d", id, navi->mWhistleCircleMode);
		}
	}

	void check(unsigned t, Navi* navi, float s)
	{
		if (s < 2) return;
		const float radius = mRound == Partial ? mScale : mR;
		int joined         = 0;
		for (int i = 0; i < mCount; ++i) {
			Piki* p           = mPiki[i];
			const bool late   = mRound != Partial && i >= mLateFrom;
			const float d     = tutorial::flatDistance(mFrom[i], mCentre);
			const float reach = late ? (mRound == Full ? 0.0f : mScale) : radius;
			const bool in     = d < reach;
			const bool party  = p->mMode == PikiMode::FormationMode && p->mNavi == navi;
			const float drift = tutorial::flatDistance(p->mSRT.t, mFrom[i]);
			std::printf("[scenario] whistle_gather_release: %s: %sPikmin at %.1f (reach %.1f): mode %d, state %d, moved %.1f\n", roundName(), late ? "late " : "",
			            d, reach, p->mMode, p->getState(), drift);
			if (in && !party) {
				fail(t, late ? "EC-32: mode 2's per-tick call reaches inside mWhistleCallRadius" : "EC-32: the whistle calls every Pikmin inside its radius",
				     "%s, %.1f inside %.1f, mode %d", roundName(), d, reach, p->mMode);
				return;
			}
			if (!in && (p->mMode != PikiMode::FreeMode || drift > kStay)) {
				fail(t, late ? "EC-32: mode 2's per-tick call uses mWhistleCallRadius (0 fresh, else the last partial's), not R"
				             : "EC-39: Pikmin outside the radius stay Free where they stood",
				     "%s, %.1f outside %.1f: mode %d, moved %.1f", roundName(), d, reach, p->mMode, drift);
				return;
			}
			joined += party;
		}
		const float stale = mRound == Full ? 0.0f : mScale;
		if (std::fabs(mGather->mWhistleCallRadius - stale) > 0.01f) {
			fail(t, "a full-length hold leaves mWhistleCallRadius as it was", "%s: %.2f, expected %.2f", roundName(), mGather->mWhistleCallRadius, stale);
			return;
		}
		GameStat::update();
		const int dForm = int(GameStat::formationPikis) - mFormation0, dFree = int(GameStat::freePikis) - mFree0;
		const int late  = mRound == Full ? 1 : mRound == Stale ? 2 : 0;
		// Late Pikmin were born Free after mFree0 was read.
		if (dForm != joined || dFree != late - joined) {
			fail(t, "EC-32: GameStat formationPikis +n and freePikis -n for n called", "%s: formation %+d, free %+d, %d called, %d born late", roundName(), dForm,
			     dFree, joined, late);
			return;
		}
		if (mRound == Full) {
			enter(Disband, t);
			mSquad0 = navi->getPlatePikis();
			mFormation0 = GameStat::formationPikis;
			mFree0      = GameStat::freePikis;
			mReleaseState = mFreeTick = mWalkTick = 0;
			return;
		}
		if (++mRound == Rounds) {
			std::printf("[scenario] whistle_gather_release: PASS at tick %u\n", t);
			mPhase = Done;
			scenarioPass();
			return;
		}
		enter(Away, t);
	}

	void disband(unsigned t, Navi* navi, float s, int id)
	{
		// Stay: 2 s of standing first; the X click is at mPhaseTick + 1 of the second part.
		if (!mStayed) {
			if (navi->getPlatePikis() != mSquad0 || id != NAVISTATE_Walk) {
				fail(t, "the squad stays in Formation while the captain stands", "squad %d of %d, state %d", navi->getPlatePikis(), mSquad0, id);
				return;
			}
			if (s >= 2) {
				mStayed = true;
				enter(Disband, t);
			}
			return;
		}
		if (id == NAVISTATE_Release && !mReleaseState) mReleaseState = t;
		if (t > mPhaseTick + 3 && !mReleaseState) {
			fail(t, "X (the disc's disband key) enters NaviReleaseState within 2 ticks", "state %d", id);
			return;
		}
		int free = 0;
		for (int i = 0; i < mCount; ++i) free += mPiki[i]->mMode == PikiMode::FreeMode;
		if (free == mCount && navi->getPlatePikis() == 0 && !mFreeTick) mFreeTick = t;
		if (mReleaseState && id == NAVISTATE_Walk && !mWalkTick) mWalkTick = t;
		if (mFreeTick && mWalkTick) {
			GameStat::update();
			const int dForm = int(GameStat::formationPikis) - mFormation0, dFree = int(GameStat::freePikis) - mFree0;
			std::printf("[scenario] whistle_gather_release: disband: Release %u ticks after X, all Free %u, Walk %u; formation %+d, free %+d\n",
			            mReleaseState - mPhaseTick - 1, mFreeTick - mPhaseTick - 1, mWalkTick - mPhaseTick - 1, dForm, dFree);
			if (mFreeTick - mPhaseTick > mRate || int(GameStat::formationPikis) != 0 || dFree != mSquad0 || dForm != -mSquad0) {
				fail(t, "EC-32: X disbands the whole squad to Free within 1 s; formationPikis 0, freePikis +n", "all Free after %u ticks, formation %d (%+d), free %+d, squad %d",
				     mFreeTick - mPhaseTick - 1, int(GameStat::formationPikis), dForm, dFree, mSquad0);
				return;
			}
			mRound = Partial;
			enter(Away, t);
			return;
		}
		if (s > 2) {
			fail(t, "EC-32: X disbands the squad and the captain is back in Walk within 2 s", "Release tick %u, all Free %u, Walk %u, squad %d, state %d", mReleaseState,
			     mFreeTick, mWalkTick, navi->getPlatePikis(), id);
		}
	}

	Phase mPhase        = Away;
	int mRound          = Full;
	unsigned mPhaseTick = 0, mRate = 30, mGatherTick = 0, mPhase1Tick = 0, mFullTick = 0, mReleaseTick = ~0u;
	unsigned mReleaseState = 0, mFreeTick = 0, mWalkTick = 0;
	bool mStayed = false;
	int mCount = 0, mLateFrom = 0, mFormation0 = 0, mFree0 = 0, mSquad0 = 0;
	float mR = 0, mMin = 0, mExpand = 0, mScale = 0, mWalkDir = 0;
	NaviGatherState* mGather = nullptr;
	Vector3f mCentre, mAwayFrom, mFrom[kMaxPikis + 2];
	Piki* mPiki[kMaxPikis + 2] = {};
};

} // namespace

PcScenario* pc_scenario_make_whistle_gather_release(void)
{
	static WhistleGatherRelease s;
	return &s;
}

#endif
