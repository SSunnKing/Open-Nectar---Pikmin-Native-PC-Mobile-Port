#ifndef PC_P2_COOP_H
#define PC_P2_COOP_H

/*
 * Pikmin 2: modo historia cooperativo (docs/PLAN_COOP.md).
 *
 * pending: elegida la opción Coop del título, antes de cargar la partida.
 * active:  dentro de la historia y con Louie ya disponible (fase 1).
 * El co-op no se guarda en la partida: se elige cada vez.
 */

#ifdef __cplusplus
extern "C" {
#endif

void pc_p2_coop_set_pending(int twoPlayers);
int  pc_p2_coop_pending(void);
/// Elegido el co-op y en la historia (superficie o cueva), con o sin Louie.
int  pc_p2_coop_session(void);
/// Sesión co-op tras la cinemática de Louie (DEMO_First_Use_Louie): los dos
/// capitanes juegan a la vez. Antes, el tutorial va con un capitán y la Y.
int  pc_p2_coop_active(void);
/// Quien abrió el último menú (pausa, cebolla, nave, agujero): 0 = P1, 1 = P2.
void pc_p2_coop_set_menu_player(int player);
int  pc_p2_coop_menu_player(void);
/// Co-op con la pantalla partida a izquierda y derecha (ajuste "Co-op Split
/// Screen"); si no, arriba y abajo como el VS.
int  pc_p2_coop_vertical(void);
/// Cámara fusionada (ajuste "Co-op Merged Camera", docs/PLAN_COOP.md §6.7):
/// con la pantalla partida, las dos vistas se dibujan a pantalla completa con
/// el frustum corrido y recortadas a su mitad, y se funden en una sola imagen
/// cuando los capitanes están cerca.
int  pc_p2_coop_merge_on(void);
/// 0 = una sola imagen, 1 = cada uno con su cámara (con transición).
void pc_p2_coop_set_split_blend(float blend);
float pc_p2_coop_split_blend(void);
/// Lado de pantalla de la vista del capitán `view` (0 = izquierda/arriba). Con
/// la cámara fusionada, el capitán 1 puede quedar a la derecha o abajo si está
/// en ese lado de la imagen al separarse.
void pc_p2_coop_set_p1_side(int side);
int  pc_p2_coop_view_side(int view);
/// Pantalla partida en curso (setPlayerMode): el HUD va por mitades.
void pc_p2_coop_set_split(int on);
int  pc_p2_coop_split(void);
/// Cinemática a pantalla completa: setPlayerMode no la devuelve a la partida.
void pc_p2_coop_set_movie_full(int on);
int  pc_p2_coop_movie_full(void);
/// Capitán que dispara una cinemática cuyo objetivo no es un capitán (p. ej.
/// conocer un Pikmin silbado). Lo consume pcCoopMovieBegin al arrancar la demo.
void pc_p2_coop_set_movie_protagonist(void* navi);
void* pc_p2_coop_take_movie_protagonist(void);
void pc_p2_coop_clear_movie_protagonist(void);

#ifdef __cplusplus
}
#endif

#endif
