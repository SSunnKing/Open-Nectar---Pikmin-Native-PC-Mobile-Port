#pragma once

// Paquetes de malla NHM1 (los de Load/Models y los model.nhm de los
// personajes del creador de modelos): malla con su rig, sin depender de
// ningún juego. Pikmin 1 (pc_port/mods/pc_hd_models.cpp) y Pikmin 2 los
// dibujan sobre el esqueleto animado del modelo original.
//
// Formato (little-endian): "NHM1", versión (1-3), huesos, partes, [flags del
// modelo, v3]; por hueso: nombre (u8 + bytes) y su inversa de reposo (4x4 por
// filas); por parte: vértices, ancho, alto, bytes RGBA, [flags de la parte,
// v2+], los vértices (NhmVertex) y la textura RGBA.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct NhmVertex {
	float position[3];
	float normal[3];
	float uv[2];
	std::uint8_t joints[4];
	float weights[4];
};
static_assert(sizeof(NhmVertex) == 52, "NHM1 vertex layout changed");

constexpr unsigned kNhmPartRepeat = 1; ///< la textura se repite en vez de recortarse
constexpr unsigned kNhmPartNoCull = 2; ///< piezas finas de dos caras (discos de los ojos)
constexpr unsigned kNhmPartNoTint = 4; ///< no lleva el tinte de quien la dibuja (cabeza/visor)

/// El pack guarda sus propias posiciones de hueso (ver nhmSkinMatrices).
constexpr unsigned kNhmModelRetarget = 1;

struct NhmBone {
	std::string name;
	float inverseBind[16]; ///< por filas
};

struct NhmPart {
	std::vector<NhmVertex> vertices;
	unsigned width = 0, height = 0, flags = 0;
	std::vector<std::uint8_t> rgba;
	/// Alfa parcial en una parte apreciable de la textura (el visor): se
	/// dibuja después de lo opaco y sin escribir profundidad.
	bool translucent() const;
};

struct NhmModel {
	unsigned version = 0, flags = 0;
	std::vector<NhmBone> bones;
	std::vector<NhmPart> parts;
};

bool parseNhm(const std::uint8_t* data, std::size_t size, NhmModel& out, std::string& error);
/// `path` en UTF-8.
bool loadNhm(const std::string& path, NhmModel& out, std::string& error);

/// Afín 3x4 por filas (como Mtx de GX).
struct Affine34 {
	float m[3][4];
};
Affine34 affineIdentity();
Affine34 affineMul(const Affine34& a, const Affine34& b);
/// Inversa afín general (la animación puede escalar huesos).
Affine34 affineInverse(const Affine34& a);
/// Las tres primeras filas de una 4x4 por filas.
Affine34 affineFromRows(const float m[16]);

/// Matrices de piel del pack (12 floats por hueso, en el orden de
/// model.bones) sobre un esqueleto animado del juego:
///  - boneJoint[i]: hueso del juego al que va el hueso i del pack (varios
///    pueden ir al mismo: el primero fija su posición y los demás lo siguen);
///  - anim[j]: matriz del hueso j del juego en este fotograma (en el espacio
///    en que se vaya a dibujar, p. ej. el de la cámara);
///  - bind[j]: su pose de reposo en espacio del modelo;
///  - parent[j]: su padre (-1 la raíz); los padres van antes que los hijos.
/// Sin kNhmModelRetarget, cada vértice va al espacio local de su hueso en el
/// pack y lo coloca el hueso animado del juego. Con él, el pack conserva sus
/// posiciones de hueso (hombros, caderas y cuello de un personaje más alto)
/// y toma del juego solo el movimiento: cada transformación local animada
/// mueve su origen la diferencia entre el reposo del pack y el del juego.
/// `packAnim` (opcional) recibe las matrices animadas de los huesos del juego
/// con las posiciones del pack (para colgar cosas de ellos).
void nhmSkinMatrices(const NhmModel& model, const std::vector<int>& boneJoint, const std::vector<Affine34>& anim,
                     const std::vector<Affine34>& bind, const std::vector<int>& parent, std::vector<float>& out,
                     std::vector<Affine34>* packAnim = nullptr);

/// Posición y normal (normalizada) de un vértice con esas matrices.
void nhmSkinVertex(const NhmVertex& v, const std::vector<float>& matrices, std::size_t boneCount, float pos[3], float nrm[3]);

} // namespace nectar
