// The tutorial stand-in stage, stages/practice.ini (see standin_practice_stage.h).
#include "standin_practice_stage.h"
#include "standin_map_dressing.h"

#include "nectar_assets/mod_writer.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_terrain.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"

#include <cstdio>

namespace standin {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr const char* kStageGenDir = "stages/practice/";
constexpr const char* kMapMod      = "courses/practice/practice.mod";
// In memory only: what compileMap reads through its GameFileReader.
constexpr const char* kSourceMod = "courses/practice/source.mod";
constexpr const char* kSourceIni = "stages/practice_base.ini";
constexpr int kSourceMaterials   = 25; // compileMap touches materials up to 24 (see standin_objects_stage.cpp)

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

bool sourceMod(Bytes& out, std::string& error)
{
	nectar::Model m;
	for (int i = 0; i < kSourceMaterials; i++) {
		nectar::ModMaterial mat;
		mat.flags   = nectar::MOD_MAT_OPAQUE;
		mat.texAttr = -1;
		mat.colour  = i == 0 ? nectar::Rgba8 { 120, 110, 100, 255 } : nectar::Rgba8 { 110, 150, 90, 255 };
		m.materials.push_back(mat);
	}
	nectar::addStandInTerrainTextures(m); // grass floor (14), sandstone walls (0)
	if (!nectar::writeModTables(m, out, error)) return fail(error, "practice source .mod: " + error);
	return true;
}

/// The same base .ini as the sandboxes: map_file and the disc's day_multiply (the disc's practice.ini also has 1.0).
std::string baseIni()
{
	return "# Tutorial (Impact Site) stand-in stage for the disc-free scenario tests, written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/practice/practice.mod\n"
	       "day_multiply\t1.0\n";
}

using Spec = nectar::ObjectGenSpec;

Spec at(Spec s, float x, float z, const char* memo, int face)
{
	s.pos         = { x, 0.0f, z };
	s.memo        = memo;
	s.faceDegrees = face;
	return s;
}

/// default.gen: the three Onions and the ship, as on the disc (generators named "    ", not "next").
std::vector<Spec> defaultGen()
{
	std::vector<Spec> g;
	Spec onion;
	onion.kind = Spec::Item;
	onion.name = "goal";
	onion.itemParam = 0;
	g.push_back(at(onion, -150.0f, -500.0f, "blue goal", 180));
	onion.itemParam = 1;
	g.push_back(at(onion, kPracticeRedOnionX, kPracticeRedOnionZ, "red goal", 100));
	onion.itemParam = 2;
	g.push_back(at(onion, 150.0f, -650.0f, "yellow goal", 320));
	Spec ufo;
	ufo.kind = Spec::Item;
	ufo.name = "ufo";
	g.push_back(at(ufo, kPracticeUfoX, kPracticeUfoZ, "ufo goal", 256));
	return g;
}

/// init.gen (one-shot): the Main Engine and the push box.
std::vector<Spec> initGen()
{
	std::vector<Spec> g;
	Spec engine;
	engine.kind = Spec::Pellet;
	engine.name = "ust5";
	g.push_back(at(engine, kPracticeEngineX, kPracticeEngineZ, "main engine", 0));
	Spec box;
	box.kind         = Spec::Work;
	box.name         = "move stone";
	box.shape        = "stone 10";
	box.stoneTo      = { kPracticeBoxX, 0.0f, kPracticeBoxZ - kPracticeBoxPush };
	box.stonePushers = 10;    // the disc's p02 (also the code default)
	box.stoneSpeed   = 50.0f; // the disc's p03 (code default 30)
	g.push_back(at(box, kPracticeBoxX, kPracticeBoxZ, "push box", 0));
	return g;
}

/// 0.gen, the Day 1 file: one red 1-pellet (the disc's 0.gen has four of them and two 5-pellets).
std::vector<Spec> day1Gen()
{
	Spec pellet;
	pellet.kind = Spec::Pellet;
	pellet.name = "pr01";
	return { at(pellet, kPracticePelletX, kPracticePelletZ, "red pellet 1", 0) };
}

/// Draw-only dressing (standin_map_dressing.h): rock faces along the map edge.
standin::MapDressing mapDressing() { return {}; }

} // namespace

bool addPracticeStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error)
{
	Bytes srcMod;
	if (!sourceMod(srcMod, error)) return false;
	const std::string ini = baseIni();

	// 1,600 units square, flat, 50-unit cells. compileMap lays a route grid with
	// diagonals over all of it, so the Onions, the ship, the box and every carry
	// have way points (GoalItem::startAI, UfoItem::startAI and HinderRock::startAI
	// look one up). The grid is 3 by 3 (400-unit steps): a carry plans its route
	// with PathFinder::findSyncOnyon, the original depth-first search with no
	// visited set, which walks every simple path of the graph; on the default
	// 150-unit grid (81 points, each linked to 8) one pellet's route did not
	// finish in two minutes. The disc's hand-made route networks are sparse too.
	nectar::MapDoc doc;
	doc.routeStep = 400;
	doc.name      = "practice";
	doc.symmetric = false;
	doc.minX      = -800;
	doc.maxX      = 800;
	doc.minZ      = -800;
	doc.maxZ      = 800;
	doc.cell      = 50;
	doc.sourceMod = kSourceMod;
	doc.baseStage = kSourceIni;
	doc.terrain.border.source = kSourceMod;
	nectar::makeTerrainGrid(doc);
	nectar::DocMaterial grass;
	grass.source           = kSourceMod;
	grass.index            = 14;
	grass.attribute        = 2;
	doc.terrain.palette[0] = grass;

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "practice map: " + error);
	const nectar::GameFileReader read = [&](const std::string& path, Bytes& out) {
		if (path == kSourceMod) {
			out = srcMod;
			return true;
		}
		if (path == kSourceIni) {
			out.assign(ini.begin(), ini.end());
			return true;
		}
		std::fprintf(stderr, "[standin] compileMap asked for %s, which the generator does not serve\n", path.c_str());
		return false;
	};
	const standin::MapDressing dressing = mapDressing();
	nectar::CompiledMap compiled;
	if (!standin::compileDressedMap(data, read, kMapMod, dressing, compiled, error)) return fail(error, "practice map: " + error);

	tree[std::string("dataDir/") + kMapMod]                       = compiled.mod;
	tree[std::string("dataDir/") + kPracticeStageIni]             = compiled.ini;
	tree[std::string("dataDir/") + kStageGenDir + "default.gen"] = nectar::writeObjectGenFile(defaultGen());
	tree[std::string("dataDir/") + kStageGenDir + "init.gen"]    = nectar::writeObjectGenFile(initGen());
	tree[std::string("dataDir/") + kStageGenDir + "0.gen"]       = nectar::writeObjectGenFile(day1Gen());
	return true;
}

} // namespace standin
