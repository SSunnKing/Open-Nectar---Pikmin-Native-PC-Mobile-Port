#pragma once

// Terrain art for the disc-free stand-in stages (#115, ART_DIRECTION.md, "Terrain,
// map edge, gates and bridges"). Everything is generated here; nothing comes from the
// disc.
//
// - Ground textures, tiling: grass with clumps and soil specks, packed soil for
//   paths, layered sandstone for cliffs. addStandInTerrainTextures puts the grass and
//   the sandstone on the materials compileMap draws the floor and the walls with.
// - Dressing meshes for compileMap props with no collision (MapProp::collide false):
//   soil paths laid on the floor, and rock faces with a rim in front of the map's
//   walls and cliffs. They are drawn only. Callers keep them inside the map's
//   existing bounds (the box of its own vertices), so the compiled map's joint
//   bounds, collision triangles and collision grid stay byte-identical.

#include "nectar_assets/image.h"
#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/mod.h"

#include <functional>
#include <vector>

namespace nectar {

/// 256 x 256, tiling: moss-green grass with soft clumps, bare patches and soil specks.
Image standInGrassImage();
/// 256 x 256, tiling along x: packed brown soil with pebbles. Rows run across a path,
/// and the top and bottom rows fade out raggedly (alpha) into the grass.
Image standInSoilPathImage();
/// 256 x 256, tiling: layered warm-grey sandstone with darker strata lines along x.
Image standInSandstoneImage();

/// The source-.mod materials compileMap draws a map with: walls and the map edge (0)
/// and the floor (14, also the terrain palette's grass).
constexpr int kStandInRockMaterial  = 0;
constexpr int kStandInGrassMaterial = 14;

/// Gives a tables-only source .mod (writeModTables) the grass and sandstone textures:
/// materials 0 and 14 become textured, opaque and white (the texture carries the
/// colour). `m` must already have at least 15 materials.
void addStandInTerrainTextures(Model& m);

/// A packed-soil path: a ribbon `width` wide along `points` (x, z), laid `lift` above
/// the ground that `ground(x, z)` gives. Alpha-tested, so its edges are ragged.
MeshModInput standInPathMesh(const std::vector<Vec2>& points, float width, float lift, const std::function<float(float, float)>& ground);

/// One stretch of cliff: the foot runs from footA to footB and the top edge from topA
/// to topB (a vertical wall has top = foot + height). `out` is the unit horizontal
/// direction the face looks to (towards where creatures stand).
struct StandInCliffSpan {
	Vec3 footA, footB, topA, topB;
	Vec2 out;
};

struct StandInCliffStyle {
	float relief   = 6.0f;  ///< how far the face stands out of its plane, at most
	float minOut   = 1.0f;  ///< the face never comes nearer its plane than this
	float lip      = 14.0f; ///< how far the rim at the top reaches out over the face, at most
	float lipDepth = 18.0f; ///< height of the rim's front, at most
	float back     = 0.0f;  ///< how far the rim's top runs back behind the top edge
	float seed     = 0.0f;
};

/// Draw-only rock faces for `spans`: layered sandstone with ledges along the strata,
/// and a grass-topped rim along the top edge. Nothing in it rises above the spans' top
/// edges or reaches further back than `style.back` behind them.
MeshModInput standInCliffMesh(const std::vector<StandInCliffSpan>& spans, const StandInCliffStyle& style);

} // namespace nectar
