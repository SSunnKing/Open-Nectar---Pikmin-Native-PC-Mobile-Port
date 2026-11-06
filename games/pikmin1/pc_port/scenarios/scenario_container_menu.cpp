// container_menu: the Onion's container menu opens, runs and closes on the
// stand-in con01.blo (verification-log.md, item 2). Runs on the opt-in object
// stage, stages/sandbox_objects.ini, whose default.gen places a blue Onion.
//
// The captain is put on the Onion's 'cont' sphere and presses A. NaviWalkState
// then enters NAVISTATE_Container, whose init calls DrawContainer::start: that
// looks up the window pane p264 and the pictures ws8c, p2c4, ws8u and ws8l, the
// panes only the menu uses. With an empty squad and an empty Onion, a stick push
// makes the menu post MSG_NothingToTransfer through its text boxes.
//
// Two cases: close with B, then open again and close with A. Each asserts the
// captain enters Container, the menu goes Wait, Start, Operation, stays in
// Operation for kHoldTicks (no abort), ends, and the captain returns to Walk
// with the squad and the Onion count unchanged.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
// The header's inline switches do not cover every enum value; scenario sources build with -Werror.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#endif
#include "zen/DrawContainer.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

constexpr unsigned kHoldTicksAt30 = 120;

enum Phase { Open, Hold, Close, Done };

struct Case {
	const char* name;
	u16 closeButton;
};
const Case kCases[] = { { "close_with_b", PAD_BUTTON_B }, { "close_with_a", PAD_BUTTON_A } };
constexpr int kCaseCount = int(sizeof(kCases) / sizeof(kCases[0]));

class ContainerMenu : public PcScenario {
public:
	const char* name() const override { return "container_menu"; }

	void onStageReady() override
	{
		mOnion = itemMgr->getContainer(0);
		if (!mOnion || !containerWindow) {
			scenarioFailf(0, "a blue Onion and the container window", "onion %p window %p", (void*)mOnion, (void*)containerWindow);
			return;
		}
		CollPart* cont = mOnion->mCollInfo->getSphere('cont');
		if (!cont) {
			scenarioFail(0, "the Onion's 'cont' sphere", "none");
			return;
		}
		Navi* navi = naviMgr->getNavi();
		std::printf("[scenario] container_menu: onion at (%.1f, %.1f, %.1f), cont centre (%.1f, %.1f, %.1f) radius %.1f, captain at (%.1f, %.1f, %.1f)\n",
		            mOnion->mSRT.t.x, mOnion->mSRT.t.y, mOnion->mSRT.t.z, cont->mCentre.x, cont->mCentre.y, cont->mCentre.z, cont->mRadius,
		            navi->mSRT.t.x, navi->mSRT.t.y, navi->mSRT.t.z);
		mOnionStore = mOnion->getTotalStorePikis();
	}

	void providePads(unsigned tick, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || !mOnion) return;
		const int cap = navi->getCurrState()->getID();
		switch (mPhase) {
		case Open:
			if (cap == NAVISTATE_Walk) {
				// Walk onto the 'cont' sphere, then click A (a click needs a release between presses).
				CollPart* cont = mOnion->mCollInfo->getSphere('cont');
				Vector3f d     = cont->mCentre - navi->getCentre();
				d.y            = 0.0f;
				const float len = d.length();
				if (len > 4.0f) {
					// Stick: +x is east, +y is up on the pad and north (-z) in the default camera; the
					// scenario only needs to get close, so the direction is refined every tick.
					pad.stickX = (int8_t)(d.x / len * 70.0f);
					pad.stickY = (int8_t)(-d.z / len * 70.0f);
				}
				// A elsewhere would punch (NAVISTATE_Attack), so press it only on the spot.
				const float reach = navi->getSize() + cont->mRadius;
				if ((cont->mCentre - navi->getCentre()).length() < reach - 1.0f && tick % 4 == 0) pad.button = PAD_BUTTON_A;
			}
			break;
		case Hold:
			// A stick push with nothing to transfer posts MSG_NothingToTransfer through the text boxes.
			if (mHoldTicks >= 30 && mHoldTicks < 36) pad.stickY = 70;
			break;
		case Close:
			if (tick % 4 == 0) pad.button = kCases[mCase].closeButton;
			break;
		case Done: break;
		}
	}

	void onTick(unsigned tick) override
	{
		if (!mOnion) return;
		Navi* navi    = naviMgr->getNavi();
		const int cap = navi->getCurrState()->getID();
		const int rate = pc_scenario_ticks_per_second();
		const unsigned bound = 10u * (unsigned)rate;
		const Case& c = kCases[mCase];
		switch (mPhase) {
		case Open:
			if (cap == NAVISTATE_Container) {
				std::printf("[scenario] container_menu %s: captain in Container at tick %u (status %d)\n", c.name, tick, (int)containerWindow->getStatus());
				mPhase     = Hold;
				mHoldTicks = 0;
				mSeen      = 1u << containerWindow->getStatus();
				mPhaseTick = tick;
			} else if (tick - mPhaseTick > bound) {
				CollPart* cont = mOnion->mCollInfo->getSphere('cont');
				Vector3f d     = cont->mCentre - navi->getCentre();
				scenarioFailf(tick, "the captain enters Container", "%s: state %d after %u ticks, %.1f from the cont centre (limit %.1f)", c.name, cap,
				              tick - mPhaseTick, d.length(), navi->getSize() + cont->mRadius);
			}
			break;
		case Hold:
			if (cap != NAVISTATE_Container) {
				scenarioFailf(tick, "the captain stays in Container while the menu runs", "%s: state %d", c.name, cap);
				return;
			}
			mSeen |= 1u << containerWindow->getStatus();
			if (containerWindow->getStatus() == zen::DrawContainer::STATE_Operation) ++mHoldTicks;
			if (mHoldTicks >= kHoldTicksAt30 * (unsigned)rate / 30u) {
				std::printf("[scenario] container_menu %s: %u ticks in Operation, statuses seen 0x%x\n", c.name, mHoldTicks, mSeen);
				if ((mSeen & 0x7u) != 0x7u) {
					scenarioFailf(tick, "the menu passes Wait, Start and Operation", "%s: statuses seen 0x%x", c.name, mSeen);
					return;
				}
				mPhase     = Close;
				mPhaseTick = tick;
			} else if (tick - mPhaseTick > bound + kHoldTicksAt30 * (unsigned)rate / 30u) {
				scenarioFailf(tick, "the menu reaches Operation", "%s: status %d, %u ticks in Operation", c.name, (int)containerWindow->getStatus(), mHoldTicks);
			}
			break;
		case Close:
			if (cap == NAVISTATE_Walk) {
				const int store = mOnion->getTotalStorePikis();
				std::printf("[scenario] container_menu %s: back to Walk at tick %u, Onion holds %d (was %d)\n", c.name, tick, store, mOnionStore);
				if (store != mOnionStore) {
					scenarioFailf(tick, "an empty transfer leaves the Onion count unchanged", "%s: %d, was %d", c.name, store, mOnionStore);
					return;
				}
				if (++mCase == kCaseCount) {
					mPhase = Done;
					scenarioPass();
					return;
				}
				mPhase     = Open;
				mPhaseTick = tick;
			} else if (tick - mPhaseTick > bound) {
				scenarioFailf(tick, "the menu closes and the captain returns to Walk", "%s: state %d, status %d", c.name, cap, (int)containerWindow->getStatus());
			}
			break;
		case Done: break;
		}
	}

private:
	GoalItem* mOnion    = nullptr;
	Phase mPhase        = Open;
	int mCase           = 0;
	unsigned mPhaseTick = 0;
	unsigned mHoldTicks = 0;
	unsigned mSeen      = 0;
	int mOnionStore     = 0;
};

} // namespace

PcScenario* pc_scenario_make_container_menu(void)
{
	static ContainerMenu s;
	return &s;
}

#endif
