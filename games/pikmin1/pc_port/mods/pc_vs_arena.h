#ifndef _PC_VS_ARENA_H
#define _PC_VS_ARENA_H

#include "types.h"

class RandomAccessStream;
struct Vector3f;

/*
 * Arena del modo VS: un rectángulo llano con muretes, una base en cada
 * extremo del eje X y el centro libre para la pieza gorda.
 *
 * No hay archivos nuevos. Cuando el juego pide una de las rutas virtuales de
 * abajo, se monta en memoria a partir de los datos extraídos del propio juego
 * (Impact Site: courses/practice/practice.mod y stages/chal0.ini): texturas y
 * materiales se copian tal cual; geometría, colisión y caminos se generan.
 * Sin esos archivos en assets/ la arena no existe.
 */
#define PC_VS_ARENA_STAGE "stages/vsarena.ini"
#define PC_VS_ARENA_MOD   "courses/vsarena/arena.mod"

/// Flujo en memoria para una ruta de la arena; nullptr si la ruta no es de la
/// arena o faltan los datos del juego.
RandomAccessStream* pc_vs_arena_open(const char* path);

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
};
/// Piezas que están desde el principio (la gorda va aparte).
int pc_vs_arena_piece_spots(PcVsPieceSpot* out, int max);
void pc_vs_arena_big_piece(Vector3f& pos);
/// Dónde duerme cada Bulborb custodio.
int pc_vs_arena_guard_spots(Vector3f* out, int max);
/// Compuerta de roca-bomba del hueco central de la base de cada jugador.
void pc_vs_arena_gate(int player, Vector3f& pos, f32& faceDirection);
/// Montón de rocas-bomba de cada base (para los amarillos).
void pc_vs_arena_bomb_pile(int player, Vector3f& pos);

#endif
