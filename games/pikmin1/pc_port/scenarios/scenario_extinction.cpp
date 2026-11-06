// extinction: every Pikmin dies on a normal day, and the extinction chain ends.
// Runs on the plain stand-in sandbox (stages/sandbox.ini, no Onion, no sprouts)
// on Day 2. The chain, each link in game code:
//  1. GameCoreSection::update sees GameStat::allPikis == 0 (maxPikis > 0) and
//     puts the captain in NAVISTATE_PikiZero.
//  2. NaviPikiZeroState::exec, 30 ticks later, sends MOVIECMD_GameEndCondition
//     (ENDCAUSE_PikminZero). The first message sets mGameEndFlag =
//     GAMEEND_PikminExtinction and plays DEMOID_Extinction (demo46.cin).
//  3. demo46's own GameEndCondition key sends the second message: the day-end
//     movie (DEMOID_ExtDayEnd, translated to demo47 for the sandbox) starts and
//     the clock is set to the end hour. NewPikiGame's RunningModeState has
//     meanwhile put the game in MessageModeState.
//  4. demo47's ForceResults key clears the end flag; MessageModeState then
//     queues DayOverModeState, whose constructor sets mIsDayEndSeqStarted.
// That last state is the terminal. The scenario passes there, before the
// day-end phases that need results screens the stand-in tree does not have.
//
// Without stand-in demo46 and demo47 the chain never ends: each missing movie
// finishes in one tick and drops its keys, the captain goes back to Walk, is put
// in PikiZero again, and the second message replays about every 30 ticks
// (tools/scenarios/coverage/terminals.md, T04). So the bounds below, and the
// check that the chain ran once, are what tells a finished chain from a loop.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "FlowController.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "ObjType.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "system.h"

namespace {

constexpr int kPikmin         = 3;
constexpr int kBlue           = 0;
constexpr unsigned kKillTicks = 5; // ticks between the spawn and the kill
// Bounds, in seconds of game time (ticks / rate). The disc's 30 Hz timings were
// 11 ticks for PikiZero, 40 for the extinction movie flag and 608 for the day
// end (two long movies); the stand-ins are 60 frame movies.
constexpr unsigned kPikiZeroSeconds = 3;  // kill to PikiZero
constexpr unsigned kFlagSeconds     = 5;  // PikiZero to the flag (the 30 tick countdown)
constexpr unsigned kClockSeconds    = 10; // flag to the clock set to the end hour (demo46)
constexpr unsigned kTerminalSeconds = 15; // clock set to the day end state (demo47)

class Extinction : public PcScenario {
public:
	const char* name() const override { return "extinction"; }
	int startDay() const override { return 2; }

	void onTick(unsigned t) override
	{
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		Navi* navi          = naviMgr->getNavi();
		const int cap       = navi->getCurrState()->getID();
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		if (cap == NAVISTATE_PikiZero && mLastCap != NAVISTATE_PikiZero) {
			mPikiZeroEntries++;
			if (mPikiZeroEntries == 1) mPikiZeroTick = t;
		}
		mLastCap = cap;

		// A second extinction movie is a replay of the chain: count the flag settings.
		const int flag = flowCont.mGameEndFlag;
		if (flag == GAMEEND_PikminExtinction && mLastFlag != GAMEEND_PikminExtinction) {
			mFlagSets++;
			if (mFlagSets == 1) {
				mFlagTick = t;
				mFlagSeen = true;
			}
		}
		mLastFlag = flag;

		const float endHour = gameflow.mParameters->mEndHour();
		if (mFlagSeen && !mClockSeen && gameflow.mWorldClock.mTimeOfDay >= endHour) {
			mClockTick = t;
			mClockSeen = true;
		}

		switch (mPhase) {
		case Spawn:
			if (movie) break; // an intro movie is still up
			spawn(navi);
			mPhase     = Kill;
			mPhaseTick = t;
			break;
		case Kill:
			if (t - mPhaseTick < kKillTicks) break;
			std::printf("[scenario] extinction: allPikis %d, maxPikis %d, clock %.3f h (end %.2f), day %d; killing all\n", (int)GameStat::allPikis, (int)GameStat::maxPikis,
			            gameflow.mWorldClock.mTimeOfDay, endHour, gameflow.mWorldClock.mCurrentDay);
			if (GameStat::allPikis != kPikmin || GameStat::maxPikis != kPikmin) {
				scenarioFailf(t, "3 Pikmin before the kill", "allPikis %d, maxPikis %d", (int)GameStat::allPikis, (int)GameStat::maxPikis);
				return;
			}
			if (flag != GAMEEND_None) {
				scenarioFailf(t, "no end flag before the kill", "mGameEndFlag %d", flag);
				return;
			}
			for (Piki* p : mPikis) p->stimulate(InteractKill(navi, 0));
			mPhase     = WaitPikiZero;
			mPhaseTick = t;
			break;
		case WaitPikiZero:
			if (cap == NAVISTATE_PikiZero) {
				std::printf("[scenario] extinction: captain in PikiZero %u ticks after the kill (allPikis %d)\n", t - mPhaseTick, (int)GameStat::allPikis);
				mPhase = WaitFlag;
			} else if (t - mPhaseTick > kPikiZeroSeconds * rate) {
				scenarioFailf(t, "the captain enters PikiZero after the last Pikmin dies", "state %d, allPikis %d, maxPikis %d after %u ticks", cap, (int)GameStat::allPikis,
				              (int)GameStat::maxPikis, t - mPhaseTick);
				return;
			}
			break;
		case WaitFlag:
			if (mFlagSeen) {
				std::printf("[scenario] extinction: end flag set %u ticks after PikiZero; movie active %d\n", mFlagTick - mPikiZeroTick, (int)movie);
				if (!movie) {
					scenarioFailf(t, "the extinction movie plays when the flag is set", "no movie active (demo46 missing or empty)");
					return;
				}
				mPhase = WaitClock;
			} else if (t - mPikiZeroTick > kFlagSeconds * rate) {
				scenarioFailf(t, "mGameEndFlag becomes GAMEEND_PikminExtinction", "flag %d, %u ticks after PikiZero", flag, t - mPikiZeroTick);
				return;
			}
			break;
		case WaitClock:
			if (mClockSeen) {
				std::printf("[scenario] extinction: clock at the end hour %u ticks after the flag; flag %d, day end active %d, day end seq %d\n", mClockTick - mFlagTick, flag,
				            (int)gameflow.mIsDayEndActive, (int)flowCont.mIsDayEndSeqStarted);
				mPhase = WaitTerminal;
			} else if (t - mFlagTick > kClockSeconds * rate) {
				scenarioFailf(t, "the second GameEndCondition (demo46's key) sets the clock to the end hour",
				              "clock %.3f h, %u ticks after the flag, flag %d, flag sets %d, PikiZero entries %d", gameflow.mWorldClock.mTimeOfDay, t - mFlagTick, flag, mFlagSets,
				              mPikiZeroEntries);
				return;
			}
			break;
		case WaitTerminal:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] extinction: day end state %u ticks after the clock was set; flag %d; PikiZero entries %d; flag sets %d; time %.3f h\n", t - mClockTick, flag,
				            mPikiZeroEntries, mFlagSets, gameflow.mWorldClock.mTimeOfDay);
				if (flag != GAMEEND_None) {
					scenarioFailf(t, "the end flag cleared (ForceResults) before the day end state", "flag %d", flag);
					return;
				}
				// The chain ran once: the captain went through PikiZero once and the flag was set once.
				if (mPikiZeroEntries != 1 || mFlagSets != 1) {
					scenarioFailf(t, "the extinction chain runs once", "PikiZero entered %d times, flag set %d times", mPikiZeroEntries, mFlagSets);
					return;
				}
				scenarioPass();
				return;
			}
			if (t - mClockTick > kTerminalSeconds * rate) {
				scenarioFailf(t, "the day end state follows the day-end movie (demo47)",
				              "no DayOverModeState %u ticks after the clock was set; flag %d, PikiZero entries %d, flag sets %d, movie %d", t - mClockTick, flag, mPikiZeroEntries,
				              mFlagSets, (int)movie);
				return;
			}
			break;
		}
	}

private:
	enum Phase { Spawn, Kill, WaitPikiZero, WaitFlag, WaitClock, WaitTerminal };

	// Pikmin as throw_release makes them: born, counted, formation members of the captain.
	void spawn(Navi* navi)
	{
		for (int i = 0; i < kPikmin; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (piki == nullptr) {
				scenarioFailf(0, "pikmin spawned", "birth returned null at %d", i);
				return;
			}
			GameStat::workPikis.inc(kBlue);
			piki->init(navi);
			Vector3f at = navi->mSRT.t;
			at.x += 45.0f * (float)(i - 1);
			at.z += 45.0f;
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(kBlue);
			piki->changeMode(PikiMode::FormationMode, navi);
			mPikis.push_back(piki);
		}
		GameStat::update();
	}

	Phase mPhase        = Spawn;
	unsigned mPhaseTick = 0, mFlagTick = 0, mClockTick = 0, mPikiZeroTick = 0;
	bool mFlagSeen = false, mClockSeen = false;
	int mLastCap = -1, mLastFlag = GAMEEND_None, mPikiZeroEntries = 0, mFlagSets = 0;
	std::vector<Piki*> mPikis;
};

} // namespace

PcScenario* pc_scenario_make_extinction(void)
{
	static Extinction s;
	return &s;
}

#endif
