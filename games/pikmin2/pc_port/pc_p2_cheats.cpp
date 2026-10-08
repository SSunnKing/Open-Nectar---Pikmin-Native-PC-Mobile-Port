// Trucos de F1 > Cheats que en Pikmin 2 se aplican escribiendo en la partida
// (PlayData) en vez de en un punto del juego. Se revisan una vez por segundo
// durante el modo historia; al guardar el dia quedan en la tarjeta.

#include <SDL2/SDL.h>

#include "Game/gamePlayData.h"
#include "Game/GameSystem.h"
#include "Game/Piki.h"
#include "Game/PikiMgr.h"
#include "PikiAI.h"
#include "settings/pc_settings.h"

void pc_p2_cheats_poll(void)
{
	static Uint32 sLastPoll = 0;
	const Uint32 now        = SDL_GetTicks();
	if (now - sLastPoll < 1000) return;
	sLastPoll = now;
	if (!Game::playData || !Game::gameSystem || !Game::gameSystem->isStoryMode()) return;

	Game::PlayData* pd = Game::playData;
	// "Unlock All Zones": las tres zonas tras el Valle del Reposo.
	if (pc_settings_get_unlock_zones()) {
		for (int i = 1; i <= 3; i++) {
			if (!pd->courseOpen(i)) pd->openCourse(i);
		}
	}
	// "All Onions": las tres cebollas (Morados y Blancos no tienen).
	if (pc_settings_get_all_onions()) {
		static const int kOnions[3] = { Game::Red, Game::Yellow, Game::Blue };
		for (int c : kOnions) {
			if (!pd->hasContainer(c)) {
				pd->setContainer(c);
				pd->setBootContainer(c);
				pd->setMeetPikmin(c);
			}
		}
	}
}

// "Idle Pikmin Counter": el menu dibuja GameStat::freePikis (shim de P1). En
// Pikmin 2 se cuentan aqui los Pikmin vivos sin tarea (accion Free).
struct GameStat {
	static int freePikis;
};

void pc_p2_count_idle_pikis(void)
{
	static Uint32 sLastCount = 0;
	const Uint32 now         = SDL_GetTicks();
	if (now - sLastCount < 200) return;
	sLastCount = now;
	if (!pc_settings_get_idle_counter() || !Game::pikiMgr) {
		GameStat::freePikis = 0;
		return;
	}
	int idle = 0;
	Iterator<Game::Piki> it(Game::pikiMgr);
	CI_LOOP(it)
	{
		Game::Piki* piki = *it;
		if (piki->isAlive() && !piki->isZikatu() && piki->getCurrActionID() == PikiAI::ACT_Free) idle++;
	}
	GameStat::freePikis = idle;
}

// Diagnóstico (PIKMIN2_PIKI_TRACE=1): una vez por segundo, posición, estado,
// acción y si está en el agua de cada Pikmin vivo, junto a la del capitán.
void pc_p2_trace_pikis(void)
{
	static const bool enabled = getenv("PIKMIN2_PIKI_TRACE") != nullptr;
	static Uint32 sLast       = 0;
	if (!enabled || !Game::pikiMgr) return;
	const Uint32 now = SDL_GetTicks();
	if (now - sLast < 1000) return;
	sLast = now;
	int n = 0;
	Iterator<Game::Piki> it(Game::pikiMgr);
	CI_LOOP(it)
	{
		Game::Piki* piki = *it;
		if (!piki->isAlive()) continue;
		const Vector3f p = piki->getPosition();
		fprintf(stderr, "[PIKI] #%d kind=%d state=%d action=%d pos=(%.0f, %.0f, %.0f) water=%d visible=%d navi=%p\n", n++,
		        (int)piki->getKind(), piki->getStateID(), piki->getCurrActionID(), p.x, p.y, p.z, piki->mWaterBox ? 1 : 0,
		        piki->isAlive() && !piki->isZikatu() ? 1 : 0, (void*)piki->mNavi);
	}
	fflush(stderr);
}

#if PIKI_PC_TOUCH
#include "Game/Navi.h"
#include "Game/NaviState.h"
#include "Game/AIConstants.h"
#include "touch/pc_touch.h"

// Táctil: qué botones de contexto tienen sentido este frame (cruceta ↑/↓ con
// sprays o madurez, cambiar de capitán). Mismas condiciones que el juego.
void pc_p2_touch_context_poll(void)
{
	unsigned flags = 0;
	Game::Navi* navi = Game::naviMgr ? Game::naviMgr->getActiveNavi() : nullptr;
	if (navi && Game::playData && Game::gameSystem) {
		if (navi->getStateID() == Game::NSID_ThrowWait) flags |= PC_TOUCH_CTX_HOLDING;
		if (Game::playData->mSprayCount[SPRAY_TYPE_BITTER] > 0) flags |= PC_TOUCH_CTX_SPRAY_BITTER;
		if (Game::playData->mSprayCount[SPRAY_TYPE_SPICY] > 0) flags |= PC_TOUCH_CTX_SPRAY_SPICY;
		if (!Game::gameSystem->isMultiplayerMode() && Game::playData->isDemoFlag(Game::DEMO_Unlock_Captain_Switch)) {
			Game::Navi* other = Game::naviMgr->getAt(1 - navi->mNaviIndex);
			if (other && other->isAlive()) flags |= PC_TOUCH_CTX_SWAP;
			if (navi->mNaviIndex == NAVIID_Olimar) flags |= PC_TOUCH_CTX_SWAP_TO_2P;
			if (Game::playData->isStoryFlag(Game::STORY_DebtPaid)) flags |= PC_TOUCH_CTX_PRESIDENT;
		}
	}
	pc_touch_set_context(flags);
}
#endif
