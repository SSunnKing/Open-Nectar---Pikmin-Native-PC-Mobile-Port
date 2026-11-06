// The sandbox_objects stage (see standin_objects_stage.h).
#include "standin_objects_stage.h"
#include "standin_map_dressing.h"

#include "nectar_assets/mod_writer.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_terrain.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"

#include <cstring>

namespace standin {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr const char* kStageGenDir = "stages/sandbox_objects/";
constexpr const char* kMapMod      = "courses/sandbox_objects/sandbox_objects.mod";
// In memory only: what compileMap reads through its GameFileReader.
constexpr const char* kSourceMod = "courses/sandbox_objects/source.mod";
constexpr const char* kSourceIni = "stages/sandbox_objects_base.ini";

// compileMap always touches materials 0 (walls), 14 (floor), 15, 16, 23 and 24
// and the game indexes mMaterialList[poly->mIndex] unchecked, so the source has
// at least 25 (the same rule as the sandbox's source .mod).
constexpr int kSourceMaterials = 25;

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

/// Flat-colour materials, tables only: the cliff is rock, the floor grass.
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
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox_objects source .mod: " + error);
	return true;
}

/// The same base .ini as stages/sandbox.ini: map_file and the disc's day_multiply.
std::string baseIni()
{
	return "# Object sandbox stage for the disc-free scenario tests, written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/sandbox_objects/sandbox_objects.mod\n"
	       "day_multiply\t1.0\n";
}

/// What default.gen places. y is 0: the generators drop things on the ground.
std::vector<nectar::ObjectGenSpec> generators()
{
	std::vector<nectar::ObjectGenSpec> g;
	{
		// The blue Onion. GenObjectItem::birth only creates an Onion whose colour
		// the player already has a container for, unless the generator is named
		// "next" (then only when the player has none).
		nectar::ObjectGenSpec s;
		s.kind          = nectar::ObjectGenSpec::Item;
		s.generatorName = "next"; // a new game starts with the red container only
		s.memo          = "blue onion";
		s.name          = "goal";
		s.itemParam     = 0;
		s.pos           = { kOnionX, 0.0f, kOnionZ };
		g.push_back(s);
	}
	const float sproutAt[3][2] = { { -100.0f, 120.0f }, { -60.0f, 150.0f }, { -20.0f, 180.0f } };
	for (int i = 0; i < 3; i++) {
		nectar::ObjectGenSpec s; // a buried sprout, colours blue, red, yellow
		s.kind       = nectar::ObjectGenSpec::Piki;
		s.memo       = "sprout";
		s.pikiState  = 0;
		s.pikiColour = i;
		s.pos        = { sproutAt[i][0], 0.0f, sproutAt[i][1] };
		g.push_back(s);
	}
	{
		nectar::ObjectGenSpec s;
		s.kind = nectar::ObjectGenSpec::Pellet;
		s.memo = "red 1 pellet";
		s.name = "pr01";
		s.pos  = { -200.0f, 0.0f, 60.0f };
		g.push_back(s);
	}
	{
		nectar::ObjectGenSpec s;
		s.kind     = nectar::ObjectGenSpec::Teki;
		s.memo     = "dwarf bulborb";
		s.tekiType = 3; // TEKI_Chappy
		s.pos      = { -330.0f, 0.0f, 300.0f };
		g.push_back(s);
	}
	return g;
}

/// The play stage's default.gen (standin_objects_stage.h): the object stage's sprouts,
/// pellet and Bulborb, and one Onion of each colour from a generator not named `next`
/// (born while the player owns that colour; free_play owns all three, ownsAllOnions).
std::vector<nectar::ObjectGenSpec> playGenerators()
{
	std::vector<nectar::ObjectGenSpec> g;
	for (const nectar::ObjectGenSpec& s : generators()) {
		if (s.kind != nectar::ObjectGenSpec::Item) g.push_back(s);
	}
	struct Onion {
		const char* memo;
		const char* generator; // "" for the default name (four spaces)
		int colour;
		float x, z;
	};
	const Onion extra[] = {
		{ "blue onion", "", 0, kOnionX, kOnionZ },
		{ "red onion", "", 1, kYellowOnionX, kYellowOnionZ },
		{ "yellow onion", "", 2, kBlueOnionX, kBlueOnionZ },
	};
	for (const Onion& o : extra) {
		nectar::ObjectGenSpec s;
		s.kind = nectar::ObjectGenSpec::Item;
		if (*o.generator) s.generatorName = o.generator;
		s.memo      = o.memo;
		s.name      = "goal";
		s.itemParam = o.colour;
		s.pos       = { o.x, 0.0f, o.z };
		g.push_back(s);
	}
	return g;
}

/// The Onion stage's default.gen (standin_objects_stage.h). GenObjectItem::birth
/// (genItem.cpp) creates a `goal` from a generator not named `next` only while
/// playerState->hasContainer(colour), and from a `next` generator only while not.
std::vector<nectar::ObjectGenSpec> onionGenerators()
{
	struct Onion {
		const char* memo;
		const char* generator; // "" for the default name (four spaces)
		int colour;
		float x, z;
	};
	const Onion onions[] = {
		{ "red onion", "", 1, kRedOnionX, kRedOnionZ },
		{ "yellow onion", "", 2, kYellowOnionX, kYellowOnionZ },
		{ "blue onion", "", 0, kBlueOnionX, kBlueOnionZ },
		{ "yellow onion to find", "next", 2, kNextYellowX, kNextYellowZ },
	};
	std::vector<nectar::ObjectGenSpec> g;
	for (const Onion& o : onions) {
		nectar::ObjectGenSpec s;
		s.kind = nectar::ObjectGenSpec::Item;
		if (*o.generator) s.generatorName = o.generator;
		s.memo      = o.memo;
		s.name      = "goal";
		s.itemParam = o.colour;
		s.pos       = { o.x, 0.0f, o.z };
		g.push_back(s);
	}
	return g;
}

/// Draw-only dressing (standin_map_dressing.h): rock faces along the map edge and
/// over the cliff's face (with a rim at its top), and a soil path from the
/// captain's start towards the Onions.
standin::MapDressing mapDressing()
{
	standin::MapDressing d;
	nectar::StandInCliffSpan cliff;
	cliff.footA = { kCliffFootX, 0.0f, -575.0f };
	cliff.footB = { kCliffFootX, 0.0f, 575.0f };
	cliff.topA  = { kCliffTopX, kCliffHeight, -575.0f };
	cliff.topB  = { kCliffTopX, kCliffHeight, 575.0f };
	cliff.out   = { -1.0f, 0.0f };
	d.cliffs.push_back(cliff);
	standin::DressPath path;
	path.points = { { 10.0f, 260.0f }, { 0.0f, 120.0f }, { -20.0f, 0.0f }, { -70.0f, -110.0f }, { -110.0f, -175.0f } };
	d.paths.push_back(path);
	return d;
}

} // namespace

bool addObjectsStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error)
{
	Bytes srcMod;
	if (!sourceMod(srcMod, error)) return false;
	const std::string ini = baseIni();

	// 600-unit half width, 50-unit cells. The cliff is a step in the heights: the
	// points from x = kCliffTopX up are kCliffHeight higher, so across the one cell
	// between the foot and the top the ground rises 200 over 50 units, a slope of
	// about 76 degrees (face normal y about 0.24, under the 0.5 that the sprout
	// code treats as a wall) with a plateau above it.
	nectar::MapDoc doc;
	doc.name      = "sandbox_objects";
	doc.symmetric = false;
	doc.minX      = -600;
	doc.maxX      = 600;
	doc.minZ      = -600;
	doc.maxZ      = 600;
	doc.cell      = 50;
	doc.sourceMod = kSourceMod;
	doc.baseStage = kSourceIni;
	doc.terrain.border.source = kSourceMod;
	nectar::makeTerrainGrid(doc);
	nectar::DocMaterial grass;
	grass.source    = kSourceMod;
	grass.index     = 14;
	grass.attribute = 2;
	doc.terrain.palette[0] = grass;
	for (int iz = 0; iz <= doc.terrain.nz; iz++) {
		for (int ix = 0; ix <= doc.terrain.nx; ix++) {
			const double x = doc.minX + ix * doc.cell;
			if (x >= double(kCliffTopX)) doc.terrain.heights[std::size_t(doc.terrain.vertexIndex(ix, iz))] = std::int16_t(kCliffHeight);
		}
	}

	// No route points on the cliff or the plateau. The face is too steep to link,
	// so plateau points would form a second network with no way to the Onion, and
	// at stage start WayPoint::initLinkInfos runs PathFinder::findSync from every
	// link of every point to each goal: the original depth-first search with no
	// visited set explores every simple path of that unreachable island before it
	// gives up (about 17 s for 20 points). Nothing on the stage walks up there;
	// seeds that land on the plateau plant without a route.
	const double blockX = (kCliffFootX + kCliffTopX) / 2.0;
	doc.routeBlocks.push_back({ { blockX, doc.minZ - 100 }, { doc.maxX + 100, doc.minZ - 100 }, { doc.maxX + 100, doc.maxZ + 100 }, { blockX, doc.maxZ + 100 } });

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox_objects map: " + error);

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
	if (!standin::compileDressedMap(data, read, kMapMod, dressing, compiled, error)) return fail(error, "sandbox_objects map: " + error);

	tree[std::string("dataDir/") + kMapMod]           = compiled.mod;
	tree[std::string("dataDir/") + kObjectsStageIni] = compiled.ini;
	tree[std::string("dataDir/") + kStageGenDir + "default.gen"] = nectar::writeObjectGenFile(generators());
	// The Onion stage: the same .ini (map, day_multiply), its own generator folder.
	tree[std::string("dataDir/") + kOnionsStageIni]                       = compiled.ini;
	tree[std::string("dataDir/stages/sandbox_onions/default.gen")] = nectar::writeObjectGenFile(onionGenerators());
	// The play stage (#127): the same .ini, the object stage's generators plus two Onions.
	tree[std::string("dataDir/") + kPlayStageIni]                  = compiled.ini;
	tree[std::string("dataDir/stages/sandbox_play/default.gen")] = nectar::writeObjectGenFile(playGenerators());
	// Story courses for the world map (#99): stage1 uses the Onion layout; the rest boot the same map empty.
	const Bytes onionGen = nectar::writeObjectGenFile(onionGenerators());
	const Bytes emptyGen = nectar::writeObjectGenFile({});
	tree["dataDir/stages/stage1.ini"]            = compiled.ini;
	tree["dataDir/stages/stage1/default.gen"]    = onionGen;
	for (const char* ini : { "stages/stage2.ini", "stages/stage3.ini", "stages/last.ini" }) {
		tree[std::string("dataDir/") + ini] = compiled.ini;
		std::string genPath = std::string("dataDir/") + ini;
		genPath.replace(genPath.size() - 4, 4, "/default.gen");
		tree[genPath] = emptyGen;
	}
	return true;
}

} // namespace standin
