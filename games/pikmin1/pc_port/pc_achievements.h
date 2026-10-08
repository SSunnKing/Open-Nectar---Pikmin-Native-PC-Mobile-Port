#ifndef _PC_ACHIEVEMENTS_H
#define _PC_ACHIEVEMENTS_H

#include "types.h"

/*
 * Logros propios del port. Títulos del set de RetroAchievements de Pikmin
 * (GameCube) de SlashTangent, con su crédito; condiciones detectadas en el
 * código del juego, no leyendo memoria. Se guardan aparte de la partida
 * (pikmin_achievements.txt, junto a pikmin_settings.conf).
 */

enum PcAchievement {
	// Piezas de la nave (30), en el orden del set.
	PC_ACH_PART_NOVA_BLASTER,
	PC_ACH_PART_ANTI_DIOXIN_FILTER,
	PC_ACH_PART_OMEGA_STABILIZER,
	PC_ACH_PART_GRAVITY_JUMPER,
	PC_ACH_PART_ANALOG_COMPUTER,
	PC_ACH_PART_GUARD_SATELLITE,
	PC_ACH_PART_LIBRA,
	PC_ACH_PART_REPAIR_TYPE_BOLT,
	PC_ACH_PART_GLUON_DRIVE,
	PC_ACH_PART_ZIRCONIUM_ROTOR,
	PC_ACH_PART_INTERSTELLAR_RADIO,
	PC_ACH_PART_PILOT_SEAT,
	PC_ACH_PART_IONIUM_JET_2,
	PC_ACH_PART_BOWSPRIT,
	PC_ACH_PART_CHRONOS_REACTOR,
	PC_ACH_PART_SPACE_FLOAT,
	PC_ACH_PART_MASSAGE_MACHINE,
	PC_ACH_PART_UV_LAMP,
	PC_ACH_PART_SECRET_SAFE,
	// Colores de Pikmin (3).
	PC_ACH_PIKMIN_RED,
	PC_ACH_PIKMIN_YELLOW,
	PC_ACH_PIKMIN_BLUE,
	PC_ACH_PART_MAIN_ENGINE,
	PC_ACH_PART_POSITRON_GENERATOR,
	PC_ACH_PART_AUTOMATIC_GEAR,
	PC_ACH_PART_IONIUM_JET_1,
	PC_ACH_PART_WHIMSICAL_RADAR,
	PC_ACH_PART_EXTRAORDINARY_BOLT,
	PC_ACH_PART_SAGITTARIUS,
	PC_ACH_PART_ETERNAL_FUEL_DYNAMO,
	PC_ACH_PART_SHOCK_ABSORBER,
	PC_ACH_PART_GEIGER_COUNTER,
	PC_ACH_PART_RADIATION_CANOPY,
	// Historia y retos (6).
	PC_ACH_BAD_ENDING,
	PC_ACH_GOOLIX,
	PC_ACH_NORMAL_ENDING,
	PC_ACH_BEST_ENDING,
	PC_ACH_ALLERGIC_TO_BLUE,
	PC_ACH_SPEED_DEMON,
	// Modo desafío (5).
	PC_ACH_CHALLENGE_IMPACT_SITE,
	PC_ACH_CHALLENGE_FOREST_OF_HOPE,
	PC_ACH_CHALLENGE_FOREST_NAVEL,
	PC_ACH_CHALLENGE_DISTANT_SPRING,
	PC_ACH_CHALLENGE_FINAL_TRIAL,

	PC_ACH_COUNT
};

struct PcAchievementInfo {
	const char* title;
	const char* description;
	int points;
	u32 partId; ///< pieza de la nave (UFOID_*) o 0
};

const PcAchievementInfo& pc_achievement_info(int id);
/// Icono del logro: textura del propio juego (ruta para zen::loadTexExp).
const char* pc_achievement_icon(int id);

/// Carga el estado guardado (una vez; las demás llamadas no hacen nada).
void pc_achievements_load(void);
bool pc_achievement_unlocked(int id);
int pc_achievements_unlocked_count(void);
int pc_achievements_points(void);
int pc_achievements_total_points(void);

/// Desbloquea si se puede (no en VS, no con trucos) y avisa en pantalla.
void pc_achievement_unlock(int id);
/// Motivo por el que ahora no se desbloquean logros, o nullptr.
const char* pc_achievements_blocked_reason(void);

// Eventos del juego.
void pc_achievements_on_ship_part(u32 partId);
/// Fin de un día de modo desafío: stage = CHALSTAGE_* (0 Impact Site ... 4 Final Trial).
void pc_achievements_on_challenge_score(int stage, int score);

/// Aviso en pantalla: el logro que toca enseñar (o -1) y cuánto lleva visible.
int pc_achievements_toast(float* ageSeconds);
/// Aviso de "trucos activos": una vez por partida, la primera vez que un
/// logro no se desbloquea por ellos. nullptr si no toca.
const char* pc_achievements_blocked_toast(float* ageSeconds);

#endif
