// The sandbox_all_objects stage (see standin_all_objects_stage.h).
#include "standin_all_objects_stage.h"
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

constexpr const char* kStageGenDir = "stages/sandbox_all_objects/";
constexpr const char* kMapMod      = "courses/sandbox_all_objects/sandbox_all_objects.mod";
constexpr const char* kSourceMod   = "courses/sandbox_all_objects/source.mod";
constexpr const char* kSourceIni   = "stages/sandbox_all_objects_base.ini";
constexpr int kSourceMaterials     = 25; // compileMap touches materials up to 24 (see standin_objects_stage.cpp)

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
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox_all_objects source .mod: " + error);
	return true;
}

std::string baseIni()
{
	return "# Every-object sandbox stage for the disc-free scenario tests, written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/sandbox_all_objects/sandbox_all_objects.mod\n"
	       "day_multiply\t1.0\n";
}

std::vector<nectar::ObjectGenSpec> generators()
{
	using Spec = nectar::ObjectGenSpec;
	std::vector<Spec> g;
	auto at = [](Spec s, float x, float z, const char* memo) {
		s.pos  = { x, 0.0f, z };
		s.memo = memo;
		return s;
	};
	{
		Spec s;
		s.kind = Spec::Item;
		s.name = "sluice soft"; // the soft gate (BuildingItem, three stages)
		// Health is hours + days * 24 (genItem.cpp:241): 12 + 12 * 24 = 300. The disc's gate values were not read; this is a
		// chosen value, so a gate survives a few hits (one left at 0 breaks fully on its first).
		s.itemDays  = 12;
		s.itemHours = 12;
		g.push_back(at(s, 0.0f, -350.0f, "soft gate"));
	}
	// PLANT_Clover 0, Chidome 1, Hutaba 2, Ine 3, Tanpopo 4, Hae 11: the plants with a model in the boot manifest.
	const int plants[6] = { 0, 1, 2, 3, 4, 11 };
	for (int i = 0; i < 6; i++) {
		Spec s;
		s.kind      = Spec::Plant;
		s.plantType = plants[i];
		g.push_back(at(s, -450.0f + 60.0f * float(i), -450.0f, "plant"));
	}
	{
		Spec s;
		s.kind  = Spec::Work;
		s.name  = "bridge test";
		s.shape = "bridge 4";
		g.push_back(at(s, 300.0f, -300.0f, "bridge"));
		s.shape = "bridge 13";
		g.push_back(at(s, 400.0f, -100.0f, "long bridge"));
		s.name    = "move stone";
		s.shape   = "stone 10";
		s.stoneTo = { 200.0f, 0.0f, 420.0f };
		g.push_back(at(s, 200.0f, 300.0f, "push box"));
	}
	{
		Spec s;
		s.kind     = Spec::Boss;
		s.bossType = 1; // GENBOSS_Snake
		g.push_back(at(s, -350.0f, 350.0f, "snake"));
		s.bossType = 8; // GENBOSS_Mizu
		g.push_back(at(s, -150.0f, 350.0f, "mizu"));
	}
	// TEKI_Iwagon 2, Chappy 3, Swallow 4, Palm 7, Beatle 17, KabekuiA 18, KabekuiB 19, Otama 25.
	const int tekis[kAllObjectsEnemies] = { 2, 3, 4, 7, 17, 18, 19, 25 };
	for (int i = 0; i < kAllObjectsEnemies; i++) {
		Spec s;
		s.kind     = Spec::Teki;
		s.tekiType = tekis[i];
		g.push_back(at(s, -500.0f + 120.0f * float(i), 150.0f, "enemy"));
	}
	return g;
}

/// Draw-only dressing (standin_map_dressing.h): rock faces along the map edge, a
/// soil path from the captain's start to the soft gate and on, and a branch to the
/// long bridge.
standin::MapDressing mapDressing()
{
	standin::MapDressing d;
	standin::DressPath main;
	main.points = { { 20.0f, 260.0f }, { 0.0f, 120.0f }, { -15.0f, 0.0f }, { 5.0f, -170.0f }, { 0.0f, -350.0f }, { -20.0f, -560.0f } };
	d.paths.push_back(main);
	standin::DressPath branch;
	branch.width  = 46.0f;
	branch.points = { { -5.0f, -120.0f }, { 120.0f, -170.0f }, { 260.0f, -165.0f }, { 400.0f, -150.0f } };
	d.paths.push_back(branch);
	return d;
}

} // namespace

bool addAllObjectsStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error)
{
	Bytes srcMod;
	if (!sourceMod(srcMod, error)) return false;
	const std::string ini = baseIni();

	nectar::MapDoc doc;
	doc.name      = "sandbox_all_objects";
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
	grass.source           = kSourceMod;
	grass.index            = 14;
	grass.attribute        = 2;
	doc.terrain.palette[0] = grass;

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox_all_objects map: " + error);
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
	if (!standin::compileDressedMap(data, read, kMapMod, dressing, compiled, error)) return fail(error, "sandbox_all_objects map: " + error);

	tree[std::string("dataDir/") + kMapMod]                       = compiled.mod;
	tree[std::string("dataDir/") + kAllObjectsStageIni]           = compiled.ini;
	tree[std::string("dataDir/") + kStageGenDir + "default.gen"] = nectar::writeObjectGenFile(generators());
	return true;
}

} // namespace standin
