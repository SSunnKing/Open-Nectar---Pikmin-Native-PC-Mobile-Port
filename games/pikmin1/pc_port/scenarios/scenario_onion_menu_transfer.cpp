// onion_menu_transfer: the Onion menu moves Pikmin both ways, pauses the world while it is open,
// cancels with B, and clamps a withdrawal at the field cap (coverage/edge-cases.md EC-03, EC-35,
// EC-47), on stages/sandbox_objects.ini (a blue Onion) with the field limit set to 10.
//
// What the code does (GameCube original; characterised, nothing fixed): A on the Onion's 'cont'
// sphere enters NAVISTATE_Container; NaviContainerState::init (naviState.cpp:1062) starts the
// menu with the squad's Onion-coloured Pikmin, the Onion's store, the field count (mapPikis plus
// pending exits) and the field limit, and sets gameflow.mPauseAll. In Operation the stick moves
// mTransferDelta (up = into the Onion); DrawContainer::operationStatus clamps it and posts
// MSG_SquadTotalFull when a withdrawal would take the field past the limit; A ends with the
// delta, B ends with 0. On a positive delta GoalItem::enterPikis sends that many squad Pikmin in
// (GoalItem::enterGoal: the store and containerPikis go up, the Pikmin leaves the field); on a
// negative one GoalItem::exitPikis dispenses them one by one (exitPiki).
//
// In order, each within a bound in seconds:
//   Open     Six blue squad Pikmin; A on 'cont' -> Container within 10 s, mPauseAll TRUE.
//   Deposit  Stick up to a delta of 4: the menu shows Onion +4, squad 2; for 1 s idle the world
//            clock does not move (Infinite Day off); A -> Walk.
//   Absorb   Within 20 s the Onion stores +4, containerPikis[blue] +4, the squad is 2; the world
//            clock has moved since the menu closed.
//   Cancel   Open, stick up to 2, B -> Walk: store and squad unchanged for 3 s.
//   Clamp    Free blue Pikmin join until the field count is 9 of 10. The field count is
//            GameStat::mapPikis, which includes the stage's three buried sprouts (mePikis), as
//            PikiMgr::birth and the menu count it. Open, hold the stick down for 3 s: the delta
//            stops at -1, MSG_SquadTotalFull was shown; A -> Walk.
//   Exit     Within 10 s the field count is exactly 10 and the store 3, and both hold for 2 s.
//
// Negative controls: Cancel is Deposit's contrast (same stick, B instead of A, nothing moves),
// and the clock check after Absorb is the pause check's (the same clock read moves once the menu
// is closed). Manual (2026-10-31, local, not committed), at 30 Hz: with A for B in Cancel it
// fails "B cancels the transfer: store and squad unchanged" (squad 2 -> 0: the two go in); with
// the field limit 100 it fails "EC-03: a withdrawal past the field limit is clamped (delta -1,
// MSG_SquadTotalFull)" (delta -4, no message).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Dolphin/pad.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "scenarios/scenario_progress.h"
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

using scenario_progress::spawnPiki;

constexpr int kFieldLimit = 10;

// Read-only peeks at the menu's protected state (a derived class's member pointers).
struct MenuPeek : zen::DrawContainer {
	static int delta(zen::DrawContainer& d) { return d.*(&MenuPeek::mTransferDelta); }
	static zen::MessageMgr* messages(zen::DrawContainer& d) { return d.*(&MenuPeek::mMessageMgr); }
};
struct MessagePeek : zen::MessageMgr {
	static int message(zen::MessageMgr& m) { return m.*(&MessagePeek::mMessage); }
};

enum Phase { Open, Deposit, Idle, CloseA, Absorb, Cancel, CloseB, Settle, Clamp, Exit, Done };

class OnionMenuTransfer : public PcScenario {
public:
	const char* name() const override { return "onion_menu_transfer"; }
	const char* stageFile() const override { return "stages/sandbox_objects.ini"; }
	int pikiLimit() const override { return kFieldLimit; }

	void onStageReady() override
	{
		pc_settings_scenario_set_infinite_day(0);
		mOnion = itemMgr->getContainer(Blue);
		Navi* navi = naviMgr->getNavi();
		if (!mOnion || !containerWindow || !navi) {
			fail(0, "a blue Onion, the container window and a captain", "onion %p window %p", (void*)mOnion, (void*)containerWindow);
			return;
		}
		for (int i = 0; i < 6; ++i) {
			if (!spawnPiki(navi, Blue, PikiMode::FormationMode, i - 3)) {
				mPhase = Done;
				return;
			}
		}
		mStore0 = mOnion->getTotalStorePikis();
		std::printf("[scenario] onion_menu_transfer: Onion stores %d, %d Pikmin on the field, field count %d\n", mStore0, field(), mapTotal());
	}

	void providePads(unsigned tick, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || !mOnion || mPhase == Done) return;
		const int cap = navi->getCurrState()->getID();
		const bool op = cap == NAVISTATE_Container && containerWindow->getStatus() == zen::DrawContainer::STATE_Operation;
		const bool opening = mPhase == Open || mPhase == Cancel || mPhase == Clamp;
		// The captain idles after a few seconds without input (NaviIdleState); A ends it.
		if (opening && cap == NAVISTATE_Idle) {
			if (tick % 4 == 0) pad.button = PAD_BUTTON_A;
			return;
		}
		if (opening && cap == NAVISTATE_Walk) {
			walkToOnionAndPressA(navi, tick, pad);
			return;
		}
		if (!op) return;
		const int d = MenuPeek::delta(*containerWindow);
		switch (mPhase) {
		case Deposit:
			if (d < 4) pad.stickY = 70;
			break;
		case Cancel:
			if (d < 2) {
				pad.stickY = 70;
			} else if (tick % 4 == 0) {
				pad.button = PAD_BUTTON_B;
			}
			break;
		case Clamp:
			if (mHold < 3.0f) pad.stickY = -70;
			break;
		case CloseA:
			if (tick % 4 == 0) pad.button = PAD_BUTTON_A;
			break;
		default: break;
		}
	}

	void onTick(unsigned t) override
	{
		if (!mOnion || mPhase == Done) return;
		Navi* navi     = naviMgr->getNavi();
		const int cap  = navi->getCurrState()->getID();
		const int rate = pc_scenario_ticks_per_second();
		const float s  = float(t - mPhaseTick) / float(rate);
		const int store = mOnion->getTotalStorePikis();
		const bool op   = cap == NAVISTATE_Container && containerWindow->getStatus() == zen::DrawContainer::STATE_Operation;
		const float clock = gameflow.mWorldClock.mTimeOfDay;

		switch (mPhase) {
		case Open:
			if (cap == NAVISTATE_Container) {
				if (!gameflow.mPauseAll) {
					fail(t, "EC-35: the Onion menu pauses the world (mPauseAll)", "mPauseAll %d", (int)gameflow.mPauseAll);
					return;
				}
				std::printf("[scenario] onion_menu_transfer: menu open at tick %u, squad %d, store %d\n", t, squad(), store);
				mSquad0 = squad();
				if (mSquad0 != 6) {
					fail(t, "six blue Pikmin in the squad", "%d", mSquad0);
					return;
				}
				enter(Deposit, t);
			} else if (s > 10.0f) {
				fail(t, "the captain opens the Onion menu within 10 s", "state %d", cap);
			}
			break;
		case Deposit:
			if (op && MenuPeek::delta(*containerWindow) == 4) {
				const int onionDisp = containerWindow->getContainerPikiDisp(), squadDisp = containerWindow->getMyPikiDisp();
				if (onionDisp != mStore0 + 4 || squadDisp != 2) {
					fail(t, "the menu shows Onion +4 and squad 2", "Onion %d (was %d), squad %d", onionDisp, mStore0, squadDisp);
					return;
				}
				mClock = clock;
				enter(Idle, t);
			} else if (s > 10.0f) {
				fail(t, "a held stick moves 4 into the Onion within 10 s", "state %d, status %d, delta %d", cap, (int)containerWindow->getStatus(),
				     MenuPeek::delta(*containerWindow));
			}
			break;
		case Idle:
			if (clock != mClock) {
				fail(t, "EC-35: the world clock does not move while the menu is open", "%.5f -> %.5f", mClock, clock);
			} else if (s >= 1.0f) {
				std::printf("[scenario] onion_menu_transfer: clock held at %.4f for 1 s in the menu\n", clock);
				enter(CloseA, t);
				mAfter = Absorb;
			}
			break;
		case CloseA:
			if (cap == NAVISTATE_Walk) {
				mContainer0 = GameStat::containerPikis[Blue];
				mClock      = clock;
				std::printf("[scenario] onion_menu_transfer: menu closed at tick %u, store %d, field %d\n", t, store, field());
				enter(mAfter, t);
			} else if (s > 10.0f) {
				fail(t, "A closes the menu within 10 s", "state %d", cap);
			}
			break;
		case Absorb:
			if (store == mStore0 + 4 && squad() == 2) {
				const int c = GameStat::containerPikis[Blue];
				if (c != mContainer0 + 4 && c != mContainer0) {
					fail(t, "EC-47: containerPikis[blue] counts the 4 that went in", "%d -> %d", mContainer0, c);
					return;
				}
				GameStat::update();
				if (GameStat::containerPikis[Blue] < 4) {
					fail(t, "EC-47: containerPikis[blue] counts the 4 that went in", "%d", (int)GameStat::containerPikis[Blue]);
					return;
				}
				if (!(clock > mClock)) {
					fail(t, "the world clock moves again once the menu is closed", "%.5f -> %.5f", mClock, clock);
					return;
				}
				std::printf("[scenario] onion_menu_transfer: 4 in at %.2f s: store %d, containerPikis[blue] %d, squad %d, clock %.4f -> %.4f\n", s,
				            store, (int)GameStat::containerPikis[Blue], squad(), mClock, clock);
				mStore1 = store;
				mSquad1 = squad();
				enter(Cancel, t);
			} else if (s > 20.0f) {
				fail(t, "EC-47: the Onion takes the 4 within 20 s", "store %d (want %d), squad %d", store, mStore0 + 4, squad());
			}
			break;
		case Cancel:
			if (cap == NAVISTATE_Container) mOpened = true;
			if (mOpened && cap == NAVISTATE_Walk) {
				enter(CloseB, t);
			} else if (s > 20.0f) {
				fail(t, "the menu opens and B closes it within 20 s", "state %d", cap);
			}
			break;
		case CloseB:
			if (store != mStore1 || squad() != mSquad1) {
				fail(t, "B cancels the transfer: store and squad unchanged", "store %d -> %d, squad %d -> %d", mStore1, store, mSquad1, squad());
			} else if (s >= 3.0f) {
				// The field count is GameStat::mapPikis, which includes the stage's buried sprouts
				// (mePikis); PikiMgr::birth and the menu both use it, refreshed in GameStat::update.
				const int add = (kFieldLimit - 1) - mapTotal();
				for (int i = 0; i < add; ++i) {
					if (!spawnPiki(navi, Blue, PikiMode::FreeMode, i - 3)) {
						mPhase = Done;
						return;
					}
					GameStat::update();
				}
				std::printf("[scenario] onion_menu_transfer: B cancelled; %d free Pikmin added, field count %d (%d Pikmin, %d sprouts)\n", add, mapTotal(),
				            field(), (int)GameStat::mePikis);
				enter(Settle, t);
			}
			break;
		case Settle:
			if (s >= 1.0f) {
				if (mapTotal() != kFieldLimit - 1) {
					fail(t, "the field count is 9 before the withdrawal", "%d", mapTotal());
					return;
				}
				mOpened = false;
				enter(Clamp, t);
			}
			break;
		case Clamp:
			if (cap == NAVISTATE_Container) mOpened = true;
			if (op) {
				const int m = MessagePeek::message(*MenuPeek::messages(*containerWindow));
				if (m == zen::MessageMgr::MSG_SquadTotalFull) mSawFull = true;
				mHold += 1.0f / float(rate);
				if (mHold >= 3.5f) {
					const int d = MenuPeek::delta(*containerWindow);
					std::printf("[scenario] onion_menu_transfer: withdrawal held 3 s: delta %d, MSG_SquadTotalFull %d\n", d, (int)mSawFull);
					if (d != -1 || !mSawFull) {
						fail(t, "EC-03: a withdrawal past the field limit is clamped (delta -1, MSG_SquadTotalFull)", "delta %d, message seen %d", d,
						     (int)mSawFull);
						return;
					}
					enter(CloseA, t);
					mAfter = Exit;
				}
			} else if (s > 20.0f) {
				fail(t, "the menu opens for the withdrawal within 20 s", "state %d", cap);
			}
			break;
		case Exit:
		{
			const int fc = mapTotal();
			if (fc > kFieldLimit) {
				fail(t, "EC-03: the field count never passes the limit", "%d", fc);
			} else if (fc == kFieldLimit && store == mStore1 - 1) {
				if (!mExitAt) mExitAt = t;
				if (float(t - mExitAt) / float(rate) >= 2.0f) {
					std::printf("[scenario] onion_menu_transfer: field count %d (limit %d), store %d\n", fc, kFieldLimit, store);
					mPhase = Done;
					scenarioPass();
				}
			} else if (mExitAt) {
				fail(t, "EC-03: field count at the limit and the rest in the Onion, held 2 s", "field count %d, store %d", fc, store);
			} else if (s > 10.0f) {
				fail(t, "EC-03: one Pikmin leaves the Onion within 10 s", "field count %d, store %d (want %d)", fc, store, mStore1 - 1);
			}
			break;
		}
		default: break;
		}
	}

private:
	static int mapTotal()
	{
		GameStat::update();
		return (int)GameStat::mapPikis;
	}

	int squad() const
	{
		int n = 0;
		Navi* navi = naviMgr->getNavi();
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p->mMode == PikiMode::FormationMode && p->mNavi == navi && p->mColor == Blue) ++n;
		}
		return n;
	}

	int field() const
	{
		int n = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it) { ++n; }
		return n;
	}

	void walkToOnionAndPressA(Navi* navi, unsigned tick, PcInputPad& pad)
	{
		CollPart* cont = mOnion->mCollInfo->getSphere('cont');
		Vector3f d     = cont->mCentre - navi->getCentre();
		d.y            = 0.0f;
		const float len = d.length();
		if (len > 4.0f) {
			pad.stickX = (int8_t)(d.x / len * 70.0f);
			pad.stickY = (int8_t)(-d.z / len * 70.0f);
		}
		const float reach = navi->getSize() + cont->mRadius;
		if ((cont->mCentre - navi->getCentre()).length() < reach - 1.0f && tick % 4 == 0) pad.button = PAD_BUTTON_A;
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

	GoalItem* mOnion = nullptr;
	Phase mPhase = Open, mAfter = Absorb;
	unsigned mPhaseTick = 0, mExitAt = 0;
	int mStore0 = 0, mStore1 = 0, mSquad0 = 0, mSquad1 = 0, mContainer0 = 0;
	float mClock = 0.0f, mHold = 0.0f;
	bool mOpened = false, mSawFull = false;
};

} // namespace

PcScenario* pc_scenario_make_onion_menu_transfer(void)
{
	static OnionMenuTransfer s;
	return &s;
}

#endif
