#ifndef _PC_ACHIEVEMENTS_H
#define _PC_ACHIEVEMENTS_H

/*
 * Logros propios del port de Pikmin 2 (misma API que los de Open Nectar).
 * Pikmin 2 no tiene los enganches de Pikmin 1 (piezas de la nave, retos por
 * dia): aqui cada logro sale de los datos de la partida (PlayData), que se
 * consultan una vez por segundo durante el modo historia. Se guardan aparte
 * de la partida (pikmin2_achievements.txt, junto a pikmin_settings.conf).
 */

enum PcAchievement {
	// Tipos de Pikmin (5).
	PC_ACH_PIKMIN_RED,
	PC_ACH_PIKMIN_YELLOW,
	PC_ACH_PIKMIN_BLUE,
	PC_ACH_PIKMIN_PURPLE,
	PC_ACH_PIKMIN_WHITE,
	// Zonas (3).
	PC_ACH_AREA_AWAKENING_WOOD,
	PC_ACH_AREA_PERPLEXING_POOL,
	PC_ACH_AREA_WISTFUL_WILD,
	// Deuda (3).
	PC_ACH_DEBT_1000,
	PC_ACH_DEBT_5000,
	PC_ACH_DEBT_PAID,
	// Tesoros (4).
	PC_ACH_TREASURE_10,
	PC_ACH_TREASURE_50,
	PC_ACH_TREASURE_100,
	PC_ACH_TREASURE_ALL,
	// Sprays (2).
	PC_ACH_SPRAY_SPICY,
	PC_ACH_SPRAY_BITTER,

	PC_ACH_COUNT
};

struct PcAchievementInfo {
	const char* title;
	const char* description;
	int points;
};

const PcAchievementInfo& pc_achievement_info(int id);
/// Icono del logro: Pikmin 2 no los carga (el menu dibuja sin icono).
const char* pc_achievement_icon(int id);

/// Carga el estado guardado (una vez; las demas llamadas no hacen nada).
void pc_achievements_load(void);
bool pc_achievement_unlocked(int id);
int pc_achievements_unlocked_count(void);
int pc_achievements_points(void);
int pc_achievements_total_points(void);

/// Desbloquea si se puede (no con trucos) y avisa en pantalla.
void pc_achievement_unlock(int id);
/// Motivo por el que ahora no se desbloquean logros, o nullptr.
const char* pc_achievements_blocked_reason(void);

/// Revisa la partida (llamar cada frame de juego; mira una vez por segundo).
void pc_achievements_poll(void);

/// Aviso en pantalla: el logro que toca ensenar (o -1) y cuanto lleva visible.
int pc_achievements_toast(float* ageSeconds);
/// Aviso de "trucos activos": una vez por partida. nullptr si no toca.
const char* pc_achievements_blocked_toast(float* ageSeconds);

#endif
