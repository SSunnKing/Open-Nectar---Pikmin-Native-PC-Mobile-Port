// Pikmin 2: Hard y Permadeath por partida (ver pc_p2_rules.h).

#include "pc_p2_rules.h"

#include <SDL2/SDL.h>
#include <cstdio>

#include "Game/Data.h"
#include "Game/MemoryCard/Mgr.h"
#include "System.h"
#include "settings/pc_settings.h"

namespace {
// Mismas escalas que el Hard de Pikmin 1.
const float kTekiLifeScale  = 1.33f;
const float kNaviDamageScale = 1.5f;
// Bayas por spray: el doble que el original.
const int kDopeCountScale = 2;

bool sActivePermadeath  = false;
bool sActiveHard        = false;
bool sPendingPermadeath = false;
bool sPendingHard       = false;
int  sSlotBits[3]       = { 0, 0, 0 };
} // namespace

extern "C" {

int pc_p2_permadeath_active(void) { return sActivePermadeath ? 1 : 0; }

int pc_hardmode_active_c(void) { return sActiveHard ? 1 : 0; }

void pc_p2_rules_set_pending(int permadeath, int hard)
{
	sPendingPermadeath = permadeath != 0;
	sPendingHard       = hard != 0;
}

void pc_p2_rules_begin_new_run(void)
{
	// Lo pendiente se consume: una partida nueva sin pasar por la ventana
	// (p. ej. sin tarjeta) sale Normal/Standard.
	sActivePermadeath  = sPendingPermadeath;
	sActiveHard        = sPendingHard;
	sPendingPermadeath = false;
	sPendingHard       = false;
	std::printf("[P2 rules] new run: %s%s\n", sActivePermadeath ? "PERMADEATH" : "standard", sActiveHard ? " HARD" : "");
}

int pc_p2_rules_save_bits(void)
{
	return (sActivePermadeath ? PC_P2_RULE_PERMADEATH : 0) | (sActiveHard ? PC_P2_RULE_HARD : 0);
}

void pc_p2_rules_adopt(int headerByte)
{
	sActivePermadeath = (headerByte & PC_P2_RULE_PERMADEATH) != 0;
	sActiveHard       = (headerByte & PC_P2_RULE_HARD) != 0;
	std::printf("[P2 rules] loaded: %s%s\n", sActivePermadeath ? "PERMADEATH" : "standard", sActiveHard ? " HARD" : "");
}

float pc_p2_hard_teki_life(float base) { return sActiveHard ? base * kTekiLifeScale : base; }

float pc_p2_hard_navi_damage(float base) { return sActiveHard ? base * kNaviDamageScale : base; }

int pc_p2_hard_dope_count(int base) { return sActiveHard ? base * kDopeCountScale : base; }

int pc_p2_hard_single_nectar(void) { return sActiveHard ? 1 : 0; }

void pc_p2_rules_erase_current_save(void)
{
	Game::MemoryCard::Mgr* mgr = static_cast<Game::MemoryCard::Mgr*>(sys->mCardMgr);
	const int slot             = sys->mPlayData->mFileIndex;
	if (!mgr || slot < 0 || slot >= 3) {
		std::printf("[P2 rules] permadeath: slot %d fuera de rango, no se borra nada\n", slot);
		return;
	}
	// deletePlayer usa OSTryLockMutex: si el hilo de la tarjeta está ocupado
	// falla sin encolar nada, así que se reintenta un rato.
	for (int tries = 0; tries < 200; tries++) {
		if (mgr->deletePlayer(slot)) {
			std::printf("[P2 rules] permadeath: borrando partida %d\n", slot);
			sActivePermadeath = false;
			sActiveHard       = false;
			sSlotBits[slot]   = 0;
			pc_erased_notice_queue();
			return;
		}
		SDL_Delay(5);
	}
	std::printf("[P2 rules] permadeath: no se pudo encolar el borrado de la partida %d\n", slot);
}

void pc_p2_rules_note_slot(int slot, int headerByte)
{
	if (slot >= 0 && slot < 3)
		sSlotBits[slot] = headerByte & PC_P2_RULE_MASK;
}

int pc_p2_rules_slot_bits(int slot) { return (slot >= 0 && slot < 3) ? sSlotBits[slot] : 0; }

} // extern "C"
