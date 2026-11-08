#pragma once

// Modelos .mod de relleno para el sandbox de los tests de escenarios (issue
// #65, bajo #62 y #59): los 45 .mod que el juego carga siempre al arrancar. Un
// .mod que falta aborta en StdSystem::loadShape, así que la lista es cerrada y
// sale del código, no del disco:
//   gameSetup.cpp (26): shapeList (peletas, 4 Pikmin, capitán) y shapeList2
//     (nave y sus 16 piezas soto y fuzoku)
//   GlobalShape::init (5): cursor/marker, cursor/cursor, happas leaf, bud, flower
//   ItemMgr::initialise (7): pikihead, bomb, water, fl_water, door, key, goal
//   ctor de ItemMgr (4): weeds stone1-3 y weed1
//   ctor de EffectMgr (3): effects/shapes b_goal, r_goal, y_goal
// Los .mod propios de cada etapa (tekis, jefes, plantas, puentes, puertas,
// mapas) no entran: un sandbox con .gen mínimos no los carga.
//
// Fixture contract (#190): every joint table and collision sphere is authored
// for these fixtures. What the game source requires is kept and cited in
// standin_models.cpp: the joint counts it indexes (ship, Onion, sprout, the
// shared Pikmin rig, the cursor regression guard) and the sphere ids it looks up
// without a null check ('rhnd', 'gol1', 'bas1'...). Parents are a flat fan, and
// radii and centres are placed on the procedural art. No geometry, texture or
// number is copied from the game disc; exact disc checks live in the opt-in
// local lanes that read a user-supplied disc install.

#include "nectar_assets/mod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Cuántos .mod se escriben (ver la lista de arriba).
constexpr int kStandInModelCount = 45;

/// UfoShapeObject llama a overrideAnim en la articulación 53 sin comprobar el
/// rango (ufoAnim.cpp:26-40): la nave necesita al menos 54.
constexpr int kStandInShipMinJoints = 54;

struct StandInModelSpec {
	std::string path;          ///< relativa a assets/, con dataDir/ como en el manifiesto de #64
	std::vector<int> parents;  ///< padre de cada articulación (-1 la raíz); su tamaño es el número de articulaciones
	Rgba8 colour;              ///< color del material de todas las cajas
	float halfSize = 5.0f;     ///< semilado de la caja de la raíz; las demás, la mitad
	std::string collInfo;      ///< texto collinfo tras el bloque final ("" = sin partes de colisión)
	/// The root box's triangles are also the model's collision (a platform, written with writePlatformMod): the
	/// push box (objects/hinderrock/cube10.mod), which HinderRock turns into a DynBuildShape that Pikmin push against.
	bool platform = false;

	int jointCount() const { return int(parents.size()); }
};

/// Los 45 modelos, en el orden de la lista de arriba.
const std::vector<StandInModelSpec>& standInModelSpecs();

/// nullptr si el .mod no es de relleno. `path` como en StandInModelSpec::path.
const StandInModelSpec* findStandInModel(const std::string& path);

/// El Model de un relleno: una caja por articulación (malla `j` colgando de la
/// articulación `j`), un material opaco de un color por caja, sin texturas.
Model buildStandInModel(const StandInModelSpec& spec);

struct StandInModelFile {
	std::string path; ///< como en StandInModelSpec::path
	std::vector<std::uint8_t> bytes;
};

/// Escribe los 45. El generador (#68) decide dónde van los ficheros.
bool writeStandInModels(std::vector<StandInModelFile>& out, std::string& error);

} // namespace nectar
