#include "pc_coop.h"
#include "GlobalGameOptions.h"
#include "mods/pc_hd_model_convert.h"
#include "mods/pc_hd_models.h"

#include <filesystem>

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
	sCaptain[player] = (captain >= 0 && captain < PC_CAPTAIN_COUNT) ? captain : PC_CAPTAIN_OLIMAR;
}
int pc_captain_piki_color(int captain)
{
	switch (captain) {
	case PC_CAPTAIN_PIKMIN_RED: return Red;
	case PC_CAPTAIN_PIKMIN_YELLOW: return Yellow;
	case PC_CAPTAIN_PIKMIN_BLUE: return Blue;
	default: return -1;
	}
}
bool pc_captain_is_pikmin(int captain)
{
	return pc_captain_piki_color(captain) >= 0 || captain == PC_CAPTAIN_PIKMIN_WHITE
	    || captain == PC_CAPTAIN_PIKMIN_PURPLE || captain == PC_CAPTAIN_BULBMIN;
}
int pc_captain_skeleton_color(int captain)
{
	const int color = pc_captain_piki_color(captain);
	if (color >= 0) return color;
	return pc_captain_is_pikmin(captain) ? Red : -1;
}
int pc_captain_pikmin2_model(int captain)
{
	switch (captain) {
	case PC_CAPTAIN_PRESIDENT: return PC_HD_MODEL_PRESIDENT;
	case PC_CAPTAIN_PIKMIN_WHITE: return PC_HD_MODEL_PIKI_WHITE;
	case PC_CAPTAIN_PIKMIN_PURPLE: return PC_HD_MODEL_PIKI_PURPLE;
	case PC_CAPTAIN_BULBMIN: return PC_HD_MODEL_BULBMIN;
	default: return -1;
	}
}
bool pc_captain_available(int captain)
{
	if (captain < 0 || captain >= PC_CAPTAIN_COUNT) return false;
	if (captain < PC_CAPTAIN_FIRST_PIKMIN2) return true;
	std::error_code ec;
	return pc_pikmin2_detected()
	    && std::filesystem::is_regular_file(pc_hd_model_path((PcHdModelId)pc_captain_pikmin2_model(captain)), ec);
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
		if (sCaptain[0] == PC_CAPTAIN_LOUIE || sCaptain[0] == PC_CAPTAIN_PIKMIN_BLUE) { c[0] = 255; c[1] = 120; c[2] = 110; } // ya son azules: rojo
		else { c[0] = 90; c[1] = 150; c[2] = 255; }
	}
	if (r) *r = c[0];
	if (g) *g = c[1];
	if (b) *b = c[2];
}

static bool sVsPending = false;
static bool sVsActive  = false;
void pc_vs_set_pending(bool on) { sVsPending = on; }
bool pc_vs_pending(void) { return sVsPending; }
bool pc_vs_active(void) { return sVsActive; }

void pc_coop_begin_run(void)
{
	sActive   = sPending;
	sVsActive = sPending && sVsPending;
}
bool pc_coop_active(void) { return sActive; }
void pc_coop_end_run(void)
{
	sActive   = false;
	sVsActive = false;
}
