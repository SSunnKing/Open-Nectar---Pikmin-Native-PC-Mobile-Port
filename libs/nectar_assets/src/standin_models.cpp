#include "nectar_assets/standin_models.h"

#include "nectar_assets/mod_writer.h"
#include "nectar_assets/piki_rig.h"
#include "nectar_assets/standin_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nectar {

namespace {

// ── Collision parts (collinfo text) ─────────────────────────────────────────
//
// Grammar of ObjCollInfo::loadini and BaseShape::importIni (shpObjColl.cpp:124,
// shapeBase.cpp:1901): "collinfo <joint> {" holding id, code, type 1, radius,
// centre x y z and nested collinfo blocks.
//
// Every joint table and sphere below is authored for these fixtures. Joint
// counts are the smallest the game source indexes (cited per model); parents are
// a flat fan under the root, because the stand-in animations are static identity
// poses and the game never reads parents back. Sphere ids are the ones the game
// looks up; their joints, radii and centres are placed on the procedural art
// (standin_art.cpp), with every joint at the model origin.

struct CollPart {
	int joint;
	const char* id;
	float radius;
	Vec3 centre;
	std::vector<CollPart> kids = {};
};

void collText(std::string& out, const CollPart& p, int depth)
{
	const std::string ind(std::size_t(depth), '\t');
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%scollinfo %d {\n", ind.c_str(), p.joint);
	out += buf;
	std::snprintf(buf, sizeof(buf), "%s\tid\t\t%s\n%s\tcode\t\tnone\n%s\ttype\t1\n%s\tradius\t%f\n%s\tcentre\t%f %f %f\n", ind.c_str(), p.id, ind.c_str(),
	              ind.c_str(), ind.c_str(), double(p.radius), ind.c_str(), double(p.centre.x), double(p.centre.y), double(p.centre.z));
	out += buf;
	for (const CollPart& k : p.kids) {
		out += "\n";
		collText(out, k, depth + 1);
	}
	out += ind + "\t}\n";
}

std::string collInfo(const std::vector<CollPart>& parts)
{
	std::string out;
	for (const CollPart& p : parts) {
		collText(out, p, 0);
		out += "\n";
	}
	return out;
}

/// One 'cent' sphere on joint 0 (pellets, ship parts, bomb).
std::string centOnly(float radius, float cy)
{
	return collInfo({ { 0, "cent", radius, { 0.0f, cy, 0.0f } } });
}

/// The root and `joints - 1` children of the root.
std::vector<int> flatRig(int joints)
{
	std::vector<int> p(std::size_t(joints), 0);
	p[0] = -1;
	return p;
}

std::vector<int> pikiRig()
{
	return std::vector<int>(kPikiRigParents, kPikiRigParents + kPikiRigJointCount);
}

// The Onion (goal.mod). 13 joints: the art draws the bulb on 0, the legs on 3-11
// (three per leg) and the stalk on 12, and GoalItem reads these spheres without a
// null check (goalItem.cpp:106-333, aiEnter.cpp:99-101, naviState.cpp:624,1786).
constexpr int kOnionJoints = 13;
// Authored geometry, the art's own: the bulb is centred 30 up with radius 15, so
// its 'cent' leaves room to walk under it. Each leg is a rope (GoalItem::startAI
// strings it from 'legN' down to its child 'basN'): the top sits under the bulb
// 10 out and 12 up, the foot on the ground 20 out, at 90, 210 and 330 degrees like
// the art's legs. 'cont' (where the captain opens the menu) is the ground under
// the bulb, 'piki' (where seeds and the flow effect come out) the flower 50 up,
// 'llit' the underside light, and 'effN' puffs at the feet.
constexpr float kOnionLegTopY = 12.0f, kOnionLegTopR = 10.0f, kOnionFootR = 20.0f;
constexpr float kCos210 = -0.8660254f, kSin210 = -0.5f;

CollPart onionLeg(const char* leg, const char* foot, int joint, float dx, float dz)
{
	return { joint, leg, 5.0f, { dx * kOnionLegTopR, kOnionLegTopY, dz * kOnionLegTopR }, { { joint, foot, 5.0f, { dx * kOnionFootR, 0.0f, dz * kOnionFootR } } } };
}

std::vector<StandInModelSpec> buildSpecs()
{
	std::vector<StandInModelSpec> s;
	auto add = [&](const char* path, std::vector<int> parents, Rgba8 colour, float half, std::string coll = std::string()) {
		StandInModelSpec m;
		m.path     = std::string("dataDir/") + path;
		m.parents  = std::move(parents);
		m.colour   = colour;
		m.halfSize = half;
		m.collInfo = std::move(coll);
		s.push_back(std::move(m));
	};
	const Rgba8 grey { 160, 160, 160, 255 }, white { 240, 240, 240, 255 }, red { 255, 45, 50, 255 }, blue { 55, 95, 255, 255 },
	    yellow { 255, 215, 35, 255 };

	// gameSetup.cpp shapeList: the number pellets. One joint (their pelAnim
	// override joint is -1). Pellet::getBottomRadius needs a 'cent' sphere
	// (pelletMgr.cpp:504-529); the radii are round steps by pellet size, each
	// sphere resting on the ground.
	add("objects/pellets/white1.mod", { -1 }, white, 5.0f, centOnly(5.0f, 5.0f));
	add("objects/pellets/white2.mod", { -1 }, white, 10.0f, centOnly(10.0f, 10.0f));
	add("objects/pellets/white3.mod", { -1 }, white, 17.0f, centOnly(15.0f, 15.0f));
	add("objects/pellets/white4.mod", { -1 }, white, 24.0f, centOnly(20.0f, 20.0f));

	// The four Pikmin (kinModel is the Puffmin) share the captain's rig. 'cent' is
	// optional for creatures (Creature::getCentre falls back to the position);
	// this one covers the art's body, which stands about 6 tall.
	const std::string pikiColl = collInfo({ { 0, "cent", 3.0f, { 0.0f, 3.0f, 0.0f } } });
	add("pikis/bluModel.mod", pikiRig(), blue, 3.0f, pikiColl);
	add("pikis/redModel.mod", pikiRig(), red, 3.0f, pikiColl);
	add("pikis/yelModel.mod", pikiRig(), yellow, 3.0f, pikiColl);
	add("pikis/kinModel.mod", pikiRig(), white, 3.0f, pikiColl);

	// Captain. 'rhnd' is required: PikiGoHangState::exec (pikiState.cpp:1703) and
	// NaviThrowWaitState::lockHangPiki (naviState.cpp:2214) dereference it
	// unchecked; the held Pikmin is placed 10 below its centre. It hangs from the
	// right-hand joint, over the art's hand (x 2), 10 above the hand top so the
	// held Pikmin's feet are level with the hand. 'cent' covers the art's body
	// (about 10 tall) and 'ante' marks the antenna tip (navi.cpp:4296); both are
	// looked up with a null check.
	add("pikis/nv3Model.mod", pikiRig(), white, 6.0f,
	    collInfo({
	        { 0, "cent", 5.0f, { 0.0f, 5.0f, 0.0f } },
	        { kPikiRigRightHandJoint, "rhnd", 1.0f, { 2.0f, 12.0f, 0.0f } },
	        { kPikiRigLeafTipJoint, "ante", 1.0f, { 0.0f, 10.4f, 0.0f } },
	    }));

	// gameSetup.cpp shapeList2: the ship, kStandInShipMinJoints joints (the most
	// UfoShapeObject and UfoItem index). UfoItem reads these spheres without a null
	// check (ufoItem.cpp:128,156,341,487-508,739). Placed on the art's hull (24
	// long, centred 12 up, nose at +z): 'gol1' is the delivery spot 30 ahead of the
	// nose, 'suc0'/'suc1' the hull centre parts are drawn into, 'eng1-4' the ends
	// of the two side pods and 'gcen' the tail. 'cent' is the body sphere every
	// creature's collision size comes from (Creature::getCentreSize, creature.cpp:281;
	// without it UfoItem::getSize gives 10, smaller than the hull): the hull's half-length.
	add("objects/ufo/ufo0705.mod", flatRig(kStandInShipMinJoints), { 150, 160, 180, 255 }, 20.0f,
	    collInfo({
	        { 0, "cent", 24.0f, { 0.0f, 12.0f, 0.0f } },
	        { 0, "gol1", 15.0f, { 0.0f, 0.0f, 60.0f } },
	        { 0, "suc0", 10.0f, { 0.0f, 12.0f, 0.0f } },
	        { 0, "suc1", 10.0f, { 0.0f, 12.0f, 0.0f } },
	        { 0, "eng1", 4.0f, { 13.5f, 11.0f, -3.0f } },
	        { 0, "eng2", 4.0f, { 13.5f, 11.0f, -17.0f } },
	        { 0, "eng3", 4.0f, { -13.5f, 11.0f, -3.0f } },
	        { 0, "eng4", 4.0f, { -13.5f, 11.0f, -17.0f } },
	        { 0, "gcen", 10.0f, { 0.0f, 12.0f, -24.0f } },
	    }));

	// The 16 ship parts. Nothing indexes their joints (the repair index in
	// parms/pelMgr.bin indexes the ship, playerState.cpp:1402) and their pelAnim
	// override joints are -1, so one joint each. A 10-unit 'cent' around the
	// art's 8-unit box.
	for (const char* name : { "soto1", "soto2", "soto3", "soto4", "soto5" }) {
		add((std::string("objects/ufoparts/") + name + ".mod").c_str(), { -1 }, grey, 8.0f, centOnly(10.0f, 0.0f));
	}
	for (const char* name : { "fuzoku1", "fuzoku2", "fuzoku3", "fuzoku4", "fuzoku5", "fuzoku6", "fuzoku7", "fuzoku8", "fuzoku9", "fuzoku10", "fuzoku11" }) {
		add((std::string("objects/ufoparts/") + name + ".mod").c_str(), { -1 }, grey, 8.0f, centOnly(10.0f, 0.0f));
	}

	// GlobalShape::init. One joint and a mesh are enough for marker.mod and the
	// leaf, bud and flower (drawn with the Pikmin's matrix 6); marker.mod needs a
	// material (navi.cpp:3985). cursor.mod is instanced on joint 0 (navi.cpp:669)
	// and drawn without updateAnim; it has two joints on purpose (the arrow on
	// joint 1) so boot_smoke covers #118/#119.
	add("shapes/cursor/marker.mod", { -1 }, yellow, 5.0f);
	add("shapes/cursor/cursor.mod", { -1, 0 }, white, 5.0f);
	add("pikis/happas/leaf.mod", { -1 }, { 60, 200, 60, 255 }, 2.0f);
	add("pikis/happas/bud.mod", { -1 }, { 200, 120, 160, 255 }, 2.0f);
	add("pikis/happas/flower.mod", { -1 }, { 240, 240, 120, 255 }, 2.0f);

	// ItemMgr::initialise. pikihead reads joint 3 of the sprout (pikiheadItem.cpp:253),
	// so 4 joints. The bomb's 'cent' matches its 3-unit art sphere. The others need
	// no joint beyond the root.
	add("objects/pikihead/pikihead.mod", flatRig(4), { 60, 200, 60, 255 }, 3.0f);
	add("objects/bomb/bomb.mod", { -1 }, grey, 3.0f, centOnly(3.0f, 0.0f));
	add("objects/water/water.mod", { -1 }, blue, 4.0f);
	add("objects/fl_water/fl_water.mod", { -1 }, blue, 4.0f);
	add("objects/door.mod", { -1 }, grey, 10.0f);
	add("objects/key.mod", { -1 }, yellow, 3.0f);
	add("objects/goal/goal.mod", flatRig(kOnionJoints), { 255, 140, 40, 255 }, 15.0f,
	    collInfo({
	        { 1, "cent", 15.0f, { 0.0f, 30.0f, 0.0f } },
	        onionLeg("leg1", "bas1", 3, 0.0f, 1.0f),
	        onionLeg("leg2", "bas2", 6, kCos210, kSin210),
	        onionLeg("leg3", "bas3", 9, -kCos210, kSin210),
	        { 0, "cont", 15.0f, { 0.0f, 0.0f, 0.0f } },
	        { 12, "piki", 10.0f, { 0.0f, 50.0f, 0.0f } },
	        // eff1 is on leg 3's joint and eff3 on leg 1's, each at that foot.
	        { 9, "eff1", 5.0f, { -kCos210 * kOnionFootR, 0.0f, kSin210 * kOnionFootR } },
	        { 6, "eff2", 5.0f, { kCos210 * kOnionFootR, 0.0f, kSin210 * kOnionFootR } },
	        { 3, "eff3", 5.0f, { 0.0f, 0.0f, kOnionFootR } },
	        { 1, "llit", 10.0f, { 0.0f, 15.0f, 0.0f } },
	    }));

	// ItemMgr constructor: stones and weed, one joint.
	add("objects/weeds/stone1.mod", { -1 }, grey, 4.0f);
	add("objects/weeds/stone2.mod", { -1 }, grey, 5.0f);
	add("objects/weeds/stone3.mod", { -1 }, grey, 6.0f);
	add("objects/weeds/weed1.mod", { -1 }, { 60, 200, 60, 255 }, 4.0f);

	// EffectMgr constructor: the Onion effect shapes, instanced on joint 0.
	add("effects/shapes/b_goal.mod", { -1 }, blue, 6.0f);
	add("effects/shapes/r_goal.mod", { -1 }, red, 6.0f);
	add("effects/shapes/y_goal.mod", { -1 }, yellow, 6.0f);
	return s;
}


} // namespace

const std::vector<StandInModelSpec>& standInModelSpecs()
{
	static const std::vector<StandInModelSpec> specs = buildSpecs();
	return specs;
}

const StandInModelSpec* findStandInModel(const std::string& path)
{
	for (const StandInModelSpec& s : standInModelSpecs()) {
		if (s.path == path) return &s;
	}
	return nullptr;
}

Model buildStandInModel(const StandInModelSpec& spec)
{
	return buildArtStandInModel(spec);
}

bool writeStandInModels(std::vector<StandInModelFile>& out, std::string& error)
{
	out.clear();
	for (const StandInModelSpec& spec : standInModelSpecs()) {
		StandInModelFile f;
		f.path = spec.path;
		std::string err;
		if (!writeMod(buildStandInModel(spec), f.bytes, err)) {
			error = spec.path + ": " + err;
			return false;
		}
		out.push_back(std::move(f));
	}
	return true;
}

} // namespace nectar
