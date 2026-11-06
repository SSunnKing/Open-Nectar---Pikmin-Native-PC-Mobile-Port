// whistle_radius: who a full whistle calls, and the Whistle Radius setting (coverage/edge-cases.md
// EC-39, EC-32 in part), on the sandbox stand-in stage.
//
// In order, each within a bound in seconds:
//   Away   The captain walks sideways for 2 s, clear of the ship.
//   Each round spawns Free blue Pikmin around the cursor (held at the captain's feet), each at its
//   own angle, at a fraction of the round's full radius R = NAVI_WHISTLE_MAX_RADIUS (mWhistleMaxRadius
//   times pc_settings_get_whistle_radius_pct / 100). They stand 1 s, then B is held for
//   mWhistleExpandTime + 0.5 s, so the circle grows to R and Navi::callPikis(R) runs, then released.
//   Within 2 s of the release:
//   - every Pikmin inside R (dist < R, the strict test) is in Formation with this captain;
//   - every Pikmin outside stays Free and within kStay of where it stood (EC-39: left behind).
//   Round 1  pct 100, Pikmin at 0.5, 0.8, 1.25 and 1.5 R.
//   Round 2  pct 50 (pc_settings_scenario_set_whistle_radius_pct, read live), so R is half:
//            Pikmin at 0.7, 0.9, 1.3 and 1.8 R, that is 0.35, 0.45, 0.65 and 0.9 of the full
//            radius. The last two are inside the full radius but outside the halved one, so they
//            stay Free.
// Every Pikmin is spawned at least kClear from the captain, so touching him does not call it.
// Nothing is poked: the stand-in Fue motion carries the disc's loop keys (#96), so its
// KEY_LoopStart sets NaviGatherState::mWhistleAnimPhase to 1 by itself. The hold requires Gather,
// that phase, and the circle at mode 2 (full, callPikis(R) run) within it.
//
// Not covered here: a partial whistle and X's disband (whistle_gather_release covers both), and
// Pikmin in the excluded states (Drown, Flick, Nukare, Swallowed, Absorb, LookAt, Pressed;
// piki_drown covers Drown).
//
// Negative controls: the Pikmin outside R (they would join if the radius were ignored), and round
// 2's Pikmin at 0.65 and 0.9 of the full radius (they would join if the setting did nothing).
// Manual (2026-10-31, local, not committed): with the setter call removed (R still computed as
// full x pct / 100) round 2 fails "EC-39: Pikmin outside the radius stay Free where they stood"
// (65.0 outside R 50.0: mode 1, moved 39.7) at 30 Hz.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Dolphin/pad.h"
#include "GameStat.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "NaviState.h"
#include "PikiState.h"
#include "scenarios/scenario_tutorial_common.h"
#include "settings/pc_settings.h"

namespace {

constexpr int kMaxPikis     = 4;
constexpr float kStay       = 20.0f;
constexpr float kClear      = 30.0f;
constexpr int kRounds       = 2;
constexpr int kPct[kRounds] = { 100, 50 };
// Fractions of the round's own R.
constexpr float kAt[kRounds][kMaxPikis] = { { 0.5f, 0.8f, 1.25f, 1.5f }, { 0.7f, 0.9f, 1.3f, 1.8f } };

float flat(const Vector3f& a, const Vector3f& b)
{
	const float dx = a.x - b.x, dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

class WhistleRadius : public PcScenario {
public:
	const char* name() const override { return "whistle_radius"; }

	void onStageReady() override
	{
		tutorial::applyDiscKeys();
		Navi* navi = naviMgr->getNavi();
		if (!navi || pikiMgr->getSize() != 0 || pc_settings_get_whistle_radius_pct() != 100) {
			fail(0, "a captain, no Pikmin and Whistle Radius 100%", "captain %d, Pikmin %d, pct %d", (int)(navi != nullptr), pikiMgr->getSize(),
			     pc_settings_get_whistle_radius_pct());
			return;
		}
		mFull = C_NAVI_PARM(navi, mWhistleMaxRadius);
		std::printf("[scenario] whistle_radius: radius %.1f..%.1f, expand %.2f s\n", C_NAVI_PARM(navi, mWhistleMinRadius), mFull,
		            C_NAVI_PARM(navi, mWhistleExpandTime));
		enter(Away, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		(void)t;
		if (mPhase == Away) pad.stickX = 100;
		if (mPhase == Hold) pad.button = PAD_BUTTON_B;
		if (mPhase == Stand || mPhase == Hold || mPhase == Check) {
			if (Navi* navi = naviMgr->getNavi()) {
				navi->mCursorPosition.set(0.0f, 0.0f, 0.0f);
				navi->mCursorTargetPosition = navi->mCursorPosition;
				navi->mCursorWorldPos       = navi->mSRT.t;
			}
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate         = unsigned(pc_scenario_ticks_per_second());
		Navi* navi    = naviMgr->getNavi();
		const float s = (t - mPhaseTick) / float(mRate);

		switch (mPhase) {
		case Away:
			if (s >= 2) enter(Settle, t);
			return;
		case Settle:
			if (navi->getCurrState()->getID() == NAVISTATE_Walk && speed(navi) < 1.0f) {
				pc_settings_scenario_set_whistle_radius_pct(kPct[mRound]);
				mR = NAVI_WHISTLE_MAX_RADIUS(navi);
				if (std::fabs(mR - mFull * kPct[mRound] / 100.0f) > 0.01f) {
					fail(t, "NAVI_WHISTLE_MAX_RADIUS is mWhistleMaxRadius times the setting", "%.2f at %d%%, full %.2f", mR, kPct[mRound], mFull);
					return;
				}
				mCentre = navi->mSRT.t;
				if (!spawn(navi)) return;
				enter(Stand, t);
			} else if (s > 5) {
				fail(t, "the captain standing in Walk within 5 s", "state %d, speed %.1f", navi->getCurrState()->getID(), speed(navi));
			}
			return;
		case Stand:
			if (s >= 1) {
				for (int i = 0; i < kMaxPikis; ++i) {
					if (mPiki[i]->mMode != PikiMode::FreeMode) {
						fail(t, "spawned Pikmin stay Free until the whistle", "round %d Pikmin %d mode %d", mRound, i, mPiki[i]->mMode);
						return;
					}
					mFrom[i] = mPiki[i]->mSRT.t;
				}
				enter(Hold, t);
			}
			return;
		case Hold:
			// No poke: the stand-in Fue's KEY_LoopStart sets the phase (read by exec that tick).
			if (navi->getCurrState()->getID() == NAVISTATE_Gather) {
				NaviGatherState* g = static_cast<NaviGatherState*>(navi->getCurrState());
				if (!mGatherTick) mGatherTick = t;
				if (g->mWhistleAnimPhase == 1 && !mLoopTick) mLoopTick = t;
				if (navi->mWhistleCircleMode == 2 && !mCalledTick) mCalledTick = t;
			}
			if (s >= C_NAVI_PARM(navi, mWhistleExpandTime) + 0.5f) {
				if (!mGatherTick || !mLoopTick || !mCalledTick) {
					fail(t, "B held: Gather, KEY_LoopStart by itself, and the circle full (mode 2, callPikis at R) within mWhistleExpandTime + 0.5 s",
					     "Gather tick %u, phase-1 tick %u, full tick %u, state %d", mGatherTick, mLoopTick, mCalledTick, navi->getCurrState()->getID());
					return;
				}
				std::printf("[scenario] whistle_radius: round %d: KEY_LoopStart %u ticks, circle full %.2f s after Gather\n", mRound, mLoopTick - mGatherTick,
				            (mCalledTick - mGatherTick) / float(mRate));
				mGatherTick = mLoopTick = mCalledTick = 0;
				enter(Check, t);
			}
			return;
		case Check: {
			if (s < 2) return;
			int joined = 0;
			for (int i = 0; i < kMaxPikis; ++i) {
				Piki* p          = mPiki[i];
				const float d    = flat(mFrom[i], mCentre);
				const bool in    = d < mR;
				const bool party = p->mMode == PikiMode::FormationMode && p->mNavi == navi;
				const float drift = flat(p->mSRT.t, mFrom[i]);
				std::printf("[scenario] whistle_radius: round %d (%d%%, R %.1f): Pikmin at %.1f (%.2f R, %.2f full): mode %d, state %d, moved %.1f\n", mRound,
				            kPct[mRound], mR, d, d / mR, d / mFull, p->mMode, p->getState(), drift);
				if (in && !party) {
					fail(t, "EC-39: a full whistle calls every Pikmin inside its radius", "round %d, %.1f inside R %.1f, mode %d", mRound, d, mR, p->mMode);
					return;
				}
				if (!in && (p->mMode != PikiMode::FreeMode || drift > kStay)) {
					fail(t, "EC-39: Pikmin outside the radius stay Free where they stood", "round %d, %.1f outside R %.1f: mode %d, moved %.1f", mRound, d, mR,
					     p->mMode, drift);
					return;
				}
				joined += party;
			}
			if (++mRound == kRounds) {
				pc_settings_scenario_set_whistle_radius_pct(100);
				mPhase = Done;
				scenarioPass();
				return;
			}
			(void)joined;
			enter(Settle, t);
			return;
		}
		default: return;
		}
	}

private:
	enum Phase { Away, Settle, Stand, Hold, Check, Done };

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
		pc_settings_scenario_set_whistle_radius_pct(100);
		scenarioFailf(t, expected, "%s", buf);
	}

	static float speed(Navi* navi) { return std::sqrt(navi->mVelocity.x * navi->mVelocity.x + navi->mVelocity.z * navi->mVelocity.z); }

	bool spawn(Navi* navi)
	{
		for (int i = 0; i < kMaxPikis; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (!piki) {
				fail(0, "Pikmin born", "birth returned null at %d", i);
				return false;
			}
			GameStat::workPikis.inc(Blue);
			piki->init(navi);
			// Round 2 half a step round, clear of round 1's leftovers.
			const float a = 6.2831853f * (i + 0.5f * mRound) / kMaxPikis;
			const float r = std::fmax(kAt[mRound][i] * mR, kClear);
			Vector3f at   = mCentre;
			at.x += r * std::cos(a);
			at.z += r * std::sin(a);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(Blue);
			piki->changeMode(PikiMode::FreeMode, navi);
			mPiki[i] = piki;
		}
		GameStat::update();
		return true;
	}

	Phase mPhase        = Away;
	unsigned mPhaseTick = 0, mRate = 30, mGatherTick = 0, mCalledTick = 0, mLoopTick = 0;
	int mRound          = 0;
	float mFull = 0, mR = 0;
	Vector3f mCentre, mFrom[kMaxPikis];
	Piki* mPiki[kMaxPikis] = {};
};

} // namespace

PcScenario* pc_scenario_make_whistle_radius(void)
{
	static WhistleRadius s;
	return &s;
}

#endif
