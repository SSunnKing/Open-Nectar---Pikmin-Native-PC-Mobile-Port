// extinction_day30: every Pikmin dies on the last day (Day 30), coverage/terminals.md
// T05 and edge-cases.md EC-12. Upstream #100 regressed a Day 30 special case; this
// one sits in GameMovieInterface::parse (MOVIECMD_GameEndCondition, ENDCAUSE_PikminZero).
//
// Runs on the plain stand-in sandbox (stages/sandbox.ini, no Onion, no sprouts),
// starting on Day 30 (startDay, the lever day30_clock's Day 30 phase stands for)
// with Infinite Day off. Three blue Pikmin are born and killed as in `extinction`.
// What the code does, read from newPikiGame.cpp and checked by this run:
//
//  1. PikiZero, then the first GameEndCondition: mGameEndFlag = GAMEEND_PikminExtinction
//     and the extinction movie (demo46). On Day 30 (or in challenge mode) the game-over
//     text window starts here, because the day will not end the usual way.
//     RunningModeState moves to MessageModeState.
//  2. demo46's own GameEndCondition key, second message: on Day 30 the flag is reset to
//     GAMEEND_None. No ExtDayEnd movie (demo47) and no clock jump: that is the Day 2
//     path (scenario `extinction`).
//  3. MessageModeState sees GAMEEND_None and queues DayOverModeState in phase zero
//     without the march-in movie; its constructor sets the clock to the end hour.
//  4. Phase one on Day 30 is the neutral or bad ending (mCurrentDay >= MAX_DAYS). With
//     no ship parts it is the bad one: demo69 (a missing stand-in, zero length), then
//     phase two: no day advance, makeTotalScoreWindow and demo73 (BadEndingFailEscape),
//     whose StartTotalResults key starts the final results window; A presses take it
//     through the scores, the parts list and the options save; when it reports done the
//     scene is skipped. Phase three: BadEndingOlimin returns at once without a red Onion
//     (MoviePlayer::startMovie). Phase four queues ONEPLAYER_GameExit: the run is over.
//
// So, against the T05 note "the day does not end early": the flag is reset as the note
// says, but MessageModeState then ends the day at once and the last day goes straight to
// the bad ending. The original code does that; this scenario characterises it.
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): the steps above,
// with at step 2 the flag reset while demo46 still plays and the clock still below the
// end hour; demo47 never starts; the flag is set once; the day stays 30; mEndingType
// stays ENDING_None (bad ending); the final results window is up at least 4 s (its own
// slide in and appear take 4 s before A counts) and the soft reset goes to GameExit (11)
// within 60 s of demo73's start.
//
// Observed, not asserted: once demo73 is skipped the captain is back in control for a
// tick and, with no Pikmin, enters PikiZero again; GameExit comes one tick later, long
// before PikiZero's 30-tick countdown could send another message.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "GlobalGameOptions.h"
#include "Interactions.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

constexpr int kPikmin         = 3;
constexpr unsigned kKillTicks = 5;
constexpr int kLastDay        = 30; // MAX_DAYS
// Bounds, in seconds. Measured at 30 Hz (60 Hz alike in seconds): PikiZero 1 tick after
// the kill, the flag 29 ticks later, the reset 58 ticks after that (demo46's key),
// DayOverModeState the next tick, demo73 3 ticks later, the soft reset about 14 s later
// with A every 6 ticks.
constexpr unsigned kPikiZeroSeconds = 3;
constexpr unsigned kFlagSeconds     = 5;
constexpr unsigned kResetSeconds    = 10;
constexpr unsigned kDayOverSeconds  = 2;
constexpr unsigned kEndingSeconds   = 10;
constexpr unsigned kTerminalSeconds = 60;
constexpr unsigned kMinTotalSeconds = 4; // DrawTotalScore: 2 s start, 2 s appear, then A counts

bool moviePlaying(int index)
{
	if (!gameflow.mMoviePlayer) return false;
	for (CoreNode* c = gameflow.mMoviePlayer->mPlayInfoList.mChild; c; c = c->mNext) {
		if ((int)static_cast<MovieInfo*>(c)->mMovieIndex == index) return true;
	}
	return false;
}

class ExtinctionDay30 : public PcScenario {
public:
	const char* name() const override { return "extinction_day30"; }
	int startDay() const override { return kLastDay; }

	// The final results window and its save step each wait for A.
	void providePads(unsigned t, PcInputPad& pad) override
	{
		if ((mPhase == WaitEnding || mPhase == WaitTerminal) && t % 6 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = (unsigned)pc_scenario_ticks_per_second();
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr; // the stage is torn down on the way out
		const int cap       = navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1;
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const float endHour = gameflow.mParameters->mEndHour();
		const float time    = gameflow.mWorldClock.mTimeOfDay;
		const int day       = gameflow.mWorldClock.mCurrentDay;
		const int flag      = flowCont.mGameEndFlag;

		if (cap == NAVISTATE_PikiZero && mLastCap != NAVISTATE_PikiZero) mPikiZeroEntries++;
		mLastCap = cap;
		if (flag == GAMEEND_PikminExtinction && mLastFlag != GAMEEND_PikminExtinction) mFlagSets++;
		mLastFlag = flag;
		if (moviePlaying(DEMOID_ExtDayEnd)) mSawExtDayEnd = true;

		// Held on every tick from the spawn on.
		if (mPhase != Spawn) {
			if (day != kLastDay) {
				scenarioFailf(t, "the day stays 30", "day %d", day);
				return;
			}
			if (mFlagSets > 1) {
				scenarioFailf(t, "the extinction flag is set once", "set %d times (the chain replays)", mFlagSets);
				return;
			}
			if (mSawExtDayEnd) {
				scenarioFailf(t, "no ExtDayEnd movie (demo47) on Day 30", "demo47 started; flag %d, %.4f h", flag, time);
				return;
			}
			if (flowCont.mEndingType != ENDING_None) {
				scenarioFailf(t, "mEndingType stays ENDING_None (bad ending, no parts)", "mEndingType %d", (int)flowCont.mEndingType);
				return;
			}
		}

		switch (mPhase) {
		case Spawn:
			if (movie) return; // an intro movie is still up
			if (day != kLastDay || flag != GAMEEND_None || !(time < endHour) || playerState->happyEndable() || playerState->getCurrParts() == MAX_UFO_PARTS) {
				scenarioFailf(t, "Day 30 before sunset, no end flag, fewer than 25 parts", "day %d, flag %d, %.3f h, happyEndable %d, parts %d", day, flag, time,
				              (int)playerState->happyEndable(), playerState->getCurrParts());
				return;
			}
			pc_settings_scenario_set_infinite_day(0);
			if (!spawn(navi)) return;
			enter(Kill, t);
			return;
		case Kill:
			if (t - mPhaseTick < kKillTicks) return;
			if (GameStat::allPikis != kPikmin || GameStat::maxPikis != kPikmin) {
				scenarioFailf(t, "3 Pikmin before the kill", "allPikis %d, maxPikis %d", (int)GameStat::allPikis, (int)GameStat::maxPikis);
				return;
			}
			std::printf("[scenario] extinction_day30: day %d, %.4f h (end %.2f h), killing all %d Pikmin at t=%u\n", day, time, endHour, kPikmin, t);
			for (Piki* p : mPikis) p->stimulate(InteractKill(navi, 0));
			enter(WaitPikiZero, t);
			return;
		case WaitPikiZero:
			if (cap == NAVISTATE_PikiZero) {
				std::printf("[scenario] extinction_day30: PikiZero at t=%u (%u ticks after the kill)\n", t, t - mPhaseTick);
				enter(WaitFlag, t);
			} else if (t - mPhaseTick > kPikiZeroSeconds * rate) {
				scenarioFailf(t, "the captain enters PikiZero within 3 s of the kill", "state %d, allPikis %d", cap, (int)GameStat::allPikis);
			}
			return;
		case WaitFlag:
			if (flag == GAMEEND_PikminExtinction) {
				std::printf("[scenario] extinction_day30: GAMEEND_PikminExtinction at t=%u (%u ticks after PikiZero), demo46 %d\n", t, t - mPhaseTick,
				            (int)moviePlaying(DEMOID_Extinction));
				if (!moviePlaying(DEMOID_Extinction)) {
					scenarioFailf(t, "the extinction movie (demo46) plays when the flag is set", "movie active %d, demo46 not in the play list", (int)movie);
					return;
				}
				enter(WaitReset, t);
			} else if (t - mPhaseTick > kFlagSeconds * rate) {
				scenarioFailf(t, "GAMEEND_PikminExtinction within 5 s of PikiZero", "flag %d", flag);
			}
			return;
		case WaitReset:
			if (flag == GAMEEND_None) {
				std::printf("[scenario] extinction_day30: flag reset at t=%u (%u ticks after it was set), %.4f h, demo46 %d, day end seq %d\n", t, t - mPhaseTick, time,
				            (int)moviePlaying(DEMOID_Extinction), (int)flowCont.mIsDayEndSeqStarted);
				// The Day 30 branch of the second GameEndCondition, sent by demo46 itself.
				if (!moviePlaying(DEMOID_Extinction) || !(time < endHour) || flowCont.mIsDayEndSeqStarted) {
					scenarioFailf(t, "the flag reset by demo46's own key, the clock untouched, before DayOverModeState",
					              "demo46 %d, %.4f h (end %.2f), day end seq %d", (int)moviePlaying(DEMOID_Extinction), time, endHour, (int)flowCont.mIsDayEndSeqStarted);
					return;
				}
				enter(WaitDayOver, t);
			} else if (t - mPhaseTick > kResetSeconds * rate) {
				scenarioFailf(t, "the end flag reset to GAMEEND_None within 10 s (Day 30 branch)", "flag %d, %.4f h, movie %d", flag, time, (int)movie);
			}
			return;
		case WaitDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] extinction_day30: DayOverModeState at t=%u (%u ticks after the reset), %.4f h\n", t, t - mPhaseTick, time);
				if (time < endHour) {
					scenarioFailf(t, "DayOverModeState sets the clock to the end hour", "%.4f h", time);
					return;
				}
				if (mPikiZeroEntries != 1) {
					scenarioFailf(t, "PikiZero entered once before the day end", "%d times", mPikiZeroEntries);
					return;
				}
				enter(WaitEnding, t);
			} else if (t - mPhaseTick > kDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 2 s of the flag reset", "none; flag %d, movie %d", flag, (int)movie);
			}
			return;
		case WaitEnding:
			if (moviePlaying(DEMOID_BadEndingFailEscape)) {
				std::printf("[scenario] extinction_day30: bad ending, demo73 at t=%u (%u ticks after DayOverModeState)\n", t, t - mPhaseTick);
				mEndingTick = t;
				enter(WaitTerminal, t);
			} else if (gsys->mSoftResetPending) {
				scenarioFailf(t, "the bad ending's escape movie (demo73) before the exit", "soft reset toward %d first", (int)gameflow.mNextOnePlayerSectionID);
			} else if (t - mPhaseTick > kEndingSeconds * rate) {
				scenarioFailf(t, "demo73 (BadEndingFailEscape) within 10 s of DayOverModeState", "none; movie %d", (int)movie);
			}
			return;
		case WaitTerminal:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] extinction_day30: soft reset at t=%u (%u ticks after demo73 started), next section %d, day %d; PikiZero entries %d\n", t,
				            t - mEndingTick, next, day, mPikiZeroEntries);
				if (next != ONEPLAYER_GameExit) {
					scenarioFailf(t, "the soft reset goes to GameExit (11): the run ends", "section %d", next);
					return;
				}
				if (t - mEndingTick < kMinTotalSeconds * rate) {
					scenarioFailf(t, "the final results window is up at least 4 s", "left after %u ticks", t - mEndingTick);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of demo73", "none; movie %d", (int)movie);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Spawn, Kill, WaitPikiZero, WaitFlag, WaitReset, WaitDayOver, WaitEnding, WaitTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	// As scenario_extinction.cpp: born, counted, formation members of the captain.
	bool spawn(Navi* navi)
	{
		for (int i = 0; i < kPikmin; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (!piki) {
				scenarioFailf(0, "pikmin spawned", "birth returned null at %d", i);
				return false;
			}
			GameStat::workPikis.inc(Blue);
			piki->init(navi);
			Vector3f at = navi->mSRT.t;
			at.x += 45.0f * (float)(i - 1);
			at.z += 45.0f;
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(Blue);
			piki->changeMode(PikiMode::FormationMode, navi);
			mPikis.push_back(piki);
		}
		GameStat::update();
		return true;
	}

	Phase mPhase        = Spawn;
	unsigned mPhaseTick = 0, mEndingTick = 0;
	int mLastCap = -2, mLastFlag = GAMEEND_None, mPikiZeroEntries = 0, mFlagSets = 0;
	bool mSawExtDayEnd = false;
	std::vector<Piki*> mPikis;
};

} // namespace

PcScenario* pc_scenario_make_extinction_day30(void)
{
	static ExtinctionDay30 s;
	return &s;
}

#endif
