// Disc-free scenario saves for the progression repair (issue #192).
//
// Each fixture in scenarios/ is a decoded save plus the stage inventory a built
// stage would report, written in a small text format with no game data in it.
// The tests drive the same adapters the game uses, in the same order:
//
//   parse (load adapter) -> global phase -> stage phase against a fake world
//   -> normal day-end save (serialize) -> reload -> repair again
//
// and assert the exact flag, count and population differences, that nothing
// grows from a second pass, that quitting before the save repairs the same way
// again, and that the backup runs before a repaired save and blocks it if it fails.
//
// Not covered here: the PlayerState and stage-object code that fills these
// records inside the game. That needs a stage with assets; the pure planner and
// these adapters are covered, and the in-game glue is compiled on every platform.

#include "pc_progression_backup.h"
#include "pc_progression_saved.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>

#ifndef PC_PROGRESSION_SCENARIO_DIR
#error "PC_PROGRESSION_SCENARIO_DIR must point at the scenarios folder"
#endif

using namespace pc::progression;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;
int gChecks   = 0;

void check(bool ok, const std::string& what)
{
	++gChecks;
	if (!ok) {
		++gFailures;
		std::fprintf(stderr, "FAIL: %s\n", what.c_str());
	}
}

std::string readFile(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ScenarioRecord load(const std::string& name)
{
	ScenarioRecord rec;
	std::string error;
	const fs::path path = fs::path(PC_PROGRESSION_SCENARIO_DIR) / (name + ".scn");
	const std::string text = readFile(path);
	check(!text.empty(), "fixture exists: " + name);
	check(parseScenario(text, rec, error), "fixture parses: " + name + " " + error);
	return rec;
}

// A stage built from a fixture. It executes requests the way the game would: a seed
// takes a pool slot and flies first, parts and Onions come back as sources and objects.
struct FakeWorld : ActionExecutor {
	WorldInventory world;
	std::array<int, kColours> flying {};
	int seedsBorn = 0;

	bool execute(const Action& a) override
	{
		switch (a.kind) {
		case Kind::ReplaceSeed:
			if (!world.seedCapacity) return false;
			world.colours[a.colour].sprouts += 1;
			flying[a.colour] += 1;
			++seedsBorn;
			return true;
		case Kind::RestoreOnionObject: world.colours[a.colour].onionPresent = true; return true;
		case Kind::RestorePartSource:
			world.parts[a.part].source = Source::Loose;
			world.parts[a.part].copies = 1;
			return true;
		case Kind::MovePartToAnchor: world.parts[a.part].source = Source::Loose; return true;
		case Kind::CollapsePartDuplicates: world.parts[a.part].copies = 1; return true;
		case Kind::SuppressPartReplica: world.parts[a.part].replicas = 0; return true;
		case Kind::RebuildObstacleRoute:
		case Kind::NormalizeObstacle:
			for (Obstacle& o : world.obstacles) {
				if (int(o.kind) * 1000 + o.id == a.value) {
					if (a.kind == Kind::RebuildObstacleRoute) o.routeOpen = o.physicalDone;
					else o.stalled = false;
				}
			}
			return true;
		case Kind::FixLimitedSource: world.generation.fault = GenFault::None; return true;
		default: return true; // factual assignments are applied to the record by the caller
		}
	}
};

struct Outcome {
	SavedProgression saved;
	FakeWorld world;
	RepairResult global;
	RepairResult stage;
	std::vector<Action> applied;
};

// Global phase at load, then the stage phase once the stage is built.
Outcome loadAndRepair(const ScenarioRecord& rec)
{
	Outcome o;
	o.saved = rec.saved;
	o.world.world = rec.world;
	for (int c = 0; c < kColours; ++c) o.world.world.colours[c].sprouts += rec.saved.colours[c].stageSprouts; // restored when the stage is built

	o.global = repairToFixedPoint(buildSnapshot(o.saved, nullptr));
	applyAllToSaved(o.saved, o.global.applied);
	const ProgressionSnapshot built = buildSnapshot(o.saved, &o.world.world);
	o.stage                         = repairToFixedPoint(built, &o.world);
	applyAllToSaved(o.saved, o.stage.applied);
	o.applied = o.global.applied;
	o.applied.insert(o.applied.end(), o.stage.applied.begin(), o.stage.applied.end());
	return o;
}

int appliedOf(const Outcome& o, Kind k, int colour = -2)
{
	int n = 0;
	for (const Action& a : o.applied) n += a.kind == k && (colour == -2 || a.colour == colour);
	return n;
}

std::set<std::string> keysOf(const Outcome& o)
{
	std::set<std::string> k;
	for (const Action& a : o.applied) k.insert(a.key);
	return k;
}

// The normal day-end save, then the next load. Seeds still flying at day end are not saved.
ScenarioRecord saveAndReload(const Outcome& o, const ScenarioRecord& original, bool flyingSaved)
{
	ScenarioRecord next;
	std::string error;
	const std::string text = serializeSaved(o.saved);
	check(parseScenario(text, next, error), "the saved text parses back: " + error);
	check(serializeSaved(next.saved) == text, "save then load round-trips exactly");
	next.hasWorld = true;
	next.world    = o.world.world;
	for (int c = 0; c < kColours; ++c) {
		const int landed = o.world.world.colours[c].sprouts - (flyingSaved ? 0 : o.world.flying[c]);
		next.saved.colours[c].stageSprouts = landed;
		next.world.colours[c].sprouts      = 0; // the next stage build re-adds the serialized sprouts
		next.world.colours[c].pending      = 0;
	}
	(void)original;
	return next;
}

void expectUnchanged(const std::string& name)
{
	const ScenarioRecord rec = load(name);
	const Outcome o          = loadAndRepair(rec);
	check(o.applied.empty(), name + ": a healthy save is not changed");
	check(serializeSaved(o.saved) == serializeSaved(rec.saved), name + ": the saved record is byte-identical");
	check(o.world.seedsBorn == 0, name + ": no seed is born");
}

void testHealthyFixtures()
{
	expectUnchanged("healthy-midgame");
	expectUnchanged("healthy-pristine-tutorial");
}

void testLegacyYellowAndBlue()
{
	for (const char* name : { "legacy-yellow-first-seed", "legacy-blue-first-seed", "s12-flying-starter" }) {
		const bool yellow        = std::string(name) != "legacy-blue-first-seed";
		const int colour         = yellow ? Yellow : Blue;
		const ScenarioRecord rec = load(name);
		const Outcome o          = loadAndRepair(rec);
		check(appliedOf(o, Kind::ReplaceSeed) == 1 && appliedOf(o, Kind::ReplaceSeed, colour) == 1, std::string(name) + ": exactly one seed of the right colour");
		check(o.applied.size() == 1, std::string(name) + ": nothing else changes");
		check(!o.saved.colours[colour].unlocked && !o.saved.colours[colour].plucked, std::string(name) + ": the colour stays locked for the first pluck");
		check(o.world.seedsBorn == 1, std::string(name) + ": one seed in the world");
		check(serializeSaved(o.saved) == serializeSaved(rec.saved), std::string(name) + ": the saved record itself is unchanged until pluck");

		// The seed lands before the day ends and is saved: the next load is stable.
		const ScenarioRecord landed = saveAndReload(o, rec, true);
		const Outcome again         = loadAndRepair(landed);
		check(again.applied.empty() && again.world.seedsBorn == 0, std::string(name) + ": save and reload with the sprout adds nothing");

		// Quit before the save: the disk still holds the original, and the same repair happens.
		const Outcome crashed = loadAndRepair(rec);
		check(keysOf(crashed) == keysOf(o), std::string(name) + ": quitting before the save repeats the same repair");

		// The day ends while the seed is still flying: it is not saved, so the next load replaces it once more, never twice.
		const ScenarioRecord lost = saveAndReload(o, rec, false);
		const Outcome second      = loadAndRepair(lost);
		check(second.world.seedsBorn == 1, std::string(name) + ": a seed lost to a flying day end is replaced exactly once");
		const Outcome third = loadAndRepair(saveAndReload(second, rec, true));
		check(third.applied.empty(), std::string(name) + ": and then it is stable");
	}
}

void testWhistleCases()
{
	for (const char* name : { "whistle-yellow", "whistle-blue" }) {
		const int colour         = std::string(name) == "whistle-yellow" ? Yellow : Blue;
		const ScenarioRecord rec = load(name);
		const Outcome o          = loadAndRepair(rec);
		check(o.saved.colours[colour].unlocked && o.saved.colours[colour].displayed && o.saved.colours[colour].plucked, std::string(name) + ": unlock, display and first pluck restored");
		check(appliedOf(o, Kind::ReplaceSeed) == 0 && o.world.seedsBorn == 0, std::string(name) + ": no seed, the Pikmin already exist");
		check(o.saved.currParts == 8, std::string(name) + ": counters untouched");
		const Outcome again = loadAndRepair(saveAndReload(o, rec, true));
		check(again.applied.empty(), std::string(name) + ": stable after save and reload");
	}
}

void testRedTutorialLoss()
{
	const ScenarioRecord rec = load("red-tutorial-loss");
	const Outcome o          = loadAndRepair(rec);
	check(appliedOf(o, Kind::ReplaceSeed, Red) == 1 && o.applied.size() == 1, "red starter replaced, nothing else");
	check(o.saved.tutorialMode == rec.saved.tutorialMode, "the tutorial state is not forced");
}

void testK1()
{
	const ScenarioRecord rec = load("k1-skipped-yellow-and-first-part");
	const Outcome o          = loadAndRepair(rec);
	check(appliedOf(o, Kind::ReplaceSeed, Yellow) == 1, "K1: one yellow seed");
	check(o.saved.currParts == 1 && o.saved.upgradeLevel == 1 && (o.saved.courseOpen & (1 << StageForest)), "K1: part count, level and Forest of Hope from the real part ID");
	check(o.saved.partVis[kPartMainEngine] == 0, "K1: the engine is not assumed owned");
	check(o.saved.collectEngine, "K1: the first-delivery marker is left alone when another part is owned");
	check(o.saved.colours[Yellow].unlocked == false, "K1: yellow stays locked");
	const Outcome again = loadAndRepair(saveAndReload(o, rec, true));
	check(again.applied.empty(), "K1: stable after save and reload");
}

void testK2()
{
	const ScenarioRecord rec = load("k2-yellow-and-blue");
	const Outcome o          = loadAndRepair(rec);
	check(o.applied.size() == 2 && appliedOf(o, Kind::ReplaceSeed, Yellow) == 1 && appliedOf(o, Kind::ReplaceSeed, Blue) == 1, "K2: one seed of each colour and no early return");
	check(!o.saved.colours[Yellow].unlocked && !o.saved.colours[Blue].unlocked, "K2: neither colour is unlocked");
	check(loadAndRepair(saveAndReload(o, rec, true)).applied.empty(), "K2: stable after save and reload");
}

void testK6()
{
	const ScenarioRecord rec = load("k6-counters-areas-tutorial");
	const Outcome o          = loadAndRepair(rec);
	check(o.saved.currParts == 12 && o.saved.requiredParts == 12 && o.saved.upgradeLevel == 3, "K6: counters and level rebuilt");
	check(o.saved.courseOpen == 0x0F, "K6: every earned lower area, Hope, Navel and Spring");
	check(!o.saved.tutorialMode, "K6: stale tutorial flag cleared");
	check(o.global.applied.size() >= 5, "K6: the global phase repairs counters and access before any stage is built");
	check(loadAndRepair(saveAndReload(o, rec, true)).applied.empty(), "K6: stable after save and reload");
}

void testK7()
{
	const ScenarioRecord rec = load("k7-cache-carrier-visit");
	const Outcome o          = loadAndRepair(rec);
	check(appliedOf(o, Kind::RestorePartSource) == 2, "K7: one source per lost part");
	check(o.world.world.parts[16].copies == 1 && o.world.world.parts[17].copies == 1, "K7: a single source each, carrier not revived on top of the drop");
	check(o.saved.currParts == 8, "K7: nothing is awarded");
	ScenarioRecord unresolved = rec;
	unresolved.world.generation.sourceSetResolved = false;
	const Outcome blocked = loadAndRepair(unresolved);
	check(appliedOf(blocked, Kind::RestorePartSource) == 0, "K7: no restore before one source set is chosen");
}

void testK8()
{
	const ScenarioRecord rec = load("k8-seed-wall-bridge");
	const Outcome o          = loadAndRepair(rec);
	check(appliedOf(o, Kind::ReplaceSeed, Blue) == 1 && appliedOf(o, Kind::RebuildObstacleRoute) == 2, "K8: seed plus two rebuilt routes");
	check(o.world.world.obstacles[0].routeOpen && o.world.world.obstacles[1].routeOpen, "K8: finished wall and bridge routes are open");
	check(!o.world.world.obstacles[2].routeOpen && !o.world.world.obstacles[3].routeOpen, "K8: intact wall and unfinished bridge unchanged");
}

void testK10()
{
	const ScenarioRecord rec = load("k10-randomizer-extinct");
	const Outcome o          = loadAndRepair(rec);
	check(appliedOf(o, Kind::ReplaceSeed) == 2, "K10: both extinct colours get a seed despite the manifest problem");
	check(o.saved.currParts == 9 && o.saved.upgradeLevel == 2, "K10: counters rebuilt from the owned parts");
	check(appliedOf(o, Kind::RestorePartSource) == 0, "K10: no speculative part restoration");
	bool diagnosed = false;
	for (const Action& a : o.stage.residual.actions) diagnosed |= a.reason == Reason::ManifestUnsupported;
	check(diagnosed, "K10: the unsupported layout is diagnosed");
}

void testK12()
{
	const ScenarioRecord rec = load("k12-day29-final-trial");
	const Outcome o          = loadAndRepair(rec);
	check(o.saved.currParts == 29 && o.saved.upgradeLevel == 4 && (o.saved.courseOpen & (1 << StageLast)), "K12: truthful counts and the Final Trial");
	check(o.saved.courseOpen == 0x1F, "K12: all lower areas as well");
	check(appliedOf(o, Kind::ReplaceSeed, Blue) == 1, "K12: the blue seed");
	check(o.saved.day == 29 && o.saved.partVis[19] == 1 && o.saved.partVis[29] == 0, "K12: no extra day and no unearned part");
}

// ---------------------------------------------------------------------------
// Parse errors and unknown sections: refused, never guessed.

void testMalformed()
{
	ScenarioRecord rec;
	std::string error;
	check(!parseScenario("[saved]\nday = x\n", rec, error) && error.find("line 2") != std::string::npos, "a bad number is rejected with its line");
	check(!parseScenario("[saved]\nowned = zzzz\n", rec, error), "an unknown part ID is rejected");
	check(!parseScenario("[saved]\nflux = 1\n", rec, error), "an unknown key is rejected");
	check(!parseScenario("[saved]\nno equals sign\n", rec, error), "a line without a key is rejected");
	check(!parseScenario("[world]\npart.ust1 = melted\n", rec, error), "an unknown part source is rejected");
	check(parseScenario("# only a comment\n", rec, error), "an empty file is a valid empty record");

	// A save that failed its structure check gets no repair: invalid input is diagnosed only.
	ScenarioRecord bad = load("legacy-yellow-first-seed");
	bad.saved.prov.formatValid = false;
	const Outcome o = loadAndRepair(bad);
	check(o.applied.empty() && o.world.seedsBorn == 0, "an invalid decode is never repaired from zero-based inference");
}

// ---------------------------------------------------------------------------
// Backup and the save boundary.

fs::path scratch(const char* name)
{
	const fs::path dir = fs::temp_directory_path() / (std::string("pc_progression_") + name + "_" + std::to_string(std::rand()));
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	return dir;
}

void testBackup()
{
	const fs::path root = scratch("backup");
	const std::string image(0x8000, 'x');

	const BackupOutcome first = backupSaveImage(root, 1, 6, image.data(), image.size());
	check(first.result == BackupResult::Written && fs::exists(first.path), "the backup is written");
	check(readFile(first.path) == image, "the backup is byte-identical to the loaded save");
	check(first.path.parent_path().filename() == "progression-repair", "the backup lives in its own folder");

	const BackupOutcome same = backupSaveImage(root, 1, 6, image.data(), image.size());
	check(same.result == BackupResult::AlreadyBackedUp && same.path == first.path, "an identical backup is reused");

	std::string other = image;
	other[10]         = 'y';
	const BackupOutcome second = backupSaveImage(root, 1, 6, other.data(), other.size());
	check(second.result == BackupResult::Written && second.path != first.path && readFile(first.path) == image, "a different save never overwrites an earlier backup");

	check(backupSaveImage(root, 1, 6, nullptr, 0).result == BackupResult::Failed, "an empty image is refused");

	// The folder cannot be created because a file is in the way.
	const fs::path blocked = scratch("blocked");
	{ std::ofstream(blocked / "progression-repair") << "in the way"; }
	check(backupSaveImage(blocked, 1, 6, image.data(), image.size()).result == BackupResult::Failed, "an unwritable backup folder fails");

	std::error_code ec;
	fs::remove_all(root, ec);
	fs::remove_all(blocked, ec);
}

void testSaveBoundary()
{
	const fs::path root = scratch("boundary");
	const std::string image(0x8000, 'a');

	PendingRepair pending;
	std::string error;
	check(pending.beforeSave(root, &error) && !fs::exists(root / "progression-repair"), "nothing pending: the save proceeds and writes no backup");

	pending.rememberLoadedImage(image.data(), image.size());
	check(pending.beforeSave(root, &error) && !fs::exists(root / "progression-repair"), "a load with no repair writes no backup");

	pending.markRepaired(2, 7);
	check(pending.active(), "a repair is pending");
	check(pending.beforeSave(root, &error), "the backup is made first");
	check(readFile(pending.lastBackupPath()) == image, "and it holds the file as it was loaded");
	pending.afterSave(false);
	check(pending.active(), "a failed save keeps the repair pending");
	check(pending.beforeSave(root, &error), "a retry does not need a second backup");
	pending.afterSave(true);
	check(!pending.active(), "a successful save commits the repair");

	// A backup that cannot be written refuses the save.
	const fs::path blocked = scratch("boundary_blocked");
	{ std::ofstream(blocked / "progression-repair") << "in the way"; }
	PendingRepair failing;
	failing.rememberLoadedImage(image.data(), image.size());
	failing.markRepaired(1, 3);
	check(!failing.beforeSave(blocked, &error) && !error.empty(), "a failed backup refuses the save");
	check(failing.active(), "and the repair stays pending");

	// A save that succeeded replaced the file on the card: a later repair backs up that file.
	{
		const fs::path later = scratch("later");
		const std::string newer(0x8000, 'b');
		PendingRepair p;
		p.rememberLoadedImage(image.data(), image.size());
		p.markRepaired(1, 4);
		check(p.beforeSave(later, &error), "first backup");
		p.afterSave(true);
		p.replaceImage(newer.data(), newer.size());
		p.markRepaired(1, 5);
		check(p.beforeSave(later, &error) && readFile(p.lastBackupPath()) == newer, "a second repair backs up the file the last save wrote");
		p.reset();
		check(!p.active(), "a new game resets the pending repair");
		std::error_code ec2;
		fs::remove_all(later, ec2);
	}

	// Loading a different save forgets the pending repair.
	failing.rememberLoadedImage(image.data(), image.size());
	check(!failing.active(), "a new load clears a pending repair");

	std::error_code ec;
	fs::remove_all(root, ec);
	fs::remove_all(blocked, ec);
}

// ---------------------------------------------------------------------------
// Plan hashes: the same decisions on every platform.

void testHashes()
{
	struct Row {
		const char* name;
		std::uint64_t expected;
	};
	const Row rows[] = {
		{ "healthy-midgame", 0x649121a8022394faull },
		{ "legacy-yellow-first-seed", 0x12db5c765694ffa2ull },
		{ "k2-yellow-and-blue", 0x063b0b6cf1c1859full },
		{ "k6-counters-areas-tutorial", 0x67c0a9db88d0f884ull },
		{ "k8-seed-wall-bridge", 0xd2675e54c85eefacull },
		{ "k12-day29-final-trial", 0x9c8ee70829db0021ull },
	};
	for (const Row& row : rows) {
		const ScenarioRecord rec = load(row.name);
		const RepairPlan global  = planRepairs(buildSnapshot(rec.saved, nullptr));
		WorldInventory world     = rec.world;
		for (int c = 0; c < kColours; ++c) world.colours[c].sprouts += rec.saved.colours[c].stageSprouts;
		const RepairPlan stage = planRepairs(buildSnapshot(rec.saved, &world));
		const std::uint64_t h  = planHash(global) * 1099511628211ull ^ planHash(stage);
		std::printf("scenario hash %-32s %016llx\n", row.name, static_cast<unsigned long long>(h));
		if (row.expected) check(h == row.expected, std::string("plan hash is the same on every platform: ") + row.name);
	}
}

} // namespace

int main()
{
	testHealthyFixtures();
	testLegacyYellowAndBlue();
	testWhistleCases();
	testRedTutorialLoss();
	testK1();
	testK2();
	testK6();
	testK7();
	testK8();
	testK10();
	testK12();
	testMalformed();
	testBackup();
	testSaveBoundary();
	testHashes();
	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
