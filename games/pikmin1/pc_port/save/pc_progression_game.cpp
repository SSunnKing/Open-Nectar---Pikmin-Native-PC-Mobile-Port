#include "pc_progression_game.h"

#include "pc_progression_backup.h"
#include "pc_progression_saved.h"

#include <SDL.h>
#include <cstdio>
#include <string>
#include <vector>

#include "AIConstant.h"
#include "BaseInf.h"
#include "Demo.h"
#include "FlowController.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "SimpleAI.h"
#include "gameflow.h"
#include "pc_coop.h"
#include "pc_speedrun.h"
#include "mods/pc_mission.h"

std::string pc_card_root_dir(); // card_stubs.cpp: the card in use (normal, Randomizer, ...)
bool pc_unlock_all_stages();    // playerState.cpp
bool pc_netplay_session_active(void);
bool pc_randomizer_active(void);

// Named one by one: the game's own Red, Yellow and Blue (PikiColor) must stay unambiguous.
using pc::progression::Action;
using pc::progression::ActionExecutor;
using pc::progression::buildSnapshot;
using pc::progression::describeApplied;
using pc::progression::Disposition;
using pc::progression::isFactual;
using pc::progression::kColours;
using pc::progression::kMaxParts;
using pc::progression::Kind;
using pc::progression::Mode;
using pc::progression::partIndexOf;
using pc::progression::PartState;
using pc::progression::PendingRepair;
using pc::progression::repairToFixedPoint;
using pc::progression::RepairResult;
using pc::progression::SavedColour;
using pc::progression::SavedProgression;
using pc::progression::Source;
using pc::progression::WorldColour;
using pc::progression::WorldInventory;

namespace {

PendingRepair sPending;
std::vector<Action> sApplied;     // repairs made since this save was loaded
std::vector<std::string> sNote;   // note lines for the screen
std::vector<const char*> sNotePtr;
Uint32 sNoteStart = 0;
bool sNoteArmed   = false;
constexpr Uint32 kNoteMs = 9000;

Mode currentMode()
{
	if (pc_netplay_session_active()) return Mode::Netplay;
	if (pc_speedrun_active()) return Mode::Speedrun;
	if (pc_vs_active()) return Mode::Versus;
	if (pc_mission_active()) return Mode::Mission;
	if (gameflow.mIsChallengeMode || (playerState && playerState->isChallengeMode())) return Mode::Challenge;
	if (pc_unlock_all_stages()) return Mode::Debug;
	if (pc_coop_active()) return Mode::CoopStory;
	return Mode::Story;
}

int demoDiscover(int colour)
{
	return colour == Red ? DEMOFLAG_DiscoverRedOnyon : colour == Yellow ? DEMOFLAG_DiscoverYellowOnyon : DEMOFLAG_DiscoverBlueOnyon;
}
int demoPluck(int colour)
{
	return colour == Red ? DEMOFLAG_PluckRedPikmin : colour == Yellow ? DEMOFLAG_PluckYellowPikmin : DEMOFLAG_PluckBluePikmin;
}

// Saved sprouts of every stage except `skip` (the loaded stage's copy is consumed when it is built).
int offStageSprouts(int colour, StageInfo* skip)
{
	int total       = 0;
	StageInfo* node = static_cast<StageInfo*>(flowCont.mStageList.mChild);
	for (int i = 0; i < STAGE_COUNT && node; ++i) {
		if (node != skip) total += node->mStageInf.mBPikiInfMgr.getPikiCount(colour);
		node = static_cast<StageInfo*>(node->mNext);
	}
	return total;
}

// The persisted progression of the loaded save. `ids` is true once the ship has registered its parts.
SavedProgression captureSaved(bool ids, StageInfo* loadedStage)
{
	SavedProgression s;
	s.mode             = currentMode();
	s.prov.lineage     = std::uint64_t(gameflow.mPlayState.mSaveSlot);
	s.prov.syntheticDemoFlags = pc_unlock_all_stages() || gameflow.mIsChallengeMode;
	s.randomizer.active = pc_randomizer_active();
	s.day              = gameflow.mWorldClock.mCurrentDay;
	s.dayMatchesClock  = gameflow.mWorldClock.mCurrentDay == gameflow.mPlayState.mSavedDay;
	s.areaParts        = { 1, AICONST._154(), AICONST._164(), AICONST._174() };
	s.perfectParts     = AICONST._184();

	s.partsKnown   = playerState->mPcLoadedPartEntries == playerState->mTotalParts && playerState->mTotalParts == kMaxParts;
	s.partIdsKnown = false;
	if (s.partsKnown && ids && playerState->mTotalRegisteredParts == kMaxParts) {
		bool mapped = true;
		std::array<bool, kMaxParts> seen {};
		for (int i = 0; i < kMaxParts; ++i) {
			const int index = partIndexOf(playerState->mUfoParts[i].mModelID);
			if (index < 0 || seen[index]) {
				mapped = false;
				break;
			}
			seen[index]      = true;
			s.partVis[index] = playerState->mUfoParts[i].mPartVisType;
		}
		s.partIdsKnown = mapped;
	}
	if (!s.partIdsKnown) {
		// Slot order only: enough for the part count, the ship level and the area bits.
		for (int i = 0; i < kMaxParts && i < playerState->mTotalParts; ++i) s.partVis[i] = playerState->mUfoParts[i].mPartVisType;
	}
	s.currParts     = playerState->mCurrParts;
	s.requiredParts = playerState->mRequiredUfoPartCount;
	s.upgradeLevel  = playerState->mShipUpgradeLevel;
	s.shipEffect    = playerState->mShipEffectPartFlag;
	s.courseOpen    = std::uint8_t(gameflow.mPlayState.mCourseOpenFlags);
	s.tutorialMode  = playerState->mIsTutorialMode;
	s.collectEngine = playerState->mDemoFlags.isFlag(DEMOFLAG_CollectEngine);
	s.endDayActive  = playerState->inDayEnd();

	for (int c = 0; c < kColours; ++c) {
		SavedColour& sc = s.colours[c];
		sc.unlocked     = playerState->hasContainer(c);
		sc.booted       = playerState->hasBootContainer(c);
		sc.displayed    = playerState->displayPikiCount(c);
		sc.discovered   = playerState->mDemoFlags.isFlag(demoDiscover(c));
		sc.plucked      = playerState->mDemoFlags.isFlag(demoPluck(c));
		sc.stored       = pikiInfMgr.getColorTotal(c);
		sc.offStage     = offStageSprouts(c, loadedStage);
	}
	return s;
}

// The built stage: live Pikmin and seeds, Onions, and whether a seed can be allocated.
// Part sources, walls, bridges and boxes are not reported, so the planner defers anything
// that would need them (it never spawns what it cannot prove missing).
WorldInventory captureWorld()
{
	WorldInventory w;
	w.sourceInventoryComplete = false;
	w.catalogueCompatible     = false;
	for (PartState& p : w.parts) p.source = Source::Unknown;

	Iterator pikis(pikiMgr);
	CI_LOOP(pikis)
	{
		Piki* piki = static_cast<Piki*>(*pikis);
		if (piki && piki->isAlive() && piki->mColor >= PikiMinColor && piki->mColor <= PikiMaxColor) w.colours[piki->mColor].live++;
	}
	Iterator seeds(itemMgr->getPikiHeadMgr());
	CI_LOOP(seeds)
	{
		PikiHeadItem* seed = static_cast<PikiHeadItem*>(*seeds);
		if (seed && seed->mSeedColor >= PikiMinColor && seed->mSeedColor <= PikiMaxColor) w.colours[seed->mSeedColor].sprouts++;
	}
	PikiHeadMgr* seedMgr = itemMgr->getPikiHeadMgr();
	w.seedCapacity       = seedMgr && seedMgr->getSize() < seedMgr->getMax();

	for (int c = 0; c < kColours; ++c) {
		GoalItem* goal             = itemMgr->getContainer(c);
		WorldColour& wc            = w.colours[c];
		wc.onionPresent            = goal != nullptr;
		wc.pending                 = goal && goal->mSAICtx.mCurrAnimId > 0 ? goal->mSAICtx.mCurrAnimId : 0;
		// finalSetup's own rule, minus the unlock: once a colour is unlocked and nobody of it is left,
		// the normal extinction movie gives it a seed.
		wc.bonusPathOpen = playerState->mHasExtinctionDemoPlayed == false && !playerState->isTutorial() && !pc_vs_active()
		                  && GameStat::allPikis[c] == 0;
		// The stage's Onion catalogue is not available here, so a missing Onion object is reported, not recreated.
		wc.onionExpected = false;
	}
	return w;
}

// Factual assignments, written into the live PlayerState exactly like the loader does.
void applyFactual(const Action& a)
{
	switch (a.kind) {
	case Kind::SetUnlocked: playerState->setContainer(a.colour); break;
	case Kind::SetBooted: playerState->setBootContainer(a.colour); break;
	case Kind::SetDisplayed: playerState->setDisplayPikiCount(a.colour); break;
	case Kind::SetDiscovered: playerState->mDemoFlags.setFlagOnly(demoDiscover(a.colour)); break;
	case Kind::SetPlucked: playerState->mDemoFlags.setFlagOnly(demoPluck(a.colour)); break;
	case Kind::SetCourseOpen: SET_STAGE_OPEN(gameflow.mPlayState.mCourseOpenFlags, a.value); break;
	case Kind::SetUpgradeLevel: playerState->mShipUpgradeLevel = u8(a.value); break;
	case Kind::SetShipEffectBits: playerState->mShipEffectPartFlag |= u8(a.value); break;
	case Kind::SetCurrParts: playerState->mCurrParts = a.value; break;
	case Kind::SetRequiredParts: playerState->mRequiredUfoPartCount = a.value; break;
	case Kind::SetTutorialComplete: playerState->mIsTutorialMode = false; break;
	case Kind::ClearPrematureEngineFlag: playerState->mDemoFlags.resetFlag(DEMOFLAG_CollectEngine); break;
	default: break;
	}
}

struct GameExecutor : ActionExecutor {
	bool execute(const Action& a) override
	{
		// A boot flag set after the stage was built would leave its Onion in the dormant pre-boot state.
		// Rebuilding that is not done in the game, so it stays deferred and nothing builds on it.
		if (a.kind == Kind::SetBooted) return false;
		if (isFactual(a.kind)) {
			applyFactual(a);
			return true;
		}
		if (a.kind != Kind::ReplaceSeed) return false; // nothing else is reported by the game, so nothing else is asked of it
		// The Onion of a tutorial stage is still in its boot animation: leave that to the tutorial.
		if (playerState->isTutorial()) return false;
		GoalItem* goal = itemMgr->getContainer(a.colour);
		PikiHeadMgr* seedMgr = itemMgr->getPikiHeadMgr();
		if (!goal || !seedMgr || seedMgr->getSize() >= seedMgr->getMax()) return false;
		// Only an Onion that is open (Wait) can play the emit animation. A dormant one cannot.
		if (!goal->mSAICtx.mStateMachine || C_SAI(goal)->getCurrID(goal) != GoalAI::GOAL_Wait) return false;
		// The same call the normal extinction seed uses: the Onion births one seed and plays its emit animation.
		goal->mSAICtx.mCurrAnimId++;
		goal->emitPiki();
		return true;
	}
};

void record(const RepairResult& r, const char* phase)
{
	for (const Action& a : r.applied) sApplied.push_back(a);
	for (const Action& a : r.residual.actions) {
		if (a.disposition == Disposition::Repair) continue;
		std::printf("[Repair] %s: %s scenario S%02d reason %d\n", phase, a.disposition == Disposition::Defer ? "deferred" : "not repaired", a.scenario, int(a.reason));
	}
	if (!r.applied.empty()) std::printf("[Repair] %s: %d change(s) applied\n", phase, int(r.applied.size()));
}

void armNote()
{
	if (sApplied.empty() || sNoteArmed) return;
	sNote = describeApplied(sApplied);
	sNotePtr.clear();
	for (const std::string& line : sNote) sNotePtr.push_back(line.c_str());
	sNoteStart = SDL_GetTicks();
	sNoteArmed = !sNote.empty();
	sApplied.clear(); // shown once; a later repair gets its own note
}

} // namespace

void pc_progression_note_loaded_image(const void* data, std::size_t size)
{
	sPending.rememberLoadedImage(data, size);
	sApplied.clear();
	sNote.clear();
	sNotePtr.clear();
	sNoteArmed = false;
}

void pc_progression_global_phase(void)
{
	if (!playerState || gameflow.mPlayState.mSaveStatus != PlayState::ReadyToSave) return;
	const SavedProgression saved = captureSaved(false, nullptr);
	GameExecutor executor;
	const RepairResult r = repairToFixedPoint(buildSnapshot(saved, nullptr), &executor);
	record(r, "load");
	if (!r.applied.empty()) sPending.markRepaired(gameflow.mPlayState.mSaveSlot, gameflow.mPlayState.mSavedDay);
}

void pc_progression_stage_phase(void)
{
	if (!playerState || !itemMgr || !pikiMgr || gameflow.mPlayState.mSaveStatus != PlayState::ReadyToSave) return;
	const SavedProgression saved = captureSaved(true, flowCont.mCurrentStage);
	const WorldInventory world   = captureWorld();
	GameExecutor executor;
	const RepairResult r = repairToFixedPoint(buildSnapshot(saved, &world), &executor);
	record(r, "stage");
	if (!r.applied.empty()) sPending.markRepaired(gameflow.mPlayState.mSaveSlot, gameflow.mPlayState.mSavedDay);
	armNote();
}

bool pc_progression_before_save(void)
{
	std::string error;
	if (sPending.beforeSave(pc_card_root_dir(), &error)) return true;
	std::printf("[Repair] the backup could not be made (%s): the save is refused so the original stays\n", error.c_str());
	return false;
}

void pc_progression_after_save(bool succeeded, const void* savedImage, std::size_t size)
{
	sPending.afterSave(succeeded);
	// The file on the card is now the one just written: a later repair must back up that, not the file loaded earlier.
	if (succeeded) sPending.replaceImage(savedImage, size);
}

void pc_progression_reset(void)
{
	sPending.reset();
	sApplied.clear();
	sNote.clear();
	sNotePtr.clear();
	sNoteArmed = false;
}

const char* const* pc_progression_note_lines(int* count, float* ageSeconds)
{
	if (!sNoteArmed) return nullptr;
	const Uint32 age = SDL_GetTicks() - sNoteStart;
	if (age >= kNoteMs) {
		sNoteArmed = false;
		return nullptr;
	}
	if (count) *count = int(sNotePtr.size());
	if (ageSeconds) *ageSeconds = age / 1000.0f;
	return sNotePtr.data();
}
