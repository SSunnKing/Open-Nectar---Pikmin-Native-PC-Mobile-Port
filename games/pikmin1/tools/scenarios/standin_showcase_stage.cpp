#include "standin_showcase_stage.h"
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

constexpr const char* kStageGenDir = "stages/sandbox_showcase/";
constexpr const char* kMapMod      = "courses/sandbox_showcase/sandbox_showcase.mod";
constexpr const char* kSourceMod   = "courses/sandbox_objects/source.mod";
constexpr const char* kSourceIni   = "stages/sandbox_objects_base.ini";

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

bool sourceMod(Bytes& out, std::string& error)
{
	nectar::Model m;
	for (int i = 0; i < 25; i++) {
		nectar::ModMaterial mat;
		mat.flags   = nectar::MOD_MAT_OPAQUE;
		mat.texAttr = -1;
		mat.colour  = (i == 0 || i == 14) ? nectar::Rgba8 { 90, 150, 70, 255 } : nectar::Rgba8 { 110, 150, 90, 255 };
		m.materials.push_back(mat);
	}
	nectar::addStandInTerrainTextures(m); // grass floor (14), sandstone walls (0)
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox_showcase source .mod: " + error);
	return true;
}

std::string baseIni()
{
	return "# Flat showcase stage for stand-in art captures (nectar_standin_generator).\n"
	       "map_file\t\tcourses/sandbox_showcase/sandbox_showcase.mod\n"
	       "day_multiply\t1.0\n";
}

std::vector<nectar::ObjectGenSpec> generators()
{
	std::vector<nectar::ObjectGenSpec> g;
	{
		nectar::ObjectGenSpec s;
		s.kind          = nectar::ObjectGenSpec::Item;
		s.memo          = "red onion";
		s.name          = "goal";
		s.itemParam     = 1;
		s.pos           = { kShowcaseRedOnionX, 0.0f, kShowcaseRedOnionZ };
		g.push_back(s);
	}
	{
		nectar::ObjectGenSpec s;
		s.kind = nectar::ObjectGenSpec::Item;
		s.memo = "ship";
		s.name = "ufo";
		s.pos  = { kShowcaseUfoX, 0.0f, kShowcaseUfoZ };
		g.push_back(s);
	}
	{
		nectar::ObjectGenSpec s;
		s.kind = nectar::ObjectGenSpec::Pellet;
		s.memo = "red 1 pellet";
		s.name = "pr01";
		s.pos  = { kShowcasePelletX, 0.0f, kShowcasePelletZ };
		g.push_back(s);
	}
	return g;
}

/// Draw-only dressing (standin_map_dressing.h): rock faces along the map edge.
standin::MapDressing mapDressing() { return {}; }

} // namespace

bool addShowcaseStage(std::map<std::string, std::vector<std::uint8_t>>& tree, std::string& error)
{
	Bytes srcMod;
	if (!sourceMod(srcMod, error)) return false;
	const std::string ini = baseIni();

	nectar::MapDoc doc;
	doc.name      = "sandbox_showcase";
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

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox_showcase map: " + error);

	const nectar::GameFileReader read = [&](const std::string& path, Bytes& out) {
		if (path == kSourceMod) {
			out = srcMod;
			return true;
		}
		if (path == kSourceIni) {
			out.assign(ini.begin(), ini.end());
			return true;
		}
		std::fprintf(stderr, "[standin] compileMap asked for %s, which the showcase generator does not serve\n", path.c_str());
		return false;
	};
	const standin::MapDressing dressing = mapDressing();
	nectar::CompiledMap compiled;
	if (!standin::compileDressedMap(data, read, kMapMod, dressing, compiled, error)) return fail(error, "sandbox_showcase map: " + error);

	tree[std::string("dataDir/") + kMapMod]            = compiled.mod;
	tree[std::string("dataDir/") + kShowcaseStageIni] = compiled.ini;
	tree[std::string("dataDir/") + kStageGenDir + "default.gen"] = nectar::writeObjectGenFile(generators());
	return true;
}

} // namespace standin
