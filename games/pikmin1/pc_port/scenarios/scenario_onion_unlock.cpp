// onion_unlock and onion_unlock_whistle: Onion ownership and the first-pluck
// unlock chain (coverage/milestones.md ON1, ON2, ON3, ON5, DF04, DF05, DF06,
// AC2, PL4; edge-cases.md EC-29 and EC-33). Both run on the opt-in object stage,
// stages/sandbox_objects.ini, whose default.gen buries one sprout of each colour
// (blue at (-100, 120), red at (-60, 150), yellow at (-20, 180)) and places a blue
// Onion from a "next" generator (standin-objects.md).
//
// Baseline (both scenarios), read when the stage is ready: a new game owns the
// red Onion only. PlayerState::initGame (playerState.cpp) clears the container
// and display bits and sets Red in both, so mContainerFlag and mDisplayPikiFlag
// are exactly 1 << Red; DemoFlags::initGame leaves only the engine's
// part-discovery flag (DEMOFLAG_UfoPartDiscoveryOffset + 4); no Challenge Mode
// stage is open, the story has Impact Site only; mIsTutorialMode is set by
// initGame and cleared by PlayerState::initCourse on any stage whose index in
// stages.ini is not 0 (the object stage's is not).
//
// onion_unlock plucks by hand, red, then yellow, then blue: the captain walks to
// the sprout with the stick and clicks A, so Navi::procActionButton,
// NaviNukuAdjustState, NaviNukuState and PikiNukareState run as in play. After
// each pluck, once the captain is back in Walk, the movie is over and the new
// Pikmin is in Formation, the whole progression state is compared with the
// snapshot taken before the approach. Each colour must change exactly what
// PikiNukareState::cleanup (pikiState.cpp) says and nothing else:
//   red     demo flags 3 (ApproachSeed), 4 (PluckRedPikmin), 7 (NoPikminTimeout);
//           display bit Red (already set); the DEMOFLAG_Unk9 timer armed with
//           demoParms _30; achievement PIKMIN_RED
//   yellow  demo flag 5 (and 7 if not yet set); RESFLAG_MeetYellowPikminNoBomb
//           and RESFLAG_Onyons on; container and display bit Yellow;
//           achievement PIKMIN_YELLOW
//   blue    demo flag 6 (and 7); RESFLAG_MeetBluePikmin on; container and
//           display bit Blue; Challenge Mode Impact Site, Forest of Hope and
//           Forest Navel open; achievement PIKMIN_BLUE
// Every other demo flag, result flag, container or display bit and challenge
// flag must be unchanged. Counts (EC-29): the colour's GameStat mePikis drops by
// one, formationPikis rises by one, allPikis is unchanged, one more Pikmin of
// that colour is on the field, other colours unchanged, and the hand path counts
// one pluck (mTotalPluckedPikiCount). The movie each flag starts (demo09, demo10,
// demo11) is not in the stand-in tree; it plays as a zero-length movie
// (standin-open-allowlist.txt), and the scenario bounds the time until it is over.
//
// At the end, PlayerState::saveCard to a buffer, the three colours' bits and
// flags cleared, and PlayerState::loadCard back: the bits must come back. That
// is the memory-card half of a day change, not the day transition itself (see
// milestones.md, ON2 notes).
//
// onion_unlock_whistle turns on the Whistle Pluck setting
// (pc_settings_scenario_set_whistle_pluck) and plants its own yellow and blue
// sprouts the way GoalAI::BootEmit plants seeds (an Onion-less PikiHeadItem set
// to the colour and dropped onto the floor), away from the stage's sprouts. It
// then calls pc_whistle_pluck, the mod's entry that NaviGatherState::exec calls
// while the whistle is held, with the cursor on the sprout: first with the
// setting off (it must pluck nothing), then on. The call site itself, with its
// cooldown and radius, is whistle_pluck_held's (#96); here the entry is called
// directly so the unlock path is isolated from the whistle's timing. The
// Pikmin leaves through PikiAutoNukiState, whose cleanup runs
// pcUnlockColourFromAutoPluck. The same state changes as the hand pluck are
// asserted for yellow and blue (red has no whistle unlock path, by design),
// except NoPikminTimeout, which only the hand path sets, and the pluck counter,
// which the whistle path does not raise (printed, not asserted).
//
// Both scenarios apply the disc's key bindings first (applyDiscKeys). The
// stand-in generator writes parms/key.bin with the same binds. Achievements: pc_achievements writes into
// the run's own NECTAR_SAVE_DIR in a scenario run (pc_achievements.cpp), so each
// run starts with none unlocked and nothing is written into the data folder.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "Demo.h"
#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "ItemMgr.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "ObjType.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "Stream.h"
#include "gameflow.h"
#include "pc_achievements.h"
#include "pc_whistle_pluck.h"
#include "settings/pc_settings.h"
#include "scenarios/scenario_progress.h"
#include "zen/ogResult.h"

namespace {

using namespace scenario_progress;

/// Shared baseline and per-pluck driver.
class OnionUnlockBase : public PcScenario {
protected:
	enum Phase { Plant, Approach, Plucking, Settle, Done };

	virtual Path path() const = 0;
	virtual int colourAt(int i) const = 0;
	virtual int pluckCount() const = 0;
	/// Sprout to pluck for case i: the stage's own (hand) or the planted one (whistle).
	virtual PikiHeadItem* target(int i) = 0;
	virtual void finish(unsigned t) = 0;

	bool checkBaseline()
	{
		applyDiscKeys();
		const Snap s = take();
		std::printf("[scenario] %s baseline: container 0x%02x display 0x%02x demo flags %s challenge 0x%x story 0x%x tutorialMode %d parts %d plucked %d "
		            "achievements %d\n",
		            name(), s.container, s.display, setBits(s.demo).c_str(), s.chal, s.course, (int)playerState->mIsTutorialMode, playerState->getCurrParts(),
		            s.plucked, s.achCount);
		std::vector<int> wantDemo((std::size_t)DEMOFLAG_COUNT, 0);
		wantDemo[DEMOFLAG_UfoPartDiscoveryOffset + 4] = 1;
		if (s.container != 1 << kRed || s.display != 1 << kRed) {
			scenarioFailf(0, "a new game owns the red Onion only (mContainerFlag and mDisplayPikiFlag == 1 << Red)", "container 0x%02x display 0x%02x", s.container,
			              s.display);
			return false;
		}
		if (s.demo != wantDemo) {
			scenarioFailf(0, "a new game's demo flags are the engine discovery preset only", "%s", setBits(s.demo).c_str());
			return false;
		}
		// initGame sets mIsTutorialMode; PlayerState::initCourse clears it on any stage but the first in stages.ini (index 0).
		const bool wantTutorial = flowCont.mCurrentStage->mStageIndex == 0;
		if (s.chal != 0 || s.course != 1 << STAGE_Practice || (bool)playerState->mIsTutorialMode != wantTutorial || playerState->getCurrParts() != 0
		    || s.plucked != 0 || s.achCount != 0) {
			scenarioFailf(0, "no challenge stage, story Impact Site only, tutorial mode only on stage index 0, 0 parts, 0 plucks, 0 achievements",
			              "challenge 0x%x story 0x%x tutorialMode %d (stage index %d) parts %d plucked %d achievements %d", s.chal, s.course,
			              (int)playerState->mIsTutorialMode, flowCont.mCurrentStage->mStageIndex, playerState->getCurrParts(), s.plucked, s.achCount);
			return false;
		}
		return true;
	}

	void providePads(unsigned tick, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || mPhase != Approach || path() != Hand) return;
		PikiHeadItem* sprout = target(mCase);
		if (!sprout || navi->getCurrState()->getID() != NAVISTATE_Walk) return;
		Vector3f d      = sprout->mSRT.t - navi->mSRT.t;
		d.y             = 0.0f;
		const float len = d.length();
		// Stick: +x east, +y north (-z) in the default camera, refined every tick (as container_menu).
		if (len > 6.0f) {
			pad.stickX = (int8_t)(d.x / len * 60.0f);
			pad.stickY = (int8_t)(-d.z / len * 60.0f);
		}
		// A away from a sprout would punch; click it only in reach (15 units, NaviMgr p62), with a release between clicks.
		if (len < 12.0f && tick % 4 == 0) pad.button = PAD_BUTTON_A;
	}

	/// Whistle path, once per case in Approach. Returns false after failing the run.
	virtual bool whistle(unsigned) { return true; }

	void onTick(unsigned t) override
	{
		if (mPhase == Done || mFailed) return;
		const int rate       = pc_scenario_ticks_per_second();
		const unsigned span  = t - mPhaseTick;
		Navi* navi           = naviMgr->getNavi();
		const int cap        = navi->getCurrState()->getID();
		const bool movie     = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const int colour     = colourAt(mCase);
		const char* name     = kColourName[colour];
		const int demoFlag   = colour == kRed ? DEMOFLAG_PluckRedPikmin : colour == kYellow ? DEMOFLAG_PluckYellowPikmin : DEMOFLAG_PluckBluePikmin;
		if (mPhase != Plant) mNaviStates.insert(cap);
		switch (mPhase) {
		case Plant:
			plantTick(t);
			return;
		case Approach:
			if (!target(mCase)) {
				fail(t, "the sprout to pluck exists", "%s sprout missing", name);
				return;
			}
			if (path() == Whistle && !mWhistled) {
				mWhistled = true;
				if (!whistle(t)) return;
			}
			if (playerState->mDemoFlags.isFlag(demoFlag) || newPiki(colour)) {
				std::printf("[scenario] %s %s: pluck started at tick %u (%.2f s after the approach began), captain state %d\n", this->name(), name, t,
				            span / (float)rate, cap);
				enter(Plucking, t);
				return;
			}
			if (span > 15u * (unsigned)rate) {
				Vector3f d = target(mCase)->mSRT.t - navi->mSRT.t;
				fail(t, "the captain reaches the sprout and the pluck starts within 15 s", "%s: captain state %d, %.1f from the sprout", name, cap, d.length());
			}
			return;
		case Plucking:
		{
			Piki* p = newPiki(colour);
			if (p) mStatesSeen.insert(p->getState());
			if (playerState->mDemoFlags.isFlag(demoFlag)) {
				mFlagTick = t;
				std::printf("[scenario] %s %s: flag %d set at tick %u, movie active %d, demo timer index %d %.2f s\n", this->name(), name, demoFlag, t, (int)movie,
				            (int)playerState->mDemoFlags.mCurrentDemoIndex, playerState->mDemoFlags.mWaitTimer);
				if (colour == kRed) {
					// PikiNukareState::cleanup arms DEMOFLAG_Unk9 with demoParms _30 (30 s by default; parms/demo.bin is not in the stand-in tree).
					const float full = demoParms->mParms._30();
					const float left = playerState->mDemoFlags.mWaitTimer;
					if (playerState->mDemoFlags.mCurrentDemoIndex != DEMOFLAG_Unk9 || left > full || left < full - 1.0f) {
						fail(t, "the red pluck arms the DEMOFLAG_Unk9 timer with demoParms _30", "index %d, %.2f s left of %.2f",
						     (int)playerState->mDemoFlags.mCurrentDemoIndex, left, full);
						return;
					}
				}
				mSawMovie = movie;
				enter(Settle, t);
				return;
			}
			if (span > 5u * (unsigned)rate) {
				fail(t, "the pluck sets the colour's demo flag within 5 s", "%s: captain state %d, new Pikmin %s, state %d", name, cap, p ? "yes" : "no",
				     p ? p->getState() : -1);
			}
			return;
		}
		case Settle:
		{
			mSawMovie = mSawMovie || movie;
			Piki* p   = newPiki(colour);
			if (p) mStatesSeen.insert(p->getState());
			if (p && cap == NAVISTATE_Walk && !movie && p->getState() == PIKISTATE_Normal && p->mMode == PikiMode::FormationMode) {
				const Snap after = take();
				std::string states;
				for (int s : mStatesSeen) states += (states.empty() ? "" : ",") + std::to_string(s);
				std::printf("[scenario] %s %s: settled at tick %u (%.2f s after the flag), movie seen %d, Pikmin states %s, plucked %d->%d, me %d->%d form %d->%d "
				            "all %d->%d\n",
				            this->name(), name, t, (t - mFlagTick) / (float)rate, (int)mSawMovie, states.c_str(), mBefore.plucked, after.plucked,
				            mBefore.me[colour], after.me[colour], mBefore.form[colour], after.form[colour], mBefore.all[colour], after.all[colour]);
				std::string naviStates;
				for (int s : mNaviStates) naviStates += (naviStates.empty() ? "" : ",") + std::to_string(s);
				std::printf("[scenario] %s %s: captain states %s\n", this->name(), name, naviStates.c_str());
				if (path() == Hand && !mNaviStates.count(NAVISTATE_Nuku)) {
					fail(t, "the captain plucks through NAVISTATE_Nuku", "%s: captain states %s", name, naviStates.c_str());
					return;
				}
				const int via = path() == Hand ? PIKISTATE_Nukare : PIKISTATE_AutoNuki;
				if (!mStatesSeen.count(via)) {
					fail(t, path() == Hand ? "the hand-plucked Pikmin passes through Nukare" : "the whistle-plucked Pikmin passes through AutoNuki", "%s: states %s",
					     name, states.c_str());
					return;
				}
				if (!checkPluck(t, path(), colour, mBefore, after)) {
					mFailed = true;
					return;
				}
				std::printf("[scenario] %s %s: container 0x%02x display 0x%02x challenge 0x%x demo flags %s achievements %d\n", this->name(), name,
				            after.container, after.display, after.chal, setBits(after.demo).c_str(), after.achCount);
				mKnown.insert(p);
				if (++mCase == pluckCount()) {
					mPhase = Done;
					finish(t);
					return;
				}
				startCase(t);
				return;
			}
			if (span > 10u * (unsigned)rate) {
				fail(t, "within 10 s of the flag the movie is over, the captain is in Walk and the Pikmin in Formation", "%s: movie %d, captain %d, Pikmin %s state %d mode %d",
				     name, (int)movie, cap, p ? "yes" : "no", p ? p->getState() : -1, p ? (int)p->mMode : -1);
			}
			return;
		}
		case Done: return;
		}
	}

	virtual void plantTick(unsigned t) { startCase(t); }

	void startCase(unsigned t)
	{
		mBefore = take();
		mStatesSeen.clear();
		mNaviStates.clear();
		mSawMovie = false;
		mWhistled = false;
		enter(Approach, t);
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	/// A Pikmin of the colour that was not on the field before this case.
	Piki* newPiki(int colour)
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p && p->mColor == colour && !mKnown.count(p)) return p;
		}
		return nullptr;
	}

	void rememberField()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it) mKnown.insert(static_cast<Piki*>(*it));
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mFailed = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Plant;
	int mCase           = 0;
	unsigned mPhaseTick = 0, mFlagTick = 0;
	bool mFailed = false, mSawMovie = false, mWhistled = false;
	Snap mBefore;
	std::set<int> mStatesSeen, mNaviStates;
	std::set<Piki*> mKnown;
};

PikiHeadItem* stageSprout(int colour)
{
	Iterator it(itemMgr->getPikiHeadMgr());
	CI_LOOP(it)
	{
		Creature* c = *it;
		if (c->mObjType != OBJTYPE_Pikihead) continue;
		PikiHeadItem* s = static_cast<PikiHeadItem*>(c);
		if (s->mSeedColor == colour && s->canPullout()) return s;
	}
	return nullptr;
}

class OnionUnlock : public OnionUnlockBase {
public:
	const char* name() const override { return "onion_unlock"; }

	void onStageReady() override
	{
		if (!checkBaseline()) {
			mFailed = true;
			return;
		}
		for (int c = 0; c < 3; c++) {
			mSprout[c] = stageSprout(c);
			if (!mSprout[c]) {
				fail(0, "the object stage's buried sprout of each colour", "no pluckable %s sprout", kColourName[c]);
				return;
			}
			std::printf("[scenario] onion_unlock %s sprout at (%.0f, %.0f), state %d\n", kColourName[c], mSprout[c]->mSRT.t.x, mSprout[c]->mSRT.t.z,
			            mSprout[c]->getCurrState()->getID());
		}
		rememberField();
	}

protected:
	Path path() const override { return Hand; }
	int colourAt(int i) const override
	{
		static const int order[3] = { kRed, kYellow, kBlue };
		return order[i];
	}
	int pluckCount() const override { return 3; }
	PikiHeadItem* target(int i) override
	{
		PikiHeadItem* s = mSprout[colourAt(i)];
		// Once plucked the sprout is killed; until then it is the stage's own.
		return s;
	}

	void finish(unsigned t) override
	{
		// Memory-card round trip: what a saved day carries into the next session.
		const int size = 0x26000; // CARD_DATA_SIZE, MemoryCard.h
		std::vector<u8> buf((std::size_t)size, 0);
		const Snap before = take();
		{
			RamStream out(buf.data(), size);
			playerState->saveCard(out);
			std::printf("[scenario] onion_unlock saveCard wrote %d bytes\n", out.getPosition());
		}
		playerState->mContainerFlag   = 1 << kRed;
		playerState->mDisplayPikiFlag = 1 << kRed;
		for (int f : { DEMOFLAG_PluckRedPikmin, DEMOFLAG_PluckYellowPikmin, DEMOFLAG_PluckBluePikmin }) playerState->mDemoFlags.resetFlag(f);
		{
			RamStream in(buf.data(), size);
			playerState->loadCard(in);
		}
		const Snap after = take();
		std::printf("[scenario] onion_unlock loadCard: container 0x%02x display 0x%02x demo flags %s\n", after.container, after.display,
		            setBits(after.demo).c_str());
		if (after.container != before.container || after.display != before.display || after.demo != before.demo || after.result != before.result) {
			scenarioFailf(t, "the container, display, demo and result flags survive saveCard/loadCard", "container 0x%02x display 0x%02x demo %s (saved 0x%02x 0x%02x %s)",
			              after.container, after.display, setBits(after.demo).c_str(), before.container, before.display, setBits(before.demo).c_str());
			return;
		}
		std::printf("[scenario] onion_unlock: red, yellow and blue unlocked by hand, PASS at tick %u\n", t);
		scenarioPass();
	}

private:
	PikiHeadItem* mSprout[3] = {};
};

class OnionUnlockWhistle : public OnionUnlockBase {
public:
	const char* name() const override { return "onion_unlock_whistle"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_whistle_pluck(1);
		if (!checkBaseline()) {
			mFailed = true;
			return;
		}
		if (!pc_settings_get_whistle_pluck()) {
			fail(0, "the Whistle Pluck setting is on", "pc_settings_get_whistle_pluck() is 0");
			return;
		}
		rememberField();
	}

protected:
	Path path() const override { return Whistle; }
	int colourAt(int i) const override { return i == 0 ? kYellow : kBlue; }
	int pluckCount() const override { return 2; }
	PikiHeadItem* target(int i) override { return mSprout[i]; }

	void plantTick(unsigned t) override
	{
		const int rate = pc_scenario_ticks_per_second();
		if (!mPlanted) {
			// Away from the stage's sprouts (x -100..-20, z 120..180), the Onion (-120, -250) and the pellet (-200, 60), 100 apart so a
			// growing whistle circle reaches only one.
			const float at[2][2] = { { -40.0f, -60.0f }, { -140.0f, -60.0f } };
			for (int i = 0; i < 2; i++) {
				PikiHeadItem* seed = static_cast<PikiHeadItem*>(itemMgr->birth(OBJTYPE_Pikihead));
				if (!seed) {
					fail(t, "a sprout born", "itemMgr->birth(OBJTYPE_Pikihead) returned null");
					return;
				}
				// As GoalAI::BootEmit (itemAI.cpp): position, colour, velocity, startAI, parent, Flying. Dropped from 20 units with no
				// sideways speed, so it lands where it is put.
				Vector3f pos(at[i][0], mapMgr->getMinY(at[i][0], at[i][1], true) + 20.0f, at[i][1]);
				seed->init(pos);
				seed->setColor(colourAt(i));
				seed->mVelocity.set(0.0f, 0.0f, 0.0f);
				seed->startAI(0);
				seed->mParentOnion = nullptr;
				C_SAI(seed)->start(seed, PikiHeadAI::PIKIHEAD_Flying);
				mSprout[i] = seed;
			}
			mPlanted = true;
			mPhaseTick = t;
			return;
		}
		if (mSprout[0]->canPullout() && mSprout[1]->canPullout()) {
			std::printf("[scenario] onion_unlock_whistle: yellow and blue sprouts planted at tick %u (colours %d %d)\n", t, mSprout[0]->mSeedColor,
			            mSprout[1]->mSeedColor);
			if (mSprout[0]->mSeedColor != kYellow || mSprout[1]->mSeedColor != kBlue) {
				fail(t, "the planted sprouts are yellow and blue", "colours %d and %d", mSprout[0]->mSeedColor, mSprout[1]->mSeedColor);
				return;
			}
			startCase(t);
			return;
		}
		// Bury lasts p49 mSeedUpTime and Tane p50 mPluckWaitTime, each plus up to 2 s at random (PikiHeadAI, itemAI.cpp),
		// then the Hatuga motion: the two parameters plus 4 s of random plus 4 s for the motions (10 s with the code's 1
		// and 1 s, 13.1 s with the disc's 0.1 and 5 s).
		PikiProp::Parms& pp      = pikiMgr->mPikiParms->mPikiParms;
		const float limitS        = pp.mSeedUpTime() + pp.mPluckWaitTime() + 8.0f;
		if (t - mPhaseTick > unsigned(limitS * rate)) {
			fail(t, "both planted sprouts can be plucked within p49 + p50 + 8 s", "%.1f s: states %d and %d", limitS, mSprout[0]->getCurrState()->getID(),
			     mSprout[1]->getCurrState()->getID());
		}
	}

	/// What NaviGatherState::exec does while the whistle is held: pc_whistle_pluck with the current circle's radius. The
	/// scenario calls the mod's entry itself (whistle_pluck_held covers the real call site), with the cursor on the
	/// sprout and the circle at the minimum radius. The other sprouts are at least 80 units away.
	bool whistle(unsigned t) override
	{
		Navi* navi           = naviMgr->getNavi();
		PikiHeadItem* sprout = mSprout[mCase];
		const float radius   = C_NAVI_PARM(navi, mWhistleMinRadius) + 5.0f;
		navi->mCursorWorldPos = sprout->mSRT.t;
		if (mCase == 0) {
			// The setting gate: off, the same call plucks nothing.
			pc_settings_scenario_set_whistle_pluck(0);
			const bool off = pc_whistle_pluck(navi, radius);
			pc_settings_scenario_set_whistle_pluck(1);
			if (off || !sprout->canPullout()) {
				fail(t, "with Whistle Pluck off, pc_whistle_pluck plucks nothing", "returned %d, sprout pluckable %d", (int)off, (int)sprout->canPullout());
				return false;
			}
		}
		if (!pc_whistle_pluck(navi, radius)) {
			fail(t, "with Whistle Pluck on, pc_whistle_pluck plucks the sprout under the cursor", "returned false (%s sprout, radius %.1f)",
			     kColourName[colourAt(mCase)], radius);
			return false;
		}
		std::printf("[scenario] onion_unlock_whistle %s: pc_whistle_pluck at tick %u, radius %.1f\n", kColourName[colourAt(mCase)], t, radius);
		return true;
	}

	void finish(unsigned t) override
	{
		std::printf("[scenario] onion_unlock_whistle: yellow and blue unlocked by Whistle Pluck, PASS at tick %u\n", t);
		scenarioPass();
	}

private:
	PikiHeadItem* mSprout[2] = {};
	bool mPlanted            = false;
};

} // namespace

PcScenario* pc_scenario_make_onion_unlock(void)
{
	static OnionUnlock s;
	return &s;
}

PcScenario* pc_scenario_make_onion_unlock_whistle(void)
{
	static OnionUnlockWhistle s;
	return &s;
}

#endif
