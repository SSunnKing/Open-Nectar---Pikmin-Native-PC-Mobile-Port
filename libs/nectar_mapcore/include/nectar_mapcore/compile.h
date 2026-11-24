#pragma once

// Compilador de mapas VS: MapData -> .mod (geometría, colisión, caminos y
// props) y .ini de escena, en memoria. Es el mismo código para el juego
// (pc_vs_arena) y para el editor, así que la vista previa de colisión del
// editor es la real.
//
// Texturas y materiales se copian de los .mod del juego del propio jugador
// (sourceMod y los de los props): por eso hace falta un lector de archivos.

#include "nectar_assets/mod.h"
#include "nectar_mapcore/map.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nectar {

/// Lee un archivo del dataDir (ruta relativa, p. ej. "courses/practice/practice.mod").
using GameFileReader = std::function<bool(const std::string& path, std::vector<std::uint8_t>& out)>;

struct CompiledMap {
	std::vector<std::uint8_t> mod;
	std::vector<std::uint8_t> ini;
	std::vector<std::uint8_t> genDefault, genPlants; ///< generadores del escenario (vacíos = sin .gen)
	std::size_t vertices = 0, collisionTris = 0, routePoints = 0, props = 0;
};

/// `modPath` es la ruta con la que el .ini apunta al .mod (map_file).
bool compileMap(const MapData& map, const GameFileReader& read, const std::string& modPath, CompiledMap& out, std::string& error);

/// Triángulos de un modelo en el orden de DocProp::paint (todos, por mallas,
/// grupos y triángulos), en la pose de reposo y sin colocar.
std::vector<std::array<Vec3, 3>> propTriangles(const Model& model);

/// Códigos de terreno que pone el compilador (atributo en los bits 31-29).
constexpr std::uint32_t kMapCodeGrass = 0x42000000;
constexpr std::uint32_t kMapCodeRock  = 0x20000000;
constexpr std::uint32_t kMapCodeWater = 0xA0000000;

} // namespace nectar
