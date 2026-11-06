// gate_break: a gate worn to zero health goes through its stages one at a time, opens its
// waypoint and refuses further hits (coverage/edge-cases.md EC-51, the gate), on
// stages/sandbox_all_objects.ini (the soft gate, OBJTYPE_SluiceSoft).
//
// What the code does (GameCube original; characterised, nothing fixed): InteractAttack::actItem
// (itemMgr.cpp:990) on a sluice subtracts the damage from mHealth; each time the health falls to
// or under the next stage's threshold (mMaxHealth - (stage + 1) * mMaxHealth / mNumStages) it
// sends SluiceAI MsgUser(0) and advances mCurrStage, and at the last stage sets the health to 0.
// A hit above the threshold sends MsgUser(1) (the damage shake) only. Once mCurrStage reaches
// mNumStages the gate is completed and every hit is refused (returns false). SluiceAI opens the
// gate's waypoint (WayPoint::setFlag(true)) when the last break motion starts or, completed, in
// WaitInit. The reinforced and bomb gates (breakable_gates setting) are not on a stand-in stage.
//
// In order, each within a bound in seconds:
//   Find     The soft gate: stages >= 1, stage 0, waypoint closed, health 300 (the stand-in
//            generator writes days 12 and hours 12: 12 + 12 * 24; a chosen value, not the disc's).
//   Scratch  A 1-damage hit is taken but the stage stays 0 and the waypoint closed.
//   Break    One hit per second of one stage's worth of health: each moves the stage by exactly 1;
//            the last leaves health 0 and the gate completed.
//   Open     The waypoint opens within 10 s of the last stage.
//   Refuse   A further hit is refused and the health stays 0.
//
// Negative controls: Scratch is Break's contrast (a hit below one stage's worth moves nothing).
// Manual (2026-10-31, local, not committed): with the Break hits at a third of a stage's worth it
// fails "each stage's worth of damage advances the gate one stage" (stage 0 -> 0, health
// 265.7); with the gate's stage set
// back by one before the Refuse hit it fails "EC-51: a completed gate refuses hits" (taken 1), both at 30 Hz.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "BuildingItem.h"
#include "Interactions.h"
#include "ItemMgr.h"
#include "NaviMgr.h"
#include "ObjType.h"
#include "Route.h"

namespace {

constexpr float kGateHealth = 300.0f;

enum Phase { Break, Open, Done };

class GateBreak : public PcScenario {
public:
	const char* name() const override { return "gate_break"; }
	const char* stageFile() const override { return "stages/sandbox_all_objects.ini"; }

	void onStageReady() override
	{
		Iterator it(itemMgr->getMeltingPotMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_SluiceSoft) mGate = static_cast<BuildingItem*>(c);
		}
		if (!mGate || !mGate->mWayPoint || mGate->mNumStages < 1 || mGate->mCurrStage != 0 || mGate->mWayPoint->mIsOpen) {
			fail(0, "a soft gate at stage 0, full health, waypoint closed", "gate %p", (void*)mGate);
			return;
		}
		// GenObjectItem gives a gate the generator's parameters C + B * hours as its health; the
		// stand-in default.gen writes 12 and 12 (the disc's gate values were not read).
		std::printf("[scenario] gate_break: generator health %.1f of %.1f\n", mGate->mHealth, mGate->mMaxHealth);
		if (mGate->mHealth != kGateHealth || mGate->mMaxHealth != kGateHealth) {
			fail(0, "the stand-in generator gives the soft gate health 300", "health %.1f of %.1f", mGate->mHealth, mGate->mMaxHealth);
			return;
		}
		mStep = mGate->mMaxHealth / float(mGate->mNumStages);
		std::printf("[scenario] gate_break: soft gate, %d stages, health %.1f (%.1f a stage), waypoint %d closed\n", mGate->mNumStages,
		            mGate->mMaxHealth, mStep, mGate->mWayPoint->mIndex);
		const bool taken = hit(1.0f);
		std::printf("[scenario] gate_break: scratch: taken %d, stage %d, health %.1f, waypoint open %d\n", (int)taken, mGate->mCurrStage,
		            mGate->mHealth, (int)mGate->mWayPoint->mIsOpen);
		if (!taken || mGate->mCurrStage != 0 || mGate->mWayPoint->mIsOpen || std::fabs(mGate->mHealth - (kGateHealth - 1.0f)) > 0.01f) {
			fail(0, "a 1-damage hit is taken, takes 1 health and moves no stage", "taken %d, stage %d, health %.1f", (int)taken, mGate->mCurrStage,
			     mGate->mHealth);
		}
	}

	void onTick(unsigned t) override
	{
		if (!mGate || mPhase == Done) return;
		const int rate = pc_scenario_ticks_per_second();
		const float s  = float(t - mPhaseTick) / float(rate);
		switch (mPhase) {
		case Break:
			if (t % rate == 0 && t > 0) {
				const int before = mGate->mCurrStage;
				// The scratch took 1; a full stage's worth from here crosses exactly one threshold.
				const bool taken = hit(mStep);
				std::printf("[scenario] gate_break: tick %u hit: taken %d, stage %d -> %d, health %.1f\n", t, (int)taken, before, mGate->mCurrStage,
				            mGate->mHealth);
				if (!taken || mGate->mCurrStage != before + 1) {
					fail(t, "each stage's worth of damage advances the gate one stage", "taken %d, stage %d -> %d", (int)taken, before,
					     mGate->mCurrStage);
				} else if (mGate->mCurrStage >= mGate->mNumStages) {
					if (mGate->mHealth != 0.0f || !mGate->isCompleted()) {
						fail(t, "EC-51: the last stage leaves the gate at health 0, completed", "health %.1f, completed %d", mGate->mHealth,
						     (int)mGate->isCompleted());
						return;
					}
					mPhase     = Open;
					mPhaseTick = t;
				}
			} else if (float(t) / float(rate) > float(mGate->mNumStages) + 5.0f) {
				fail(t, "the gate breaks within its stage count plus 5 s", "stage %d", mGate->mCurrStage);
			}
			break;
		case Open:
			if (mGate->mWayPoint->mIsOpen) {
				const bool taken = hit(mStep);
				std::printf("[scenario] gate_break: waypoint open %.2f s after the last stage; a further hit taken %d, health %.1f\n", s, (int)taken,
				            mGate->mHealth);
				if (taken || mGate->mHealth != 0.0f) {
					fail(t, "EC-51: a completed gate refuses hits", "taken %d, health %.1f", (int)taken, mGate->mHealth);
					return;
				}
				mPhase = Done;
				scenarioPass();
			} else if (s > 10.0f) {
				fail(t, "EC-51: the broken gate's waypoint opens within 10 s", "closed");
			}
			break;
		default: break;
		}
	}

private:
	bool hit(float damage) { return mGate->stimulate(InteractAttack(naviMgr->getNavi(), nullptr, damage, false)); }

	void fail(unsigned t, const char* expected, const char* fmt, ...) __attribute__((format(printf, 4, 5)))
	{
		char buf[256];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		mPhase = Done;
		scenarioFailf(t, expected, "%s", buf);
	}

	BuildingItem* mGate = nullptr;
	Phase mPhase        = Break;
	unsigned mPhaseTick = 0;
	float mStep         = 0.0f;
};

} // namespace

PcScenario* pc_scenario_make_gate_break(void)
{
	static GateBreak s;
	return &s;
}

#endif
