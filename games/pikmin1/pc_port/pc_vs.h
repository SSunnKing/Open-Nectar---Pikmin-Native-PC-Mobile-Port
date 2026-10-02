#ifndef _PC_VS_H
#define _PC_VS_H

#include "types.h"

/*
 * Partida VS "Carrera de piezas": reglas, marcador, reloj, cuenta atrás y
 * cierre. El montaje del mapa está en GameCoreSection (pcVsSetupBases) y la
 * arena en mods/pc_vs_arena.
 */

/// Reglas de la partida (menú previo, se guardan en la configuración).
struct PcVsRules {
	f32 matchSeconds;    ///< duración
	bool rocketWin;      ///< el cohete se puede asediar y destruirlo gana
	f32 rocketHp;        ///< vida de cada cohete
	f32 bigPieceSeconds; ///< cuándo sale la gorda (0 = desde el inicio, <0 = nunca)
	int fieldLimit;      ///< Pikmin en el campo por jugador
	f32 pelletSeconds;   ///< cada cuánto reaparecen las pastillas (<=0 = nunca)
};
void pc_vs_set_rules(const PcVsRules& rules);
const PcVsRules& pc_vs_rules(void);

/// Asedio al cohete: radio, daño por Pikmin y reparación por pieza.
#define PC_VS_SIEGE_RADIUS     200.0f
#define PC_VS_SIEGE_DPS        0.125f /* con vida normal: 20 Pikmin lo tiran en 40 s */
#define PC_VS_REPAIR_PER_PIECE 10.0f

void pc_vs_match_reset(void);
/// Avanza el reloj (solo mientras se juega).
void pc_vs_match_update(f32 dt);
f32 pc_vs_match_elapsed(void);
f32 pc_vs_match_time_left(void);
bool pc_vs_match_over(void);

int pc_vs_score(int player);
void pc_vs_add_score(int player, int points);
/// Piezas entregadas por cada jugador (para el resumen final).
int pc_vs_pieces(int player);
/// Pikmin vivos de cada jugador (lo actualiza la partida cada fotograma).
void pc_vs_set_alive(int player, int count);
int pc_vs_alive(int player);
/// Cambia en cada partida nueva (para reiniciar estado de la interfaz).
int pc_vs_match_serial(void);

/// Puntos de cada pieza, por id de pastilla (0 si no es una pieza del VS).
void pc_vs_set_piece_points(u32 pelletId, int points);
int pc_vs_piece_points(u32 pelletId);

/// Eventos: cada uno devuelve true una sola vez cuando toca.
bool pc_vs_take_big_piece_event(void);
bool pc_vs_take_pellet_event(void);

f32 pc_vs_rocket_hp(int player);
/// Vida en porcentaje de la máxima.
int pc_vs_rocket_percent(int player);
void pc_vs_damage_rocket(int player, f32 amount, int attacker);
/// Daño a los dos cohetes en el mismo paso: si caen a la vez, empate.
void pc_vs_damage_rockets(f32 amount0, f32 amount1);
void pc_vs_repair_rocket(int player, f32 amount);
/// -1 sin decidir; 0/1 ganador; 2 empate.
int pc_vs_winner(void);
/// True si la partida acabó por cohete destruido.
bool pc_vs_won_by_rocket(void);

/// Cuenta atrás con el mundo en pausa. Se arma al montar el mapa y empieza
/// con el primer fotograma que se ve; va por tiempo real.
void pc_vs_countdown_arm(void);
/// -1 sin cuenta atrás (o ya acabó); 3, 2, 1; 0 = "START".
int pc_vs_countdown_phase(void);
/// True mientras el mundo tiene que seguir en pausa (antes del START).
bool pc_vs_countdown_holding(void);
/// Congela la cuenta atrás (menús del port abiertos encima).
void pc_vs_countdown_set_frozen(bool frozen);

/// Aviso grande en pantalla (texto y segundos que quedan de mostrarlo).
void pc_vs_announce(const char* text, f32 seconds);
/// No hay piezas de la nave suficientes en los datos del juego.
void pc_vs_set_missing_pieces(bool missing);
bool pc_vs_missing_pieces(void);
const char* pc_vs_announcement(void);

/// Salida de la partida pedida desde la pantalla final o la pausa.
enum { PC_VS_EXIT_NONE = 0, PC_VS_EXIT_REMATCH = 1, PC_VS_EXIT_TITLE = 2 };
void pc_vs_request_exit(int kind);
int pc_vs_take_exit_request(void);

#endif
