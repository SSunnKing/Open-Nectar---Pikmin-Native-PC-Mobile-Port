#pragma once
// Helpers shared by the tutorial scenarios (tutorial_firsts, tutorial_work) on the
// tutorial stand-in stage, stages/practice.ini (tools/scenarios/standin_practice_stage.h).
#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Demo.h"
#include "GameStat.h"
#include "ItemMgr.h"
#include "KeyConfig.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "ObjType.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "SimpleAI.h"
#include "gameflow.h"
#include "netplay/pc_input_log.h"

namespace tutorial {

inline std::vector<int> demoFlags()
{
	std::vector<int> v;
	for (int i = 0; i < DEMOFLAG_COUNT; i++) v.push_back(playerState->mDemoFlags.isFlag(i) ? 1 : 0);
	return v;
}

inline std::string setBits(const std::vector<int>& v)
{
	std::string out;
	for (std::size_t i = 0; i < v.size(); i++) {
		if (v[i]) out += (out.empty() ? "" : ",") + std::to_string(i);
	}
	return out.empty() ? "none" : out;
}

/// The flags in `now` that are not in `before`, ignoring `ignore`.
inline std::vector<int> newFlags(const std::vector<int>& before, const std::vector<int>& now, int ignore = -1)
{
	std::vector<int> out;
	for (std::size_t i = 0; i < now.size() && i < before.size(); i++) {
		if (now[i] && !before[i] && int(i) != ignore) out.push_back(int(i));
	}
	return out;
}

inline std::string list(const std::vector<int>& v)
{
	std::string out;
	for (int i : v) out += (out.empty() ? "" : ",") + std::to_string(i);
	return out.empty() ? "none" : out;
}

/// The movie being played (MovieInfo::mMovieIndex, after translateIndex), or -1.
inline int currentMovie()
{
	if (!gameflow.mMoviePlayer || !gameflow.mMoviePlayer->mIsActive) return -1;
	MovieInfo* info = static_cast<MovieInfo*>(gameflow.mMoviePlayer->mPlayInfoList.mChild);
	return info ? int(info->mMovieIndex) : -1;
}

/// The disc's parms/key.bin bindings (onion_unlock): A throws and plucks, B whistles. The stand-in tree has no key.bin.
inline void applyDiscKeys()
{
	KeyConfig* k           = KeyConfig::_instance;
	k->mThrowKey.mBind     = KBBTN_A;
	k->mSetCursorKey.mBind = KBBTN_B;
	k->mExtractKey.mBind   = KBBTN_A;
	k->mAttackKey.mBind    = KBBTN_A;
	k->mDisbandKey.mBind   = KBBTN_X;
	k->mMenuKey.mBind      = KBBTN_Y;
}

inline float flatDistance(const Vector3f& a, const Vector3f& b)
{
	const float dx = a.x - b.x, dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

/// Stick towards `target`. Navi::makeVelocity turns the stick by the control yaw (in the scenario's
/// deterministic mode, pc_netplay_control_yaw for pad 0, which the tutorial's attention camera turns),
/// so the world direction is turned back by the same yaw. Returns the remaining flat distance.
inline float steer(Navi* navi, const Vector3f& target, PcInputPad& pad, float stop = 6.0f, float strength = 60.0f)
{
	Vector3f d(target.x - navi->mSRT.t.x, 0.0f, target.z - navi->mSRT.t.z);
	const float len = std::sqrt(d.x * d.x + d.z * d.z);
	if (len <= stop) return len;
	float s = 0.0f, c = 1.0f;
	if (!pc_netplay_control_yaw(0, &s, &c)) {
		s = 0.0f;
		c = 1.0f;
	}
	// makeVelocity: world = R(yaw) * (stickX, 0, -stickY). R(yaw) about y maps (x, z) to (c x + s z, -s x + c z).
	const float wx = d.x / len, wz = d.z / len;
	const float lx = c * wx - s * wz;
	const float lz = s * wx + c * wz;
	pad.stickX     = (int8_t)(lx * strength);
	pad.stickY     = (int8_t)(-lz * strength);
	return len;
}

/// A red Pikmin born and put in the captain's party, as the game's spawners do (throw_release, day_end).
inline Piki* spawnPiki(Navi* navi, int colour, int mode, float dx, float dz)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (!piki) return nullptr;
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at = navi->mSRT.t;
	at.x += dx;
	at.z += dz;
	at.y = mapMgr->getMinY(at.x, at.z, true);
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(mode, navi);
	return piki;
}

/// A sprout planted the way GoalAI::BootEmit plants a seed (onion_unlock_whistle): no Onion, dropped from 20 units.
inline PikiHeadItem* plantSprout(int colour, float x, float z)
{
	PikiHeadItem* seed = static_cast<PikiHeadItem*>(itemMgr->birth(OBJTYPE_Pikihead));
	if (!seed) return nullptr;
	Vector3f pos(x, mapMgr->getMinY(x, z, true) + 20.0f, z);
	seed->init(pos);
	seed->setColor(colour);
	seed->mVelocity.set(0.0f, 0.0f, 0.0f);
	seed->startAI(0);
	seed->mParentOnion = nullptr;
	C_SAI(seed)->start(seed, PikiHeadAI::PIKIHEAD_Flying);
	return seed;
}

inline int liveCount(int colour)
{
	int n = 0;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* p = static_cast<Piki*>(*it);
		if (p && p->mColor == colour) n++;
	}
	return n;
}

} // namespace tutorial

#endif
