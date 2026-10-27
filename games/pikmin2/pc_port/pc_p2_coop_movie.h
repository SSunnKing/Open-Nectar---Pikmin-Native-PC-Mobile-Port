#ifndef PC_P2_COOP_MOVIE_H
#define PC_P2_COOP_MOVIE_H

#include "pc_p2_coop.h"

#ifdef __cplusplus

#include "Vector3.h"

#ifdef PIKI_PC_PORT

namespace Game {
class Navi;
class Piki;
class Pellet;
}

/** Tag the captain who should own the next co-op full-screen cutscene (no-op unless co-op is active). */
void pc_p2_coop_tag_movie_protagonist(Game::Navi* navi);

/** Pikmin's squad captain, else nearest alive captain to the Pikmin. */
Game::Navi* pc_p2_coop_movie_navi_from_piki(Game::Piki* piki);

/** Captain with the most carriers on this pellet, else nearest captain to the pellet. */
Game::Navi* pc_p2_coop_movie_navi_from_pellet(Game::Pellet* pellet);

/** Nearest alive captain (Olimar/Louie) to a world position. */
Game::Navi* pc_p2_coop_movie_navi_nearest(const Vector3f& pos);

void pc_p2_coop_tag_movie_from_piki(Game::Piki* piki);
void pc_p2_coop_tag_movie_from_pellet(Game::Pellet* pellet);
void pc_p2_coop_tag_movie_from_position(const Vector3f& pos);

/**
 * Captain for one demo at the moment it starts.
 * `taggedProtagonist` is the explicit tag stored when that demo was requested
 * (null if nobody tagged it). `resolvedFallback` is resolved from THIS demo's
 * targets at playback, never from the demo that was already on screen.
 */
void* pc_p2_coop_movie_queue_slot_protagonist(void* taggedProtagonist, void* resolvedFallback);

#else

inline void pc_p2_coop_tag_movie_protagonist(Game::Navi*) {}
inline Game::Navi* pc_p2_coop_movie_navi_from_piki(Game::Piki*) { return nullptr; }
inline Game::Navi* pc_p2_coop_movie_navi_from_pellet(Game::Pellet*) { return nullptr; }
inline Game::Navi* pc_p2_coop_movie_navi_nearest(const Vector3f&) { return nullptr; }
inline void pc_p2_coop_tag_movie_from_piki(Game::Piki*) {}
inline void pc_p2_coop_tag_movie_from_pellet(Game::Pellet*) {}
inline void pc_p2_coop_tag_movie_from_position(const Vector3f&) {}
inline void* pc_p2_coop_movie_queue_slot_protagonist(void* tagged, void* fallback) { return tagged ? tagged : fallback; }

#endif /* PIKI_PC_PORT */

#endif /* __cplusplus */

#endif
