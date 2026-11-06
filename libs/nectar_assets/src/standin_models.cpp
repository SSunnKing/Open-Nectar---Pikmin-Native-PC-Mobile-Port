#include "nectar_assets/standin_models.h"

#include "nectar_assets/mod_writer.h"
#include "nectar_assets/piki_rig.h"
#include "nectar_assets/standin_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nectar {

namespace {

// ── Partes de colisión (texto collinfo) ────────────────────────────────────
//
// Gramática de ObjCollInfo::loadini y BaseShape::importIni (shpObjColl.cpp:124,
// shapeBase.cpp:1901): "collinfo <articulación> {" y dentro id, code, type 1,
// radius, centre x y z, y más collinfo anidados. El formato del texto es el de
// los .mod del disco.

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

/// Una sola esfera 'cent' en la articulación 0 (pelotas, piezas de la nave).
std::string centOnly(float radius, float cy)
{
	return collInfo({ { 0, "cent", radius, { 0.0f, cy, 0.0f } } });
}

// ── Tablas ─────────────────────────────────────────────────────────────────
//
// Número y padres de articulaciones de cada modelo: DEL DISCO. Se leyeron de los
// .mod originales (solo lectura, en ~/OpenNectar-launcher-fixes/assets/dataDir)
// porque las animaciones de #66 tienen que tener el mismo número y el código
// lee articulaciones por índice. Solo la estructura: sin geometría.

const std::vector<int> kPelletRig = { -1, 0, 0 };                      // white1-4
const std::vector<int> kShipRig   = { -1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 16, 2, 18, 19, 20, 21, 22, 20, 24, 25, 26, 19,
                                      2, 29, 29, 2, 32, 32, 32, 2, 36, 2, 38, 39, 38, 41, 42, 43, 44, 45, 46, 41, 41, 41, 41, 41, 41, 53, 54, 54 }; // ufo0705: 57
const std::vector<int> kGoalRig   = { -1, 0, 1, 1, 3, 4, 1, 6, 7, 1, 9, 10, 1 };                          // goal.mod: 13

std::vector<int> pikiRig()
{
	return std::vector<int>(kPikiRigParents, kPikiRigParents + kPikiRigJointCount);
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

	// gameSetup.cpp shapeList: peletas. Radio y centro de 'cent' del disco.
	add("objects/pellets/white1.mod", kPelletRig, white, 5.0f, centOnly(10.0f, 5.0f));
	add("objects/pellets/white2.mod", kPelletRig, white, 10.0f, centOnly(20.0f, 0.0f));
	add("objects/pellets/white3.mod", kPelletRig, white, 17.0f, centOnly(35.0f, 0.0f));
	add("objects/pellets/white4.mod", kPelletRig, white, 24.0f, centOnly(48.0f, 12.0f));

	// Los cuatro Pikmin (kinModel es el Puffmin): el esqueleto del capitán, con
	// 'cent' (radio 3.2, centro 0 0.8 0.5, valores del disco).
	const std::string pikiColl = collInfo({ { 0, "cent", 3.2f, { 0.0f, 0.8f, 0.5f } } });
	add("pikis/bluModel.mod", pikiRig(), blue, 3.0f, pikiColl);
	add("pikis/redModel.mod", pikiRig(), red, 3.0f, pikiColl);
	add("pikis/yelModel.mod", pikiRig(), yellow, 3.0f, pikiColl);
	add("pikis/kinModel.mod", pikiRig(), white, 3.0f, pikiColl);

	// Capitán. 'rhnd' es OBLIGATORIA: PikiGoHangState::exec (pikiState.cpp:1695) y
	// NaviThrowWaitState::lockHangPiki (naviState.cpp:2143,2203) la usan sin
	// comprobar si existe, y sin ella el agarre y el lanzamiento (#61
	// throw_release) se caen. Su articulación es la mano derecha, la 7, que solo
	// está en el disco (piki_rig.h). Radio 2 y centro 2.7 0 0 son del disco; las
	// demás partes (bnds, cent, head, ante) también, y el resto del juego las
	// busca con comprobación de nulo.
	add("pikis/nv3Model.mod", pikiRig(), white, 6.0f,
	    collInfo({
	        { 0, "bnds", 12.0f, { 0.0f, 4.5f, 5.0f }, { { 0, "cent", 6.0f, { 0.0f, 2.5f, 2.0f } }, { kPikiRigHeadJoint, "head", 7.0f, { 4.0f, 2.0f, 0.0f } } } },
	        { kPikiRigRightHandJoint, "rhnd", 2.0f, { 2.7f, 0.0f, 0.0f } },
	        { kPikiRigLeafTipJoint, "ante", 2.0f, { 2.0f, 0.0f, 0.0f } },
	    }));

	// gameSetup.cpp shapeList2: la nave. 57 articulaciones y padres del disco; el
	// código exige al menos 54 (kStandInShipMinJoints). Las esferas son las que
	// UfoItem busca sin comprobar nulo (gol1, suc0, suc1, eng1-4, gcen; ufoItem.cpp:125,358,484-505,736),
	// con articulación, radio y centro del disco.
	add("objects/ufo/ufo0705.mod", kShipRig, { 150, 160, 180, 255 }, 20.0f,
	    collInfo({
	        { 3, "bnds", 75.0f, { 0.0f, -30.0f, 0.0f }, { { 3, "cent", 40.0f, { 0.0f, -5.0f, 0.0f } }, { 3, "none", 35.0f, { 0.0f, -55.0f, 0.0f } } } },
	        { 16, "suc0", 10.0f, { 0.0f, -3.0f, 5.0f } },
	        { 16, "suc1", 10.0f, { 0.0f, -3.0f, 5.0f } },
	        { 3, "gol0", 10.0f, { 37.0f, 71.5f, 123.599998f } },
	        { 0, "gol1", 10.0f, { 0.0f, 0.0f, 120.0f } },
	        { 3, "eng1", 10.0f, { 14.4f, 11.6f, 14.4f } },
	        { 3, "eng2", 10.0f, { 14.4f, 11.6f, -14.4f } },
	        { 3, "eng3", 10.0f, { -14.4f, 11.6f, 14.4f } },
	        { 3, "eng4", 10.0f, { -14.4f, 11.6f, -14.4f } },
	        { 3, "gcen", 10.0f, { 0.0f, 16.0f, 0.0f } },
	    }));

	// Piezas de la nave: solo se indexan por mRepairAnimJointIndex, que sale de
	// parms/pelMgr.bin (#66), así que tienen el número de articulaciones del
	// disco para que cualquier índice de reparación del disco siga en rango. Se
	// dibujan como pelotas: 'cent' con radio y centro del disco.
	struct Part {
		const char* name;
		std::vector<int> parents;
		float radius, cy;
	};
	const Part soto[] = {
		{ "soto1", { -1, 0, 0, 2, 0, 4, 5, 6, 7, 8, 9 }, 22.0f, 3.0f },
		{ "soto2", { -1, 0, 1, 1, 1, 1 }, 31.0f, 3.0f },
		{ "soto3", { -1 }, 32.5f, 16.0f },
		{ "soto4", { -1 }, 25.5f, 0.0f },
		{ "soto5", { -1, 0, 1, 2, 3, 1, 5, 6, 1, 8, 9, 1, 11, 12, 1, 14, 15, 1, 17, 18 }, 20.0f, 0.0f },
	};
	for (const Part& p : soto) add((std::string("objects/ufoparts/") + p.name + ".mod").c_str(), p.parents, grey, 8.0f, centOnly(p.radius, p.cy));
	const Part fuzoku[] = {
		{ "fuzoku1", { -1, 0, 1, 2, 3 }, 10.0f, 0.0f },
		{ "fuzoku2", { -1, 0, 1 }, 11.5f, 3.0f },
		{ "fuzoku3", { -1, 0, 1 }, 11.5f, 3.0f },
		{ "fuzoku4", { -1 }, 22.0f, 4.0f },
		{ "fuzoku5", { -1 }, 15.0f, 0.0f },
		{ "fuzoku6", { -1, 0 }, 8.4f, 0.0f },
		{ "fuzoku7", { -1 }, 8.0f, 0.0f },
		{ "fuzoku8", { -1 }, 8.0f, 0.0f },
		{ "fuzoku9", { -1, 0 }, 20.0f, 0.0f },
		{ "fuzoku10", { -1 }, 10.0f, 7.0f },
		{ "fuzoku11", { -1 }, 10.0f, 7.0f },
	};
	for (const Part& p : fuzoku) add((std::string("objects/ufoparts/") + p.name + ".mod").c_str(), p.parents, grey, 8.0f, centOnly(p.radius, p.cy));

	// GlobalShape::init. Una articulación y una malla bastan para marker.mod,
	// hoja, capullo y flor (se dibujan con la matriz 6 del Pikmin); marker.mod
	// necesita material (navi.cpp:3985). cursor.mod se instancia en la 0
	// (navi.cpp:669) y se dibuja sin updateAnim; lleva dos articulaciones a
	// propósito (la flecha va en la 1) para que boot_smoke cubra #118/#119.
	add("shapes/cursor/marker.mod", { -1 }, yellow, 5.0f);
	add("shapes/cursor/cursor.mod", { -1, 0 }, white, 5.0f);
	add("pikis/happas/leaf.mod", { -1 }, { 60, 200, 60, 255 }, 2.0f);
	add("pikis/happas/bud.mod", { -1 }, { 200, 120, 160, 255 }, 2.0f);
	add("pikis/happas/flower.mod", { -1 }, { 240, 240, 120, 255 }, 2.0f);

	// ItemMgr::initialise. pikihead lee la articulación 3 del brote
	// (pikiheadItem.cpp:243,257,261), con 4 del disco; fl_water también tiene 4.
	add("objects/pikihead/pikihead.mod", { -1, 0, 1, 2 }, { 60, 200, 60, 255 }, 3.0f);
	add("objects/bomb/bomb.mod", { -1 }, grey, 3.0f, centOnly(6.0f, 0.0f));
	add("objects/water/water.mod", { -1 }, blue, 4.0f);
	add("objects/fl_water/fl_water.mod", { -1, 0, 0, 0 }, blue, 4.0f);
	add("objects/door.mod", { -1 }, grey, 10.0f);
	add("objects/key.mod", { -1 }, yellow, 3.0f);
	// goal.mod es el Onyon: 13 articulaciones del disco. GoalItem busca estas
	// esferas sin comprobar nulo (goalItem.cpp:101-323,411,677,860; aiEnter.cpp:99):
	// cent, leg1-3 con su hijo bas1-3, cont, piki, eff1-3 y llit. Todo del disco.
	add("objects/goal/goal.mod", kGoalRig, { 255, 140, 40, 255 }, 15.0f,
	    collInfo({
	        // cent and cont are moved to the disc's world height (see cont below).
	        // The disc's cent is -16.67 from joint 1, which the disc rig puts at
	        // 291.5 - 181.7 = 109.8, so the body sphere is about 93 up. At -16.67
	        // it sat on the ground and pushed the captain off the cont spot.
	        // The other spheres keep the disc offsets on the identity rig (they
	        // end up lower than on the disc; no scenario depends on them).
	        { 1, "cent", 30.0f, { 0.0f, 93.1f, 0.0f } },
	        { 3, "leg1", 10.0f, { 0.0f, 0.0f, 0.0f }, { { 3, "bas1", 10.0f, { 120.0f, 0.0f, 0.0f } } } },
	        { 6, "leg2", 10.0f, { 0.0f, 0.0f, 0.0f }, { { 6, "bas2", 10.0f, { 120.0f, 0.0f, 0.0f } } } },
	        { 9, "leg3", 10.0f, { 0.0f, 0.0f, 0.0f }, { { 9, "bas3", 10.0f, { 120.0f, 0.0f, 0.0f } } } },
	        // The disc's cont is -290 from joint 0, which the disc rig puts 291.5 up
	        // (the body on its legs), so the spot sits 1.5 above the ground. The
	        // stand-in joints all stay at the origin (identity poses), so the
	        // centre is the disc's world height: with -290 the captain could never
	        // reach it and the container menu never opened (verification-log.md).
	        { 0, "cont", 20.0f, { 0.0f, 1.5f, 0.0f } },
	        { 12, "piki", 10.0f, { 0.0f, 0.0f, 0.0f } },
	        { 9, "eff1", 10.0f, { -2.0f, 5.0f, 0.0f } },
	        { 6, "eff2", 10.0f, { -2.0f, 5.0f, 0.0f } },
	        { 3, "eff3", 10.0f, { -2.0f, 5.0f, 0.0f } },
	        { 1, "llit", 10.0f, { 0.0f, -34.0f, 0.0f } },
	    }));

	// ctor de ItemMgr: piedras y hierba, una articulación.
	add("objects/weeds/stone1.mod", { -1 }, grey, 4.0f);
	add("objects/weeds/stone2.mod", { -1 }, grey, 5.0f);
	add("objects/weeds/stone3.mod", { -1 }, grey, 6.0f);
	add("objects/weeds/weed1.mod", { -1 }, { 60, 200, 60, 255 }, 4.0f);

	// ctor de EffectMgr: las formas de los efectos del Onyon, 2 articulaciones del
	// disco (se instancian en la 0).
	add("effects/shapes/b_goal.mod", { -1, 0 }, blue, 6.0f);
	add("effects/shapes/r_goal.mod", { -1, 0 }, red, 6.0f);
	add("effects/shapes/y_goal.mod", { -1, 0 }, yellow, 6.0f);
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
