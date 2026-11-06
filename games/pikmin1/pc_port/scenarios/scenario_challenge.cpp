// challenge_day_end, challenge_captain_death, challenge_extinction: the three ways a
// challenge-mode day ends (coverage/terminals.md T17, T18).
//
// The runner forces story mode; these scenarios ask for challenge mode
// (PcScenario::challengeMode), so pc_scenario_pre_stage takes CardSelect's challenge path:
// gameflow.mIsChallengeMode and PlayerState::setChallengeMode (every demo flag, every
// Onion). The stand-in stages have no chid; the runner gives the stage challenge course 0
// (Impact Site), because DayOverModeState indexes the hiscore table with it.
//
//   challenge_day_end (T17, object stage, stages/sandbox_objects.ini): with every Onion
//     owned, GenObjectItem::birth makes Onions only from generators named 'next', so the
//     object stage has no Onion in challenge mode; its sprouts are there. Two blue Pikmin
//     (Formation and Free) and one red are spawned and the clock is moved 60 open clock
//     ticks before the end hour. Sunset: DayOverModeState from phase zero (no march-in in
//     challenge mode); phase one plays ChalDayEnd and, in challenge mode, no take-off
//     movie; phase two: exitDayEnd (the three field Pikmin killed: no Onion of their
//     colour), demo36, the day advance, and DrawCMresult with GameStat::allPikis as the
//     score, which is the sprouts (GamePrefs::checkIsHiscore records it as the best).
//   challenge_captain_death (T18, sandbox): the T06 hit. MessageModeState starts the
//     game-over window in its Starting state (showGameOver: challenge mode), and its
//     ForceDayEnd clears the window (challenge branch) with no OliDownDayEnd movie; then
//     phase one and two as above, score 0.
//   challenge_extinction (T18, sandbox): three blue Pikmin killed as in scenario
//     extinction. The first GameEndCondition starts demo46 and, in challenge mode, the
//     game-over window (MODE_Extinction) at once; demo46's own key resets the flag
//     (challenge branch, no ExtDayEnd movie); MessageModeState then queues
//     DayOverModeState; phase one and two as above, score 0.
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): challenge mode on;
// each chain's steps above (Dead and NaviDown, or PikiZero and the extinction flag, or the
// sunset); the game-over window started once in the right mode (none on the sunset path),
// and cleared by the time DayOverModeState starts in the captain-death case; demo47 never
// starts; DayOverModeState; phase two with the day advanced by one (see below) and no
// diary entry recorded (no story results window in challenge mode); the challenge results
// window (DrawCMresult) built and started with the score GameStat::allPikis holds after
// exitDayEnd; the score as the course's new best (rank 0) when above zero, no rank at zero;
// the soft reset to MapSelect (6) at least 1 s after the window opened and within 60 s.
// mEndingType stays None throughout.
//
// The day: DayOverModeState::initialisePhaseTwo advances mCurrentDay before it tests
// mIsChallengeMode, so a challenge day end moves the clock's day from 1 to 2 as a story day
// end does (the coverage plan's T17 row said "mCurrentDay unchanged"; the code and this run
// say otherwise). The scenario asserts what the code does.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "scenarios/scenario_endings_common.h"

#include "Dolphin/pad.h"
#include "GlobalGameOptions.h"
#include "Interactions.h"
#include "ItemMgr.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Parameters.h"
#include "system.h"
#include "zen/DrawCM.h"

namespace {

using namespace scenario_endings;

enum class Path { DayEnd, CaptainDeath, Extinction };

constexpr unsigned kHitTick     = 10;
constexpr unsigned kSunsetTicks = 60;
constexpr int kExtinctPikmin    = 3;
// Bounds, in seconds; measured times in the log lines. The hit and extinction chains take
// about 6 s (Dead 4 s, NaviDown 1.3 s; or PikiZero 1 tick, the flag 1 s, demo46's key 2 s)
// before DayOverModeState; the results window opens a few ticks after it; the challenge
// window takes its 1.1 s appear, the score count and the save step.
constexpr unsigned kToStepSeconds     = 8;
constexpr unsigned kToDayOverSeconds  = 15;
constexpr unsigned kToResultsSeconds  = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinResultsSeconds = 1;

// DrawCMresult keeps the score, its rank and its mode protected; read them through a derived type.
struct ChalPeek : zen::DrawCMresult {
	static int score(zen::DrawCMresult* w) { return w->*(&ChalPeek::_84); }
	static int rank(zen::DrawCMresult* w) { return w->*(&ChalPeek::_8C); }
	static int mode(zen::DrawCMresult* w) { return w->*(&ChalPeek::mMode); }
};

class Challenge : public PcScenario {
public:
	Challenge(const char* name, Path path)
	    : mName(name)
	    , mPath(path)
	{
	}

	const char* name() const override { return mName; }
	bool challengeMode() const override { return true; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// The challenge window, its score count and its save step wait for A; before that,
		// only a text window would (A also skips a movie during play, so never otherwise).
		if (mPhase == ToTerminal ? t % 6 == 0 : (mPhase < ToDayOver && gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr;
		const int cap       = navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1;
		const int flag      = flowCont.mGameEndFlag;
		const int day       = gameflow.mWorldClock.mCurrentDay;
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		WorldClock& clock   = gameflow.mWorldClock;
		const float endHour = gameflow.mParameters->mEndHour();
		mGameOver.tick(t);
		if (moviePlaying(DEMOID_ExtDayEnd)) mSawExtDayEnd = true;

		if (!gameflow.mIsChallengeMode || !playerState->isChallengeMode() || flowCont.mEndingType != ENDING_None || mSawExtDayEnd || mGameOver.starts > 1) {
			scenarioFailf(t, "challenge mode, no ending, no ExtDayEnd movie, the game-over window started at most once",
			              "challenge %d/%d, ending %d, demo47 %d, window starts %d", (int)gameflow.mIsChallengeMode, (int)playerState->isChallengeMode(),
			              (int)flowCont.mEndingType, (int)mSawExtDayEnd, mGameOver.starts);
			return;
		}
		if (mPhase != Start && mPhase < ToResults && day != mStartDay) {
			scenarioFailf(t, "the day unchanged before phase two", "day %d, start %d", day, mStartDay);
			return;
		}

		switch (mPhase) {
		case Start:
		{
			if (movie || gameflow.mIsUIOverlayActive) return;
			if (mPath == Path::CaptainDeath && t < kHitTick) return;
			mStartDay = day;
			mBest0    = gameflow.mGamePrefs.mHiscores.mChalModeRecords[CHALSTAGE_Practice].mScores[0];
			if (flag != GAMEEND_None || gameflow.mIsDayEndActive || !(clock.mTimeOfDay < endHour) || flowCont.mCurrentStage->mChalStageID != CHALSTAGE_Practice) {
				scenarioFailf(t, "a running challenge day on course 0", "flag %d, day end %d, %.3f h, course %d", flag, (int)gameflow.mIsDayEndActive, clock.mTimeOfDay,
				              (int)flowCont.mCurrentStage->mChalStageID);
				return;
			}
			pc_settings_scenario_set_infinite_day(0);
			std::printf("[scenario] %s: challenge mode, day %d, Onions %d%d%d, best score %d\n", mName, day, (int)playerState->hasContainer(Blue),
			            (int)playerState->hasContainer(Red), (int)playerState->hasContainer(Yellow), mBest0);
			if (mPath == Path::DayEnd) {
				// With every Onion owned, GenObjectItem::birth makes Onions only from generators
				// named 'next'; the object stage's blue Onion generator is not one, so the stage has
				// no Onion in challenge mode. Its sprouts are there (GameStat::mePikis).
				if (itemMgr->getContainer(Blue) || itemMgr->getContainer(Red) || itemMgr->getContainer(Yellow) || GameStat::mePikis <= 0) {
					scenarioFailf(t, "no Onion on the object stage in challenge mode, and its sprouts", "Onions %d%d%d, sprouts %d",
					              (int)(itemMgr->getContainer(Blue) != nullptr), (int)(itemMgr->getContainer(Red) != nullptr),
					              (int)(itemMgr->getContainer(Yellow) != nullptr), (int)GameStat::mePikis);
					return;
				}
				mSprouts  = GameStat::mePikis;
				mBlueDead = GameStat::deadPikis[Blue];
				mRedDead  = GameStat::deadPikis[Red];
				if (!spawnPikmin(navi, Blue, PikiMode::FormationMode, -1) || !spawnPikmin(navi, Blue, PikiMode::FreeMode, 0) || !spawnPikmin(navi, Red, PikiMode::FormationMode, 1))
					return;
				GameStat::update();
				const float hoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
				clock.setTime(endHour - kSunsetTicks * hoursPerTick);
				std::printf("[scenario] %s: 2 blue and 1 red Pikmin, clock %.4f h\n", mName, clock.mTimeOfDay);
			} else if (mPath == Path::CaptainDeath) {
				if (cap != NAVISTATE_Walk) {
					scenarioFailf(t, "the captain walking", "state %d", cap);
					return;
				}
				navi->stimulate(InteractFlick(navi, 0.0f, 1000.0f, 0.0f));
			} else {
				for (int i = 0; i < kExtinctPikmin; i++)
					if (!spawnPikmin(navi, Blue, PikiMode::FormationMode, i - 1, &mPikis)) return;
				GameStat::update();
			}
			enter(Step1, t);
			return;
		}
		case Step1:
			// Day end: the sunset. Captain death: Dead. Extinction: the kill, then PikiZero.
			if (mPath == Path::DayEnd ? gameflow.mIsDayEndActive : mPath == Path::CaptainDeath ? cap == NAVISTATE_Dead : cap == NAVISTATE_PikiZero) {
				if (mPath == Path::DayEnd && clock.mTimeOfDay < endHour) {
					scenarioFailf(t, "the day end at or after the end hour", "%.4f h", clock.mTimeOfDay);
					return;
				}
				std::printf("[scenario] %s: %s at t=%u (%u ticks)\n", mName, mPath == Path::DayEnd ? "day end" : mPath == Path::CaptainDeath ? "Dead" : "PikiZero", t,
				            t - mPhaseTick);
				enter(mPath == Path::DayEnd ? ToDayOver : Step2, t);
			} else if (mPath == Path::Extinction && t - mPhaseTick == 5) {
				if (GameStat::allPikis != kExtinctPikmin) {
					scenarioFailf(t, "3 Pikmin before the kill", "allPikis %d", (int)GameStat::allPikis);
					return;
				}
				for (Piki* p : mPikis) p->stimulate(InteractKill(navi, 0));
			} else if (t - mPhaseTick > (mPath == Path::DayEnd ? 15 : kToStepSeconds) * rate) {
				scenarioFailf(t, "the chain's first step within its bound", "state %d, flag %d, day end %d, %.4f h", cap, flag, (int)gameflow.mIsDayEndActive,
				              clock.mTimeOfDay);
			}
			return;
		case Step2:
		{
			// The game-end flag and the game-over window: NaviDown then the window 0.5 s later
			// (MessageModeState Starting); Extinction with the window at the same message.
			const int wantFlag = mPath == Path::CaptainDeath ? GAMEEND_NaviDown : GAMEEND_PikminExtinction;
			const int wantMode = mPath == Path::CaptainDeath ? zen::DrawGameOver::MODE_NaviDown : zen::DrawGameOver::MODE_Extinction;
			if (flag == wantFlag && !mFlagSeen) {
				mFlagSeen = true;
				mFlagTick = t;
				std::printf("[scenario] %s: game-end flag %d at t=%u\n", mName, flag, t);
				if (mPath == Path::Extinction && !moviePlaying(DEMOID_Extinction)) {
					scenarioFailf(t, "demo46 plays with the extinction flag", "not in the play list");
					return;
				}
			}
			if (mFlagSeen && mGameOver.starts == 1) {
				if (mGameOver.lastMode != wantMode) {
					scenarioFailf(t, "the game-over window in the chain's mode", "mode %d, want %d", mGameOver.lastMode, wantMode);
					return;
				}
				if (t - mFlagTick > 2 * rate) {
					scenarioFailf(t, "the game-over window within 2 s of the flag (challenge mode shows it at once)", "%u ticks", t - mFlagTick);
					return;
				}
				enter(ToDayOver, t);
			} else if (flowCont.mIsDayEndSeqStarted) {
				scenarioFailf(t, "the flag and the game-over window before DayOverModeState", "flag seen %d, window starts %d", (int)mFlagSeen, mGameOver.starts);
			} else if (t - mPhaseTick > kToStepSeconds * rate) {
				scenarioFailf(t, "the game-end flag and the game-over window within 8 s", "flag %d, window starts %d", flag, mGameOver.starts);
			}
			return;
		}
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] %s: DayOverModeState at t=%u (%u ticks), game-over window %s, flag %d\n", mName, t, t - mPhaseTick,
				            pc_scenario_gameover_window() ? "set" : "cleared", flag);
				if (mPath == Path::DayEnd && mGameOver.starts != 0) {
					scenarioFailf(t, "no game-over window on the sunset path", "started, mode %d", mGameOver.lastMode);
					return;
				}
				// MessageModeState's ForceDayEnd clears the window in challenge mode, on the tick it
				// builds DayOverModeState (phase one, which also clears it, runs later).
				if (mPath == Path::CaptainDeath && pc_scenario_gameover_window() != nullptr) {
					scenarioFailf(t, "ForceDayEnd clears the game-over window in challenge mode", "still set");
					return;
				}
				enter(ToResults, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 15 s", "none; flag %d, movie %d", flag, (int)movie);
			}
			return;
		case ToResults:
		{
			zen::DrawCMresult* w = pc_scenario_challenge_window();
			if (w) {
				const int score = ChalPeek::score(w), rank = ChalPeek::rank(w);
				const int best  = gameflow.mGamePrefs.mHiscores.mChalModeRecords[CHALSTAGE_Practice].mScores[0];
				int pages       = 0;
				const int doc   = playerState->mResultFlags.getDayDocument(mStartDay, pages);
				std::printf("[scenario] %s: challenge results at t=%u (%u ticks after DayOverModeState): day %d, score %d (allPikis %d), rank %d, best %d -> %d, "
				            "diary entry %d, mode %d\n",
				            mName, t, t - mPhaseTick, day, score, (int)GameStat::allPikis, rank, mBest0, best, doc, ChalPeek::mode(w));
				if (day != mStartDay + 1) {
					scenarioFailf(t, "the day advances by one (initialisePhaseTwo, before its challenge test)", "day %d, start %d", day, mStartDay);
					return;
				}
				if (doc != -1) {
					scenarioFailf(t, "no diary entry in challenge mode", "entry %d", doc);
					return;
				}
				if (score != (int)GameStat::allPikis || ChalPeek::mode(w) == 0) {
					scenarioFailf(t, "the window started with GameStat::allPikis as the score", "score %d, allPikis %d, mode %d", score, (int)GameStat::allPikis,
					              ChalPeek::mode(w));
					return;
				}
				if (mPath == Path::DayEnd) {
					// exitDayEnd kills every field Pikmin with no Onion of its colour (all three here);
					// the sprouts are not field Pikmin and stay, and allPikis counts them.
					const int blueDead = GameStat::deadPikis[Blue] - mBlueDead;
					const int redDead  = GameStat::deadPikis[Red] - mRedDead;
					if (blueDead != 2 || redDead != 1 || GameStat::mapPikis != (int)GameStat::mePikis || score != mSprouts || score <= 0) {
						scenarioFailf(t, "the three field Pikmin killed (no Onion), the score the sprouts left (above 0)", "blue dead %d, red dead %d, map %d, sprouts %d (%d at the start), score %d",
						              blueDead, redDead, (int)GameStat::mapPikis, (int)GameStat::mePikis, mSprouts, score);
						return;
					}
				} else if (score != 0) {
					scenarioFailf(t, "score 0 after the captain's death or an extinction", "%d", score);
					return;
				}
				const bool record = score > mBest0;
				if (record ? (rank != 0 || best != score) : (rank != -1 || best != mBest0)) {
					scenarioFailf(t, record ? "a new best: rank 0, stored as the course's first score" : "no rank, the best unchanged", "rank %d, best %d", rank, best);
					return;
				}
				mResultsTick = t;
				enter(ToTerminal, t);
			} else if (gsys->mSoftResetPending) {
				scenarioFailf(t, "the challenge results window before the exit", "soft reset toward %d first", (int)gameflow.mNextOnePlayerSectionID);
			} else if (t - mPhaseTick > kToResultsSeconds * rate) {
				scenarioFailf(t, "the challenge results window within 10 s of DayOverModeState", "none; day %d, movie %d", day, (int)movie);
			}
			return;
		}
		case ToTerminal:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at t=%u (%u ticks after the window), next section %d, day %d\n", mName, t, t - mResultsTick, next, day);
				if (next != ONEPLAYER_MapSelect) {
					scenarioFailf(t, "the soft reset goes to MapSelect (6), the challenge course select", "section %d", next);
					return;
				}
				if (t - mResultsTick < kMinResultsSeconds * rate) {
					scenarioFailf(t, "the challenge window up at least 1 s", "left after %u ticks", t - mResultsTick);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of the challenge window", "none");
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Start, Step1, Step2, ToDayOver, ToResults, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	const char* mName;
	Path mPath;
	Phase mPhase        = Start;
	unsigned mPhaseTick = 0, mFlagTick = 0, mResultsTick = 0;
	int mStartDay = 1, mBest0 = 0, mSprouts = 0, mBlueDead = 0, mRedDead = 0;
	bool mFlagSeen = false, mSawExtDayEnd = false;
	std::vector<Piki*> mPikis;
	GameOverWatch mGameOver;
};

} // namespace

PcScenario* pc_scenario_make_challenge_day_end(void)
{
	static Challenge s("challenge_day_end", Path::DayEnd);
	return &s;
}

PcScenario* pc_scenario_make_challenge_captain_death(void)
{
	static Challenge s("challenge_captain_death", Path::CaptainDeath);
	return &s;
}

PcScenario* pc_scenario_make_challenge_extinction(void)
{
	static Challenge s("challenge_extinction", Path::Extinction);
	return &s;
}

#endif
