// bomb_firsts: the three first-time bomb flags (coverage/milestones.md DF18, DF20, DF27), on the
// sandbox stage. The bomb is the game's own BombItem (itemMgr->birth(OBJTYPE_Bomb), startAI), the
// Pikmin act on it through Piki::actOnSituaton as in tutorial_day1, and the bomb is lit the way
// ActPutBomb lights one: a MsgUser(0) to its BombAI (set, fuse mBombSetFuseTime 2 s), then the fuse
// ends and BombAI::BombInit explodes it.
//
// In order, each within a bound in seconds:
//   Red     A red Pikmin beside the bomb: only a yellow Pikmin picks a bomb up (Piki::graspSituation),
//           so for 3 s it does nothing: the bomb is not held, flag 18 stays clear (negative control).
//   Yellow  A yellow Pikmin beside it picks the bomb up (ActPickItem, hasBomb) and, back in the captain's
//           party (ActCrowd::setFormed or the formed walk), DEMOFLAG_GrabFirstBomb (18) sets: only that
//           flag (the stage's timers 7 and 9 aside) and the movie DEMOID_YellowWithBomb is asked for.
//   Fuse    A second bomb, 400 units off (beyond the 50-unit blast of Pikmin and captain), is lit. It is
//           out of sight while set, and after the fuse (2 s, a few percent over) explodes. Flag 20 does
//           not set with the blast: BombInit starts DemoFlags' 1.5 s timer (setTimer); when it runs out
//           with no movie playing the game sets 20 and opens TUT_BombInfo (id 20, two pages, A closes
//           both). Flag 27 stays clear: nothing was in range.
//   Death   A red Pikmin is given a bomb interaction (InteractBomb, mBombDamagePiki): flag 27
//           (DEMOFLAG_FirstBombDeath) sets, and no other flag; the Pikmin is hurt (flick/dying).
//
// The pickup needs KEY_Action0 on PIKIANIM_Pick (ActPick grabs the bomb on it): standin_anims kAnimInfoPick.
//
// Not covered: the movie behind flag 18 (demo68; a missing cinema is a zero-length movie, allowlisted),
// a Pikmin throwing the bomb itself (ActPutBomb), the explosion's effects and damage to enemies.
//
// Negative control (manual): put the Red phase's Pikmin as yellow and its phase fails because the bomb
// is held.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "BombItem.h"
#include "Demo.h"
#include "Interactions.h"
#include "ItemAI.h"
#include "NaviMgr.h"
#include "Parameters.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kBombX = 0.0f, kBombZ = 120.0f; // 120 units from the captain, open floor
constexpr float kFarX = 0.0f, kFarZ = 400.0f;   // the second bomb: well outside the 50-unit blast

class BombFirsts : public PcScenario {
public:
	const char* name() const override { return "bomb_firsts"; }

	void onStageReady() override
	{
		mBomb  = spawnBomb(kBombX, kBombZ);
		mFlags = demoFlags();
		std::printf("[scenario] bomb_firsts: bomb %d, flags %s, fuse %.1f s, blast radius %.1f\n", (int)(mBomb != nullptr), setBits(mFlags).c_str(),
		            pikiMgr->mPikiParms->mPikiParms.mBombSetFuseTime(), pikiMgr->mPikiParms->mPikiParms.mBombExplodeRadius());
		if (!mBomb || playerState->mDemoFlags.isFlag(DEMOFLAG_GrabFirstBomb) || playerState->mDemoFlags.isFlag(DEMOFLAG_FirstBombExplode)
		    || playerState->mDemoFlags.isFlag(DEMOFLAG_FirstBombDeath)) {
			fail(0, "a new game with a bomb and flags 18, 20 and 27 clear", "bomb %d", (int)(mBomb != nullptr));
			return;
		}
		spawnRow(naviMgr->getNavi(), Red, 1, kBombX + 6.0f, kBombZ, mLanding);
		enter(RedPhase, 0);
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
		Navi* navi          = naviMgr->getNavi();
		land(mLanding);
		switch (mPhase) {
		case RedPhase:
			if (mBomb->isGrabbed() || !freshFlags().empty()) {
				fail(t, "DF18: a red Pikmin does not pick a bomb up and sets no flag", "held %d, new flags %s", (int)mBomb->isGrabbed(), list(freshFlags()).c_str());
				return;
			}
			if (span >= 3 * rate) {
				std::printf("[scenario] bomb_firsts: 3 s with a red Pikmin beside the bomb: not held, no flag\n");
				spawnRow(navi, Yellow, 1, kBombX - 6.0f, kBombZ, mLanding);
				enter(YellowPhase, t);
			}
			return;
		case YellowPhase: {
			const std::vector<int> fresh = freshFlags();
			if (!fresh.empty()) {
				std::printf("[scenario] bomb_firsts: flag(s) %s at t=%u, bomb held %d, movie %d\n", list(fresh).c_str(), t, (int)mBomb->isGrabbed(), currentMovie());
				if (fresh.size() != 1 || fresh[0] != DEMOFLAG_GrabFirstBomb) {
					fail(t, "DF18: the yellow Pikmin with the bomb sets flag 18 and no other", "new flags %s", list(fresh).c_str());
					return;
				}
				if (!mBomb->isGrabbed()) {
					fail(t, "DF18: the bomb is held when the flag sets", "held 0");
					return;
				}
				mFlags = demoFlags();
				mFar   = spawnBomb(kFarX, kFarZ);
				if (!mFar) {
					fail(t, "a second bomb is born", "birth returned null");
					return;
				}
				MsgUser msg(0);
				C_SAI(mFar)->procMsg(mFar, &msg);
				mLit   = t;
				mTexts0 = pc_scenario_text_windows_opened(nullptr);
				enter(Fuse, t);
			} else if (span > 30 * rate) {
				fail(t, "the yellow Pikmin picks the bomb up and DF18 sets within 30 s", "held %d, new flags %s", (int)mBomb->isGrabbed(), list(fresh).c_str());
			}
			return;
		}
		case Fuse: {
			const int state = mFar->getCurrState()->getID();
			if (state == BombAI::BOMB_Bomb && !mBlast) {
				mBlast = t;
				const float fuse = (mBlast - mLit) / float(rate);
				std::printf("[scenario] bomb_firsts: the bomb exploded %.2f s after it was lit (fuse %.1f s)\n", fuse, pikiMgr->mPikiParms->mPikiParms.mBombSetFuseTime());
				const float want = pikiMgr->mPikiParms->mPikiParms.mBombSetFuseTime();
				if (fuse < want - 0.1f || fuse > want * 1.05f + 0.2f) {
					fail(t, "the fuse is mBombSetFuseTime (2 s, up to 5% more)", "%.2f s", fuse);
					return;
				}
			}
			if (!mBlast) {
				if (mFar->isVisible() || mFar->isAlive()) {
					fail(t, "a lit bomb is hidden (Set state) until it explodes", "visible %d alive %d state %d", (int)mFar->isVisible(), (int)mFar->isAlive(), state);
					return;
				}
				if (!freshFlags().empty() || span > 6 * rate) {
					fail(t, "the lit bomb explodes within 6 s, setting no flag before", "new flags %s, state %d", list(freshFlags()).c_str(), state);
					return;
				}
				return;
			}
			const std::vector<int> fresh = freshFlags();
			int textId                   = -1;
			const int opened             = pc_scenario_text_windows_opened(&textId);
			if (fresh.empty()) {
				if (t - mBlast > 4 * rate) {
					fail(t, "DF20: flag 20 sets within 4 s of the first bomb exploding", "no new flag, state %d", state);
					return;
				}
				if (opened != mTexts0) {
					fail(t, "the text waits for the flag (they come together)", "%d windows before the flag", opened - mTexts0);
					return;
				}
				return;
			}
			const float delay = (t - mBlast) / float(rate);
			std::printf("[scenario] bomb_firsts: flag(s) %s %.2f s after the blast, windows %d (was %d)\n", list(fresh).c_str(), delay, opened, mTexts0);
			if (fresh.size() != 1 || fresh[0] != DEMOFLAG_FirstBombExplode) {
				fail(t, "DF20: the blast sets flag 20 and no other (27: nothing was in range)", "new flags %s", list(fresh).c_str());
				return;
			}
			if (delay < 1.3f || delay > 2.2f) {
				fail(t, "DF20: flag 20 comes about 1.5 s after the blast (DemoFlags::setTimer)", "%.2f s", delay);
				return;
			}
			mFlags   = demoFlags();
			mWindow  = t;
			enter(Text, t);
			return;
		}
		case Text: {
			int textId       = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTexts0) {
				if (opened != mTexts0 + 1 || textId != zen::ogScrTutorialMgr::TUT_BombInfo) {
					fail(t, "one text window, TUT_BombInfo (20)", "%d windows, text id %d", opened - mTexts0, textId);
					return;
				}
				mSawText = true;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				std::printf("[scenario] bomb_firsts: TUT_BombInfo (%d) opened, closed after %.2f s\n", textId, (t - mWindow) / float(rate));
				mVictim = spawnOne(Red);
				if (!mVictim) {
					fail(t, "a red Pikmin is born", "birth returned null");
					return;
				}
				enter(Death, t);
			} else if (span > 15 * rate) {
				fail(t, "the bomb text opens and closes within 15 s", "windows %d (was %d), overlay %d", opened, mTexts0, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		}
		case Death:
			if (span == 3) {
				if (playerState->mDemoFlags.isFlag(DEMOFLAG_FirstBombDeath) || !freshFlags().empty()) {
					fail(t, "DF27: no flag before the Pikmin meets a bomb", "new flags %s", list(freshFlags()).c_str());
					return;
				}
				InteractBomb act(navi, pikiMgr->mPikiParms->mPikiParms.mBombDamagePiki(), nullptr);
				const bool hit = mVictim->stimulate(act);
				std::printf("[scenario] bomb_firsts: InteractBomb on a red Pikmin: stimulate %d, state %d, health %.0f\n", (int)hit, mVictim->getState(), mVictim->mHealth);
				const std::vector<int> fresh = freshFlags();
				if (fresh.size() != 1 || fresh[0] != DEMOFLAG_FirstBombDeath) {
					fail(t, "DF27: a bomb on a Pikmin sets flag 27 and no other", "stimulate %d, new flags %s", (int)hit, list(fresh).c_str());
					return;
				}
				if (mVictim->getState() != PIKISTATE_Flick && mVictim->getState() != PIKISTATE_Dying && mVictim->getState() != PIKISTATE_Dead) {
					fail(t, "the bombed Pikmin is hurt (flick, dying or dead)", "state %d", mVictim->getState());
					return;
				}
				mPhase = Done;
				std::printf("[scenario] bomb_firsts: flags now %s\n", setBits(demoFlags()).c_str());
				scenarioPass();
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { RedPhase, YellowPhase, Fuse, Text, Death, Done };

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

	/// New demo flags since mFlags, less the timer flags 7 and 9 that the stage sets by itself.
	std::vector<int> freshFlags() const
	{
		std::vector<int> out;
		for (int f : newFlags(mFlags, demoFlags())) {
			if (f != DEMOFLAG_NoPikminTimeout && f != DEMOFLAG_Unk9) out.push_back(f);
		}
		return out;
	}

	BombItem* spawnBomb(float x, float z)
	{
		BombItem* bomb = static_cast<BombItem*>(itemMgr->birth(OBJTYPE_Bomb));
		if (!bomb) return nullptr;
		Vector3f at(x, mapMgr->getMinY(x, z, true), z);
		bomb->init(at);
		bomb->startAI(0);
		return bomb;
	}

	Piki* spawnOne(int colour)
	{
		Navi* navi = naviMgr->getNavi();
		Piki* p    = spawnPiki(navi, colour, PikiMode::FreeMode, 30.0f, 30.0f);
		GameStat::update();
		return p;
	}

	Phase mPhase        = RedPhase;
	unsigned mPhaseTick = 0, mLit = 0, mBlast = 0, mWindow = 0;
	int mTexts0 = 0;
	bool mSawText = false;
	BombItem* mBomb = nullptr;
	BombItem* mFar  = nullptr;
	Piki* mVictim   = nullptr;
	std::vector<int> mFlags;
	std::vector<Piki*> mLanding;
};

} // namespace

PcScenario* pc_scenario_make_bomb_firsts(void)
{
	static BombFirsts s;
	return &s;
}

#endif
