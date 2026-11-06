#include "nectar_assets/standin_sandbox.h"

#include "nectar_assets/anim.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod_writer.h"

#include <algorithm>
#include <cstdio>

namespace nectar {

namespace {

// ── The gate's shape (ours) ──────────────────────────────────────────────────
// Local space: the wall spans x, faces +-z, stands on y = 0. The panel is the
// part the Pikmin attack ('gate'); the posts at its ends ('side') are solid but
// not attackable. Sizes are round numbers of the same order as a Forest of
// Hope gate as seen in play (a wall a few captains high, wider than the squad),
// not measured from the disc model. The panel's half-width (105, a 210-unit
// wall) was chosen with the placement below: a wider panel moves where the
// Pikmin work it away from the captain, which sets how far the first one walks
// back to his hand (standin-k-investigation.md).
constexpr float kPanelHalfWidth = 111.0f;
constexpr float kPanelHeight    = 90.0f;
constexpr float kPanelHalfDepth = 8.0f;
constexpr float kPostWidth      = 20.0f;
constexpr float kPostHeight     = 110.0f;
constexpr float kPostHalfDepth  = 18.0f;
/// The 'flag' sphere: BuildingItem::getBoundingSphereRadius (culling).
constexpr float kBoundRadius = 120.0f;

constexpr const char* kGateModel     = "objects/gate/gt_hard.mod";
constexpr const char* kPanelPlatform = "objects/gate/centcoll.mod";
constexpr const char* kPostPlatform  = "objects/gate/sidecoll.mod";
constexpr const char* kGateAnimTable  = "objects/gate/gate.bin";
constexpr const char* kGateAnimBundle = "objects/gate/gt_hard.anm";
constexpr const char* kGateAnimDir    = "gate";
constexpr int kGateAnimCount          = 6;
constexpr int kGateAnimFrames         = 30;

/// Collision code of the gate's triangles: rock (attribute bits 31-29 = 1), as
/// nectar_mapcore gives its walls (kMapCodeRock).
constexpr std::uint32_t kRockCode = 0x20000000u;

struct Box {
	Vec3 lo, hi;
};

std::vector<Box> panelBoxes() { return { { { -kPanelHalfWidth, 0.0f, -kPanelHalfDepth }, { kPanelHalfWidth, kPanelHeight, kPanelHalfDepth } } }; }

std::vector<Box> postBoxes()
{
	return {
		{ { -kPanelHalfWidth - kPostWidth, 0.0f, -kPostHalfDepth }, { -kPanelHalfWidth, kPostHeight, kPostHalfDepth } },
		{ { kPanelHalfWidth, 0.0f, -kPostHalfDepth }, { kPanelHalfWidth + kPostWidth, kPostHeight, kPostHalfDepth } },
	};
}

const Vec3 kFaceNormal[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
// Corners: bit 0 = x, 1 = y, 2 = z. Face 3 is the bottom.
const int kFaces[6][4] = { { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };

/// The two triangles of a face of the box whose 8 corners start at `base`,
/// wound so that (C - A) x (B - A) points along the face normal (the game's
/// rule for drawing and for collision planes).
void faceTris(const Model& m, int base, int f, int (&out)[2][3])
{
	const int* q        = kFaces[f];
	const int tri[2][3] = { { q[0], q[1], q[2] }, { q[0], q[2], q[3] } };
	for (int t = 0; t < 2; t++) {
		const Vec3 &a = m.positions[std::size_t(base + tri[t][0])], &b = m.positions[std::size_t(base + tri[t][1])],
		           &c = m.positions[std::size_t(base + tri[t][2])];
		const Vec3 u { c.x - a.x, c.y - a.y, c.z - a.z }, v { b.x - a.x, b.y - a.y, b.z - a.z };
		const Vec3 n { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
		const Vec3& want = kFaceNormal[f];
		const bool flip  = n.x * want.x + n.y * want.y + n.z * want.z < 0.0f;
		out[t][0]        = base + tri[t][0];
		out[t][1]        = base + tri[t][flip ? 2 : 1];
		out[t][2]        = base + tri[t][flip ? 1 : 2];
	}
}

void startModel(Model& m)
{
	m.normals.assign(kFaceNormal, kFaceNormal + 6);
	m.uvs[0].push_back(Vec2 {}); // UV 0 as writeMeshMod and the stand-in models write it
}

/// One mesh on joint `joint` made of `boxes`, with its own material and vertex
/// matrix. With `collide`, every face but the bottom also becomes collision.
void addMesh(Model& m, int joint, const std::vector<Box>& boxes, Rgba8 colour, bool collide)
{
	ModMtxGroup group;
	group.deps = { int(m.vtxMatrices.size()) };
	m.vtxMatrices.push_back({ true, joint });
	for (const Box& b : boxes) {
		const int base = int(m.positions.size());
		for (int c = 0; c < 8; c++) m.positions.push_back({ (c & 1) ? b.hi.x : b.lo.x, (c & 2) ? b.hi.y : b.lo.y, (c & 4) ? b.hi.z : b.lo.z });
		for (int f = 0; f < 6; f++) {
			int tris[2][3];
			faceTris(m, base, f, tris);
			for (const auto& t : tris) {
				ModTriangle tr;
				for (int k = 0; k < 3; k++) {
					tr.v[k].mtxSlot = 0;
					tr.v[k].pos     = t[k];
					tr.v[k].nrm     = f;
					tr.v[k].uv[0]   = 0;
				}
				group.tris.push_back(tr);
				if (collide && f != 3) {
					ModCollTri ct;
					ct.code = kRockCode;
					for (int k = 0; k < 3; k++) ct.v[k] = t[k];
					m.collision.push_back(ct);
				}
			}
		}
	}
	const int meshIndex = int(m.meshes.size());
	ModMesh mesh;
	mesh.parentJoint = joint;
	mesh.flags       = 1u | (1u << 3); // position matrix and UV 0, as writeMeshMod
	mesh.groups.push_back(std::move(group));
	m.meshes.push_back(std::move(mesh));

	ModMaterial mat;
	mat.colour = colour;
	m.materials.push_back(mat);
	m.joints[std::size_t(joint)].matPolys.push_back({ int(m.materials.size()) - 1, meshIndex });

	ModJoint& j = m.joints[std::size_t(joint)];
	for (const Box& b : boxes) {
		j.boundsMin = { std::min(j.boundsMin.x, b.lo.x), std::min(j.boundsMin.y, b.lo.y), std::min(j.boundsMin.z, b.lo.z) };
		j.boundsMax = { std::max(j.boundsMax.x, b.hi.x), std::max(j.boundsMax.y, b.hi.y), std::max(j.boundsMax.z, b.hi.z) };
	}
}

ModJoint joint(int parent)
{
	ModJoint j;
	j.parent = parent;
	j.scale  = { 1, 1, 1 };
	return j;
}

const Rgba8 kPostColour { 120, 110, 100, 255 }, kPanelColour { 90, 90, 110, 255 };

} // namespace

SandboxGatePlacement sandboxGatePlacement()
{
	// The disc's throw_release trace (Forest of Hope, 30 Hz, temporary tracing,
	// standin-k-investigation.md) has the captain walk about 185 units straight,
	// slide off a reinforced gate and stop against it about 220 units from the
	// start; that gate's generator stands 203 units ahead of him and 34 units to
	// his left, turned 35 degrees. The sandbox gate takes that relation (a
	// gameplay layout, not geometry: our wall has its own size), moved 6 units
	// closer and turned. Re-fitted for #92 (GameCube RNG draw order): disc K is 13 /
	// 22 at 30 / 60 Hz; the layout below matches on the stand-in tree (42 degrees,
	// z -207, x -31.5, 222-unit panel, depth 16). K is a chaotic outcome of where
	// each Pikmin stands when it gives up, so this is a chosen point, not a plateau
	// (standin-k-investigation.md).
	SandboxGatePlacement p;
	p.x    = -31.5f;
	p.z    = -207.0f;
	p.rotY = 42;
	return p;
}

Model buildSandboxGateModel()
{
	Model m;
	startModel(m);
	m.joints = { joint(-1), joint(0) };
	m.joints[0].boundsMin = m.joints[1].boundsMin = { 1e9f, 1e9f, 1e9f };
	m.joints[0].boundsMax = m.joints[1].boundsMax = { -1e9f, -1e9f, -1e9f };
	addMesh(m, 0, postBoxes(), kPostColour, false);
	addMesh(m, 1, panelBoxes(), kPanelColour, false);

	// Collinfo grammar of ObjCollInfo::loadini (shpObjColl.cpp): type 2 is a
	// platform (a .mod with collision, loaded by path), type 1 a sphere. The panel
	// hangs from joint 1, which SluiceAI's break motions would lower; the bomb gate
	// never plays them. BuildingItem::getBoundingSphereRadius reads 'flag'; the
	// Pikmin react to the 'gate' part (Piki::collisionCallback, ActBreakWall).
	char buf[512];
	std::snprintf(buf, sizeof(buf),
	              "// Collision info for the sandbox gate (nectar_standin_generator)\n"
	              "collinfo 1 {\n\tid\t\tgate\n\tcode\t\tnone\n\ttype\t2\n\tplatform\t%s\n\t}\n\n"
	              "collinfo 0 {\n\tid\t\tside\n\tcode\t\tnone\n\ttype\t2\n\tplatform\t%s\n\t}\n\n"
	              "collinfo 0 {\n\tid\t\tflag\n\tcode\t\tnone\n\ttype\t1\n\tradius\t%f\n\tcentre\t0.000000 0.000000 0.000000\n\t}\n\n",
	              kPanelPlatform, kPostPlatform, double(kBoundRadius));
	m.trailingIni = buf;
	return m;
}

Model buildSandboxGatePlatform(bool panel)
{
	Model m;
	startModel(m);
	m.joints              = { joint(-1) };
	m.joints[0].boundsMin = { 1e9f, 1e9f, 1e9f };
	m.joints[0].boundsMax = { -1e9f, -1e9f, -1e9f };
	addMesh(m, 0, panel ? panelBoxes() : postBoxes(), panel ? kPanelColour : kPostColour, true);
	return m;
}

bool writeSandboxGateFiles(std::vector<SandboxFile>& out, std::string& error)
{
	struct Item {
		const char* path;
		Model model;
		bool platform;
	};
	const Item items[] = {
		{ kGateModel, buildSandboxGateModel(), false },
		{ kPanelPlatform, buildSandboxGatePlatform(true), true },
		{ kPostPlatform, buildSandboxGatePlatform(false), true },
	};
	for (const Item& it : items) {
		SandboxFile f;
		f.path = std::string("dataDir/") + it.path;
		std::string err;
		const bool ok = it.platform ? writePlatformMod(it.model, f.bytes, err) : writeMod(it.model, f.bytes, err);
		if (!ok) {
			error = f.path + ": " + err;
			return false;
		}
		out.push_back(std::move(f));
	}

	// Animations: ItemShapeObject("gate", "gate.bin") loads the AnimMgr table
	// objects/gate/gate.bin and the bundle the game derives from the model name
	// (gt_hard.anm). BuildingItem::startAI starts motion 0 and stops it; without
	// a motion BaseShape panics ("no joint anim!!"). SluiceAI plays motion n on
	// each broken stage and n + 3 on a damage hit (itemAI.cpp), so with three
	// stages it can reach 0 to 5. A bomb gate never takes damage from Pikmin,
	// so none of them plays in a scenario: each is a static pose of the two
	// joints, kGateAnimFrames long.
	AnimMgrTable table;
	table.baseDir = kGateAnimDir;
	std::vector<BundleEntry> entries;
	const std::vector<int> parents = { -1, 0 };
	for (int i = 0; i < kGateAnimCount; i++) {
		AnimInfoSpec info;
		info.name     = "gate" + std::to_string(i);
		info.animKeys = { 0, kGateAnimFrames - 1 };
		BundleEntry e;
		e.type = BUNDLE_DCK;
		e.name = std::string(kGateAnimDir) + "/" + info.name;
		table.infos.push_back(std::move(info));
		if (!writeDckPayload(makeStaticAnim(parents, kGateAnimFrames), e.payload, error)) {
			error = "gate animation " + e.name + ": " + error;
			return false;
		}
		entries.push_back(std::move(e));
	}
	SandboxFile tableFile, bundleFile;
	tableFile.path  = std::string("dataDir/") + kGateAnimTable;
	bundleFile.path = std::string("dataDir/") + kGateAnimBundle;
	if (!writeAnimMgrTable(table, tableFile.bytes, error) || !writeBundle(entries, bundleFile.bytes, error)) {
		error = "gate animations: " + error;
		return false;
	}
	out.push_back(std::move(tableFile));
	out.push_back(std::move(bundleFile));
	return true;
}

bool writeSandboxDefaultGen(const std::string& genDir, SandboxFile& out, std::string& error)
{
	const SandboxGatePlacement p = sandboxGatePlacement();
	GenItemSpec gate;
	gate.memo = "sandbox gate";
	gate.pos  = { p.x, 0.0f, p.z }; // BuildingItem::startAI drops it onto the floor
	gate.item = "sluice bomb";
	// Health (hour + day * hoursInDay) does not matter: a bomb gate ignores Pikmin
	// hits. Three stages, the GenObjectItem default.
	gate.day    = 0;
	gate.hour   = 10;
	gate.stages = 3;
	gate.rotY   = p.rotY;
	std::vector<std::uint8_t> raw;
	if (!writeItemGenerator(gate, raw, error)) {
		error = "sandbox gate generator: " + error;
		return false;
	}
	out.path  = "dataDir/" + genDir + "default.gen";
	out.bytes = writeGen({ raw });
	return true;
}

} // namespace nectar
