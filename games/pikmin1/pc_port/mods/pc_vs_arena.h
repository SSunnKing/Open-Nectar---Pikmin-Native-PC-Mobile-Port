#ifndef _PC_VS_ARENA_H
#define _PC_VS_ARENA_H

#include "types.h"

class RandomAccessStream;
class Vector3f;

enum {
	PC_VS_REACT_NONE,
	PC_VS_REACT_ENEMY,
	PC_VS_REACT_PELLET,
	PC_VS_REACT_BOMB,
	PC_VS_REACT_NECTAR,
};
struct PcVsReaction {
	int kind;
	u32 pelletId;
	int teki, boss, count;
	f32 radius;
};

/*
 * Arena del modo VS: un rectángulo llano con muretes, una base en cada
 * extremo del eje X y el centro libre para la pieza gorda.
 *
 * La disposición se lee de un map.json (pc_vs_map). Cuando el juego pide una
 * de las rutas virtuales de abajo, se monta en memoria a partir de esa
 * descripción y de los datos extraídos del propio juego (los de serie, de
 * Impact Site: courses/practice/practice.mod y stages/chal0.ini): texturas y
 * materiales se copian tal cual; geometría, colisión y caminos se generan.
 * Sin esos archivos en assets/ la arena no existe.
 */
/// Cuántas cosas de cada tipo puede tener un mapa (los mismos números que
/// nectar::kMapMax* de nectar_mapcore/map.h).
#define PC_VS_MAX_PELLETS 48
#define PC_VS_MAX_PIECES  32
#define PC_VS_MAX_GUARDS  16
#define PC_VS_MAX_ENEMIES 120
#define PC_VS_MAX_OBJECTS 64

#define PC_VS_ARENA_STAGE "stages/vsarena.ini"
#define PC_VS_ARENA_MOD   "courses/vsarena/arena.mod"
// Generadores (solo con un escenario del juego como base): el juego los busca
// junto al .ini de la escena.
#define PC_VS_ARENA_GEN    "stages/vsarena/default.gen"
#define PC_VS_ARENA_PLANTS "stages/vsarena/plants.gen"

/// Flujo en memoria para una ruta de la arena; nullptr si la ruta no es de la
/// arena o faltan los datos del juego.
RandomAccessStream* pc_vs_arena_open(const char* path);

/// Lee y compila ya el mapa elegido (pc_vs_map_select) para empezar la
/// partida sin sorpresas. nullptr si está listo; si no, el motivo, para
/// enseñarlo en el menú.
const char* pc_vs_arena_prepare(void);
/// Lo compilado por pc_vs_arena_prepare (vacío si no hay): netplay compara su
/// huella entre los dos PCs.
void pc_vs_arena_compiled(const u8** mod, size_t* modLen, const u8** ini, size_t* iniLen);

/// Posición y orientación inicial del capitán de cada jugador (0 = J1, 1 = J2).
void pc_vs_arena_base(int player, Vector3f& pos, f32& faceDirection);
/// Posición de la cebolla de ese color (Blue/Red/Yellow) del jugador.
void pc_vs_arena_onion(int player, int color, Vector3f& pos);

/// Sitio donde aparece una pastilla y cuál ('pr01', 'py05'...).
struct PcVsPelletSpot {
	f32 x, z;
	u32 pelletId;
};
int pc_vs_arena_pellet_spots(PcVsPelletSpot* out, int max);

/// Cohete de cada jugador (en su base, al fondo).
void pc_vs_arena_rocket(int player, Vector3f& pos, f32& faceDirection);

/// Tipos de pieza del mapa. Cada pareja simétrica usa la misma pieza.
enum PcVsPieceKind {
	PC_VS_PIECE_SMALL_A = 0, ///< pequeñas junto a cada base (3 tipos)
	PC_VS_PIECE_SMALL_B,
	PC_VS_PIECE_SMALL_C,
	PC_VS_PIECE_GUARDED, ///< mediana custodiada por un Bulborb
	PC_VS_PIECE_POND,    ///< mediana de las charcas
	PC_VS_PIECE_BIG,     ///< la gorda del cráter (minuto 5)
	PC_VS_PIECE_KINDS
};
struct PcVsPieceSpot {
	f32 x, z;
	int kind;
	u32 part; ///< pieza concreta elegida en el editor; 0 = la que toque por `kind`
};
/// Piezas que están desde el principio (la gorda va aparte).
int pc_vs_arena_piece_spots(PcVsPieceSpot* out, int max);
void pc_vs_arena_big_piece(Vector3f& pos);
/// Dónde duerme cada Bulborb custodio y qué ocurre al derrotarlo.
struct PcVsGuardSpot {
	f32 x, z;
	PcVsReaction reaction;
};
int pc_vs_arena_guard_spots(PcVsGuardSpot* out, int max);
/// Compuerta de roca-bomba del hueco central de la base de cada jugador.
void pc_vs_arena_gate(int player, Vector3f& pos, f32& faceDirection);
PcVsReaction pc_vs_arena_gate_reaction(int player);
/// Montón de rocas-bomba de cada base (para los amarillos).
void pc_vs_arena_bomb_pile(int player, Vector3f& pos);

/// Enemigos y jefes puestos en el editor (teki o boss >= 0).
struct PcVsEnemySpot {
	f32 x, z, face;
	int teki, boss;
	PcVsReaction reaction;
};
int pc_vs_arena_enemy_spots(PcVsEnemySpot* out, int max);

/// Construcciones: puentes y rampas (las construyen los Pikmin), bloques que
/// se empujan y puertas que se derriban.
enum {
	PC_VS_OBJ_BRIDGE,
	PC_VS_OBJ_BRIDGE_LONG,
	PC_VS_OBJ_SLOPE_UP,
	PC_VS_OBJ_SLOPE_DOWN,
	PC_VS_OBJ_PUSH_ROCK,
	PC_VS_OBJ_GATE_SOFT,
	PC_VS_OBJ_GATE_HARD,
	PC_VS_OBJ_GATE_BOMB,
	PC_VS_OBJ_KEY,
	PC_VS_OBJ_KEY_DOOR,
	PC_VS_OBJ_NECTAR,
};
struct PcVsObjectSpot {
	f32 x, z, face, push;
	int kind, work;
	f32 y; ///< sobre el suelo
	PcVsReaction reaction;
};
int pc_vs_arena_object_spots(PcVsObjectSpot* out, int max);

#endif
