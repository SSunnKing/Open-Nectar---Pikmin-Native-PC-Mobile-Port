// Stand-in asset generator for the disc-free scenario tests (issue #68, under
// #62 and #59). It calls every M2 writer in libs/nectar_assets and writes the
// complete stand-in tree to <out>/assets/dataDir/..., so `nectar --scenario`
// boots the sandbox stage with no disc data:
//
//   nectar_standin_generator <out> [--manifest <boot-manifest.txt>]
//   nectar_standin_generator --manifest-from-env
//
// <out> is the folder a scenario run uses as PIKMIN_SCENARIO_DATA_DIR (CTest
// passes <build>/scenario-data). The tree omits assets/.pikmin-assets: only the
// launcher reads it (#62).
//
// Pass criteria owned here (#68): the tree is built twice in-process and the two
// hashes must match (run-to-run identity on one machine; no cross-OS identity is
// claimed), and every writer error is fatal.
//
// Fixture contract (#190): nothing here is read from disc data. Every value in
// the tree is authored for these fixtures or is a game-source constructor
// default; the per-file comments in libs/nectar_assets and the sibling stage
// files say which. Exact disc comparisons are opt-in local tests that read a
// user-supplied disc install.
//
// Local lane only: with --manifest, every path of a boot manifest is either in
// the tree or on the allowlist below with its reason. A boot manifest is the
// open log of a disc run (make_boot_manifest.py over PIKMIN_LOG_FILE_OPENS
// logs, #64); it is generated on the user's machine from their own install and
// never committed. --manifest-from-env reads its path from PIKMIN_BOOT_MANIFEST
// and exits 77 (skipped) when that is unset: the CTest entry
// scenario_standin_manifest (label game-assets) runs it that way.
//
// Provenance (#190): nothing here is read from disc data. Values are authored
// fixtures or the game source's own defaults; PROVENANCE.md lists each class.

#include "nectar_assets/archive.h"
#include "nectar_assets/bundle.h"
#include "nectar_assets/cinema.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mod_writer.h"
#include "nectar_assets/parms.h"
#include "nectar_assets/stages_ini.h"
#include "nectar_assets/standin_anims.h"
#include "nectar_assets/standin_audio.h"
#include "nectar_assets/standin_models.h"
#include "nectar_assets/standin_objects.h"
#include "nectar_assets/standin_sandbox.h"
#include "nectar_assets/standin_terrain.h"
#include "nectar_assets/standin_textures.h"
#include "nectar_assets/standin_ui.h"
#include "nectar_mapcore/compile.h"
#include "nectar_mapcore/map.h"
#include "standin_all_objects_stage.h"
#include "standin_map_dressing.h"
#include "standin_objects_stage.h"
#include "standin_showcase_stage.h"
#include "standin_practice_stage.h"
#include "standin_water_stage.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The game's BARC table for pikiseq.arc (src/jaudio/pikiseq.c, compiled into
// this executable). Its size is 0x20 plus 0x20 per entry; the entry count is
// the header's big-endian u32 at 0x0C.
extern "C" std::uint8_t HEAD_pikiseq[];

namespace {

using Bytes = std::vector<std::uint8_t>;
/// Path relative to assets/ (so it starts with "dataDir/", as in the boot manifest) -> bytes.
using Tree = std::map<std::string, Bytes>;

// ── The sandbox stage ────────────────────────────────────────────────────────
// stages.ini `file` value, the stage .ini, its generator folder (the game cuts
// the path at its first '.', gameCoreSection.cpp initStage) and the map model.
constexpr const char* kSandboxIni    = "stages/sandbox.ini";
constexpr const char* kSandboxGenDir = "stages/sandbox/";
constexpr const char* kSandboxMod    = "courses/sandbox/sandbox.mod";
// In-memory only: what compileMap reads through its GameFileReader.
constexpr const char* kSourceMod  = "courses/sandbox/source.mod";
constexpr const char* kSourceIni  = "stages/sandbox_base.ini";

// compileMap with grid=false always builds meshes on materials 0 (walls), 14
// (floor), 15, 16 and 24 (water layers) and 23 (pond bed), and the game reads
// mMaterialList[poly->mIndex] without a bounds check (shapeBase.cpp), so the
// source .mod has at least 25 materials.
constexpr int kSourceMaterials = 25;

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

Bytes textBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

/// The map-source .mod: tables only. The floor (14) and the walls (0) carry the
/// stand-in grass and sandstone textures (standin_terrain.h); the rest are flat colours.
bool sandboxSourceMod(Bytes& out, std::string& error)
{
	nectar::Model m;
	for (int i = 0; i < kSourceMaterials; i++) {
		nectar::ModMaterial mat;
		mat.flags   = nectar::MOD_MAT_OPAQUE;
		mat.texAttr = -1;
		mat.colour  = { 110, 150, 90, 255 }; // grass green
		if (i == 1) mat.colour = { 90, 130, 70, 255 }; // darker grass
		m.materials.push_back(mat);
	}
	nectar::addStandInTerrainTextures(m);
	if (!nectar::writeModTables(m, out, error)) return fail(error, "sandbox source .mod: " + error);
	return true;
}

/// The stage .ini base that compileMap's buildIni rewrites: it only needs a
/// map_file line. createMapObjects (newPikiGame.cpp) acts on map_file,
/// day_multiply and an optional dayMgr block; with no dayMgr block DayMgr keeps
/// TimeSetting's default white parallel lights. day_multiply 1.0 is the
/// source default (newPikiGame.cpp sets mTimeMultiplier to 1.0 before it reads
/// the .ini), written out so the clock rate is explicit.
std::string sandboxBaseIni()
{
	return "# Sandbox stage for the disc-free scenario tests (#68), written by nectar_standin_generator.\n"
	       "map_file\t\tcourses/sandbox/sandbox.mod\n"
	       "day_multiply\t1.0\n";
}

/// A flat floor (compileMap with grid=false, no walls), the gate and default.gen
/// (standin_sandbox.h).
bool sandboxMap(Tree& tree, std::string& error)
{
	Bytes sourceMod;
	if (!sandboxSourceMod(sourceMod, error)) return false;
	const std::string baseIni = sandboxBaseIni();

	nectar::MapDoc doc;
	doc.name      = "sandbox";
	doc.symmetric = false;
	doc.minX      = -1200;
	doc.maxX      = 1200;
	doc.minZ      = -1200;
	doc.maxZ      = 1200;
	doc.sourceMod = kSourceMod;
	doc.baseStage = kSourceIni;
	doc.terrain.border.source = kSourceMod;

	nectar::MapData data;
	if (!nectar::expandMap(doc, data, error, /*validate=*/false)) return fail(error, "sandbox map: " + error);

	// Serves the in-memory source files and refuses every other path, so an
	// accidental disc read fails loudly.
	const nectar::GameFileReader read = [&](const std::string& path, Bytes& out) {
		if (path == kSourceMod) {
			out = sourceMod;
			return true;
		}
		if (path == kSourceIni) {
			out = textBytes(baseIni);
			return true;
		}
		std::fprintf(stderr, "[standin] compileMap asked for %s, which the generator does not serve\n", path.c_str());
		return false;
	};
	// Draw-only dressing (standin_map_dressing.h): rock faces along the four walls
	// and a soil path from the captain's start through the gate (sandboxGatePlacement,
	// x 0, z -240), authored round points.
	standin::MapDressing dressing;
	standin::DressPath path;
	path.points = { { 60.0f, 260.0f }, { 20.0f, 120.0f }, { 0.0f, 0.0f }, { 0.0f, -120.0f }, { 0.0f, -240.0f }, { -20.0f, -360.0f }, { 0.0f, -480.0f }, { 50.0f, -600.0f } };
	dressing.paths.push_back(path);
	nectar::CompiledMap compiled;
	if (!standin::compileDressedMap(data, read, kSandboxMod, dressing, compiled, error)) return fail(error, "sandbox map: " + error);

	tree[std::string("dataDir/") + kSandboxMod] = compiled.mod;
	tree[std::string("dataDir/") + kSandboxIni] = compiled.ini;
	// The captain at the origin and the gate in his way (standin_sandbox.h); day,
	// init and plants files are omitted because scenarios spawn their own Pikmin
	// (#61).
	std::vector<nectar::SandboxFile> files;
	nectar::SandboxFile gen;
	if (!nectar::writeSandboxGateFiles(files, error) || !nectar::writeSandboxDefaultGen(kSandboxGenDir, gen, error)) return fail(error, "sandbox: " + error);
	files.push_back(std::move(gen));
	for (nectar::SandboxFile& f : files) tree[f.path] = std::move(f.bytes);
	return true;
}

// ── stages/stages.ini ────────────────────────────────────────────────────────
// Five story entries with ids 0-4 (GameSetupSection walks the first STAGE_COUNT
// list nodes without a null check, #64), then the sandbox as the sixth. The
// sandbox has no `id` line, so it is STAGE_TESTMAP (5): not the tutorial (the
// clock runs), no zone banner, music falls back to stage 0. `hidden` keeps it out
// of the in-game map select. Only `file` must match. stages/practice.ini and
// stages/stage1.ini are code literals (game.cpp setStage); the other three story
// files have authored names (course2..course4). practice has its own stand-in
// files (standin_practice_stage.h); stage1 and course2..course4 are the world
// map's courses (standin_objects_stage.cpp).
bool stagesIni(Tree& tree, std::string& error)
{
	std::vector<nectar::StagesEntry> entries;
	const char* const story[] = { "stages/practice.ini", "stages/stage1.ini", "stages/course2.ini", "stages/course3.ini", "stages/course4.ini" };
	for (int id = 0; id < 5; id++) {
		nectar::StagesEntry e;
		e.visible = true;
		e.name    = "story" + std::to_string(id);
		e.id      = id;
		e.file    = story[id];
		entries.push_back(e);
	}
	nectar::StagesEntry sandbox;
	sandbox.visible = false; // `other`: readMapList treats anything but `visible` as hidden
	sandbox.name    = "sandbox";
	sandbox.file    = kSandboxIni;
	entries.push_back(sandbox);
	nectar::StagesEntry objects; // the opt-in object sandbox (standin_objects_stage.h), also hidden, also STAGE_TESTMAP
	objects.visible = false;
	objects.name    = "sandbox_objects";
	objects.file    = standin::kObjectsStageIni;
	entries.push_back(objects);
	nectar::StagesEntry all; // every-object stage (standin_all_objects_stage.h)
	all.visible = false;
	all.name    = "sandbox_all_objects";
	all.file    = standin::kAllObjectsStageIni;
	entries.push_back(all);
	nectar::StagesEntry onions; // the Onion stage of onion_day3 (standin_objects_stage.h), last so no other index moves
	onions.visible = false;
	onions.name    = "sandbox_onions";
	onions.file    = standin::kOnionsStageIni;
	entries.push_back(onions);
	nectar::StagesEntry water;
	water.visible = false;
	water.name    = "sandbox_water";
	water.file    = standin::kWaterStageIni;
	entries.push_back(water);
	nectar::StagesEntry showcase;
	showcase.visible = false;
	showcase.name    = "sandbox_showcase";
	showcase.file    = standin::kShowcaseStageIni;
	entries.push_back(showcase);
	nectar::StagesEntry play; // the free_play stage (standin_objects_stage.h), last so no other index moves
	play.visible = false;
	play.name    = "sandbox_play";
	play.file    = standin::kPlayStageIni;
	entries.push_back(play);

	std::string text;
	if (!nectar::writeStagesIni(entries, text, error)) return fail(error, "stages.ini: " + error);
	tree["dataDir/stages/stages.ini"] = textBytes(text);
	return true;
}

// ── Parameter files ─────────────────────────────────────────────────────────
// Synthetic scenarios exercise the source-defined constructor defaults. Empty
// tagged lists leave those defaults intact; NaviProp and PikiProp each read a
// CreatureProp list followed by their own list. No retail parameter data is used.
bool parmFiles(Tree& tree, std::string& error)
{
	struct File {
		const char* path;
		int lists;
	};
	const File files[] = {
		{ "dataDir/parms/gamePrms.bin", 1 },
		{ "dataDir/parms/aiconst.bin", 1 },
		{ "dataDir/parms/demo.bin", 1 },
		{ "dataDir/parms/cunit.bin", 1 },
		{ "dataDir/parms/naviMgr.bin", 2 },
		{ "dataDir/parms/pikiMgr.bin", 2 },
	};
	for (const File& f : files) {
		nectar::ParmFile pf;
		pf.lists.resize(f.lists);
		Bytes bytes;
		if (!nectar::writeParmFile(pf, bytes, error)) return fail(error, std::string(f.path) + ": " + error);
		tree[f.path] = bytes;
	}
	// parms/key.bin: six big-endian ints, one per KeyConfig::Key::read (keyConfig.cpp):
	// throw A, set cursor B, extract A, attack A, disband X, menu Y (KBBTN values are
	// Controller.h). This is the Classic control scheme the scenarios drive (A throws
	// and plucks, B whistles), written as an authored fixture: it is six button masks
	// from the game's public controls, not a copied file. The constructor default
	// binds extract to B, which would stop the shared-key pluck path being exercised.
	{
		const std::uint32_t binds[] = { 0x1000u, 0x2000u, 0x1000u, 0x1000u, 0x4000u, 0x8000u };
		Bytes key;
		key.reserve(sizeof binds);
		for (std::uint32_t v : binds) {
			key.push_back(std::uint8_t(v >> 24));
			key.push_back(std::uint8_t(v >> 16));
			key.push_back(std::uint8_t(v >> 8));
			key.push_back(std::uint8_t(v));
		}
		tree["dataDir/parms/key.bin"] = std::move(key);
	}
	return true;
}

// ── Archive pairs ───────────────────────────────────────────────────────────
// The 18 pairs the USA build parses (arambundleList in gameSetup.cpp plus
// blo_eng from preloadLanguage). On PC files are read loose and the entries are
// never consumed, so an empty index and a 0-byte blob are enough; a missing .arc
// aborts with "Missing archive".
bool archives(Tree& tree, std::string& error)
{
	const char* const names[] = { "blo_eng", "tekis",   "bosses", "tekipara", "tekikey", "plants",   "ufopartsbin", "bridges", "gates",
		                          "bomb",    "rope",    "water",  "fl_water", "pelletsbin", "pikihead", "effshapes",   "weeds",   "goal" };
	for (const char* n : names) {
		nectar::ArchivePair pair;
		if (!nectar::writeArchivePair({}, pair, error)) return fail(error, std::string(n) + ": " + error);
		tree[std::string("dataDir/archives/") + n + ".dir"] = pair.dir;
		tree[std::string("dataDir/archives/") + n + ".arc"] = pair.arc;
	}
	return true;
}

// ── Models, animations, textures, screens and particles (#65 #66 #67 #72) ──
bool siblingStandIns(Tree& tree, std::string& error)
{
	std::vector<nectar::StandInModelFile> models;
	if (!nectar::writeStandInModels(models, error)) return fail(error, "stand-in models: " + error);
	for (auto& f : models) tree[f.path] = std::move(f.bytes);

	std::vector<nectar::StandInAnimFile> anims;
	if (!nectar::writeStandInAnimations(anims, error)) return fail(error, "stand-in animations: " + error);
	for (auto& f : anims) tree[f.path] = std::move(f.bytes);

	// Object, ship-part, plant, gate, boss and enemy animations, the extra models
	// and the pellet tables (standin_objects.h).
	std::vector<nectar::StandInAnimFile> objects;
	if (!nectar::writeStandInObjectFiles(objects, error)) return fail(error, "stand-in objects: " + error);
	for (auto& f : objects) tree[f.path] = std::move(f.bytes);

	// Optional textures too: the game tolerates their absence, but its boot path
	// opens them, so writing them keeps the open log free of tolerated misses.
	std::vector<nectar::StandInFile> textures;
	if (!nectar::standInTextureFiles(textures, error)) return fail(error, "stand-in textures: " + error);
	for (auto& f : textures) tree["dataDir/" + f.path] = std::move(f.bytes);

	for (const nectar::StandinLayout& l : nectar::standinBloLayouts()) {
		Bytes bytes;
		if (!nectar::writeBlo(l.panes, bytes, error)) return fail(error, l.file + ": " + error);
		tree["dataDir/screen/eng_blo/" + l.file] = std::move(bytes);
	}

	// effects/pcr/effects.bun: one inert type-0 entry per particle name, under its
	// full path, which is what particleLoader::load looks up.
	std::vector<nectar::BundleEntry> pcr;
	const Bytes inert = nectar::inertPcrPayload();
	for (const std::string& name : nectar::standinPcrNames()) pcr.push_back({ nectar::BUNDLE_BINARY, name, inert });
	// The 2D effects of the file-select and results screens (EffectMgr2D).
	for (const std::string& name : nectar::standinPcr2dNames()) pcr.push_back({ nectar::BUNDLE_BINARY, name, inert });
	Bytes bun;
	if (!nectar::writeBundle(pcr, bun, error)) return fail(error, "effects/pcr/effects.bun: " + error);
	tree["dataDir/effects/pcr/effects.bun"] = std::move(bun);
	return true;
}

// ── Cinemas (standin-cinema.md) ────────────────────────────────────────────
bool standInCinemas(Tree& tree, std::string& error)
{
	std::vector<nectar::StandInCinemaFile> files;
	if (!nectar::writeStandInCinemas(files, error)) return fail(error, "stand-in cinemas: " + error);
	for (const auto& f : files) tree[f.path] = textBytes(f.text);
	tree["dataDir/cinemas/opening.ini"] = textBytes("# Stand-in title opening (titles.cpp reads dayMgr optionally)\n");
	return true;
}

// ── SndData for the native JAudio engine (standin-audio.md) ─────────────────
// Only the PIKMIN_NATIVE_JAUDIO=ON build reads it; the legacy facade (OFF) loads
// pikiseq.arc only after Banks/pikibank.bx, which is not written.
bool audioStandIns(Tree& tree, std::string& error)
{
	const std::uint8_t* h     = HEAD_pikiseq;
	const std::size_t entries = std::size_t(h[12]) << 24 | std::size_t(h[13]) << 16 | std::size_t(h[14]) << 8 | h[15];
	std::vector<nectar::StandInFile> files;
	if (!nectar::standInSndDataFiles(h, 0x20 + 0x20 * entries, files, error)) return fail(error, "stand-in SndData: " + error);
	for (auto& f : files) tree["dataDir/" + f.path] = std::move(f.bytes);
	return true;
}

bool buildTree(Tree& tree, std::string& error)
{
	tree.clear();
	return parmFiles(tree, error) && archives(tree, error) && stagesIni(tree, error) && sandboxMap(tree, error) && siblingStandIns(tree, error)
	    && standInCinemas(tree, error) && audioStandIns(tree, error) && standin::addObjectsStage(tree, error) && standin::addAllObjectsStage(tree, error)
	    && standin::addWaterStage(tree, error) && standin::addShowcaseStage(tree, error)
	    && standin::addPracticeStage(tree, error);
}

/// FNV-1a 64 over every path and its bytes, in path order.
std::uint64_t hashTree(const Tree& tree)
{
	std::uint64_t h = 1469598103934665603ull;
	auto mix        = [&](const std::uint8_t* p, std::size_t n) {
		for (std::size_t i = 0; i < n; i++) {
			h ^= p[i];
			h *= 1099511628211ull;
		}
	};
	for (const auto& [path, bytes] : tree) {
		mix(reinterpret_cast<const std::uint8_t*>(path.data()), path.size() + 1);
		const std::uint64_t n = bytes.size();
		mix(reinterpret_cast<const std::uint8_t*>(&n), sizeof(n));
		mix(bytes.data(), bytes.size());
	}
	return h;
}

// ── Manifest check ───────────────────────────────────────────────────────────
// Paths a local boot manifest (#64, a Forest of Hope run on the user's install)
// lists that the stand-in tree leaves out on purpose. A prefix covers every
// path under it.
struct Allowed {
	const char* prefix;
	const char* why;
	const char* suffix = nullptr; ///< when set, only paths that also end with it
};
const Allowed kManifestAllowlist[] = {
	// What the Forest of Hope loads and the sandbox (no generators) does not.
	{ "dataDir/courses/stage1/", "the manifest run's course map; the stand-in stage1.ini uses the object sandbox map" },
	{ "dataDir/stages/stage1", "the manifest run's stage .ini and generator files; the stand-in stage1 has its own .ini and one default.gen" },
	// TekiMgr constructs each parameter type before CoreNode::load optionally
	// reads an override. Missing files leave the source defaults in place.
	{ "dataDir/tekipara/", "enemy parameter overrides, using source constructor defaults", ".bin" },
	// Silent when missing: CoreNode::load and the null-checked opens keep the
	// code defaults (see standin-open-allowlist.txt for each).
	{ "dataDir/parms/sound.bin", "sound constants, skipped when missing" },
	{ "dataDir/camera/camepara.bin", "camera parameters, skipped when missing" },
	{ "dataDir/ghost/record.gst", "ghost replay, null-checked" },
	// Loose .dck files the game opens only when a part's bundle lacks an entry
	// (FAILED in a manifest too). The stand-in bundle has every entry.
	{ "dataDir/objects/ufoparts/mnaka1/", "loose part animations, opened only when the bundle lacks them" },
	// The stand-in .cin files are the code's paths; their scenes have authored
	// names (cinemas/standin/, standin_cinema.cpp), so no manifest scene name is
	// expected in the tree.
	{ "dataDir/cinemas/", "stand-in scenes have authored names under cinemas/standin/", ".dsk" },
};

int checkManifest(const Tree& tree, const std::string& manifestPath)
{
	std::ifstream in(manifestPath);
	if (!in) {
		std::fprintf(stderr, "[standin] cannot read %s\n", manifestPath.c_str());
		return 1;
	}
	int missing = 0, allowed = 0, present = 0;
	std::set<std::string> listed;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		const std::string path = line.substr(0, line.find('\t'));
		listed.insert(path);
		if (tree.count(path)) {
			present++;
			continue;
		}
		const Allowed* hit = nullptr;
		for (const Allowed& a : kManifestAllowlist) {
			if (path.rfind(a.prefix, 0) != 0) continue;
			if (a.suffix && (path.size() < std::strlen(a.suffix) || path.compare(path.size() - std::strlen(a.suffix), std::string::npos, a.suffix) != 0)) continue;
			hit = &a;
		}
		if (hit) {
			allowed++;
			continue;
		}
		std::fprintf(stderr, "[standin] manifest path not in the tree and not allowlisted: %s\n", path.c_str());
		missing++;
	}
	int extra = 0;
	for (const auto& kv : tree) extra += listed.count(kv.first) ? 0 : 1;
	std::printf("[standin] manifest: %d in the tree, %d allowlisted, %d missing; %d tree files the manifest run does not open (sandbox stage, stand-in parameter files)\n", present, allowed, missing, extra);
	return missing == 0 ? 0 : 1;
}

bool writeTree(const Tree& tree, const fs::path& out, std::string& error)
{
	std::error_code ec;
	// Only the generated dataDir is replaced, so a stale file from an older
	// generator never survives into a run.
	fs::remove_all(out / "assets" / "dataDir", ec);
	for (const auto& [path, bytes] : tree) {
		const fs::path file = out / "assets" / path;
		fs::create_directories(file.parent_path(), ec);
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		if (!f) return fail(error, "cannot write " + file.string());
		f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
		if (!f) return fail(error, "cannot write " + file.string());
	}
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	std::string out, manifest;
	bool manifestOnly = false;
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--manifest-from-env" && argc == 2) {
			const char* env = std::getenv("PIKMIN_BOOT_MANIFEST");
			if (!env || !*env) {
				std::printf("[standin] PIKMIN_BOOT_MANIFEST is not set: skipping the local boot-manifest check\n");
				return 77;
			}
			manifest     = env;
			manifestOnly = true;
		} else if (a == "--manifest" && i + 1 < argc) {
			manifest = argv[++i];
		} else if (out.empty() && !a.empty() && a[0] != '-') {
			out = a;
		} else {
			out.clear();
			break;
		}
	}
	if (out.empty() && !manifestOnly) {
		std::fprintf(stderr, "usage: nectar_standin_generator <out> [--manifest <boot-manifest.txt>] | --manifest-from-env\n");
		return 2;
	}

	std::string error;
	Tree first, second;
	if (!buildTree(first, error) || !buildTree(second, error)) {
		std::fprintf(stderr, "[standin] %s\n", error.c_str());
		return 1;
	}
	const std::uint64_t h1 = hashTree(first), h2 = hashTree(second);
	if (h1 != h2 || first != second) {
		std::fprintf(stderr, "[standin] two builds of the tree differ (%016llx vs %016llx)\n", (unsigned long long)h1, (unsigned long long)h2);
		return 1;
	}
	std::size_t bytes = 0;
	for (const auto& kv : first) bytes += kv.second.size();
	std::printf("[standin] %zu files, %zu bytes, hash %016llx (two builds identical)\n", first.size(), bytes, (unsigned long long)h1);

	if (!manifest.empty() && checkManifest(first, manifest) != 0) return 1;
	if (manifestOnly) return 0;

	if (!writeTree(first, out, error)) {
		std::fprintf(stderr, "[standin] %s\n", error.c_str());
		return 1;
	}
	std::printf("[standin] wrote %s/assets/dataDir\n", out.c_str());
	return 0;
}
