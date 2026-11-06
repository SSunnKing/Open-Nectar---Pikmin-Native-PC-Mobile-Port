// permadeath_erase, permadeath_unsaved, permadeath_bad_slot, permadeath_off: the
// port's Permadeath rule, which deletes the save of a run whose captain dies
// (coverage/terminals.md T20; MessageModeState STATE_ForceDayEnd and
// pcErasePermadeathSave in newPikiGame.cpp).
//
// Sandbox stage, Day 1. Before the death the scenario prepares the scratch
// NECTAR_SAVE_DIR through the game's own card code: the card file
// (MemoryCard::makeDefaultFile) and, for every variant but permadeath_unsaved,
// three saves made with MemoryCard::saveCurrentGame: slot A on day 5, slot B on
// day 4 and slot C on day 7 as normal files (each writes card0/days/slotN/
// dayNN.bin), then slot B again as the Permadeath file of the run, on day 1 (no
// day history for a Permadeath run). The run's rules are set as a new Permadeath file gets them, the play
// state names the slot (B; 3 for permadeath_bad_slot), every file under the
// scratch directory is hashed and the card is read back. Then the captain takes a
// 1000-damage Flick at tick 10, as in captain_death.
//
// Asserted for every variant: Dead (29) once, GAMEEND_NaviDown, the day does not
// advance, and a soft reset within the bounds.
//   - permadeath_erase: the soft reset goes to CardSelect (1) and DayOverModeState
//     never starts (no results, no day-end save); the "expedition lost" notice is
//     queued (pc_erased_notice_open_if_queued; the variants that erase nothing do
//     not queue it). In the scratch directory exactly these change: the card file
//     (changed), card0/days/slot1/ and its day04.bin
//     (removed); the card's stat record stays (its bytes hold the write time in
//     seconds, so they may change). Nothing is added, slots A and C's day history
//     are untouched. Read back from disk: slot B is a fresh slot
//     (status Fresh, day 1, no Permadeath or Hard mark), slots A and C keep their
//     block, checksum, day and Pikmin; the banner and option sections are
//     byte-identical, and exactly one of the four 0x8000 game blocks changed: the
//     block that was the spare, which slot B now reads from. Slot B's old block
//     keeps its bytes (the delete supersedes it, as the original's delete does).
//   - permadeath_unsaved: no card file at all. The run still leaves for CardSelect
//     with no notice; the scratch directory is unchanged (no card file is
//     created, nothing is removed).
//   - permadeath_bad_slot: the play state names slot 3 (out of range): nothing in
//     the scratch directory changes; CardSelect, no notice.
//   - permadeath_off: the same saves, a normal run: the death goes to
//     DayOverModeState as in captain_death (the scenario passes when it starts) and
//     the scratch directory is unchanged at that point.
// The runner removes the scratch directory at exit; the CTest entry
// scenario_save_dir_permadeath_erase (check_scenario_save_dir.cmake) checks that
// nothing appears outside it.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "FlowController.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MemoryCard.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Section.h"
#include "gameflow.h"
#include "pc_permadeath.h"
#include "scenarios/scenario_save_common.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

using namespace scen_save;

constexpr unsigned kSetupTick = 5;
constexpr unsigned kHitTick   = 10;
constexpr int kBlock          = 0x8000;
constexpr int kGameBlocks     = 0x6000; // banner 0x2000, two option blocks of 0x2000

enum Variant { Erase, Unsaved, BadSlot, Off };

class Permadeath : public PcScenario {
public:
	Permadeath(const char* name, Variant v)
	    : mName(name)
	    , mVariant(v)
	{
	}
	const char* name() const override { return mName; }

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr;
		const int cap       = navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1;
		if (cap == NAVISTATE_Dead && mLastCap != NAVISTATE_Dead) mDeadEntries++;
		mLastCap = cap;
		if (mDeadEntries > 1) {
			scenarioFailf(t, "the Dead state is entered once", "%s: %d times", mName, mDeadEntries);
			return;
		}
		if (gameflow.mWorldClock.mCurrentDay != 1) {
			scenarioFailf(t, "the day does not advance", "%s: day %d", mName, gameflow.mWorldClock.mCurrentDay);
			return;
		}
		if (t == kSetupTick) {
			prepare(t);
			return;
		}
		if (t == kHitTick) {
			if (!navi || cap != NAVISTATE_Walk) {
				scenarioFailf(t, "the captain walking at the hit", "%s: state %d", mName, cap);
				return;
			}
			navi->stimulate(InteractFlick(navi, 0.0f, 1000.0f, 0.0f));
			return;
		}
		if (t < kHitTick) return;
		if (!mDead && cap == NAVISTATE_Dead) mDead = t;
		if (!mDown && flowCont.mGameEndFlag == GAMEEND_NaviDown) mDown = t;
		if (!mDead && t - kHitTick > 8 * rate) {
			scenarioFailf(t, "Dead within 8 s of the hit", "%s: state %d", mName, cap);
			return;
		}
		if (mDead && !mDown && t - mDead > 4 * rate) {
			scenarioFailf(t, "GAMEEND_NaviDown within 4 s of Dead", "%s: flag %d", mName, (int)flowCont.mGameEndFlag);
			return;
		}
		if (mVariant == Off) {
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] %s: DayOverModeState at t=%u (permadeath off)\n", mName, t);
				if (gsys->mSoftResetPending) {
					scenarioFailf(t, "no soft reset before the day-end results", "%s: soft reset pending", mName);
					return;
				}
				if (checkUnchanged(t)) scenarioPass();
				return;
			}
		} else if (flowCont.mIsDayEndSeqStarted) {
			scenarioFailf(t, "no DayOverModeState in a Permadeath run (no results, no day-end save)", "%s: it started", mName);
			return;
		}
		if (gsys->mSoftResetPending) {
			const int next = gameflow.mNextOnePlayerSectionID;
			std::printf("[scenario] %s: soft reset at t=%u (%u ticks after NaviDown), next section %d\n", mName, t, t - mDown, next);
			if (mVariant == Off || !mDown) {
				scenarioFailf(t, "the soft reset only after NaviDown, in a Permadeath run", "%s: next %d", mName, next);
				return;
			}
			if (next != ONEPLAYER_CardSelect) {
				scenarioFailf(t, "a Permadeath death leaves for CardSelect (1)", "%s: section %d", mName, next);
				return;
			}
			// The notice says the save is gone: queued only when one was erased.
			const bool wantNotice = mVariant == Erase;
			if (pc_erased_notice_open_if_queued() != wantNotice) {
				scenarioFailf(t, wantNotice ? "the expedition-lost notice is queued" : "no expedition-lost notice when nothing was erased",
				              "%s: queued %d", mName, (int)!wantNotice);
				return;
			}
			if (mVariant == Erase ? checkErased(t) : checkUnchanged(t)) scenarioPass();
			return;
		}
		if (mDown && t - mDown > 25 * rate) {
			scenarioFailf(t, "the death resolves within 25 s of NaviDown", "%s: no soft reset, no day end", mName);
		}
	}

private:
	void prepare(unsigned t)
	{
		if (mVariant != Unsaved) {
			if (!createCardFile(t, mName)) return;
			struct Save {
				int slot, day;
				bool permadeath;
			};
			const Save saves[] = { { 0, 5, false }, { 1, 4, false }, { 2, 7, false }, { 1, 1, true } };
			for (const Save& s : saves) {
				setRunRules(s.permadeath, false);
				gameflow.mWorldClock.mCurrentDay = s.day;
				if (!saveToSlot(t, mName, s.slot)) return;
			}
			gameflow.mWorldClock.mCurrentDay = 1;

		}
		setRunRules(mVariant != Off, false);
		gameflow.mPlayState.mSaveSlot = mVariant == BadSlot ? 3 : 1;
		waitCardIdle();
		mFilesBefore = listSaveDir();
		mCardBefore  = readCardFile();
		printFiles(mName, "before the death", mFilesBefore);
		if (mVariant == Unsaved) {
			if (!mCardBefore.empty()) scenarioFailf(t, "no card file", "%s: %zu bytes", mName, mCardBefore.size());
			return;
		}
		if (!reloadCard(t, mName, mInfosBefore)) return;
		for (int s = 0; s < 3; s++) {
			const CardQuickInfo& i = mInfosBefore[s];
			std::printf("[scenario] %s: before: slot %d block %d status %u day %d permadeath %d\n", mName, s, i.mMemCardSaveIndex, i.mSaveStatus,
			            i.mCurrentDay, (int)pc_permadeath_slot(s));
			const int day = s == 0 ? 5 : s == 1 ? 1 : 7;
			if (i.mSaveStatus != PlayState::ReadyToSave || i.mCurrentDay != day || pc_permadeath_slot(s) != (s == 1)) {
				scenarioFailf(t, "slots A, B, C saved on days 5, 1, 7, only B marked Permadeath", "%s: slot %d status %u day %d mark %d", mName, s,
				              i.mSaveStatus, i.mCurrentDay, (int)pc_permadeath_slot(s));
				return;
			}
		}
		pickSlot(mInfosBefore[1], 1); // the run plays slot B, as if loaded from it
		if (mVariant == BadSlot) gameflow.mPlayState.mSaveSlot = 3;
	}

	bool checkUnchanged(unsigned t)
	{
		waitCardIdle();
		const FileMap after = listSaveDir();
		const std::string diff = diffFiles(mFilesBefore, after);
		if (!diff.empty()) {
			printFiles(mName, "after", after);
			scenarioFailf(t, "nothing in the scratch save directory changes", "%s: %s", mName, diff.c_str());
			return false;
		}
		std::printf("[scenario] %s: scratch save directory unchanged (%zu entries)\n", mName, after.size());
		return true;
	}

	bool checkErased(unsigned t)
	{
		waitCardIdle();
		const FileMap after = listSaveDir();
		printFiles(mName, "after the erase", after);
		// The stat record holds the write time in seconds, so whether its bytes change
		// depends on the wall clock; it must still be there.
		FileMap before = mFilesBefore, now = after;
		const char* meta = "card0/.meta_Pikmin dataFile";
		if (!now.count(meta)) {
			scenarioFailf(t, "the card file's stat record stays", "%s: it is gone", mName);
			return false;
		}
		before.erase(meta);
		now.erase(meta);
		const std::string diff = diffFiles(before, now);
		const std::string want = "removed card0/days/slot1/, card0/days/slot1/day04.bin; changed card0/Pikmin dataFile";
		std::printf("[scenario] %s: scratch save directory: %s\n", mName, diff.c_str());
		if (diff != want) {
			scenarioFailf(t, "only the card file, its stat record, slot B's day history change", "%s: %s", mName,
			              diff.c_str());
			return false;
		}
		const std::vector<uint8_t> card = readCardFile();
		if (card.size() != mCardBefore.size() || card.size() < size_t(kGameBlocks + 4 * kBlock)) {
			scenarioFailf(t, "the card file keeps its size", "%s: %zu bytes, was %zu", mName, card.size(), mCardBefore.size());
			return false;
		}
		if (std::memcmp(card.data(), mCardBefore.data(), kGameBlocks) != 0) {
			scenarioFailf(t, "the banner and option sections are untouched", "%s: they changed", mName);
			return false;
		}
		int changed = -1, count = 0;
		for (int b = 0; b < 4; b++) {
			if (std::memcmp(card.data() + kGameBlocks + b * kBlock, mCardBefore.data() + kGameBlocks + b * kBlock, kBlock) != 0) {
				changed = b;
				count++;
			}
		}
		int spare = -1;
		for (int b = 0; b < 4; b++) {
			bool used = false;
			for (int s = 0; s < 3; s++) used |= mInfosBefore[s].mMemCardSaveIndex == b;
			if (!used) spare = b;
		}
		CardQuickInfo infos[4];
		if (!reloadCard(t, mName, infos)) return false;
		for (int s = 0; s < 3; s++) {
			const CardQuickInfo& i = infos[s];
			std::printf("[scenario] %s: after: slot %d block %d status %u day %d red %d blue %d permadeath %d\n", mName, s, i.mMemCardSaveIndex,
			            i.mSaveStatus, i.mCurrentDay, i.mRedPikiCount, i.mBluePikiCount, (int)pc_permadeath_slot(s));
		}
		std::printf("[scenario] %s: game blocks changed %d (block %d), spare before %d; slot B's old block %d %s\n", mName, count, changed, spare,
		            mInfosBefore[1].mMemCardSaveIndex,
		            std::memcmp(card.data() + kGameBlocks + mInfosBefore[1].mMemCardSaveIndex * kBlock,
		                        mCardBefore.data() + kGameBlocks + mInfosBefore[1].mMemCardSaveIndex * kBlock, kBlock)
		                    == 0
		                ? "keeps its bytes (superseded, not wiped)"
		                : "was rewritten");
		if (count != 1 || changed != spare || infos[1].mMemCardSaveIndex != spare) {
			scenarioFailf(t, "exactly one game block changes, the spare, and slot B now reads from it", "%s: %d changed (block %d), spare %d, slot B block %d",
			              mName, count, changed, spare, infos[1].mMemCardSaveIndex);
			return false;
		}
		const CardQuickInfo& b = infos[1];
		// The Pikmin and part counts of the fresh header are not checked: delFile writes
		// them through writeCurrentGame, which fills them from the game in memory (the
		// original's delete does the same; the file screen shows a Fresh slot as new).
		if (b.mSaveStatus != PlayState::Fresh || b.mCurrentDay != 1 || pc_permadeath_slot(1) || pc_hardmode_slot(1)) {
			scenarioFailf(t, "slot B reads back as a fresh slot (Fresh, day 1, no marks)", "%s: status %u day %d marks %d %d", mName, b.mSaveStatus,
			              b.mCurrentDay, (int)pc_permadeath_slot(1), (int)pc_hardmode_slot(1));
			return false;
		}
		for (int s : { 0, 2 }) {
			const CardQuickInfo &was = mInfosBefore[s], &now = infos[s];
			if (now.mMemCardSaveIndex != was.mMemCardSaveIndex || now.mCrc != was.mCrc || now.mCurrentDay != was.mCurrentDay
			    || now.mSaveStatus != was.mSaveStatus || now.mRedPikiCount != was.mRedPikiCount || pc_permadeath_slot(s)) {
				scenarioFailf(t, "slots A and C are untouched", "%s: slot %d block %d->%d crc %08x->%08x day %d->%d", mName, s, was.mMemCardSaveIndex,
				              now.mMemCardSaveIndex, was.mCrc, now.mCrc, was.mCurrentDay, now.mCurrentDay);
				return false;
			}
		}
		return true;
	}

	const char* mName;
	Variant mVariant;
	int mLastCap = -2, mDeadEntries = 0;
	unsigned mDead = 0, mDown = 0;
	FileMap mFilesBefore;
	std::vector<uint8_t> mCardBefore;
	CardQuickInfo mInfosBefore[4] = {};
};

} // namespace

PcScenario* pc_scenario_make_permadeath_erase(void)
{
	static Permadeath s("permadeath_erase", Erase);
	return &s;
}

PcScenario* pc_scenario_make_permadeath_unsaved(void)
{
	static Permadeath s("permadeath_unsaved", Unsaved);
	return &s;
}

PcScenario* pc_scenario_make_permadeath_bad_slot(void)
{
	static Permadeath s("permadeath_bad_slot", BadSlot);
	return &s;
}

PcScenario* pc_scenario_make_permadeath_off(void)
{
	static Permadeath s("permadeath_off", Off);
	return &s;
}

#endif
