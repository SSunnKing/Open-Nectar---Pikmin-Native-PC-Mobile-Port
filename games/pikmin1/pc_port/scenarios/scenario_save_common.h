#pragma once
// Shared pieces of the save, load, results-exit, permadeath and memory-card
// scenarios (coverage/terminals.md T13, T14, T15, T20, T21):
//  - the scratch save directory as a file list with content hashes;
//  - the card file reloaded from disk the way the file select does, and the
//    reset CardSelectSection makes before it loads a slot;
//  - a field-by-field snapshot of what a save carries;
//  - a pad driver for the results screen and its save menus, which answers
//    each Yes/No choice from a plan and presses A everywhere else.
#if PIKI_SCENARIOS

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "GlobalGameOptions.h"
#include "MemoryCard.h"
#include "netplay/pc_input_log.h"
#include "scenarios/pc_scenario.h"

class Navi;

namespace scen_save {

// ---- the scratch save directory ----

// NECTAR_SAVE_DIR, which the runner points at its scratch folder.
std::string saveDir();
// Every file under saveDir(), relative path -> FNV-1a 64 of its bytes. A
// directory appears as "<path>/" with hash 0, so an emptied folder still shows.
using FileMap = std::map<std::string, uint64_t>;
FileMap listSaveDir();
// "" when equal, else "added a, b; removed c; changed d".
std::string diffFiles(const FileMap& before, const FileMap& after);
void printFiles(const char* scenario, const char* label, const FileMap& files);
// card0/"Pikmin dataFile", empty if missing.
std::vector<uint8_t> readCardFile();

// ---- the card, through the game's own MemoryCard ----

// Waits for the card thread (CardSelect does the same before it reads).
void waitCardIdle();
// Clears the in-memory card image, reads the file back from disk
// (getMemoryCardState(true)), checks it is not broken and lists the slots.
// Fails the scenario with a reason and returns false otherwise.
bool reloadCard(unsigned tick, const char* scenario, CardQuickInfo infos[4]);
// Creates the card file the way the memory-card check does when there is none
// (MemoryCard::makeDefaultFile), and reloads it.
bool createCardFile(unsigned tick, const char* scenario);
// Saves the game in memory to a slot through MemoryCard::saveCurrentGame, as the
// results screen does after its slot choice (spare block from getQuickInfos).
bool saveToSlot(unsigned tick, const char* scenario, int slot);
// Sets the run's permadeath and Hard rules as a new file would get them
// (pending, then begin_new_run).
void setRunRules(bool permadeath, bool hard);
// What the file select records when a slot is chosen (cardSelect.cpp: the
// slot, its block as mMemCardSaveIndex, its checksum, mHasSaveGame), without the
// load: the run in memory now plays that slot, as if it had been loaded.
void pickSlot(const CardQuickInfo& info, int slot);
// CardSelectSection's reset before a slot is loaded, then MemoryCard::loadCurrentGame
// for the slot's newest block (mMemCardSaveIndex), as the file select does.
void loadSlot(const CardQuickInfo& info);

// ---- what a save carries ----

struct Snapshot {
	int day = 0;
	// PlayState, as writeCurrentGame fills it.
	int savedDay = 0, saveSlot = 0, shipParts = 0, red = 0, yellow = 0, blue = 0, courseOpen = 0;
	// PlayerState::saveCard.
	int currParts = 0, requiredParts = 0, containerFlag = 0, upgradeLevel = 0, shipEffect = 0, displayPiki = 0;
	int sprouted = 0, lostBattle = 0, leftBehind = 0, plucked = 0, dead = 0, born = 0;
	int stageParts[STAGE_COUNT] = {};
	int partsByDay[MAX_DAYS] = {}, partsToNext[MAX_DAYS] = {};
	// Pikmin in the Onions (PikiInfMgr), per colour and stage of growth.
	int onion[3][3] = {};
	// DemoFlags and ResultFlags as their own saveCard writes them.
	std::vector<uint8_t> demoFlags, resultFlags;
	bool permadeath = false, hard = false;
};
Snapshot capture();
void printSnapshot(const char* scenario, const char* label, const Snapshot& s);
// Every field equal, else fails the scenario naming the first field that differs.
bool compareSnapshots(unsigned tick, const char* scenario, const Snapshot& want, const Snapshot& got);

// ---- the day end and the results screen ----

// Born, counted and given a mode as the game's own spawners do (day_end.cpp).
bool spawnPiki(Navi* navi, int colour, int mode, int slot);
// Moves the clock this many open ticks before the end hour (day_end.cpp).
void clockToSunset(unsigned ticks);

// Pad driver for the results screen. Each Yes/No choice that reaches its input
// state (ogNitakuMgr Status_3) takes the next answer of the plan; with the plan
// used up it answers Yes. Everywhere else A is pressed every 6 ticks, as
// captain_death and day_end do (the diary page, the results, the card prompts,
// the slot screen, whose cursor starts on slot A).
enum Answer { Yes, No, Cancel };
class ResultsDriver {
public:
	void setPlan(const std::vector<Answer>& plan) { mPlan = plan; mNext = 0; }
	size_t answered() const { return mNext; }
	// Call from onTick with the tick; the pad goes out on the next providePads.
	void update(unsigned tick);
	void fill(PcInputPad& pad) const;

private:
	std::vector<Answer> mPlan;
	size_t mNext     = 0;
	uint16_t mButton = 0;
	int8_t mStickY   = 0;
	unsigned mLastPress = 0;
};

// The notes under key (pc_scenario_note.h) since the last clear, as text.
std::string noteHistory(const char* key);
bool noteSeen(const char* key, int value);

// A sunset day end on the object stage that stops at the results screen and
// hands it to a ResultsDriver (day_end's set-up: Day 2, Infinite Day off, the
// clock 60 open ticks before the end hour, the first-noon text closed with A).
// Two blue Pikmin (Formation, Free) and one red are spawned; at the day end the
// blue ones enter the blue Onion and the red one dies (T03).
class DayEndResults : public PcScenario {
public:
	explicit DayEndResults(const char* name)
	    : mName(name)
	{
	}
	const char* name() const override { return mName; }
	int startDay() const override { return 2; }
	void providePads(unsigned tick, PcInputPad& pad) override;
	void onTick(unsigned tick) override;

protected:
	// Once, before the clock jump. Return false after a scenarioFail.
	virtual bool setup(unsigned /*tick*/) { return true; }
	// Every tick on the results screen, after the driver.
	virtual void onResults(unsigned /*tick*/) { }
	// The soft reset is pending toward next. Report the verdict.
	virtual void onTerminal(unsigned tick, int next) = 0;

	const char* mName;
	ResultsDriver mDriver;
	unsigned mResultsTick = 0;

private:
	enum Phase { Settle, ToSunset, ToAdvance, Results, Done };
	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0;
	int mStartDay       = 0;
};

} // namespace scen_save

#endif
