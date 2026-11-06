// flick_out_of_range: does a Pikmin that an enemy flicks out of its work-search range go passive, or go
// back to the fight? (fork issue #146, split out of #101.)
//
// The work-search range is p41, PikiMgr's mIdleAttackSearchRange ("敵のサーチエリア", code default 100, disc 70): the
// distance Piki::graspSituation (piki.cpp) looks for an enemy to attack, and the only place it is read
// on this path. graspSituation runs when a free Pikmin looks for work (ActFree::exec, Piki::actOnSituaton
// on landing or after a finished job). Getting up from a flick does not call it: PikiFlickState::cleanup
// (pikiState.cpp) asks the current action whether it is resumable, and ActAttack::resumable returns true,
// so ActAttack::restart calls init on the enemy it was fighting (mOther) with no distance test. A flicked
// attacker should therefore run back to the fight from any distance.
//
// The scenario: one blue Pikmin born 40 units from the Dwarf Bulborb and handed to actOnSituaton (it
// finds the Bulborb and attacks), the captain 90 units on the other side. When it reaches the Bulborb in
// attack mode (stuck on it or at its feet), the scenario flicks it as BTeki::flickUpper does (InteractFlick
// from the Bulborb, FLICK_BACKWARDS_ANGLE, no damage) with kFlickPower times the Bulborb's own upper flick
// power: at 3x it still lands inside p41. Every 0.25 s for 6 s it prints the Pikmin's state, mode,
// current action, attack target and distance to the Bulborb.
//
// Asserted: during the flick the Pikmin passes the work-search range (its distance to the Bulborb is over
// p41 plus the Bulborb's centre size, and graspSituation from there finds no enemy); from getting up until
// it reaches the Bulborb again it is in attack mode with the Bulborb as its attack target on every tick;
// and it reaches the Bulborb within 5 s of getting up. The distance at getting up is not asserted: the
// Bulborb is awake and walks after the Pikmin on some runs. Measured on stand-ins and the disc (Forest of
// Hope) at 30 and 60 Hz: not passive on any. Traces and stand-in differences: tools/scenarios/README.md.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "Interactions.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiAI.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "TekiParameters.h"
#include "scenarios/scenario_carry_common.h"
#include "teki.h"

namespace {

constexpr float kFlickPower  = 10.0f;  // times the Bulborb's TPF_UpperFlickPower
constexpr float kApproachS   = 10.0f; // to reach attack mode
constexpr float kWatchS      = 6.0f;  // printed after the flick (at least)
constexpr float kGetUpS      = 8.0f;  // flight plus knock-down (disc up to 3.83 s, stand-ins 2.23 s)
constexpr float kReturnS     = 5.0f;  // to reach the Bulborb again after getting up

// ActAttack keeps the enemy it fights in the protected mOther; a pointer to that member taken in a
// derived class reads it without changing the game's header.
struct AttackOther : ActAttack {
	static SmartPtr<Creature> ActAttack::*member() { return &AttackOther::mOther; }
};

class FlickOutOfRange : public PcScenario {
public:
	const char* name() const override { return "flick_out_of_range"; }

	void onStageReady() override
	{
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* t = static_cast<Teki*>(*it);
			if (t->mTekiType == TEKI_Chappy && !mBulb) mBulb = t;
		}
		Navi* navi = naviMgr->getNavi();
		if (!mBulb || !navi) {
			scenarioFailf(0, "a Dwarf Bulborb and a captain", "bulborb %d, captain %d", (int)(mBulb != nullptr), (int)(navi != nullptr));
			mDone = true;
			return;
		}
		const Vector3f at = mBulb->mSRT.t;
		navi->mSRT.t.set(at.x - 90.0f, mapMgr->getMinY(at.x - 90.0f, at.z, true), at.z);
		std::vector<Piki*> landing;
		carry::spawnRow(navi, Blue, 1, at.x + 40.0f, at.z, landing);
		if (landing.empty()) {
			scenarioFailf(0, "a blue Pikmin born beside the Bulborb", "none");
			mDone = true;
			return;
		}
		mPiki = landing[0];
		carry::land(landing);
		mRange = pikiMgr->mPikiParms->mPikiParms.mIdleAttackSearchRange();
		mPower = mBulb->getParameterF(TPF_UpperFlickPower);
		std::printf("[scenario] flick_out_of_range: Bulborb at (%.0f, %.0f), centre size %.1f; p41 work-search range %.1f; "
		            "p74/p43 knock-down %.2f-%.2f s; Bulborb upper flick power %.1f, applied %.1f\n",
		            at.x, at.z, mBulb->getCentreSize(), mRange, C_PIKI_PARM(mPiki, mMinFlickKnockDownTime), C_PIKI_PARM(mPiki, mMaxFlickKnockDownTime), mPower,
		            mPower * kFlickPower);
	}

	void providePads(unsigned, PcInputPad&) override {}

	void onTick(unsigned t) override
	{
		if (mDone) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const float limit   = mRange + mBulb->getCentreSize();

		if (!mFlickTick) {
			if (fighting()) {
				InteractFlick flick(mBulb, mPower * kFlickPower, 0.0f, FLICK_BACKWARDS_ANGLE);
				mPiki->stimulate(flick);
				mFlickTick = t;
				mFlickAt   = mPiki->mSRT.t;
				mBulbAt    = mBulb->mSRT.t;
				std::printf("[scenario] flick_out_of_range: t=%.2f s flicked at distance %.1f (stuck %d); state now %d\n", t / float(rate), dist(),
				            (int)(mPiki->mStickTarget == mBulb), mPiki->getState());
				if (mPiki->getState() != PIKISTATE_Flick) {
					fail(t, "the Pikmin enters Flick when flicked", "state %d", mPiki->getState());
				}
			} else if (t % rate == 0) {
				std::printf("[scenario] flick_out_of_range: t=%.0f s approach: state %d mode %d action %d target-bulborb %d stuck %d distance %.1f\n", t / float(rate),
				            mPiki->getState(), mPiki->mMode, action(), (int)(target() == mBulb), (int)(mPiki->mStickTarget == mBulb), dist());
			}
			if (!mFlickTick && t >= unsigned(kApproachS * rate)) {
				fail(t, "the Pikmin attacks the Bulborb within 10 s", "mode %d, action %d", mPiki->mMode, action());
			}
			return;
		}

		const unsigned since = t - mFlickTick;
		const int state      = mPiki->isAlive() ? mPiki->getState() : -1;
		if (state == PIKISTATE_Flick) {
			mMaxDist        = dist() > mMaxDist ? dist() : mMaxDist;
			const float fly = flat(mPiki->mSRT.t, mFlickAt);
			mMaxFly         = fly > mMaxFly ? fly : mMaxFly;
			// Out of the range: where graspSituation, the work search, finds no enemy at all.
			Creature* found = nullptr;
			if (!mOutTick && dist() > limit && mPiki->graspSituation(&found) != PIKISITCH_Unk1) {
				mOutTick = t;
				std::printf("[scenario] flick_out_of_range: t=%.2f s past the work-search range: distance %.1f, limit %.1f, graspSituation finds no enemy\n",
				            t / float(rate), dist(), limit);
			}
		} else if (!mUpTick) {
			mUpTick = t;
			std::printf("[scenario] flick_out_of_range: t=%.2f s got up after %.2f s at distance %.1f (furthest %.1f, limit %.1f); Pikmin flew %.1f, "
			            "Bulborb moved %.1f; state %d, mode %d, action %d, target is Bulborb %d\n",
			            t / float(rate), since / float(rate), dist(), mMaxDist, limit, mMaxFly, flat(mBulb->mSRT.t, mBulbAt), state, state >= 0 ? mPiki->mMode : -1,
			            state >= 0 ? action() : -9, (int)(state >= 0 && target() == mBulb));
			if (!mOutTick) {
				fail(t, "the flick takes the Pikmin past the work-search range (graspSituation finds no enemy)", "furthest %.1f, limit %.1f", mMaxDist, limit);
				return;
			}
		}
		// From getting up until it reaches the Bulborb again, every tick: still in attack mode on the Bulborb.
		if (mUpTick && !mReachTick) {
			if (state < 0 || mPiki->mMode != PikiMode::AttackMode || action() != PikiAction::Attack || target() != mBulb) {
				fail(t, "the Pikmin stays on the attack against the Bulborb after getting up (not passive)", "state %d, mode %d, action %d, target is Bulborb %d", state,
				     state >= 0 ? mPiki->mMode : -1, state >= 0 ? action() : -9, (int)(state >= 0 && target() == mBulb));
				return;
			}
			if (mPiki->mStickTarget == mBulb || dist() <= mBulb->getCentreSize() + 15.0f) {
				mReachTick = t;
				std::printf("[scenario] flick_out_of_range: t=%.2f s reached the Bulborb again %.2f s after getting up (distance %.1f, stuck %d)\n", t / float(rate),
				            (t - mUpTick) / float(rate), dist(), (int)(mPiki->mStickTarget == mBulb));
			} else if (t - mUpTick > unsigned(kReturnS * rate)) {
				fail(t, "the Pikmin reaches the Bulborb again within 5 s of getting up", "distance %.1f", dist());
				return;
			}
		}
		if (since % (rate / 4) == 0) {
			std::printf("[scenario] flick_out_of_range: +%.2f s state %d mode %d action %d target-bulborb %d stuck %d distance %.1f\n", since / float(rate), state,
			            state >= 0 ? mPiki->mMode : -1, state >= 0 ? action() : -9, (int)(state >= 0 && target() == mBulb), (int)(state >= 0 && mPiki->mStickTarget == mBulb),
			            dist());
		}
		// Getting up (flight plus knock-down) has its own bound; the return window (kReturnS) then
		// starts at mUpTick, so a long valid knock-down never shortens it.
		if (!mUpTick && since > unsigned(kGetUpS * rate)) {
			fail(t, "the Pikmin gets up within 8 s of the flick", "still down after %.2f s (state %d)", since / float(rate), state);
			return;
		}
		if (mReachTick && since >= unsigned(kWatchS * rate)) {
			std::printf("[scenario] flick_out_of_range: PASS at tick %u: out of range %.2f s after the flick, got up attacking the Bulborb, reached it %.2f s later\n", t,
			            (mOutTick - mFlickTick) / float(rate), (mReachTick - mUpTick) / float(rate));
			mDone = true;
			scenarioPass();
		}
	}

private:
	int action() const { return mPiki->mActiveAction->mCurrActionIdx; }
	Creature* target() const
	{
		if (action() != PikiAction::Attack) return nullptr;
		ActAttack* attack = static_cast<ActAttack*>(mPiki->mActiveAction->getCurrAction());
		return (attack->*AttackOther::member()).getPtr();
	}
	// In attack mode against the Bulborb and on it or at its feet (BTeki::flickLower reaches Pikmin there).
	bool fighting() const
	{
		return mPiki->isAlive() && mPiki->mMode == PikiMode::AttackMode && target() == mBulb
		    && (mPiki->mStickTarget == mBulb || dist() <= mBulb->getCentreSize() + 15.0f);
	}
	float dist() const { return qdist2(mPiki, mBulb); }
	static float flat(const Vector3f& a, const Vector3f& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
	void fail(unsigned t, const char* expected, const char* fmt, ...);

	Teki* mBulb  = nullptr;
	Piki* mPiki  = nullptr;
	bool mDone   = false;
	float mRange = 0.0f, mPower = 0.0f, mMaxDist = 0.0f, mMaxFly = 0.0f;
	Vector3f mFlickAt, mBulbAt;
	unsigned mFlickTick = 0, mOutTick = 0, mUpTick = 0, mReachTick = 0;
};

void FlickOutOfRange::fail(unsigned t, const char* expected, const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	mDone = true;
	scenarioFailf(t, expected, "%s", buf);
}

} // namespace

PcScenario* pc_scenario_make_flick_out_of_range(void)
{
	return new FlickOutOfRange();
}

#endif
