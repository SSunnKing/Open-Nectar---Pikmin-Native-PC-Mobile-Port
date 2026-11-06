#include "standin_art_meshes.h"

#include <algorithm>

namespace nectar {

namespace {

// The exported models (art/export/all.inc and the files it includes): plain
// static tables, so no parser and no file I/O at run time.
#include "../art/export/all.inc"

constexpr std::uint32_t kMeshFeatColour = 1u << 2; // Mesh::FeatureFlags::VtxColor

} // namespace

int artModelCount() { return int(sizeof(kArtModels) / sizeof(kArtModels[0])); }

const ArtModel& artModelAt(int index) { return *kArtModels[index]; }

const ArtModel* findArtModel(const std::string& name)
{
	for (const ArtModel* a : kArtModels) {
		if (name == a->name) return a;
	}
	return nullptr;
}

bool addArtModel(Model& m, const StandInModelSpec& spec, const ArtModel& art, float scale, std::string& error, const std::vector<int>& tintJoints)
{
	if (art.joints != spec.jointCount()) {
		error = std::string(art.name) + " is made for " + std::to_string(art.joints) + " joints, " + spec.path + " has " + std::to_string(spec.jointCount());
		return false;
	}
	ModMaterial white;
	white.colour = { 255, 255, 255, 255 };
	m.materials.push_back(white);
	const int material = int(m.materials.size()) - 1;
	if (m.uvs[0].empty()) m.uvs[0].push_back(Vec2 {});

	for (int i = 0; i < art.meshCount; i++) {
		const ArtMesh& am = art.meshes[i];
		if (am.joint < 0 || am.joint >= spec.jointCount() || std::size_t(am.joint) >= m.meshes.size()) {
			error = std::string(art.name) + ": a mesh hangs from joint " + std::to_string(am.joint) + ", outside the rig";
			return false;
		}
		// skeletonModel gives joint j mesh j; the art replaces whatever is on it.
		ModMesh& mesh = m.meshes[std::size_t(am.joint)];
		mesh.flags |= kMeshFeatColour;
		ModMtxGroup& group = mesh.groups[0];
		group.tris.clear();
		// Material 0 is the one the game tints (Shape::mMaterialList->setColour);
		// white, so the tint is the colour and the vertex colours the shading.
		int jointMaterial = material;
		if (std::find(tintJoints.begin(), tintJoints.end(), am.joint) != tintJoints.end() && !m.materials.empty()) {
			m.materials[0].colour = { 255, 255, 255, 255 };
			jointMaterial         = 0;
		}
		m.joints[std::size_t(am.joint)].matPolys = { { jointMaterial, am.joint } };

		const int base = int(m.positions.size()), nbase = int(m.normals.size()), cbase = int(m.colours.size());
		for (std::uint32_t v = 0; v < am.vertexCount; v++) {
			const ArtVertex& av = art.vertices[am.firstVertex + v];
			m.positions.push_back({ av.x * scale, av.y * scale, av.z * scale });
			m.normals.push_back({ av.nx, av.ny, av.nz });
			m.colours.push_back({ av.r, av.g, av.b, 255 });
		}
		for (std::uint32_t t = 0; t < am.triCount; t++) {
			const ArtTri& at = art.tris[am.firstTri + t];
			ModTriangle tr;
			const int idx[3] = { at.a, at.b, at.c };
			for (int k = 0; k < 3; k++) {
				tr.v[k].mtxSlot = 0;
				tr.v[k].pos     = base + idx[k];
				tr.v[k].nrm     = nbase + idx[k];
				tr.v[k].col     = cbase + idx[k];
				tr.v[k].uv[0]   = 0;
			}
			group.tris.push_back(tr);
		}
	}
	return true;
}

} // namespace nectar
