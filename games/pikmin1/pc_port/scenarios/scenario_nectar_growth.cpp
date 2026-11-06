// nectar_growth: a Pikmin drinking nectar grows into a flower (coverage/milestones.md DF26 and PL1,
// edge-cases.md EC-48), on the sandbox stage. The nectar is a FallWater drop born as the game's
// own sources birth it (aiWeed.cpp, aiStone.cpp): it falls, lands (FallWaterAI Collide, the
// motion's Action0 key), and emits the OBJTYPE_Water puddle that Pikmin drink.
//
// In order, each within a bound in seconds:
//   Drop     The drop is born 120 units above the ground at (kX, kZ). A puddle (OBJTYPE_Water) appears.
//   Flower   A flower Pikmin is put on the puddle for 3 s. PikiNormalState::procCollideMsg lets only
//            a Pikmin that is not a flower drink: it never enters PikiAbsorbState (negative control for
//            EC-48), stays a flower, and no flag or result flag is set.
//   Bud      A bud Pikmin is put on the puddle. It enters PikiAbsorbState (the Mizunomi motion; its
//            KEY_LoopStart and KEY_Finished are stand-in keys, standin_anims.h kAnimInfoMizunomi) and then
//            PikiGrowupState (the GrowUp motion's Action0 key): mHappa becomes Flower and
//            RESFLAG_PikminSeeds is set (PL1; ResultFlags has no reader but the diary's getDocument, which
//            picks the set flag of the lowest priority number, and the scenario asserts that it is this one;
//            it also errors when no flag is set, so it is only called while the day entries are), DEMOFLAG_FirstNectar (26) is set (and no other flag), and
//            the TUT_Mitu text window opens (id 22); A closes it. The Pikmin returns to PikiNormalState.
//            The drink uses the puddle up (PikiAbsorbState sends the puddle a user message; WaterAI Die).
//   Leaf     A second drop, and a leaf Pikmin on its puddle: it grows into a flower too (PikiGrowupState sets the
//            flower stage whatever it was). Flag 26 stays set, no second Mitu window opens: the text is
//            once per game (DF26's latch).
//
// Not covered: the drink and the grow-up motions' own timing (stand-in keys at frames 0, 10, 20;
// the disc's were not read), the puddle's own lifetime (WaterAI).
//
// Negative control (manual): put the flower Pikmin's phase on a bud (spawn it as Bud) and the Flower
// phase fails at once, because it enters Absorb.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Demo.h"
#include "Traversable.h"
#include "ItemMgr.h"
#include "MizuItem.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kX = 0.0f, kZ = 200.0f; // sandbox: z > 0 is open floor south of the gate

class NectarGrowth : public PcScenario {
public:
	const char* name() const override { return "nectar_growth"; }

	void onStageReady() override
	{
		mFlags = demoFlags();
		std::printf("[scenario] nectar_growth: flag 26 %d, flags %s\n", (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNectar), setBits(mFlags).c_str());
		if (playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNectar)) {
			fail(0, "a new game: flag 26 clear", "flag 26 set");
			return;
		}
		if (!dropNectar(0)) return;
		enter(Drop, 0);
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
		switch (mPhase) {
		case Drop:
			if ((mPuddle = findPuddle())) {
				std::printf("[scenario] nectar_growth: puddle at (%.0f, %.0f, %.0f) after %.1f s\n", mPuddle->mSRT.t.x, mPuddle->mSRT.t.y, mPuddle->mSRT.t.z, span / float(rate));
				mTexts0 = pc_scenario_text_windows_opened(nullptr);
				mFlower = putPiki(navi, Flower);
				if (!mFlower) {
					fail(t, "a flower Pikmin is born", "pikiMgr->birth returned null");
					return;
				}
				enter(FlowerPhase, t);
			} else if (span > 20 * rate) {
				fail(t, "the drop lands and makes a puddle within 20 s", "drop state %d at y %.1f", mDrop->getCurrState() ? mDrop->getCurrState()->getID() : -1, mDrop->mSRT.t.y);
			}
			return;
		case FlowerPhase:
			if (mFlower->getState() == PIKISTATE_Absorb || mFlower->getState() == PIKISTATE_GrowUp || mFlower->mHappa != Flower) {
				fail(t, "EC-48: a flower Pikmin on the nectar does not drink", "state %d, happa %d", mFlower->getState(), mFlower->mHappa);
				return;
			}
			if (!newFlags(mFlags, demoFlags()).empty()) {
				fail(t, "no flag sets while a flower stands on the nectar", "new flags %s", list(newFlags(mFlags, demoFlags())).c_str());
				return;
			}
			if (span >= 3 * rate) {
				int pages     = 0;
				const int doc = playerState->mResultFlags.getDocument(pages);
				if (doc == zen::RESFLAG_PikminSeeds) {
					fail(t, "RESFLAG_PikminSeeds does not set while a flower stands on the nectar", "document %d", doc);
					return;
				}
				std::printf("[scenario] nectar_growth: the flower stood on the puddle for 3 s (%.0f units off), state %d: no drink\n",
				            flatDistance(mFlower->mSRT.t, mPuddle->mSRT.t), mFlower->getState());
				mSubject = putPiki(navi, Bud);
				if (!mSubject) {
					fail(t, "a bud Pikmin is born beside the puddle", "spawnPiki returned null");
					return;
				}
				mSawAbsorb = mSawGrow = false;
				enter(BudPhase, t);
			}
			return;
		case BudPhase:
			if (!drink(t, span, rate, "bud")) return;
			{
				// The diary entry the flower earns: ResultFlags::getDocument returns the set flag of the lowest
				// priority number (a new game also has the day entries 58 and up set, which come after it).
				int pages     = 0;
				const int doc = playerState->mResultFlags.getDocument(pages);
				if (!playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNectar) || doc != zen::RESFLAG_PikminSeeds) {
					fail(t, "DF26: flag 26 and the result flag RESFLAG_PikminSeeds (the diary entry) set by the first flower", "flag 26 %d, document %d (%d pages)",
					     (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNectar), doc, pages);
					return;
				}
				std::printf("[scenario] nectar_growth: diary document %d, %d page(s)\n", doc, pages);
			}
			{
				const std::vector<int> fresh = newFlags(mFlags, demoFlags());
				if (fresh.size() != 1 || fresh[0] != DEMOFLAG_FirstNectar) {
					fail(t, "the first flower sets flag 26 and no other", "new flags %s", list(fresh).c_str());
					return;
				}
			}
			enter(BudText, t);
			return;
		case BudText: {
			int textId = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTexts0) {
				if (opened != mTexts0 + 1 || textId != zen::ogScrTutorialMgr::TUT_Mitu) {
					fail(t, "one text window, TUT_Mitu (22)", "%d windows, text id %d", opened - mTexts0, textId);
					return;
				}
				mSawText = true;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				std::printf("[scenario] nectar_growth: TUT_Mitu opened and closed at t=%u\n", t);
				mFlags  = demoFlags();
				mTexts0 = opened;
				if (!dropNectar(t)) return;
				enter(Drop2, t);
			} else if (span > 10 * rate) {
				fail(t, "the first flower opens the TUT_Mitu window within 10 s", "windows %d (was %d)", opened, mTexts0);
			}
			return;
		}
		case Drop2:
			// The bud's drink used its puddle up (Piki -> WaterAI user event -> Die): wait for the second one.
			if (Creature* puddle = findPuddle()) {
				mPuddle    = puddle;
				mSubject   = putPiki(navi, Leaf);
				if (!mSubject) {
					fail(t, "a leaf Pikmin is born beside the puddle", "spawnPiki returned null");
					return;
				}
				mSawAbsorb = mSawGrow = false;
				enter(LeafPhase, t);
			} else if (span > 20 * rate) {
				fail(t, "the second drop lands and makes a puddle within 20 s", "no puddle");
			}
			return;
		case LeafPhase:
			if (!drink(t, span, rate, "leaf")) return;
			if (!newFlags(mFlags, demoFlags()).empty() || pc_scenario_text_windows_opened(nullptr) != mTexts0 || gameflow.mIsUIOverlayActive) {
				fail(t, "DF26 is once: the second flower sets no flag and opens no window", "new flags %s, windows %d (was %d)", list(newFlags(mFlags, demoFlags())).c_str(),
				     pc_scenario_text_windows_opened(nullptr), mTexts0);
				return;
			}
			if (span > 2 * rate) {
				std::printf("[scenario] nectar_growth: done, flags %s\n", setBits(demoFlags()).c_str());
				mPhase = Done;
				scenarioPass();
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Drop, FlowerPhase, BudPhase, BudText, Drop2, LeafPhase, Done };

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

	/// A FallWater drop, 120 units up at (kX, kZ).
	bool dropNectar(unsigned t)
	{
		MizuItem* drop = static_cast<MizuItem*>(itemMgr->birth(OBJTYPE_FallWater));
		if (!drop) {
			fail(t, "a FallWater drop is born", "itemMgr->birth returned null");
			return false;
		}
		Vector3f at(kX, mapMgr->getMinY(kX, kZ, true) + 120.0f, kZ);
		drop->init(at);
		drop->mVelocity.set(0.0f, 0.0f, 0.0f);
		drop->startAI(0);
		mDrop = drop;
		return true;
	}

	Creature* findPuddle()
	{
		Iterator it(itemMgr);
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c && c->mObjType == OBJTYPE_Water && c->isAlive()) return c;
		}
		return nullptr;
	}

	/// A free Pikmin of the given stage on the puddle.
	Piki* putPiki(Navi* navi, int happa)
	{
		Piki* p = spawnPiki(navi, Blue, PikiMode::FreeMode, mPuddle->mSRT.t.x - navi->mSRT.t.x, mPuddle->mSRT.t.z - navi->mSRT.t.z);
		if (p) p->setFlower(happa);
		GameStat::update();
		return p;
	}

	/// Watches `mSubject` drink: Absorb, GrowUp, then a flower back in the normal state. False until it has.
	bool drink(unsigned t, unsigned span, unsigned rate, const char* what)
	{
		const int state = mSubject->getState();
		if (state == PIKISTATE_Absorb) mSawAbsorb = true;
		if (state == PIKISTATE_GrowUp) mSawGrow = true;
		if (mSubject->mHappa == Flower && mSawAbsorb && mSawGrow && state == PIKISTATE_Normal) {
			std::printf("[scenario] nectar_growth: %s drank (Absorb then GrowUp) and is a flower at t=%u\n", what, t);
			return true;
		}
		if (span > 15 * rate) {
			fail(t, "the Pikmin drinks, grows into a flower and returns to normal within 15 s", "%s: state %d, happa %d, absorb %d, growup %d", what, state, mSubject->mHappa, (int)mSawAbsorb, (int)mSawGrow);
		}
		return false;
	}

	Phase mPhase        = Drop;
	unsigned mPhaseTick = 0;
	int mTexts0         = 0;
	bool mSawAbsorb = false, mSawGrow = false, mSawText = false;
	MizuItem* mDrop    = nullptr;
	Creature* mPuddle  = nullptr;
	Piki* mFlower      = nullptr;
	Piki* mSubject     = nullptr;
	std::vector<int> mFlags;
};

} // namespace

PcScenario* pc_scenario_make_nectar_growth(void)
{
	static NectarGrowth s;
	return &s;
}

#endif
