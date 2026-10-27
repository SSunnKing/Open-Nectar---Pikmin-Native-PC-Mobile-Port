#include "pc_p2_coop_movie.h"

#include "Game/Navi.h"
#include "Game/Piki.h"
#include "Game/pelletMgr.h"
#include "Game/Stickers.h"
#include "Container.h"
#include "nans.h"

void pc_p2_coop_tag_movie_protagonist(Game::Navi* navi)
{
	if (!pc_p2_coop_active() || !navi || !navi->isAlive()) {
		return;
	}
	pc_p2_coop_set_movie_protagonist(navi);
}

Game::Navi* pc_p2_coop_movie_navi_nearest(const Vector3f& pos)
{
	if (!Game::naviMgr) {
		return nullptr;
	}

	Game::Navi* best    = nullptr;
	f32 bestDistSquared = 0.0f;
	for (int i = NAVIID_Olimar; i <= NAVIID_Louie; i++) {
		Game::Navi* navi = Game::naviMgr->getAt(i);
		if (!navi || !navi->isAlive()) {
			continue;
		}
		// Compare squared length. FLOAT_DIST_MAX is 128000, a distance, so using
		// it as a ceiling drops any captain more than ~358 units from the spot.
		Vector3f delta = pos;
		delta -= navi->getPosition();
		const f32 distSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
		if (!best || distSquared < bestDistSquared) {
			bestDistSquared = distSquared;
			best            = navi;
		}
	}
	return best;
}

Game::Navi* pc_p2_coop_movie_navi_from_piki(Game::Piki* piki)
{
	if (!piki) {
		return nullptr;
	}
	if (piki->mNavi && piki->mNavi->isAlive()) {
		return piki->mNavi;
	}
	return pc_p2_coop_movie_navi_nearest(piki->getPosition());
}

Game::Navi* pc_p2_coop_movie_navi_from_pellet(Game::Pellet* pellet)
{
	if (!pellet) {
		return nullptr;
	}

	int carryCount[2] = { 0, 0 };
	Game::Stickers stick(pellet);
	Iterator<Game::Creature> it(&stick);
	CI_LOOP(it)
	{
		Game::Creature* creature = *it;
		if (!creature || !creature->isPiki()) {
			continue;
		}
		Game::Piki* piki = static_cast<Game::Piki*>(creature);
		if (piki->mNavi && piki->mNavi->isAlive() && piki->mNavi->mNaviIndex >= NAVIID_Olimar
		    && piki->mNavi->mNaviIndex <= NAVIID_Louie) {
			carryCount[piki->mNavi->mNaviIndex]++;
		}
	}

	int bestIndex = -1;
	int bestCount = 0;
	for (int i = NAVIID_Olimar; i <= NAVIID_Louie; i++) {
		if (carryCount[i] > bestCount) {
			bestCount = carryCount[i];
			bestIndex = i;
		}
	}
	if (bestIndex >= 0 && Game::naviMgr) {
		Game::Navi* navi = Game::naviMgr->getAt(bestIndex);
		if (navi && navi->isAlive()) {
			return navi;
		}
	}
	return pc_p2_coop_movie_navi_nearest(pellet->getPosition());
}

void pc_p2_coop_tag_movie_from_piki(Game::Piki* piki)
{
	pc_p2_coop_tag_movie_protagonist(pc_p2_coop_movie_navi_from_piki(piki));
}

void pc_p2_coop_tag_movie_from_pellet(Game::Pellet* pellet)
{
	pc_p2_coop_tag_movie_protagonist(pc_p2_coop_movie_navi_from_pellet(pellet));
}

void pc_p2_coop_tag_movie_from_position(const Vector3f& pos)
{
	pc_p2_coop_tag_movie_protagonist(pc_p2_coop_movie_navi_nearest(pos));
}
