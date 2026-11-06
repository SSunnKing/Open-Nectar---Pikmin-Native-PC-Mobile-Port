// piki_drown: water hazards on stages/sandbox_water.ini (coverage/edge-cases.md
// EC-20 non-blue drown and blue immunity, EC-21 whistle rescue during Drown).
// EC-41 throw into water is scenario piki_throw_water.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "gameflow.h"
#include "scenarios/scenario_progress.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

constexpr int kBlue = 0, kRed = 1, kYellow = 2;
constexpr float kWaterX = 180.0f, kWaterZ = 0.0f, kShoreX = 40.0f, kShoreZ = 0.0f;
constexpr float kRescueWaterX = 95.0f; // west edge of pond, closer to shore for whistle rescue

inline bool resultOn(int id) { return scenario_progress::ResultPeek::get(playerState->mResultFlags, id) != 0; }

Piki* spawnShore(Navi* navi, int colour, float x, float z)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (piki == nullptr) return nullptr;
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at { x, mapMgr->getMinY(x, z, true), z };
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(PikiMode::FreeMode, navi);
	GameStat::update();
	return piki;
}

void dropOntoWater(Piki* piki, float x, float z)
{
	piki->mSRT.t.x = x;
	piki->mSRT.t.z = z;
	piki->mSRT.t.y = mapMgr->getMinY(x, z, true) + 40.0f;
	piki->mVelocity.set(0.0f, -100.0f, 0.0f);
	piki->mFSM->transit(piki, PIKISTATE_Flying);
}

enum Phase { DropBlue, WaitBlue, DropYellow, WhistleRescue, DropRed, WaitRed, Done };

class PikiDrown : public PcScenario {
public:
	const char* name() const override { return "piki_drown"; }

	// First Pikmin death can open the re_k diary overlay; run that last so whistle rescue is not interrupted.
	bool checksGameStat() const override { return mPhase < DropRed; }

	void onStageReady() override
	{
		pc_settings_scenario_set_blues_only_water(0);
		scenario_progress::applyDiscKeys();
		mNavi = naviMgr->getNavi();
		if (mNavi == nullptr) scenarioFailf(0, "a captain", "none");
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mWhistleTicks > 0) {
			pad.button |= PAD_BUTTON_B;
			if (mNavi && mYellow) {
				mNavi->mCursorWorldPos = mYellow->mSRT.t;
			}
			--mWhistleTicks;
		}
		(void)t;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		switch (mPhase) {
		case DropBlue:
			if (t == 0) {
				mBlue = spawnShore(mNavi, kBlue, kShoreX, kShoreZ + 10.0f);
				if (mBlue == nullptr) {
					scenarioFailf(t, "blue Pikmin for immunity", "spawn failed");
					return;
				}
				dropOntoWater(mBlue, kWaterX + 12.0f, kWaterZ);
				mPhase = WaitBlue;
				mPhaseTick = t;
			}
			break;
		case WaitBlue:
			if (mBlue->getState() == PIKISTATE_Drown) {
				scenarioFailf(t, "blue immune (must not enter Drown)", "state Drown");
				return;
			}
			if (resultOn(zen::RESFLAG_BluePikminWaterImmunity) && t - mPhaseTick > 3) {
				std::printf("[scenario] piki_drown: blue immunity flag tick %u\n", t);
				mNavi->mSRT.t.x = kShoreX;
				mNavi->mSRT.t.z = kShoreZ;
				mNavi->mSRT.t.y = mapMgr->getMinY(mNavi->mSRT.t.x, mNavi->mSRT.t.z, true);
				mPhase = DropYellow;
				mPhaseTick = t;
			} else if (t - mPhaseTick > 20 * rate) {
				scenarioFailf(t, "RESFLAG_BluePikminWaterImmunity after blue lands on water", "flag clear, state %d", mBlue->getState());
			}
			break;
		case DropYellow:
			if (mYellow == nullptr) {
				mYellow = spawnShore(mNavi, kYellow, kShoreX, kShoreZ - 10.0f);
				if (mYellow == nullptr) {
					scenarioFailf(t, "yellow for whistle rescue", "spawn failed");
					return;
				}
				mNavi->mSRT.t.x = kShoreX + 5.0f;
				mNavi->mSRT.t.z = kShoreZ;
				mNavi->mSRT.t.y = mapMgr->getMinY(mNavi->mSRT.t.x, mNavi->mSRT.t.z, true);
				dropOntoWater(mYellow, kRescueWaterX, kWaterZ);
				mPhase = WhistleRescue;
				mPhaseTick = t;
				mRescueArmed = true;
				mWhistleTicks = 120 * (int)rate / 30;
				mRescueTick = t;
			}
			break;
		case WhistleRescue:
			if (mYellow && mNavi) {
				mNavi->mCursorWorldPos = mYellow->mSRT.t;
				mNavi->callPikis(NAVI_WHISTLE_MAX_RADIUS(mNavi));
			}
			if (mYellow->getState() == PIKISTATE_Drown && !mRescueDrownSeen) {
				mRescueDrownSeen = true;
				std::printf("[scenario] piki_drown: yellow in Drown tick %u\n", t);
			}
			if (mRescueArmed && mRescueDrownSeen && mYellow->isAlive() && mYellow->getState() != PIKISTATE_Drown && mYellow->getState() != PIKISTATE_Dying
			    && mYellow->getState() != PIKISTATE_Dead && mYellow->mInWaterTimer == 0) {
				if (mYellow->getState() == PIKISTATE_Emotion && !mRescueEmotionSeen) {
					mRescueEmotionSeen = true;
					mRescueEmotionTick = t;
					std::printf("[scenario] piki_drown: yellow shake-dry Emotion tick %u\n", t);
				}
				if (mYellow->mMode == PikiMode::FormationMode) {
					std::printf("[scenario] piki_drown: whistle rescue formation state %d tick %u\n", mYellow->getState(), t);
					mPhase = DropRed;
					mPhaseTick = t;
				} else if (mRescueEmotionSeen && t - mRescueEmotionTick > 20 * rate) {
					scenarioFailf(t, "whistle rescue returns to FormationMode after Emotion within 20 s", "state %d mode %d", mYellow->getState(),
					              (int)mYellow->mMode);
				}
			} else if (mRescueArmed && t - mRescueTick > 45 * rate) {
				scenarioFailf(t, "whistle rescue leaves Drown within 45 s", "state %d inWaterTimer %d drown seen %d", mYellow->getState(), mYellow->mInWaterTimer,
				              (int)mRescueDrownSeen);
			} else if (t - mPhaseTick > 60 * rate) {
				scenarioFailf(t, "yellow enters Drown for rescue test", "state %d", mYellow->getState());
			}
			break;
		case DropRed:
			if (mRed == nullptr) {
				mRed = spawnShore(mNavi, kRed, kShoreX, kShoreZ);
				if (mRed == nullptr) {
					scenarioFailf(t, "red Pikmin for drown", "spawn failed");
					return;
				}
				dropOntoWater(mRed, kWaterX, kWaterZ);
				mPhase = WaitRed;
				mPhaseTick = t;
			}
			break;
		case WaitRed:
			if (mRed && (GameStat::deadPikis[kRed] > 0 || mRed->getState() == PIKISTATE_Dying || mRed->getState() == PIKISTATE_Dead)) {
				std::printf("[scenario] piki_drown: red drowned deadPikis %d state %d tick %u\n", GameStat::deadPikis[kRed], mRed->getState(), t);
				mPhase = Done;
				scenarioPass();
			} else if (t - mPhaseTick > 45 * rate) {
				scenarioFailf(t, "red thrown into water reaches Dying/Dead or deadPikis within 45 s", "state %d dead %d", mRed ? mRed->getState() : -1,
				              GameStat::deadPikis[kRed]);
			}
			break;
		default:
			break;
		}
	}

private:
	Phase mPhase = DropBlue;
	unsigned mPhaseTick = 0, mRescueTick = 0;
	Navi* mNavi = nullptr;
	Piki *mRed = nullptr, *mBlue = nullptr, *mYellow = nullptr;
	bool mRescueArmed = false, mRescueDrownSeen = false, mRescueEmotionSeen = false;
	unsigned mRescueEmotionTick = 0;
	int mWhistleTicks = 0;
};

class PikiDrownBlueFail : public PcScenario {
public:
	const char* name() const override { return "piki_drown_blue_fail"; }
	bool testOnly() const override { return true; }

	void onStageReady() override
	{
		pc_settings_scenario_set_blues_only_water(0);
		Navi* navi = naviMgr->getNavi();
		if (navi) {
			mBlue = spawnShore(navi, kBlue, kShoreX, 0.0f);
			if (mBlue) dropOntoWater(mBlue, kWaterX, kWaterZ);
		}
	}

	void onTick(unsigned t) override
	{
		if (t != 60) return;
		if (mBlue && mBlue->getState() == PIKISTATE_Drown) scenarioPass();
		scenarioFailf(t, "blue in Drown (wrong: blue is immune)", "state %d", mBlue ? mBlue->getState() : -1);
	}

	Piki* mBlue = nullptr;
};

} // namespace

PcScenario* pc_scenario_make_piki_drown(void)
{
	static PikiDrown s;
	return &s;
}

PcScenario* pc_scenario_make_piki_drown_blue_fail(void)
{
	static PikiDrownBlueFail s;
	return &s;
}

#endif
