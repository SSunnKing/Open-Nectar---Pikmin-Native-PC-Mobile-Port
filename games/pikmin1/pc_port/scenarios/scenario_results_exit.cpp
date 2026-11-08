// results_nosave_map, results_nosave_cardselect: the results screen's exits
// that do not save (coverage/terminals.md T13). captain_death and save_load take
// the save path to MapSelect.
//
// Runs DayEndResults (scenario_save_common.h) to the results screen, then answers
// the save menu (ogSaveMgr, ac_save.blo and ac_save2.blo):
//   - results_nosave_map: "save?" No, then the second question Yes: ExitSuccess
//     (13), RESULT_ExitToMapSelect, the soft reset goes to MapSelect (6);
//   - results_nosave_cardselect: first B on "save?" (cancel: the menu closes,
//     ExitFailure (12), and the results screen takes A again), then No and No:
//     AlternativeExitSuccess (15), RESULT_ExitToCardSelect, the soft reset goes to
//     CardSelect (1), the file screen.
// Both assert the second question was shown (SecondarySelectionActive, 9), the
// save never ran (no SavingInProgress, 5; mHasSaveGame false) and the scratch
// save directory holds no card file. ogSaveMgr only starts the memory-card check
// after a Yes, so the card is never touched.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "gameflow.h"
#include "scenarios/scenario_save_common.h"

namespace {

using namespace scen_save;

class ResultsNoSave : public DayEndResults {
public:
	ResultsNoSave(const char* name, std::vector<Answer> plan, int next, bool cancelFirst)
	    : DayEndResults(name)
	    , mPlan(plan)
	    , mNext(next)
	    , mCancelFirst(cancelFirst)
	{
	}

protected:
	bool setup(unsigned) override
	{
		mDriver.setPlan(mPlan);
		return true;
	}

	void onTerminal(unsigned t, int next) override
	{
		const FileMap files = listSaveDir();
		printFiles(mName, "at the exit", files);
		if (mDriver.answered() != mPlan.size()) {
			scenarioFailf(t, "every planned answer was given", "%s: %zu of %zu", mName, mDriver.answered(), mPlan.size());
			return;
		}
		if (!noteSeen("savemgr", 9) || noteSeen("savemgr", 5) || gameflow.mGamePrefs.mHasSaveGame) {
			scenarioFailf(t, "the second question shown (9), no save (no 5, mHasSaveGame false)", "%s: save manager %s, mHasSaveGame %d", mName,
			              noteHistory("savemgr").c_str(), (int)gameflow.mGamePrefs.mHasSaveGame);
			return;
		}
		if (mCancelFirst && !noteSeen("savemgr", 12)) {
			scenarioFailf(t, "B on the first question goes back to the results (ExitFailure 12)", "%s: save manager %s", mName,
			              noteHistory("savemgr").c_str());
			return;
		}
		if (files.count("card0/Pikmin dataFile") || files.count("card0/.meta_Pikmin dataFile")) {
			scenarioFailf(t, "no card file written", "%s: the card file exists", mName);
			return;
		}
		if (next != mNext) {
			scenarioFailf(t, mNext == ONEPLAYER_MapSelect ? "the exit goes to MapSelect (6)" : "the exit goes to CardSelect (1)", "%s: section %d",
			              mName, next);
			return;
		}
		scenarioPass();
	}

private:
	std::vector<Answer> mPlan;
	int mNext;
	bool mCancelFirst;
};

} // namespace

PcScenario* pc_scenario_make_results_nosave_map(void)
{
	static ResultsNoSave s("results_nosave_map", { No, Yes }, ONEPLAYER_MapSelect, false);
	return &s;
}

PcScenario* pc_scenario_make_results_nosave_cardselect(void)
{
	static ResultsNoSave s("results_nosave_cardselect", { Cancel, No, No }, ONEPLAYER_CardSelect, true);
	return &s;
}

#endif
