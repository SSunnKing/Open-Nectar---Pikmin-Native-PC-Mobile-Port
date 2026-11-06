// ship_parts: the ship-part upgrade chain (coverage/milestones.md SP1, SP2, SP3,
// SP4, SP5-SP11, SP13). Drives PlayerState::getUfoParts(id, true) directly, one
// part per tick, all 30 ids, and after every part checks what the game code did:
//  - SP1: mCurrParts counts, the part's mPartVisType is Invisible (isInvisible
//    = true), mCurrentRepairingPart is cleared, mStagePartsCollected[stage]
//    counts per stage;
//  - SP2: mRequiredUfoPartCount skips the five optional parts (Nova Blaster,
//    Space Float, Massage Machine, Secret Safe, UV Lamp), happyEndable() turns
//    true at 25 REQUIRED parts (the 26th part here, as an optional one was taken
//    early), which is not the same as the 25th part;
//  - SP3, SP4: the radar and the two Ionium Jets set bits 1, 2, 4 of
//    mShipEffectPartFlag and keep the initial bit 8;
//  - SP5-SP9: mShipUpgradeLevel and the stage unlocks (Forest at the first
//    part, Forest Navel at AICONST._154, Distant Spring at _164, Final Trial at
//    _174, perfect at _184) are checked against the live AICONST values, never
//    literals: the disc and the stand-in tree both set p19 = 5 (standin-aiconst.md),
//    the code default is 6;
//  - SP10: the result flags (UnlockCave, UnlockYakushima, Collect15Parts,
//    Collect25Parts, Collect29Parts, Collect10Parts) in their on (1) and seen (2)
//    states, with Collect10Parts needing the clock on day 10 or later;
//  - SP11: getNextPowerupNumber() is the number of parts left to the next level;
//  - SP13: a second getUfoParts of the same id changes nothing, right after the
//    first collection and again at the end for all 30.
//
// Stage id: the sandbox stage has id STAGE_TESTMAP (5), and getUfoParts does
// mStagePartsCollected[mStageID]++ on a u8[STAGE_COUNT = 5], a one-byte write
// past the array (into the byte after it, the start of mPerHourGraph). The
// scenario sets flowCont.mCurrentStage->mStageID to one of the five story stages
// around each call (part i goes to stage i % 5) and restores it, so the game code
// is not changed and the bug is not hit. The per-stage counts are then 6 each. A
// snapshot of the bytes that follow the array is compared at the end.
//
// Registration: findUfoParts only finds registered parts, and getUfoParts writes
// parts->mPartVisType on the invisible path, so an unregistered id would
// dereference null (it does not "avoid registration"). Parts register through
// pelMgr.bin and pelAnim.bin; the ids the tree has not registered are registered
// here with PlayerState::registerUfoParts (public), as the game does for each
// config of type UfoPart. The log says how many were already registered.
//
// Achievements: getUfoParts calls pc_achievements_on_ship_part, which would unlock
// the part achievements, save pikmin_achievements.txt in the working directory and
// queue a toast that draws the part's icon (screen/tex/imageNNN.bti, a file the
// stand-in tree does not have; which icons are asked for depends on wall-clock
// time). The scenario sets the scenario-only infinite-day setting, which is one of
// the reasons pc_achievements_blocked_reason() gives ("cheats are on"), so nothing
// unlocks, nothing is written and no icon is read. It is cleared before the verdict,
// and the scenario checks that no pikmin_achievements.txt appeared.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstring>

#include "AIConstant.h"
#include "FlowController.h"
#include "GlobalGameOptions.h"
#include "Pellet.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "zen/ogResult.h"

namespace {

constexpr int kParts = 30;

// Order: the story-ish required parts first, the radar and the jets early, an
// optional part (Nova Blaster) third, the other four optional parts last.
const u32 kOrder[kParts] = {
	UFOID_MainEngine,      UFOID_Bowsprit,         UFOID_NovaBlaster,      UFOID_GluonDrive,        UFOID_AntiDioxinFilter, UFOID_EternalFuelDynamo,
	UFOID_WhimsicalRadar,  UFOID_IoniumJet1,       UFOID_IoniumJet2,       UFOID_InterstellarRadio, UFOID_GuardSatellite,   UFOID_ChronosReactor,
	UFOID_RadiationCanopy, UFOID_GeigerCounter,    UFOID_Sagittarius,      UFOID_Libra,             UFOID_OmegaStabilizer,  UFOID_ShockAbsorber,
	UFOID_GravityJumper,   UFOID_PilotSeat,        UFOID_AutomaticGear,    UFOID_ZirconiumRotor,    UFOID_ExtraordinaryBolt, UFOID_RepairTypeBolt,
	UFOID_PositronGenerator, UFOID_AnalogComputer, UFOID_SpaceFloat,       UFOID_MassageMachine,    UFOID_SecretSafe,       UFOID_UVLamp,
};

bool isOptional(u32 id)
{
	return id == UFOID_NovaBlaster || id == UFOID_SpaceFloat || id == UFOID_MassageMachine || id == UFOID_SecretSafe || id == UFOID_UVLamp;
}

// ResultFlags::getFlag is protected: 0 clear, 1 on, 2 seen (setSeen overwrites on), 3 both.
struct ResultPeek : ResultFlags {
	static int get(ResultFlags& r, int flag)
	{
		u8 (ResultFlags::*p)(int) = &ResultPeek::getFlag;
		return (r.*p)(flag);
	}
};

int flagState(int flag) { return ResultPeek::get(playerState->mResultFlags, flag); }

const char* kAchievementFile = "pikmin_achievements.txt";

class ShipParts : public PcScenario {
public:
	const char* name() const override { return "ship_parts"; }

	void onStageReady() override
	{
		mHadAchievementFile = fileExists(kAchievementFile);
		pc_settings_scenario_set_infinite_day(1); // blocks achievements (see the header)

		PlayerState* ps = playerState;
		int already     = 0;
		for (int i = 0; i < ps->mTotalRegisteredParts; i++) already++;
		for (u32 id : kOrder) {
			if (registered(id)) continue;
			if (ps->mTotalRegisteredParts >= ps->mTotalParts) {
				scenarioFailf(0, "room to register every ship part", "%d registered of %d", ps->mTotalRegisteredParts, ps->mTotalParts);
				return;
			}
			ps->registerUfoParts(-1, id, id);
			mAddedByScenario++;
		}
		std::printf("[scenario] ship_parts: %d parts registered by the tree, %d registered by the scenario, total %d of %d\n", already, mAddedByScenario,
		            ps->mTotalRegisteredParts, ps->mTotalParts);

		mThr[0] = 1;
		mThr[1] = AICONST._154();
		mThr[2] = AICONST._164();
		mThr[3] = AICONST._174();
		mThr[4] = AICONST._184();
		std::printf("[scenario] ship_parts: live AICONST level thresholds p19 %d, p20 %d, p21 %d, p22 %d (levels 2-5); code defaults 6, 12, 29, 30\n", mThr[1], mThr[2],
		            mThr[3], mThr[4]);
		std::printf("[scenario] ship_parts: start state: level %d, parts %d, required %d, effect flags %d, stage ids Forest/Cave/Spring/Last open %d%d%d%d\n",
		            (int)ps->mShipUpgradeLevel, ps->mCurrParts, ps->mRequiredUfoPartCount, (int)ps->mShipEffectPartFlag, open(STAGE_Forest), open(STAGE_Cave),
		            open(STAGE_Yakushima), open(STAGE_Last));

		if (ps->mTotalParts != kParts || !(mThr[1] <= mThr[2] && mThr[2] <= mThr[3] && mThr[3] <= mThr[4] && mThr[4] <= kParts)) {
			scenarioFailf(0, "30 ship parts and ordered thresholds", "total %d, thresholds %d %d %d %d", ps->mTotalParts, mThr[1], mThr[2], mThr[3], mThr[4]);
			return;
		}
		if (ps->mCurrParts != 0 || ps->mRequiredUfoPartCount != 0 || ps->mShipUpgradeLevel != 0 || open(STAGE_Forest) || open(STAGE_Cave) || open(STAGE_Yakushima)
		    || open(STAGE_Last)) {
			scenarioFailf(0, "a fresh game: 0 parts, level 0, Forest/Cave/Spring/Last closed", "parts %d required %d level %d open %d%d%d%d", ps->mCurrParts,
			              ps->mRequiredUfoPartCount, (int)ps->mShipUpgradeLevel, open(STAGE_Forest), open(STAGE_Cave), open(STAGE_Yakushima), open(STAGE_Last));
			return;
		}
		for (int i = 0; i < kParts; i++) {
			if (ps->hasUfoParts(kOrder[i])) {
				scenarioFailf(0, "no part collected at the start", "part %d already collected", i);
				return;
			}
		}
		if (playerState->mShipEffectPartFlag != 8) {
			scenarioFailf(0, "mShipEffectPartFlag 8 at the start", "%d", (int)playerState->mShipEffectPartFlag);
			return;
		}
		if (ps->getNextPowerupNumber() != 1) {
			scenarioFailf(0, "getNextPowerupNumber 1 before any part", "%d", ps->getNextPowerupNumber());
			return;
		}
		mStartDay = gameflow.mWorldClock.mCurrentDay;
		std::memcpy(mAfterArray, afterArray(), sizeof(mAfterArray));
		mReady = true;
	}

	void onTick(unsigned t) override
	{
		if (mDone || !mReady) return;
		if (t < (unsigned)kParts) {
			collect((int)t, t);
			return;
		}
		finish(t);
	}

private:
	static bool fileExists(const char* path)
	{
		FILE* f = std::fopen(path, "rb");
		if (f) std::fclose(f);
		return f != nullptr;
	}

	static bool open(int stage) { return gameflow.mPlayState.isStageOpen(stage); }

	// The bytes right after mStagePartsCollected (where a stage id 5 would write).
	static const u8* afterArray() { return reinterpret_cast<const u8*>(&playerState->mStagePartsCollected[0]) + STAGE_COUNT; }

	static bool registered(u32 id)
	{
		for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
			if (playerState->mUfoParts[i].mModelID == id) return true;
		}
		return false;
	}

	static int visType(u32 id)
	{
		for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
			if (playerState->mUfoParts[i].mModelID == id) return playerState->mUfoParts[i].mPartVisType;
		}
		return -1;
	}

	// Level the game should be at with n parts, from the live thresholds.
	int levelFor(int n) const
	{
		if (n >= mThr[4]) return 5;
		if (n >= mThr[3]) return 4;
		if (n >= mThr[2]) return 3;
		if (n >= mThr[1]) return 2;
		return n >= 1 ? 1 : 0;
	}

	// Parts left to the next level at n parts: the first threshold above n, else 30.
	int nextFor(int n) const
	{
		for (int i = 0; i < 5; i++) {
			if (mThr[i] > n) return mThr[i] - n;
		}
		return kParts - n;
	}

	void call(u32 id, int stage)
	{
		const int saved = flowCont.mCurrentStage->mStageID;
		flowCont.mCurrentStage->mStageID = stage;
		playerState->getUfoParts(id, true);
		flowCont.mCurrentStage->mStageID = saved;
	}

	struct Snap {
		int parts, required, level, effect, vis, repairing, openMask, nextPower;
		int stageCount[STAGE_COUNT];
		int flags[6];
	};

	Snap snap(u32 id) const
	{
		Snap s;
		s.parts     = playerState->mCurrParts;
		s.required  = playerState->mRequiredUfoPartCount;
		s.level     = playerState->mShipUpgradeLevel;
		s.effect    = playerState->mShipEffectPartFlag;
		s.vis       = visType(id);
		s.repairing = playerState->mCurrentRepairingPart != nullptr;
		s.openMask  = open(STAGE_Forest) | open(STAGE_Cave) << 1 | open(STAGE_Yakushima) << 2 | open(STAGE_Last) << 3;
		s.nextPower = playerState->getNextPowerupNumber();
		for (int i = 0; i < STAGE_COUNT; i++) s.stageCount[i] = playerState->mStagePartsCollected[i];
		const int f[6] = { zen::RESFLAG_UnlockCave, zen::RESFLAG_UnlockYakushima, zen::RESFLAG_Collect15Parts, zen::RESFLAG_Collect25Parts, zen::RESFLAG_Collect29Parts,
			               zen::RESFLAG_Collect10Parts };
		for (int i = 0; i < 6; i++) s.flags[i] = flagState(f[i]);
		return s;
	}

	static bool same(const Snap& a, const Snap& b) { return std::memcmp(&a, &b, sizeof(Snap)) == 0; }

	void collect(int i, unsigned t)
	{
		const u32 id   = kOrder[i];
		const int n    = i + 1;
		const int stage = i % STAGE_COUNT;
		int required   = 0;
		for (int k = 0; k <= i; k++) required += isOptional(kOrder[k]) ? 0 : 1;

		// Collection number 13 happens on day 10 (Collect10Parts needs >= 11 parts and day >= 10); the others on the start day.
		const bool lateDay = n == 13;
		if (lateDay) gameflow.mWorldClock.mCurrentDay = 10;
		call(id, stage);
		if (lateDay) gameflow.mWorldClock.mCurrentDay = mStartDay;

		PlayerState* ps = playerState;
		char tag[64];
		std::snprintf(tag, sizeof(tag), "part %d (id %c%c%c%c)", n, char(id >> 24), char(id >> 16), char(id >> 8), char(id));
		auto bad = [&](const char* what, int expected, int observed) {
			scenarioFailf(t, what, "%s: expected %d, observed %d", tag, expected, observed);
			cleanup();
			mDone = true;
		};

		// SP1: counts and visibility.
		if (ps->mCurrParts != n) return bad("mCurrParts after the part", n, ps->mCurrParts);
		if (visType(id) != PARTVIS_Invisible) return bad("part visibility Invisible (2)", PARTVIS_Invisible, visType(id));
		if (!ps->hasUfoParts(id)) return bad("hasUfoParts true", 1, 0);
		if (ps->mCurrentRepairingPart != nullptr) return bad("mCurrentRepairingPart null for an invisible part", 0, 1);
		int collectedStages = 0;
		for (int s = 0; s < STAGE_COUNT; s++) {
			const int want = (n + STAGE_COUNT - 1 - s) / STAGE_COUNT; // parts 1..n go to stages 0,1,2,3,4,0,...
			if (ps->mStagePartsCollected[s] != want) return bad("mStagePartsCollected per stage", want, ps->mStagePartsCollected[s]);
			collectedStages += ps->mStagePartsCollected[s];
		}
		if (collectedStages != n) return bad("stage counts sum to the part count", n, collectedStages);

		// SP2: required vs optional.
		if (ps->mRequiredUfoPartCount != required) return bad("mRequiredUfoPartCount", required, ps->mRequiredUfoPartCount);
		if (ps->happyEndable() != (required >= MIN_HAPPY_END_PARTS)) return bad("happyEndable() at required >= 25", required >= MIN_HAPPY_END_PARTS, ps->happyEndable());
		if (required >= MIN_HAPPY_END_PARTS && !mHappyTick) mHappyTick = t, mHappyParts = n;

		// SP3, SP4: effect flags (bit 8 stays).
		int effect = 8;
		for (int k = 0; k <= i; k++) {
			if (kOrder[k] == UFOID_WhimsicalRadar) effect |= 1;
			if (kOrder[k] == UFOID_IoniumJet1) effect |= 2;
			if (kOrder[k] == UFOID_IoniumJet2) effect |= 4;
		}
		if (ps->mShipEffectPartFlag != effect) return bad("mShipEffectPartFlag", effect, ps->mShipEffectPartFlag);

		// SP5-SP9: level and unlocks.
		const int level = levelFor(n);
		if (ps->mShipUpgradeLevel != level) return bad("mShipUpgradeLevel", level, ps->mShipUpgradeLevel);
		if (level > mLastLevel) {
			std::printf("[scenario] ship_parts: level %d at part %d (tick %u)\n", level, n, t);
			mLevelTick[level] = t;
			mLastLevel         = level;
		}
		const int wantOpen = (n >= 1) | (n >= mThr[1]) << 1 | (n >= mThr[2]) << 2 | (n >= mThr[3]) << 3;
		const int gotOpen  = open(STAGE_Forest) | open(STAGE_Cave) << 1 | open(STAGE_Yakushima) << 2 | open(STAGE_Last) << 3;
		if (gotOpen != wantOpen) return bad("open stages mask (Forest, Cave, Spring, Last = bits 0-3)", wantOpen, gotOpen);
		// openStage queues a world-map animation for the stage just opened.
		if (n == 1 && gameflow.mPendingStageUnlockID != STAGE_Forest) return bad("mPendingStageUnlockID Forest at the first part", STAGE_Forest, gameflow.mPendingStageUnlockID);
		if (n == mThr[1] && gameflow.mPendingStageUnlockID != STAGE_Cave) return bad("mPendingStageUnlockID Cave at the level 2 part", STAGE_Cave, gameflow.mPendingStageUnlockID);
		if (n == mThr[2] && gameflow.mPendingStageUnlockID != STAGE_Yakushima) return bad("mPendingStageUnlockID Spring at the level 3 part", STAGE_Yakushima, gameflow.mPendingStageUnlockID);
		if (n == mThr[3] && gameflow.mPendingStageUnlockID != STAGE_Last) return bad("mPendingStageUnlockID Last at the level 4 part", STAGE_Last, gameflow.mPendingStageUnlockID);

		// SP10: result flags. 0 clear, 1 on, 2 seen.
		struct { int flag; int want; const char* name; } flags[] = {
			{ zen::RESFLAG_UnlockCave, n >= mThr[1] ? 1 : 0, "RESFLAG_UnlockCave" },
			{ zen::RESFLAG_UnlockYakushima, n >= mThr[2] ? 1 : 0, "RESFLAG_UnlockYakushima" },
			{ zen::RESFLAG_Collect15Parts, n >= 25 ? 2 : (n >= 15 ? 1 : 0), "RESFLAG_Collect15Parts" },
			{ zen::RESFLAG_Collect25Parts, n >= 29 ? 2 : (n >= 25 ? 1 : 0), "RESFLAG_Collect25Parts" },
			{ zen::RESFLAG_Collect29Parts, n >= 29 ? 1 : 0, "RESFLAG_Collect29Parts" },
			{ zen::RESFLAG_Collect10Parts, n >= 13 ? 1 : 0, "RESFLAG_Collect10Parts (>= 11 parts and day >= 10; day 10 from part 13)" },
		};
		for (auto& f : flags) {
			const int got = flagState(f.flag);
			if (got != f.want) return bad(f.name, f.want, got);
		}

		// SP11: parts left to the next level.
		if (ps->getNextPowerupNumber() != nextFor(n)) return bad("getNextPowerupNumber (parts to the next level)", nextFor(n), ps->getNextPowerupNumber());

		// SP13: a second collection of the same id changes nothing.
		const Snap before = snap(id);
		call(id, (stage + 1) % STAGE_COUNT); // a different stage: a counted duplicate would show in another stage
		const Snap again = snap(id);
		if (!same(before, again)) {
			return bad("duplicate collection changes nothing (parts)", before.parts, again.parts);
		}
	}

	void finish(unsigned t)
	{
		PlayerState* ps = playerState;
		// SP13 at the end: all 30 again, a stage id valid, nothing moves.
		const Snap before = snap(kOrder[kParts - 1]);
		for (int i = 0; i < kParts; i++) call(kOrder[i], i % STAGE_COUNT);
		const Snap after = snap(kOrder[kParts - 1]);
		if (!same(before, after)) {
			scenarioFailf(t, "all 30 duplicate collections change nothing", "parts %d -> %d, required %d -> %d", before.parts, after.parts, before.required, after.required);
			cleanup();
			return;
		}
		if (ps->mCurrParts != kParts || ps->mRequiredUfoPartCount != 25 || ps->mShipUpgradeLevel != 5 || !ps->happyEndable() || ps->getNextPowerupNumber() != 0) {
			scenarioFailf(t, "30 parts, 25 required, level 5, happyEndable, 0 to the next level", "parts %d, required %d, level %d, happyEndable %d, next %d", ps->mCurrParts,
			              ps->mRequiredUfoPartCount, (int)ps->mShipUpgradeLevel, (int)ps->happyEndable(), ps->getNextPowerupNumber());
			cleanup();
			return;
		}
		for (int s = 0; s < STAGE_COUNT; s++) {
			if (ps->mStagePartsCollected[s] != 6) {
				scenarioFailf(t, "6 parts counted in each of the 5 stages", "stage %d has %d", s, (int)ps->mStagePartsCollected[s]);
				cleanup();
				return;
			}
		}
		if (std::memcmp(mAfterArray, afterArray(), sizeof(mAfterArray)) != 0) {
			scenarioFailf(t, "the bytes after mStagePartsCollected unchanged (no out-of-bounds write)", "changed");
			cleanup();
			return;
		}
		if (gameflow.mWorldClock.mCurrentDay != mStartDay) {
			scenarioFailf(t, "the day restored", "day %d, start %d", gameflow.mWorldClock.mCurrentDay, mStartDay);
			cleanup();
			return;
		}
		std::printf("[scenario] ship_parts: 30 parts at ticks 0-29 (one per tick); levels reached at ticks 1:%u 2:%u 3:%u 4:%u 5:%u; happyEndable at part %d (tick %u, 25 required)\n",
		            mLevelTick[1], mLevelTick[2], mLevelTick[3], mLevelTick[4], mLevelTick[5], mHappyParts, mHappyTick);
		if (!mHadAchievementFile && fileExists(kAchievementFile)) {
			scenarioFailf(t, "no pikmin_achievements.txt written", "the file exists");
			cleanup();
			return;
		}
		std::printf("[scenario] ship_parts: %d parts registered by the scenario; achievements blocked, no %s written\n", mAddedByScenario, kAchievementFile);
		cleanup();
		scenarioPass();
	}

	void cleanup()
	{
		if (mCleaned) return;
		mCleaned = true;
		pc_settings_scenario_set_infinite_day(0);
	}

	int mThr[5]             = { 1, 6, 12, 29, 30 };
	unsigned mLevelTick[6]  = {};
	int mLastLevel          = 0;
	unsigned mHappyTick     = 0;
	int mHappyParts         = 0;
	int mStartDay           = 1;
	int mAddedByScenario    = 0;
	u8 mAfterArray[4]       = {};
	bool mReady             = false;
	bool mDone              = false;
	bool mCleaned           = false;
	bool mHadAchievementFile = false;
};

} // namespace

PcScenario* pc_scenario_make_ship_parts(void)
{
	static ShipParts s;
	return &s;
}

#endif
