// Write-then-read tests for nectar::writeMod and the 45 stand-in models (#65).
//
// Oracles, in order of strength:
//  1. nectar::parseMod (libs/nectar_assets/src/mod.cpp), the lib's existing
//     .mod reader. BaseShape::read cannot link into a host test (it needs a
//     live StdSystem for heap, gsys->addGfxObject...), so this is the reader
//     the issue names. Its bounds checks and "chunk start not on boundary"
//     rule mirror the game's.
//  2. A raw chunk walk below that asserts what parseMod does NOT: the 32-byte
//     alignment of every chunk, exact chunkLength (the game ignores it, parseMod
//     jumps by it), the chunk ORDER the game needs (Mesh and Material before
//     Joint) and the 0xFFFF end chunk.
//  3. The game's real CmdStream (src/sysCommon/cmdStream.cpp, linked with the
//     System::halt and sysCon stubs of tests/game_loader_stubs.cpp) over the
//     text after the end chunk. ObjCollInfo::loadini and BaseShape::importIni
//     need the whole engine, so their token loop is transcribed in readColl
//     (same calls, same order). If shpObjColl.cpp:124 or shapeBase.cpp:1901
//     change, update the copy. The input starts 8 bytes after the end chunk
//     starts, where the game's stream is when it calls importIni, so the end
//     chunk's NUL padding goes through the real tokenizer too.
//  4. boot_smoke (#61) on the stand-ins built by the generator (#68) is the
//     proof that BaseShape::read itself accepts the files.
//
// Usage: nectar_mod_writer_test [boot-manifest.txt]. With the manifest, the
// 45 code-derived models are checked against the disc-run list from #64.
#include "nectar_assets/mod.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/piki_rig.h"
#include "nectar_assets/standin_art.h"
#include "nectar_assets/standin_models.h"

#include "CmdStream.h"
#include "Common/String.h"
#include "Stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern std::string g_haltMessage;

namespace {
int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}

using Bytes = std::vector<std::uint8_t>;

std::uint32_t be32(const Bytes& b, std::size_t at)
{
	return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

struct ChunkInfo {
	std::uint32_t type;
	std::size_t start, len;
};

// Walks the chunk list the way BaseShape::read does, checking what parseMod does not.
bool walk(const Bytes& b, std::vector<ChunkInfo>& chunks, std::size_t& endOfEndChunk, const std::string& who)
{
	std::size_t pos = 0;
	chunks.clear();
	while (pos + 8 <= b.size()) {
		check((pos & 0x1F) == 0, who + ": chunk start not 32-aligned");
		ChunkInfo c { be32(b, pos), pos, be32(b, pos + 4) };
		chunks.push_back(c);
		const std::size_t next = pos + 8 + c.len;
		check(next <= b.size(), who + ": chunk longer than file");
		check((next & 0x1F) == 0, who + ": chunkLength does not cover the trailing padding");
		pos = next;
		if (c.type == 0xFFFF) {
			endOfEndChunk = next;
			return true;
		}
	}
	check(false, who + ": no 0xFFFF end chunk");
	return false;
}

// --- BaseShape::importIni / ObjCollInfo::loadini token loop (shapeBase.cpp:1901, shpObjColl.cpp:124) ---
struct CollRead {
	int joint = -1, depth = 0, type = -1;
	std::string id;
	float radius = 0.0f;
	nectar::Vec3 centre;
};

void loadini(CmdStream* cmd, std::vector<CollRead>& out, std::size_t self, int depth)
{
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("id")) {
			cmd->getToken(true);
			out[self].id = std::string(cmd->mCurrentToken, strnlen(cmd->mCurrentToken, 4));
		} else if (cmd->isToken("code")) {
			cmd->getToken(true);
		} else if (cmd->isToken("type")) {
			sscanf(cmd->getToken(true), "%d", &out[self].type);
			if (out[self].type == 1) {
				cmd->getToken(true);
				sscanf(cmd->getToken(true), "%f", &out[self].radius);
				cmd->getToken(true);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.x);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.y);
				sscanf(cmd->getToken(true), "%f", &out[self].centre.z);
			}
		} else if (cmd->isToken("collinfo")) {
			int joint = -1;
			sscanf(cmd->getToken(true), "%d", &joint);
			out.emplace_back();
			const std::size_t child = out.size() - 1;
			out[child].joint        = joint;
			out[child].depth        = depth + 1;
			cmd->getToken(true);
			loadini(cmd, out, child, depth + 1);
		}
	}
	if (!cmd->endOfCmds()) cmd->getToken(true);
}

// `from` is where the game's stream is after reading the end chunk header.
std::vector<CollRead> readColl(const Bytes& b, std::size_t from)
{
	std::vector<CollRead> out;
	Bytes tail(b.begin() + long(from), b.end());
	RamStream file(tail.data(), int(tail.size()));
	CmdStream* cmd = new CmdStream(&file);
	while (!cmd->endOfCmds() && !cmd->endOfSection()) {
		cmd->getToken(true);
		if (cmd->isToken("collinfo")) {
			int joint = -1;
			sscanf(cmd->getToken(true), "%d", &joint);
			out.emplace_back();
			const std::size_t self = out.size() - 1;
			out[self].joint        = joint;
			out[self].depth        = 0;
			cmd->getToken(true);
			loadini(cmd, out, self, 0);
		}
	}
	if (!cmd->endOfCmds()) cmd->getToken(true);
	return out;
}

const CollRead* findColl(const std::vector<CollRead>& v, const char* id)
{
	for (const CollRead& c : v) {
		if (c.id == id) return &c;
	}
	return nullptr;
}

// --- parseMod comparison ----------------------------------------------------
bool same(const nectar::Vec3& a, const nectar::Vec3& b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

void compareModels(const nectar::Model& a, const nectar::Model& b, const std::string& who)
{
	check(a.shapeFlags == b.shapeFlags, who + ": shapeFlags");
	check(a.positions.size() == b.positions.size(), who + ": position count");
	for (std::size_t i = 0; i < a.positions.size() && i < b.positions.size(); i++) check(same(a.positions[i], b.positions[i]), who + ": position");
	check(a.normals.size() == b.normals.size(), who + ": normal count");
	for (std::size_t i = 0; i < a.normals.size() && i < b.normals.size(); i++) check(same(a.normals[i], b.normals[i]), who + ": normal");
	check(a.colours.size() == b.colours.size(), who + ": colour count");
	for (std::size_t i = 0; i < a.colours.size() && i < b.colours.size(); i++) {
		const nectar::Rgba8 &x = a.colours[i], &y = b.colours[i];
		check(x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a, who + ": vertex colour");
	}
	for (int k = 0; k < 8; k++) {
		check(a.uvs[k].size() == b.uvs[k].size(), who + ": uv count");
		for (std::size_t i = 0; i < a.uvs[k].size() && i < b.uvs[k].size(); i++) check(a.uvs[k][i].x == b.uvs[k][i].x && a.uvs[k][i].y == b.uvs[k][i].y, who + ": uv");
	}
	check(a.textures.empty() && b.textures.empty() && b.texAttrs.empty() && b.tevInfos.empty(), who + ": no texture data");
	check(a.materials.size() == b.materials.size(), who + ": material count");
	for (std::size_t i = 0; i < a.materials.size() && i < b.materials.size(); i++) {
		const nectar::ModMaterial &x = a.materials[i], &y = b.materials[i];
		check(x.flags == y.flags && y.texAttr == -1, who + ": material flags and no texture");
		check(x.colour.r == y.colour.r && x.colour.g == y.colour.g && x.colour.b == y.colour.b && x.colour.a == y.colour.a, who + ": material colour");
	}
	check(a.vtxMatrices.size() == b.vtxMatrices.size(), who + ": vtx matrix count");
	for (std::size_t i = 0; i < a.vtxMatrices.size() && i < b.vtxMatrices.size(); i++) {
		check(a.vtxMatrices[i].joint == b.vtxMatrices[i].joint && a.vtxMatrices[i].index == b.vtxMatrices[i].index, who + ": vtx matrix");
	}
	check(b.envelopes.empty(), who + ": no envelopes (rigid)");
	check(a.meshes.size() == b.meshes.size(), who + ": mesh count");
	for (std::size_t i = 0; i < a.meshes.size() && i < b.meshes.size(); i++) {
		const nectar::ModMesh &x = a.meshes[i], &y = b.meshes[i];
		check(x.parentJoint == y.parentJoint && x.flags == y.flags && x.groups.size() == y.groups.size(), who + ": mesh header");
		for (std::size_t g = 0; g < x.groups.size() && g < y.groups.size(); g++) {
			check(x.groups[g].deps == y.groups[g].deps, who + ": group deps");
			check(x.groups[g].tris.size() == y.groups[g].tris.size(), who + ": triangle count");
			for (std::size_t t = 0; t < x.groups[g].tris.size() && t < y.groups[g].tris.size(); t++) {
				for (int k = 0; k < 3; k++) {
					const nectar::ModVertex &p = x.groups[g].tris[t].v[k], &q = y.groups[g].tris[t].v[k];
					const bool colourDiffers = (x.flags & (1u << 2)) && p.col != q.col;
					if (p.pos != q.pos || p.nrm != q.nrm || p.uv[0] != q.uv[0] || p.mtxSlot != q.mtxSlot || colourDiffers) {
						check(false, who + ": vertex");
						t = x.groups[g].tris.size();
						break;
					}
				}
			}
		}
	}
	check(a.joints.size() == b.joints.size(), who + ": joint count");
	for (std::size_t i = 0; i < a.joints.size() && i < b.joints.size(); i++) {
		const nectar::ModJoint &x = a.joints[i], &y = b.joints[i];
		check(x.parent == y.parent && x.flags == y.flags, who + ": joint parent and flags");
		check(same(x.boundsMin, y.boundsMin) && same(x.boundsMax, y.boundsMax), who + ": joint bounds");
		check(same(x.scale, y.scale) && same(x.rotation, y.rotation) && same(x.translation, y.translation), who + ": joint SRT");
		check(x.matPolys == y.matPolys, who + ": joint matPolys");
		check(x.name == y.name, who + ": joint name");
	}
	check(b.collision.empty() && !b.hasGrid, who + ": no map collision");
	check(a.trailingIni == b.trailingIni, who + ": trailing text");
}

nectar::ModTriangle tri(int a, int b, int c, int nrm, int mtx)
{
	nectar::ModTriangle t;
	const int p[3] = { a, b, c };
	for (int k = 0; k < 3; k++) {
		t.v[k].pos     = p[k];
		t.v[k].nrm     = nrm;
		t.v[k].uv[0]   = 0;
		t.v[k].mtxSlot = mtx;
	}
	return t;
}

// A model that exercises every chunk the writer can emit.
nectar::Model handModel()
{
	nectar::Model m;
	m.shapeFlags = 0x0102;
	m.positions  = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	m.normals    = { { 0, 0, 1 }, { 0, 1, 0 } };
	m.colours    = { { 255, 0, 0, 255 }, { 0, 128, 255, 200 }, { 9, 8, 7, 6 } };
	m.uvs[0]     = { { 0.0f, 0.0f }, { 0.5f, 0.25f } };
	m.materials.resize(2);
	m.materials[0].colour = { 10, 20, 30, 255 };
	m.materials[1].colour = { 200, 100, 50, 128 };
	m.materials[1].flags  = nectar::MOD_MAT_ALPHA_BLEND;
	m.vtxMatrices         = { { true, 0 }, { true, 1 }, { true, 2 } };

	nectar::ModMesh a;
	a.parentJoint = 0;
	a.flags       = 1u | (1u << 2) | (1u << 3); // with vertex colours (#115)
	a.groups.resize(1);
	a.groups[0].deps = { 0 };
	a.groups[0].tris = { tri(0, 1, 2, 0, 0), tri(0, 2, 3, 1, 0) };
	for (int t = 0; t < 2; t++) {
		for (int k = 0; k < 3; k++) a.groups[0].tris[std::size_t(t)].v[k].col = (t + k) % 3;
	}
	// More triangles than fit one GX primitive (0xFFFF / 3 = 21845): two primitives in one list.
	nectar::ModMesh big;
	big.parentJoint = 1;
	big.flags       = 1u | (1u << 3);
	big.groups.resize(1);
	big.groups[0].deps = { 1 };
	for (int i = 0; i < 30000; i++) big.groups[0].tris.push_back(tri(0, 1, 2, 0, 0));
	// No matrix byte, two groups (the second with no triangles).
	nectar::ModMesh plain;
	plain.parentJoint = -1;
	plain.flags       = 1u << 3;
	plain.groups.resize(2);
	plain.groups[0].tris = { tri(1, 2, 3, 1, 0) };
	m.meshes             = { a, big, plain };

	m.joints.resize(3);
	m.joints[0].parent    = -1;
	m.joints[0].name      = "root";
	m.joints[0].matPolys  = { { 0, 0 } };
	m.joints[1].parent    = 0;
	m.joints[1].name      = "child";
	m.joints[1].matPolys  = { { 1, 1 }, { 0, 2 } };
	m.joints[1].translation = { 1, 2, 3 };
	m.joints[2].parent    = 1;
	m.joints[2].name      = "leaf";
	m.joints[2].scale     = { 1, 2, 3 };
	m.joints[2].rotation  = { 0.5f, 0.25f, 0.125f };
	m.joints[2].boundsMin = { -1, -2, -3 };
	m.joints[2].boundsMax = { 4, 5, 6 };
	m.trailingIni         = "collinfo 1 {\n\tid\t\ttest\n\tcode\t\tnone\n\ttype\t1\n\tradius\t3.000000\n\tcentre\t1.000000 2.000000 3.000000\n\t}\n";
	return m;
}

// --- tests ------------------------------------------------------------------
void testHandModelRoundTrip()
{
	const nectar::Model m = handModel();
	Bytes bytes;
	std::string err;
	check(nectar::writeMod(m, bytes, err), "hand model writes: " + err);
	nectar::Model back;
	check(nectar::parseMod(bytes.data(), bytes.size(), back, err), "hand model parses: " + err);
	compareModels(m, back, "hand model");

	std::vector<ChunkInfo> chunks;
	std::size_t end = 0;
	check(walk(bytes, chunks, end, "hand model"), "hand model chunk walk");
	std::vector<std::uint32_t> order;
	for (const ChunkInfo& c : chunks) order.push_back(c.type);
	const std::vector<std::uint32_t> want = { 0x00, 0x10, 0x11, 0x13, 0x18, 0x30, 0x40, 0x50, 0x60, 0x61, 0xFFFF };
	check(order == want, "hand model chunk order is header, vertex, normal, colour, uv0, material, vtxmatrix, mesh, joint, jointname, end");
	check(end + m.trailingIni.size() == bytes.size(), "trailing text starts right after the end chunk and ends the file");
	check(std::string(bytes.begin() + long(end), bytes.end()) == m.trailingIni, "trailing text is written verbatim");
	for (std::size_t i = 0; i + 1 < chunks.size(); i++) {
		check(chunks[i].start + 8 + chunks[i].len == chunks[i + 1].start, "chunks are contiguous");
	}
	check(be32(bytes, chunks.back().start + 4) == 24, "end chunk is a 32-byte chunk (length 24), as in the disc files");

	// Without joint names there is no 0x61 chunk.
	nectar::Model unnamed = m;
	for (nectar::ModJoint& j : unnamed.joints) j.name.clear();
	check(nectar::writeMod(unnamed, bytes, err), "unnamed model writes");
	check(walk(bytes, chunks, end, "unnamed"), "unnamed chunk walk");
	bool has61 = false;
	for (const ChunkInfo& c : chunks) has61 = has61 || c.type == 0x61;
	check(!has61, "no JointName chunk when no joint is named");
}

void testRejects()
{
	Bytes out;
	std::string err;
	auto rejects = [&](nectar::Model m, const char* why) {
		err.clear();
		const bool ok = nectar::writeMod(m, out, err);
		check(!ok && !err.empty(), std::string("writeMod rejects ") + why);
	};
	const nectar::Model good = handModel();
	nectar::Model m;
	rejects(m, "a model with no joints");
	m = good; m.joints[0].parent = 0; rejects(m, "a non-root joint 0");
	m = good; m.joints[1].parent = 1; rejects(m, "a parent that is not lower than its child");
	m = good; m.joints[1].parent = 2; rejects(m, "a parent that comes later");
	m = good; m.joints[1].parent = -1; rejects(m, "a second root");
	m = good; m.textures.resize(1); rejects(m, "textures");
	m = good; m.materials[0].flags |= nectar::MOD_MAT_PVW; rejects(m, "PVW materials");
	m = good; m.materials[0].flags &= ~0x700u; rejects(m, "a material with no render pass bit");
	m = good; m.materials[0].flags |= 0x500u; rejects(m, "a material with two render pass bits");
	m = good; m.materials[0].texAttr = 0; rejects(m, "a textured material");
	m = good; m.envelopes.resize(1); rejects(m, "envelopes");
	m = good; m.meshes[0].groups[0].tris[0].v[0].col = 3; rejects(m, "a vertex colour index out of range");
	m = good; m.meshes[0].groups[0].tris[0].v[0].col = -1; rejects(m, "a missing vertex colour on a coloured mesh");
	m = good; m.collRooms = 1; rejects(m, "map collision");
	m = good; m.meshes[0].groups[0].tris[0].v[0].pos = 99; rejects(m, "a vertex position out of range");
	m = good; m.meshes[0].groups[0].tris[0].v[0].nrm = 99; rejects(m, "a normal out of range");
	m = good; m.meshes[0].groups[0].tris[0].v[0].uv[0] = 99; rejects(m, "a UV out of range");
	m = good; m.meshes[0].groups[0].deps = { 7 }; rejects(m, "a dependency on a missing vtx matrix");
	m = good; m.vtxMatrices[0].joint = false; rejects(m, "an envelope vtx matrix");
	m = good; m.vtxMatrices[0].index = 9; rejects(m, "a vtx matrix on a missing joint");
	m = good; m.meshes[0].parentJoint = 9; rejects(m, "a mesh on a missing joint");
	m = good; m.joints[0].matPolys = { { 5, 0 } }; rejects(m, "a matPoly on a missing material");
	m = good; m.joints[0].matPolys = { { 0, 9 } }; rejects(m, "a matPoly on a missing mesh");
	m = good; m.meshes[2].flags |= 1u << 2; rejects(m, "the vertex-colour mesh flag on a mesh whose vertices name no colour");
}

// --- stand-in models ----------------------------------------------------------
bool isChain(const std::vector<int>& parents)
{
	if (parents.empty() || parents[0] != -1) return false;
	for (std::size_t i = 1; i < parents.size(); i++) {
		if (parents[i] < 0 || std::size_t(parents[i]) >= i) return false;
	}
	return true;
}

void testStandIns(const std::string& manifestPath)
{
	const std::vector<nectar::StandInModelSpec>& specs = nectar::standInModelSpecs();
	check(int(specs.size()) == nectar::kStandInModelCount, "45 stand-in models");
	std::set<std::string> paths;
	for (const nectar::StandInModelSpec& s : specs) paths.insert(s.path);
	check(paths.size() == specs.size(), "stand-in paths are unique");

	std::vector<nectar::StandInModelFile> files;
	std::string err;
	// Blender art (#115): every binding names exported art made for the spec's rig.
	check(nectar::checkStandInArtBindings(specs, err), "Blender art bindings: " + err);
	for (const nectar::StandInArtBinding& b : nectar::standInArtBindings()) {
		const nectar::StandInModelSpec* spec = nectar::findStandInModel(b.path);
		if (!spec) continue;
		const nectar::Model m = nectar::buildStandInModel(*spec);
		check(!m.colours.empty(), std::string(b.path) + ": drawn from its Blender art (vertex colours)");
		// A tinted joint draws with material 0 (the one the game recolours), made
		// white, and its art is authored white: grey vertex colours (the baked
		// shading), so the runtime colour is the colour.
		for (int tj : b.tintJoints) {
			const std::string where = std::string(b.path) + " joint " + std::to_string(tj);
			const auto& mp = m.joints[std::size_t(tj)].matPolys;
			check(mp.size() == 1 && mp[0].first == 0, where + ": the tinted joint uses material 0");
			const nectar::Rgba8 c = m.materials[0].colour;
			check(c.r == 255 && c.g == 255 && c.b == 255, std::string(b.path) + ": material 0 is white under the tint");
			int spread = 0;
			std::size_t corners = 0;
			for (const nectar::ModTriangle& t : m.meshes[std::size_t(tj)].groups[0].tris) {
				for (const auto& v : t.v) {
					const nectar::Rgba8 vc = m.colours[std::size_t(v.col)];
					spread = std::max(spread, std::max({ int(vc.r), int(vc.g), int(vc.b) }) - std::min({ int(vc.r), int(vc.g), int(vc.b) }));
					corners++;
				}
			}
			check(corners > 0, where + ": the tinted joint has art");
			check(spread <= 16, where + ": the tinted art is authored white (vertex colour spread " + std::to_string(spread) + ")");
		}
	}
	// The walking folk (review of #160): ViewPiki::demoDraw sets the type colour,
	// the idle pastel and the white damage flash on material 0, so the body
	// (joint 0), arms (7, 8) and feet (10, 11) draw with it; the eyes and the
	// trait (joint 1) keep their own material.
	for (const char* path : { "dataDir/pikis/redModel.mod", "dataDir/pikis/yelModel.mod", "dataDir/pikis/bluModel.mod", "dataDir/pikis/kinModel.mod" }) {
		const nectar::StandInArtBinding* folk = nullptr;
		for (const nectar::StandInArtBinding& b : nectar::standInArtBindings()) {
			if (std::string(b.path) == path) folk = &b;
		}
		check(folk != nullptr, std::string(path) + ": has Blender art");
		if (!folk) continue;
		for (int j : { 0, nectar::kPikiRigRightHandJoint, nectar::kPikiRigLeftHandJoint, 10, 11 }) {
			check(std::find(folk->tintJoints.begin(), folk->tintJoints.end(), j) != folk->tintJoints.end(),
			      std::string(path) + ": joint " + std::to_string(j) + " takes the game's folk colour");
		}
		const nectar::Model m = nectar::buildStandInModel(*nectar::findStandInModel(path));
		const auto& eyes = m.joints[1].matPolys;
		check(eyes.size() == 1 && eyes[0].first != 0, std::string(path) + ": the eyes and trait (joint 1) keep their own colours");
	}
	check(nectar::writeStandInModels(files, err), "writeStandInModels: " + err);
	check(files.size() == specs.size(), "one file per spec");

	std::map<std::string, nectar::Model> parsed;
	for (const nectar::StandInModelFile& f : files) {
		const nectar::StandInModelSpec* spec = nectar::findStandInModel(f.path);
		check(spec != nullptr, f.path + ": has a spec");
		if (!spec) continue;
		nectar::Model m;
		check(nectar::parseMod(f.bytes.data(), f.bytes.size(), m, err), f.path + ": parses: " + err);
		parsed[f.path] = m;

		// Round trip against the Model the builder made.
		compareModels(nectar::buildStandInModel(*spec), m, f.path);

		// The structure the game needs.
		std::vector<ChunkInfo> chunks;
		std::size_t end = 0;
		walk(f.bytes, chunks, end, f.path);
		std::map<std::uint32_t, int> seen;
		std::size_t meshAt = 0, matAt = 0, jointAt = 0, vtxAt = 0;
		for (std::size_t i = 0; i < chunks.size(); i++) {
			seen[chunks[i].type]++;
			if (chunks[i].type == 0x50) meshAt = i;
			if (chunks[i].type == 0x30) matAt = i;
			if (chunks[i].type == 0x60) jointAt = i;
			if (chunks[i].type == 0x40) vtxAt = i;
		}
		check(seen[0x00] == 1 && seen[0x10] == 1 && seen[0x11] == 1 && seen[0x30] == 1 && seen[0x40] == 1 && seen[0x50] == 1 && seen[0x60] == 1 && seen[0xFFFF] == 1,
		      f.path + ": one of each required chunk");
		check(meshAt < jointAt && matAt < jointAt && vtxAt < jointAt, f.path + ": Mesh, Material and VtxMatrix precede Joint");
		check(chunks.back().type == 0xFFFF, f.path + ": ends with 0xFFFF");
		for (const ChunkInfo& c : chunks) {
			check(c.type != 0x20 && c.type != 0x22 && c.type != 0x41 && c.type != 0x61 && c.type != 0x100 && c.type != 0x110 && c.type != 0xF00,
			      f.path + ": no texture, envelope, name, collision or PC chunk");
		}

		check(m.joints.size() == std::size_t(spec->jointCount()), f.path + ": joint count");
		check(isChain(spec->parents), f.path + ": joint 0 is the root and parents are lower");
		for (std::size_t j = 0; j < m.joints.size(); j++) check(m.joints[j].parent == spec->parents[j], f.path + ": joint parent");
		// Joint j owns mesh j; a joint drawn in several colours owns extra meshes after
		// those, one per material, all rigid on that joint and listed in its matPolys.
		check(m.meshes.size() >= m.joints.size() && m.materials.size() >= 1, f.path + ": a mesh per joint, at least one material");
		for (std::size_t j = 0; j < m.meshes.size(); j++) {
			const int owner = m.meshes[j].parentJoint;
			check((j < m.joints.size() ? owner == int(j) : owner >= 0 && owner < int(m.joints.size())) && m.meshes[j].groups.size() == 1
			          && m.meshes[j].groups[0].deps.size() == 1,
			      f.path + ": rigid mesh layout");
			check(m.vtxMatrices[std::size_t(m.meshes[j].groups[0].deps[0])].joint && m.vtxMatrices[std::size_t(m.meshes[j].groups[0].deps[0])].index == owner,
			      f.path + ": mesh depends on its own joint");
		}
		for (std::size_t j = 0; j < m.joints.size(); j++) {
			check(!m.joints[j].matPolys.empty(), f.path + ": every joint draws a mesh");
			for (const auto& mp : m.joints[j].matPolys) {
				check(mp.second >= 0 && std::size_t(mp.second) < m.meshes.size() && m.meshes[std::size_t(mp.second)].parentJoint == int(j),
				      f.path + ": matPoly mesh belongs to its joint");
			}
		}

		// Front faces are clockwise: n = (c - a) x (b - a) points along the stored normal.
		for (const nectar::ModTriangle& t : m.meshes[0].groups[0].tris) {
			const nectar::Vec3 &a = m.positions[std::size_t(t.v[0].pos)], &b = m.positions[std::size_t(t.v[1].pos)], &c = m.positions[std::size_t(t.v[2].pos)];
			const nectar::Vec3 u { c.x - a.x, c.y - a.y, c.z - a.z }, v { b.x - a.x, b.y - a.y, b.z - a.z };
			const nectar::Vec3 n { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
			const nectar::Vec3& s = m.normals[std::size_t(t.v[0].nrm)];
			check(n.x * s.x + n.y * s.y + n.z * s.z > 0.0f, f.path + ": clockwise front faces");
		}

		// The collinfo text, through the real CmdStream.
		g_haltMessage.clear();
		const std::vector<CollRead> parts = readColl(f.bytes, chunks.back().start + 8);
		check(g_haltMessage.empty(), f.path + ": CmdStream raised an error: " + g_haltMessage);
		check(spec->collInfo.empty() == parts.empty(), f.path + ": collinfo present exactly when specified");
		for (const CollRead& p : parts) {
			check(p.joint >= 0 && p.joint < spec->jointCount(), f.path + ": collinfo " + p.id + " joint index is in range");
			check(p.type == 1 && p.radius > 0.0f && p.id.size() == 4, f.path + ": collinfo " + p.id + " is a sphere with a 4-letter id");
		}
	}

	// Shared rig (#66 includes the same header): captain and the four Pikmin.
	const char* rigModels[] = { "pikis/nv3Model.mod", "pikis/bluModel.mod", "pikis/redModel.mod", "pikis/yelModel.mod", "pikis/kinModel.mod" };
	for (const char* name : rigModels) {
		const nectar::StandInModelSpec* s = nectar::findStandInModel(std::string("dataDir/") + name);
		check(s != nullptr && s->jointCount() == nectar::kPikiRigJointCount, std::string(name) + ": 12 joints, the shared rig");
		if (!s) continue;
		bool same = true;
		for (int j = 0; j < nectar::kPikiRigJointCount; j++) same = same && s->parents[std::size_t(j)] == nectar::kPikiRigParents[j];
		check(same, std::string(name) + ": the rig's parent table");
	}
	check(nectar::kPikiRigRightHandJoint == 7 && nectar::kPikiRigJointCount == 12, "rig constants: 12 joints, rhnd hand joint 7");
	check(nectar::kPikiRigParents[0] == -1 && nectar::kPikiRigParents[11] == 9, "rig parents {-1,0,1,2,2,4,5,1,1,0,9,9}");

	// Captain: the 'rhnd' part PikiGoHangState and NaviThrowWaitState dereference unchecked.
	{
		const nectar::StandInModelSpec* s = nectar::findStandInModel("dataDir/pikis/nv3Model.mod");
		for (const nectar::StandInModelFile& f : files) {
			if (f.path != s->path) continue;
			std::vector<ChunkInfo> chunks;
			std::size_t end = 0;
			walk(f.bytes, chunks, end, f.path);
			const std::vector<CollRead> parts = readColl(f.bytes, chunks.back().start + 8);
			const CollRead* rhnd = findColl(parts, "rhnd");
			check(rhnd && rhnd->joint == nectar::kPikiRigRightHandJoint && rhnd->radius == 2.0f && rhnd->centre.x == 2.7f && rhnd->centre.y == 0.0f && rhnd->centre.z == 0.0f,
			      "captain has rhnd on the right hand (joint 7), radius 2, centre 2.7 0 0");
			check(findColl(parts, "cent") && findColl(parts, "head") && findColl(parts, "bnds") && findColl(parts, "ante"), "captain also has bnds, cent, head, ante");
			const CollRead* head = findColl(parts, "head");
			check(head && head->joint == nectar::kPikiRigHeadJoint && head->depth == 1, "captain head is a child of bnds on joint 2");
		}
	}

	// Ship: overrideAnim joints 0 38 36 19 32 37 29 53 (ufoAnim.cpp:26-40), 2 41 48 49 (ufoItem.cpp:1031-1079), getAnimMatrix(11) (playerState.cpp:1416).
	{
		const nectar::StandInModelSpec* ship = nectar::findStandInModel("dataDir/objects/ufo/ufo0705.mod");
		check(ship && ship->jointCount() >= nectar::kStandInShipMinJoints && nectar::kStandInShipMinJoints == 54, "ship has at least 54 joints");
		const int read[] = { 0, 38, 36, 19, 32, 37, 29, 53, 2, 41, 48, 49, 11 };
		for (int j : read) check(ship && j < ship->jointCount(), "ship joint " + std::to_string(j) + " exists");
		check(ship && ship->jointCount() == 57, "ship joint count is the disc's 57");
		for (const nectar::StandInModelFile& f : files) {
			if (f.path != ship->path) continue;
			std::vector<ChunkInfo> chunks;
			std::size_t end = 0;
			walk(f.bytes, chunks, end, f.path);
			const std::vector<CollRead> parts = readColl(f.bytes, chunks.back().start + 8);
			for (const char* id : { "gol1", "gcen", "suc0", "suc1", "eng1", "eng2", "eng3", "eng4" }) check(findColl(parts, id) != nullptr, std::string("ship has ") + id);
		}
	}

	// Onyon: legN with child basN, plus cent, cont, piki, eff1-3, llit.
	for (const nectar::StandInModelFile& f : files) {
		if (f.path != "dataDir/objects/goal/goal.mod") continue;
		std::vector<ChunkInfo> chunks;
		std::size_t end = 0;
		walk(f.bytes, chunks, end, f.path);
		const std::vector<CollRead> parts = readColl(f.bytes, chunks.back().start + 8);
		for (const char* id : { "cent", "leg1", "leg2", "leg3", "bas1", "bas2", "bas3", "cont", "piki", "eff1", "eff2", "eff3", "llit" }) {
			check(findColl(parts, id) != nullptr, std::string("goal has ") + id);
		}
		for (int i = 1; i <= 3; i++) {
			const CollRead* bas = findColl(parts, (std::string("bas") + std::to_string(i)).c_str());
			check(bas && bas->depth == 1 && bas->centre.x == 120.0f, "goal basN is a child of legN");
		}
	}

	// Other minimum joint counts from the code.
	auto joints = [&](const char* path) {
		const nectar::StandInModelSpec* s = nectar::findStandInModel(std::string("dataDir/") + path);
		return s ? s->jointCount() : 0;
	};
	check(joints("objects/pikihead/pikihead.mod") >= 4, "pikihead has at least 4 joints (joint 3)");
	check(joints("shapes/cursor/marker.mod") >= 1 && joints("shapes/cursor/cursor.mod") >= 1, "cursor shapes have a joint");
	// Regression guard for #118/#119: a cursor with a mesh on joint 1 used to
	// crash the draw, because the cursor never runs updateAnim.
	check(joints("shapes/cursor/cursor.mod") == 2, "cursor.mod keeps its second joint (#119)");
	for (const nectar::StandInModelSpec& s : specs) check(s.jointCount() >= 1, s.path + ": at least one joint");

	// The 16 ship parts: any repair index below the disc's joint count stays in range (#66).
	check(joints("objects/ufoparts/soto1.mod") == 11 && joints("objects/ufoparts/soto5.mod") == 20 && joints("objects/ufoparts/fuzoku1.mod") == 5,
	      "ship parts keep the disc joint counts");

	// Manifest cross-check (#64): the code-derived set must be inside the disc-run list.
	if (manifestPath.empty()) {
		std::printf("note: no manifest argument, skipping the boot-manifest cross-check\n");
		return;
	}
	std::ifstream in(manifestPath);
	check(bool(in), "boot manifest opens: " + manifestPath);
	std::set<std::string> manifest;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		const std::string path = line.substr(0, line.find('\t'));
		if (path.size() > 4 && path.compare(path.size() - 4, 4, ".mod") == 0) manifest.insert(path);
	}
	check(!manifest.empty(), "boot manifest lists .mod files");
	for (const nectar::StandInModelSpec& s : specs) check(manifest.count(s.path) == 1, s.path + ": appears in the boot manifest");
	// Extras are reported, never silently added. Known stage-conditional or data-driven prefixes are classified.
	const char* stageConditional[] = { "dataDir/tekis/", "dataDir/bosses/", "dataDir/objects/plants/", "dataDir/objects/bridge/", "dataDir/objects/gate/",
		                               "dataDir/objects/hinderrock/", "dataDir/courses/" };
	int classified = 0;
	for (const std::string& p : manifest) {
		if (paths.count(p)) continue;
		bool known = false;
		for (const char* pre : stageConditional) known = known || p.rfind(pre, 0) == 0;
		if (known) {
			classified++;
		} else {
			std::printf("REPORT: manifest .mod outside the 45 and not stage-conditional (data-driven or a gap): %s\n", p.c_str());
		}
	}
	std::printf("manifest: %zu .mod files, %d code-derived, %d stage-conditional extras\n", manifest.size(), nectar::kStandInModelCount, classified);
}
} // namespace

int main(int argc, char** argv)
{
	testHandModelRoundTrip();
	testRejects();
	testStandIns(argc > 1 ? argv[1] : "");
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("mod writer tests passed\n");
	return 0;
}
