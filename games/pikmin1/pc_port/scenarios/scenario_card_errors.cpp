// card_errors, card_errors_saved: every memory-card error the day-end save
// handles, injected into the card stubs (pc_card_fault.h), and its error screen
// (coverage/terminals.md, memory-card errors under T13 and T14).
//
// Runs DayEndResults (scenario_save_common.h) to the results screen. Each attempt
// waits for the results screen to be idle (ogSaveMgr inactive), sets one fault,
// presses A and answers Yes to "save?". The memory-card check
// (ogScrMemChkMgr::start, getMemoryCardState) or the save itself meets the fault,
// and the scenario answers the error screen so that it retries: No on "format?"
// and Retry (No) on the error's own question. The check returns ExitFailure, the
// save manager ExitFailure (12) and the results screen takes A again. Asserted per
// attempt: the fault was hit, the error screen's state was reached (memchk notes,
// ogMemChk.cpp), no save ran (no SavingInProgress) unless the attempt is the
// write failure, and the card in the scratch save directory is as before (no file
// on the empty card, byte-identical on the saved one).
//
// card_errors (empty card, no file yet):
//   no card (CARDProbe false): NoCard (8); not a card (mount WRONGDEVICE):
//   NotACard (9); unformatted (mount result ENCODING): the format question
//   (WritingFormatMsg 1, DoFormatSelection 2), then UnformattedCard (10); broken
//   (check result BROKEN): the format question, then BrokenCard (11); full (no
//   free bytes): CardFull (12); unusable (sector size 0x4000): UnusableCard (13);
//   no directory entry: FileNotMade (14); the default file cannot be created
//   (CARDCreate IOERROR): MakeDefaultFile (15), then BrokenCard (11). A create
//   failure with another code (INSSPACE, for one) is not reported: the card
//   thread sets mDidSaveFail only for IOERROR and MemoryCard::hasCardFinished
//   reads the result code after its own unmount, so the check reports the file
//   made and the save then reports success with no file written (seen with
//   INSSPACE; not part of this scenario).
// card_errors_saved (the card file with slots A, B and C saved on days 5, 4, 7,
// and the run playing slot A as if loaded from it, scen_save::pickSlot):
//   the write fails (CARDWrite IOERROR): the card is the run's own, so the save
//   skips the check and the slot screen ("same file") and runs at once
//   (SavingInProgress 5), then the failure window, FadeOut (2), ExitFailure (12),
//   mDidSaveFail; broken (check result BROKEN): BrokenCard; no card: NoCard.
// Last, with no fault, the save goes through to slot A and the results screen
// leaves for MapSelect. Read back from disk, slot A holds the new day (3); on the
// saved card slots B and C keep their block, checksum and day.
//
// Not covered: the format itself (Yes on "format?" erases card0, CARDFormat), the
// repair of a broken file section (RepairFile) and the faults during the slot
// screen's own second check.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/card.h"
#include "MemoryCard.h"
#include "gameflow.h"
#include "dolphin_stubs/pc_card_fault.h"
#include "scenarios/pc_scenario_note.h"
#include "scenarios/scenario_save_common.h"

namespace {

using namespace scen_save;

struct Attempt {
	const char* what;
	int fault, code;
	int status; // the ogScrMemChkMgr state the error shows; -1 for the write failure
	std::vector<Answer> answers;
};

class CardErrors : public DayEndResults {
public:
	CardErrors(const char* name, bool saved, std::vector<Attempt> attempts)
	    : DayEndResults(name)
	    , mSaved(saved)
	    , mAttempts(attempts)
	{
	}

protected:
	bool setup(unsigned t) override
	{
		if (mSaved) {
			if (!createCardFile(t, mName)) return false;
			const int days[3] = { 5, 4, 7 };
			const int today   = gameflow.mWorldClock.mCurrentDay;
			for (int s = 0; s < 3; s++) {
				gameflow.mWorldClock.mCurrentDay = days[s];
				if (!saveToSlot(t, mName, s)) return false;
			}
			gameflow.mWorldClock.mCurrentDay = today;
			if (!reloadCard(t, mName, mInfosBefore)) return false;
			pickSlot(mInfosBefore[0], 0); // the run plays slot A
		}
		mCardBefore = readCardFile();
		return true;
	}

	// The results screen is idle (its save manager inactive, noted this tick). The
	// next A starts the save manager, and no card call comes before its "save?"
	// is answered, so the fault set here is in place for the whole attempt.
	void startAttempt(unsigned t)
	{
		pc_scenario_note_clear("memchk");
		pc_scenario_note_clear("savemgr");
		if (mNext < mAttempts.size()) {
			const Attempt& a = mAttempts[mNext];
			pc_card_fault_set(a.fault, a.code);
			mDriver.setPlan(a.answers);
			std::printf("[scenario] %s: attempt '%s' at t=%u\n", mName, a.what, t);
		} else {
			pc_card_fault_clear_all();
			mDriver.setPlan({ Yes });
			std::printf("[scenario] %s: the save with no fault at t=%u\n", mName, t);
		}
		mStep     = Running;
		mStepTick = t;
		mSeenBusy = false;
	}

	void onResults(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		int saveMgr         = 0;
		const bool noted    = pc_scenario_note_take("savemgr", &saveMgr);
		if (mStep == Idle) {
			if (noted && saveMgr == -1) startAttempt(t); // past the diary page, the results are up
			return;
		}
		if (noted && saveMgr != -1) mSeenBusy = true;
		if (mNext < mAttempts.size() && noted && saveMgr == -1 && mSeenBusy) {
			if (!finishAttempt(t)) return;
			mNext++;
			startAttempt(t);
			return;
		}
		if (t - mStepTick > 40 * rate) {
			scenarioFailf(t, "each attempt ends within 40 s", "%s: attempt %zu; save manager %s; card check %s", mName, mNext,
			              noteHistory("savemgr").c_str(), noteHistory("memchk").c_str());
		}
	}

	void onTerminal(unsigned t, int next) override
	{
		pc_card_fault_clear_all();
		if (mNext != mAttempts.size()) {
			scenarioFailf(t, "every attempt retried before the save", "%s: left during attempt %zu (section %d); save manager %s", mName, mNext,
			              next, noteHistory("savemgr").c_str());
			return;
		}
		if (next != ONEPLAYER_MapSelect || !noteSeen("savemgr", 14) || gameflow.mMemoryCard.mDidSaveFail) {
			scenarioFailf(t, "the save with no fault succeeds and leaves for MapSelect", "%s: section %d, save manager %s, failed %d", mName, next,
			              noteHistory("savemgr").c_str(), (int)gameflow.mMemoryCard.mDidSaveFail);
			return;
		}
		CardQuickInfo infos[4];
		if (!reloadCard(t, mName, infos)) return;
		if (infos[0].mSaveStatus != PlayState::ReadyToSave || infos[0].mCurrentDay != 3) {
			scenarioFailf(t, "slot A holds the save of day 3", "%s: status %u day %d", mName, infos[0].mSaveStatus, infos[0].mCurrentDay);
			return;
		}
		if (mSaved) {
			for (int s : { 1, 2 }) {
				if (infos[s].mMemCardSaveIndex != mInfosBefore[s].mMemCardSaveIndex || infos[s].mCrc != mInfosBefore[s].mCrc
				    || infos[s].mCurrentDay != mInfosBefore[s].mCurrentDay) {
					scenarioFailf(t, "slots B and C untouched", "%s: slot %d block %d->%d crc %08x->%08x day %d->%d", mName, s,
					              mInfosBefore[s].mMemCardSaveIndex, infos[s].mMemCardSaveIndex, mInfosBefore[s].mCrc, infos[s].mCrc,
					              mInfosBefore[s].mCurrentDay, infos[s].mCurrentDay);
					return;
				}
			}
		}
		std::printf("[scenario] %s: slot A day %d after %zu failed attempts\n", mName, infos[0].mCurrentDay, mAttempts.size());
		scenarioPass();
	}

private:
	bool finishAttempt(unsigned t)
	{
		const Attempt& a = mAttempts[mNext];
		const int hits   = pc_card_fault_hits(a.fault);
		pc_card_fault_clear_all();
		waitCardIdle();
		const std::string checks = noteHistory("memchk"), saves = noteHistory("savemgr");
		std::printf("[scenario] %s: attempt '%s' ended at t=%u: fault hits %d, card check %s, save manager %s, failed %d\n", mName, a.what, t, hits,
		            checks.c_str(), saves.c_str(), (int)gameflow.mMemoryCard.mDidSaveFail);
		if (hits == 0) {
			scenarioFailf(t, "the injected fault is reached", "%s: '%s' never hit", mName, a.what);
			return false;
		}
		if (mDriver.answered() != a.answers.size()) {
			scenarioFailf(t, "every planned answer was given", "%s: '%s' %zu of %zu", mName, a.what, mDriver.answered(), a.answers.size());
			return false;
		}
		if (!noteSeen("savemgr", 12) || noteSeen("savemgr", 13) || noteSeen("savemgr", 14)) {
			scenarioFailf(t, "the save manager ends in ExitFailure (12)", "%s: '%s' save manager %s", mName, a.what, saves.c_str());
			return false;
		}
		if (a.status >= 0) {
			if (!noteSeen("memchk", a.status) || noteSeen("savemgr", 5)) {
				scenarioFailf(t, "the card check shows the error and no save runs", "%s: '%s' wanted state %d; card check %s; save manager %s", mName,
				              a.what, a.status, checks.c_str(), saves.c_str());
				return false;
			}
		} else if (!noteSeen("savemgr", 5) || noteSeen("savemgr", 6) || !gameflow.mMemoryCard.mDidSaveFail || pc_scenario_note_count("memchk") != 0) {
			scenarioFailf(t, "the save runs at once (no card check) and fails (5, no 6, mDidSaveFail)", "%s: '%s' save manager %s, card check %s, failed %d",
			              mName, a.what, saves.c_str(), checks.c_str(), (int)gameflow.mMemoryCard.mDidSaveFail);
			return false;
		}
		if (readCardFile() != mCardBefore) {
			scenarioFailf(t, mSaved ? "the card file is byte-identical after the error" : "no card file after the error", "%s: '%s' changed it",
			              mName, a.what);
			return false;
		}
		return true;
	}

	enum Step { Idle, Running };
	bool mSaved;
	std::vector<Attempt> mAttempts;
	Step mStep         = Idle;
	size_t mNext       = 0;
	unsigned mStepTick = 0;
	bool mSeenBusy     = false;
	std::vector<uint8_t> mCardBefore;
	CardQuickInfo mInfosBefore[4] = {};
};

} // namespace

PcScenario* pc_scenario_make_card_errors(void)
{
	static CardErrors s("card_errors", false,
	                    {
	                        { "no card", PC_CARD_FAULT_PROBE, 1, 8, { Yes, No } },
	                        { "not a card", PC_CARD_FAULT_MOUNT, CARD_RESULT_WRONGDEVICE, 9, { Yes, No } },
	                        { "unformatted", PC_CARD_FAULT_MOUNT_RESULT, CARD_RESULT_ENCODING, 10, { Yes, No, No } },
	                        { "broken", PC_CARD_FAULT_CHECK_RESULT, CARD_RESULT_BROKEN, 11, { Yes, No, No } },
	                        { "full", PC_CARD_FAULT_NO_FREE_BYTES, 1, 12, { Yes, No } },
	                        { "unusable", PC_CARD_FAULT_SECTOR_SIZE, 0x4000, 13, { Yes, No } },
	                        { "no directory entry", PC_CARD_FAULT_NO_FREE_FILES, 1, 14, { Yes, No } },
	                        { "create fails", PC_CARD_FAULT_CREATE, CARD_RESULT_IOERROR, 15, { Yes, No } },
	                    });
	return &s;
}

PcScenario* pc_scenario_make_card_errors_saved(void)
{
	static CardErrors s("card_errors_saved", true,
	                    {
	                        { "write fails", PC_CARD_FAULT_WRITE, CARD_RESULT_IOERROR, -1, { Yes } },
	                        { "broken", PC_CARD_FAULT_CHECK_RESULT, CARD_RESULT_BROKEN, 11, { Yes, No, No } },
	                        { "no card", PC_CARD_FAULT_PROBE, 1, 8, { Yes, No } },
	                    });
	return &s;
}

#endif
