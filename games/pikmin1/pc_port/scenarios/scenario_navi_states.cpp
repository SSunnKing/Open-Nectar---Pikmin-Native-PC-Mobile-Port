// navi_states: the captain's injected hazard states, Geyzer and Bury, enter and return to Walk,
// and Bury kills at low health (coverage/edge-cases.md EC-34, EC-51's geyser launch), on the
// sandbox stand-in stage.
//
// What the code does (GameCube original; characterised, nothing fixed): InteractGeyzer::actNavi
// (navi.cpp:4500) refuses when the captain is damaged or already in Geyzer or Pellet, otherwise
// enters NAVISTATE_Geyzer with the launch target; NaviGeyzerState throws him at the target
// (getThrowVelocity, height 100) and ends in Walk after the landing motions. InteractBury::actNavi
// refuses in an invincible state, enters NAVISTATE_Bury, takes the damage and goes to Dead at
// health <= 1 (navi.cpp:4520-4532). NaviBuryState with no input frees him after mEscapeTimer, 150
// exec ticks at 30 Hz (naviState.cpp:418, 463), then the GNuke motion (1.3 s at both rates). The
// port scales the timer by PC_TICKS30 (300 ticks at 60 Hz), so the escape is 5 s at every rate;
// before that fix the timer counted raw ticks and 60 Hz freed him after 2.5 s (Walk after 3.80 s
// against 6.30 s at 30 Hz; the W7 findings on #100). Escape asserts 5 to 7 s. Rope, RopeExit, Funbari, Stuck, Pellet,
// Pick and Water have no injection path on a stand-in stage (they are entered from rope, wall, pellet, plucking
// and water geometry or objects) and are not driven here (coverage/edge-cases.md EC-34).
//
// In order, each within a bound in seconds:
//   Settle   The captain stands in Walk (within 5 s).
//   Geyzer   InteractGeyzer to 150 units +x is taken: state Geyzer. A second one is refused.
//   Land     Walk within 15 s, standing within 40 units (xz) of the target.
//   Bury     InteractBury(10) is taken: state Bury, health -10. A second one is refused
//            (invincible) and the health is unchanged.
//   Escape   With no input, Walk again after 5 to 7 s at both rates (the time is printed).
//   Lethal   Health set to 5; InteractBury(10) takes the captain to Dead within 1 s.
//
// Negative controls: the refused second Geyzer and Bury are the contrasts to the taken first ones
// (the same call, refused only because of the state). Manual (2026-10-31, local, not committed):
// with the health left at full for Lethal it fails "EC-34: Bury at health <= 1 kills (Dead within 1 s)"
// (state 19 Bury, health 80); with the Geyzer target 150 units further than checked it fails "the geyser lands the
// captain at its target (within 40 units)", both at 30 Hz. Without the PC_TICKS30 scaling
// (2026-11-04, local) navi_states_60 fails "EC-34: with no input the captain is out of Bury after
// the 5 s escape timer and his get-up (5 to 7 s)" at 3.80 s; navi_states_30 passes at 6.30 s.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Interactions.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"

namespace {

enum Phase { Settle, Land, Escape, Lethal, Done };

class NaviStates : public PcScenario {
public:
	const char* name() const override { return "navi_states"; }

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		Navi* navi = naviMgr->getNavi();
		if (!navi) {
			fail(t, "a captain", "none");
			return;
		}
		const int st  = navi->mStateMachine->getCurrID(navi);
		const float s = float(t - mPhaseTick) / float(pc_scenario_ticks_per_second());
		switch (mPhase) {
		case Settle:
			if (st == NAVISTATE_Walk && s >= 1.0f) {
				mTarget   = navi->mSRT.t;
				mTarget.x += 150.0f;
				mTarget.y = mapMgr->getMinY(mTarget.x, mTarget.z, true);
				const bool first  = navi->stimulate(InteractGeyzer(navi, mTarget));
				const int now     = navi->mStateMachine->getCurrID(navi);
				const bool second = navi->stimulate(InteractGeyzer(navi, mTarget));
				std::printf("[scenario] navi_states: tick %u geyzer from (%.0f, %.0f): taken %d, state %d, second taken %d\n", t, navi->mSRT.t.x,
				            navi->mSRT.t.z, (int)first, now, (int)second);
				if (!first || now != NAVISTATE_Geyzer) {
					fail(t, "EC-51: InteractGeyzer in Walk enters Geyzer", "taken %d, state %d", (int)first, now);
				} else if (second) {
					fail(t, "EC-34: a second InteractGeyzer in Geyzer is refused", "taken");
				} else {
					enter(Land, t);
				}
			} else if (s > 5.0f) {
				fail(t, "the captain stands in Walk within 5 s", "state %d", st);
			}
			break;
		case Land:
			if (st == NAVISTATE_Walk) {
				const float dx = navi->mSRT.t.x - mTarget.x, dz = navi->mSRT.t.z - mTarget.z;
				const float off = std::sqrt(dx * dx + dz * dz);
				std::printf("[scenario] navi_states: Walk after %.2f s at (%.0f, %.0f), %.1f from the target\n", s, navi->mSRT.t.x, navi->mSRT.t.z,
				            off);
				if (off > 40.0f) {
					fail(t, "EC-51: the geyser lands the captain at its target (within 40 units)", "%.1f away", off);
					return;
				}
				mHealth0          = navi->mHealth;
				const bool first  = navi->stimulate(InteractBury(navi, false, 10.0f));
				const int now     = navi->mStateMachine->getCurrID(navi);
				const float h1    = navi->mHealth;
				const bool second = navi->stimulate(InteractBury(navi, false, 10.0f));
				std::printf("[scenario] navi_states: tick %u bury: taken %d, state %d, health %.1f -> %.1f, second taken %d (health %.1f)\n", t,
				            (int)first, now, mHealth0, h1, (int)second, navi->mHealth);
				if (!first || now != NAVISTATE_Bury || std::fabs(mHealth0 - 10.0f - h1) > 0.01f) {
					fail(t, "EC-34: InteractBury in Walk enters Bury and takes 10", "taken %d, state %d, health %.1f -> %.1f", (int)first, now,
					     mHealth0, h1);
				} else if (second || navi->mHealth != h1) {
					fail(t, "EC-34: a second InteractBury in Bury is refused", "taken %d, health %.1f -> %.1f", (int)second, h1, navi->mHealth);
				} else {
					enter(Escape, t);
				}
			} else if (st != NAVISTATE_Geyzer) {
				fail(t, "EC-51: Geyzer ends in Walk", "state %d", st);
			} else if (s > 15.0f) {
				fail(t, "EC-51: Geyzer returns to Walk within 15 s", "state %d", st);
			}
			break;
		case Escape:
			if (st == NAVISTATE_Walk) {
				std::printf("[scenario] navi_states: out of Bury with no input after %.2f s (%u ticks)\n", s, t - mPhaseTick);
				// The 5 s escape timer, then the get-up motion: the same time at every tick rate.
				if (s < 5.0f || s > 7.0f) {
					fail(t, "EC-34: with no input the captain is out of Bury after the 5 s escape timer and his get-up (5 to 7 s)", "%.2f s", s);
					return;
				}
				navi->mHealth = 5.0f;
				const bool taken = navi->stimulate(InteractBury(navi, false, 10.0f));
				std::printf("[scenario] navi_states: tick %u lethal bury: taken %d, health %.1f\n", t, (int)taken, navi->mHealth);
				enter(Lethal, t);
			} else if (st != NAVISTATE_Bury) {
				fail(t, "EC-34: Bury ends in Walk", "state %d", st);
			} else if (s > 10.0f) {
				fail(t, "EC-34: Bury with no input returns to Walk within 10 s", "state %d", st);
			}
			break;
		case Lethal:
			if (st == NAVISTATE_Dead) {
				std::printf("[scenario] navi_states: Dead at %.2f s, health %.1f\n", s, navi->mHealth);
				mPhase = Done;
				scenarioPass();
			} else if (s > 1.0f) {
				fail(t, "EC-34: Bury at health <= 1 kills (Dead within 1 s)", "state %d, health %.1f", st, navi->mHealth);
			}
			break;
		default: break;
		}
	}

private:
	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

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

	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0;
	Vector3f mTarget;
	float mHealth0 = 0.0f;
};

} // namespace

PcScenario* pc_scenario_make_navi_states(void)
{
	static NaviStates s;
	return &s;
}

#endif
