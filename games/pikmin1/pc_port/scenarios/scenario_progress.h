#pragma once
// Helpers shared by the progression scenarios: onion_unlock and onion_unlock_whistle
// (the first-pluck snapshot and its check, the disc's key bindings), day_end (a
// Pikmin spawned as the game's spawners do, the sunset clock jump) and onion_day3,
// which uses both. Moved here unchanged from scenario_onion_unlock.cpp and
// scenario_day_end.cpp.
#if PIKI_SCENARIOS

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "Demo.h"
#include "GameStat.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "Navi.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "ResultFlags.h"
#include "WorldClock.h"
#include "gameflow.h"
#include "pc_achievements.h"
#include "scenarios/pc_scenario.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "zen/ogResult.h"

namespace scenario_progress {

constexpr int kBlue = 0, kRed = 1, kYellow = 2;
inline const char* const kColourName[3] = { "blue", "red", "yellow" };

// ResultFlags keeps its flag reader protected; a pointer to member named through a
// derived class reads it without changing the class.
struct ResultPeek : ResultFlags {
	static int get(ResultFlags& f, int i) { return (f.*&ResultPeek::getFlag)(i); }
	static int length(ResultFlags& f) { return f.*&ResultPeek::mLength; }
};

inline int achievementFor(int colour)
{
	return colour == kRed ? PC_ACH_PIKMIN_RED : colour == kYellow ? PC_ACH_PIKMIN_YELLOW : PC_ACH_PIKMIN_BLUE;
}

/// Everything a first pluck may change.
struct Snap {
	std::vector<int> demo, result;
	int container = 0, display = 0, chal = 0, course = 0, plucked = 0, achCount = 0;
	int me[3] = {}, form[3] = {}, all[3] = {}, field[3] = {};
	bool ach[3] = {};
};

inline Snap take()
{
	Snap s;
	for (int i = 0; i < DEMOFLAG_COUNT; i++) s.demo.push_back(playerState->mDemoFlags.isFlag(i) ? 1 : 0);
	const int n = ResultPeek::length(playerState->mResultFlags);
	for (int i = 0; i < n; i++) s.result.push_back(ResultPeek::get(playerState->mResultFlags, i));
	s.container = playerState->mContainerFlag;
	s.display   = playerState->mDisplayPikiFlag;
	s.chal      = gameflow.mGamePrefs.mChalCourseOpenFlags;
	s.course    = gameflow.mPlayState.mCourseOpenFlags;
	s.plucked   = playerState->mTotalPluckedPikiCount;
	s.achCount  = pc_achievements_unlocked_count();
	for (int c = 0; c < 3; c++) {
		s.me[c]   = GameStat::mePikis[c];
		s.form[c] = GameStat::formationPikis[c];
		s.all[c]  = GameStat::allPikis[c];
		s.ach[c]  = pc_achievement_unlocked(achievementFor(c));
	}
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* p = static_cast<Piki*>(*it);
		if (p && p->mColor >= 0 && p->mColor < 3) s.field[p->mColor]++;
	}
	return s;
}

inline std::string setBits(const std::vector<int>& v)
{
	std::string out;
	for (std::size_t i = 0; i < v.size(); i++) {
		if (v[i]) out += (out.empty() ? "" : ",") + std::to_string(i);
	}
	return out.empty() ? "none" : out;
}

enum Path { Hand, Whistle };

/// Checks after == before plus exactly what a first pluck of `colour` by `path` sets.
inline bool checkPluck(unsigned t, Path path, int colour, const Snap& b, const Snap& a)
{
	const char* name = kColourName[colour];
	// Demo flags: PikiNukareState::cleanup / pcUnlockColourFromAutoPluck, plus NoPikminTimeout from
	// NaviNukuAdjustState::init and PikiNukareState::init on the hand path.
	std::set<int> demo;
	demo.insert(colour == kRed ? DEMOFLAG_PluckRedPikmin : colour == kYellow ? DEMOFLAG_PluckYellowPikmin : DEMOFLAG_PluckBluePikmin);
	if (path == Hand) demo.insert(DEMOFLAG_NoPikminTimeout);
	if (colour == kRed) demo.insert(DEMOFLAG_ApproachSeed);
	std::vector<int> want = b.demo;
	for (int i : demo) want[(std::size_t)i] = 1;
	if (a.demo != want) {
		scenarioFailf(t, "the pluck sets exactly its demo flags", "%s: flags %s, expected %s (before %s)", name, setBits(a.demo).c_str(), setBits(want).c_str(),
		              setBits(b.demo).c_str());
		return false;
	}
	// Result flags: setOn moves an unset flag (0) to on (1).
	std::vector<int> wantRes = b.result;
	auto on = [&](int f) {
		if (f < (int)wantRes.size() && wantRes[(std::size_t)f] == 0) wantRes[(std::size_t)f] = 1;
	};
	if (colour == kYellow) {
		on(zen::RESFLAG_MeetYellowPikminNoBomb);
		on(zen::RESFLAG_Onyons);
	} else if (colour == kBlue) {
		on(zen::RESFLAG_MeetBluePikmin);
	}
	if (a.result != wantRes) {
		std::string diff;
		for (std::size_t i = 0; i < a.result.size() && i < wantRes.size(); i++) {
			if (a.result[i] != wantRes[i]) diff += " " + std::to_string(i) + ":" + std::to_string(a.result[i]) + "!=" + std::to_string(wantRes[i]);
		}
		scenarioFailf(t, "the pluck sets exactly its result flags", "%s: differs at%s", name, diff.c_str());
		return false;
	}
	const int wantContainer = b.container | (colour == kRed ? 0 : 1 << colour);
	const int wantDisplay   = b.display | 1 << colour;
	if (a.container != wantContainer || a.display != wantDisplay) {
		scenarioFailf(t, "container and display bits gain only the plucked colour", "%s: container 0x%02x display 0x%02x, expected 0x%02x 0x%02x", name, a.container,
		              a.display, wantContainer, wantDisplay);
		return false;
	}
	const int wantChal = b.chal | (colour == kBlue ? (1 << CHALSTAGE_Practice) | (1 << CHALSTAGE_Forest) | (1 << CHALSTAGE_Cave) : 0);
	if (a.chal != wantChal || a.course != b.course) {
		scenarioFailf(t, "Challenge Mode stages open on blue only; story stages unchanged", "%s: challenge 0x%x (expected 0x%x), story 0x%x (was 0x%x)", name,
		              a.chal, wantChal, a.course, b.course);
		return false;
	}
	for (int c = 0; c < 3; c++) {
		const bool want = b.ach[c] || c == colour;
		if (a.ach[c] != want) {
			scenarioFailf(t, "the colour's achievement unlocks on its first pluck, no other", "%s: achievement %s %d, expected %d", name, kColourName[c],
			              (int)a.ach[c], (int)want);
			return false;
		}
	}
	if (a.achCount != b.achCount + (b.ach[colour] ? 0 : 1)) {
		scenarioFailf(t, "one achievement more", "%s: %d unlocked, was %d", name, a.achCount, b.achCount);
		return false;
	}
	for (int c = 0; c < 3; c++) {
		const int dMe = c == colour ? -1 : 0, dPiki = c == colour ? 1 : 0;
		if (a.me[c] - b.me[c] != dMe || a.form[c] - b.form[c] != dPiki || a.all[c] != b.all[c] || a.field[c] - b.field[c] != dPiki) {
			scenarioFailf(t, "a sprout becomes one Formation Pikmin of its colour (mePikis -1, formationPikis +1, allPikis and other colours unchanged)",
			              "%s pluck, %s: me %d->%d form %d->%d all %d->%d field %d->%d", name, kColourName[c], b.me[c], a.me[c], b.form[c], a.form[c], b.all[c],
			              a.all[c], b.field[c], a.field[c]);
			return false;
		}
	}
	// A whistle pluck counts too (pc_whistle_pluck): the pluck camera stops zooming after 100.
	if (a.plucked != b.plucked + 1) {
		scenarioFailf(t, "a hand or whistle pluck counts one pluck (mTotalPluckedPikiCount)", "%s: %d, was %d", name, a.plucked, b.plucked);
		return false;
	}
	return true;
}

/// The disc's parms/key.bin bindings. The stand-in tree has no key.bin, so KeyConfig keeps its code defaults, where
/// extract (pluck) is B, the whistle's button: a press of B then runs NaviWalkState's shared-key branch, which no disc
/// run takes. The disc's file (six big-endian ints, read 2026-10-28) binds throw A, set cursor B, extract A, attack A,
/// disband X, menu Y. Setting them here makes A pluck and B whistle on either tree and changes no other scenario.
inline void applyDiscKeys()
{
	KeyConfig* k = KeyConfig::_instance;
	std::printf("[scenario] key bindings before: throw 0x%x cursor 0x%x extract 0x%x attack 0x%x\n", k->mThrowKey.mBind, k->mSetCursorKey.mBind,
	            k->mExtractKey.mBind, k->mAttackKey.mBind);
	k->mThrowKey.mBind     = KBBTN_A;
	k->mSetCursorKey.mBind = KBBTN_B;
	k->mExtractKey.mBind   = KBBTN_A;
	k->mAttackKey.mBind    = KBBTN_A;
	k->mDisbandKey.mBind   = KBBTN_X;
	k->mMenuKey.mBind      = KBBTN_Y;
}

// Born, counted and given a mode as the game's own spawners do (see throw_release).
inline bool spawnPiki(Navi* navi, int colour, int mode, int slot)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (!piki) {
		scenarioFailf(0, "a Pikmin is born", "birth returned null");
		return false;
	}
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at = navi->mSRT.t;
	at.x += 45.0f * (float)slot;
	at.z += 45.0f;
	at.y = mapMgr->getMinY(at.x, at.z, true);
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(mode, navi);
	return true;
}

/// Infinite Day off and the clock `ticks` open clock ticks before the end hour (day_end): sunset follows by itself.
inline void jumpToSunset(unsigned ticks)
{
	WorldClock& clock = gameflow.mWorldClock;
	pc_settings_scenario_set_infinite_day(0);
	const float hoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
	clock.setTime(gameflow.mParameters->mEndHour() - ticks * hoursPerTick);
}

} // namespace scenario_progress

#endif
