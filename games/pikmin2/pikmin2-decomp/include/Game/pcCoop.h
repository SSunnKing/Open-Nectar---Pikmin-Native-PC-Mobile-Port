#ifndef _GAME_PCCOOP_H
#define _GAME_PCCOOP_H

#include "Game/GameSystem.h"

// Port: dos capitanes con mando a la vez. Además del VS y el desafío 2P
// (isMultiplayerMode), el co-op de la historia (docs/PLAN_COOP.md §2.1).
// No se redefine isMultiplayerMode: las secciones de VS y desafío la usan para
// otras cosas (puntuación, tiempo, resultado).
#ifdef PIKI_PC_PORT
#include "pc_p2_coop.h"
#include "pc_p2_coop_movie.h"
// Lo que cambia en cada frame: con Louie ya disponible.
#define PC_TWO_CAPTAIN_PLAY() (Game::gameSystem->isMultiplayerMode() || pc_p2_coop_active())
// Lo que se decide al cargar (recursos): basta con la sesión co-op.
#define PC_TWO_CAPTAIN_SESSION() (Game::gameSystem->isMultiplayerMode() || pc_p2_coop_session())
// El capitán de quien abrió el menú de pausa (mapa, datos).
#define PC_MENU_NAVI() (Game::naviMgr->getMenuNavi())
#else
#define PC_TWO_CAPTAIN_PLAY()    (Game::gameSystem->isMultiplayerMode())
#define PC_MENU_NAVI()           (Game::naviMgr->getActiveNavi())
#define PC_TWO_CAPTAIN_SESSION() (Game::gameSystem->isMultiplayerMode())
#endif

#endif
