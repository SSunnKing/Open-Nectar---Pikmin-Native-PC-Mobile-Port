#pragma once
// Helpers shared by the disc-free W2 scenarios (pellet_table, ufo_part_discovery, pellet_carry,
// ship_part_carry, nectar_growth, bomb_firsts, carry_blocked): pellets born the way
// GenObjectPellet::birth makes them, free Pikmin handed to Piki::actOnSituaton as a landing
// Pikmin is (scenario_tutorial_day1.cpp), and the stand-in tree test. Builds on the tutorial
// scenarios' helpers (scenario_tutorial_common.h).
#if PIKI_SCENARIOS

#include <cstdio>
#include <cstring>
#include <vector>

#include "Dolphin/pad.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "scenarios/pc_scenario.h"
#include "scenarios/scenario_tutorial_common.h"

namespace carry {

using tutorial::currentMovie;
using tutorial::demoFlags;
using tutorial::flatDistance;
using tutorial::list;
using tutorial::liveCount;
using tutorial::newFlags;
using tutorial::setBits;
using tutorial::spawnPiki;
using tutorial::steer;

/// A four-character id as the game's multi-character constants hold it ('pr01').
inline u32 id(const char* s) { return (u32(u8(s[0])) << 24) | (u32(u8(s[1])) << 16) | (u32(u8(s[2])) << 8) | u32(u8(s[3])); }

/// True when the run's stage is one the generator wrote (stages/sandbox*.ini, stages/practice.ini); false on the disc's.
inline bool isStandInTree()
{
	const char* stage = pc_scenario_requested_stage_file();
	return stage && (std::strncmp(stage, "stages/sandbox", 14) == 0 || std::strncmp(stage, "stages/practice", 15) == 0);
}

/// A pellet born as GenObjectPellet::birth does (position, facing, AI, first state); null when the config or shape is missing.
inline Pellet* spawnPellet(u32 pelletId, float x, float z)
{
	Pellet* pelt = pelletMgr->newPellet(pelletId, nullptr);
	if (!pelt) return nullptr;
	Vector3f at(x, mapMgr->getMinY(x, z, true), z);
	pelt->init(at);
	pelt->mFaceDirection = pelt->mSRT.r.y;
	pelt->startAI(0);
	const float scale = pelt->mConfig->mPelletScale();
	pelt->mSRT.s.set(scale, scale, scale);
	pelt->mStateMachine->transit(pelt, 0);
	return pelt;
}

/// `n` free Pikmin of `colour` in a short row at (x, z), each added to `landing`, to be handed to actOnSituaton by land().
inline void spawnRow(Navi* navi, int colour, int n, float x, float z, std::vector<Piki*>& landing)
{
	for (int i = 0; i < n; i++) {
		const float dx = x - navi->mSRT.t.x + 6.0f * float(i - n / 2);
		const float dz = z - navi->mSRT.t.z + 4.0f * float(i % 2);
		if (Piki* p = spawnPiki(navi, colour, PikiMode::FreeMode, dx, dz)) landing.push_back(p);
	}
	GameStat::update();
}

/// What a thrown Pikmin does when it lands (PikiFlyingState::procBounceMsg): look for work in range and take it.
inline void land(std::vector<Piki*>& landing)
{
	if (landing.empty()) return;
	int modes[16] = {};
	for (Piki* p : landing) {
		p->actOnSituaton();
		modes[p->mMode & 15]++;
	}
	std::printf("[scenario] %zu Pikmin looked for work; modes:", landing.size());
	for (int m = 0; m < 16; m++)
		if (modes[m]) std::printf(" %d x%d", m, modes[m]);
	std::printf("\n");
	landing.clear();
}

} // namespace carry

#endif
