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
	// Segunda fila, solo con Pikmin 2 instalado: modelos sacados de su disco
	// (pc_hd_models_import_pikmin2). También solo estéticos.
	PC_CAPTAIN_PRESIDENT     = 5,
	PC_CAPTAIN_PIKMIN_WHITE  = 6,
	PC_CAPTAIN_PIKMIN_PURPLE = 7,
	PC_CAPTAIN_BULBMIN       = 8,
	// Puffmin (Pikmin seta de Pikmin 1): va en la segunda fila, pero no pide
	// Pikmin 2; sin él es el único de esa fila.
	PC_CAPTAIN_PUFFMIN = 9,
	// Tercera fila: personajes propios instalados en Load/Characters (creador
	// de modelos). Van sobre el esqueleto y la jugabilidad de Olimar; cada
	// hueco es una carpeta, por nombre (pc_characters.cpp). Se ven de cinco en
	// cinco: Q/E, LB/RB o las flechas táctiles cambian de página.
	PC_CAPTAIN_CUSTOM_FIRST = 10,
	PC_CAPTAIN_CUSTOM_MAX   = 45, // 9 páginas de 5 en el selector
	PC_CAPTAIN_COUNT        = PC_CAPTAIN_CUSTOM_FIRST + PC_CAPTAIN_CUSTOM_MAX
};
/// Primer capitán de la segunda fila (los de Pikmin 2).
#define PC_CAPTAIN_FIRST_PIKMIN2 PC_CAPTAIN_PRESIDENT
/// Color de Pikmin (Red/Yellow/Blue del juego) de un capitán Pikmin, o -1.
int pc_captain_piki_color(int captain);
/// True para cualquier capitán con forma de Pikmin (también los de Pikmin 2).
bool pc_captain_is_pikmin(int captain);
/// Color del Pikmin de Pikmin 1 cuyo esqueleto y animaciones usa un capitán
/// Pikmin (los de Pikmin 2 van sobre el del rojo; el Puffmin, PIKI_Kinoko), o -1.
int pc_captain_skeleton_color(int captain);
/// Modelo NHM (PcHdModelId) que dibuja a un capitán de Pikmin 2, o -1.
int pc_captain_pikmin2_model(int captain);
/// True si el capitán se puede elegir: los de Pikmin 2 piden tenerlo instalado
/// y los propios, que haya un personaje en su hueco.
bool pc_captain_available(int captain);
/// Personajes propios (Load/Characters). Se vuelven a leer al abrir el
/// selector de capitán, así un personaje recién instalado sale sin reiniciar.
bool pc_captain_is_custom(int captain);
void pc_captain_custom_refresh(void);
int pc_captain_custom_count(void);
/// Nombre (de su character.json), modelo (.nhm) y tarjeta ("@ruta" para
/// pc_art, o nullptr sin portrait.png) de un capitán propio.
const char* pc_captain_custom_name(int captain);
const char* pc_captain_custom_model(int captain);
const char* pc_captain_custom_portrait(int captain);
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

/// Split screen draws the world once per view in the same tick. Sim work that
/// runs inside the world draw must run in the first view pass only. The view
/// loop in newPikiGame.cpp sets the pass; it is 0 outside split screen.
inline int& pc_coop_view_pass_ref(void)
{
	static int sPass = 0;
	return sPass;
}
inline void pc_coop_set_view_pass(int view) { pc_coop_view_pass_ref() = view; }
inline bool pc_coop_first_view_pass(void) { return pc_coop_view_pass_ref() == 0; }

#endif // PC_COOP_H
