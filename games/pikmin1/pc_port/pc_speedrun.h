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

// ─── Categorías (speedrun.com) ─────────────────────────────────────────────
// All Parts y Low-Pikmin: partida nueva → Secret Safe, un split por día.
// Low-Pikmin además cuenta los brotes que sacan las cebollas (50; 85 en la
// versión japonesa) y una run que se pasa no cuenta.
// 5 Parts: desde un race file (día 1 terminado y guardado). El reloj empieza
// al entrar al Bosque de la Esperanza el día 2 y acaba con la 5ª pieza; un
// split por pieza. El race file se guarda solo la primera vez que se entra.
// 200 Pikmin Challenge: Modo Desafío con su propia tarjeta y las cinco zonas
// abiertas (pc_speedrun_challenge_all_open, gameflow.h). El reloj empieza
// al elegir la primera zona y acaba al salir de la última con 200 Pikmin en
// las cinco; un split por zona.
enum {
    PC_SR_CAT_ALL_PARTS = 0,
    PC_SR_CAT_LOW_PIKMIN,
    PC_SR_CAT_5_PARTS,
    PC_SR_CAT_CHALLENGE,
    PC_SR_CAT_COUNT
};
// Límite de brotes de Low-Pikmin, como texto para los menús.
#if defined(VERSION_GPIJ01_01) || defined(VERSION_GPIJ01_02)
#define PC_SR_SPROUT_LIMIT_TEXT "85"
#else
#define PC_SR_SPROUT_LIMIT_TEXT "50"
#endif
int pc_speedrun_category(void);
const char* pc_speedrun_category_name(int category);
// Días en el registro (PB y últimas runs): solo en las categorías de historia completas.
bool pc_speedrun_category_uses_days(void);
// Subcarpeta de save/speedrun con la tarjeta de la categoría; nullptr = la
// tarjeta normal del jugador.
const char* pc_speedrun_card_subdir(void);

// Se entra a una zona desde el mapa (historia o Modo Desafío).
void pc_speedrun_on_enter_course(int stageId);
// Una cebolla saca un brote (Low-Pikmin).
void pc_speedrun_on_onion_sprout(void);
// Resultado de una zona del Modo Desafío (stageId de historia).
void pc_speedrun_on_challenge_result(int stageId, int score);

// ─── Reset rápido ──────────────────────────────────────────────────────────
// Mantener 1 s la acción PC_KEY_ACT_SPEEDRUN_RESET (Controls o menú Speedrun)
// descarta la run. take_reset: true una vez, en el fotograma en que se
// cumple el segundo; quien lo consume sale a CardSelect, que con take_restart
// empieza otra run de la misma categoría sin menús.
bool pc_speedrun_take_reset(void);
bool pc_speedrun_take_restart(void);

// ─── Modo práctica (All Parts y Low-Pikmin) ────────────────────────────────
// Un día suelto: el 1 es partida nueva; el N > 1 carga la tarjeta guardada al
// final del día N-1 de alguna run (punto de práctica, se copia al entrar a la
// zona ese día). El reloj mide solo ese día y se compara con su tramo del PB y
// con el mejor tramo. Nunca toca el registro ni LiveSplit.
// day 0 = práctica apagada (Start Run normal).
void pc_speedrun_set_practice_day(int day);
int pc_speedrun_practice_day(void);         // 0 si apagada o la categoría no tiene días
int pc_speedrun_next_practice_day(int day, int dir); // siguiente día disponible (0 = apagada)

// ─── Atajos de prueba (PIKMIN_SR_DEBUG=1) ──────────────────────────────────
// Solo con el modo activo y esa variable de entorno, para comprobar el modo
// sin jugar una run entera. F6 acaba el día (como "Ir al atardecer"), F7 cuenta
// una pieza para 5 Parts, F8 la Secret Safe, F9 acaba la zona del Desafío con
// 200, F10 suma 10 brotes y F11 salta el tutorial (día 1). F7, F8 y F10 solo
// avisan al speedrun: no cambian la partida.
bool pc_speedrun_debug_take_day_end(void); // F6/F9 pendiente: la partida acaba el día

// Puntos de práctica y race file de 5 Parts (card_stubs.cpp)
bool pc_speedrun_practice_restore(int day);
void pc_speedrun_practice_save(int day);
bool pc_speedrun_practice_exists(int day);
void pc_speedrun_practice_delete(int day);

// Race file de 5 Parts (card_stubs.cpp): copia de la tarjeta de la categoría.
bool pc_speedrun_race_restore(void); // race → tarjeta; false si no hay
void pc_speedrun_race_save(void);    // tarjeta → race, si aún no hay
void pc_speedrun_race_delete(void);

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
// Splits de LiveSplit (.lss) de la categoría en save/speedrun: los del PB con
// sus mejores tramos; sin PB, la plantilla de 5 Parts o del Desafío. Los
// nombres son los que el juego manda por LiveSplit. outName = nombre del
// archivo; false si no hay nada que exportar o no se pudo escribir.
bool pc_speedrun_export_lss(char* outName, int n);

// Reloj arriba a la izquierda y visor de controles debajo.
void pc_speedrun_draw(void);
