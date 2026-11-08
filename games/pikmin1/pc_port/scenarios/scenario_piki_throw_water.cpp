// piki_throw_water: thrown Pikmin landing on water (coverage/edge-cases.md EC-41).
// Drops Pikmin in Flying above the pond so bounceCallback runs on landing.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "system.h"
#include "scenarios/scenario_progress.h"

namespace {

constexpr int kBlue = 0, kRed = 1;
constexpr float kWaterX = 180.0f, kWaterZ = 0.0f;

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

enum Phase { DropRed, WaitRed, DropBlue, WaitBlue, Done };

class PikiThrowWater : public PcScenario {
public:
	const char* name() const override { return "piki_throw_water"; }

	void onStageReady() override
	{
		mNavi = naviMgr->getNavi();
		if (mNavi == nullptr) scenarioFailf(0, "a captain", "none");
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		switch (mPhase) {
		case DropRed:
			if (t == 0) {
				mRed = spawnShore(mNavi, kRed, 40.0f, 0.0f);
				if (mRed == nullptr) {
					scenarioFailf(t, "red Pikmin", "spawn failed");
					return;
				}
				dropOntoWater(mRed, kWaterX, kWaterZ);
				mPhase = WaitRed;
				mPhaseTick = t;
			}
			break;
		case WaitRed:
			if (mRed->getState() == PIKISTATE_Drown || GameStat::deadPikis[kRed] > 0) {
				std::printf("[scenario] piki_throw_water: red thrown into water drowns tick %u\n", t);
				mPhase = DropBlue;
				mPhaseTick = t;
			} else if (t - mPhaseTick > 30 * rate) {
				scenarioFailf(t, "red thrown into water enters Drown or dies", "state %d dead %d", mRed->getState(), GameStat::deadPikis[kRed]);
			}
			break;
		case DropBlue:
			if (mBlue == nullptr) {
				mBlue = spawnShore(mNavi, kBlue, 40.0f, 10.0f);
				if (mBlue == nullptr) {
					scenarioFailf(t, "blue Pikmin", "spawn failed");
					return;
				}
				dropOntoWater(mBlue, kWaterX + 12.0f, kWaterZ);
				mPhase = WaitBlue;
				mPhaseTick = t;
			}
			break;
		case WaitBlue:
			if (mBlue->getState() == PIKISTATE_Drown) {
				scenarioFailf(t, "blue survives thrown water landing", "state Drown");
				return;
			}
			if (resultOn(zen::RESFLAG_BluePikminWaterImmunity) && t - mPhaseTick > 3) {
				std::printf("[scenario] piki_throw_water: blue survives tick %u state %d\n", t, mBlue->getState());
				mPhase = Done;
				scenarioPass();
			} else if (t - mPhaseTick > 20 * rate) {
				scenarioFailf(t, "blue water immunity after throw landing", "state %d flag %d", mBlue->getState(),
				              (int)resultOn(zen::RESFLAG_BluePikminWaterImmunity));
			}
			break;
		default:
			break;
		}
	}

private:
	Phase mPhase = DropRed;
	unsigned mPhaseTick = 0;
	Navi* mNavi = nullptr;
	Piki *mRed = nullptr, *mBlue = nullptr;
};

class PikiThrowWaterRedSurviveFail : public PcScenario {
public:
	const char* name() const override { return "piki_throw_water_red_survive_fail"; }
	bool testOnly() const override { return true; }

	void onTick(unsigned t) override
	{
		if (t == 0) {
			Navi* navi = naviMgr->getNavi();
			if (navi == nullptr) return;
			mRed = spawnShore(navi, kRed, 40.0f, 0.0f);
			if (mRed) dropOntoWater(mRed, kWaterX, kWaterZ);
			return;
		}
		if (t != 40) return;
		if (mRed && mRed->getState() == PIKISTATE_Normal && GameStat::deadPikis[kRed] == 0) scenarioPass();
		scenarioFailf(t, "red survives water (wrong)", "state %d dead %d", mRed ? mRed->getState() : -1, GameStat::deadPikis[kRed]);
	}

	Piki* mRed = nullptr;
};

} // namespace

PcScenario* pc_scenario_make_piki_throw_water(void)
{
	static PikiThrowWater s;
	return &s;
}

PcScenario* pc_scenario_make_piki_throw_water_red_survive_fail(void)
{
	static PikiThrowWaterRedSurviveFail s;
	return &s;
}

#endif
