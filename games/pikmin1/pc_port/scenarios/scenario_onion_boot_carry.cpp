// Onion boot-then-carry regression (#100 W2-4): do not skip the boot,
// call pcSnapLanded, or write waypoint flags. Start the original boot API and
// let the real animator/GoalAI emit the first seed and return to Wait. Its route
// must then be open and two Pikmin must deliver a pellet. On the stand-in tree the
// boot motion is the authored one (nectar/standin_onion_boot.h). The local exact lane
// (the _disc entries, PIKMIN_ONION_BOOT_EXACT=1 on a user-supplied install) checks the
// original game's route-effect and seed frames instead, per colour.
// This isolates animation/routing, not the discovery trigger or movie replay.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Demo.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemAI.h"
#include "NaviMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PlayerState.h"
#include "Route.h"
#include "gameflow.h"
#include "nectar_assets/standin_onion_boot.h"
#include "scenarios/scenario_carry_common.h"

namespace {

class OnionBootCarry : public PcScenario {
public:
	explicit OnionBootCarry(int colour) : mColour(colour) { }
	const char* name() const override { return mColour == Blue ? "blue_onion_boot" : mColour == Yellow ? "yellow_onion_boot" : "red_onion_boot"; }
	int startDay() const override { return 2; }
	const char* stageFile() const override
	{
		return mColour == Blue ? "stages/sandbox_objects.ini" : "stages/sandbox_onions.ini";
	}

	void onStageReady() override
	{
		mOnion = itemMgr->getContainer(mColour);
		if (!mOnion) {
			scenarioFailf(0, "the stage's target Onion", "colour %d missing", mColour);
			return;
		}
		mWay = routeMgr->getWayPoint('test', mOnion->getRouteIndex());
		const int state = C_SAI(mOnion)->getCurrID(mOnion);
		if (!mWay || mWay->mIsOpen || playerState->hasBootContainer(mColour)) {
			scenarioFailf(0, "an Onion with no boot bit and its route closed", "colour %d state %d waypoint %d open %d boot bit %d",
			              mColour, state, (int)(mWay != nullptr), mWay ? (int)mWay->mIsOpen : -1,
			              (int)playerState->hasBootContainer(mColour));
			return;
		}
		std::printf("[scenario] %s: pre-boot AI state %d, boot bit clear, route closed\n", name(), state);
		mBornBeforeBoot = GameStat::bornPikis[mColour];
		// The normal Boot movie event calls this API. No state or waypoint poke.
		mOnion->startBoot();
	}

	// Measured expected outputs of the original game, used only by the local exact lane.
	static bool exact() { const char* v = std::getenv("PIKMIN_ONION_BOOT_EXACT"); return v && v[0] == '1' && v[1] == '\0'; }
	float routeFrameFor() const { return exact() ? (mColour == Red ? 221.0f : 194.0f) : float(nectar::kOnionBootRouteFrame); }
	float seedFrameFor() const { return exact() ? (mColour == Red ? 270.0f : 240.0f) : float(nectar::kOnionBootSeedFrame); }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// Close unrelated first-seed tutorial text, without skipping a movie.
		if (gameflow.mIsTutorialTextActive && t % 6 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (mPhase == Boot) {
			const int state = C_SAI(mOnion)->getCurrID(mOnion);
			const int seeds = GameStat::bornPikis[mColour] - mBornBeforeBoot;
			const float frame = mOnion->mItemAnimator.getCounter();
			if (mOnion->mItemAnimator.getCurrentMotionIndex() == mColour + 3) {
				// PaniAnimator dispatches effects over [oldFrame, newFrame).
				const float routeFrame = routeFrameFor();
				if (mWay->mIsOpen != (frame > routeFrame)) {
					scenarioFailf(t, "route closed through the effect frame, then open", "colour %d motion frame %.2f, effect %.0f, open %d", mColour, frame, routeFrame, int(mWay->mIsOpen));
					return;
				}
				if (mWay->mIsOpen && !mSawOpen) {
					mSawOpen = true;
					std::printf("[scenario] %s: route opened at motion frame %.2f, %.3f s\n", name(), frame, t / float(rate));
				}
				if (seeds && !mSawSeed) {
					const float seedFrame = seedFrameFor();
					if (frame < seedFrame || frame > seedFrame + 2.0f) {
						scenarioFailf(t, "first seed at the boot motion's Action0 frame (within two animation frames for AI dispatch)", "frame %.2f expected %.0f", frame, seedFrame);
						return;
					}
					mSawSeed = true;
					std::printf("[scenario] %s: first seed at motion frame %.2f, %.3f s\n", name(), frame, t / float(rate));
				}
			}
			if (state == GoalAI::GOAL_Wait && seeds == 1) {
				std::printf("[scenario] %s: natural boot finished at %.2f s, one seed, waypoint open %d\n",
				            name(), t / float(rate), (int)mWay->mIsOpen);
				if (!mWay->mIsOpen || !mSawOpen || !mSawSeed) {
					scenarioFailf(t, "natural Onion boot opens its route before carry", "colour %d returned to Wait after one seed but waypoint stayed closed", mColour);
					return;
				}
				// Place a pellet near the Onion; use the real carry AI on either map.
				mPellet = carry::spawnPellet(carry::id("pr01"), mOnion->mSRT.t.x + 100.0f, mOnion->mSRT.t.z);
				if (!mPellet) {
					scenarioFailf(t, "a red 1-pellet", "spawn failed");
					return;
				}
				carry::spawnRow(naviMgr->getNavi(), mColour, 2, mPellet->mSRT.t.x + 25.0f, mPellet->mSRT.t.z, mLanding);
				if (mLanding.size() != 2) {
					scenarioFailf(t, "two same-colour carriers", "%zu born", mLanding.size());
					return;
				}
				mPhase = Carry;
				mCarryTick = t;
				return;
			}
			if (seeds > 1 || t > 30 * rate) {
				scenarioFailf(t, "natural boot returns to Wait with exactly one seed within 30 s", "colour %d state %d seeds %d waypoint open %d",
				              mColour, state, seeds, (int)mWay->mIsOpen);
			}
			return;
		}
		carry::land(mLanding);
		if (!mWay->mIsOpen) {
			scenarioFailf(t, "naturally opened route stays open during carry", "closed after boot");
			return;
		}
		if (!mPellet->isAlive() || mPellet->getState() == PELSTATE_Dead) {
			if (mPellet->mTargetGoal != mOnion) {
				scenarioFailf(t, "pellet routed to the naturally booted Onion", "different target");
				return;
			}
			std::printf("[scenario] %s: pellet delivered %.2f s after carry started\n", name(), (t - mCarryTick) / float(rate));
			scenarioPass();
		} else if (t - mCarryTick > 90 * rate) {
			scenarioFailf(t, "two Pikmin deliver the pellet within 90 s after natural boot", "colour %d state %d carriers %d at (%.0f, %.0f)",
			              mColour, mPellet->getState(), (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
		}
	}

private:
	enum Phase { Boot, Carry };
	int mColour;
	Phase mPhase = Boot;
	GoalItem* mOnion = nullptr;
	WayPoint* mWay = nullptr;
	Pellet* mPellet = nullptr;
	bool mSawOpen = false, mSawSeed = false;
	int mBornBeforeBoot = 0;
	unsigned mCarryTick = 0;
	std::vector<Piki*> mLanding;
};

} // namespace

PcScenario* pc_scenario_make_blue_onion_boot(void)
{
	static OnionBootCarry s(Blue);
	return &s;
}

PcScenario* pc_scenario_make_red_onion_boot(void)
{
	static OnionBootCarry s(Red);
	return &s;
}

PcScenario* pc_scenario_make_yellow_onion_boot(void)
{
	static OnionBootCarry s(Yellow);
	return &s;
}
#endif
