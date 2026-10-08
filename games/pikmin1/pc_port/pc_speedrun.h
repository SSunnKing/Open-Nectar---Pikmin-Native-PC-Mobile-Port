#pragma once

#include "types.h"

// Modo Speedrun: se elige en el título (bajo Start). Mientras está activo,
// todos los ajustes que cambian la jugabilidad devuelven su valor original
// (Pikmin 1 vanilla) y se dibuja un reloj en tiempo real con un visor de
// los controles.
//
// Cronometraje de speedrun.com, categoría All Parts: tiempo real (RTA, sin
// quitar cargas), empieza al seleccionar una partida nueva y termina al
// recoger la Secret Safe.

void pc_speedrun_set_active(bool on);
bool pc_speedrun_active(void);

// Partida nueva elegida en la selección de partida: arranca el reloj.
void pc_speedrun_start_timer(void);
// Una pieza ha llegado a la nave; la Secret Safe para el reloj y cierra la
// run: último split, récords actualizados y guardados.
void pc_speedrun_on_ship_part(u32 partId);
// Fin de un día (zona StageID jugada): un split "Zona N" con el tiempo
// acumulado, como en LiveSplit.
void pc_speedrun_on_day_end(int stageId, int day);

// ─── Registro de tiempos (save/speedrun/records) ───────────────────────────
// Mejor run (PB) con sus splits, mejor tramo por split (dorado), Sum of Best
// y las últimas runs completas. Tiempos en milisegundos.
void pc_speedrun_format_time(u64 ms, char* out, int n);
bool pc_speedrun_has_pb(void);
u64 pc_speedrun_pb_ms(void);
int pc_speedrun_pb_days(void);
const char* pc_speedrun_pb_date(void);
int pc_speedrun_pb_split_count(void);
const char* pc_speedrun_pb_split_name(int i);
u64 pc_speedrun_pb_split_ms(int i);
u64 pc_speedrun_sum_of_best(void);
int pc_speedrun_recent_count(void);
void pc_speedrun_recent(int i, u64* ms, int* days, const char** date);
void pc_speedrun_reset_records(void);

// Reloj arriba a la izquierda y visor de controles debajo.
void pc_speedrun_draw(void);
