// free_play and first_yellow: scenarios that end by handing the game to the
// player (issue #76). Run with --scenario-handoff, the window is visible, the
// scripted part plays at real-time pacing, and at the hand-off point the runner
// detaches the scripted pad and gives the real keyboard, mouse and controllers
// the game (pc_scenario.cpp, scenarioHandOff). Run without it (CTest), the
// hand-off point is a pass: the scripted part reached the state it promises.
//
// free_play: no assertions. Boots the stage named by PIKMIN_SCENARIO_STAGE on
// Day 2 with Infinite Day on, so the clock never runs out (Infinite Day holds
// Days 2 to 29, not Day 1), and hands off on the first tick after the intro.
//
// first_yellow: the moment before the first yellow Pikmin, on the stand-in
// object stage (stages/sandbox_objects.ini, Day 1). The stage buries one sprout
// of each colour (standin-objects.md); a new game owns the red Onion only and
// has never plucked yellow. The captain walks with the stick to the yellow
// sprout at (-20, 180) and stops in reach of it (NaviMgr p62, 15 units), facing
// it, with no Pikmin. Asserted before the hand-off, each within a bound in
// seconds: the captain reaches the sprout and stops (Walk, within 1 unit of one
// spot for 0.5 s, within 15 units, facing it within 30 degrees) and yellow is still
// undiscovered: the sprout can be plucked, DEMOFLAG_PluckYellowPikmin is clear,
// the Yellow container and display bits are clear and no yellow Pikmin is on the
// field. The next A press is the first yellow pluck, which onion_unlock covers
// (demo flag 5, RESFLAG_MeetYellowPikminNoBomb, the Yellow bits). The stand-in
// tree has no yellow Onion and no yellow discovery movie (demo05), so the
// GameCube's Onion discovery is not part of it.
//
// Both apply the disc's key bindings first (scenarioApplyDiscKeyBindings). The
// stand-in generator writes parms/key.bin with the same binds (pluck on A).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "Demo.h"
#include "GoalItem.h"
#include "ItemAI.h"
#include "ItemMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "ObjType.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "settings/pc_settings.h"
#include "sysMath.h"

namespace {

constexpr int kYellow = 2; // PikiColor Yellow

class FreePlay : public PcScenario {
public:
	const char* name() const override { return "free_play"; }
	int startDay() const override { return 2; }
	bool handsOff() const override { return true; }
	bool ownsAllOnions() const override { return true; }
	void onStageReady() override
	{
		scenarioApplyDiscKeyBindings();
		pc_settings_scenario_set_infinite_day(1);
		// Every Onion of the stage must wait with its route open: a carrier of the pellet
		// that finds one in BootInit circles it (ActTransport, TUT_Rute). Print them.
		for (int colour = 0; colour < 3; colour++) {
			GoalItem* onion = itemMgr->getContainer(colour);
			std::printf("[scenario] free_play: Onion %d %s\n", colour,
			            !onion ? "absent" : (C_SAI(onion)->getCurrID(onion) == GoalAI::GOAL_Wait ? "waiting, route open" : "not waiting"));
		}
	}
	void onTick(unsigned) override { scenarioHandOff(); }
};

class FirstYellow : public PcScenario {
public:
	const char* name() const override { return "first_yellow"; }
	bool handsOff() const override { return true; }

	void onStageReady() override
	{
		scenarioApplyDiscKeyBindings();
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType != OBJTYPE_Pikihead) continue;
			PikiHeadItem* s = static_cast<PikiHeadItem*>(c);
			if (s->mSeedColor == kYellow && s->canPullout()) mSprout = s;
		}
		if (!mSprout) {
			scenarioFailf(0, "the object stage's buried yellow sprout", "no pluckable yellow sprout (PIKMIN_SCENARIO_STAGE=stages/sandbox_objects.ini?)");
			mDone = true;
			return;
		}
		std::printf("[scenario] first_yellow: yellow sprout at (%.0f, %.0f)\n", mSprout->mSRT.t.x, mSprout->mSRT.t.z);
		if (!yellowUndiscovered(0)) mDone = true;
	}

	void providePads(unsigned, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (mDone || mPhase != Approach || !mSprout || !navi || navi->getCurrState()->getID() != NAVISTATE_Walk) return;
		const Vector3f d = toSprout(navi);
		const float len  = std::sqrt(d.x * d.x + d.z * d.z);
		// Stick: +x east, +y north (-z) on the neutral control basis the scripted pad uses (as onion_unlock).
		if (len > kParkDistance) {
			pad.stickX = (int8_t)(d.x / len * 60.0f);
			pad.stickY = (int8_t)(-d.z / len * 60.0f);
		}
	}

	void onTick(unsigned t) override
	{
		if (mDone) return;
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		Navi* navi          = naviMgr->getNavi();
		const Vector3f d    = toSprout(navi);
		const float len     = std::sqrt(d.x * d.x + d.z * d.z);
		const int state     = navi->getCurrState()->getID();
		switch (mPhase) {
		case Approach:
			if (len <= kParkDistance) {
				std::printf("[scenario] first_yellow: in reach at tick %u (%.2f s), %.1f units from the sprout\n", t, t / (float)rate, len);
				mPhase     = Park;
				mPhaseTick = t;
				return;
			}
			if (t > kApproachSeconds * rate) {
				fail(t, "the captain walks to within 12 units of the yellow sprout within 15 s", "captain state %d, %.1f units away", state, len);
			}
			return;
		case Park:
		{
			// Stopped: the captain has stayed within 1 unit of one spot for half a second.
			const Vector3f step = navi->mSRT.t - mStillPos;
			if (std::sqrt(step.x * step.x + step.z * step.z) < 1.0f) {
				mStillTicks++;
			} else {
				mStillPos   = navi->mSRT.t;
				mStillTicks = 0;
			}
			const float facing = std::fabs(angDist(roundAng(std::atan2(d.x, d.z)), roundAng(navi->mFaceDirection)));
			if (state == NAVISTATE_Walk && mStillTicks >= rate / 2) {
				if (len > kReach || facing > kFacingRadians) {
					fail(t, "the parked captain is within 15 units of the yellow sprout and faces it within 30 degrees", "%.1f units, facing off by %.1f degrees",
					     len, facing * 180.0f / PI);
					return;
				}
				if (!yellowUndiscovered(t)) return;
				std::printf("[scenario] first_yellow: parked at tick %u (%.2f s), %.1f units from the yellow sprout, facing it within %.1f degrees, "
				            "captain at (%.1f, %.1f)\n",
				            t, t / (float)rate, len, facing * 180.0f / PI, navi->mSRT.t.x, navi->mSRT.t.z);
				mDone = true;
				scenarioHandOff();
				return;
			}
			if (t - mPhaseTick > kParkSeconds * rate) {
				fail(t, "the captain stops in Walk within 3 s of reaching the sprout (within 1 unit for 0.5 s)", "captain state %d, still for %u ticks", state,
				     mStillTicks);
			}
			return;
		}
		}
	}

private:
	enum Phase { Approach, Park };
	static constexpr float kParkDistance     = 12.0f;
	static constexpr float kReach            = 15.0f; // NaviMgr p62, the pluck reach
	static constexpr float kFacingRadians    = PI / 6.0f;
	static constexpr unsigned kApproachSeconds = 15;
	static constexpr unsigned kParkSeconds     = 3;

	Vector3f toSprout(Navi* navi) const
	{
		Vector3f d = mSprout->mSRT.t - navi->mSRT.t;
		d.y        = 0.0f;
		return d;
	}

	bool yellowUndiscovered(unsigned t)
	{
		int yellowOnField = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			if (static_cast<Piki*>(*it)->mColor == kYellow) yellowOnField++;
		}
		const bool flag = playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin);
		const bool bits = (playerState->mContainerFlag & (1 << kYellow)) || (playerState->mDisplayPikiFlag & (1 << kYellow));
		if (!mSprout->canPullout() || flag || bits || yellowOnField != 0) {
			fail(t, "yellow is undiscovered: the sprout can be plucked, DEMOFLAG_PluckYellowPikmin and the Yellow bits are clear, no yellow Pikmin",
			     "pluckable %d, flag %d, container 0x%02x, display 0x%02x, yellow Pikmin %d", (int)mSprout->canPullout(), (int)flag, playerState->mContainerFlag,
			     playerState->mDisplayPikiFlag, yellowOnField);
			return false;
		}
		return true;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mDone = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	PikiHeadItem* mSprout = nullptr;
	Phase mPhase          = Approach;
	unsigned mPhaseTick = 0, mStillTicks = 0;
	Vector3f mStillPos;
	bool mDone = false;
};

} // namespace

PcScenario* pc_scenario_make_free_play(void)
{
	static FreePlay s;
	return &s;
}

PcScenario* pc_scenario_make_first_yellow(void)
{
	static FirstYellow s;
	return &s;
}

#endif
