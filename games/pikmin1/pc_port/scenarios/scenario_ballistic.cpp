// Controlled probes of real Creature movement, including the volatile pass. The fall and
// flick setup mirrors the GPIE01 rev 1 Dolphin probe recorded for issue #157. The flick
// enters the real state, then normalizes its random vertical impulse to 125 units/s.
// PIKMIN_BALLISTIC_SPOT=x,z selects a flat unobstructed spot (disc: -228,1976;
// stand-in stage1: -350,350). Traces include the initial state at elapsed tick zero.
#include "scenarios/pc_scenario_list.h"
#if PIKI_SCENARIOS
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "GameStat.h"
#include "AIConstant.h"
#include "Peve/MotionEvents.h"
#include "Interactions.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "gameflow.h"
#include "teki.h"
namespace {
enum Kind { Fall, Flick, Launch, Impulse, ZeroGravity, Drag, Volatile, GravityToggle, SubtickImpulse, FlowerNegative, FlowerApexImpulse };
class Ballistic : public PcScenario {
public:
	Ballistic(const char* n, Kind k) : mName(n), mKind(k) {}
	const char* name() const override { return mName; }
	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi = naviMgr->getNavi();
		if (!mPiki) {
			if ((gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive) || !navi || navi->getCurrState()->getID() != NAVISTATE_Walk) {
				mQuiet = 0;
				return;
			}
			if (++mQuiet < rate) return;
			Vector3f at(-350.0f, 0.0f, 350.0f);
			if (const char* spot = std::getenv("PIKMIN_BALLISTIC_SPOT")) std::sscanf(spot, "%f,%f", &at.x, &at.z);
			mGround = mapMgr->getMinY(at.x, at.z, true);
			navi->mSRT.t.set(at.x - 100.0f, mGround, at.z);
			navi->mVelocity.set(0, 0, 0);
			mPiki = static_cast<Piki*>(pikiMgr->birth());
			if (!mPiki) { scenarioFailf(t, "one born Pikmin", "birth returned null"); return; }
			GameStat::workPikis.inc(Blue);
			mPiki->init(navi);
			at.y = mGround + (mKind == Flick ? 0.0f : 200.0f);
			mPiki->Creature::init(at);
			mPiki->initColor(Blue);
			mPiki->setFlower(Leaf);
			mPiki->changeMode(PikiMode::FreeMode, navi);
			GameStat::update();
			if (mKind == Flick) {
				InteractFlick flick(navi, 60.0f, 0.0f, 0.0f);
				mPiki->stimulate(flick);
				if (mPiki->getState() != PIKISTATE_Flick) {
					scenarioFailf(t, "real Flick state", "state %d", mPiki->getState()); return;
				}
			}
			if (mKind == FlowerNegative || mKind == FlowerApexImpulse) {
				mPiki->setFlower(Flower);
				mPiki->mFSM->transit(mPiki, PIKISTATE_Flying);
			}
			mPiki->mVelocity.set(0, mKind == Flick || mKind == Launch || mKind == Drag || mKind == GravityToggle || mKind == SubtickImpulse || mKind == FlowerApexImpulse ? 125.0f : (mKind == FlowerNegative ? -50.0f : 0.0f), 0);
			mPiki->mTargetVelocity.set(0, 0, 0);
			mPiki->mVolatileVelocity.set(0, 0, 0);
			mPiki->mSRT.t = at;
			mPiki->resetCreatureFlag(CF_IsOnGround | CF_IsFlying | CF_IgnoreGravity | CF_EnableAirDrag | CF_IsPositionFixed | CF_AllowFixPosition);
			mPiki->mGroundTriangle = nullptr;
			if (mKind == GravityToggle || mKind == SubtickImpulse) mPiki->stopAI();
			if (mKind == ZeroGravity) { mPiki->disableGravity(); mPiki->stopAI(); mPiki->mVelocity.y = 125.0f; }
			if (mKind == Drag) { mPiki->setCreatureFlag(CF_EnableAirDrag); mPiki->mAirResistance = 0.75f; }
			mStart = t;
			mPeak = at.y;
			std::printf("[ballistic] BEGIN %s rate %u y %.6f vy %.6f state %d flags %x\n", mName, rate, at.y - mGround, mPiki->mVelocity.y, mPiki->getState(), mPiki->mCreatureFlags);
		}
		const unsigned elapsed = t - mStart;
		mPeak = std::max(mPeak, mPiki->mSRT.t.y);
		std::printf("[ballistic] TRACE %s %u %.6f %.6f\n", mName, elapsed, mPiki->mSRT.t.y - mGround, mPiki->mVelocity.y);
		if (!mLanded && elapsed > 0 && mPiki->mSRT.t.y <= mGround + 0.01f) mLanded = elapsed;
		if (mKind == GravityToggle && elapsed > 1) {
			const float expected = mEventY + mEventVelocity * float(elapsed - 1) / float(rate);
			if (std::fabs(mPiki->mSRT.t.y - expected) > 0.02f) {
				scenarioFailf(t, "disabling gravity removes the discarded pass's gravity transport immediately", "y %.6f, expected %.6f", mPiki->mSRT.t.y, expected);
				return;
			}
			if (elapsed >= rate / 3) { scenarioPass(); return; }
		}
		if (mKind == SubtickImpulse && elapsed == rate / 30) {
			const float h = 1.0f / 30.0f;
			// The first reference kick is already consumed when the impulse arrives.
			// Its remaining segment gains125 units/s; gravity must not kick again.
			const float expected = mGround + 200.0f + (125.0f - 2.0f * AICONST.mGravity() * h) * h
								 + 125.0f * (h - 1.0f / float(rate));
			if (std::fabs(mPiki->mSRT.t.y - expected) > 0.02f) {
				scenarioFailf(t, "a sub-tick impulse preserves consumed reference gravity", "y %.6f, expected %.6f", mPiki->mSRT.t.y, expected);
			} else scenarioPass();
			return;
		}
		if (elapsed >= 2 * rate) {
			std::printf("[ballistic] END %s peak %.6f landing %u\n", mName, mPeak - mGround, mLanded);
			if (mKind != ZeroGravity && !mLanded) scenarioFailf(t, "landing within two seconds", "height %.6f", mPiki->mSRT.t.y - mGround);
			else scenarioPass();
			return;
		}
		// Inject after the trace, so the next movement tick consumes this real impulse.
		// Use the same physical time (7/30 s) at every rate; helper tests cover subframe impulses.
		if ((mKind == Impulse || mKind == FlowerApexImpulse) && elapsed == 7 * (rate / 30)) mPiki->mVelocity.y += 125.0f;
		if (mKind == GravityToggle && elapsed == 1) {
			mPiki->disableGravity();
			mEventY = mPiki->mSRT.t.y;
			mEventVelocity = mPiki->mVelocity.y;
		}
		if (mKind == SubtickImpulse && elapsed == 1) mPiki->mVelocity.y += 125.0f;
		if (mKind == Volatile) mPiki->mVolatileVelocity.y = 20.0f;
	}
private:
	const char* mName;
	Kind mKind;
	Piki* mPiki = nullptr;
	unsigned mQuiet = 0, mStart = 0, mLanded = 0;
	float mGround = 0, mPeak = 0, mEventY = 0, mEventVelocity = 0;
};
class EnemyBallistic : public PcScenario {
public:
	const char* name() const override { return "ballistic_enemy"; }
	void onStageReady() override
	{
		Iterator it(tekiMgr);
		CI_LOOP(it) { Teki* teki = static_cast<Teki*>(*it); if (teki->mTekiType == TEKI_Chappy) { mEnemy = teki; break; } }
		if (!mEnemy) { scenarioFailf(0, "a generated Dwarf Bulborb", "none"); return; }
		// Suppress unrelated animation/AI/movement; invoke the real action-gravity owner below.
		mEnemy->mIsFrozen = true; mEnemy->stopAI(); mEnemy->setCreatureFlag(CF_DisableMovement);
		mEnemy->mDeadState = 1;
		mGround = mapMgr->getMinY(mEnemy->mSRT.t.x, mEnemy->mSRT.t.z, true);
		mEnemy->mSRT.t.y = mGround + 200;
		mEnemy->mActionVelocity.set(0, 125, 0);
	}
	void onTick(unsigned t) override
	{
		if (!mEnemy) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (!t) std::printf("[ballistic] BEGIN ballistic_enemy rate %u y %.6f vy %.6f state -1 flags 0\n", rate, mEnemy->mSRT.t.y - mGround, mEnemy->mActionVelocity.y);
		const float y = mEnemy->mSRT.t.y - mGround;
		mPeak = std::max(mPeak, y);
		std::printf("[ballistic] TRACE ballistic_enemy %u %.6f %.6f\n", t, y, mEnemy->mActionVelocity.y);
		if (!mLanding && t && y <= .01f) mLanding = t;
		if (t >= 2 * rate) {
			std::printf("[ballistic] END ballistic_enemy peak %.6f landing %u\n", mPeak, mLanding);
			const float expectedVelocity = 125.0f - AICONST.mGravity() * 2.0f;
			if (!mLanding) scenarioFailf(t, "enemy action-gravity reaches ground", "y %.6f", y);
			else if (std::fabs(mEnemy->mActionVelocity.y - expectedVelocity) > 0.02f)
				scenarioFailf(t, "repeated ground contacts preserve the reference gravity cadence", "vy %.6f, expected %.6f", mEnemy->mActionVelocity.y, expectedVelocity);
			else scenarioPass();
			return;
		}
		mEnemy->gravitate(AICONST.mGravity());
	}
private:
	Teki* mEnemy = nullptr;
	float mGround = 0, mPeak = 200;
	unsigned mLanding = 0;
};
class ScriptedBallistic : public PcScenario {
public:
	const char* name() const override { return "ballistic_scripted"; }
	void onStageReady() override
	{
		NVector3f pos(0, 200, 0), vel(0, 125, 0), accel(0, -AICONST.mGravity(), 0);
		mPosition.input(pos); mVelocity.input(vel); mAcceleration.input(accel);
		mEvent.makeAccelerationEvent(nullptr, &mPosition, &mVelocity, &mAcceleration);
	}
	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		NVector3f pos, vel;
		mPosition.output(pos); mVelocity.output(vel);
		if (!t) std::printf("[ballistic] BEGIN ballistic_scripted rate %u y %.6f vy %.6f state -1 flags 0\n", rate, pos.y, vel.y);
		mPeak = std::max(mPeak, pos.y);
		std::printf("[ballistic] TRACE ballistic_scripted %u %.6f %.6f\n", t, pos.y, vel.y);
		if (t >= 2 * rate) {
			std::printf("[ballistic] END ballistic_scripted peak %.6f landing 0\n", mPeak);
			scenarioPass(); return;
		}
		mEvent.update();
	}
private:
	NVector3fIOClass mPosition, mVelocity, mAcceleration;
	PeveAccelerationEvent mEvent;
	float mPeak = 200;
};
}
#define BALLISTIC_FACTORY(suffix, kind) PcScenario* pc_scenario_make_ballistic_##suffix() { static Ballistic s("ballistic_" #suffix, kind); return &s; }
BALLISTIC_FACTORY(flower_negative, FlowerNegative)
BALLISTIC_FACTORY(flower_apex_impulse, FlowerApexImpulse)
BALLISTIC_FACTORY(fall, Fall)
BALLISTIC_FACTORY(flick, Flick)
BALLISTIC_FACTORY(launch, Launch)
BALLISTIC_FACTORY(impulse, Impulse)
BALLISTIC_FACTORY(zero_gravity, ZeroGravity)
BALLISTIC_FACTORY(drag, Drag)
BALLISTIC_FACTORY(volatile, Volatile)
BALLISTIC_FACTORY(gravity_toggle, GravityToggle)
BALLISTIC_FACTORY(subtick_impulse, SubtickImpulse)
#undef BALLISTIC_FACTORY
PcScenario* pc_scenario_make_ballistic_enemy() { static EnemyBallistic s; return &s; }
PcScenario* pc_scenario_make_ballistic_scripted() { static ScriptedBallistic s; return &s; }
#endif
