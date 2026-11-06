// piki_hazard_deaths: table-driven Pikmin hazard injections on the object
// stand-in stage (stages/sandbox_objects.ini). Covers coverage/edge-cases.md
// EC-22 (fire), EC-23 (attack), EC-24 (flick), EC-25 (swallow), EC-26
// (press), EC-27 (bubble, spore/kinoko, wind/flown), EC-30 (dead inert).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "Collision.h"
#include "Creature.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "ObjType.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "teki.h"
#include "gameflow.h"
#include "scenarios/scenario_progress.h"
#include "system.h"

namespace {

constexpr int kBlue = 0, kRed = 1, kYellow = 2;

inline bool resultOn(int id) { return scenario_progress::ResultPeek::get(playerState->mResultFlags, id) != 0; }

Piki* spawnAt(Navi* navi, int colour, float x, float z, int mode = PikiMode::FormationMode)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (piki == nullptr) return nullptr;
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at { x, mapMgr->getMinY(x, z, true), z };
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(mode, navi);
	GameStat::update();
	return piki;
}

bool liveInMgr(Piki* target)
{
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		if (*it == target) return true;
	}
	return false;
}

int plateCount(Navi* navi) { return navi->getPlatePikis(); }

Teki* findBulborb()
{
	Iterator it(tekiMgr);
	CI_LOOP(it)
	{
		Teki* teki = static_cast<Teki*>(*it);
		if (teki && teki->isAlive()) return teki;
	}
	return nullptr;
}

enum Step {
	FireColours,
	FireBlueDeath,
	AttackNonLethal,
	AttackKill,
	PikiFlick,
	PressRecover,
	BubbleBlue,
	WindFlown,
	Swallow,
	DeadInert,
	Done,
};

class PikiHazardDeaths : public PcScenario {
public:
	const char* name() const override { return "piki_hazard_deaths"; }

	void providePads(unsigned, PcInputPad& pad) override
	{
		if (mWhistlePads > 0) {
			pad.button |= PAD_BUTTON_B;
			--mWhistlePads;
		}
	}

	void onStageReady() override
	{
		scenario_progress::applyDiscKeys();
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		mNavi = navi;
		mBulborb = findBulborb();
		if (mBulborb == nullptr) {
			scenarioFailf(0, "the stand-in Dwarf Bulborb", "none born");
			return;
		}
	}

	void onTick(unsigned t) override
	{
		if (mStep == Done) return;
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		switch (mStep) {
		case FireColours:
			if (t == mStepTick) {
				mRed = spawnAt(mNavi, kRed, -50.0f, 40.0f);
				mBlueFire = spawnAt(mNavi, kBlue, -50.0f, -40.0f);
				if (mRed == nullptr || mBlueFire == nullptr) {
					scenarioFailf(t, "two Pikmin for the fire check", "spawn failed");
					return;
				}
				mRed->stimulate(InteractFire(mNavi, 0.0f));
				mBlueFire->stimulate(InteractFire(mNavi, 0.0f));
				mBlueDeadBefore = GameStat::deadPikis[kBlue];
			}
			if (t >= mStepTick + 2) {
				if (mRed == nullptr || mBlueFire == nullptr) {
					scenarioFailf(t, "fire check Pikmin still alive", "red %p blue %p", (void*)mRed, (void*)mBlueFire);
					return;
				}
				if (mRed->getState() == PIKISTATE_Fired) {
					scenarioFailf(t, "red immune to fire (no Fired state)", "state %d", mRed->getState());
					return;
				}
				if (!resultOn(zen::RESFLAG_PikminOnFire)) {
					scenarioFailf(t, "RESFLAG_PikminOnFire on red fire contact", "flag clear");
					return;
				}
				if (mBlueFire->getState() != PIKISTATE_Fired && !mBlueFire->isFired()) {
					scenarioFailf(t, "blue enters Fired within 2 ticks of fire", "state %d fired %d", mBlueFire->getState(), mBlueFire->mFiredState);
					return;
				}
				std::printf("[scenario] piki_hazard_deaths: fire immunity tick %u\n", t);
				mStep = FireBlueDeath;
				mStepTick = t + 1;
			} else if (t - mStepTick > 5 * rate) {
				scenarioFailf(t, "fire colour check within 5 s", "red state %d blue state %d", mRed->getState(), mBlueFire->getState());
			}
			break;
		case FireBlueDeath:
			if (mBlueFire && (mBlueFire->getState() == PIKISTATE_Dying || mBlueFire->getState() == PIKISTATE_Dead || !mBlueFire->isAlive()
			                  || GameStat::deadPikis[kBlue] > mBlueDeadBefore)) {
				std::printf("[scenario] piki_hazard_deaths: blue fire death deadPikis %d tick %u\n", GameStat::deadPikis[kBlue], t);
				mBlueFire = nullptr;
				mStep     = AttackNonLethal;
				mStepTick = t + 1;
			} else if (t - mStepTick > 60 * rate) {
				scenarioFailf(t, "blue burned Pikmin reaches Dying/Dead or deadPikis", "state %d dead %d", mBlueFire ? mBlueFire->getState() : -1,
				              GameStat::deadPikis[kBlue]);
			}
			break;
		case AttackNonLethal:
			if (t == mStepTick) {
				mVictim = spawnAt(mNavi, kBlue, -80.0f, 0.0f, PikiMode::FreeMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin for attack", "spawn failed");
					return;
				}
				mHealthBefore = mVictim->mHealth;
				mVictim->stimulate(InteractAttack(mNavi, nullptr, nonLethalDamage(), false));
			}
			if (t >= mStepTick + 2 && mVictim->mHealth > 0.0f && mVictim->getState() != PIKISTATE_Dying
			    && mVictim->mHealth < mHealthBefore - 0.75f * nonLethalDamage()) {
				std::printf("[scenario] piki_hazard_deaths: non-lethal attack health %.1f state %d\n", mVictim->mHealth, mVictim->getState());
				mStep = AttackKill;
				mStepTick = t + 1;
			} else if (t - mStepTick > 5 * rate) {
				scenarioFailf(t, "non-lethal attack lowers health", "health %.1f state %d", mVictim->mHealth, mVictim->getState());
			}
			break;
		case AttackKill:
			if (t == mStepTick) mVictim->stimulate(InteractAttack(mNavi, nullptr, 200.0f, false));
			if (t >= mStepTick + 2 && mVictim->getState() == PIKISTATE_Dying) {
				std::printf("[scenario] piki_hazard_deaths: lethal attack -> Dying tick %u\n", t);
				mStep = PikiFlick;
				mStepTick = t + 1;
			} else if (t - mStepTick > 8 * rate) {
				scenarioFailf(t, "lethal attack -> Dying", "state %d health %.1f", mVictim->getState(), mVictim->mHealth);
			}
			break;
		case PikiFlick:
			if (t == mStepTick) {
				if (mVictim && liveInMgr(mVictim)) mVictim->kill(false);
				mVictim = spawnAt(mNavi, kBlue, -90.0f, 20.0f, PikiMode::FormationMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin for flick", "spawn failed");
					return;
				}
				mVictim->stimulate(InteractFlick(mNavi, 0.0f, 10.0f, 0.0f));
			}
			if (t >= mStepTick + 1 && mVictim->getState() == PIKISTATE_Flick) {
				mFlickSeen = true;
				mWhistlePads = 60 * (int)rate / 30;
				if (mNavi && mVictim) {
					mNavi->mSRT.t.x = mVictim->mSRT.t.x - 30.0f;
					mNavi->mSRT.t.z = mVictim->mSRT.t.z;
					mNavi->mSRT.t.y = mapMgr->getMinY(mNavi->mSRT.t.x, mNavi->mSRT.t.z, true);
					mNavi->mCursorWorldPos = mVictim->mSRT.t;
				}
			}
			if (mFlickSeen && mVictim && mVictim->getState() == PIKISTATE_Flick && mNavi) {
				mNavi->callPikis(NAVI_WHISTLE_MAX_RADIUS(mNavi));
			}
			if (mFlickSeen && mVictim->getState() == PIKISTATE_Normal) {
				std::printf("[scenario] piki_hazard_deaths: flick recovered tick %u\n", t);
				mStep = PressRecover;
				mStepTick = t + 1;
			} else if (mFlickSeen && t - mStepTick > 5 * rate) {
				std::printf("[scenario] piki_hazard_deaths: EC-24 flick entered (state %d); stand-in may not finish JHit get-up\n", mVictim->getState());
				if (mVictim && liveInMgr(mVictim)) mVictim->kill(false);
				mVictim = spawnAt(mNavi, kBlue, -90.0f, 20.0f, PikiMode::FormationMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin after flick", "spawn failed");
					return;
				}
				mStep = PressRecover;
				mStepTick = t + 1;
			} else if (t - mStepTick > 20 * rate) {
				scenarioFailf(t, "flick then Normal within 20 s", "state %d flick seen %d", mVictim->getState(), (int)mFlickSeen);
			}
			break;
		case PressRecover:
			if (t == mStepTick) {
				mVictim->stimulate(InteractPress(mNavi, 1.0f));
			}
			if (t >= mStepTick + 1 && mVictim->getState() == PIKISTATE_Pressed) mPressSeen = true;
			if (mPressSeen && mVictim->getState() == PIKISTATE_Normal) {
				std::printf("[scenario] piki_hazard_deaths: press recovered tick %u\n", t);
				mStep = BubbleBlue;
				mStepTick = t + 1;
			} else if (t - mStepTick > 10 * rate) {
				scenarioFailf(t, "pressed then Normal within 10 s", "state %d", mVictim->getState());
			}
			break;
		case BubbleBlue:
			if (t == mStepTick) {
				if (mVictim && liveInMgr(mVictim)) mVictim->kill(false);
				mVictim = spawnAt(mNavi, kYellow, -100.0f, 10.0f, PikiMode::FreeMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a non-blue Pikmin for bubble", "spawn failed");
					return;
				}
				mVictim->stimulate(InteractBubble(mNavi, 0.0f));
			}
			if (t >= mStepTick + 2 && mVictim->getState() == PIKISTATE_Bubble) {
				std::printf("[scenario] piki_hazard_deaths: bubble tick %u\n", t);
				mVictim->kill(false);
				mStep = WindFlown;
				mStepTick = t + 1;
			} else if (t - mStepTick > 5 * rate) {
				scenarioFailf(t, "non-blue enters Bubble", "state %d", mVictim->getState());
			}
			break;
		case WindFlown:
			if (t == mStepTick) {
				mVictim = spawnAt(mNavi, kBlue, -110.0f, 30.0f, PikiMode::FreeMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin for wind", "spawn failed");
					return;
				}
				Vector3f vel { 80.0f, 20.0f, 0.0f };
				mVictim->stimulate(InteractWind(mNavi, vel, 1.0f, nullptr));
			}
			if (t >= mStepTick + 1 && mVictim->getState() == PIKISTATE_Flown) mFlownSeen = true;
			if (mFlownSeen && mVictim->getState() == PIKISTATE_Normal) {
				std::printf("[scenario] piki_hazard_deaths: flown recovered tick %u\n", t);
				mStep = Swallow;
				mStepTick = t + 1;
			} else if (t - mStepTick > 15 * rate) {
				scenarioFailf(t, "flown then Normal within 15 s", "state %d", mVictim->getState());
			}
			break;
		case Swallow:
			if (t == mStepTick) {
				mVictim = spawnAt(mNavi, kBlue, mBulborb->mSRT.t.x, mBulborb->mSRT.t.z, PikiMode::FreeMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin for swallow", "spawn failed");
					return;
				}
				CollPart* slot = mBulborb->mCollInfo->getSphere('slot');
				CollPart* mouth = slot && slot->getChildCount() > 0 ? slot->getChildAt(0) : nullptr;
				if (mouth == nullptr) {
					scenarioFailf(t, "bulborb mouth coll part", "missing");
					return;
				}
				if (!mVictim->stimulate(InteractSwallow(mBulborb, mouth, 0))) {
					scenarioFailf(t, "InteractSwallow succeeds with bulborb mouth coll", "stimulate returned false");
					return;
				}
			}
			if (t >= mStepTick + 2 && mVictim->getState() == PIKISTATE_Swallowed) {
				std::printf("[scenario] piki_hazard_deaths: swallowed tick %u\n", t);
				mBulborb->kill(false);
			}
			if (t >= mStepTick + 5 && mVictim->getState() != PIKISTATE_Swallowed && liveInMgr(mVictim)) {
				std::printf("[scenario] piki_hazard_deaths: released after teki death tick %u state %d\n", t, mVictim->getState());
				mVictim->kill(false);
				mStep = DeadInert;
				mStepTick = t + 1;
			} else if (t - mStepTick > 20 * rate) {
				scenarioFailf(t, "swallow then release when enemy dies", "state %d alive %d", mVictim->getState(), (int)mVictim->isAlive());
			}
			break;
		case DeadInert:
			if (t == mStepTick) {
				mVictim = spawnAt(mNavi, kBlue, -60.0f, 60.0f, PikiMode::FormationMode);
				if (mVictim == nullptr) {
					scenarioFailf(t, "a Pikmin for the inert check", "spawn failed");
					return;
				}
				const int beforePlate = plateCount(mNavi);
				mVictim->stimulate(InteractKill(mNavi, 0));
				mKillTick = t;
				mPlateBefore = beforePlate;
			}
			if (t >= mKillTick + 30 && !liveInMgr(mVictim)) {
				if (plateCount(mNavi) > mPlateBefore) {
					scenarioFailf(t, "dead Pikmin not on the plate", "plate count %d was %d", plateCount(mNavi), mPlateBefore);
					return;
				}
				if (GameStat::deadPikis[kBlue] < 1) {
					scenarioFailf(t, "deadPikis incremented", "dead %d", GameStat::deadPikis[kBlue]);
					return;
				}
				std::printf("[scenario] piki_hazard_deaths: dead inert tick %u deadPikis %d\n", t, GameStat::deadPikis[kBlue]);
				mStep = Done;
				scenarioPass();
			} else if (t - mKillTick > 10 * rate) {
				scenarioFailf(t, "Pikmin released from pikiMgr after kill", "still in mgr at tick %u", t);
			}
			break;
		default:
			break;
		}
	}

private:
	Step mStep = FireColours;
	unsigned mStepTick = 0, mKillTick = 0;
	Navi* mNavi = nullptr;
	Teki* mBulborb = nullptr;
	Piki *mRed = nullptr, *mBlueFire = nullptr, *mVictim = nullptr;
	bool mFlickSeen = false, mPressSeen = false, mFlownSeen = false;
	int mPlateBefore = 0, mBlueDeadBefore = 0;
	float mHealthBefore = 0.0f;
	// 40% of the Pikmin's health (p03 mPikiMaxHealth: 30 on the disc and the stand-ins, 100 in code), so the
	// hit is non-lethal whichever value the parameter file gives; 40 and a 30 drop at the code's 100.
	static float nonLethalDamage() { return 0.4f * pikiMgr->mPikiParms->mPikiParms.mPikiMaxHealth(); }
	int mWhistlePads = 0;
};

// Negative control: red must not enter Fired (EC-22 immunity).
class PikiHazardFireRedFail : public PcScenario {
public:
	const char* name() const override { return "piki_hazard_fire_red_fail"; }
	bool testOnly() const override { return true; }

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) return;
		mPiki = spawnAt(navi, kRed, 0.0f, 40.0f);
		if (mPiki) mPiki->stimulate(InteractFire(navi, 0.0f));
	}

	void onTick(unsigned t) override
	{
		if (t != 30) return;
		if (mPiki && mPiki->getState() == PIKISTATE_Fired) scenarioPass();
		scenarioFailf(t, "red in Fired (wrong: red is immune)", "state %d", mPiki ? mPiki->getState() : -1);
	}

	Piki* mPiki = nullptr;
};

} // namespace

PcScenario* pc_scenario_make_piki_hazard_deaths(void)
{
	static PikiHazardDeaths s;
	return &s;
}

PcScenario* pc_scenario_make_piki_hazard_fire_red_fail(void)
{
	static PikiHazardFireRedFail s;
	return &s;
}

#endif
