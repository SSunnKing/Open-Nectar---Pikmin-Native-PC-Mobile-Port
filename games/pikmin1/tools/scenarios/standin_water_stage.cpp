#include "standin_water_stage.h"
#include "standin_map_dressing.h"

#include "nectar_assets/mod_writer.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_terrain.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"

namespace standin {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr const char* kStageGenDir = "stages/sandbox_water/";
constexpr const char* kMapMod      = "courses/sandbox_water/sandbox_water.mod";
constexpr const char* kSourceMod   = "courses/sandbox_water/source.mod";
constexpr const char* kSourceIni   = "stages/sandbox_water_base.ini";
constexpr int kSourceMaterials     = 25;

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
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox_water source .mod: " + error);
	return true;
}

std::string baseIni()
{
	return "# Water sandbox stage for disc-free hazard scenarios (#93), written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/sandbox_water/sandbox_water.mod\n"
	       "day_multiply\t1.0\n";
}

/// Draw-only dressing (standin_map_dressing.h): rock faces along the map edge.
standin::MapDressing mapDressing() { return {}; }

} // namespace

bool addWaterStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error)
{
	Bytes srcMod;
	if (!sourceMod(srcMod, error)) return false;
	const std::string ini = baseIni();

	nectar::MapDoc doc;
	doc.name      = "sandbox_water";
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

	nectar::DocPond pond;
	pond.cx      = kPondCx;
	pond.cz      = kPondCz;
	pond.rx      = kPondRx;
	pond.rz      = kPondRz;
	pond.depth   = kPondDepth;
	pond.surface = kPondSurface;
	pond.island  = 0;
	doc.ponds.push_back(pond);

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox_water map: " + error);

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
	if (!standin::compileDressedMap(data, read, kMapMod, dressing, compiled, error)) return fail(error, "sandbox_water map: " + error);

	tree[std::string("dataDir/") + kMapMod]                  = compiled.mod;
	tree[std::string("dataDir/") + kWaterStageIni]            = compiled.ini;
	const Bytes emptyGen = nectar::writeObjectGenFile({});
	tree[std::string("dataDir/") + kStageGenDir + "default.gen"] = emptyGen;
	tree[std::string("dataDir/") + kStageGenDir + "0.gen"]       = emptyGen;
	tree[std::string("dataDir/") + kStageGenDir + "init.gen"]    = emptyGen;
	tree[std::string("dataDir/") + kStageGenDir + "plants.gen"]  = emptyGen;
	return true;
}

} // namespace standin
