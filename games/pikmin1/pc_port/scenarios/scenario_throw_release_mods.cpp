// throw_release_quick_grab (EC-53): calibration K with Quick Grab on must stay
// below the vanilla K that throw_release measures (13 at 30 Hz, 22 at 60 Hz).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "CPlate.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "settings/pc_settings.h"

namespace {

constexpr int kPikminCount = 3;
constexpr int kBlue        = 0;
constexpr unsigned kAwaySeconds = 4;
constexpr unsigned kWalkStartTicks = 45;
constexpr float kSelectDistance = 80.0f;

class ThrowReleaseQuickGrab : public PcScenario {
public:
	const char* name() const override { return "throw_release_quick_grab"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_quick_grab(1);
		if (pc_settings_get_quick_grab() == 0) scenarioFailf(0, "Quick Grab on", "off");
		Navi* navi = naviMgr->getNavi();
		const unsigned intro = pc_scenario_intro_ticks();
		mHold                = intro < kWalkStartTicks ? kWalkStartTicks - intro : 0;
		if (mHold == 0) {
			spawnSquad(navi);
			mPhase = Away;
		} else {
			mPhase = Hold;
		}
	}

	void spawnSquad(Navi* navi)
	{
		for (int i = 0; i < kPikminCount; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			GameStat::workPikis.inc(kBlue);
			piki->init(navi);
			const float a = 6.2831853f * (float)i / (float)kPikminCount;
			Vector3f at   = navi->mSRT.t;
			at.x += 45.0f * std::cos(a);
			at.z += 45.0f * std::sin(a);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(kBlue);
			piki->changeMode(PikiMode::FormationMode, navi);
		}
		GameStat::update();
	}

	void providePads(unsigned, PcInputPad& pad) override
	{
		if (mPhase == Away) pad.stickY = 100;
		pad.button = mHoldA ? PAD_BUTTON_A : 0;
	}

	void onTick(unsigned t) override
	{
		Navi* navi = naviMgr->getNavi();
		const int cap = navi->getCurrState()->getID();
		const int rate = pc_scenario_ticks_per_second();
		const unsigned bound = 5u * rate;
		switch (mPhase) {
		case Hold:
			if (t + 1 >= mHold) {
				spawnSquad(navi);
				mPhase     = Away;
				mAwayStart = t + 1;
			}
			break;
		case Away:
			if (t + 1 >= mAwayStart + kAwaySeconds * (unsigned)rate) {
				mPhase     = Settle;
				mStepStart = t;
			}
			break;
		case Settle:
			if (cap == NAVISTATE_Walk && plateNormalCount(navi, kSelectDistance) > 0) {
				mPress = t + 1;
				mHoldA = true;
				mPhase = Held;
				mStepStart = t;
			} else if (t - mStepStart > 2 * bound) {
				scenarioFailf(t, "captain Walk with Pikmin in reach", "state %d", cap);
			}
			break;
		case Held:
			if (t == mPress && cap != NAVISTATE_ThrowWait) {
				scenarioFailf(t, "ThrowWait the tick after the press with Quick Grab", "state %d", cap);
				return;
			}
			if (cap == NAVISTATE_ThrowWait && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mIsHoldingThrowPiki) {
				const unsigned k = t - mPress;
				const unsigned kNormalMax = rate == 60 ? 22u : 13u;
				if (k >= kNormalMax) {
					scenarioFailf(t, "Quick Grab K < normal K", "K=%u normal max %u", k, kNormalMax);
					return;
				}
				std::printf("[scenario] throw_release_quick_grab: K=%u (< %u)\n", k, kNormalMax);
				scenarioPass();
			} else if (t - mPress > bound) {
				scenarioFailf(t, "Quick Grab grab completes", "timeout");
			}
			break;
		default: break;
		}
	}

	int plateNormalCount(Navi* navi, float within)
	{
		int n = 0;
		CPlate* plate = navi->mPlateMgr;
		for (int i = plate->getFirst(); !plate->isDone(i); i = plate->getNext(i)) {
			Creature* c = plate->getCreature(i);
			if (c == nullptr || c->mObjType != OBJTYPE_Piki || static_cast<Piki*>(c)->getState() != PIKISTATE_Normal) continue;
			Vector3f d = c->mSRT.t - navi->mSRT.t;
			if (d.length() < within) ++n;
		}
		return n;
	}

	enum Phase { Hold, Away, Settle, Held };
	Phase mPhase = Hold;
	unsigned mHold = 0, mAwayStart = 0, mPress = 0, mStepStart = 0;
	bool mHoldA = false;
};

} // namespace

PcScenario* pc_scenario_make_throw_release_quick_grab(void)
{
	static ThrowReleaseQuickGrab s;
	return &s;
}

#endif
