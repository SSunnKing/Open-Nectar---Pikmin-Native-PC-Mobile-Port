#include "scenarios/scenario_save_common.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "BaseInf.h"
#include "CardUtil.h"
#include "Demo.h"
#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "Generator.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "Stream.h"
#include "gameflow.h"
#include "pc_permadeath.h"
#include "scenarios/pc_scenario.h"
#include "scenarios/pc_scenario_note.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace fs = std::filesystem;

namespace scen_save {

namespace {

uint64_t fnv(const std::vector<uint8_t>& bytes)
{
	uint64_t h = 1469598103934665603ull;
	for (uint8_t b : bytes) {
		h ^= b;
		h *= 1099511628211ull;
	}
	return h;
}

std::vector<uint8_t> readFile(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

template <typename T>
std::vector<uint8_t> serialise(T& flags)
{
	std::vector<uint8_t> buf(0x1000, 0);
	RamStream stream(buf.data(), (int)buf.size());
	flags.saveCard(stream);
	buf.resize(stream.getPosition());
	return buf;
}

} // namespace

std::string saveDir()
{
	const char* dir = std::getenv("NECTAR_SAVE_DIR");
	return dir ? dir : "";
}

FileMap listSaveDir()
{
	FileMap files;
	std::error_code ec;
	const fs::path root = saveDir();
	if (root.empty()) return files;
	for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		const std::string rel = fs::relative(it->path(), root, ec).generic_string();
		// The GL shader cache also lives in the save dir; Mesa (Linux CI) writes to it
		// mid-run, macOS and Windows drivers do not. It is not save data.
		if (rel == "shader_cache" || rel.rfind("shader_cache/", 0) == 0) continue;
		if (it->is_directory(ec)) {
			files[rel + "/"] = 0;
		} else {
			files[rel] = fnv(readFile(it->path()));
		}
	}
	return files;
}

std::string diffFiles(const FileMap& before, const FileMap& after)
{
	std::string added, removed, changed;
	auto add = [](std::string& list, const std::string& name) { list += (list.empty() ? "" : ", ") + name; };
	for (const auto& [name, hash] : after) {
		auto it = before.find(name);
		if (it == before.end()) add(added, name);
		else if (it->second != hash) add(changed, name);
	}
	for (const auto& [name, hash] : before) {
		if (!after.count(name)) add(removed, name);
	}
	std::string out;
	if (!added.empty()) out += "added " + added;
	if (!removed.empty()) out += (out.empty() ? "" : "; ") + std::string("removed ") + removed;
	if (!changed.empty()) out += (out.empty() ? "" : "; ") + std::string("changed ") + changed;
	return out;
}

void printFiles(const char* scenario, const char* label, const FileMap& files)
{
	std::printf("[scenario] %s: save dir %s (%zu entries)\n", scenario, label, files.size());
	for (const auto& [name, hash] : files) std::printf("[scenario] %s:   %s %016llx\n", scenario, name.c_str(), (unsigned long long)hash);
}

std::vector<uint8_t> readCardFile() { return readFile(fs::path(saveDir()) / "card0" / "Pikmin dataFile"); }

void waitCardIdle()
{
	CardUtilIdleWhileBusy();
	gameflow.mMemoryCard.hasCardFinished();
}

bool reloadCard(unsigned tick, const char* scenario, CardQuickInfo infos[4])
{
	waitCardIdle();
	std::memset(cardData, 0, sizeof(cardData));
	const s32 state = gameflow.mMemoryCard.getMemoryCardState(true);
	if (state != CARD_RESULT_READY || gameflow.mMemoryCard.mSaveFileIndex < 0) {
		scenarioFailf(tick, "the card file reads back from disk", "%s: getMemoryCardState %d, file index %d", scenario, (int)state,
		              gameflow.mMemoryCard.mSaveFileIndex);
		return false;
	}
	if (gameflow.mMemoryCard.isFileBroken()) {
		scenarioFailf(tick, "the card file read back is not broken (every checksum matches)", "%s: sections mask %#x", scenario,
		              (unsigned)gameflow.mMemoryCard.mOkSectionsMask);
		return false;
	}
	gameflow.mMemoryCard.getQuickInfos(infos);
	return true;
}

bool createCardFile(unsigned tick, const char* scenario)
{
	waitCardIdle();
	const s32 state = gameflow.mMemoryCard.getMemoryCardState(true);
	if (state != CARD_RESULT_READY || gameflow.mMemoryCard.mSaveFileIndex != -1) {
		scenarioFailf(tick, "an empty card (no game file yet)", "%s: getMemoryCardState %d, file index %d", scenario, (int)state,
		              gameflow.mMemoryCard.mSaveFileIndex);
		return false;
	}
	gameflow.mMemoryCard.makeDefaultFile();
	waitCardIdle();
	CardQuickInfo infos[4];
	return reloadCard(tick, scenario, infos);
}

bool saveToSlot(unsigned tick, const char* scenario, int slot)
{
	CardQuickInfo infos[4];
	gameflow.mMemoryCard.getQuickInfos(infos); // picks the spare block, as the slot screen does
	gameflow.mPlayState.mSaveSlot = slot;
	gameflow.mMemoryCard.saveCurrentGame();
	waitCardIdle();
	if (gameflow.mMemoryCard.mDidSaveFail) {
		scenarioFailf(tick, "the save succeeds", "%s: slot %d, mDidSaveFail", scenario, slot);
		return false;
	}
	return true;
}

void setRunRules(bool permadeath, bool hard)
{
	pc_permadeath_set_pending(permadeath);
	pc_hardmode_set_pending(hard);
	pc_permadeath_begin_new_run();
	pc_hardmode_begin_new_run();
	pc_permadeath_set_pending(false);
	pc_hardmode_set_pending(false);
}

void pickSlot(const CardQuickInfo& info, int slot)
{
	gameflow.mGamePrefs.mHasSaveGame        = true;
	gameflow.mSaveGameCrc                   = info.mCrc;
	gameflow.mGamePrefs.mMostRecentFileSlot = slot;
	gameflow.mGamePrefs.mMemCardSaveIndex   = info.mMemCardSaveIndex + 1;
	gameflow.mPlayState.mSaveSlot           = slot;
}

void loadSlot(const CardQuickInfo& info)
{
	// CardSelectSection::CardSelectSection, then the file select's choice of a slot.
	playerState->initGame();
	generatorCache->initGame();
	pikiInfMgr.initGame();
	FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
	{
		stage->mHasInitialised = FALSE;
		stage->mStageInf.initGame();
	}
	gameflow.mGamePrefs.mHasSaveGame      = true;
	gameflow.mGamePrefs.mMemCardSaveIndex = info.mMemCardSaveIndex + 1;
	gameflow.mWorldClock.mCurrentDay      = 1; // CardSelectSetupSection sets it before the load
	gameflow.mMemoryCard.loadCurrentGame();
}

Snapshot capture()
{
	Snapshot s;
	PlayerState& p = *playerState;
	PlayState& ps  = gameflow.mPlayState;
	s.day          = gameflow.mWorldClock.mCurrentDay;
	s.savedDay     = ps.mSavedDay;
	s.saveSlot     = ps.mSaveSlot;
	s.shipParts    = ps.mShipPartsCount;
	s.red          = ps.mRedPikiCount;
	s.yellow       = ps.mYellowPikiCount;
	s.blue         = ps.mBluePikiCount;
	s.courseOpen   = ps.mCourseOpenFlags;
	s.currParts     = p.mCurrParts;
	s.requiredParts = p.mRequiredUfoPartCount;
	s.containerFlag = p.mContainerFlag;
	s.upgradeLevel  = p.mShipUpgradeLevel;
	s.shipEffect    = p.mShipEffectPartFlag;
	s.displayPiki   = p.mDisplayPikiFlag;
	s.sprouted      = p.mSproutedNum;
	s.lostBattle    = p.mLostBattlePikis;
	s.leftBehind    = p.mLeftBehindPikis;
	s.plucked       = p.mTotalPluckedPikiCount;
	s.dead          = p.mTotalDeadPikiNum;
	s.born          = p.mTotalBornPikiNum;
	for (int i = 0; i < STAGE_COUNT; i++) s.stageParts[i] = p.mStagePartsCollected[i];
	for (int i = 0; i < MAX_DAYS; i++) {
		s.partsByDay[i]  = p.mPartsCollectedByDay[i];
		s.partsToNext[i] = p.mPartsToNextByDay[i];
	}
	for (int c = 0; c < 3; c++)
		for (int h = 0; h < 3; h++) s.onion[c][h] = pikiInfMgr.mPikiCounts[c][h];
	s.demoFlags   = serialise(p.mDemoFlags);
	s.resultFlags = serialise(p.mResultFlags);
	s.permadeath  = pc_permadeath_active();
	s.hard        = pc_hardmode_active();
	return s;
}

void printSnapshot(const char* scenario, const char* label, const Snapshot& s)
{
	std::printf("[scenario] %s: %s: day %d (saved %d, slot %d), parts %d (required %d, card count %d), containers %#x, upgrade %d, "
	            "Pikmin red %d yellow %d blue %d, Onion blue %d red %d yellow %d, dead %d born %d, permadeath %d hard %d\n",
	            scenario, label, s.day, s.savedDay, s.saveSlot, s.currParts, s.requiredParts, s.shipParts, s.containerFlag, s.upgradeLevel,
	            s.red, s.yellow, s.blue, s.onion[Blue][0] + s.onion[Blue][1] + s.onion[Blue][2], s.onion[Red][0] + s.onion[Red][1] + s.onion[Red][2],
	            s.onion[Yellow][0] + s.onion[Yellow][1] + s.onion[Yellow][2], s.dead, s.born, (int)s.permadeath, (int)s.hard);
}

bool compareSnapshots(unsigned tick, const char* scenario, const Snapshot& want, const Snapshot& got)
{
	struct Field {
		const char* name;
		int want, got;
	};
	const Field fields[] = {
		{ "day", want.day, got.day },
		{ "PlayState saved day", want.savedDay, got.savedDay },
		{ "PlayState save slot", want.saveSlot, got.saveSlot },
		{ "PlayState ship parts", want.shipParts, got.shipParts },
		{ "PlayState red Pikmin", want.red, got.red },
		{ "PlayState yellow Pikmin", want.yellow, got.yellow },
		{ "PlayState blue Pikmin", want.blue, got.blue },
		{ "PlayState open courses", want.courseOpen, got.courseOpen },
		{ "mCurrParts", want.currParts, got.currParts },
		{ "mRequiredUfoPartCount", want.requiredParts, got.requiredParts },
		{ "mContainerFlag", want.containerFlag, got.containerFlag },
		{ "mShipUpgradeLevel", want.upgradeLevel, got.upgradeLevel },
		{ "mShipEffectPartFlag", want.shipEffect, got.shipEffect },
		{ "mDisplayPikiFlag", want.displayPiki, got.displayPiki },
		{ "mSproutedNum", want.sprouted, got.sprouted },
		{ "mLostBattlePikis", want.lostBattle, got.lostBattle },
		{ "mLeftBehindPikis", want.leftBehind, got.leftBehind },
		{ "mTotalPluckedPikiCount", want.plucked, got.plucked },
		{ "mTotalDeadPikiNum", want.dead, got.dead },
		{ "mTotalBornPikiNum", want.born, got.born },
		{ "permadeath", want.permadeath, got.permadeath },
		{ "Hard", want.hard, got.hard },
	};
	for (const Field& f : fields) {
		if (f.want != f.got) {
			scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: %s saved %d, loaded %d", scenario, f.name, f.want,
			              f.got);
			return false;
		}
	}
	for (int i = 0; i < STAGE_COUNT; i++) {
		if (want.stageParts[i] != got.stageParts[i]) {
			scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: mStagePartsCollected[%d] saved %d, loaded %d",
			              scenario, i, want.stageParts[i], got.stageParts[i]);
			return false;
		}
	}
	for (int i = 0; i < MAX_DAYS; i++) {
		if (want.partsByDay[i] != got.partsByDay[i] || want.partsToNext[i] != got.partsToNext[i]) {
			scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: day %d parts %d/%d saved, %d/%d loaded", scenario,
			              i, want.partsByDay[i], want.partsToNext[i], got.partsByDay[i], got.partsToNext[i]);
			return false;
		}
	}
	for (int c = 0; c < 3; c++) {
		for (int h = 0; h < 3; h++) {
			if (want.onion[c][h] != got.onion[c][h]) {
				scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: Onion Pikmin colour %d stage %d saved %d, loaded %d",
				              scenario, c, h, want.onion[c][h], got.onion[c][h]);
				return false;
			}
		}
	}
	if (want.demoFlags != got.demoFlags) {
		scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: DemoFlags differ (%zu bytes saved, %zu loaded)", scenario,
		              want.demoFlags.size(), got.demoFlags.size());
		return false;
	}
	if (want.resultFlags != got.resultFlags) {
		scenarioFailf(tick, "the loaded game equals the saved one, field by field", "%s: ResultFlags differ (%zu bytes saved, %zu loaded)",
		              scenario, want.resultFlags.size(), got.resultFlags.size());
		return false;
	}
	return true;
}

bool spawnPiki(Navi* navi, int colour, int mode, int slot)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (!piki) {
		scenarioFailf(0, "a Pikmin is born", "birth returned null");
		return false;
	}
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at = navi->mSRT.t;
	at.x += 45.0f * (float)slot;
	at.z += 45.0f;
	at.y = mapMgr->getMinY(at.x, at.z, true);
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(mode, navi);
	return true;
}

void clockToSunset(unsigned ticks)
{
	WorldClock& clock         = gameflow.mWorldClock;
	const float hoursPerTick  = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
	clock.setTime(gameflow.mParameters->mEndHour() - ticks * hoursPerTick);
}

void ResultsDriver::update(unsigned tick)
{
	mButton = 0;
	mStickY = 0;
	int nitaku = 0;
	const bool choice = pc_scenario_note_take("nitaku", &nitaku);
	if (choice) {
		// ogNitakuMgr: Status_3 (3) takes input; value = status * 2 + (cursor on Yes).
		if (nitaku / 2 != 3 || tick < mLastPress + 2) return;
		const bool onYes   = (nitaku & 1) != 0;
		const Answer want  = mNext < mPlan.size() ? mPlan[mNext] : Yes;
		if (want == Cancel) {
			mButton = PAD_BUTTON_B;
		} else if ((want == Yes) != onYes) {
			mStickY = -70; // MSTICK_DOWN toggles the cursor
		} else {
			mButton = PAD_BUTTON_A;
		}
		if (mButton) mNext++;
		mLastPress = tick;
		return;
	}
	if (tick % 6 == 0) mButton = PAD_BUTTON_A;
}

void ResultsDriver::fill(PcInputPad& pad) const
{
	pad.button |= mButton;
	if (mStickY) pad.stickY = mStickY;
}

std::string noteHistory(const char* key)
{
	std::string out;
	const int n = pc_scenario_note_count(key);
	for (int i = 0; i < n; i++) out += (i ? " " : "") + std::to_string(pc_scenario_note_at(key, i));
	return out;
}

bool noteSeen(const char* key, int value)
{
	const int n = pc_scenario_note_count(key);
	for (int i = 0; i < n; i++)
		if (pc_scenario_note_at(key, i) == value) return true;
	return false;
}

void DayEndResults::providePads(unsigned t, PcInputPad& pad)
{
	if (mPhase == Results) {
		mDriver.fill(pad);
	} else if (gameflow.mIsUIOverlayActive && t % 15 == 0) {
		pad.button |= PAD_BUTTON_A; // the first-noon text
	}
}

void DayEndResults::onTick(unsigned t)
{
	const unsigned rate = unsigned(pc_scenario_ticks_per_second());
	WorldClock& clock   = gameflow.mWorldClock;
	const int day       = clock.mCurrentDay;
	switch (mPhase) {
	case Settle: {
		if ((gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive) || gameflow.mIsUIOverlayActive) return;
		Navi* navi = naviMgr->getNavi();
		if (!itemMgr->getContainer(Blue) || itemMgr->getContainer(Red) || gameflow.mIsDayEndActive) {
			scenarioFailf(t, "the object stage (a blue Onion, no red one) before sunset", "%s: blue %d red %d day end %d", mName,
			              (int)(itemMgr->getContainer(Blue) != nullptr), (int)(itemMgr->getContainer(Red) != nullptr), (int)gameflow.mIsDayEndActive);
			return;
		}
		pc_settings_scenario_set_infinite_day(0);
		mStartDay = day;
		if (!spawnPiki(navi, Blue, PikiMode::FormationMode, -1) || !spawnPiki(navi, Blue, PikiMode::FreeMode, 0)
		    || !spawnPiki(navi, Red, PikiMode::FormationMode, 1))
			return;
		GameStat::update();
		if (!setup(t)) return;
		clockToSunset(60);
		mPhase     = ToSunset;
		mPhaseTick = t;
		return;
	}
	case ToSunset:
		if (gameflow.mIsDayEndActive) {
			mPhase     = ToAdvance;
			mPhaseTick = t;
		} else if (t - mPhaseTick > 15 * rate) {
			scenarioFailf(t, "the day end within 15 s of the clock jump", "%s: %.3f h", mName, clock.mTimeOfDay);
		}
		return;
	case ToAdvance:
		if (day == mStartDay + 1) {
			std::printf("[scenario] %s: day %d at t=%u: results window opens\n", mName, day, t);
			pc_scenario_note_clear("savemgr");
			pc_scenario_note_clear("memchk");
			pc_scenario_note_clear("nitaku");
			mResultsTick = t;
			mPhase       = Results;
			mPhaseTick   = t;
		} else if (t - mPhaseTick > 15 * rate) {
			scenarioFailf(t, "the day advances within 15 s of the day end", "%s: day %d", mName, day);
		}
		return;
	case Results:
		if (day != mStartDay + 1) {
			scenarioFailf(t, "the day advances once", "%s: day %d", mName, day);
			return;
		}
		if (gsys->mSoftResetPending) {
			const int next = gameflow.mNextOnePlayerSectionID;
			std::printf("[scenario] %s: soft reset at t=%u (%u ticks after the results opened), next section %d; save manager %s\n", mName, t,
			            t - mResultsTick, next, noteHistory("savemgr").c_str());
			mPhase = Done;
			onTerminal(t, next);
			return;
		}
		mDriver.update(t);
		onResults(t);
		if (t - mPhaseTick > 120 * rate) {
			scenarioFailf(t, "the soft reset within 120 s of the results opening", "%s: none; save manager %s, card check %s", mName,
			              noteHistory("savemgr").c_str(), noteHistory("memchk").c_str());
		}
		return;
	case Done: return;
	}
}

} // namespace scen_save

#endif
