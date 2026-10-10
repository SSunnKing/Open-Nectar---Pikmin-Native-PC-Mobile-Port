#pragma once

// Modo Speedrun de Pikmin 2 (docs/PLAN_SPEEDRUN.md). Mientras está activo se
// cronometra la run en tiempo real (RTA, sin quitar cargas) con un split por
// día y por cueva, y al terminar se guarda el registro de la categoría en
// save/speedrun/records-<categoría>.
//
// Se activa con la opción Speedrun del título (TTitleMenu) y se apaga al volver
// a él. La categoría y los splits visibles se eligen en el menú del modo
// (pc_settings). Una run es una sola sentada: no hay ranuras ni guardado.
// Splits y cierre también por terminal. Atajos de prueba con PIKMIN_SR_DEBUG=1:
// F6 acaba el día, F7 sale de la cueva, F8 cierra la categoría.
//
// API C: se llama desde el decomp.

#ifdef __cplusplus
extern "C" {
#endif

// Categorías de speedrun.com (juego pdv9zv1w).
// Pay Off Debt y No Major Exploits: partida nueva → pago de la deuda.
// All Treasures: partida nueva → juego completado (todos los tesoros).
// Desafío (Challenge Mode All Levels / All Treasures): el Modo Desafío con sus
// niveles como en una partida nueva (los cinco primeros abiertos). El reloj
// empieza al elegir el primer nivel y acaba con el resultado del último que
// falte: superado (All Levels) o con todos sus tesoros (All Treasures). Un
// split por nivel. No guarda: el progreso del Desafío del jugador vuelve a
// estar como estaba al volver al título.
enum {
    PC_SR_CAT_PAY_OFF_DEBT = 0,
    PC_SR_CAT_ALL_TREASURES,
    PC_SR_CAT_NO_MAJOR_EXPLOITS,
    PC_SR_CAT_CHALLENGE_LEVELS,
    PC_SR_CAT_CHALLENGE_TREASURES,
    PC_SR_CAT_COUNT
};

// Cómo termina la partida en GameState (singleGS_MainGame.cpp).
enum {
    PC_SR_FINISH_DEBT = 0,     // RDS_GoToPayDebt
    PC_SR_FINISH_COMPLETE = 1, // RDS_GameComplete
    PC_SR_FINISH_CHALLENGE = 2, // pc_speedrun_on_challenge_result
};

void pc_speedrun_set_active(int on);
int pc_speedrun_active(void);
int pc_speedrun_category(void);
const char* pc_speedrun_category_name(int category);
// Categoría del Modo Desafío (la run va en VsGameSection, no en la historia).
int pc_speedrun_category_is_challenge(void);
// Desafío: la run está elegida y el reloj espera al primer nivel.
int pc_speedrun_challenge_awaiting(void);
// Desafío: se elige un nivel (índice de la lista); acaba un subnivel (al bajar
// por el agujero o salir por el géiser) con remaining tesoros sin recoger en
// el radar; resultado del nivel (cleared = salida por el géiser).
void pc_speedrun_on_challenge_start(int stage);
void pc_speedrun_on_challenge_floor_end(int remainingTreasures);
void pc_speedrun_on_challenge_result(int stage, int displayIndex, int cleared, int stageCount);
// Desafío: guarda el progreso del jugador (niveles, flores, récords) y deja
// el de una partida nueva; restore lo devuelve (al volver al título).
void pc_speedrun_challenge_progress_reset(void);
void pc_speedrun_challenge_progress_restore(void);

// Partida nueva elegida en el selector de ficheros: arranca el reloj.
void pc_speedrun_start_timer(void);
// Fin de un día en superficie (CourseInfo::mCourseIndex).
void pc_speedrun_on_day_end(int courseIndex);
// Salida de una cueva con resultado (SingleGameSection::mCaveID, p. ej. 't_01').
void pc_speedrun_on_cave_exit(unsigned caveId);
// La partida termina (PC_SR_FINISH_*, o -1 = la de la categoría en curso);
// days = días jugados.
void pc_speedrun_on_finish(int kind, int days);

// ─── Registro de tiempos (save/speedrun/records-<categoría>) ───────────────
// Mejor run (PB) con sus splits, mejor tramo por split (dorado), Sum of Best
// y las últimas runs completas, de la categoría elegida. Tiempos en ms.
void pc_speedrun_format_time(unsigned long long ms, char* out, int n);
int pc_speedrun_has_pb(void);
unsigned long long pc_speedrun_pb_ms(void);
int pc_speedrun_pb_days(void);
const char* pc_speedrun_pb_date(void);
int pc_speedrun_pb_split_count(void);
const char* pc_speedrun_pb_split_name(int i);
unsigned long long pc_speedrun_pb_split_ms(int i);
unsigned long long pc_speedrun_sum_of_best(void);
int pc_speedrun_recent_count(void);
void pc_speedrun_recent(int i, unsigned long long* ms, int* days, const char** date);
void pc_speedrun_reset_records(void);
// Splits de LiveSplit (.lss) de la categoría en save/speedrun: los del PB con
// sus mejores tramos, con los mismos nombres que los splits del juego.
// outName = nombre del archivo; 0 si no hay PB o no se pudo escribir.
int pc_speedrun_export_lss(char* outName, int n);

// ─── Modo práctica ─────────────────────────────────────────────────────────
// Un día suelto: el 1 es partida nueva; el N > 1 parte del estado del juego al
// acabar el día N-1 de alguna run de la categoría (punto de práctica). El reloj
// mide ese día (con sus cuevas) hasta que acaba, y se compara con el mismo día
// del PB. Nunca toca el registro ni LiveSplit. day 0 = práctica apagada.
void pc_speedrun_set_practice_day(int day);
int pc_speedrun_practice_day(void);
int pc_speedrun_next_practice_day(int day, int dir); // siguiente día disponible (0 = apagada)
// Puntos de práctica: save/speedrun/practice/<categoría>/dayN, los datos de la
// partida (PlayData::write) al acabar el día N-1. Nada de la tarjeta.
// wants: hay una run de verdad en marcha (no práctica) que puede guardarlos.
int pc_speedrun_wants_practice_point(void);
void pc_speedrun_practice_point_save(int day, const void* data, int size);
int pc_speedrun_practice_point_load(int day, void* data, int size); // 0 si no hay o no vale

// ─── Reset rápido ──────────────────────────────────────────────────────────
// Mantener 1 s la acción PC_KEY_ACT_SPEEDRUN_RESET (F5 por defecto; Controls)
// descarta la run. take_reset: 1 una vez cuando se cumple el segundo; quien lo
// recoge (SingleGameSection::doUpdate) vuelve a empezar la sección de juego, y
// la selección de partida, con take_restart, empieza otra run sin menús.
int pc_speedrun_take_reset(void);
int pc_speedrun_take_restart(void);

// Una vez por refresco (p2_os_host.cpp): reset rápido y atajos de prueba.
void pc_speedrun_frame(void);
// HUD (reloj, splits y visor de controles), entre pc_settings_p2_begin_frame()
// y pc_settings_draw() para que el menú F1 quede encima.
void pc_speedrun_draw(void);

// Atajos de prueba retenidos hasta que el bucle de juego los recoge.
int pc_speedrun_debug_take_day_end(void);
int pc_speedrun_debug_take_cave_exit(void);
int pc_speedrun_debug_take_finish(void);
// F10: el capitán (y los Pikmin) junto al agujero de Emergence Cave, o de la
// primera cueva del mapa si no está en él. Se entra con A, como siempre.
int pc_speedrun_debug_take_cave_warp(void);

#ifdef __cplusplus
}
#endif
