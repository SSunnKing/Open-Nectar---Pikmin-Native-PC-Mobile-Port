#include "pc_achievements.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <deque>

#include "Pellet.h"
#include "pc_coop.h"
#include "mods/pc_mission.h"
#include "settings/pc_settings.h"
#if PIKI_SCENARIOS
#include <cstdlib>
#include <string>

#include "scenarios/pc_scenario.h"
#endif

namespace {

// Títulos: set de RetroAchievements de Pikmin (GameCube), de SlashTangent.
const PcAchievementInfo kInfo[PC_ACH_COUNT] = {
	{ "It Has a Strange Allure", "Recover the Nova Blaster.", UFOID_NovaBlaster },
	{ "Eco-Friendly Exhaust", "Recover the Anti-Dioxin Filter.", UFOID_AntiDioxinFilter },
	{ "Straighten Up and Fly Right", "Recover the Omega Stabilizer.", UFOID_OmegaStabilizer },
	{ "Swim Gracefully Through the Sea of Stars... Like a Dolphin", "Recover the Gravity Jumper.", UFOID_GravityJumper },
	{ "HAL-9000", "Recover the Analog Computer.", UFOID_AnalogComputer },
	{ "Space Pirate Protection", "Recover the Guard Satellite.", UFOID_GuardSatellite },
	{ "A Daughter's Gift", "Recover the Libra.", UFOID_Libra },
	{ "Robot in Disguise", "Recover the Repair-type Bolt.", UFOID_RepairTypeBolt },
	{ "A Very Expensive Noisemaker", "Recover the Gluon Drive.", UFOID_GluonDrive },
	{ "I'm 40% Zirconium!", "Recover the Zirconium Rotor.", UFOID_ZirconiumRotor },
	{ "S.O.S. Signal Emitter", "Recover the Interstellar Radio.", UFOID_InterstellarRadio },
	{ "A Throne Fit for an Emperor", "Recover the Pilot's Seat.", UFOID_PilotSeat },
	{ "Easy on the Eyes AND the Budget", "Recover the #2 Ionium Jet.", UFOID_IoniumJet2 },
	{ "Aerodynamically Designed", "Recover the Bowsprit.", UFOID_Bowsprit },
	{ "Space-Time Continuum Rubber Band", "Recover the Chronos Reactor.", UFOID_ChronosReactor },
	{ "Always Be Prepared", "Recover the Space Float.", UFOID_SpaceFloat },
	{ "Lower Lumbar Support", "Recover the Massage Machine.", UFOID_MassageMachine },
	{ "Tanning Rays", "Recover the UV Lamp.", UFOID_UVLamp },
	{ "My Most Prized Possession!", "Recover the Secret Safe.", UFOID_SecretSafe },
	{ "Its Shape Is Similar to the Pikpik Brand Carrots I Love So Much", "Unlock the Red Pikmin.", 0 },
	{ "The Color Is Different, but It Seems to Be a Pikmin Nonetheless", "Unlock the Yellow Pikmin.", 0 },
	{ "Near This One's Cheeks Is What Appears to Be a Set of Gills", "Unlock the Blue Pikmin.", 0 },
	{ "Fate Has Smiled Upon Me!", "Recover the Main Engine.", UFOID_MainEngine },
	{ "Those Instant Space Noodles Will Taste Better When Heated Up", "Recover the Positron Generator.", UFOID_PositronGenerator },
	{ "This Thing Chugs Right Along at Its Own Pace", "Recover the Automatic Gear.", UFOID_AutomaticGear },
	{ "This Puts Out a Slightly Odoriferous Exhaust", "Recover the #1 Ionium Jet.", UFOID_IoniumJet1 },
	{ "With This, I'll Be Able to See All Nearby Ship Parts", "Recover the Whimsical Radar.", UFOID_WhimsicalRadar },
	{ "Exactly What Makes It So Extraordinary Is a Secret", "Recover the Extraordinary Bolt.", UFOID_ExtraordinaryBolt },
	{ "My Son Gave This to Me as a Present", "Recover the Sagittarius.", UFOID_Sagittarius },
	{ "I Won't Have to Worry About Saving Electricity Anymore!", "Recover the Eternal Fuel Dynamo.", UFOID_EternalFuelDynamo },
	{ "It's Smooth Sailing With This in Place... Usually", "Recover the Shock Absorber.", UFOID_ShockAbsorber },
	{ "I Don't Actually Know What It's For", "Recover the Geiger Counter.", UFOID_GeigerCounter },
	{ "I'll Sleep Like a Baby Once I Get This Back to the Ship!", "Recover the Radiation Canopy.", UFOID_RadiationCanopy },
	{ "Pikmar", "Get the bad ending.", 0 },
	{ "Having a GOOd Time", "Defeat Goolix.", 0 },
	{ "I Have No Choice but to Try to Blast Off", "Collect all 25 critical ship parts, but not all 30, and get the normal ending.", 0 },
	{ "Now I Can Return Home to Hocotate!", "Collect all 30 ship parts and get the best ending.", 0 },
	{ "Allergic to Blue", "Collect all ship parts in the Forest of Hope before unlocking the Blue Pikmin.", 0 },
	{ "Hocotate Speed Demon", "Collect all 30 ship parts and beat the game in 12 days or less.", 0 },
	{ "Shipwreck Point", "In Challenge Mode, get a score of 200 or higher in The Impact Site.", 0 },
	{ "Forest of Hope", "In Challenge Mode, get a score of 400 or higher in The Forest of Hope.", 0 },
	{ "Navel of the Forest", "In Challenge Mode, get a score of 300 or higher in The Forest Navel.", 0 },
	{ "Where Boys Become Dandori", "In Challenge Mode, get a score of 450 or higher in The Distant Spring.", 0 },
	{ "Final Ordeal", "In Challenge Mode, get a score of 200 or higher in The Final Trial.", 0 },
};

constexpr const char* kFilename  = "pikmin_achievements.txt";
constexpr Uint32 kToastMs        = 4500;
bool sLoaded                     = false;
bool sUnlocked[PC_ACH_COUNT]     = {};
std::deque<int> sToasts;
Uint32 sToastStart               = 0;
Uint32 sBlockedToastStart        = 0;
bool sBlockedToastShown          = false;

// The file sits in the working directory. A scenario run changes into the shared
// data folder, so there it goes to the run's own NECTAR_SAVE_DIR instead: a
// scenario that unlocks one writes nothing into the stand-in tree, and parallel
// runs each start with none unlocked.
const char* filePath()
{
#if PIKI_SCENARIOS
	if (pc_scenario_active()) {
		static std::string path;
		const char* dir = std::getenv("NECTAR_SAVE_DIR");
		if (path.empty() && dir && *dir) path = std::string(dir) + "/" + kFilename;
		if (!path.empty()) return path.c_str();
	}
#endif
	return kFilename;
}

void save()
{
	FILE* out = fopen(filePath(), "w");
	if (!out) {
		printf("[Achievements] Failed to write %s\n", filePath());
		return;
	}
	fprintf(out, "# Open Nectar achievements (unlocked ids)\n");
	for (int i = 0; i < PC_ACH_COUNT; i++) {
		if (sUnlocked[i]) fprintf(out, "%d\n", i);
	}
	fclose(out);
}

} // namespace

const PcAchievementInfo& pc_achievement_info(int id) { return kInfo[(id >= 0 && id < PC_ACH_COUNT) ? id : 0]; }

const char* pc_achievement_icon(int id)
{
	// Solo imágenes del juego, que salen del disco del usuario: el cohete
	// para las piezas (no hay iconos 2D de cada pieza, se ven en 3D), las
	// caras de cada color y las fotos de las zonas.
	if (id >= 0 && id < PC_ACH_COUNT && kInfo[id].partId) {
		// Icono de la lista de piezas (ufo_p.blo): la etiqueta ui01..ui30 va
		// en el orden de la tabla de piezas y cada una usa su imageNNN.bti.
		static const short kPartImage[MAX_UFO_PARTS] = { 4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 27, 29, 31,
			                                             32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60 };
		static char path[40];
		const int idx = PelletMgr::getUfoIndexFromID(kInfo[id].partId);
		if (idx < 0 || idx >= MAX_UFO_PARTS) return "screen/tex/i_rocket.bti";
		snprintf(path, sizeof(path), "screen/tex/image%03d.bti", kPartImage[idx]);
		return path;
	}
	switch (id) {
	case PC_ACH_PIKMIN_RED: return "screen/tex/rp_f64.bti";
	case PC_ACH_PIKMIN_YELLOW: return "screen/tex/yp_f64.bti";
	case PC_ACH_PIKMIN_BLUE: return "screen/tex/bp_f64.bti";
	case PC_ACH_BAD_ENDING: return "screen/tex/end_navi.bti";
	case PC_ACH_NORMAL_ENDING: return "screen/tex/rocket2.bti";
	case PC_ACH_BEST_ENDING: return "screen/tex/c_star.bti";
	case PC_ACH_SPEED_DEMON: return "screen/tex/sun_64.bti";
	case PC_ACH_GOOLIX: return "screen/tex/ca_snap.bti";
	case PC_ACH_ALLERGIC_TO_BLUE: return "screen/tex/fo_snap.bti";
	case PC_ACH_CHALLENGE_IMPACT_SITE: return "screen/tex/pr_snap.bti";
	case PC_ACH_CHALLENGE_FOREST_OF_HOPE: return "screen/tex/fo_snap.bti";
	case PC_ACH_CHALLENGE_FOREST_NAVEL: return "screen/tex/ca_snap.bti";
	case PC_ACH_CHALLENGE_DISTANT_SPRING: return "screen/tex/ya_snap.bti";
	case PC_ACH_CHALLENGE_FINAL_TRIAL: return "screen/tex/ga_snap.bti";
	default: return "screen/tex/parts32.bti";
	}
}

void pc_achievements_load(void)
{
	if (sLoaded) return;
	sLoaded = true;
	FILE* in = fopen(filePath(), "r");
	if (!in) return;
	char line[64];
	while (fgets(line, sizeof(line), in)) {
		if (line[0] == '#') continue;
		int id = -1;
		if (sscanf(line, "%d", &id) == 1 && id >= 0 && id < PC_ACH_COUNT) sUnlocked[id] = true;
	}
	fclose(in);
}

bool pc_achievement_unlocked(int id)
{
	pc_achievements_load();
	return id >= 0 && id < PC_ACH_COUNT && sUnlocked[id];
}

int pc_achievements_unlocked_count(void)
{
	pc_achievements_load();
	int n = 0;
	for (bool u : sUnlocked) n += u;
	return n;
}

const char* pc_achievements_blocked_reason(void)
{
	if (pc_vs_active()) return "Achievements are off in Versus mode.";
	if (pc_mission_active()) return "Achievements are off in custom maps.";
	if (pc_settings_get_piki_invincible() || pc_settings_get_all_flowers() || pc_settings_get_unlock_zones()
	    || pc_settings_get_no_day_advance() || pc_settings_get_all_onions() || pc_settings_get_infinite_day()
	    || pc_settings_get_carry_speed_scale() != 1.0f || pc_settings_get_navi_speed_scale() != 1.0f
	    || pc_settings_get_navi_health_pct() != 100 || pc_settings_get_teki_health_pct() != 100
	    || pc_settings_get_piki_limit() > 100) {
		return "Achievements are off while cheats are on.";
	}
	return nullptr;
}

void pc_achievement_unlock(int id)
{
	if (id < 0 || id >= PC_ACH_COUNT) return;
	pc_achievements_load();
	if (sUnlocked[id]) return;
	if (pc_achievements_blocked_reason()) {
		if (!sBlockedToastShown) {
			sBlockedToastShown = true;
			sBlockedToastStart = SDL_GetTicks();
		}
		return;
	}
	sUnlocked[id] = true;
	save();
	printf("[Achievements] Unlocked: %s\n", kInfo[id].title);
	if (sToasts.empty()) sToastStart = SDL_GetTicks();
	sToasts.push_back(id);
}

void pc_achievements_on_ship_part(u32 partId)
{
	for (int i = 0; i < PC_ACH_COUNT; i++) {
		if (kInfo[i].partId == partId) {
			pc_achievement_unlock(i);
			return;
		}
	}
}

void pc_achievements_on_challenge_score(int stage, int score)
{
	static const int kMin[5] = { 200, 400, 300, 450, 200 };
	if (stage < 0 || stage >= 5 || score < kMin[stage]) return;
	pc_achievement_unlock(PC_ACH_CHALLENGE_IMPACT_SITE + stage);
}

int pc_achievements_toast(float* ageSeconds)
{
	const Uint32 now = SDL_GetTicks();
	while (!sToasts.empty() && now - sToastStart >= kToastMs) {
		sToasts.pop_front();
		sToastStart = now;
	}
	if (sToasts.empty()) return -1;
	if (ageSeconds) *ageSeconds = (now - sToastStart) / 1000.0f;
	return sToasts.front();
}

const char* pc_achievements_blocked_toast(float* ageSeconds)
{
	if (!sBlockedToastStart) return nullptr;
	const Uint32 age = SDL_GetTicks() - sBlockedToastStart;
	if (age >= kToastMs) {
		sBlockedToastStart = 0;
		return nullptr;
	}
	if (ageSeconds) *ageSeconds = age / 1000.0f;
	return pc_achievements_blocked_reason();
}
