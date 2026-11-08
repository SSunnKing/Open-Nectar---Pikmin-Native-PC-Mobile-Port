// pause_continue: the pause menu's "Continue from last save" (coverage/
// terminals.md T15; ogScrPauseMgr, RunningModeState::update PAUSE_ExitToTitle).
//
// Sandbox stage, Day 2: on Day 1 (and Day 30) ogScrPauseMgr::update disables "Go
// to sunset" and the cursor skips it. A timed script, in seconds so the same at 30
// and 60 Hz: START opens the pause menu (pause.blo); after 1.5 s the stick moves
// the cursor down twice (Continue, Go to sunset, Continue from last save, 0.5 s
// apart); A opens the confirmation (pause_ok.blo, which starts on Yes) and A every
// 0.5 s from 1 s later takes Yes once the confirmation is up (the main menu fades
// out first). The menu fades out (0.5 s) and RunningModeState queues
// QuittingGameModeState toward CardSelect.
//
// Asserted: the soft reset is pending toward CardSelect (1) within 10 s of START;
// no day end started and the day is still 2; nothing was saved (mHasSaveGame
// false, the scratch save directory unchanged, no card file): continuing from the
// last save discards the day in progress.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "Section.h"
#include "gameflow.h"
#include "scenarios/scenario_save_common.h"
#include "system.h"

namespace {

using namespace scen_save;

constexpr unsigned kStartTick = 30;

class PauseContinue : public PcScenario {
public:
	const char* name() const override { return "pause_continue"; }
	int startDay() const override { return 2; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned half = rate / 2;
		if (t == kStartTick) pad.button |= PAD_BUTTON_START;
		if (t == kStartTick + 3 * half || t == kStartTick + 4 * half) pad.stickY = -70; // down, down
		if (t == kStartTick + 5 * half) pad.button |= PAD_BUTTON_A; // the entry
		// Yes on the confirmation, every 0.5 s from 1 s on (the main menu fades out first)
		if (t >= kStartTick + 7 * half && t <= kStartTick + 15 * half && (t - kStartTick) % half == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (gameflow.mIsDayEndActive || flowCont.mIsDayEndSeqStarted || gameflow.mWorldClock.mCurrentDay != 2) {
			scenarioFailf(t, "no day end and no day change", "day end %d, day-end sequence %d, day %d", (int)gameflow.mIsDayEndActive,
			              (int)flowCont.mIsDayEndSeqStarted, gameflow.mWorldClock.mCurrentDay);
			return;
		}
		if (t == 0) {
			mFilesBefore = listSaveDir();
			return;
		}
		if (gsys->mSoftResetPending) {
			const int next = gameflow.mNextOnePlayerSectionID;
			std::printf("[scenario] pause_continue: soft reset at t=%u (%u ticks after START), next section %d\n", t, t - kStartTick, next);
			if (t < kStartTick || next != ONEPLAYER_CardSelect) {
				scenarioFailf(t, "Continue from last save leaves for CardSelect (1)", "section %d at t=%u", next, t);
				return;
			}
			const std::string diff = diffFiles(mFilesBefore, listSaveDir());
			if (gameflow.mGamePrefs.mHasSaveGame || !diff.empty() || !readCardFile().empty()) {
				scenarioFailf(t, "nothing saved", "mHasSaveGame %d, save directory: %s", (int)gameflow.mGamePrefs.mHasSaveGame,
				              diff.empty() ? "unchanged" : diff.c_str());
				return;
			}
			scenarioPass();
			return;
		}
		if (t > kStartTick + 10 * rate) {
			scenarioFailf(t, "the soft reset within 10 s of START", "none (the pause menu did not take the script)");
		}
	}

private:
	FileMap mFilesBefore;
};

} // namespace

PcScenario* pc_scenario_make_pause_continue(void)
{
	static PauseContinue s;
	return &s;
}

#endif
