// save_load, save_load_flags: a day-end save through the results screen and the
// card stubs, read back from disk and loaded (coverage/terminals.md T13, T14,
// T21).
//
// Runs DayEndResults (scenario_save_common.h): Day 2 on the object stage, sunset,
// two blue Pikmin into the blue Onion and a red one lost. Before the clock jump
// the scenario gives the run state a save must carry: 7 ship parts (6 required),
// per-stage part counts, ship upgrade level 1, a part on the day graph, and the
// red, blue and yellow container bits (the yellow one so the save writes a
// yellow count of 0 rather than -1). save_load_flags also starts the run as
// Permadeath and Hard (pending + begin_new_run, what CardSelect does for a new
// file; the scenario boot sets them at stage ready instead of on the file screen).
//
// On the results screen the driver answers Yes to "save?" and presses A through
// the memory-card check (no file yet: the game makes its default file), the slot
// screen (slot A) and the save, the path captain_death takes. Asserted:
//   - the save runs (ogSaveMgr SavingInProgress, SaveComplete, then
//     SaveCompletedSuccess) and mDidSaveFail stays false;
//   - T13: the results screen leaves for MapSelect after the save;
//   - the scratch NECTAR_SAVE_DIR holds the card file and its stat record in
//     card0 and, only for a non-Permadeath run, the day history copy
//     card0/days/slot0/day03.bin (pc_days_store skips Permadeath runs);
//   - T14: the card file read back from disk (in-memory image cleared first) is
//     not broken; slot A's quick info has the saved day, part count and Pikmin
//     counts; after CardSelectSection's reset and loadCurrentGame the game in
//     memory equals the one snapshotted when the save ran, field by field
//     (scen_save::compareSnapshots: day, PlayState, parts, containers, per-stage
//     and per-day parts, Onion Pikmin by colour and stage, demo and result flags);
//   - T21: the file screen's per-slot marks (getQuickInfos peeks the port block)
//     and the rules the load adopts are the run's: off and off for save_load, on
//     and on for save_load_flags. The rules are forced to the opposite before the
//     load, so only the file can set them.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "MemoryCard.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "pc_permadeath.h"
#include "scenarios/pc_scenario_note.h"
#include "scenarios/scenario_save_common.h"

namespace {

using namespace scen_save;

class SaveLoad : public DayEndResults {
public:
	SaveLoad(const char* name, bool rules)
	    : DayEndResults(name)
	    , mRules(rules)
	{
	}

protected:
	bool setup(unsigned) override
	{
		setRunRules(mRules, mRules);
		PlayerState& p          = *playerState;
		p.mCurrParts            = 7;
		p.mRequiredUfoPartCount = 6;
		p.mShipUpgradeLevel     = 1;
		const u8 perStage[STAGE_COUNT] = { 3, 2, 1, 1, 0 };
		for (int i = 0; i < STAGE_COUNT; i++) p.mStagePartsCollected[i] = perStage[i];
		p.mPartsCollectedByDay[0] = 4;
		p.setContainer(Red);
		p.setContainer(Blue);
		p.setContainer(Yellow);
		mDriver.setPlan({}); // Yes, A everywhere
		return true;
	}

	void onResults(unsigned t) override
	{
		if (!mSaved && gameflow.mGamePrefs.mHasSaveGame) {
			mSaved = true;
			if (gameflow.mMemoryCard.mDidSaveFail) {
				scenarioFailf(t, "the save succeeds", "%s: mDidSaveFail", mName);
				return;
			}
			mAtSave = capture();
			printSnapshot(mName, "at the save", mAtSave);
		}
	}

	void onTerminal(unsigned t, int next) override
	{
		if (!mSaved) {
			scenarioFailf(t, "a save before the results screen closes", "%s: none; save manager %s", mName, noteHistory("savemgr").c_str());
			return;
		}
		for (int status : { 5, 6, 14 }) { // SavingInProgress, SaveComplete, SaveCompletedSuccess
			if (!noteSeen("savemgr", status)) {
				scenarioFailf(t, "the save manager runs the save (5, 6, 14)", "%s: %s", mName, noteHistory("savemgr").c_str());
				return;
			}
		}
		if (next != ONEPLAYER_MapSelect) {
			scenarioFailf(t, "the results screen leaves for MapSelect (6) after the save", "%s: section %d", mName, next);
			return;
		}
		waitCardIdle();
		const FileMap files = listSaveDir();
		printFiles(mName, "after the save", files);
		const bool history = files.count("card0/days/slot0/day03.bin") != 0;
		if (!files.count("card0/Pikmin dataFile") || !files.count("card0/.meta_Pikmin dataFile") || history == mRules) {
			scenarioFailf(t, mRules ? "the card file, its stat record and no day history (Permadeath)" : "the card file, its stat record and day03.bin",
			              "%s: card %d, stat %d, day history %d", mName, (int)files.count("card0/Pikmin dataFile"),
			              (int)files.count("card0/.meta_Pikmin dataFile"), (int)history);
			return;
		}

		CardQuickInfo infos[4];
		if (!reloadCard(t, mName, infos)) return;
		const CardQuickInfo& a = infos[0];
		std::printf("[scenario] %s: slot A from disk: block %d, status %u, day %d, parts %d, red %d yellow %d blue %d, permadeath %d hard %d\n",
		            mName, a.mMemCardSaveIndex, a.mSaveStatus, a.mCurrentDay, a.mCurrentPartsCount, a.mRedPikiCount, a.mYellowPikiCount,
		            a.mBluePikiCount, (int)pc_permadeath_slot(0), (int)pc_hardmode_slot(0));
		if (a.mMemCardSaveIndex < 0 || a.mSaveStatus != PlayState::ReadyToSave || a.mCurrentDay != mAtSave.day
		    || a.mCurrentPartsCount != mAtSave.shipParts || a.mRedPikiCount != mAtSave.red || a.mYellowPikiCount != mAtSave.yellow
		    || a.mBluePikiCount != mAtSave.blue) {
			scenarioFailf(t, "slot A read from disk shows the saved day, parts and Pikmin", "%s: block %d status %u day %d parts %d red %d yellow %d blue %d",
			              mName, a.mMemCardSaveIndex, a.mSaveStatus, a.mCurrentDay, a.mCurrentPartsCount, a.mRedPikiCount, a.mYellowPikiCount,
			              a.mBluePikiCount);
			return;
		}
		if (pc_permadeath_slot(0) != mRules || pc_hardmode_slot(0) != mRules) {
			scenarioFailf(t, "the slot's Permadeath and Hard marks are the run's", "%s: permadeath %d hard %d, run %d", mName,
			              (int)pc_permadeath_slot(0), (int)pc_hardmode_slot(0), (int)mRules);
			return;
		}

		setRunRules(!mRules, !mRules); // only the file may set them back
		loadSlot(a);
		const Snapshot loaded = capture();
		printSnapshot(mName, "loaded", loaded);
		if (!compareSnapshots(t, mName, mAtSave, loaded)) return;
		scenarioPass();
	}

private:
	bool mRules;
	bool mSaved = false;
	Snapshot mAtSave;
};

} // namespace

PcScenario* pc_scenario_make_save_load(void)
{
	static SaveLoad s("save_load", false);
	return &s;
}

PcScenario* pc_scenario_make_save_load_flags(void)
{
	static SaveLoad s("save_load_flags", true);
	return &s;
}

#endif
