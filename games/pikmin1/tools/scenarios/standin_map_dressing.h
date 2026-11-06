#pragma once

// Draw-only dressing for the stand-in stage maps (#115, ART_DIRECTION.md "Terrain,
// map edge, gates and bridges"): sandstone rock faces with a grass-topped rim in
// front of the map's walls (the map edge) and of steep terrain steps, and packed-soil
// paths on the floor. The meshes come from nectar_assets/standin_terrain.h and go
// into the map as compileMap props with no collision.
//
// Gameplay stays the same: compileDressedMap compiles the map once plain and once
// dressed and refuses the result unless the two agree byte for byte on everything
// the game's collision and routes read (the joint bounds, the collision triangles,
// the collision grid and the route text), and the .ini. The dressing only adds
// vertices, normals, UVs, textures, materials and draw meshes.

#include "nectar_assets/standin_terrain.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"

#include <string>
#include <vector>

namespace standin {

struct DressPath {
	std::vector<nectar::Vec2> points; ///< (x, z), at least two
	float width = 56.0f;
};

struct MapDressing {
	/// Rock faces in front of every wall the plain map draws with the rock material
	/// (the map edge, and on the flat sandbox its four walls).
	bool cliffEdges = true;
	/// More cliffs, e.g. a step in the terrain heights.
	std::vector<nectar::StandInCliffSpan> cliffs;
	std::vector<DressPath> paths;
};

/// The map compiled with `dressing` (see above). `read` serves the map's source
/// files; the dressing .mod is served from memory next to `modPath`.
bool compileDressedMap(const nectar::MapData& data, const nectar::GameFileReader& read, const std::string& modPath, const MapDressing& dressing,
                       nectar::CompiledMap& out, std::string& error);

} // namespace standin
