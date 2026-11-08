// coop_smoke and coop_look: local co-op in split screen, two captains, pad 0
// driving the first and pad 1 the second (PcScenario::coop, providePad1).
//
// A co-op scenario arms co-op as the title's 2P choice does (pending co-op,
// Olimar and Louie), so GameCoreSection builds two captains and the second
// camera. The runner also turns off the netplay two-pass presentation for it
// (pc_netplay_present_scenario_split_views): deterministic mode otherwise draws
// one full-screen view of the local captain, and the split screen's two views
// per tick (newPikiGame.cpp, the mSplitViews loop) never run. The sim stays
// deterministic (fixed tick, the sim RNG, the visibility pins); only the draw is
// the one ordinary local co-op uses. Every tick both scenarios check that this
// holds: co-op active, two captains, the second camera manager, separate
// cameras, the two-pass presentation off, and no movie.
//
// coop_smoke: both captains move as driven, each by its own pad only. Phase one
// (2 s): pad 0 pushes east, pad 1 north. Each captain must travel at least 40
// units in its own direction (and less than half that across it). Phase two
// (2 s): pad 0 is released and pad 1 pushes south, back over the ground the
// second captain just crossed (pushed west from there it went 144 degrees, not
// 180: something on the sandbox deflects it; not investigated). From
// 0.5 s into the phase (the first captain's slide-out) the first captain moves
// less than 5 units and the second at least 30 units south. Both captains end
// in Walk.
//
// coop_look: the split-screen head-look fix (c53ce41e, upstream PR #228). The
// world draw runs once per view, and the head look (updateLook: head turn,
// mLookTimer, the end of a look) runs inside it, so it advanced twice per tick.
// pc_coop_first_view_pass() now lets it run in the first view only. Subjects:
// both captains (Navi::updateHeadMatrix) and one blue Pikmin in Free mode
// (ViewPiki::refresh). Once all three are idle and not looking, each gets a look
// at a fixed point with mLookTimer = 12; after every tick the timer must have
// dropped by exactly one, and on the twelfth tick the look must end
// (forceFinishLook: no target, timer 0). Two rounds. Without the gate the timers
// drop by two per tick and the first check fails (negative control in the
// README). 30 Hz only: the timer counts calls, not time.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <string>

#include "GameStat.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Pcam/CameraManager.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "gameflow.h"
#include "netplay/pc_netplay_present.h"
#include "pc_coop.h"

namespace {

/// The split-screen preconditions every co-op scenario holds on every tick.
bool checkSplitScreen(unsigned t, const char* scenario)
{
	Navi* n0 = naviMgr ? naviMgr->getNavi(0) : nullptr;
	Navi* n1 = naviMgr && naviMgr->getNaviCount() > 1 ? naviMgr->getNavi(1) : nullptr;
	const bool movie = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
	if (!pc_coop_active() || !n0 || !n1 || naviMgr->getNaviCount() != 2 || !cameraMgrP2 || n0->mNaviCamera == n1->mNaviCamera
	    || pc_netplay_present_two_pass_active() != 0 || movie) {
		scenarioFailf(t, "split screen: co-op active, two captains with their own cameras, the second camera manager, the two-pass presentation off, no movie",
		              "%s: coop %d captains %d cameraMgrP2 %d separate cameras %d two-pass %d movie %d", scenario, (int)pc_coop_active(),
		              naviMgr ? naviMgr->getNaviCount() : -1, cameraMgrP2 != nullptr, n0 && n1 && n0->mNaviCamera != n1->mNaviCamera,
		              pc_netplay_present_two_pass_active(), (int)movie);
		return false;
	}
	return true;
}

class CoopSmoke : public PcScenario {
public:
	const char* name() const override { return "coop_smoke"; }
	bool coop() const override { return true; }

	void onStageReady() override
	{
		if (!checkSplitScreen(0, name())) mDone = true;
	}

	void providePads(unsigned, PcInputPad& pad0) override
	{
		if (mPhase == Drive1) pad0.stickX = 60; // east (+x) on the neutral control basis
	}

	void providePad1(unsigned, PcInputPad& pad1) override
	{
		if (mPhase == Drive1) pad1.stickY = 60;  // north (-z)
		if (mPhase == Drive2) pad1.stickY = -60; // south (+z), back over the ground it crossed
	}

	void onTick(unsigned t) override
	{
		if (mDone || !checkSplitScreen(t, name())) {
			mDone = true;
			return;
		}
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		Navi* n[2]          = { naviMgr->getNavi(0), naviMgr->getNavi(1) };
		const int s0 = n[0]->getCurrState()->getID(), s1 = n[1]->getCurrState()->getID();
		const unsigned span = t - mPhaseTick;
		switch (mPhase) {
		case Settle:
			if (s0 == NAVISTATE_Walk && s1 == NAVISTATE_Walk) {
				std::printf("[scenario] coop_smoke: both captains in Walk at tick %u, captain 1 at (%.1f, %.1f), captain 2 at (%.1f, %.1f)\n", t, n[0]->mSRT.t.x,
				            n[0]->mSRT.t.z, n[1]->mSRT.t.x, n[1]->mSRT.t.z);
				begin(Drive1, t, n);
			} else if (span > 5 * rate) {
				fail(t, "both captains in Walk within 5 s", "captain states %d and %d", s0, s1);
			}
			return;
		case Drive1:
			if (span + 1 >= 2 * rate) {
				const Vector3f d0 = n[0]->mSRT.t - mStart[0], d1 = n[1]->mSRT.t - mStart[1];
				std::printf("[scenario] coop_smoke: after 2 s, captain 1 moved (%.1f, %.1f), captain 2 moved (%.1f, %.1f)\n", d0.x, d0.z, d1.x, d1.z);
				if (!(d0.x >= 40.0f && std::fabs(d0.z) < d0.x / 2.0f)) {
					fail(t, "pad 0 east moves captain 1 at least 40 units east", "moved (%.1f, %.1f)", d0.x, d0.z);
					return;
				}
				if (!(-d1.z >= 40.0f && std::fabs(d1.x) < -d1.z / 2.0f)) {
					fail(t, "pad 1 north moves captain 2 at least 40 units north (-z)", "moved (%.1f, %.1f)", d1.x, d1.z);
					return;
				}
				begin(Drive2, t, n);
			}
			return;
		case Drive2:
			if (span + 1 == rate / 2) {
				mStart[0] = n[0]->mSRT.t; // after the first captain's slide-out
				mStart[1] = n[1]->mSRT.t;
			}
			if (span + 1 >= 2 * rate) {
				const Vector3f d0 = n[0]->mSRT.t - mStart[0], d1 = n[1]->mSRT.t - mStart[1];
				const float moved0 = std::sqrt(d0.x * d0.x + d0.z * d0.z);
				std::printf("[scenario] coop_smoke: last 1.5 s, captain 1 moved (%.1f, %.1f), captain 2 moved (%.1f, %.1f), states %d %d\n", d0.x, d0.z, d1.x, d1.z,
				            s0, s1);
				if (moved0 >= 5.0f) {
					fail(t, "with pad 0 released captain 1 stays put while pad 1 drives captain 2", "captain 1 moved %.1f units", moved0);
					return;
				}
				if (!(d1.z >= 30.0f && std::fabs(d1.x) < d1.z / 2.0f)) {
					fail(t, "pad 1 south moves captain 2 at least 30 units south (+z)", "moved (%.1f, %.1f)", d1.x, d1.z);
					return;
				}
				if (s0 != NAVISTATE_Walk || s1 != NAVISTATE_Walk) {
					fail(t, "both captains end in Walk", "captain states %d and %d", s0, s1);
					return;
				}
				mDone = true;
				scenarioPass();
			}
			return;
		}
	}

private:
	enum Phase { Settle, Drive1, Drive2 };

	void begin(Phase p, unsigned t, Navi* const n[2])
	{
		mPhase     = p;
		mPhaseTick = t;
		mStart[0]  = n[0]->mSRT.t;
		mStart[1]  = n[1]->mSRT.t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mDone = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0;
	Vector3f mStart[2];
	bool mDone = false;
};

class CoopLook : public PcScenario {
public:
	const char* name() const override { return "coop_look"; }
	bool coop() const override { return true; }

	void onStageReady() override
	{
		if (!checkSplitScreen(0, name())) {
			mDone = true;
			return;
		}
		// One blue Pikmin in Free mode, away from both captains, born as the game's spawners do (see day_end).
		Navi* navi = naviMgr->getNavi(0);
		mPiki      = static_cast<Piki*>(pikiMgr->birth());
		if (!mPiki) {
			fail(0, "a Pikmin is born", "birth returned null");
			return;
		}
		GameStat::workPikis.inc(Blue);
		mPiki->init(navi);
		Vector3f at = navi->mSRT.t;
		at.x -= 60.0f;
		at.z += 60.0f;
		at.y = mapMgr->getMinY(at.x, at.z, true);
		mPiki->Creature::init(at);
		mPiki->initColor(Blue);
		mPiki->changeMode(PikiMode::FreeMode, navi);
	}

	void onTick(unsigned t) override
	{
		if (mDone || !checkSplitScreen(t, name())) {
			mDone = true;
			return;
		}
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		Navi* n[2]          = { naviMgr->getNavi(0), naviMgr->getNavi(1) };
		switch (mPhase) {
		case Settle:
		{
			// The captains start no look of their own in Walk; the Pikmin looks around
			// whenever it idles (Piki::startMotion, startHimaLook), so its look is replaced.
			const int s0 = n[0]->getCurrState()->getID(), s1 = n[1]->getCurrState()->getID();
			const bool walk = s0 == NAVISTATE_Walk && s1 == NAVISTATE_Walk;
			const bool idle = !n[0]->mLookAtPosPtr && !n[0]->mLookTimer && !n[1]->mLookAtPosPtr && !n[1]->mLookTimer && mPiki->getState() == PIKISTATE_Normal;
			mIdleTicks = walk && idle ? mIdleTicks + 1 : 0;
			if (mIdleTicks >= rate / 2) {
				startLooks(t, n);
			} else if (t - mPhaseTick > 10 * rate) {
				fail(t, "both captains in Walk, not looking, and the Pikmin in Normal within 10 s", "captain states %d %d, captain looks %d/%u %d/%u, Pikmin state %d",
				     s0, s1, n[0]->mLookAtPosPtr != nullptr, (unsigned)n[0]->mLookTimer, n[1]->mLookAtPosPtr != nullptr, (unsigned)n[1]->mLookTimer,
				     mPiki->getState());
			}
			return;
		}
		case Looking:
		{
			const unsigned k = t - mLookTick;
			const char* who[3] = { "captain 1", "captain 2", "Pikmin" };
			immut Vector3f* pos[3] = { n[0]->mLookAtPosPtr, n[1]->mLookAtPosPtr, mPiki->mLookatPosPtr };
			const unsigned timer[3] = { n[0]->mLookTimer, n[1]->mLookTimer, mPiki->mLookTimer };
			// Every subject is judged before failing, so one line names each that is off.
			std::string off;
			for (int i = 0; i < 3; i++) {
				const bool ok = k < kTimer ? pos[i] == &mTarget[i] && timer[i] == kTimer - k : pos[i] == nullptr && timer[i] == 0;
				if (ok) continue;
				char buf[96];
				std::snprintf(buf, sizeof buf, "%s%s timer %u, target %s", off.empty() ? "" : "; ", who[i], timer[i],
				              pos[i] == &mTarget[i] ? "kept" : pos[i] ? "replaced" : "cleared");
				off += buf;
			}
			if (!off.empty()) {
				if (k < kTimer) {
					fail(t, "in split screen the look advances once per tick: mLookTimer drops by 1 per tick and the look target stays",
					     "round %d, %u ticks after a start at %u, expected timer %u: %s", mRound + 1, k, kTimer, kTimer - k, off.c_str());
				} else {
					fail(t, "the look ends on its twelfth tick (forceFinishLook: no target, timer 0)", "round %d: %s", mRound + 1, off.c_str());
				}
				return;
			}
			if (k < kTimer) return;
			std::printf("[scenario] coop_look: round %d, the three looks ended on tick %u after %u ticks, one step per tick\n", mRound + 1, t, kTimer);
			if (++mRound == kRounds) {
				mDone = true;
				scenarioPass();
				return;
			}
			mPhase     = Settle;
			mPhaseTick = t;
			mIdleTicks = 0;
			return;
		}
		}
	}

private:
	enum Phase { Settle, Looking };
	static constexpr unsigned kTimer = 12;
	static constexpr int kRounds     = 2;

	void startLooks(unsigned t, Navi* const n[2])
	{
		// Up and to the side of each subject, within the head's turn limits or not: updateLook counts the timer either way.
		mTarget[0] = n[0]->mSRT.t + Vector3f(40.0f, 30.0f, 40.0f);
		mTarget[1] = n[1]->mSRT.t + Vector3f(-40.0f, 30.0f, 40.0f);
		mTarget[2] = mPiki->mSRT.t + Vector3f(40.0f, 20.0f, -40.0f);
		n[0]->startLook(&mTarget[0]);
		n[1]->startLook(&mTarget[1]);
		mPiki->forceFinishLook(); // ends its own idle look first
		mPiki->startLook(&mTarget[2]);
		n[0]->mLookTimer = n[1]->mLookTimer = mPiki->mLookTimer = (u8)kTimer;
		std::printf("[scenario] coop_look: round %d, looks started at tick %u with mLookTimer %u\n", mRound + 1, t, kTimer);
		mPhase    = Looking;
		mLookTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mDone = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Settle;
	unsigned mIdleTicks = 0, mLookTick = 0, mPhaseTick = 0;
	int mRound          = 0;
	Vector3f mTarget[3];
	Piki* mPiki = nullptr;
	bool mDone  = false;
};

} // namespace

PcScenario* pc_scenario_make_coop_smoke(void)
{
	static CoopSmoke s;
	return &s;
}

PcScenario* pc_scenario_make_coop_look(void)
{
	static CoopLook s;
	return &s;
}

#endif
