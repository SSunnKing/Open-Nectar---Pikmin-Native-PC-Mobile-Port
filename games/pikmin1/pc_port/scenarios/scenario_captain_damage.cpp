// captain_damage: the captain's non-lethal damage, the Flick state's
// invincibility, the OlimarLowHealth demo flag (DF29) and then the lethal hit
// (coverage/edge-cases.md EC-14 and EC-15; coverage/milestones.md DF29).
//
// Runs on the stand-in sandbox (stages/sandbox.ini). Every hit is the stimulus
// an enemy's flick sends: navi->stimulate(InteractFlick(owner, knockback,
// damage, angle)) with knockback 0, so the captain stays where he is.
//
// What the code does (GameCube original; the scenario characterises it, it
// fixes nothing):
//
//   InteractFlick::actNavi (navi.cpp:4732):
//     - returns false, and changes nothing, in PikiZero or when the state's
//       invincible() is true (navi.cpp:4733-4739);
//     - otherwise, for damage > 0, calls startDamageEffect() BEFORE the health
//       is lowered (navi.cpp:4741-4742), then mHealth -= pcNaviHurt(damage)
//       (navi.cpp:4749; 100 percent and no Hard mode here, so the damage
//       itself), sets mFlickIntensity and transits to NAVISTATE_Flick.
//   NaviFlickState (include/NaviState.h:404-411) is invincible: every hit that
//   arrives while the captain is in Flick is ignored, a lethal one included.
//   NaviWalkState is not: the first tick back in Walk takes a hit in full.
//   There is no grace period after the knock-back, and no counter: the
//   invincibility is exactly "the state is Flick" (the JHit, JKoke and GetUp
//   motions plus a random wait of up to 0.1 s, naviState.cpp:1534-1590).
//   NaviFlickState::procAnimMsg ends in Walk (health above 1) or Dead
//   (health at or below 1) (naviState.cpp:1578-1585). NaviDeadState::init sets
//   mHealth to 0 (naviState.cpp:3367), so a lethal hit reads -25 right after
//   the hit and 0 from Dead on.
//
//   DF29, DEMOFLAG_OlimarLowHealth (navi.cpp:441-445): in startDamageEffect,
//   when the movie player is idle and mHealth <= 0.25 * the health parameter
//   (100 by default, so 25: the comparison is "<=") and the flag is clear, the
//   flag is set and the tutorial text TUT_APunchUFO is requested. Because
//   startDamageEffect runs before the subtraction, the test reads the health
//   BEFORE the hit: the hit that brings the health under 25 does not set the
//   flag, the next hit does. That is the original's order (Navi::finishDamage,
//   navi.cpp:497-500, has the same test after the hit but only the unused
//   startDamage path reaches it).
//
// Hits, with the health before and after (damage, health parameter 100):
//   1: 30  100 -> 70    2: 30  70 -> 40    3: 15  40 -> 25    4: 1  25 -> 24
//   5: 19  24 -> 5      6: 30  5 -> -25 (lethal)
// Hit 3 leaves the health exactly on the threshold (25) and does not set the
// flag (it read 40). Hit 4 reads 25 and sets it: the boundary is inclusive.
//
// Asserted:
//   - hits 1 to 5 each lower the health by exactly the damage, put the captain
//     in Flick (8) on the next tick and bring him back to Walk (0), alive;
//   - during hit 1's Flick, a 1000-damage probe every tick is refused
//     (stimulate false, health and knock-back unchanged, state still Flick);
//   - on the first tick back in Walk, the next hit is applied in full;
//   - DF29 is clear after hits 1 to 3 and set after hit 4 (and stays set);
//   - the five undisturbed Flicks (all but hit 4, whose DF29 text window holds
//     the sim) last the same time in seconds to within 250 ms, at either rate;
//   - the lethal hit gives Flick then Dead (29) once, GameStat::orimaDead set,
//     health at or below 1; the scenario ends one second later with Dead
//     entered once (the NaviDown chain is captain_death's).
//
// Bounds are in seconds (so the same at 30 and 60 Hz); the tick counts are
// printed. Which assertions depend on what:
//   - code constants: the health arithmetic, the DF29 threshold and order, the
//     Flick invincibility, "back in Walk takes a hit", Dead on health <= 1,
//     Dead entered once;
//   - stand-in animation lengths (invented, the disc's differ): how long Flick
//     lasts (JHit + JKoke + GetUp), checked only against the wide bound of
//     0.5 to 8 s, and the time from the lethal hit to Dead.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "Demo.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "PlayerState.h"
#include "Section.h"
#include "gameflow.h"
#include "system.h"

namespace {

constexpr unsigned kFirstHitTick = 10;
// The Flick state: JHit + JKoke + GetUp on the stand-in animations, plus a
// random wait of up to 0.1 s. Wide on purpose; the measured value is printed.
constexpr unsigned kMinFlickMilli = 500;
constexpr unsigned kMaxFlickMilli = 8000;
constexpr unsigned kToDeadSeconds = 8; // lethal hit to Dead (captain_death measures about 4 s)
constexpr unsigned kDeadHoldSeconds = 1;
constexpr unsigned kFlickSpreadMilli = 250;

struct Hit {
	float damage;
	float before, after; // health around the hit
	bool flagBefore;     // DF29 before the hit
	bool flagAfter;      // DF29 after the hit
};
// Hits 1 to 5 are non-lethal, hit 6 is lethal.
constexpr Hit kHits[] = {
	{ 30.0f, 100.0f, 70.0f, false, false }, { 30.0f, 70.0f, 40.0f, false, false }, { 15.0f, 40.0f, 25.0f, false, false },
	{ 1.0f, 25.0f, 24.0f, false, true },    { 19.0f, 24.0f, 5.0f, true, true },    { 30.0f, 5.0f, -25.0f, true, true },
};
constexpr int kNonLethal = 5;
constexpr int kHitCount  = 6;

enum Phase { Idle, Flicking, Dying, Hold, Done };

class CaptainDamage : public PcScenario {
public:
	const char* name() const override { return "captain_damage"; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// DF29 opens the TUT_APunchUFO text window; A closes it (as day30_clock does).
		if (gameflow.mIsUIOverlayActive && t % 6 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr;
		if (!navi || !navi->getCurrState()) {
			scenarioFailf(t, "a captain with a state", "none");
			return;
		}
		const int cap = navi->getCurrState()->getID();
		if (cap == NAVISTATE_Dead && mLastCap != NAVISTATE_Dead) mDeadEntries++;
		mLastCap = cap;
		if (mDeadEntries > 1) {
			scenarioFailf(t, "the Dead state is entered once", "entered %d times", mDeadEntries);
			return;
		}
		const bool flag = playerState->mDemoFlags.isFlag(DEMOFLAG_OlimarLowHealth);

		switch (mPhase) {
		case Idle:
			if (t < kFirstHitTick) return;
			if (cap != NAVISTATE_Walk) {
				// Walk after a Flick, not a state in between.
				if (mHit == 0) scenarioFailf(t, "the captain in Walk at the first hit", "state %d", cap);
				else scenarioFailf(t, "the captain in Walk when the next hit is due", "state %d", cap);
				return;
			}
			hit(t, navi, flag);
			return;
		case Flicking:
			// The Flick state is entered by the hit (the transit runs on the next update).
			if (!mSawFlick) {
				if (cap != NAVISTATE_Flick) {
					if (t - mHitTick > 2) scenarioFailf(t, "Flick (8) within 2 ticks of the hit", "state %d", cap);
					return;
				}
				mSawFlick  = true;
				mFlickTick = t;
				std::printf("[scenario] captain_damage hit %d: Flick at t=%u (hit at t=%u), health %.1f\n", mHit + 1, t, mHitTick, navi->mHealth);
			}
			if (cap == NAVISTATE_Flick) {
				probe(t, navi);
				if (t - mFlickTick > kMaxFlickMilli * rate / 1000) {
					scenarioFailf(t, "Flick ends within 8 s", "still in Flick after %u ticks", t - mFlickTick);
				}
				return;
			}
			// Out of Flick.
			{
				const unsigned ticks = t - mFlickTick;
				const unsigned milli = ticks * 1000 / rate;
				std::printf("[scenario] captain_damage hit %d: Flick lasted %u ticks (%u ms at %u Hz), then state %d at t=%u, probes refused %u\n", mHit + 1,
				            ticks, milli, rate, cap, t, mProbes);
				if (milli < kMinFlickMilli) {
					scenarioFailf(t, "Flick lasts at least 0.5 s", "%u ms", milli);
					return;
				}
				// Hit 4 opens the DF29 text window, which holds the sim until A closes it: left out.
				if (mHit != 3) noteFlick(milli);
				if (cap != NAVISTATE_Walk) {
					scenarioFailf(t, "the captain returns to Walk (0) from a non-lethal Flick", "state %d, health %.1f", cap, navi->mHealth);
					return;
				}
				if (navi->mHealth != kHits[mHit].after) {
					scenarioFailf(t, "health unchanged through the Flick", "%.1f, expected %.1f", navi->mHealth, kHits[mHit].after);
					return;
				}
				if (mHit == 0 && mProbes == 0) {
					scenarioFailf(t, "the invincibility probes ran during hit 1's Flick", "none ran");
					return;
				}
				mHit++;
				mPhase = Idle;
				if (mHit >= kHitCount) return;
				// The next hit goes in on this very tick: Walk takes it in full.
				hit(t, navi, flag);
			}
			return;
		case Dying:
			if (cap == NAVISTATE_Dead) {
				if (!GameStat::orimaDead) {
					scenarioFailf(t, "GameStat::orimaDead set when the captain is down", "false");
					return;
				}
				if (navi->mHealth > 1.0f) {
					scenarioFailf(t, "health at or below 1 at Dead", "%.1f", navi->mHealth);
					return;
				}
				std::printf("[scenario] captain_damage lethal hit at t=%u, Dead at t=%u (%u ticks, %u ms at %u Hz), health %.1f, Flick %u ticks\n",
				            mHitTick, t, t - mHitTick, (t - mHitTick) * 1000 / rate, rate, navi->mHealth, mFlickTick ? t - mFlickTick : 0);
				if (mFlickTick) noteFlick((t - mFlickTick) * 1000 / rate);
				mPhase     = Hold;
				mPhaseTick = t;
			} else if (cap == NAVISTATE_Walk && t - mHitTick > 2) {
				scenarioFailf(t, "the lethal hit goes Flick then Dead", "back in Walk, health %.1f", navi->mHealth);
			} else if (cap == NAVISTATE_Flick) {
				if (!mSawFlick) {
					mSawFlick  = true;
					mFlickTick = t;
				}
			} else if (t - mHitTick > kToDeadSeconds * rate) {
				scenarioFailf(t, "Dead (29) within 8 s of the lethal hit", "state %d", cap);
			}
			return;
		case Hold:
			if (cap != NAVISTATE_Dead) {
				scenarioFailf(t, "the captain stays Dead", "state %d", cap);
				return;
			}
			if (t - mPhaseTick >= kDeadHoldSeconds * rate) {
				if (mDeadEntries != 1) {
					scenarioFailf(t, "Dead entered once", "%d", mDeadEntries);
					return;
				}
				// The Flick has no fixed length (motions plus a random wait of up to 0.1 s, in
				// seconds), so at either rate the five undisturbed ones agree to a few ticks.
				if (mFlickMax - mFlickMin > kFlickSpreadMilli) {
					scenarioFailf(t, "the Flick lasts the same time in seconds on every hit (to 250 ms)", "%u to %u ms", mFlickMin, mFlickMax);
					return;
				}
				std::printf("[scenario] captain_damage done at t=%u, Dead entered once, DF29 %d, Flick %u to %u ms over 5 hits\n", t, (int)flag, mFlickMin,
				            mFlickMax);
				mPhase = Done;
				scenarioPass();
			}
			return;
		case Done: return;
		}
	}

private:
	// Sends hit mHit and checks its immediate effect.
	void hit(unsigned t, Navi* navi, bool flag)
	{
		const Hit& h = kHits[mHit];
		if (navi->mHealth != h.before) {
			scenarioFailf(t, "health as planned before the hit", "hit %d: %.1f, expected %.1f", mHit + 1, navi->mHealth, h.before);
			return;
		}
		if (flag != h.flagBefore) {
			scenarioFailf(t, "DF29 as planned before the hit", "hit %d: flag %d, expected %d", mHit + 1, (int)flag, (int)h.flagBefore);
			return;
		}
		const bool took = navi->stimulate(InteractFlick(navi, 0.0f, h.damage, 0.0f));
		if (!took) {
			scenarioFailf(t, "the hit is applied", "hit %d refused (state %d)", mHit + 1, navi->getCurrState()->getID());
			return;
		}
		if (navi->mHealth != h.after) {
			scenarioFailf(t, "health lowered by exactly the damage", "hit %d: %.1f, expected %.1f", mHit + 1, navi->mHealth, h.after);
			return;
		}
		const bool flagNow = playerState->mDemoFlags.isFlag(DEMOFLAG_OlimarLowHealth);
		if (flagNow != h.flagAfter) {
			scenarioFailf(t, "DF29 set after this hit exactly as the original orders it", "hit %d: flag %d, expected %d (health %.1f)", mHit + 1,
			              (int)flagNow, (int)h.flagAfter, navi->mHealth);
			return;
		}
		std::printf("[scenario] captain_damage hit %d at t=%u: damage %.0f, health %.1f -> %.1f, DF29 %d -> %d, in Walk (no grace period)\n", mHit + 1, t,
		            h.damage, h.before, navi->mHealth, (int)h.flagBefore, (int)flagNow);
		mHitTick  = t;
		mSawFlick = false;
		mProbes   = 0;
		mPhase    = (mHit == kHitCount - 1) ? Dying : Flicking;
		if (mHit == kHitCount - 1) mFlickTick = 0;
	}

	void noteFlick(unsigned milli)
	{
		if (milli < mFlickMin) mFlickMin = milli;
		if (milli > mFlickMax) mFlickMax = milli;
	}

	// During hit 1's Flick: a lethal probe every tick must be refused and change nothing.
	void probe(unsigned t, Navi* navi)
	{
		if (mHit != 0) return;
		const float health = navi->mHealth;
		const float flick  = navi->mFlickIntensity;
		const bool took    = navi->stimulate(InteractFlick(navi, 777.0f, 1000.0f, 0.0f));
		if (took) {
			scenarioFailf(t, "a hit in Flick is refused (NaviFlickState::invincible)", "applied: health %.1f -> %.1f", health, navi->mHealth);
			return;
		}
		if (navi->mHealth != health || navi->mFlickIntensity != flick) {
			scenarioFailf(t, "a refused hit changes nothing", "health %.1f -> %.1f, knock-back %.1f -> %.1f", health, navi->mHealth, flick, navi->mFlickIntensity);
			return;
		}
		if (navi->getCurrState()->getID() != NAVISTATE_Flick) {
			scenarioFailf(t, "a refused hit leaves the state", "state %d", navi->getCurrState()->getID());
			return;
		}
		mProbes++;
	}

	Phase mPhase        = Idle;
	unsigned mPhaseTick = 0, mHitTick = 0, mFlickTick = 0, mProbes = 0;
	unsigned mFlickMin = 1000000, mFlickMax = 0;
	bool mSawFlick      = false;
	int mHit = 0, mDeadEntries = 0, mLastCap = -2;
};

} // namespace

PcScenario* pc_scenario_make_captain_damage(void)
{
	static CaptainDamage s;
	return &s;
}

#endif
