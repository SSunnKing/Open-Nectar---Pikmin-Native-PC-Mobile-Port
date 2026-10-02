#ifndef PC_COOP_H
#define PC_COOP_H

/* Modo cooperativo local (PLAN_COOP): 2 Olimar, pantalla partida, P2 con
   mando físico. Se elige en el título al pulsar "Empezar", antes del slot.
   Estado de sesión: no se guarda en la partida. */

/// Lo que va a ser la próxima partida que arranque. Lo fija el selector 1P/2P.
void pc_coop_set_pending(bool on);
bool pc_coop_pending(void);
/// El título ya eligió 1P/Co-op (menú principal): el selector 1P/2P previo
/// al slot se salta. Se consume al leerlo.
void pc_coop_set_chosen_at_title(bool on);
bool pc_coop_take_chosen_at_title(void);

/// Capitán de cada jugador (0 = P1, 1 = P2). Se elige en el prompt de
/// mandos (coop) o en el de capitán (1 jugador); no se guarda.
enum PcCaptain {
	PC_CAPTAIN_OLIMAR = 0,
	PC_CAPTAIN_LOUIE  = 1,
	// Un Pikmin como capitán: solo cambia el aspecto (modelo, luz de la hoja,
	// retrato); la jugabilidad es la de Olimar.
	PC_CAPTAIN_PIKMIN_RED    = 2,
	PC_CAPTAIN_PIKMIN_YELLOW = 3,
	PC_CAPTAIN_PIKMIN_BLUE   = 4,
	PC_CAPTAIN_COUNT
};
/// Color de Pikmin (Red/Yellow/Blue del juego) de un capitán Pikmin, o -1.
int pc_captain_piki_color(int captain);
void pc_coop_set_captain(int player, int captain);
int pc_coop_captain(int player);
/// Tinte de distinción de P2 cuando ambos llevan el mismo capitán:
/// Olimar/Olimar -> azul, Louie/Louie y Azul/Azul -> rojo, mixto -> ninguno (255,255,255).
void pc_coop_p2_tint(unsigned char* r, unsigned char* g, unsigned char* b);
bool pc_coop_p2_tinted(void);

/// Modo VS (1 contra 1): elegido en el menú principal. Va siempre con el
/// cooperativo (dos capitanes) y activa flowCont.mIsVersusMode en la partida.
void pc_vs_set_pending(bool on);
bool pc_vs_pending(void);
/// True mientras la partida en curso es un VS.
bool pc_vs_active(void);

/// Aplica la elección pendiente a la partida que empieza (GameCoreSection).
void pc_coop_begin_run(void);
/// True mientras la partida en curso es cooperativa.
bool pc_coop_active(void);
void pc_coop_end_run(void);

#endif // PC_COOP_H
