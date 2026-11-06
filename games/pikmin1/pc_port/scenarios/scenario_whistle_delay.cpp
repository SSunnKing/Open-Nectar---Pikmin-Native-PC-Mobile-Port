// whistle_delay: how long a called Pikmin takes to join the squad (issue #133), measured the same
// way on the stand-in tree and on a disc install (a stage the scenario is given, as throw_release).
//
// Six Free Pikmin stand 20 to 60 units from the captain, inside the whistle radius. B is held until
// the circle is full and 0.3 s more. For each Pikmin the scenario records the first tick it is in
// PikiLookAtState (the call; Navi::callPikis sends a Free Pikmin there) and the first tick it is
// in Formation and out of LookAt (it joined). The delay is the difference; LookAt's own duration
// (the random 0 to 0.3 s wait, then the Kizuku motion, then the return) is most of it.
//
// Asserted: every Pikmin is called, plays Kizuku in LookAt, and joins 0.45 to 1 s after the call (the
// disc: 0.50 to 0.75 s at 30 and 60 Hz; the stand-ins' Kizuku motion carries the disc's timing since
// #133, standin_anims.h). The delays are printed; the disc run is made by hand, with
// PIKMIN_SCENARIO_DATA_DIR on a disc install.
//
// Issue #145: LookAt takes any KEY_Finished as Kizuku's end, including one from the motion the
// Pikmin was already playing when called (PikiLookAtState::procAnimMsg sets LAS_Returning during
// the random delay, and the leftover delay then ends LookAt). On the disc an idle Free Pikmin plays
// Wait, which loops and never sends one; the stand-in Wait was a 40-frame one-shot until #145, and
// at 30 Hz three of six Pikmin had it end inside the delay, skipped Kizuku and joined in 0.07 to
// 0.20 s. Kizuku alone is 15 frames (0.5 s), so the lower bound and the Kizuku check catch that.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Dolphin/pad.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "PaniPikiAnimator.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "scenarios/scenario_tutorial_common.h"

namespace {

constexpr int kCount     = 6;
constexpr float kMaxJoin = 1.0f;
constexpr float kMinJoin = 0.45f; // Kizuku is 15 frames at speed 30: 0.5 s, less one tick of rounding

class WhistleDelay : public PcScenario {
public:
	const char* name() const override { return "whistle_delay"; }

	void onStageReady() override
	{
		tutorial::applyDiscKeys();
		Navi* navi = naviMgr->getNavi();
		if (!navi || pikiMgr->getSize() != 0) {
			fail(0, "a captain and no Pikmin", "captain %d, Pikmin %d", (int)(navi != nullptr), pikiMgr->getSize());
			return;
		}
		mExpand = C_NAVI_PARM(navi, mWhistleExpandTime);
		std::printf("[scenario] whistle_delay: radius %.1f..%.1f, expand %.2f s\n", C_NAVI_PARM(navi, mWhistleMinRadius), NAVI_WHISTLE_MAX_RADIUS(navi), mExpand);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Done) return;
		if (Navi* navi = naviMgr->getNavi()) {
			navi->mCursorPosition.set(0.0f, 0.0f, 0.0f);
			navi->mCursorTargetPosition = navi->mCursorPosition;
			navi->mCursorWorldPos       = navi->mSRT.t;
		}
		if (mPhase == Hold && t < mReleaseTick) pad.button = PAD_BUTTON_B;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr->getNavi();
		const float s       = (t - mPhaseTick) / float(rate);
		switch (mPhase) {
		case Settle:
			if (s >= 1.0f) {
				for (int i = 0; i < kCount; i++) {
					const float a = 0.5f + 0.9f * float(i), d = 20.0f + 8.0f * float(i);
					if (!(mPiki[i] = tutorial::spawnPiki(navi, i % 2 ? Red : Blue, PikiMode::FreeMode, d * std::cos(a), d * std::sin(a)))) {
						fail(t, "Pikmin born", "birth %d returned null", i);
						return;
					}
				}
				mPhase     = Stand;
				mPhaseTick = t;
			}
			return;
		case Stand:
			if (s >= 1.0f) {
				mPhase       = Hold;
				mPhaseTick   = t;
				mReleaseTick = ~0u;
			}
			return;
		case Hold:
			if (navi->mWhistleCircleMode == 2 && mReleaseTick == ~0u) mReleaseTick = t + unsigned(std::lround(0.3f * rate));
			watch(t);
			if (s > 10.0f) fail(t, "the whistle circle full within 10 s", "mode %d", (int)navi->mWhistleCircleMode);
			if (mReleaseTick != ~0u && t >= mReleaseTick) {
				mPhase     = After;
				mPhaseTick = t;
			}
			return;
		case After: watch(t); finish(t, rate, s); return;
		default: return;
		}
	}

private:
	enum Phase { Settle, Stand, Hold, After, Done };

	void fail(unsigned t, const char* expected, const char* fmt, ...) __attribute__((format(printf, 4, 5)))
	{
		char buf[320];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		mPhase = Done;
		scenarioFailf(t, expected, "%s", buf);
	}

	void watch(unsigned t)
	{
		for (int i = 0; i < kCount; i++) {
			if (!mPiki[i]) continue;
			if (!mCalled[i] && mPiki[i]->getState() == PIKISTATE_LookAt) mCalled[i] = t;
			if (mCalled[i] && !mJoined[i] && mPiki[i]->getState() == PIKISTATE_LookAt
			    && mPiki[i]->mPikiAnimMgr.getUpperAnimator().getCurrentMotionIndex() == PIKIANIM_Kizuku)
				mKizuku[i] = true;
			if (mCalled[i] && !mJoined[i] && mPiki[i]->mMode == PikiMode::FormationMode && mPiki[i]->getState() != PIKISTATE_LookAt) mJoined[i] = t;
		}
	}

	void finish(unsigned t, unsigned rate, float s)
	{
		bool allJoined = true;
		for (int i = 0; i < kCount; i++) allJoined = allJoined && mJoined[i];
		if (!allJoined && s < 12.0f) return;
		unsigned first = ~0u, last = 0;
		float minD = 1e9f, maxD = 0.0f, sum = 0.0f;
		for (int i = 0; i < kCount; i++) {
			if (!mCalled[i] || !mJoined[i]) {
				fail(t, "every Pikmin called and joined within 12 s", "Pikmin %d called %u joined %u", i, mCalled[i], mJoined[i]);
				return;
			}
			const float d = (mJoined[i] - mCalled[i]) / float(rate);
			std::printf("[scenario] whistle_delay: Pikmin %d: called at tick %u, joined %.2f s later%s\n", i, mCalled[i], d, mKizuku[i] ? "" : " (no Kizuku)");
			minD = std::min(minD, d);
			maxD = std::max(maxD, d);
			sum += d;
			first = std::min(first, mCalled[i]);
			last  = std::max(last, mCalled[i]);
		}
		std::printf("[scenario] whistle_delay: call to joined %.2f s min, %.2f s mean, %.2f s max; calls spread over %.2f s\n", minD, sum / kCount, maxD,
		            (last - first) / float(rate));
		for (int i = 0; i < kCount; i++) {
			if (!mKizuku[i]) {
				fail(t, "every called Pikmin plays Kizuku before it joins (#145)", "Pikmin %d joined %.2f s after its call without Kizuku", i,
				     (mJoined[i] - mCalled[i]) / float(rate));
				return;
			}
		}
		if (minD < kMinJoin) {
			fail(t, "every called Pikmin joins at least 0.45 s after its call (#145)", "fastest %.2f s", minD);
			return;
		}
		if (maxD > kMaxJoin) {
			fail(t, "every called Pikmin joins within 1 s of its call", "slowest %.2f s", maxD);
			return;
		}
		std::printf("[scenario] whistle_delay: PASS at tick %u\n", t);
		mPhase = Done;
		scenarioPass();
	}

	Piki* mPiki[kCount]     = {};
	unsigned mCalled[kCount] = {}, mJoined[kCount] = {};
	bool mKizuku[kCount]     = {};
	Phase mPhase            = Settle;
	unsigned mPhaseTick = 0, mReleaseTick = ~0u;
	float mExpand       = 0.0f;
};

} // namespace

PcScenario* pc_scenario_make_whistle_delay(void)
{
	return new WhistleDelay();
}

#endif
