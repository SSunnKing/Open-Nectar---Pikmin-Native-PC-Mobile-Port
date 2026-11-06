// whistle_pluck_held: the Whistle Pluck mod at its real call site (coverage/milestones.md PL4, MD1;
// issue #96), on the sandbox stand-in stage, with no state poked: B held through the real
// NaviGatherState::exec, which calls pc_whistle_pluck with the growing circle's radius.
//
// The cursor P is held 60 ahead of the captain (mCursorPosition, as the stick would put it).
// Sprouts are planted around P: a at 3 (inside the min radius), b at 58, c at 85 (both inside R
// but outside the circle's first ticks) and d at 130 (outside R). R is NAVI_WHISTLE_MAX_RADIUS.
//
// In order, each within a bound in seconds:
//   Away   The captain walks sideways for 2 s, clear of the ship, and stands in Walk.
//   Plant  Sprouts a-d planted; each can be pulled, and the captain stands in Walk with no movie
//          for 1 s, within 30 s.
//   On     Whistle Pluck on. B held until the whistle ends by itself (Walk again), then 3 s:
//          - every tick of the real exec is checked against the call site: while B is down after
//            KEY_LoopStart the cooldown drops by the frame time; when it reaches 0 the sprout
//            nearest P with flat distance < min + frac (R - min) is plucked (frac = timer / expand
//            while the circle grows, mWhistleRadiusFrac once full), and on no other tick;
//          - so a, b and c in that order, a on the KEY_LoopStart tick (radius min), b only once the
//            circle reaches 58 (later than the 0.08 s interval would allow), c one interval after
//            b; d never;
//          - each pluck: one new Pikmin of the sprout's colour where it stood, in AutoNuki with
//            mPcWhistleNuki, and playerState->mTotalPluckedPikiCount +1;
//          - within 3 s each plucked Pikmin has left AutoNuki and mPcWhistleNuki is clear.
//   Off    Whistle Pluck off, a sprout e at 3 from P, the same full hold: nothing plucked
//          (e and d still on the PikiHead list, no Pikmin born, the pluck count unchanged).
//
// "Plucked" means taken off the PikiHead list (pc_whistle_pluck's kill(false) leaves health and AI
// state alone). canPullout alone is not used for that: a growing sprout passes through states it
// refuses (seen at 60 Hz for d, ~40 s after planting), so it only picks the expected candidate.
//
// Negative controls: d (outside R), b's late pluck (it would go one interval after a if the
// radius were ignored), and Off (the setting gates the call site).
// The setting is restored to off when the scenario ends.
//
// Not covered: the pause, UI-overlay, movie and day-end gates, Versus ownership, two captains.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Dolphin/pad.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "MoviePlayer.h"
#include "gameflow.h"
#include "pc_whistle_pluck.h"
#include "scenarios/scenario_tutorial_common.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

constexpr float kAhead   = 60.0f;
constexpr int kSprouts   = 5; // a, b, c, d, e
constexpr int kOnSprouts = 4;

class WhistlePluckHeld : public PcScenario {
public:
	const char* name() const override { return "whistle_pluck_held"; }

	void onStageReady() override
	{
		tutorial::applyDiscKeys();
		pc_settings_scenario_set_whistle_pluck(0);
		Navi* navi = naviMgr->getNavi();
		if (!navi || pikiMgr->getSize() != 0 || pc_settings_get_whistle_radius_pct() != 100) {
			fail(0, "a captain, no Pikmin and Whistle Radius 100%", "captain %d, Pikmin %d, pct %d", (int)(navi != nullptr), pikiMgr->getSize(),
			     pc_settings_get_whistle_radius_pct());
			return;
		}
		mR      = NAVI_WHISTLE_MAX_RADIUS(navi);
		mMin    = C_NAVI_PARM(navi, mWhistleMinRadius);
		mExpand = C_NAVI_PARM(navi, mWhistleExpandTime);
		std::printf("[scenario] whistle_pluck_held: radius %.1f..%.1f, expand %.2f s, hold %.2f s, interval %.2f s\n", mMin, mR, mExpand,
		            C_NAVI_PARM(navi, mWhistleHoldTime), PC_WHISTLE_PLUCK_INTERVAL);
		enter(Away, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Away) pad.stickX = 100;
		mDown = mPhase == Hold && !mWalkAgain;
		if (mDown) pad.button = PAD_BUTTON_B;
		Navi* navi = naviMgr->getNavi();
		// Standing through the demos sends the captain to Idle; a nudge brings him back to Walk.
		if (mPhase == Plant && navi && navi->getCurrState()->getID() == NAVISTATE_Idle) pad.stickX = 40;
		if (mPhase != Away && mPhase != Settle && navi) {
			navi->mCursorPosition.set(mP.x - navi->mSRT.t.x, 0.0f, mP.z - navi->mSRT.t.z);
			navi->mCursorTargetPosition = navi->mCursorPosition;
			navi->mCursorWorldPos       = mP;
		}
		(void)t;
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
				const Vector3f c = navi->mSRT.t;
				mWalkDir         = std::atan2(c.z - mAwayFrom.z, c.x - mAwayFrom.x);
				mP.set(c.x + kAhead * std::cos(mWalkDir), 0.0f, c.z + kAhead * std::sin(mWalkDir));
				mP.y = mapMgr->getMinY(mP.x, mP.z, true) + 1.0f;
				const float dist[kOnSprouts] = { 3.0f, 58.0f, 85.0f, 130.0f };
				const float ang[kOnSprouts]  = { 0.0f, 1.5708f, -1.5708f, 3.1416f };
				const int col[kOnSprouts]    = { Blue, Red, Yellow, Blue };
				for (int i = 0; i < kOnSprouts; ++i) {
					if (!plant(t, i, col[i], dist[i], ang[i])) return;
				}
				enter(Plant, t);
			} else if (s > 5) {
				fail(t, "the captain standing in Walk within 5 s", "state %d, speed %.1f", id, speed(navi));
			}
			return;
		case Plant: {
			// The On round's first red and yellow Pikmin play their colour's demo (cinemas
			// demo10/demo11) and the captain goes Idle; wait for him to stand in Walk with no movie
			// for 1 s.
			const bool calm = id == NAVISTATE_Walk && speed(navi) < 1.0f && !(gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive);
			if (!calm) mCalmTick = 0;
			else if (!mCalmTick) mCalmTick = t;
			if (!mCalmTick || t - mCalmTick < mRate) {
				if (s > 30) fail(t, "the captain back in Walk with no movie within 30 s", "state %d", id);
				return;
			}
			for (int i = 0; i < mPlanted; ++i) {
				if (mPlucked[i] || (mOn ? false : i < 3)) continue; // plucked in On
				if (!mSprout[i]->canPullout()) {
					if (s > 30) fail(t, "planted sprouts can be pulled within 30 s", "sprout %c", 'a' + i);
					return;
				}
			}
			for (int i = 0; i < mPlanted; ++i) {
				mAt[i]       = mSprout[i]->mSRT.t;
				mPullable[i] = pullable(i);
				mPresent[i]  = present(i);
				std::printf("[scenario] whistle_pluck_held: %s: sprout %c %.1f from the cursor\n", roundName(), 'a' + i, flatDist(mAt[i]));
			}
			mPluckCount0 = playerState->mTotalPluckedPikiCount;
			mPikis0      = pikiMgr->getSize();
			mNPlucked = 0;
			mGatherTick = mPhase1Tick = 0;
			mWalkAgain  = false;
			mPrevValid  = false;
			pc_settings_scenario_set_whistle_pluck(mOn ? 1 : 0);
			enter(Hold, t);
			return;
		}
		case Hold: hold(t, navi, s, id); return;
		default: return;
		}
	}

private:
	enum Phase { Away, Settle, Plant, Hold, Done };

	const char* roundName() const { return mOn ? "on" : "off"; }

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
		pc_settings_scenario_set_whistle_pluck(0);
		scenarioFailf(t, expected, "%s", buf);
	}

	static float speed(Navi* navi) { return std::sqrt(navi->mVelocity.x * navi->mVelocity.x + navi->mVelocity.z * navi->mVelocity.z); }

	float flatDist(const Vector3f& v) const { return std::sqrt((v.x - mP.x) * (v.x - mP.x) + (v.z - mP.z) * (v.z - mP.z)); }

	bool plant(unsigned t, int i, int colour, float dist, float angle)
	{
		const float a = mWalkDir + angle;
		mSprout[i]    = tutorial::plantSprout(colour, mP.x + dist * std::cos(a), mP.z + dist * std::sin(a));
		if (!mSprout[i]) {
			fail(t, "a sprout planted", "sprout %c: birth returned null", 'a' + i);
			return false;
		}
		mColour[i]  = colour;
		mPlucked[i] = 0;
		mPlanted    = i + 1;
		return true;
	}

	/// The sprout pc_whistle_pluck picks for `radius`: the nearest that can be pulled, flat d^2 < r^2, |dy| < 25.
	int expectedPick(float radius) const
	{
		int best   = -1;
		float bestSq = radius * radius;
		for (int i = 0; i < mPlanted; ++i) {
			if (!mPullable[i]) continue;
			const Vector3f d = mAt[i] - mP;
			if (std::fabs(d.y) >= 25.0f) continue;
			const float sq = d.x * d.x + d.z * d.z;
			if (sq < bestSq) {
				best   = i;
				bestSq = sq;
			}
		}
		return best;
	}

	/// Still on the field: pc_whistle_pluck's kill(false) takes the sprout off the PikiHead manager
	/// but leaves its health and AI state as they were. Slots plucked earlier (the Off round's e
	/// may reuse a's) do not count.
	bool present(int i) const
	{
		if (mPlucked[i] || (!mOn && i < 3)) return false;
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			if (*it == mSprout[i]) return true;
		}
		return false;
	}

	/// A growing sprout passes through states canPullout refuses (leaf to bud to flower).
	bool pullable(int i) const { return present(i) && mSprout[i]->canPullout(); }

	Piki* newWhistlePiki(int i) const
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			bool known = false;
			for (int k = 0; k < mNPlucked; ++k) known |= p == mPluckedPiki[k];
			if (!known && p->mPcWhistleNuki && p->mColor == mColour[i]) return p;
		}
		return nullptr;
	}

	void hold(unsigned t, Navi* navi, float s, int id)
	{
		// Which sprouts went this tick.
		int gone = -1, goneCount = 0;
		for (int i = 0; i < mPlanted; ++i) {
			const bool now = present(i);
			if (mPresent[i] && !now) {
				gone = i;
				goneCount++;
			}
		}

		// The call site. onTick runs after the sim, so the cooldown, timer and mode are last tick's
		// (exec reads them before it changes them); the anim phase is this tick's, because the
		// motion's KEY_LoopStart reaches procAnimMsg before exec runs in the same tick.
		if (id == NAVISTATE_Gather) mGather = static_cast<NaviGatherState*>(navi->getCurrState());
		const int phaseNow = mGather ? mGather->mWhistleAnimPhase : 0;
		if (mGather && mPrevGather && phaseNow == 1 && !mPhase1Tick) mPhase1Tick = t;
		int expect = -1;
		bool attempt = false;
		float radius = 0;
		if (mPrevValid && mPrevGather && phaseNow == 1 && mWasDown && mOn) {
			const float cd = mPrevCooldown - gsys->getFrameTime();
			if (cd <= 0.0f) {
				attempt = true;
				const float frac = mPrevMode == 1 ? mPrevTimer / mExpand : mPrevFrac;
				radius           = mMin + frac * (mR - mMin);
				expect           = expectedPick(radius);
			}
		}
		if (goneCount > 1 || gone != expect) {
			fail(t, "PL4: the whistle plucks the nearest sprout inside the circle whenever its cooldown is out, and only then",
			     "%s: tick %u after Gather: sprout %c went (%d gone), expected %c (attempt %d, radius %.1f); captain %.1f from it, state %d, plucked %u, Pikmin %d",
			     roundName(), t - mGatherTick, gone < 0 ? '-' : 'a' + gone, goneCount, expect < 0 ? '-' : 'a' + expect, (int)attempt, radius,
			     gone < 0 ? -1.0f : tutorial::flatDistance(navi->mSRT.t, mAt[gone]), id, playerState->mTotalPluckedPikiCount, pikiMgr->getSize());
			return;
		}
		if (gone >= 0) {
			Piki* p = newWhistlePiki(gone);
			const unsigned plucked = playerState->mTotalPluckedPikiCount;
			if (!p || p->getState() != PIKISTATE_AutoNuki || tutorial::flatDistance(p->mSRT.t, mAt[gone]) > 5.0f
			    || plucked != mPluckCount0 + unsigned(mNPlucked + 1) || pikiMgr->getSize() != mPikis0 + mNPlucked + 1) {
				fail(t, "PL4: a whistle pluck births the sprout's colour in AutoNuki with mPcWhistleNuki, where it stood, and counts it",
				     "sprout %c: Pikmin %d, state %d, %.1f away, plucked %u (from %u), Pikmin %d", 'a' + gone, (int)(p != nullptr),
				     p ? p->getState() : -1, p ? tutorial::flatDistance(p->mSRT.t, mAt[gone]) : -1.0f, plucked, mPluckCount0,
				     pikiMgr->getSize());
				return;
			}
			std::printf("[scenario] whistle_pluck_held: on: sprout %c (%.1f) plucked %u ticks after KEY_LoopStart at radius %.1f\n", 'a' + gone,
			            flatDist(mAt[gone]), t - mPhase1Tick, radius);
			mPlucked[gone]            = t;
			mPluckedPiki[mNPlucked++] = p;
		}

		// Record this tick's end state for the next tick's check.
		for (int i = 0; i < mPlanted; ++i) {
			mPresent[i]  = present(i);
			mPullable[i] = pullable(i);
		}
		mPrevGather = id == NAVISTATE_Gather;
		if (mPrevGather) {
			NaviGatherState* g = mGather;
			if (!mGatherTick) mGatherTick = t;
			mPrevPhase    = g->mWhistleAnimPhase;
			mPrevCooldown = g->mWhistlePluckCooldown;
			mPrevMode     = navi->mWhistleCircleMode;
			mPrevTimer    = navi->mWhistleTimer;
			mPrevFrac     = navi->mWhistleRadiusFrac;
		}
		mPrevValid = true;
		if (mGatherTick && id == NAVISTATE_Walk && !mWalkAgain) {
			mWalkAgain = true;
			mWalkTick  = t;
		}
		mWasDown = !mWalkAgain; // B next tick (providePads holds it until Walk)

		if (!mGatherTick && s > 1) {
			fail(t, "B enters Gather", "state %d", id);
			return;
		}
		if (mGatherTick && !mPhase1Tick && s > 1) {
			fail(t, "KEY_LoopStart within 1 s of Gather", "state %d", id);
			return;
		}
		if (!mWalkAgain) {
			if (s > 10) fail(t, "the held whistle ends by itself within 10 s", "state %d, mode %d", id, navi->mWhistleCircleMode);
			return;
		}
		if ((t - mWalkTick) < 3 * mRate) return;
		finishRound(t);
	}

	void finishRound(unsigned t)
	{
		const unsigned plucked = playerState->mTotalPluckedPikiCount;
		if (!mOn) {
			if (!present(3) || !present(4) || plucked != mPluckCount0 || pikiMgr->getSize() != mPikis0) {
				fail(t, "MD1: with Whistle Pluck off a held whistle plucks nothing", "e %d, d %d, plucked %u (from %u), Pikmin %d (from %d)",
				     (int)present(4), (int)present(3), plucked, mPluckCount0, pikiMgr->getSize(), mPikis0);
				return;
			}
			std::printf("[scenario] whistle_pluck_held: off: nothing plucked; PASS at tick %u\n", t);
			mPhase = Done;
			pc_settings_scenario_set_whistle_pluck(0);
			scenarioPass();
			return;
		}
		unsigned ivl = 0;
		for (float cd = PC_WHISTLE_PLUCK_INTERVAL; cd > 0.0f; cd -= 1.0f / float(mRate)) ivl++;
		const unsigned a = mPlucked[0], b = mPlucked[1], c = mPlucked[2];
		std::printf("[scenario] whistle_pluck_held: on: a +%d, b +%d, c +%d ticks after KEY_LoopStart, interval %u ticks, d %s\n",
		            a ? int(a - mPhase1Tick) : -1, b ? int(b - mPhase1Tick) : -1, c ? int(c - mPhase1Tick) : -1, ivl,
		            mPlucked[3] ? "plucked" : "left");
		if (!a || !b || !c || mPlucked[3] || !(a < b && b < c) || a != mPhase1Tick || b - a <= ivl || c - b != ivl) {
			fail(t, "PL4: a on the KEY_LoopStart tick, b once the circle reaches it (later than one interval), c one interval after b, d never",
			     "a +%d, b +%d, c +%d, d %d, interval %u", a ? int(a - mPhase1Tick) : -1, b ? int(b - mPhase1Tick) : -1, c ? int(c - mPhase1Tick) : -1,
			     (int)(mPlucked[3] != 0), ivl);
			return;
		}
		for (int k = 0; k < mNPlucked; ++k) {
			Piki* p = mPluckedPiki[k];
			if (p->getState() == PIKISTATE_AutoNuki || p->mPcWhistleNuki) {
				fail(t, "a whistle-plucked Pikmin leaves AutoNuki within 3 s and clears mPcWhistleNuki", "Pikmin %d: state %d, flag %d", k, p->getState(),
				     (int)p->mPcWhistleNuki);
				return;
			}
		}
		// Off: a fresh sprout e where a was.
		mOn = false;
		pc_settings_scenario_set_whistle_pluck(0);
		if (!plant(t, 4, Red, 3.0f, 0.0f)) return;
		enter(Plant, t);
	}

	Phase mPhase        = Away;
	bool mOn            = true;
	unsigned mCalmTick = 0, mPhaseTick = 0, mRate = 30, mGatherTick = 0, mPhase1Tick = 0, mWalkTick = 0, mPluckCount0 = 0;
	int mPikis0 = 0, mPlanted = 0, mNPlucked = 0;
	float mR = 0, mMin = 0, mExpand = 0, mWalkDir = 0;
	Vector3f mP, mAwayFrom, mAt[kSprouts];
	PikiHeadItem* mSprout[kSprouts] = {};
	NaviGatherState* mGather        = nullptr;
	int mColour[kSprouts]           = {};
	unsigned mPlucked[kSprouts]     = {};
	bool mPullable[kSprouts]        = {};
	bool mPresent[kSprouts]         = {};
	Piki* mPluckedPiki[kSprouts]    = {};
	// The state at the end of the previous tick.
	bool mDown = false, mWasDown = false, mWalkAgain = false, mPrevValid = false, mPrevGather = false;
	int mPrevPhase = 0, mPrevMode = 0;
	float mPrevCooldown = 0, mPrevTimer = 0, mPrevFrac = 0;
};

} // namespace

PcScenario* pc_scenario_make_whistle_pluck_held(void)
{
	static WhistlePluckHeld s;
	return &s;
}

#endif
