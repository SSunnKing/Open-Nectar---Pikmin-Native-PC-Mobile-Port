#include "nectar_assets/standin_art.h"

#include "standin_art_meshes.h"

#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/piki_rig.h"
#include "nectar_assets/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace nectar {

namespace {

constexpr Vec3 kFaceNormal[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
constexpr int kFaces[6][4] = { { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };

int addMaterial(Model& m, Rgba8 colour)
{
	ModMaterial mat;
	mat.colour = colour;
	m.materials.push_back(mat);
	return int(m.materials.size()) - 1;
}

ModMtxGroup& jointGroup(Model& m, int joint)
{
	return m.meshes[std::size_t(joint)].groups[0];
}

bool jointInRange(const StandInModelSpec& spec, int joint)
{
	return joint >= 0 && joint < spec.jointCount();
}

void clearJointMesh(Model& m, int joint)
{
	jointGroup(m, joint).tris.clear();
}

// A .mod mesh has one material (ModJoint::matPolys pairs a material with a mesh),
// so a second colour on the same joint needs its own mesh. replaceJoint clears the
// joint back to its first mesh; otherwise the part goes in the joint's mesh for
// that material, made on first use.
ModMtxGroup& groupFor(Model& m, int joint, int material, bool replaceJoint)
{
	ModJoint& j = m.joints[std::size_t(joint)];
	if (replaceJoint || j.matPolys.empty()) {
		clearJointMesh(m, joint);
		j.matPolys.clear();
		j.matPolys.push_back({ material, joint });
		return jointGroup(m, joint);
	}
	for (const auto& mp : j.matPolys) {
		if (mp.first == material) return m.meshes[std::size_t(mp.second)].groups[0];
	}
	ModMesh mesh;
	mesh.parentJoint = joint;
	mesh.flags       = m.meshes[std::size_t(joint)].flags;
	mesh.groups.resize(1);
	mesh.groups[0].deps = { joint };
	m.meshes.push_back(std::move(mesh));
	j.matPolys.push_back({ material, int(m.meshes.size()) - 1 });
	return m.meshes.back().groups[0];
}

int addUnitNormal(Model& m, Vec3 n)
{
	const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (len > 1e-6f) {
		n.x /= len;
		n.y /= len;
		n.z /= len;
	}
	m.normals.push_back(n);
	return int(m.normals.size()) - 1;
}

// normalHint < 0 orients the triangle away from `centre` (the centre of the
// sphere it belongs to), not from the joint origin: for a sphere off the origin
// the origin test flips some faces inward, which lit them as a dark checker.
void pushClockwiseTri(Model& m, ModMtxGroup& group, int ia, int ib, int ic, int normalHint, Vec3 centre = {})
{
	auto crossN = [&](int a, int b, int c) {
		const Vec3 &pa = m.positions[std::size_t(a)], &pb = m.positions[std::size_t(b)], &pc = m.positions[std::size_t(c)];
		const Vec3 u { pc.x - pa.x, pc.y - pa.y, pc.z - pa.z }, v { pb.x - pa.x, pb.y - pa.y, pb.z - pa.z };
		return Vec3 { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
	};

	int nrm = normalHint;
	if (nrm < 0) {
		Vec3 sum {};
		for (int idx : { ia, ib, ic }) {
			Vec3 p = m.positions[std::size_t(idx)];
			p.x -= centre.x;
			p.y -= centre.y;
			p.z -= centre.z;
			const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
			if (len > 1e-6f) {
				sum.x += p.x / len;
				sum.y += p.y / len;
				sum.z += p.z / len;
			}
		}
		const float len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
		if (len > 1e-6f) {
			sum.x /= len;
			sum.y /= len;
			sum.z /= len;
		}
		if (crossN(ia, ib, ic).x * sum.x + crossN(ia, ib, ic).y * sum.y + crossN(ia, ib, ic).z * sum.z < 0.0f) std::swap(ib, ic);
		nrm = addUnitNormal(m, crossN(ia, ib, ic));
	} else {
		const Vec3& want = m.normals[std::size_t(nrm)];
		if (crossN(ia, ib, ic).x * want.x + crossN(ia, ib, ic).y * want.y + crossN(ia, ib, ic).z * want.z < 0.0f) std::swap(ib, ic);
	}

	ModTriangle tr;
	const int order[3] = { ia, ib, ic };
	for (int k = 0; k < 3; k++) {
		tr.v[k].mtxSlot = 0;
		tr.v[k].pos     = order[k];
		tr.v[k].nrm     = nrm;
		tr.v[k].uv[0]   = 0;
	}
	group.tris.push_back(tr);
}

void addBoxMesh(Model& m, const StandInModelSpec& spec, int joint, int material, float hx, float hy, float hz, Vec3 center, bool replaceJoint = true)
{
	if (!jointInRange(spec, joint)) return;
	ModMtxGroup& group = groupFor(m, joint, material, replaceJoint);
	const int base     = int(m.positions.size());
	for (int c = 0; c < 8; c++) {
		m.positions.push_back({ center.x + ((c & 1) ? hx : -hx), center.y + ((c & 2) ? hy : -hy), center.z + ((c & 4) ? hz : -hz) });
	}
	if (m.normals.empty()) m.normals.assign(kFaceNormal, kFaceNormal + 6);
	if (m.uvs[0].empty()) m.uvs[0].push_back(Vec2 {});

	for (int f = 0; f < 6; f++) {
		const int* q = kFaces[f];
		const int tri[2][3] = { { q[0], q[1], q[2] }, { q[0], q[2], q[3] } };
		for (const auto& t : tri) {
			pushClockwiseTri(m, group, base + t[0], base + t[1], base + t[2], f);
		}
	}
}

// Ellipsoid split into horizontal bands, top to bottom, one material per band:
// light top, darker underside. Unlit stand-in draws are flat colour, so the bands
// are what make a bulb or a hull read as round.
void addBandedEllipsoid(Model& m, const StandInModelSpec& spec, int joint, const std::vector<int>& bands, Vec3 radii, int stacks, int slices,
                        Vec3 center, bool replaceJoint = true)
{
	if (!jointInRange(spec, joint) || bands.empty()) return;
	if (m.normals.empty()) m.normals.assign(kFaceNormal, kFaceNormal + 6);
	if (m.uvs[0].empty()) m.uvs[0].push_back(Vec2 {});
	// Make every band's mesh first: groupFor can grow m.meshes and move groups.
	groupFor(m, joint, bands[0], replaceJoint);
	for (int mat : bands) groupFor(m, joint, mat, false);
	auto bandGroup = [&](int row) -> ModMtxGroup& {
		const int mat = bands[std::size_t(std::min(int(bands.size()) - 1, row * int(bands.size()) / stacks))];
		return groupFor(m, joint, mat, false);
	};

	const int base  = int(m.positions.size());
	const int north = base;
	m.positions.push_back({ center.x, center.y + radii.y, center.z });
	for (int si = 1; si < stacks; si++) {
		const float phi = float(si) / float(stacks) * 3.14159265f;
		for (int ti = 0; ti < slices; ti++) {
			const float theta = float(ti) / float(slices) * 2.0f * 3.14159265f;
			m.positions.push_back({ center.x + radii.x * std::sin(phi) * std::cos(theta), center.y + radii.y * std::cos(phi),
			                        center.z + radii.z * std::sin(phi) * std::sin(theta) });
		}
	}
	const int south = int(m.positions.size());
	m.positions.push_back({ center.x, center.y - radii.y, center.z });

	auto ring = [&](int si, int ti) { return base + 1 + (si - 1) * slices + (ti % slices); };
	for (int ti = 0; ti < slices; ti++) {
		pushClockwiseTri(m, bandGroup(0), north, ring(1, ti + 1), ring(1, ti), -1, center);
	}
	for (int si = 1; si < stacks - 1; si++) {
		for (int ti = 0; ti < slices; ti++) {
			const int i0 = ring(si, ti), i1 = ring(si, ti + 1), i2 = ring(si + 1, ti + 1), i3 = ring(si + 1, ti);
			pushClockwiseTri(m, bandGroup(si), i0, i1, i2, -1, center);
			pushClockwiseTri(m, bandGroup(si), i0, i2, i3, -1, center);
		}
	}
	for (int ti = 0; ti < slices; ti++) {
		pushClockwiseTri(m, bandGroup(stacks - 1), south, ring(stacks - 1, ti), ring(stacks - 1, ti + 1), -1, center);
	}
}

void addSphereMesh(Model& m, const StandInModelSpec& spec, int joint, int material, float radius, int stacks, int slices, Vec3 center, bool replaceJoint = true)
{
	addBandedEllipsoid(m, spec, joint, { material }, { radius, radius, radius }, stacks, slices, center, replaceJoint);
}

Model skeletonModel(const StandInModelSpec& spec)
{
	Model m;
	m.normals.assign(kFaceNormal, kFaceNormal + 6);
	m.uvs[0].push_back(Vec2 {});
	const int joints = spec.jointCount();
	const int defaultMat = addMaterial(m, spec.colour);
	for (int j = 0; j < joints; j++) {
		const float h = j == 0 ? spec.halfSize : spec.halfSize * 0.5f;
		ModMesh mesh;
		mesh.parentJoint = j;
		mesh.flags       = 1u | (1u << 3);
		mesh.groups.resize(1);
		mesh.groups[0].deps = { j };
		m.meshes.push_back(std::move(mesh));
		ModJoint joint;
		joint.parent    = spec.parents[std::size_t(j)];
		joint.boundsMin = { -h, -h, -h };
		joint.boundsMax = { h, h, h };
		joint.scale     = { 1, 1, 1 };
		m.joints.push_back(std::move(joint));
		m.vtxMatrices.push_back({ true, j });
		(void)defaultMat;
	}
	m.trailingIni = spec.collInfo;
	return m;
}

void fillEmptyJointMeshes(Model& m, const StandInModelSpec& spec)
{
	const int placeholderMat = addMaterial(m, { 8, 8, 8, 255 });
	for (int j = 0; j < spec.jointCount(); j++) {
		if (jointGroup(m, j).tris.empty()) addBoxMesh(m, spec, j, placeholderMat, 0.01f, 0.01f, 0.01f, {}, true);
	}
}

void addPlatformCollision(Model& m, const StandInModelSpec& spec)
{
	if (!spec.platform || m.meshes.empty()) return;
	for (const ModMtxGroup& g : m.meshes[0].groups) {
		for (const ModTriangle& t : g.tris) {
			ModCollTri c;
			c.code = 0x20000000u;
			for (int k = 0; k < 3; k++) c.v[k] = t.v[k].pos;
			m.collision.push_back(c);
		}
	}
}

enum class Kind {
	Generic,
	Captain,
	Piki,
	Cursor,
	Marker,
	Happa,
	Pellet,
	Onion,
	Ship,
	Bomb,
	PikiHead,
	Chappy,
};

Kind kindForPath(const std::string& path)
{
	if (path.find("nv3Model") != std::string::npos) return Kind::Captain;
	if (path.find("Model.mod") != std::string::npos && path.find("pikis/") != std::string::npos) return Kind::Piki;
	if (path.find("cursor.mod") != std::string::npos) return Kind::Cursor;
	if (path.find("marker.mod") != std::string::npos) return Kind::Marker;
	if (path.find("happas/") != std::string::npos) return Kind::Happa;
	if (path.find("pellets/") != std::string::npos) return Kind::Pellet;
	if (path.find("objects/goal/goal.mod") != std::string::npos) return Kind::Onion;
	if (path.find("ufo0705") != std::string::npos) return Kind::Ship;
	if (path.find("bomb.mod") != std::string::npos) return Kind::Bomb;
	if (path.find("pikihead") != std::string::npos) return Kind::PikiHead;
	if (path.find("chappy") != std::string::npos) return Kind::Chappy;
	return Kind::Generic;
}

void buildPikiBody(Model& m, const StandInModelSpec& spec, bool red, bool yellow, bool blue)
{
	// Material 0 is tinted at runtime via MaterialList[0].setColour(); keep the whole
	// body on slot 0 and secondary slots for eyes / stem toppers only.
	Rgba8 skin = spec.colour;
	if (red) skin = { 255, 45, 50, 255 };
	else if (yellow) skin = { 255, 215, 35, 255 };
	else if (blue) skin = { 55, 95, 255, 255 };
	const int skinMat   = addMaterial(m, skin);
	const int eyeDark   = addMaterial(m, { 15, 15, 20, 255 });
	const int leafMat   = addMaterial(m, { 45, 190, 55, 255 });
	const int budMat    = addMaterial(m, { 140, 220, 75, 255 });
	const int flowerMat = addMaterial(m, { 255, 180, 210, 255 });
	const int noseMat   = addMaterial(m, { 220, 40, 40, 255 });

	// The stand-in animations leave every joint at the model origin, so each part's
	// offset is its rest position; the feet sit about 1 unit under the origin.
	// Legs, a round body, a big round head on top with two dark eyes on its front
	// (+z), and the stem rising from the crown. The happa .mod is drawn at the
	// leaf-tip joint and is offset up to the stem top in its own stand-in.
	addBoxMesh(m, spec, 10, skinMat, 0.3f, 0.55f, 0.3f, { 0.55f, -0.55f, 0.0f });
	addBoxMesh(m, spec, 11, skinMat, 0.3f, 0.55f, 0.3f, { -0.55f, -0.55f, 0.0f });
	addBandedEllipsoid(m, spec, 0, { skinMat }, { 1.2f, 1.6f, 1.1f }, 6, 10, { 0.0f, 1.4f, 0.0f });
	addBoxMesh(m, spec, kPikiRigRightHandJoint, skinMat, 0.28f, 0.85f, 0.28f, { 1.35f, 1.5f, 0.0f });
	addBoxMesh(m, spec, kPikiRigLeftHandJoint, skinMat, 0.28f, 0.85f, 0.28f, { -1.35f, 1.5f, 0.0f });
	addSphereMesh(m, spec, kPikiRigHeadJoint, skinMat, 1.6f, 7, 10, { 0.0f, 4.4f, 0.2f });
	addBoxMesh(m, spec, kPikiRigHeadJoint, eyeDark, 0.28f, 0.36f, 0.1f, { -0.55f, 4.7f, 1.72f }, false);
	addBoxMesh(m, spec, kPikiRigHeadJoint, eyeDark, 0.28f, 0.36f, 0.1f, { 0.55f, 4.7f, 1.72f }, false);
	if (red) addBoxMesh(m, spec, 3, noseMat, 0.3f, 0.25f, 0.45f, { 0.0f, 4.15f, 1.85f });
	addBoxMesh(m, spec, 4, skinMat, 0.15f, 1.1f, 0.15f, { 0.0f, 7.0f, 0.0f });
	(void)leafMat;
	(void)budMat;
	(void)flowerMat;
}

void buildCaptain(Model& m, const StandInModelSpec& spec)
{
	const int suit = addMaterial(m, { 240, 240, 250, 255 });
	const int pack = addMaterial(m, { 190, 55, 55, 255 });
	const int helm = addMaterial(m, { 200, 220, 255, 255 });
	const int dark = addMaterial(m, { 35, 35, 45, 255 });
	const int face = addMaterial(m, { 235, 190, 150, 255 });
	// Every joint stays at the origin (see buildPikiBody); feet about 1 under it.
	// Dark boots, a round white suit with the red pack on his back, then the glass
	// helmet with his face, big nose and dark eyes at the front, and the antenna.
	addBoxMesh(m, spec, 10, dark, 0.45f, 0.8f, 0.45f, { 0.7f, -0.2f, 0.0f });
	addBoxMesh(m, spec, 11, dark, 0.45f, 0.8f, 0.45f, { -0.7f, -0.2f, 0.0f });
	addBandedEllipsoid(m, spec, 1, { suit }, { 1.7f, 1.8f, 1.5f }, 6, 10, { 0.0f, 2.2f, 0.0f });
	addBoxMesh(m, spec, 4, pack, 1.2f, 1.4f, 0.6f, { 0.0f, 2.6f, -1.7f });
	addBoxMesh(m, spec, kPikiRigRightHandJoint, suit, 0.45f, 1.0f, 0.45f, { 2.0f, 2.4f, 0.0f });
	addBoxMesh(m, spec, kPikiRigLeftHandJoint, suit, 0.45f, 1.0f, 0.45f, { -2.0f, 2.4f, 0.0f });
	addBoxMesh(m, spec, 0, dark, 1.9f, 0.25f, 1.9f, { 0.0f, 3.9f, 0.0f });
	addSphereMesh(m, spec, kPikiRigHeadJoint, helm, 2.2f, 7, 10, { 0.0f, 6.0f, 0.3f });
	addBoxMesh(m, spec, kPikiRigHeadJoint, face, 1.2f, 1.0f, 0.15f, { 0.0f, 6.0f, 2.45f }, false);
	addSphereMesh(m, spec, kPikiRigHeadJoint, face, 0.6f, 5, 8, { 0.0f, 5.6f, 2.75f }, false);
	addBoxMesh(m, spec, kPikiRigHeadJoint, dark, 0.25f, 0.3f, 0.1f, { -0.5f, 6.4f, 2.65f }, false);
	addBoxMesh(m, spec, kPikiRigHeadJoint, dark, 0.25f, 0.3f, 0.1f, { 0.5f, 6.4f, 2.65f }, false);
	addBoxMesh(m, spec, 5, dark, 0.12f, 1.0f, 0.12f, { 0.0f, 9.2f, 0.0f });
	addSphereMesh(m, spec, 6, pack, 0.4f, 4, 6, { 0.0f, 10.4f, 0.0f });
	(void)spec;
}

void buildOnion(Model& m, const StandInModelSpec& spec)
{
	// Red Onion: a round red bulb up on three thin legs, a pale rim under it, and a
	// stalk with a pale flower on top. The bulb is banded light to dark so it reads
	// as round when drawn unlit.
	const float k = spec.halfSize / 15.0f;
	std::vector<int> bulb;
	for (Rgba8 c : { Rgba8 { 255, 125, 120, 255 }, Rgba8 { 240, 60, 60, 255 }, Rgba8 { 215, 38, 44, 255 }, Rgba8 { 170, 26, 32, 255 },
	                 Rgba8 { 120, 18, 24, 255 } }) {
		bulb.push_back(addMaterial(m, c));
	}
	const int rimMat    = addMaterial(m, { 235, 225, 205, 255 });
	const int legMat    = addMaterial(m, { 95, 70, 70, 255 });
	const int footMat   = addMaterial(m, { 60, 45, 45, 255 });
	const int stalkMat  = addMaterial(m, { 120, 190, 80, 255 });
	const int petalMat  = addMaterial(m, { 255, 215, 225, 255 });
	const int centreMat = addMaterial(m, { 255, 225, 70, 255 });
	const float bulbY   = 30.0f * k;
	addBandedEllipsoid(m, spec, 0, bulb, { 15.0f * k, 12.5f * k, 15.0f * k }, 10, 16, { 0.0f, bulbY, 0.0f });
	addBandedEllipsoid(m, spec, 0, { rimMat }, { 9.0f * k, 2.2f * k, 9.0f * k }, 5, 12, { 0.0f, bulbY - 11.5f * k, 0.0f }, false);
	// Legs: three joints (3, 6, 9 with children) at 120 degrees; each is a thin
	// upper strut under the bulb, a lower strut further out and a foot pad.
	const int legJoints[3][3] = { { 3, 4, 5 }, { 6, 7, 8 }, { 9, 10, 11 } };
	for (int i = 0; i < 3; i++) {
		const float a  = (90.0f + 120.0f * float(i)) * 3.14159265f / 180.0f;
		const float dx = std::cos(a), dz = std::sin(a);
		addBoxMesh(m, spec, legJoints[i][0], legMat, 0.9f * k, 6.0f * k, 0.9f * k, { dx * 10.0f * k, 15.0f * k, dz * 10.0f * k });
		addBoxMesh(m, spec, legJoints[i][1], legMat, 0.9f * k, 5.0f * k, 0.9f * k, { dx * 13.0f * k, 5.5f * k, dz * 13.0f * k });
		addBoxMesh(m, spec, legJoints[i][2], footMat, 2.0f * k, 0.6f * k, 2.0f * k, { dx * 13.5f * k, 0.6f * k, dz * 13.5f * k });
	}
	// Stalk and flower on joint 12.
	addBoxMesh(m, spec, 12, stalkMat, 0.7f * k, 4.0f * k, 0.7f * k, { 0.0f, bulbY + 15.5f * k, 0.0f });
	addBoxMesh(m, spec, 12, petalMat, 5.0f * k, 0.8f * k, 1.8f * k, { 0.0f, bulbY + 20.0f * k, 0.0f }, false);
	addBoxMesh(m, spec, 12, petalMat, 1.8f * k, 0.8f * k, 5.0f * k, { 0.0f, bulbY + 20.0f * k, 0.0f }, false);
	addBoxMesh(m, spec, 12, centreMat, 1.4f * k, 1.0f * k, 1.4f * k, { 0.0f, bulbY + 20.6f * k, 0.0f }, false);
}

void buildShip(Model& m, const StandInModelSpec& spec)
{
	// Dolphin: a long rounded silver hull on three short legs, a red-orange nose at
	// +z, a blue cockpit dome on top and a tail fin. Banded light to dark like the
	// onion so the hull reads as round when drawn unlit.
	const float k = spec.halfSize / 20.0f;
	std::vector<int> hull, nose;
	for (Rgba8 c : { Rgba8 { 245, 247, 252, 255 }, Rgba8 { 222, 228, 238, 255 }, Rgba8 { 196, 204, 218, 255 }, Rgba8 { 160, 168, 186, 255 },
	                 Rgba8 { 112, 120, 138, 255 } }) {
		hull.push_back(addMaterial(m, c));
	}
	for (Rgba8 c : { Rgba8 { 255, 150, 90, 255 }, Rgba8 { 245, 95, 45, 255 }, Rgba8 { 215, 60, 35, 255 }, Rgba8 { 150, 40, 25, 255 } }) {
		nose.push_back(addMaterial(m, c));
	}
	const int glassTop = addMaterial(m, { 140, 200, 255, 255 });
	const int glass    = addMaterial(m, { 50, 120, 215, 255 });
	const int dark     = addMaterial(m, { 55, 60, 75, 255 });
	const int finMat   = addMaterial(m, { 205, 60, 45, 255 });
	const float hullY  = 12.0f * k;
	addBandedEllipsoid(m, spec, 0, hull, { 13.0f * k, 8.5f * k, 24.0f * k }, 10, 18, { 0.0f, hullY, 0.0f });
	addBandedEllipsoid(m, spec, 0, nose, { 7.0f * k, 6.0f * k, 7.5f * k }, 8, 12, { 0.0f, hullY - 0.5f * k, 21.0f * k }, false);
	addBandedEllipsoid(m, spec, 0, { glassTop, glass, glass }, { 6.0f * k, 4.5f * k, 8.0f * k }, 6, 12, { 0.0f, hullY + 7.0f * k, 8.0f * k }, false);
	// Tail fin and side engine pods.
	addBoxMesh(m, spec, 0, finMat, 0.8f * k, 5.0f * k, 4.0f * k, { 0.0f, hullY + 9.0f * k, -18.0f * k }, false);
	addBandedEllipsoid(m, spec, 0, { hull[1], hull[2], hull[3] }, { 3.5f * k, 3.5f * k, 7.0f * k }, 6, 10, { 13.5f * k, hullY - 1.0f * k, -10.0f * k }, false);
	addBandedEllipsoid(m, spec, 0, { hull[1], hull[2], hull[3] }, { 3.5f * k, 3.5f * k, 7.0f * k }, 6, 10, { -13.5f * k, hullY - 1.0f * k, -10.0f * k }, false);
	// Three landing legs down to the ground.
	for (Vec3 c : { Vec3 { 8.0f * k, 2.5f * k, -10.0f * k }, Vec3 { -8.0f * k, 2.5f * k, -10.0f * k }, Vec3 { 0.0f, 2.5f * k, 14.0f * k } }) {
		addBoxMesh(m, spec, 0, dark, 0.8f * k, 2.5f * k, 0.8f * k, c, false);
		addBoxMesh(m, spec, 0, dark, 2.0f * k, 0.4f * k, 2.0f * k, { c.x, 0.4f * k, c.z }, false);
	}
}

void buildPellet(Model& m, const StandInModelSpec& spec)
{
	// Number pellet: a thick upright disc, banded yellow, with a dark "1" on the face.
	std::vector<int> shell;
	for (Rgba8 c : { Rgba8 { 255, 245, 140, 255 }, Rgba8 { 255, 220, 40, 255 }, Rgba8 { 230, 180, 20, 255 }, Rgba8 { 170, 125, 10, 255 } }) {
		shell.push_back(addMaterial(m, c));
	}
	const int mark = addMaterial(m, { 120, 70, 10, 255 });
	const float r  = spec.halfSize * 0.9f;
	addBandedEllipsoid(m, spec, 0, shell, { r, r, r * 0.55f }, 8, 14, { 0.0f, r, 0.0f });
	addBoxMesh(m, spec, 0, mark, r * 0.12f, r * 0.5f, 0.15f, { 0.0f, r, r * 0.55f }, false);
	addBoxMesh(m, spec, 0, mark, r * 0.12f, r * 0.5f, 0.15f, { 0.0f, r, -r * 0.55f }, false);
}

void buildChappy(Model& m, const StandInModelSpec& spec)
{
	const int body = addMaterial(m, spec.colour);
	const int belly = addMaterial(m, { 240, 210, 170, 255 });
	const int spot = addMaterial(m, { 120, 40, 30, 255 });
	addSphereMesh(m, spec, 0, body, 10.0f, 6, 10, { 0.0f, 0.0f, 0.0f });
	addSphereMesh(m, spec, 1, belly, 7.5f, 5, 8, { 0.0f, -1.0f, 2.0f });
	addSphereMesh(m, spec, 4, body, 6.5f, 5, 7, { 0.0f, 2.0f, 6.0f });
	addBoxMesh(m, spec, 5, spot, 1.8f, 0.5f, 0.9f, { 0.0f, 2.5f, 8.5f });
	for (int j : { 8, 11 }) addBoxMesh(m, spec, j, body, 1.8f, 2.8f, 1.8f, { 0.0f, -2.0f, 0.0f });
	(void)spec;
}

} // namespace

void artFill(Image& img, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
	for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
		img.rgba[i] = r;
		img.rgba[i + 1] = g;
		img.rgba[i + 2] = b;
		img.rgba[i + 3] = a;
	}
}

void artFillRect(Image& img, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(img.width, x1);
	y1 = std::min(img.height, y1);
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			std::uint8_t* p = img.at(x, y);
			p[0] = r;
			p[1] = g;
			p[2] = b;
			p[3] = a;
		}
	}
}

void artFillCircle(Image& img, int cx, int cy, int radius, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
	const int r2 = radius * radius;
	for (int y = cy - radius; y <= cy + radius; y++) {
		for (int x = cx - radius; x <= cx + radius; x++) {
			if (x < 0 || y < 0 || x >= img.width || y >= img.height) continue;
			const int dx = x - cx, dy = y - cy;
			if (dx * dx + dy * dy > r2) continue;
			std::uint8_t* p = img.at(x, y);
			p[0] = r;
			p[1] = g;
			p[2] = b;
			p[3] = a;
		}
	}
}

void artFillRing(Image& img, int cx, int cy, int outerR, int innerR, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
	const int o2 = outerR * outerR, i2 = innerR * innerR;
	for (int y = cy - outerR; y <= cy + outerR; y++) {
		for (int x = cx - outerR; x <= cx + outerR; x++) {
			if (x < 0 || y < 0 || x >= img.width || y >= img.height) continue;
			const int dx = x - cx, dy = y - cy;
			const int d2 = dx * dx + dy * dy;
			if (d2 > o2 || d2 < i2) continue;
			std::uint8_t* p = img.at(x, y);
			p[0] = r;
			p[1] = g;
			p[2] = b;
			p[3] = a;
		}
	}
}

void artDrawDigit(Image& img, int digit, int x, int y, int scale, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
	static const unsigned char kSeg[10][7] = {
	    { 1, 1, 1, 0, 1, 1, 1 }, { 0, 0, 1, 0, 0, 1, 0 }, { 1, 0, 1, 1, 1, 0, 1 }, { 1, 0, 1, 1, 0, 1, 1 },
	    { 0, 1, 1, 1, 0, 1, 0 }, { 1, 1, 0, 1, 0, 1, 1 }, { 1, 1, 0, 1, 1, 1, 1 }, { 1, 0, 1, 0, 0, 1, 0 },
	    { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 0, 1, 1 },
	};
	if (digit < 0 || digit > 9) return;
	const auto seg = [&](int sx0, int sy0, int sx1, int sy1) { artFillRect(img, x + sx0 * scale, y + sy0 * scale, x + sx1 * scale, y + sy1 * scale, r, g, b); };
	if (digit == 1) {
		// The seven-segment "1" is only the right-hand bar, one stroke wide and off centre, so it vanishes at menu
		// sizes. Draw it as a stem two strokes wide in the middle of the cell, with a flag and a foot.
		seg(1, 0, 3, 7);
		seg(0, 1, 1, 2);
		seg(0, 6, 4, 7);
		return;
	}
	if (kSeg[digit][0]) seg(1, 0, 3, 1);
	if (kSeg[digit][1]) seg(0, 1, 1, 3);
	if (kSeg[digit][2]) seg(3, 1, 4, 3);
	if (kSeg[digit][3]) seg(1, 3, 3, 4);
	if (kSeg[digit][4]) seg(0, 4, 1, 6);
	if (kSeg[digit][5]) seg(3, 4, 4, 6);
	if (kSeg[digit][6]) seg(1, 6, 3, 7);
}

std::vector<std::uint8_t> artBtiRgb5a3(int width, int height, const Image& img)
{
	BtiInput in;
	in.format = BTI_RGB5A3;
	in.width  = width;
	in.height = height;
	in.data   = encodeRgb5a3(img);
	std::vector<std::uint8_t> out;
	std::string error;
	if (!writeBti(in, out, error)) std::abort();
	return out;
}

Image artFlatGreyImage(int width, int height)
{
	Image img;
	img.resize(width, height);
	artFill(img, 0x80, 0x80, 0x80);
	return img;
}

std::vector<std::uint8_t> artHudTexture(const std::string& baseName)
{
	Image img;
	img.resize(64, 64);
	artFill(img, 0, 0, 0, 0);
	if (baseName == "navi" || baseName == "navi_portrait") {
		artFillCircle(img, 32, 36, 22, 240, 240, 250);
		artFillCircle(img, 32, 30, 18, 200, 220, 255, 220);
		artFillRect(img, 26, 8, 38, 16, 80, 80, 90);
	} else if (baseName.find("piki") != std::string::npos || baseName.find("bp_") == 0 || baseName.find("rp_") == 0
	           || baseName.find("yp_") == 0) {
		std::uint8_t r = 220, g = 50, b = 50;
		if (baseName.find("bp_") == 0 || baseName.find("yp_") == 0) {
			r = baseName[0] == 'y' ? 230 : 40;
			g = baseName[0] == 'y' ? 210 : 60;
			b = baseName[0] == 'y' ? 40 : 230;
		}
		artFillCircle(img, 32, 34, 18, r, g, b);
		artFillCircle(img, 32, 16, 8, 60, 180, 60);
	} else if (baseName.find("_sha64") != std::string::npos) {
		const int digit = baseName[0] >= '0' && baseName[0] <= '9' ? baseName[0] - '0' : -1;
		if (digit >= 0) artFillCircle(img, 34, 38, 12, 0, 0, 0, 70);
	} else if (baseName.find("_64") != std::string::npos || (baseName.size() == 5 && baseName[1] == '_')) {
		const int digit = baseName[0] >= '0' && baseName[0] <= '9' ? baseName[0] - '0' : -1;
		artFillRect(img, 4, 4, 60, 60, 20, 20, 30, 200);
		if (digit >= 0) artDrawDigit(img, digit, 18, 12, 4, 240, 240, 120);
	} else if (baseName.find("sun") != std::string::npos || baseName == "w08_160" || baseName == "ws08_160"
	           || baseName == "ws08_yel") {
		artFillCircle(img, 32, 32, 20, 255, 220, 80);
		artFillCircle(img, 32, 32, 14, 255, 240, 140);
	} else if (baseName == "moon") {
		artFillCircle(img, 28, 32, 18, 200, 210, 240);
		artFillCircle(img, 36, 28, 16, 20, 30, 60);
	} else if (baseName.find("dot") != std::string::npos) {
		artFillCircle(img, 32, 32, 10, 240, 240, 240);
	} else if (baseName.find("waku") != std::string::npos || baseName.find("p2b") != std::string::npos) {
		artFillRect(img, 4, 4, 60, 60, 40, 80, 140, 220);
		artFillRect(img, 10, 10, 54, 54, 0, 0, 0, 0);
	} else {
		artFill(img, 120, 120, 140);
	}
	return artBtiRgb5a3(64, 64, img);
}

const std::vector<StandInArtBinding>& standInArtBindings()
{
	// Every model is authored at its stand-in size, so no scale.
	static const std::vector<int> kFolkTintJoints = { 0, kPikiRigRightHandJoint, kPikiRigLeftHandJoint, 10, 11 };
	static const std::vector<StandInArtBinding> bindings = {
		// Pellets: one token per value (white1-4 are the 1, 5, 10 and 20 pellets).
		{ "dataDir/objects/pellets/white1.mod", "pellet_1", 1.0f },
		{ "dataDir/objects/pellets/white2.mod", "pellet_5", 1.0f },
		{ "dataDir/objects/pellets/white3.mod", "pellet_10", 1.0f },
		{ "dataDir/objects/pellets/white4.mod", "pellet_20", 1.0f },
		// The captain and the pebble folk (art/blender/folk.blend). kinModel is the
		// Puffmin: a folk body with no trait. The folk body (joint 0), arms and feet
		// take the colour the game sets on material 0 (ViewPiki::demoDraw: the type
		// colour, the idle pastel and the white damage flash); the eyes and the
		// trait (joint 1) keep their own colours. The captain's draw sets no
		// material colour, so his art keeps its own.
		{ "dataDir/pikis/nv3Model.mod", "captain", 1.0f },
		{ "dataDir/pikis/redModel.mod", "folk_red", 1.0f, kFolkTintJoints },
		{ "dataDir/pikis/yelModel.mod", "folk_yellow", 1.0f, kFolkTintJoints },
		{ "dataDir/pikis/bluModel.mod", "folk_blue", 1.0f, kFolkTintJoints },
		{ "dataDir/pikis/kinModel.mod", "folk_kin", 1.0f, kFolkTintJoints },
		// The maturity crests, drawn at the folk's joint 6 (the origin): bead, shards, star.
		{ "dataDir/pikis/happas/leaf.mod", "crest_bead", 1.0f },
		{ "dataDir/pikis/happas/bud.mod", "crest_shards", 1.0f },
		{ "dataDir/pikis/happas/flower.mod", "crest_star", 1.0f },
		// The buried folk: its body (joint 0) takes the folk colour the game sets on material 0.
		{ "dataDir/objects/pikihead/pikihead.mod", "buried_folk", 1.0f, { 0 } },
		// The home hive is one model for every type; its colour is the crystals in
		// the Onion effect shape of its type, which the game draws at the hive's
		// position at 0.1 scale (effectMgr.cpp geometryLoadInfo), hence 10.
		{ "dataDir/objects/goal/goal.mod", "hive", 1.0f },
		{ "dataDir/effects/shapes/r_goal.mod", "hive_crystals_red", 10.0f },
		{ "dataDir/effects/shapes/y_goal.mod", "hive_crystals_yellow", 10.0f },
		{ "dataDir/effects/shapes/b_goal.mod", "hive_crystals_blue", 10.0f },
		{ "dataDir/objects/ufo/ufo0705.mod", "ship", 1.0f },
		// Nectar: the falling drop and the puddle it leaves are the same beads.
		{ "dataDir/objects/water/water.mod", "nectar", 1.0f },
		{ "dataDir/objects/fl_water/fl_water.mod", "nectar_drop", 1.0f },
		{ "dataDir/objects/bomb/bomb.mod", "bomb_rock", 1.0f },
		// The first enemy (chappy's role), standing on the ground at the stand-in
		// size, so no scale. Its spec is in standInObjectModelSpecs().
		{ "dataDir/tekis/chappy/chappy.mod", "chappy", 1.0f },
	};
	return bindings;
}

bool checkStandInArtBindings(const std::vector<StandInModelSpec>& specs, std::string& error)
{
	for (const StandInArtBinding& b : standInArtBindings()) {
		const StandInModelSpec* spec = nullptr;
		for (const StandInModelSpec& s : specs) {
			if (s.path == b.path) spec = &s;
		}
		if (!spec) continue; // another spec list (objects, world) owns it
		const ArtModel* art = findArtModel(b.art);
		if (!art) {
			error = std::string(b.path) + ": no exported art named " + b.art;
			return false;
		}
		Model m = skeletonModel(*spec);
		if (!addArtModel(m, *spec, *art, b.scale, error, b.tintJoints)) return false;
	}
	return true;
}

Model buildArtStandInModel(const StandInModelSpec& spec)
{
	Model m = skeletonModel(spec);
	for (const StandInArtBinding& b : standInArtBindings()) {
		if (spec.path != b.path) continue;
		const ArtModel* art = findArtModel(b.art);
		std::string err;
		if (art && addArtModel(m, spec, *art, b.scale, err, b.tintJoints)) {
			fillEmptyJointMeshes(m, spec);
			addPlatformCollision(m, spec);
			return m;
		}
		m = skeletonModel(spec); // checkStandInArtBindings (mod_writer_test) reports why
		break;
	}
	const Kind kind = kindForPath(spec.path);
	switch (kind) {
	case Kind::Captain: buildCaptain(m, spec); break;
	case Kind::Piki:
		buildPikiBody(m, spec, spec.path.find("red") != std::string::npos, spec.path.find("yel") != std::string::npos, spec.path.find("blu") != std::string::npos);
		break;
	case Kind::Cursor:
		addBoxMesh(m, spec, 0, addMaterial(m, { 255, 255, 255, 255 }), 1.4f, 0.18f, 0.18f, { 0.0f, 0.0f, 0.0f });
		addBoxMesh(m, spec, 1, addMaterial(m, { 255, 255, 80, 255 }), 0.25f, 0.25f, 2.2f, { 0.0f, 0.0f, 1.8f });
		break;
	case Kind::Marker:
		addSphereMesh(m, spec, 0, addMaterial(m, { 255, 255, 100, 120 }), spec.halfSize, 5, 8, {});
		break;
	case Kind::Happa:
		// Drawn at the Pikmin's leaf-tip joint, which stays at its origin: lift the
		// leaf, bud or flower to the top of the stem (y 8.1 in buildPikiBody).
		if (spec.path.find("leaf") != std::string::npos) addBoxMesh(m, spec, 0, addMaterial(m, spec.colour), 0.15f, 1.0f, 0.6f, { 0.0f, 9.0f, 0.0f });
		else if (spec.path.find("bud") != std::string::npos) addSphereMesh(m, spec, 0, addMaterial(m, spec.colour), 0.75f, 4, 6, { 0.0f, 8.7f, 0.0f });
		else addBoxMesh(m, spec, 0, addMaterial(m, spec.colour), 1.0f, 0.25f, 1.0f, { 0.0f, 8.3f, 0.0f });
		break;
	case Kind::Pellet: buildPellet(m, spec); break;
	case Kind::Onion: buildOnion(m, spec); break;
	case Kind::Ship: buildShip(m, spec); break;
	case Kind::Bomb: addSphereMesh(m, spec, 0, addMaterial(m, spec.colour), spec.halfSize, 5, 7, {}); break;
	case Kind::PikiHead:
		addSphereMesh(m, spec, 0, addMaterial(m, spec.colour), 2.5f, 5, 6, { 0.0f, 1.0f, 0.0f });
		addBoxMesh(m, spec, 3, addMaterial(m, { 50, 180, 50, 255 }), 0.2f, 1.2f, 0.2f, { 0.0f, 2.5f, 0.0f });
		break;
	case Kind::Chappy: buildChappy(m, spec); break;
	default: {
		const int mat = addMaterial(m, spec.colour);
		for (int j = 0; j < spec.jointCount(); j++) {
			const float h = j == 0 ? spec.halfSize : spec.halfSize * 0.5f;
			addBoxMesh(m, spec, j, mat, h, h, h, {});
		}
		break;
	}
	}
	fillEmptyJointMeshes(m, spec);
	addPlatformCollision(m, spec);
	return m;
}

} // namespace nectar

// ---------------------------------------------------------------------------------------------
// Screen art for the stand-in menus (#99): sx_* (and control_sx) in screen.bun. Everything is
// drawn here from shapes; nothing comes from the disc.
namespace nectar {
namespace {

void sxPolygon(Image& img, std::initializer_list<std::pair<float, float>> pts, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
	std::vector<std::pair<float, float>> v(pts);
	for (int y = 0; y < img.height; y++) {
		for (int x = 0; x < img.width; x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			bool in = false;
			for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
				const auto [xi, yi] = v[i];
				const auto [xj, yj] = v[j];
				if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
			}
			if (!in) continue;
			std::uint8_t* p = img.at(x, y);
			p[0] = r, p[1] = g, p[2] = b, p[3] = a;
		}
	}
}

void sxRoundRect(Image& img, int x0, int y0, int x1, int y1, int rad, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
	artFillRect(img, x0 + rad, y0, x1 - rad, y1, r, g, b, a);
	artFillRect(img, x0, y0 + rad, x1, y1 - rad, r, g, b, a);
	for (int cy : { y0 + rad, y1 - rad - 1 })
		for (int cx : { x0 + rad, x1 - rad - 1 }) artFillCircle(img, cx, cy, rad, r, g, b, a);
}

std::uint32_t sxHash(int x, int y)
{
	std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

Image sxImage(int w, int h)
{
	Image img;
	img.resize(w, h);
	artFill(img, 0, 0, 0, 0);
	return img;
}

void sxPikmin(Image& img, int cx, int cy, int s, std::uint8_t r, std::uint8_t g, std::uint8_t b, bool flower)
{
	// Body (a capsule), eyes, a stalk and a leaf or a flower: recognisably a Pikmin at icon size.
	sxRoundRect(img, cx - s, cy - s / 2, cx + s, cy + 3 * s, s, r, g, b);
	artFillCircle(img, cx, cy, s + s / 4, r, g, b);
	artFillCircle(img, cx - s / 2, cy, s / 3 + 1, 255, 255, 255);
	artFillCircle(img, cx + s / 2, cy, s / 3 + 1, 255, 255, 255);
	artFillCircle(img, cx - s / 2, cy, s / 6 + 1, 20, 20, 20);
	artFillCircle(img, cx + s / 2, cy, s / 6 + 1, 20, 20, 20);
	artFillRect(img, cx - 1, cy - 3 * s, cx + 2, cy - s, 70, 150, 60);
	if (flower) {
		for (int i = 0; i < 5; i++) {
			const float a = i * 6.2831853f / 5;
			artFillCircle(img, cx + int(std::cos(a) * s * 0.7f), cy - 3 * s + int(std::sin(a) * s * 0.7f), s / 2, 255, 255, 255);
		}
		artFillCircle(img, cx, cy - 3 * s, s / 3 + 1, 250, 220, 60);
	} else {
		sxPolygon(img, { { float(cx), float(cy - 3 * s) }, { float(cx + s), float(cy - 4 * s) }, { float(cx + 2 * s), float(cy - 3 * s) }, { float(cx + s), float(cy - 2.4f * s) } },
		          80, 200, 70);
	}
}

void sxRocket(Image& img, int cx, int top, int s)
{
	// Hull (white), nose (red), window (blue), two fins (red) and an exhaust flame.
	sxPolygon(img, { { float(cx), float(top) }, { float(cx + s), float(top + 2 * s) }, { float(cx + s), float(top + 5 * s) }, { float(cx - s), float(top + 5 * s) },
	                 { float(cx - s), float(top + 2 * s) } },
	          235, 235, 240);
	sxPolygon(img, { { float(cx), float(top) }, { float(cx + s * 0.62f), float(top + 1.3f * s) }, { float(cx - s * 0.62f), float(top + 1.3f * s) } }, 220, 50, 40);
	artFillCircle(img, cx, top + 2 * s + s / 2, s / 2, 60, 130, 230);
	sxPolygon(img, { { float(cx - s), float(top + 3.4f * s) }, { float(cx - 2 * s), float(top + 5.6f * s) }, { float(cx - s), float(top + 5 * s) } }, 220, 50, 40);
	sxPolygon(img, { { float(cx + s), float(top + 3.4f * s) }, { float(cx + 2 * s), float(top + 5.6f * s) }, { float(cx + s), float(top + 5 * s) } }, 220, 50, 40);
	sxPolygon(img, { { float(cx - s * 0.6f), float(top + 5 * s) }, { float(cx + s * 0.6f), float(top + 5 * s) }, { float(cx), float(top + 6.6f * s) } }, 255, 170, 40);
}

void sxOnion(Image& img, int cx, int cy, int s, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
	for (int dx : { -s, 0, s }) sxPolygon(img, { { float(cx + dx * 0.5f), float(cy) }, { float(cx + dx * 1.2f - 1), float(cy + 1.6f * s) }, { float(cx + dx * 1.2f + 2), float(cy + 1.6f * s) } }, 120, 100, 70);
	artFillCircle(img, cx, cy, s, r, g, b);
	artFillCircle(img, cx - s / 3, cy - s / 3, s / 3, std::uint8_t(std::min(255, r + 70)), std::uint8_t(std::min(255, g + 70)), std::uint8_t(std::min(255, b + 70)));
	artFillRect(img, cx - 1, cy - s - s / 2, cx + 2, cy - s + 1, 90, 160, 70);
}

} // namespace

bool artIsScreenTexture(const std::string& stem) { return stem.rfind("sx_", 0) == 0 || stem == "control_sx"; }

std::vector<std::uint8_t> artScreenTexture(const std::string& stem)
{
	Image img;
	if (stem == "sx_panel") {
		// 128 square so the border stays thin when a window stretches it to a few hundred pixels.
		img = sxImage(128, 128);
		sxRoundRect(img, 0, 0, 128, 128, 14, 150, 200, 255, 230);
		sxRoundRect(img, 3, 3, 125, 125, 12, 18, 36, 78, 215);
		artFillRect(img, 12, 7, 116, 10, 60, 100, 170, 200);
	} else if (stem == "sx_black") {
		img = sxImage(8, 8);
		artFill(img, 0, 0, 0, 255);
	} else if (stem == "sx_arrow_l" || stem == "sx_arrow_r") {
		img = sxImage(64, 64);
		const bool left = stem == "sx_arrow_l";
		auto X = [&](float x) { return left ? x : 64 - x; };
		sxPolygon(img, { { X(4), 32 }, { X(52), 2 }, { X(52), 62 } }, 90, 50, 0);
		sxPolygon(img, { { X(10), 32 }, { X(48), 9 }, { X(48), 55 } }, 255, 200, 40);
		sxPolygon(img, { { X(14), 30 }, { X(44), 12 }, { X(44), 28 } }, 255, 240, 150);
	} else if (stem == "sx_btn") {
		img = sxImage(32, 32);
		artFillCircle(img, 16, 16, 15, 90, 90, 100);
		artFillCircle(img, 16, 16, 13, 255, 255, 255);
		artFillCircle(img, 12, 11, 4, 255, 255, 255);
	} else if (stem == "sx_stick") {
		img = sxImage(32, 32);
		artFillRing(img, 16, 16, 15, 11, 90, 90, 100);
		artFillCircle(img, 16, 16, 10, 255, 255, 255);
		artFillRing(img, 16, 16, 7, 5, 170, 170, 180);
	} else if (stem == "sx_bar") {
		img = sxImage(64, 16);
		sxRoundRect(img, 0, 0, 64, 16, 7, 90, 90, 100);
		sxRoundRect(img, 2, 2, 62, 14, 5, 255, 255, 255);
	} else if (stem == "control_sx") {
		// A GameCube pad, front view: indigo body with two grips, the grey stick on the left,
		// the yellow C stick lower right and the D-pad lower left. The face buttons, START and
		// the shoulders are the layout's own button pictures drawn over it.
		img = sxImage(128, 80);
		sxRoundRect(img, 14, 4, 114, 46, 18, 80, 70, 160);
		artFillCircle(img, 28, 44, 24, 80, 70, 160);
		artFillCircle(img, 100, 44, 24, 80, 70, 160);
		artFillCircle(img, 26, 62, 16, 70, 60, 145);
		artFillCircle(img, 102, 62, 16, 70, 60, 145);
		artFillCircle(img, 30, 26, 12, 50, 45, 100);
		artFillCircle(img, 30, 26, 8, 170, 170, 180);
		artFillCircle(img, 82, 50, 9, 50, 45, 100);
		artFillCircle(img, 82, 50, 6, 240, 210, 50);
		artFillRect(img, 42, 44, 56, 50, 40, 40, 60);
		artFillRect(img, 46, 40, 52, 54, 40, 40, 60);
	} else if (stem == "sx_radar") {
		// An area map seen from above: grass with mottling, darker forest, a pond, dirt paths
		// and a cliff rim, with a faint grid.
		img = sxImage(128, 128);
		for (int y = 0; y < 128; y++) {
			for (int x = 0; x < 128; x++) {
				const std::uint32_t h = sxHash(x / 4, y / 4);
				const int dx = x - 64, dy = y - 64;
				const int d2 = dx * dx + dy * dy;
				std::uint8_t* p = img.at(x, y);
				int r = 70, g = 140 + int(h % 24), b = 60;
				if (d2 > 56 * 56) r = 40, g = 80 + int(h % 16), b = 40;           // forest edge
				if (d2 > 62 * 62) r = 70, g = 60, b = 45;                         // cliff rim
				if ((x - 88) * (x - 88) + (y - 40) * (y - 40) < 15 * 15) r = 50, g = 110, b = 210; // pond
				const int path1 = std::abs(y - (64 + (x - 64) / 3)), path2 = std::abs(x - (60 - (y - 64) / 4));
				if ((path1 < 3 || path2 < 3) && d2 < 56 * 56) r = 190, g = 160, b = 100;
				if ((x % 32 == 0 || y % 32 == 0) && d2 < 62 * 62) r = r * 3 / 4, g = g * 3 / 4, b = b * 3 / 4;
				p[0] = std::uint8_t(r), p[1] = std::uint8_t(g), p[2] = std::uint8_t(b), p[3] = 255;
			}
		}
	} else if (stem == "sx_rocket") {
		img = sxImage(64, 64);
		sxRocket(img, 32, 4, 7);
	} else if (stem == "sx_olimar") {
		img = sxImage(32, 32);
		sxPolygon(img, { { 16, 1 }, { 30, 30 }, { 16, 23 }, { 2, 30 } }, 255, 255, 255);
		sxPolygon(img, { { 16, 5 }, { 26, 27 }, { 16, 21 }, { 6, 27 } }, 255, 200, 30);
	} else if (stem == "sx_onion_r" || stem == "sx_onion_y" || stem == "sx_onion_b") {
		img = sxImage(32, 32);
		const char c = stem.back();
		sxOnion(img, 16, 14, 10, c == 'r' ? 220 : c == 'y' ? 235 : 50, c == 'r' ? 50 : c == 'y' ? 205 : 80, c == 'b' ? 230 : 40);
	} else if (stem == "sx_seed") {
		img = sxImage(16, 16);
		artFillCircle(img, 8, 8, 6, 255, 255, 255);
	} else if (stem == "sx_part") {
		img = sxImage(32, 32);
		for (int i = 0; i < 8; i++) {
			const float a = i * 0.785398f;
			artFillCircle(img, 16 + int(std::cos(a) * 11), 16 + int(std::sin(a) * 11), 4, 230, 180, 40);
		}
		artFillCircle(img, 16, 16, 11, 230, 180, 40);
		artFillCircle(img, 16, 16, 5, 120, 80, 20);
	} else if (stem == "sx_compass") {
		img = sxImage(64, 64);
		artFillCircle(img, 32, 32, 30, 20, 30, 60, 200);
		artFillRing(img, 32, 32, 30, 27, 220, 220, 230);
		sxPolygon(img, { { 32, 6 }, { 41, 32 }, { 23, 32 } }, 230, 50, 40);
		sxPolygon(img, { { 32, 58 }, { 41, 32 }, { 23, 32 } }, 230, 230, 235);
	} else if (stem == "sx_pikmin_r" || stem == "sx_pikmin_y" || stem == "sx_pikmin_b") {
		img = sxImage(32, 64);
		const char c = stem.back();
		sxPikmin(img, 16, 34, 7, c == 'r' ? 225 : c == 'y' ? 240 : 50, c == 'r' ? 45 : c == 'y' ? 210 : 90, c == 'b' ? 235 : 40, c != 'r');
	} else if (stem == "sx_space") {
		// The world map's sky (w_back): deep navy darkening towards the top, with scattered stars.
		img = sxImage(256, 256);
		for (int y = 0; y < 256; y++) {
			for (int x = 0; x < 256; x++) {
				std::uint8_t* p = img.at(x, y);
				p[0] = std::uint8_t(4 + y / 24), p[1] = std::uint8_t(6 + y / 16), p[2] = std::uint8_t(28 + y / 5), p[3] = 255;
				const std::uint32_t h = sxHash(x, y);
				if (h % 233 == 0) p[0] = p[1] = p[2] = std::uint8_t(170 + h % 80);
			}
		}
	} else if (stem == "sx_planet") {
		// The planet seen from orbit (w_back): green land mottled with forest, brown highlands and
		// blue lakes, lit from the upper left, inside a pale blue rim of atmosphere.
		img = sxImage(256, 256);
		const int c = 128, rad = 120;
		auto noise = [](int x, int y, float cell, std::uint32_t seed) {
			const float fx = x / cell, fy = y / cell;
			const int ix = int(fx), iy = int(fy);
			const float tx = fx - ix, ty = fy - iy;
			auto v = [&](int i, int j) { return float(sxHash(i + int(seed) * 131, j + int(seed) * 71) % 1024) / 1023.0f; };
			const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
			const float top = v(ix, iy) + (v(ix + 1, iy) - v(ix, iy)) * sx;
			const float bot = v(ix, iy + 1) + (v(ix + 1, iy + 1) - v(ix, iy + 1)) * sx;
			return top + (bot - top) * sy;
		};
		for (int y = 0; y < 256; y++) {
			for (int x = 0; x < 256; x++) {
				const int dx = x - c, dy = y - c, d2 = dx * dx + dy * dy;
				std::uint8_t* p = img.at(x, y);
				if (d2 > (rad + 6) * (rad + 6)) continue;
				if (d2 > rad * rad) {
					p[0] = 120, p[1] = 190, p[2] = 255, p[3] = 150;
					continue;
				}
				// Smooth value noise (two octaves) instead of hard cells, so the land has soft coasts.
				const float n = 0.65f * noise(x, y, 40.0f, 3) + 0.35f * noise(x, y, 14.0f, 9);
				const float f = noise(x, y, 22.0f, 17);
				int r = 84, g = 150 + int(30.0f * f), b = 62;
				if (f > 0.62f) r = 52, g = 108, b = 48;                    // forest
				if (n > 0.66f) r = 150, g = 126, b = 80;                   // highland
				if (n < 0.30f) r = 56, g = 118, b = 200;                   // lake
				const float light = 1.15f - 0.55f * float(dx + dy + 2 * rad) / float(4 * rad);
				p[0] = std::uint8_t(std::min(255.0f, r * light));
				p[1] = std::uint8_t(std::min(255.0f, g * light));
				p[2] = std::uint8_t(std::min(255.0f, b * light));
				p[3] = 255;
			}
		}
	} else if (stem == "sx_pin" || stem == "sx_pin_s") {
		// A course marker: a map pin, white stem and head (red unselected, gold selected).
		img = sxImage(32, 48);
		const bool sel = stem == "sx_pin_s";
		sxPolygon(img, { { 11, 20 }, { 21, 20 }, { 16, 47 } }, 240, 240, 245);
		artFillCircle(img, 16, 16, 15, 40, 20, 10);
		artFillCircle(img, 16, 16, 13, sel ? 255 : 220, sel ? 205 : 50, sel ? 40 : 40);
		artFillCircle(img, 12, 11, 4, 255, 255, 255);
	} else if (stem == "sx_ring") {
		// An open course's landing ring.
		img = sxImage(64, 32);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 64; x++) {
				const float ex = (x - 31.5f) / 31.0f, ey = (y - 15.5f) / 15.0f, e = ex * ex + ey * ey;
				if (e <= 1.0f && e >= 0.55f) {
					std::uint8_t* p = img.at(x, y);
					p[0] = 120, p[1] = 255, p[2] = 140, p[3] = 220;
				}
			}
	} else if (stem == "sx_halo") {
		// The selected course's glow (w_line).
		img = sxImage(64, 64);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++) {
				const float dx = x - 31.5f, dy = y - 31.5f, d = std::sqrt(dx * dx + dy * dy);
				if (d > 31.0f) continue;
				std::uint8_t* p = img.at(x, y);
				p[0] = 255, p[1] = 220, p[2] = 80, p[3] = std::uint8_t(d > 26.0f ? 230 : 90 * d / 26.0f);
			}
	} else if (stem == "sx_part_n") {
		// A ship part not yet found: a grey outline of sx_part.
		img = sxImage(32, 32);
		for (int i = 0; i < 8; i++) {
			const float a = i * 0.785398f;
			artFillCircle(img, 16 + int(std::cos(a) * 11), 16 + int(std::sin(a) * 11), 4, 110, 110, 120);
		}
		artFillCircle(img, 16, 16, 11, 110, 110, 120);
		artFillCircle(img, 16, 16, 8, 40, 40, 50);
	} else {
		img = sxImage(64, 64);
		artFill(img, 255, 0, 255);
	}
	return artBtiRgb5a3(img.width, img.height, img);
}

} // namespace nectar
