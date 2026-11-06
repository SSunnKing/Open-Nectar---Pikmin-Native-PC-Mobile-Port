// carry_blocked: a carried pellet whose route is closed (coverage/milestones.md DF28, "the carry is
// blocked" text TUT_Rute), on the object stage, stages/sandbox_objects.ini: a blue Onion, a red
// 1-pellet and open floor. The Pikmin are the game's own (free Pikmin handed to
// Piki::actOnSituaton, as in pellet_carry); the route is closed the way the game closes it, by
// WayPoint::setFlag(false) on the Onion's waypoint (what GoalItem::startAI does to an Onion that
// has not been booted yet), so ActTransport::crMove/moveToWayPoint put the carriers into guru-guru
// (STATE_Guru) at the last open point. The Onion is booted first (GoalItem::pcSnapLanded, the
// skip the tutorial uses), so the route is open until the scenario closes it.
//
// In order, each within a bound in seconds:
//   Open     Two blue Pikmin carry the red 1-pellet to the open Onion: the pellet is delivered and
//            DEMOFLAG_CarryPathBlocked (28) stays clear and no text opens (negative control).
//   Blocked  A second 1-pellet and two blue Pikmin, the Onion's waypoint closed: the pellet moves,
//            stops short of the Onion and the carriers spin (guru-guru: moveGuruGuru's wait timer
//            counts up 2 s); then DF28 sets, and only it (flags 7 and 9 are stage timers), with
//            TUT_Rute (id 23) as the one text window, closed with A. The pellet is not delivered
//            for 5 s more and the flag stays set.
//   Reopen   The waypoint is opened again: the carriers set off (STATE_Move) and deliver the pellet.
//
// Negative control (manual): do not close the waypoint in Blocked and the phase fails: the pellet is
// delivered with DF28 clear.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Demo.h"
#include "ItemAI.h"
#include "NaviMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "Piki.h"
#include "Route.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kSpotX = -300.0f, kSpotZ = -60.0f; // open floor on the Onion's side, away from the sprouts and the Bulborb

class CarryBlocked : public PcScenario {
public:
	const char* name() const override { return "carry_blocked"; }

	void onStageReady() override
	{
		mOnion = itemMgr->getContainer(Blue);
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p->mConfig->mModelId.mId == id("pr01")) mPellet = p;
		}
		std::printf("[scenario] carry_blocked: blue Onion %d, red 1-pellet %d, flags %s\n", (int)(mOnion != nullptr), (int)(mPellet != nullptr), setBits(demoFlags()).c_str());
		if (!mOnion || !mPellet || playerState->mDemoFlags.isFlag(DEMOFLAG_CarryPathBlocked)) {
			fail(0, "the object stage's blue Onion and red 1-pellet, flag 28 clear", "Onion %d, pellet %d", (int)(mOnion != nullptr), (int)(mPellet != nullptr));
			return;
		}
		mOnion->pcSnapLanded();
		mWay = routeMgr->getWayPoint('test', mOnion->getRouteIndex());
		if (!mWay || !mWay->mIsOpen) {
			fail(0, "the booted Onion's waypoint is open", "waypoint %d", (int)(mWay != nullptr));
			return;
		}
		mFlags = demoFlags();
		mStart = mPellet->mSRT.t;
		mTexts = pc_scenario_text_windows_opened(nullptr);
		spawnRow(naviMgr->getNavi(), Blue, 2, mStart.x + 25.0f, mStart.z, mLanding);
		enter(Open, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (gameflow.mIsUIOverlayActive && t % 6 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		land(mLanding);
		if (t % (5 * rate) == 0 && mPellet) {
			std::printf("[scenario-progress] t=%u phase %d: pellet state %d carriers %d at (%.0f, %.0f), flag 28 %d\n", t, (int)mPhase, mPellet->getState(), (int)mPellet->mCarrierCounter,
			            mPellet->mSRT.t.x, mPellet->mSRT.t.z, (int)playerState->mDemoFlags.isFlag(DEMOFLAG_CarryPathBlocked));
		}
		switch (mPhase) {
		case Open:
			if (!freshFlags().empty() || pc_scenario_text_windows_opened(nullptr) != mTexts) {
				fail(t, "DF28: a carry on an open route sets no flag and opens no text", "new flags %s, windows %d", list(freshFlags()).c_str(), pc_scenario_text_windows_opened(nullptr) - mTexts);
				return;
			}
			if (delivered()) {
				std::printf("[scenario] carry_blocked: the pellet reached the open Onion at t=%u (%.1f s), flag 28 clear, no text\n", t, span / float(rate));
				mPellet = spawnPellet(id("pr01"), kSpotX, kSpotZ);
				if (!mPellet) {
					fail(t, "a second 1-pellet is born", "newPellet returned null");
					return;
				}
				mWay->setFlag(false);
				mStart = mPellet->mSRT.t;
				spawnRow(naviMgr->getNavi(), Blue, 2, mStart.x + 25.0f, mStart.z, mLanding);
				enter(Blocked, t);
			} else if (span > 90 * rate) {
				fail(t, "the pellet reaches the open Onion within 90 s", "state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(), (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x,
				     mPellet->mSRT.t.z);
			}
			return;
		case Blocked: {
			if (delivered()) {
				fail(t, "DF28: with the Onion's route closed the pellet is not delivered", "delivered at %.1f s, flags %s", span / float(rate), list(newFlags(mFlags, demoFlags())).c_str());
				return;
			}
			if (!mSetAt) {
				const std::vector<int> fresh = freshFlags();
				if (fresh.empty()) {
					if (span > 60 * rate) {
						fail(t, "DF28: the blocked carry sets flag 28 within 60 s", "no new flag, pellet at (%.0f, %.0f), moved %.1f, carriers %d", mPellet->mSRT.t.x, mPellet->mSRT.t.z,
						     flatDistance(mPellet->mSRT.t, mStart), (int)mPellet->mCarrierCounter);
					}
					return;
				}
				mSetAt = t;
				const float moved = flatDistance(mPellet->mSRT.t, mStart);
				std::printf("[scenario] carry_blocked: flag(s) %s %.1f s after the Pikmin took hold, pellet moved %.1f, at (%.0f, %.0f), windows %d (was %d)\n", list(fresh).c_str(), span / float(rate),
				            moved, mPellet->mSRT.t.x, mPellet->mSRT.t.z, pc_scenario_text_windows_opened(nullptr), mTexts);
				if (fresh.size() != 1 || fresh[0] != DEMOFLAG_CarryPathBlocked) {
					fail(t, "DF28: the blocked carry sets flag 28 and no other", "new flags %s", list(fresh).c_str());
					return;
				}
				if (moved < 5.0f) {
					fail(t, "the pellet is carried toward the closed point before it spins", "moved %.1f", moved);
					return;
				}
				mHeld = mPellet->mSRT.t;
			}
			int textId       = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTexts) {
				if (opened != mTexts + 1 || textId != zen::ogScrTutorialMgr::TUT_Rute) {
					fail(t, "one text window, TUT_Rute (23)", "%d windows, text id %d", opened - mTexts, textId);
					return;
				}
				mSawText = true;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				if (!mClosedAt) {
					mClosedAt = t;
					std::printf("[scenario] carry_blocked: TUT_Rute (%d) opened and closed, %.2f s after the flag\n", textId, (t - mSetAt) / float(rate));
				}
				if (t - mClosedAt >= 5 * rate) {
					if (flatDistance(mPellet->mSRT.t, mHeld) > 60.0f || !playerState->mDemoFlags.isFlag(DEMOFLAG_CarryPathBlocked)) {
						fail(t, "the blocked pellet stays near the closed point and flag 28 stays set", "moved %.1f since the flag", flatDistance(mPellet->mSRT.t, mHeld));
						return;
					}
					std::printf("[scenario] carry_blocked: 5 s later the pellet is still short of the Onion (%.1f from where it blocked); opening the route\n", flatDistance(mPellet->mSRT.t, mHeld));
					mWay->setFlag(true);
					enter(Reopen, t);
				}
			} else if (mSetAt && t - mSetAt > 15 * rate) {
				fail(t, "TUT_Rute opens and closes within 15 s of flag 28", "windows %d (was %d), overlay %d", opened, mTexts, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		}
		case Reopen:
			if (delivered()) {
				std::printf("[scenario] carry_blocked: route open again, the pellet was delivered %.1f s later; flags %s\n", span / float(rate), setBits(demoFlags()).c_str());
				mPhase = Done;
				scenarioPass();
			} else if (span > 60 * rate) {
				fail(t, "with the route open again the carriers deliver the pellet within 60 s", "state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(), (int)mPellet->mCarrierCounter,
				     mPellet->mSRT.t.x, mPellet->mSRT.t.z);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Open, Blocked, Reopen, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	bool delivered() { return !mPellet->isAlive() || mPellet->getState() == PELSTATE_Dead; }

	/// New demo flags since mFlags, less the timer flags 7 and 9 that the stage sets by itself.
	std::vector<int> freshFlags() const
	{
		std::vector<int> out;
		for (int f : newFlags(mFlags, demoFlags())) {
			if (f != DEMOFLAG_NoPikminTimeout && f != DEMOFLAG_Unk9) out.push_back(f);
		}
		return out;
	}

	Phase mPhase        = Open;
	unsigned mPhaseTick = 0, mSetAt = 0, mClosedAt = 0;
	int mTexts = 0;
	bool mSawText = false;
	GoalItem* mOnion = nullptr;
	Pellet* mPellet  = nullptr;
	WayPoint* mWay   = nullptr;
	Vector3f mStart, mHeld;
	std::vector<int> mFlags;
	std::vector<Piki*> mLanding;
};

} // namespace

PcScenario* pc_scenario_make_carry_blocked(void)
{
	static CarryBlocked s;
	return &s;
}

#endif
