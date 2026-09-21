#include "pc_coop.h"

static bool sPending = false;
static bool sActive  = false;

void pc_coop_set_pending(bool on) { sPending = on; }
bool pc_coop_pending(void) { return sPending; }

static bool sChosenAtTitle = false;
void pc_coop_set_chosen_at_title(bool on) { sChosenAtTitle = on; }
bool pc_coop_take_chosen_at_title(void)
{
	const bool v   = sChosenAtTitle;
	sChosenAtTitle = false;
	return v;
}

static int sCaptain[2] = { PC_CAPTAIN_OLIMAR, PC_CAPTAIN_LOUIE };
void pc_coop_set_captain(int player, int captain)
{
	if (player < 0 || player > 1) return;
	sCaptain[player] = captain == PC_CAPTAIN_LOUIE ? PC_CAPTAIN_LOUIE : PC_CAPTAIN_OLIMAR;
}
int pc_coop_captain(int player)
{
	if (player < 0 || player > 1) return PC_CAPTAIN_OLIMAR;
	// P1 también elige en 1 jugador (prompt de capitán tras Start).
	return sCaptain[player];
}
bool pc_coop_p2_tinted(void) { return sCaptain[0] == sCaptain[1]; }
void pc_coop_p2_tint(unsigned char* r, unsigned char* g, unsigned char* b)
{
	unsigned char c[3] = { 255, 255, 255 };
	if (pc_coop_p2_tinted()) {
		if (sCaptain[0] == PC_CAPTAIN_LOUIE) { c[0] = 255; c[1] = 120; c[2] = 110; } // Louie ya es azul: rojo
		else { c[0] = 90; c[1] = 150; c[2] = 255; }
	}
	if (r) *r = c[0];
	if (g) *g = c[1];
	if (b) *b = c[2];
}

void pc_coop_begin_run(void) { sActive = sPending; }
bool pc_coop_active(void) { return sActive; }
void pc_coop_end_run(void) { sActive = false; }
