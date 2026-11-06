// pikmin_cap, pikmin_cap_1, pikmin_cap_250, pikmin_cap_999: the field Pikmin limit (coverage/milestones.md ON6,
// DF17, DF21-DF25; coverage/edge-cases.md EC-01, EC-02, EC-04, EC-38, EC-57), on the object stage
// (stages/sandbox_objects.ini) with its blue Onion. pikmin_cap runs the default limit (no
// override: the GameCube's 100); the others set the port's limit before the stage build
// (PcScenario::pikiLimit, pc_settings_scenario_set_piki_limit).
//
// In order, each within a bound in seconds:
//   Sizing  AICONST.mMaxPikisOnField is the limit (100 by default), the Pikmin pool holds limit + 2,
//           the sprout pool limit and the captain's plate limit + 10 slots (GameCoreSection::initStage,
//           PikiHeadMgr, CPlate::CPlate).
//   Seed    Below the limit the blue Onion's GoalAI::EmitPiki gives a seed (negative control for the
//           cap phase): one Pikihead, bornPikis +1, no container Pikmin, no flag, no text. Once
//           planted the sprout counts in GameStat::mePikis, so in mapPikis.
//   Fill    Pikmin born into the captain's party (PikiMgr::birth) until birth returns null: the limit
//           less the stage's three buried sprouts and the seed are born, mapPikis is the limit, and
//           every further plain birth fails (ON6, EC-01).
//   Plate   getPlatePikis() is the number born, CPlate's used slots match, and the slot occupants are
//           distinct, live, in Formation (EC-02, EC-38, EC-57).
//   Pluck   At the limit, the seed's sprout plucked by an InteractBikkuri (PikiHeadItem::interactBikkuri,
//           PikiMgr::meBirthMode) still becomes a Pikmin: meBirthMode allows the one birth over the
//           limit the sprout itself takes. mapPikis stays at the limit.
//   Cap     At the limit, EmitPiki gives no seed: the Pikmin goes into the Onion (containerPikis +1,
//           mHeldPikis leaf +1), bornPikis +1 all the same, and the first time on a stage the flag
//           stage + 21 sets (DEMOFLAG_PikminLimitOffset) and TUT_Limit100 opens. The stage id is
//           set to each story stage in turn (0..4, DF21-DF25) for the call, as ship_parts does;
//           a second seed at the limit on stage 0 sets no flag and opens no text.
//
//   DF17    At the stage start (GameCoreSection::finalSetup) PostExtinctionSeed is set when an owned
//           colour has no Pikmin: in pikmin_cap_1 only (the red Onion, no red sprout), clear in the
//           others (the negative control).
//
// pikmin_cap_1 (EC-57): the sprout pool holds one, so the stage generator builds one of its three
// sprouts and the field starts at the limit; there is no Seed phase and the stage sprout is the one
// plucked. pikmin_cap_999 is the setting's maximum (PIKI_FIELD_LIMIT_MAX).
//
// Not covered: a real Onion emission (the Onion menu, mPikisToExit), challenge mode (no flag
// there), a movie active at the time (no text), and the test-map collision of stage id 5 + 21
// with DEMOFLAG_FirstNectar (26), which no disc stage can reach.
//
// Negative control: the Seed phase is the in-scenario contrast to the Cap phase. Manual (2026-10-31,
// local, not committed): with one Pikmin killed just before the first emission at the limit, the
// Cap phase fails at tick 136 (30 Hz) with "stage 0: sprouts +1, Onion +0, leaf +0, born +1, new
// flags none": one under the limit, EmitPiki gives a seed again.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <set>
#include <vector>

#include "AIConstant.h"
#include "CPlate.h"
#include "Demo.h"
#include "FlowController.h"
#include "GoalItem.h"
#include "Interactions.h"
#include "ItemMgr.h"
#include "NaviMgr.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

/// CPlate's protected slot table, read through a member pointer as ResultPeek reads ogScrResultMgr.
struct PlatePeek : CPlate {
	static int used(CPlate& p) { return p.*&PlatePeek::mUsedSlotCount; }
	static int total(CPlate& p) { return p.*&PlatePeek::mTotalSlotCount; }
	static int size(CPlate& p) { return p.*&PlatePeek::mSlotListSize; }
	static Creature* occupant(CPlate& p, int i) { return (p.*&PlatePeek::mSlotList)[i].mOccupant.getPtr(); }
};

class PikminCap : public PcScenario {
public:
	PikminCap(const char* name, int limit)
	    : mName(name)
	    , mOverride(limit)
	    , mLimit(limit > 0 ? limit : 100)
	{
	}

	const char* name() const override { return mName; }
	const char* stageFile() const override { return "stages/sandbox_objects.ini"; }
	int pikiLimit() const override { return mOverride; }

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi();
		mOnion     = itemMgr->getContainer(Blue);
		mFlags     = demoFlags();
		const int aiMax = AICONST.mMaxPikisOnField(), pool = pikiMgr->getMax(), heads = itemMgr->getPikiHeadMgr()->getMax();
		const int slots = PlatePeek::size(*navi->mPlateMgr);
		std::printf("[scenario] %s: limit %d: AICONST %d, Pikmin pool %d, sprout pool %d, plate slots %d, map Pikmin %d, flags %s\n", mName, mLimit, aiMax,
		            pool, heads, slots, (int)GameStat::mapPikis, setBits(mFlags).c_str());
		if (aiMax != mLimit || pool != mLimit + 2 || heads != mLimit || slots != mLimit + 10) {
			fail(0, "ON6/EC-02: field limit N, Pikmin pool N + 2, sprout pool N, plate N + 10", "N %d: %d, %d, %d, %d", mLimit, aiMax, pool, heads, slots);
			return;
		}
		// The stage's three buried sprouts count in mePikis, so in mapPikis, from the start.
		mStart = GameStat::mapPikis;
		mStageSprout = newestSeed();
		if (!mOnion || mStart != GameStat::mePikis || mStart != itemMgr->getPikiHeadMgr()->getSize() || !mStageSprout) {
			fail(0, "the object stage's blue Onion, its buried sprouts the only Pikmin on the field", "Onion %d, map Pikmin %d, mePikis %d, sprouts %d",
			     (int)(mOnion != nullptr), (int)GameStat::mapPikis, (int)GameStat::mePikis, itemMgr->getPikiHeadMgr()->getSize());
			return;
		}
		for (int s = STAGE_Practice; s < STAGE_COUNT; s++) {
			if (playerState->mDemoFlags.isFlag(s + DEMOFLAG_PikminLimitOffset)) {
				fail(0, "a new game with flags 21-25 clear", "flag %d set", s + DEMOFLAG_PikminLimitOffset);
				return;
			}
		}
		// DF17: GameCoreSection::finalSetup sets PostExtinctionSeed when a colour the player has the Onion
		// of has no Pikmin anywhere. A new game has the red Onion; the stage's sprouts are one of each
		// colour, so only a sprout pool under three (limit 1: the blue one alone) leaves red at none.
		bool extinct = false;
		for (int c = Blue; c <= Yellow; c++) extinct = extinct || (GameStat::allPikis[c] == 0 && playerState->hasContainer(c));
		const bool wantExtinct = mLimit < 3;
		std::printf("[scenario] %s: DF17: Pikmin B%d R%d Y%d, Onions B%d R%d Y%d, flag 17 %d, extinction played %d\n", mName, (int)GameStat::allPikis[Blue],
		            (int)GameStat::allPikis[Red], (int)GameStat::allPikis[Yellow], (int)playerState->hasContainer(Blue), (int)playerState->hasContainer(Red),
		            (int)playerState->hasContainer(Yellow), (int)playerState->mDemoFlags.isFlag(DEMOFLAG_PostExtinctionSeed),
		            (int)playerState->mHasExtinctionDemoPlayed);
		if (extinct != wantExtinct || playerState->mDemoFlags.isFlag(DEMOFLAG_PostExtinctionSeed) != wantExtinct
		    || playerState->mHasExtinctionDemoPlayed != wantExtinct) {
			fail(0, wantExtinct ? "DF17: an owned colour with no Pikmin sets PostExtinctionSeed at the stage start"
			                    : "DF17 control: every owned colour has a Pikmin, PostExtinctionSeed clear",
			     "extinct %d, flag 17 %d, played %d", (int)extinct, (int)playerState->mDemoFlags.isFlag(DEMOFLAG_PostExtinctionSeed),
			     (int)playerState->mHasExtinctionDemoPlayed);
			return;
		}
		mTexts0 = pc_scenario_text_windows_opened(nullptr);
		if (mLimit <= mStart) {
			// EC-57: a limit under the stage's three sprouts (only a scenario override reaches it; the
			// setting's floor is 50). The sprout pool holds only the limit, so the generator builds that
			// many; no seed phase, the field is already at the limit.
			std::printf("[scenario] %s: the stage's %d sprouts already fill the limit %d\n", mName, mStart, mLimit);
			enter(Fill, 0);
			return;
		}
		Snap before = snap();
		mSeedCount  = itemMgr->getPikiHeadMgr()->getSize();
		emit(STAGE_TESTMAP);
		mSeed = newestSeed();
		Snap after = snap();
		std::printf("[scenario] %s: seed below the limit: sprouts %d -> %d, born %d -> %d, Onion %d -> %d\n", mName, mSeedCount,
		            itemMgr->getPikiHeadMgr()->getSize(), before.born, after.born, before.container, after.container);
		if (!mSeed || itemMgr->getPikiHeadMgr()->getSize() != mSeedCount + 1 || after.born != before.born + 1 || after.container != before.container
		    || after.held != before.held) {
			fail(0, "EC-04 control: below the limit EmitPiki gives a seed, bornPikis +1, nothing into the Onion", "seed %d, sprouts +%d, born +%d, Onion +%d",
			     (int)(mSeed != nullptr), itemMgr->getPikiHeadMgr()->getSize() - mSeedCount, after.born - before.born, after.container - before.container);
			return;
		}
		enter(Seed, 0);
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
		case Seed: {
			if (!freshFlags().empty() || pc_scenario_text_windows_opened(nullptr) != mTexts0) {
				fail(t, "EC-04 control: a seed below the limit sets no flag and opens no text", "new flags %s", list(freshFlags()).c_str());
				return;
			}
			if (span < 3 * rate) return;
			const int state = mSeed->getCurrState() ? mSeed->getCurrState()->getID() : -1;
			std::printf("[scenario] %s: after 3 s the seed is in state %d, mePikis %d, mapPikis %d\n", mName, state, (int)GameStat::mePikis,
			            (int)GameStat::mapPikis);
			if (!mSeed->isAlive() || GameStat::mePikis != mStart + 1 || GameStat::mapPikis != mStart + 1) {
				fail(t, "ON6: the planted sprout counts in mePikis and mapPikis", "alive %d, mePikis %d, mapPikis %d", (int)mSeed->isAlive(),
				     (int)GameStat::mePikis, (int)GameStat::mapPikis);
				return;
			}
			enter(Fill, t);
			return;
		}
		case Fill: {
			const int want = mLimit > mStart ? mLimit - mStart - 1 : 0;
			int born       = 0;
			while (born < mLimit + 5) {
				Piki* p = spawnPiki(navi, Blue, PikiMode::FormationMode, 20.0f + 4.0f * float(born % 10), 20.0f + 4.0f * float(born / 10 % 10));
				if (!p) break;
				born++;
			}
			GameStat::update();
			Piki* extra = static_cast<Piki*>(pikiMgr->birth());
			std::printf("[scenario] %s: %d Pikmin born before PikiMgr::birth returned null; mapPikis %d, one more birth %s\n", mName, born,
			            (int)GameStat::mapPikis, extra ? "succeeded" : "null");
			if (born != want || GameStat::mapPikis != (mLimit > mStart ? mLimit : mStart) || extra) {
				fail(t, "ON6/EC-01: Pikmin born up to the limit less the sprouts, mapPikis the limit, no birth past it", "born %d (want %d), mapPikis %d, extra %d",
				     born, want, (int)GameStat::mapPikis, (int)(extra != nullptr));
				return;
			}
			mBorn = born;
			enter(Plate, t);
			return;
		}
		case Plate: {
			if (span < rate) return;
			CPlate& plate   = *navi->mPlateMgr;
			const int count = navi->getPlatePikis(), used = PlatePeek::used(plate), total = PlatePeek::total(plate);
			std::set<Creature*> seen;
			int occupied = 0, bad = 0;
			for (int i = 0; i < total; i++) {
				Creature* c = PlatePeek::occupant(plate, i);
				if (!c) continue;
				occupied++;
				Piki* p = static_cast<Piki*>(c);
				if (!seen.insert(c).second || !c->isAlive() || p->mMode != PikiMode::FormationMode) bad++;
			}
			std::printf("[scenario] %s: plate: %d Pikmin, %d used of %d slots (%d in the list), %d occupied, %d duplicate/dead/not in Formation\n", mName, count,
			            used, total, PlatePeek::size(plate), occupied, bad);
			if (count != mBorn || used != count || occupied != count || bad != 0 || total > PlatePeek::size(plate)) {
				fail(t, "EC-02/EC-38: every born Pikmin in the plate, one distinct live Formation Pikmin per used slot", "plate %d, used %d, occupied %d, bad %d, slots %d/%d",
				     count, used, occupied, bad, total, PlatePeek::size(plate));
				return;
			}
			mLiveBefore = liveCount(Blue);
			InteractBikkuri act(navi);
			PikiHeadItem* sprout = mSeed ? mSeed : mStageSprout;
			const bool plucked   = sprout->stimulate(act);
			GameStat::update();
			if (!mSeed) {
				// The sprout pool holds the limit, so the stage generator built only that many of its three
				// sprouts; at the limit meBirthMode still lets one become a Pikmin.
				std::printf("[scenario] %s: stage sprout plucked at the limit: %d, live Pikmin %d -> %d, mapPikis %d\n", mName, (int)plucked, mLiveBefore,
				            liveCount(Blue), (int)GameStat::mapPikis);
				if (mStart != mLimit || !plucked || liveCount(Blue) != mLiveBefore + 1 || GameStat::mapPikis != mLimit) {
					fail(t, "EC-57: the sprout pool caps the stage's sprouts at the limit, and one still plucks at the limit", "sprouts %d, plucked %d, live %d -> %d, mapPikis %d",
					     mStart, (int)plucked, mLiveBefore, liveCount(Blue), (int)GameStat::mapPikis);
					return;
				}
				mStage = STAGE_Practice;
				enter(Cap, t);
				return;
			}
			std::printf("[scenario] %s: sprout plucked at the limit: %d, live Pikmin %d -> %d, mapPikis %d\n", mName, (int)plucked, mLiveBefore, liveCount(Blue),
			            (int)GameStat::mapPikis);
			if (!plucked || liveCount(Blue) != mLiveBefore + 1 || GameStat::mapPikis != mLimit) {
				fail(t, "EC-01: meBirthMode lets the sprout at the limit become a Pikmin, mapPikis stays the limit", "plucked %d, live %d -> %d, mapPikis %d",
				     (int)plucked, mLiveBefore, liveCount(Blue), (int)GameStat::mapPikis);
				return;
			}
			mStage = STAGE_Practice;
			enter(Cap, t);
			return;
		}
		case Cap: {
			if (span < rate / 2) return;
			const int flag = mStage + DEMOFLAG_PikminLimitOffset;
			if (!mEmitted) {
				mBefore    = snap();
				mSeedCount = itemMgr->getPikiHeadMgr()->getSize();
				mTexts0    = pc_scenario_text_windows_opened(nullptr);
				mFlags     = demoFlags();
				emit(mStage);
				mEmitted        = true;
				mWindowTick     = t;
				mSawText        = false;
				const Snap a    = snap();
				const int seeds = itemMgr->getPikiHeadMgr()->getSize() - mSeedCount;
				const std::vector<int> fresh = freshFlags();
				std::printf("[scenario] %s: seed at the limit on stage %d (%s): sprouts +%d, Onion +%d (leaf +%d), born +%d, new flags %s\n", mName, mStage,
				            mRepeat ? "again" : "first", seeds, a.container - mBefore.container, a.held - mBefore.held, a.born - mBefore.born, list(fresh).c_str());
				const bool wantFlag = !mRepeat;
				if (seeds != 0 || a.container != mBefore.container + 1 || a.held != mBefore.held + 1 || a.born != mBefore.born + 1
				    || (wantFlag ? (fresh.size() != 1 || fresh[0] != flag) : !fresh.empty())) {
					fail(t, wantFlag ? "EC-04/DF21-25: at the limit no seed, the Pikmin into the Onion, bornPikis +1, flag stage + 21 and no other"
					                 : "EC-04: a second seed at the limit on the stage sets no flag",
					     "stage %d: sprouts +%d, Onion +%d, leaf +%d, born +%d, new flags %s", mStage, seeds, a.container - mBefore.container,
					     a.held - mBefore.held, a.born - mBefore.born, list(fresh).c_str());
					return;
				}
				return;
			}
			int textId       = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (mRepeat) {
				if (opened != mTexts0) {
					fail(t, "EC-04: the second seed at the limit opens no text", "%d windows", opened - mTexts0);
					return;
				}
				if (t - mWindowTick < 2 * rate) return;
				mPhase = Done;
				std::printf("[scenario] %s: flags now %s; Onion holds %u leaf Pikmin\n", mName, setBits(demoFlags()).c_str(), mOnion->mHeldPikis[Leaf]);
				scenarioPass();
				return;
			}
			if (opened > mTexts0) {
				if (opened != mTexts0 + 1 || textId != zen::ogScrTutorialMgr::TUT_Limit100) {
					fail(t, "DF21-25: one text window, TUT_Limit100 (21)", "%d windows, text id %d", opened - mTexts0, textId);
					return;
				}
				mSawText = true;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				std::printf("[scenario] %s: stage %d: TUT_Limit100 opened and closed after %.2f s\n", mName, mStage, (t - mWindowTick) / float(rate));
				mEmitted = false;
				if (++mStage == STAGE_COUNT) {
					mStage  = STAGE_Practice;
					mRepeat = true;
				}
				enter(Cap, t);
			} else if (t - mWindowTick > 10 * rate) {
				fail(t, "TUT_Limit100 opens and closes within 10 s", "windows %d (was %d), overlay %d", opened, mTexts0, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		}
		case Done: return;
		}
	}

private:
	enum Phase { Seed, Fill, Plate, Cap, Done };
	struct Snap {
		int born, container, held;
	};

	Snap snap() const { return { (int)GameStat::bornPikis[Blue], (int)GameStat::containerPikis[Blue], (int)mOnion->mHeldPikis[Leaf] }; }

	/// One seed from the Onion as GoalAI::EmitPiki gives it, with the stage id `stage` for the call.
	void emit(int stage)
	{
		const int saved                     = flowCont.mCurrentStage->mStageID;
		flowCont.mCurrentStage->mStageID    = stage;
		mOnion->mSAICtx.mCounter            = 0;
		mOnion->mSAICtx.mCurrAnimId         = 2; // left at 1 after the call: no finishMotion
		GoalAI::EmitPiki action;
		action.act(mOnion);
		mOnion->mSAICtx.mCurrAnimId      = 0;
		flowCont.mCurrentStage->mStageID = saved;
	}

	PikiHeadItem* newestSeed() const
	{
		PikiHeadItem* found = nullptr;
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it) { found = static_cast<PikiHeadItem*>(*it); }
		return found;
	}

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

	const char* mName;
	int mOverride, mLimit;
	Phase mPhase        = Seed;
	unsigned mPhaseTick = 0, mWindowTick = 0;
	int mTexts0 = 0, mSeedCount = 0, mLiveBefore = 0, mStage = 0, mStart = 0, mBorn = 0;
	bool mEmitted = false, mSawText = false, mRepeat = false;
	Snap mBefore {};
	GoalItem* mOnion     = nullptr;
	PikiHeadItem* mSeed  = nullptr;
	PikiHeadItem* mStageSprout = nullptr;
	std::vector<int> mFlags;
};

} // namespace

PcScenario* pc_scenario_make_pikmin_cap(void)
{
	static PikminCap s("pikmin_cap", 0);
	return &s;
}

PcScenario* pc_scenario_make_pikmin_cap_1(void)
{
	static PikminCap s("pikmin_cap_1", 1);
	return &s;
}

PcScenario* pc_scenario_make_pikmin_cap_250(void)
{
	static PikminCap s("pikmin_cap_250", 250);
	return &s;
}

PcScenario* pc_scenario_make_pikmin_cap_999(void)
{
	static PikminCap s("pikmin_cap_999", 999);
	return &s;
}

#endif
