#pragma once

// Stand-in art authored in Blender (#115 follow-up). The .blend files under
// libs/nectar_assets/art/blender are the source; art/export_standins.py bakes
// their lighting into vertex colours and writes art/export/*.inc, which
// standin_art_meshes.cpp compiles in. No Blender is needed to build: the .inc
// files are committed, and art/check_export.py checks they match a fresh export.

#include "nectar_assets/mod.h"
#include "nectar_assets/standin_models.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// One vertex in game space (Y up, front +z): position, unit normal, sRGB colour
/// with the lighting baked in. Alpha is always opaque.
struct ArtVertex {
	float x, y, z;
	float nx, ny, nz;
	std::uint8_t r, g, b;
};

/// Clockwise seen from the front (the .mod convention); indices into the mesh's
/// own vertices.
struct ArtTri {
	std::uint16_t a, b, c;
};

/// The part of a model that hangs from one joint.
struct ArtMesh {
	int joint;
	std::uint32_t firstVertex, vertexCount, firstTri, triCount;
};

struct ArtModel {
	const char* name;
	int joints; ///< the rig the art was made for; must equal the spec's joint count
	const ArtMesh* meshes;
	int meshCount;
	const ArtVertex* vertices;
	const ArtTri* tris;
};

/// nullptr when there is no Blender art with that name.
const ArtModel* findArtModel(const std::string& name);

/// Number of exported art models (for tests).
int artModelCount();
const ArtModel& artModelAt(int index);

/// Adds `art` to `m` (built by skeletonModel for `spec`), scaled by `scale`
/// about the model origin: one vertex-coloured mesh per joint on one white
/// material, so the unlit stand-in draw shows the baked colours. The meshes on
/// `tintJoints` use material 0 instead, made white, which the game tints at run
/// time. false with the reason if the art does not fit the spec's rig.
bool addArtModel(Model& m, const StandInModelSpec& spec, const ArtModel& art, float scale, std::string& error,
                 const std::vector<int>& tintJoints = {});

} // namespace nectar
