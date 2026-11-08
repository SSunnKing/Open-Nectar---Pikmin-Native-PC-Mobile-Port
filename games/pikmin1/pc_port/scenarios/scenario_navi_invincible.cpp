// navi_invincible: the captain's per-state invincibility table and what it blocks
// (coverage/edge-cases.md EC-37), on the sandbox stand-in stage.
//
// What the code does (GameCube original; characterised, nothing fixed): NaviState::invincible is
// false by default (include/NaviState.h) and true in 14 states: Bury, Container, Dead, DemoInf,
// DemoWait, Flick, Geyzer, Nuku, PartsAccess, Pellet, PikiZero, Pressed, UfoAccess and Ufo.
// InteractFlick, InteractPress, InteractSuck, InteractBury, InteractBomb, InteractBubble,
// InteractFire and InteractWind return false and change nothing in those states (navi.cpp
// Interact*::actNavi); InteractAttack and InteractSwallow refuse through Navi::startDamage, which
// tests the same virtual.
//
// In order, each within a bound in seconds:
//   Table   Every registered state's invincible() matches the list above (kInvincible).
//   Settle  The captain stands in Walk (within 5 s).
//   Press   InteractPress(10) in Walk is taken: health 100 -> 90, then the captain is in Pressed.
//   Refuse  In Pressed, InteractPress(10), InteractFlick(1000), InteractSuck(10) and
//           InteractBury(10) each return false; health and state are unchanged.
//   Back    Pressed ends in Walk within mPressedTotalTime + 0.5 s, health still 90.
//   Walk    InteractSuck(10) in Walk is taken (health 80) and the captain stays in Walk.
// Flick's own invincibility (a lethal hit during Flick) is captain_damage's (EC-14, EC-15).
//
// Negative controls: Press and Walk take the same interactions in Walk that Refuse requires to be
// refused, so a table that never refused (or always did) fails one or the other.
// Manual (2026-10-31, local, not committed): with Nuku dropped from kInvincible it fails at tick 0
// "invincible() is true in exactly the listed states" (state 5: 1); with the Walk press removed
// and the Refuse hits sent in Walk it fails "Pressed refuses every hit" (press 1, health
// 100.0 -> 90.0), both at 30 Hz.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Interactions.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"

namespace {

constexpr int kInvincible[] = { NAVISTATE_Bury,      NAVISTATE_Container, NAVISTATE_Dead,      NAVISTATE_DemoInf,     NAVISTATE_DemoWait,
	                            NAVISTATE_Flick,     NAVISTATE_Geyzer,    NAVISTATE_Nuku,      NAVISTATE_PartsAccess, NAVISTATE_Pellet,
	                            NAVISTATE_PikiZero,  NAVISTATE_Pressed,   NAVISTATE_UfoAccess, NAVISTATE_Ufo };

bool listed(int id)
{
	for (int s : kInvincible) {
		if (s == id) return true;
	}
	return false;
}

class NaviInvincible : public PcScenario {
public:
	const char* name() const override { return "navi_invincible"; }

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi) {
			fail(0, "a captain", "none");
			return;
		}
		NaviStateMachine* sm = navi->mStateMachine;
		int on = 0;
		for (int i = 0; i < sm->mStateCount; ++i) {
			NaviState* st  = static_cast<NaviState*>(sm->mStates[i]);
			const bool inv = st->invincible(navi);
			if (inv != listed(st->getID())) {
				fail(0, "EC-37: invincible() is true in exactly the listed states", "state %d: %d", st->getID(), (int)inv);
				return;
			}
			on += inv;
		}
		std::printf("[scenario] navi_invincible: %d registered states, %d invincible\n", sm->mStateCount, on);
		if (on != int(sizeof(kInvincible) / sizeof(kInvincible[0]))) {
			fail(0, "every listed state registered", "%d of %d invincible states registered", on, int(sizeof(kInvincible) / sizeof(kInvincible[0])));
			return;
		}
		enter(Settle, 0);
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate         = unsigned(pc_scenario_ticks_per_second());
		Navi* navi    = naviMgr->getNavi();
		const int id  = navi->getCurrState()->getID();
		const float s = (t - mPhaseTick) / float(mRate);

		switch (mPhase) {
		case Settle:
			if (id == NAVISTATE_Walk && s >= 1) {
				navi->mHealth = C_NAVI_PARM(navi, mHealth);
				if (!take(t, navi, "Press", InteractPress(navi, 10.0f), C_NAVI_PARM(navi, mHealth) - 10.0f)) return;
				enter(Press, t);
			} else if (s > 5) {
				fail(t, "the captain in Walk within 5 s", "state %d", id);
			}
			return;
		case Press:
			if (id == NAVISTATE_Pressed) {
				const float h = navi->mHealth;
				const bool a  = navi->stimulate(InteractPress(navi, 10.0f));
				const bool b  = navi->stimulate(InteractFlick(navi, 0.0f, 1000.0f, 0.0f));
				const bool c  = navi->stimulate(InteractSuck(navi, 10.0f));
				const bool d  = navi->stimulate(InteractBury(navi, false, 10.0f));
				const int now = navi->getCurrState()->getID();
				std::printf("[scenario] navi_invincible: in Pressed: press %d, flick %d, suck %d, bury %d, health %.1f -> %.1f, state %d\n", (int)a, (int)b,
				            (int)c, (int)d, h, navi->mHealth, now);
				if (a || b || c || d || navi->mHealth != h || now != NAVISTATE_Pressed) {
					fail(t, "EC-37: Pressed refuses every hit and changes nothing", "press %d, flick %d, suck %d, bury %d, health %.1f -> %.1f, state %d",
					     (int)a, (int)b, (int)c, (int)d, h, navi->mHealth, now);
					return;
				}
				enter(Back, t);
			} else if (s > 0.5f) {
				fail(t, "InteractPress puts the captain in Pressed within 0.5 s", "state %d", id);
			}
			return;
		case Back:
			if (id == NAVISTATE_Walk) {
				std::printf("[scenario] navi_invincible: Pressed to Walk in %.2f s (mPressedTotalTime %.2f), health %.1f\n", s,
				            C_NAVI_PARM(navi, mPressedTotalTime), navi->mHealth);
				if (navi->mHealth != C_NAVI_PARM(navi, mHealth) - 10.0f) {
					fail(t, "health unchanged through Pressed", "%.1f", navi->mHealth);
					return;
				}
				if (!take(t, navi, "Suck", InteractSuck(navi, 10.0f), C_NAVI_PARM(navi, mHealth) - 20.0f)) return;
				enter(Walk, t);
			} else if (s > C_NAVI_PARM(navi, mPressedTotalTime) + 0.5f) {
				fail(t, "Pressed ends in Walk within mPressedTotalTime + 0.5 s", "state %d after %.2f s", id, s);
			}
			return;
		case Walk:
			if (s >= 0.5f) {
				if (id != NAVISTATE_Walk) {
					fail(t, "InteractSuck leaves the captain in Walk", "state %d", id);
					return;
				}
				mPhase = Done;
				scenarioPass();
			}
			return;
		default: return;
		}
	}

private:
	enum Phase { Settle, Press, Back, Walk, Done };

	bool take(unsigned t, Navi* navi, const char* what, const Interaction& hit, float want)
	{
		const float h = navi->mHealth;
		const bool ok = navi->stimulate(hit);
		std::printf("[scenario] navi_invincible: %s in Walk: taken %d, health %.1f -> %.1f\n", what, (int)ok, h, navi->mHealth);
		if (!ok || std::fabs(navi->mHealth - want) > 0.01f) {
			fail(t, "EC-37: Walk is not invincible: the hit is taken in full", "%s: taken %d, health %.1f -> %.1f, expected %.1f", what, (int)ok, h,
			     navi->mHealth, want);
			return false;
		}
		return true;
	}

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
	unsigned mPhaseTick = 0, mRate = 30;
};

} // namespace

PcScenario* pc_scenario_make_navi_invincible(void)
{
	static NaviInvincible s;
	return &s;
}

#endif
