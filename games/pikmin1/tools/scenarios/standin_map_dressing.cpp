// Draw-only dressing for the stand-in stage maps (see standin_map_dressing.h).
#include "standin_map_dressing.h"

#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/mod.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>

namespace standin {

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint32_t be32(const Bytes& d, std::size_t o) { return (std::uint32_t(d[o]) << 24) | (std::uint32_t(d[o + 1]) << 16) | (std::uint32_t(d[o + 2]) << 8) | d[o + 3]; }

/// The .mod's chunks by type (the bytes of each, header included), and the text
/// after the end chunk (the map's routes).
bool chunksOf(const Bytes& d, std::map<std::uint32_t, Bytes>& out, Bytes& trailing)
{
	std::size_t o = 0;
	while (o + 8 <= d.size()) {
		const std::uint32_t type = be32(d, o), len = be32(d, o + 4);
		if (o + 8 + len > d.size()) return false;
		out[type] = Bytes(d.begin() + long(o), d.begin() + long(o + 8 + len));
		o += 8 + len;
		if (type == 0xFFFF) {
			trailing.assign(d.begin() + long(o), d.end());
			return true;
		}
	}
	return false;
}

/// The rock-material walls of a compiled map, one span per wall quad: the two
/// corner columns of the quad (same x and z), their lowest and highest points, and
/// the quad's normal (compileMap points it at where creatures stand).
std::vector<nectar::StandInCliffSpan> wallSpans(const nectar::Model& m, const nectar::MapData& data)
{
	std::vector<nectar::StandInCliffSpan> spans;
	const std::vector<int> mats = m.meshMaterials();
	for (std::size_t mi = 0; mi < m.meshes.size(); mi++) {
		if (mats[mi] != nectar::kStandInRockMaterial) continue;
		// Group the mesh's triangles into quads: the triangles of one quad share vertices.
		std::vector<const nectar::ModTriangle*> tris;
		for (const nectar::ModMtxGroup& g : m.meshes[mi].groups) {
			for (const nectar::ModTriangle& t : g.tris) tris.push_back(&t);
		}
		std::map<int, int> parent;
		auto find = [&](int v) {
			while (parent[v] != v) v = parent[v] = parent[parent[v]];
			return v;
		};
		for (const nectar::ModTriangle* t : tris) {
			for (int k = 0; k < 3; k++) {
				if (!parent.count(t->v[k].pos)) parent[t->v[k].pos] = t->v[k].pos;
			}
			for (int k = 1; k < 3; k++) parent[find(t->v[k].pos)] = find(t->v[0].pos);
		}
		std::map<int, std::vector<const nectar::ModTriangle*>> quads;
		for (const nectar::ModTriangle* t : tris) quads[find(t->v[0].pos)].push_back(t);
		for (const auto& q : quads) {
			const nectar::ModTriangle& t0 = *q.second[0];
			if (t0.v[0].nrm < 0) continue;
			const nectar::Vec3 n = m.normals[std::size_t(t0.v[0].nrm)];
			if (std::fabs(n.y) > 0.2f) continue; // a wall's top, not a wall
			// The quad's columns: vertices with the same x and z.
			struct Column {
				float x, z, lo, hi;
			};
			std::vector<Column> cols;
			for (const nectar::ModTriangle* t : q.second) {
				for (int k = 0; k < 3; k++) {
					const nectar::Vec3& p = m.positions[std::size_t(t->v[k].pos)];
					bool found            = false;
					for (Column& c : cols) {
						if (std::fabs(c.x - p.x) < 0.01f && std::fabs(c.z - p.z) < 0.01f) {
							c.lo  = std::min(c.lo, p.y);
							c.hi  = std::max(c.hi, p.y);
							found = true;
						}
					}
					if (!found) cols.push_back({ p.x, p.z, p.y, p.y });
				}
			}
			if (cols.size() != 2) continue;
			const float hl = std::sqrt(n.x * n.x + n.z * n.z);
			nectar::StandInCliffSpan s;
			// The face starts at the ground (walls on a heightfield sink below it).
			const float gA = std::max(cols[0].lo, nectar::terrainHeight(data, cols[0].x, cols[0].z) - 2.0f);
			const float gB = std::max(cols[1].lo, nectar::terrainHeight(data, cols[1].x, cols[1].z) - 2.0f);
			s.footA = { cols[0].x, gA, cols[0].z };
			s.footB = { cols[1].x, gB, cols[1].z };
			s.topA  = { cols[0].x, cols[0].hi, cols[0].z };
			s.topB  = { cols[1].x, cols[1].hi, cols[1].z };
			s.out   = { n.x / hl, n.z / hl };
			spans.push_back(s);
		}
	}
	return spans;
}

bool sameCollision(const Bytes& plain, const Bytes& dressed, std::string& why)
{
	std::map<std::uint32_t, Bytes> a, b;
	Bytes ta, tb;
	if (!chunksOf(plain, a, ta) || !chunksOf(dressed, b, tb)) {
		why = "cannot walk the chunks";
		return false;
	}
	for (std::uint32_t type : { 0x100u, 0x110u }) {
		if (a[type] != b[type]) {
			char buf[64];
			std::snprintf(buf, sizeof buf, "chunk 0x%x differs", type);
			why = buf;
			return false;
		}
	}
	if (ta != tb) {
		why = "the route text differs";
		return false;
	}
	nectar::Model ma, mb;
	if (!nectar::parseMod(plain.data(), plain.size(), ma, why) || !nectar::parseMod(dressed.data(), dressed.size(), mb, why)) return false;
	if (ma.joints.size() != mb.joints.size()) {
		why = "the joint count differs";
		return false;
	}
	for (std::size_t j = 0; j < ma.joints.size(); j++) {
		const nectar::ModJoint &x = ma.joints[j], &y = mb.joints[j];
		if (std::memcmp(&x.boundsMin, &y.boundsMin, sizeof x.boundsMin) != 0 || std::memcmp(&x.boundsMax, &y.boundsMax, sizeof x.boundsMax) != 0) {
			why = "the joint bounds differ";
			return false;
		}
	}
	// Every vertex the collision uses is where it was (the dressing's come after).
	for (const nectar::ModCollTri& t : ma.collision) {
		for (int k = 0; k < 3; k++) {
			const nectar::Vec3 &p = ma.positions[std::size_t(t.v[k])], &q = mb.positions[std::size_t(t.v[k])];
			if (std::memcmp(&p, &q, sizeof p) != 0) {
				why = "a collision vertex moved";
				return false;
			}
		}
	}
	return true;
}

/// Appends `b` to `a` (indices shifted).
void append(nectar::MeshModInput& a, const nectar::MeshModInput& b, int materialOffset)
{
	const int p = int(a.positions.size()), n = int(a.normals.size()), u = int(a.uvs.size());
	a.positions.insert(a.positions.end(), b.positions.begin(), b.positions.end());
	a.normals.insert(a.normals.end(), b.normals.begin(), b.normals.end());
	a.uvs.insert(a.uvs.end(), b.uvs.begin(), b.uvs.end());
	for (nectar::MeshModTri t : b.tris) {
		for (int k = 0; k < 3; k++) {
			t.pos[k] += p;
			if (t.nrm[k] >= 0) t.nrm[k] += n;
			if (t.uv[k] >= 0) t.uv[k] += u;
		}
		t.material += materialOffset;
		a.tris.push_back(t);
	}
}

} // namespace

bool compileDressedMap(const nectar::MapData& data, const nectar::GameFileReader& read, const std::string& modPath, const MapDressing& dressing,
                       nectar::CompiledMap& out, std::string& error)
{
	nectar::CompiledMap plain;
	if (!nectar::compileMap(data, read, modPath, plain, error)) return false;

	std::vector<nectar::StandInCliffSpan> cliffs = dressing.cliffs;
	if (dressing.cliffEdges) {
		nectar::Model parsed;
		if (!nectar::parseMod(plain.mod.data(), plain.mod.size(), parsed, error)) {
			error = modPath + ": " + error;
			return false;
		}
		for (const nectar::StandInCliffSpan& s : wallSpans(parsed, data)) cliffs.push_back(s);
	}
	// One mesh per kind, so its materials (and textures) are written once.
	std::vector<std::pair<std::string, nectar::MeshModInput>> meshes;
	if (!cliffs.empty()) {
		nectar::StandInCliffStyle style;
		meshes.push_back({ "cliffs", nectar::standInCliffMesh(cliffs, style) });
	}
	if (!dressing.paths.empty()) {
		nectar::MeshModInput paths;
		bool first = true;
		for (const DressPath& p : dressing.paths) {
			const nectar::MeshModInput one = nectar::standInPathMesh(p.points, p.width, 0.6f, [&](float x, float z) { return nectar::terrainHeight(data, x, z); });
			if (first) {
				paths = one;
				first = false;
			} else {
				append(paths, one, 0);
			}
		}
		meshes.push_back({ "paths", paths });
	}
	if (meshes.empty()) {
		out = plain;
		return true;
	}

	const std::string dir = modPath.substr(0, modPath.find_last_of('/') + 1);
	std::map<std::string, Bytes> served;
	nectar::MapData dressed = data;
	for (const auto& m : meshes) {
		Bytes bytes;
		if (!nectar::writeMeshMod(m.second, bytes, error)) {
			error = modPath + " dressing (" + m.first + "): " + error;
			return false;
		}
		const std::string path = dir + "dressing_" + m.first + ".mod";
		served[path]           = std::move(bytes);
		nectar::MapProp prop {};
		prop.asset   = path;
		prop.scale   = 1.0f;
		prop.collide = false;
		prop.ground  = false;
		dressed.props.push_back(prop);
	}
	const nectar::GameFileReader dressedRead = [&](const std::string& path, Bytes& bytes) {
		auto it = served.find(path);
		if (it != served.end()) {
			bytes = it->second;
			return true;
		}
		return read(path, bytes);
	};
	if (!nectar::compileMap(dressed, dressedRead, modPath, out, error)) {
		error = modPath + " dressed: " + error;
		return false;
	}
	std::string why;
	if (!sameCollision(plain.mod, out.mod, why) || plain.ini != out.ini) {
		error = modPath + ": the dressing changed what the game collides with (" + (why.empty() ? std::string("the .ini differs") : why) + ")";
		return false;
	}
	return true;
}

} // namespace standin
