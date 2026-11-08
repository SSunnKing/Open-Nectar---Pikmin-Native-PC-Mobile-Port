// Table-driven tests for the progression repair decision function (issue #192 spec):
// every scenario S01-S32, every combination K1-K13, pairwise combinations of
// disjoint faults, threshold edges, population and source matrices, and the
// fixed-point properties (empty plan on healthy, idempotence, shuffle order,
// failed allocations). Header-only logic plus one .cpp: no game, disc or assets.
//
// Labels: C = confirmed historical or reachable mechanism (legacy fixture),
// T = theoretical inconsistent state (synthetic until a producer is shown).

#include "pc_progression_repair.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace pc::progression;

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

// ---------------------------------------------------------------------------
// Helpers.

ProgressionSnapshot stage()
{
	ProgressionSnapshot s = makeHealthySnapshot();
	s.world               = Known::Materialized;
	s.population          = Known::Materialized;
	s.sourceInventoryComplete = true;
	s.catalogueCompatible = true;
	return s;
}

ProgressionSnapshot globalPhase() { return makeHealthySnapshot(); }

// A yellow or blue Onion that was booted and discovered but never gave its first seed.
void firstSeedLost(ProgressionSnapshot& s, int c)
{
	ColourState& cs = s.colours[c];
	cs             = ColourState {};
	cs.booted = cs.discovered = true;
	cs.onionPresent = cs.onionExpected = true;
}

void ownFirst(ProgressionSnapshot& s, int n, bool consistent)
{
	for (int i = 0; i < kMaxParts; ++i) {
		s.parts[i].vis    = i < n ? Vis::Visible : Vis::Uncollected;
		s.parts[i].copies = i < n ? 0 : 1;
		s.parts[i].source = Source::Loose;
	}
	if (consistent) {
		s.currParts     = s.ownedCount();
		s.requiredParts = s.ownedRequiredCount();
	}
}

struct Want {
	Kind kind;
	int colour = -1;
	int part   = -1;
	int value  = -1; // -1 = any
};

bool matches(const Action& a, const Want& w)
{
	return a.kind == w.kind && a.colour == w.colour && a.part == w.part && (w.value < 0 || a.value == w.value);
}

bool hasRepair(const RepairPlan& p, const Want& w)
{
	for (const Action& a : p.actions) {
		if (a.disposition == Disposition::Repair && matches(a, w)) return true;
	}
	return false;
}
bool hasDisposition(const RepairPlan& p, Disposition d, Reason r)
{
	for (const Action& a : p.actions) {
		if (a.disposition == d && a.reason == r) return true;
	}
	return false;
}
bool hasScenario(const RepairPlan& p, int scenario)
{
	for (const Action& a : p.actions) {
		if (a.scenario == scenario) return true;
	}
	return false;
}

std::set<std::string> appliedKeys(const RepairResult& r)
{
	std::set<std::string> k;
	for (const Action& a : r.applied) k.insert(a.key);
	return k;
}

bool appliedHas(const RepairResult& r, const Want& w)
{
	for (const Action& a : r.applied) {
		if (matches(a, w)) return true;
	}
	return false;
}

int appliedCount(const RepairResult& r, Kind k, int colour = -2)
{
	int n = 0;
	for (const Action& a : r.applied) n += a.kind == k && (colour == -2 || a.colour == colour);
	return n;
}

// Second run on the repaired snapshot must change nothing.
void checkIdempotent(const std::string& name, const RepairResult& r)
{
	const RepairResult again = repairToFixedPoint(r.final);
	check(again.applied.empty(), name + ": a second run applies nothing");
	check(!planRepairs(r.final).hasRepairs(), name + ": the repaired snapshot plans no repair");
	check(r.stable, name + ": the loop ended stable");
}

// ---------------------------------------------------------------------------
// Healthy snapshots: no change.

void testHealthy()
{
	check(planRepairs(globalPhase()).empty(), "healthy global snapshot has an empty plan");
	check(planRepairs(stage()).empty(), "healthy stage snapshot has an empty plan");

	struct Healthy {
		const char* name;
		std::function<void(ProgressionSnapshot&)> mutate;
	};
	const Healthy table[] = {
		{ "pristine undiscovered tutorial", [](ProgressionSnapshot& s) {
			 s = stage();
			 ownFirst(s, 0, true);
			 s.upgradeLevel = 0;
			 s.shipEffect   = 8;
			 s.courseOpen   = 1;
			 s.collectEngineFlag = false;
			 s.tutorialMode = true;
			 s.day          = 1;
			 for (ColourState& c : s.colours) c = ColourState {};
			 s.colours[Red].unlocked = s.colours[Red].displayed = true;
		 } },
		{ "undiscovered yellow and blue despite open areas", [](ProgressionSnapshot& s) { s.courseOpen |= 1 << StageYakushima; } },
		{ "discovered Onion with first sprout left overnight", [](ProgressionSnapshot& s) {
			 firstSeedLost(s, Yellow);
			 s.colours[Yellow].sproutsLoaded = 1;
		 } },
		{ "off-stage sprout only", [](ProgressionSnapshot& s) {
			 firstSeedLost(s, Blue);
			 s.colours[Blue].sproutsOffStage = 1;
		 } },
		{ "normal extinction with the queued BonusPiki", [](ProgressionSnapshot& s) {
			 s.colours[Yellow].stored = 0;
			 s.colours[Yellow].bonusPathOpen = true;
		 } },
		{ "first-part flag with the delivery pending", [](ProgressionSnapshot& s) {
			 ownFirst(s, 0, true);
			 s.upgradeLevel = 0;
			 s.shipEffect   = 8;
			 s.courseOpen   = 1;
			 s.deliveryPending = true;
		 } },
		{ "skipped part text, ownership correct", [](ProgressionSnapshot& s) { s.collectEngineFlag = false; } },
		{ "underweight army with growth available", [](ProgressionSnapshot& s) {
			 s.colours[Red].stored = 3;
			 s.colours[Yellow].stored = 0;
			 s.colours[Yellow].sproutsLoaded = 2;
		 } },
		{ "intact wall, unfinished bridge, unpushed box", [](ProgressionSnapshot& s) {
			 s.obstacles = { { ObstacleKind::Wall, 1, true, false, false, false },
				             { ObstacleKind::Bridge, 2, true, false, false, false },
				             { ObstacleKind::Box, 3, true, false, false, false } };
		 } },
		{ "finished wall with its route open", [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Wall, 1, true, true, true, false } }; } },
		{ "legitimately delayed generator part", [](ProgressionSnapshot& s) { s.parts[10].source = Source::FutureGenerator; } },
		{ "carrier alive with its part", [](ProgressionSnapshot& s) { s.parts[11].source = Source::CarrierAlive; } },
		{ "absorbing part", [](ProgressionSnapshot& s) { s.parts[12].source = Source::Absorbing; } },
		{ "healthy Randomizer", [](ProgressionSnapshot& s) { s.randomizer.active = true; } },
		{ "day 30 with few parts is an honest loss", [](ProgressionSnapshot& s) { s.day = 30; } },
		{ "co-op story", [](ProgressionSnapshot& s) { s.mode = Mode::CoopStory; } },
		{ "all demo flags synthetic world", [](ProgressionSnapshot& s) {
			 s.mode = Mode::Debug;
			 firstSeedLost(s, Yellow);
		 } },
	};
	for (const Healthy& h : table) {
		ProgressionSnapshot s = stage();
		h.mutate(s);
		const RepairPlan p = planRepairs(s);
		check(p.empty(), std::string("healthy: ") + h.name + " gets no plan");
		check(repairToFixedPoint(s).applied.empty(), std::string("healthy: ") + h.name + " applies nothing");
	}
}

// ---------------------------------------------------------------------------
// Scenario table S01-S32.

struct Scenario {
	const char* name;
	int id;
	char label; // 'C' or 'T'
	std::function<void(ProgressionSnapshot&)> make;
	std::vector<Want> repairs;       // must be proposed in the first plan
	std::vector<Want> after;         // must be applied by the fixed point
	std::vector<Kind> never;         // kinds that must not be applied
	std::function<void(const ProgressionSnapshot&, const RepairResult&)> extra;
	bool global = false;             // run in the global phase instead
	int labelAs = 0;                 // the label the shared detector reports (S12 shares S01-S03's, S16's flags dedupe into S03's)
};

std::vector<Scenario> scenarios()
{
	std::vector<Scenario> t;

	t.push_back({ "S01 yellow first seed missing", 1, 'C', [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); },
	              { { Kind::ReplaceSeed, Yellow } }, {}, { Kind::SetUnlocked, Kind::SetPlucked }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(appliedCount(r, Kind::ReplaceSeed, Yellow) == 1, "S01: exactly one yellow seed");
		              check(!r.final.colours[Yellow].unlocked && !r.final.colours[Yellow].plucked, "S01: the unlock and first pluck stay for the player");
		              check(r.final.colours[Yellow].pending == 1, "S01: the seed is reserved");
	              } });
	t.push_back({ "S02 blue first seed missing", 2, 'C', [](ProgressionSnapshot& s) { firstSeedLost(s, Blue); }, { { Kind::ReplaceSeed, Blue } }, {},
	              { Kind::SetUnlocked, Kind::SetPlucked }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(!r.final.colours[Blue].unlocked, "S02: blue stays locked");
		              check(appliedCount(r, Kind::ReplaceSeed) == 1, "S02: red and yellow get nothing");
	              } });
	t.push_back({ "S03 red starter missing", 3, 'T', [](ProgressionSnapshot& s) {
		              ColourState& c = s.colours[Red];
		              c              = ColourState {};
		              c.unlocked = c.displayed = c.discovered = c.booted = true;
		              c.onionPresent = c.onionExpected = true;
	              }, { { Kind::ReplaceSeed, Red } }, {}, { Kind::SetPlucked }, nullptr });
	t.push_back({ "S03 red baseline unlock bit missing", 3, 'T', [](ProgressionSnapshot& s) {
		              s.colours[Red].unlocked  = false;
		              s.colours[Red].displayed = false;
	              }, { { Kind::SetUnlocked, Red }, { Kind::SetDisplayed, Red } }, {}, {}, nullptr });
	t.push_back({ "S04 discovery recorded, boot never happened", 4, 'T', [](ProgressionSnapshot& s) {
		              ColourState& c = s.colours[Blue];
		              c              = ColourState {};
		              c.discovered = true;
		              c.onionPresent = c.onionExpected = true;
	              }, { { Kind::SetBooted, Blue } }, { { Kind::SetBooted, Blue }, { Kind::ReplaceSeed, Blue } }, { Kind::SetUnlocked }, nullptr });
	t.push_back({ "S05 whistle-plucked yellow without unlock", 5, 'C', [](ProgressionSnapshot& s) {
		              ColourState& c = s.colours[Yellow];
		              c              = ColourState {};
		              c.booted = c.discovered = true;
		              c.live  = 3;
		              c.onionPresent = c.onionExpected = true;
	              }, { { Kind::SetUnlocked, Yellow }, { Kind::SetDisplayed, Yellow }, { Kind::SetPlucked, Yellow } }, {}, { Kind::ReplaceSeed },
	              [](const ProgressionSnapshot&, const RepairResult& r) { check(r.final.colours[Yellow].unlocked, "S05: yellow unlocked"); } });
	t.push_back({ "S06 whistle-plucked blue without unlock", 6, 'C', [](ProgressionSnapshot& s) {
		              ColourState& c = s.colours[Blue];
		              c              = ColourState {};
		              c.booted = c.discovered = true;
		              c.stored = 2;
		              c.onionPresent = c.onionExpected = true;
	              }, { { Kind::SetUnlocked, Blue }, { Kind::SetDisplayed, Blue }, { Kind::SetPlucked, Blue } }, {}, { Kind::ReplaceSeed }, nullptr });
	t.push_back({ "S07 pluck flag and unlock disagree", 7, 'T', [](ProgressionSnapshot& s) {
		              ColourState& c = s.colours[Yellow];
		              c.displayed    = false;
	              }, { { Kind::SetDisplayed, Yellow } }, {}, { Kind::SetUnlocked }, nullptr });
	t.push_back({ "S08 acquired Onion lost its boot state", 8, 'T', [](ProgressionSnapshot& s) {
		              s.colours[Yellow].booted = false;
	              }, { { Kind::SetBooted, Yellow } }, {}, { Kind::ReplaceSeed }, nullptr });
	t.push_back({ "S08 booted Onion lost its discovery flag", 8, 'T', [](ProgressionSnapshot& s) { s.colours[Yellow].discovered = false; },
	              { { Kind::SetDiscovered, Yellow } }, {}, {}, nullptr });
	t.push_back({ "S09 earned Onion object missing", 9, 'T', [](ProgressionSnapshot& s) { s.colours[Red].onionPresent = false; },
	              { { Kind::RestoreOnionObject, Red } }, {}, { Kind::ReplaceSeed }, nullptr });
	t.push_back({ "S10 unlocked colour extinct, bonus seed missing", 10, 'T', [](ProgressionSnapshot& s) { s.colours[Yellow].stored = 0; },
	              { { Kind::ReplaceSeed, Yellow } }, {}, {}, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(appliedCount(r, Kind::ReplaceSeed) == 1, "S10: one seed, not one per colour");
	              } });
	t.push_back({ "S11 total extinction with missing ownership", 11, 'T', [](ProgressionSnapshot& s) {
		              for (int c : { int(Red), int(Yellow) }) {
			              ColourState& cs = s.colours[c];
			              cs              = ColourState {};
			              cs.discovered = cs.plucked = true;
			              cs.onionPresent = cs.onionExpected = true;
		              }
	              }, { { Kind::SetUnlocked, Red }, { Kind::SetUnlocked, Yellow } }, { { Kind::SetBooted, Red }, { Kind::SetBooted, Yellow }, { Kind::ReplaceSeed, Red },
	                                                                                     { Kind::ReplaceSeed, Yellow } },
	              {}, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(!r.final.colours[Blue].unlocked && !r.final.colours[Blue].discovered, "S11: undiscovered blue stays undiscovered");
		              check(appliedCount(r, Kind::ReplaceSeed) == 2, "S11: one seed per acquired colour");
	              } });
	t.push_back({ "S12 flying starter excluded at the day-end save", 12, 'T', [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); },
	              { { Kind::ReplaceSeed, Yellow } }, {}, {}, nullptr, false, 1 });
	t.push_back({ "S13 rescue blocked by a full seed pool", 13, 'T', [](ProgressionSnapshot& s) {
		              firstSeedLost(s, Yellow);
		              s.seedCapacity = false;
	              }, {}, {}, { Kind::ReplaceSeed }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Defer, Reason::AllocationUnavailable), "S13: deferred, not claimed");
		              check(r.final.colours[Yellow].pending == 0, "S13: no success marker without a seed");
	              } });
	t.push_back({ "S14 first-part flag with no part behind it", 14, 'T', [](ProgressionSnapshot& s) {
		              ownFirst(s, 0, true);
		              s.upgradeLevel = 0;
		              s.shipEffect   = 8;
		              s.courseOpen   = 1;
	              }, { { Kind::ClearPrematureEngineFlag } }, {}, {}, nullptr });
	t.push_back({ "S15 engine owned but tutorial flag stale", 15, 'T', [](ProgressionSnapshot& s) { s.tutorialMode = true; },
	              { { Kind::SetTutorialComplete } }, {}, {}, nullptr, true });
	t.push_back({ "S16 partial skip-tutorial bootstrap with provenance", 16, 'T', [](ProgressionSnapshot& s) {
		              s.prov.bootstrapKnown    = true;
		              s.prov.bootstrapMismatch = true;
		              s.tutorialMode           = true;
		              ColourState& r           = s.colours[Red];
		              r.unlocked = r.displayed = r.discovered = false;
	              }, { { Kind::SetUnlocked, Red }, { Kind::SetDisplayed, Red }, { Kind::SetDiscovered, Red }, { Kind::SetTutorialComplete } }, {},
	              { Kind::ReplaceSeed, Kind::RestorePartSource }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(r.final.colours[Red].stored == 30, "S16: no Pikmin were added");
		              check(r.final.ownedCount() == 8, "S16: no part was granted");
	              }, false, 3 });
	t.push_back({ "S16 bootstrap mismatch without provenance is a diagnosis", 16, 'T', [](ProgressionSnapshot& s) { s.prov.bootstrapMismatch = true; }, {}, {}, {},
	              [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::AmbiguousHistory), "S16: ambiguity is diagnosed");
	              } });
	t.push_back({ "S17 counters disagree with owned IDs", 17, 'T', [](ProgressionSnapshot& s) {
		              s.currParts     = 0;
		              s.requiredParts = 3;
		              s.upgradeLevel  = 0;
	              }, { { Kind::SetCurrParts, -1, -1, 8 }, { Kind::SetRequiredParts, -1, -1, 8 }, { Kind::SetUpgradeLevel, -1, -1, 2 } }, {}, {}, nullptr, true });
	t.push_back({ "S18 earned areas still closed", 18, 'T', [](ProgressionSnapshot& s) {
		              ownFirst(s, 29, true);
		              s.upgradeLevel = 4;
		              s.courseOpen   = 1;
	              }, { { Kind::SetCourseOpen, -1, -1, 1 }, { Kind::SetCourseOpen, -1, -1, 2 }, { Kind::SetCourseOpen, -1, -1, 3 }, { Kind::SetCourseOpen, -1, -1, 4 } },
	              {}, {}, [](const ProgressionSnapshot&, const RepairResult& r) { check(r.final.courseOpen == 0x1F, "S18: every lower area is recovered"); }, true });
	t.push_back({ "S19 loose part dropped from a full cache", 19, 'C', [](ProgressionSnapshot& s) {
		              s.parts[10].source = Source::Missing;
		              s.parts[10].why    = MissingWhy::CacheRefused;
		              s.parts[10].copies = 0;
	              }, { { Kind::RestorePartSource, -1, 10 } }, {}, {}, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(!r.final.parts[10].owned(), "S19: the part is restored as a source, never as collected");
		              check(r.final.currParts == 8, "S19: the part counters did not move");
	              } });
	t.push_back({ "S20 uncollected direct part suppressed", 20, 'T', [](ProgressionSnapshot& s) {
		              s.parts[11].source = Source::Missing;
		              s.parts[11].why    = MissingWhy::OneShotConsumed;
		              s.parts[11].copies = 0;
	              }, { { Kind::RestorePartSource, -1, 11 } }, {}, {}, nullptr });
	t.push_back({ "S21 carrier consumed, drop absent", 21, 'T', [](ProgressionSnapshot& s) {
		              s.parts[12].source = Source::Missing;
		              s.parts[12].why    = MissingWhy::CarrierConsumed;
		              s.parts[12].copies = 0;
	              }, { { Kind::RestorePartSource, -1, 12 } }, {}, {}, nullptr });
	t.push_back({ "S22 duplicated and half absorbed identities", 22, 'T', [](ProgressionSnapshot& s) {
		              s.parts[10].copies   = 2;
		              s.parts[2].replicas  = 1;
	              }, { { Kind::CollapsePartDuplicates, -1, 10 }, { Kind::SuppressPartReplica, -1, 2 } }, {}, {}, nullptr });
	t.push_back({ "S23 only copy out of bounds", 23, 'T', [](ProgressionSnapshot& s) { s.parts[13].source = Source::OutOfBounds; },
	              { { Kind::MovePartToAnchor, -1, 13 } }, {}, { Kind::RestorePartSource }, nullptr });
	t.push_back({ "S24 radar and jet capability bits missing", 24, 'T', [](ProgressionSnapshot& s) {
		              ownFirst(s, 16, true);
		              s.upgradeLevel = 3;
		              s.courseOpen   = 0x0F;
		              s.shipEffect   = 8; // the tutorial camera bit must survive
	              }, { { Kind::SetShipEffectBits, -1, -1, 7 } }, {}, {}, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(r.final.shipEffect == 15, "S24: bit 8 is kept and 1,2,4 are added");
	              }, true });
	t.push_back({ "S25 destroyed wall restored closed", 25, 'T', [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Wall, 7, true, true, false, false } }; },
	              { { Kind::RebuildObstacleRoute } }, {}, {}, nullptr });
	t.push_back({ "S26 finished bridge route closed", 26, 'T', [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Bridge, 1, true, true, false, false } }; },
	              { { Kind::RebuildObstacleRoute } }, {}, {}, nullptr });
	t.push_back({ "S27 pushed box and route disagree", 27, 'T', [](ProgressionSnapshot& s) {
		              s.obstacles = { { ObstacleKind::Box, 1, true, true, false, false }, { ObstacleKind::Box, 2, true, false, false, true } };
	              }, { { Kind::RebuildObstacleRoute }, { Kind::NormalizeObstacle } }, {}, {}, nullptr });
	t.push_back({ "S28 limited source wrongly suppressed", 28, 'T', [](ProgressionSnapshot& s) {
		              s.generation.fault         = GenFault::LimitedFlagStale;
		              s.generation.limitedProven = true;
		              s.generation.stage         = StageCave;
		              s.generation.index         = 4;
	              }, { { Kind::FixLimitedSource, -1, -1, StageCave * 256 + 4 } }, {}, {}, nullptr });
	t.push_back({ "S29 Randomizer manifest mismatch", 29, 'T', [](ProgressionSnapshot& s) {
		              s.randomizer.active          = true;
		              s.randomizer.manifestMatches = false;
		              s.parts[10].source           = Source::Missing;
		              s.parts[10].copies           = 0;
		              firstSeedLost(s, Blue);
	              }, { { Kind::ReplaceSeed, Blue } }, {}, { Kind::RestorePartSource }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::ManifestUnsupported), "S29: unsupported layout is diagnosed");
	              } });
	t.push_back({ "S30 no growth path", 30, 'T', [](ProgressionSnapshot& s) {
		              for (int c : { int(Red), int(Yellow) }) {
			              s.colours[c].stored        = 0;
			              s.colours[c].onionPresent  = false;
			              s.colours[c].onionExpected = false;
		              }
	              }, {}, {}, { Kind::ReplaceSeed }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::NoGrowthPath), "S30: no growth path is diagnosed, not granted");
		              check(r.final.colours[Red].viable() == 0, "S30: no free Pikmin");
	              } });
	t.push_back({ "S30 incompatible settings", 30, 'T', [](ProgressionSnapshot& s) { s.feasibility.settingsIncompatible = true; }, {}, {}, {},
	              [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::SettingsIncompatible), "S30: settings are explained");
	              } });
	t.push_back({ "S31 invalid day is diagnosed, not clamped", 31, 'T', [](ProgressionSnapshot& s) { s.day = 31; }, {}, {}, {}, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::InvalidDay), "S31: day 31 is diagnosed");
		              check(r.final.day == 31, "S31: the day is untouched");
	              } });
	t.push_back({ "S32 corrupt save is refused", 32, 'T', [](ProgressionSnapshot& s) {
		              s.prov.formatValid = false;
		              firstSeedLost(s, Yellow);
		              s.currParts = 0;
	              }, {}, {}, { Kind::ReplaceSeed, Kind::SetCurrParts }, [](const ProgressionSnapshot&, const RepairResult& r) {
		              check(hasDisposition(r.residual, Disposition::Diagnose, Reason::SaveInvalid), "S32: invalid input is diagnosed");
		              check(r.applied.empty(), "S32: nothing is inferred from a failed decode");
	              } });
	return t;
}

void testScenarios()
{
	for (const Scenario& sc : scenarios()) {
		const std::string name = sc.name;
		ProgressionSnapshot s  = sc.global ? globalPhase() : stage();
		sc.make(s);
		const RepairPlan plan = planRepairs(s);
		for (const Want& w : sc.repairs) check(hasRepair(plan, w), name + ": plans " + kindName(w.kind) + " c" + std::to_string(w.colour) + " p" + std::to_string(w.part));
		const RepairResult r = repairToFixedPoint(s);
		for (const Want& w : sc.after) check(appliedHas(r, w), name + ": applies " + kindName(w.kind));
		for (const Want& w : sc.repairs) check(appliedHas(r, w), name + ": applies " + kindName(w.kind));
		for (Kind k : sc.never) check(appliedCount(r, k) == 0, name + ": never applies " + kindName(k));
		const int label = sc.labelAs ? sc.labelAs : sc.id;
		check(hasScenario(plan, label) || sc.repairs.empty(), name + ": labelled S" + std::to_string(label));
		if (sc.extra) sc.extra(s, r);
		checkIdempotent(name, r);
	}
}

// ---------------------------------------------------------------------------
// One negative case per detector.

void testNegatives()
{
	auto planOf = [](const std::function<void(ProgressionSnapshot&)>& m, bool global = false) {
		ProgressionSnapshot s = global ? globalPhase() : stage();
		m(s);
		return planRepairs(s);
	};
	auto none = [&](const char* name, const std::function<void(ProgressionSnapshot&)>& m, bool global = false) {
		check(!planOf(m, global).hasRepairs(), std::string("negative: ") + name);
	};

	// S01/S02/S12: seeds that exist anywhere, or a colour never discovered.
	none("S01 no seed for an undiscovered colour", [](ProgressionSnapshot& s) { s.colours[Yellow] = ColourState {}; });
	none("S01 sprout on the stage", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); s.colours[Yellow].sproutsLoaded = 1; });
	none("S01 sprout on another stage", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); s.colours[Yellow].sproutsOffStage = 1; });
	none("S01 seed in flight", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); s.colours[Yellow].pending = 1; });
	none("S01 in the global phase the stage is not built", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); }, true);
	none("S01 partial inventory is unknown, not zero", [](ProgressionSnapshot& s) {
		firstSeedLost(s, Yellow);
		s.population = Known::Saved;
	});
	// S03
	none("S03 red unlocked and zero before discovery", [](ProgressionSnapshot& s) {
		s.colours[Red] = ColourState {};
		s.colours[Red].unlocked = s.colours[Red].displayed = true;
		ownFirst(s, 0, true);
		s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1; s.collectEngineFlag = false; s.day = 1;
	});
	// S04
	none("S04 queued movie", [](ProgressionSnapshot& s) {
		ColourState& c = s.colours[Blue];
		c              = ColourState {};
		c.discovered = c.movieQueued = c.onionPresent = c.onionExpected = true;
	});
	none("S04 synthetic demo flags", [](ProgressionSnapshot& s) {
		s.prov.syntheticDemoFlags = true;
		ColourState& c            = s.colours[Blue];
		c                         = ColourState {};
		c.discovered = c.plucked = c.onionPresent = c.onionExpected = true;
	});
	// S05..S07
	none("S05 sprouts alone do not prove a pluck", [](ProgressionSnapshot& s) {
		ColourState& c = s.colours[Yellow];
		c              = ColourState {};
		c.booted = c.discovered = c.onionPresent = c.onionExpected = true;
		c.sproutsLoaded = 3;
	});
	none("S07 a lone pluck bit proves nothing", [](ProgressionSnapshot& s) {
		ColourState& c = s.colours[Blue];
		c              = ColourState {};
		c.plucked      = true;
	});
	none("S07 all-Onions cheat residue", [](ProgressionSnapshot& s) {
		ColourState& c = s.colours[Blue];
		c              = ColourState {};
		c.unlocked = c.displayed = true;
	});
	// S08, S09
	none("S08 pristine red", [](ProgressionSnapshot& s) {
		s.colours[Red] = ColourState {};
		s.colours[Red].unlocked = s.colours[Red].displayed = true;
		ownFirst(s, 0, true); s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1; s.collectEngineFlag = false; s.day = 1;
	});
	none("S09 absent Onion on a stage that never spawns it", [](ProgressionSnapshot& s) {
		s.colours[Yellow].onionPresent  = false;
		s.colours[Yellow].onionExpected = false;
		s.colours[Yellow].stored        = 0;
		s.colours[Yellow].bonusPathOpen = true;
	});
	none("S09 incomplete inventory defers", [](ProgressionSnapshot& s) {
		s.colours[Red].onionPresent    = false;
		s.sourceInventoryComplete = false;
	});
	// S10
	none("S10 one colour extinct while the queued seed is coming", [](ProgressionSnapshot& s) {
		s.colours[Yellow].stored = 0;
		s.colours[Yellow].bonusPathOpen = true;
	});
	// S14
	none("S14 live absorption", [](ProgressionSnapshot& s) { ownFirst(s, 0, true); s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1; s.deliveryPending = true; });
	none("S14 a part is owned", [](ProgressionSnapshot&) {});
	// S15
	none("S15 tutorial without the engine", [](ProgressionSnapshot& s) {
		s.tutorialMode = true;
		s.parts[kPartMainEngine].vis = Vis::Uncollected;
		s.parts[kPartMainEngine].copies = 1;
		s.currParts = 7; s.requiredParts = 7;
	}, true);
	none("S15 active day end", [](ProgressionSnapshot& s) { s.tutorialMode = true; s.endDayActive = true; }, true);
	// S16
	none("S16 low-Pikmin legitimate run", [](ProgressionSnapshot& s) { s.colours[Red].stored = 2; });
	// S17, S18, S24
	none("S17 unknown table is not zero", [](ProgressionSnapshot& s) { s.partsKnown = false; s.currParts = 0; }, true);
	none("S18 never grants below a threshold", [](ProgressionSnapshot& s) { s.courseOpen = 0x0F; });
	none("S18 cheat-opened extra area stays", [](ProgressionSnapshot& s) { s.courseOpen |= 1 << StageLast; });
	none("S24 preserves unrelated bits", [](ProgressionSnapshot& s) { s.shipEffect = 9; });
	// S19..S23
	none("S19 another source exists", [](ProgressionSnapshot& s) { s.parts[10].source = Source::Loose; s.parts[10].why = MissingWhy::CacheRefused; });
	none("S19 collected IDs stay absent", [](ProgressionSnapshot& s) { s.parts[2].source = Source::Missing; });
	none("S19 incomplete inventory", [](ProgressionSnapshot& s) {
		s.parts[10].source = Source::Missing;
		s.sourceInventoryComplete = false;
	});
	none("S21 living carrier", [](ProgressionSnapshot& s) { s.parts[12].source = Source::CarrierAlive; });
	none("S21 delayed drop", [](ProgressionSnapshot& s) { s.parts[12].source = Source::DropPending; });
	none("S22 absorbing is not missing", [](ProgressionSnapshot& s) { s.parts[12].source = Source::Absorbing; });
	none("S23 temporarily carried", [](ProgressionSnapshot& s) { s.parts[13].source = Source::Carried; });
	// S25..S27
	none("S25 unfinished wall", [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Wall, 1, true, false, false, false } }; });
	none("S26 unfinished bridge", [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Bridge, 1, true, false, false, false } }; });
	none("S27 unpushed box", [](ProgressionSnapshot& s) { s.obstacles = { { ObstacleKind::Box, 1, true, false, false, false } }; });
	// S28
	none("S28 usable cache with a stale visit bit", [](ProgressionSnapshot& s) {
		s.generation.fault             = GenFault::VisitedWithoutCache;
		s.generation.sourceSetResolved = true;
	});
	// S29
	none("S29 healthy manifest", [](ProgressionSnapshot& s) { s.randomizer.active = true; s.randomizer.manifestMatches = true; });
	// S31
	for (int day : { 1, 2, 29, 30 }) {
		check(planOf([day](ProgressionSnapshot& s) { s.day = day; }).empty(), "negative: S31 day " + std::to_string(day) + " is valid");
	}
	// S32
	none("S32 truncated save gets no repair", [](ProgressionSnapshot& s) { s.prov.formatValid = false; firstSeedLost(s, Blue); });
	none("S32 wrong slot gets no repair", [](ProgressionSnapshot& s) { s.prov.slotIdentityValid = false; firstSeedLost(s, Blue); });
}

// ---------------------------------------------------------------------------
// Modes.

void testModes()
{
	auto faulty = [](Mode m) {
		ProgressionSnapshot s = stage();
		s.mode                = m;
		firstSeedLost(s, Yellow);
		s.currParts = 0;
		return s;
	};
	for (Mode m : { Mode::Versus, Mode::Mission, Mode::Challenge, Mode::Debug }) {
		check(planRepairs(faulty(m)).empty(), "non-story mode " + std::to_string(int(m)) + " never mutates campaign state");
	}
	const RepairPlan net = planRepairs(faulty(Mode::Netplay));
	check(!net.hasRepairs() && hasDisposition(net, Disposition::Diagnose, Reason::NetplayRefused), "netplay refuses a host-only repair");
	const RepairPlan speed = planRepairs(faulty(Mode::Speedrun));
	check(!speed.hasRepairs() && hasDisposition(speed, Disposition::Diagnose, Reason::SpeedrunProtected), "speedrun and practice are never changed silently");
	ProgressionSnapshot healthySpeed = stage();
	healthySpeed.mode                = Mode::Speedrun;
	check(planRepairs(healthySpeed).empty(), "healthy speedrun saves report nothing");
	ProgressionSnapshot dead = faulty(Mode::Story);
	dead.prov.permadeathTerminated = true;
	check(planRepairs(dead).empty(), "a terminated permadeath run is not revived");
	check(planHash(planRepairs(faulty(Mode::Story))) == planHash(planRepairs(faulty(Mode::CoopStory))), "co-op plans the same common plan as story");
	ProgressionSnapshot hard = faulty(Mode::Story);
	hard.prov.rulesValid     = true;
	check(planRepairs(hard).hasRepairs(), "Hard mode uses the same invariants");
}

// ---------------------------------------------------------------------------
// Combinations K1-K13.

void testCombinations()
{
	// K1: skipped first yellow plus the first collected artifact on the same day.
	{
		// (a) The part was collected; the counters and areas missed it.
		ProgressionSnapshot s = stage();
		ownFirst(s, 0, true);
		s.parts[7].vis    = Vis::Visible; // not the engine
		s.parts[7].copies = 0;
		s.currParts = 0; s.requiredParts = 0; s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
		s.collectEngineFlag = true;
		firstSeedLost(s, Yellow);
		const RepairResult r = repairToFixedPoint(s);
		check(appliedHas(r, { Kind::ReplaceSeed, Yellow }), "K1a: the yellow seed is replaced");
		check(appliedHas(r, { Kind::SetCurrParts, -1, -1, 1 }) && appliedHas(r, { Kind::SetCourseOpen, -1, -1, 1 }), "K1a: counts and access rebuilt from the real part ID");
		check(!r.final.parts[kPartMainEngine].owned(), "K1a: the engine is not assumed owned from the flag");
		check(appliedCount(r, Kind::ClearPrematureEngineFlag) == 0, "K1a: the flag is left alone when another part is owned");
		checkIdempotent("K1a", r);

		// (b) The healthy variant: the text was skipped, ownership is right.
		ProgressionSnapshot h = stage();
		firstSeedLost(h, Yellow);
		const RepairResult rb = repairToFixedPoint(h);
		check(rb.applied.size() == 1 && appliedHas(rb, { Kind::ReplaceSeed, Yellow }), "K1b: only the seed changes when ownership is right");

		// (c) The part is genuinely missing: restore its source, award nothing.
		ProgressionSnapshot m = stage();
		ownFirst(m, 0, true);
		m.upgradeLevel = 0; m.shipEffect = 8; m.courseOpen = 1;
		m.collectEngineFlag = true;
		m.parts[kPartMainEngine].source = Source::Missing;
		m.parts[kPartMainEngine].why    = MissingWhy::OneShotConsumed;
		m.parts[kPartMainEngine].copies = 0;
		firstSeedLost(m, Yellow);
		const RepairResult rc = repairToFixedPoint(m);
		check(appliedHas(rc, { Kind::RestorePartSource, -1, kPartMainEngine }) && appliedHas(rc, { Kind::ReplaceSeed, Yellow }), "K1c: source and seed both restored");
		check(!rc.final.parts[kPartMainEngine].owned() && rc.final.currParts == 0, "K1c: nothing is awarded");
		checkIdempotent("K1c", rc);
	}
	// K2: yellow and blue both lost.
	{
		ProgressionSnapshot s = stage();
		firstSeedLost(s, Yellow);
		firstSeedLost(s, Blue);
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::ReplaceSeed) == 2 && appliedCount(r, Kind::ReplaceSeed, Yellow) == 1 && appliedCount(r, Kind::ReplaceSeed, Blue) == 1,
		      "K2: two distinct reservations");
		check(!r.final.colours[Yellow].unlocked && !r.final.colours[Blue].unlocked, "K2: neither colour is unlocked");
		check(r.final.colours[Red].pending == 0, "K2: red is untouched");
		checkIdempotent("K2", r);
	}
	// K3: red starter loss + engine source gone + no carriers.
	{
		ProgressionSnapshot s = stage();
		ownFirst(s, 0, true);
		s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
		s.collectEngineFlag = false;
		s.colours[Red]      = ColourState {};
		s.colours[Red].unlocked = s.colours[Red].displayed = s.colours[Red].discovered = s.colours[Red].booted = true;
		s.colours[Red].onionPresent = s.colours[Red].onionExpected = true;
		s.parts[kPartMainEngine].source = Source::Missing;
		s.parts[kPartMainEngine].why    = MissingWhy::OneShotConsumed;
		s.parts[kPartMainEngine].copies = 0;
		s.feasibility.noRenewableFood = true;
		const RepairResult r = repairToFixedPoint(s);
		check(appliedHas(r, { Kind::ReplaceSeed, Red }) && appliedHas(r, { Kind::RestorePartSource, -1, kPartMainEngine }), "K3: starter and engine source restored");
		check(!r.final.parts[kPartMainEngine].owned(), "K3: the engine is not marked collected");
		check(r.final.colours[Red].viable() == 1, "K3: exactly one starter, not an army");
		check(hasDisposition(r.residual, Disposition::Diagnose, Reason::NoFood), "K3: the missing carriers are reported");
		checkIdempotent("K3", r);
	}
	// K4: whistle blue + blue extinction + water part.
	{
		ProgressionSnapshot s = stage();
		ColourState& b        = s.colours[Blue];
		b                     = ColourState {};
		b.discovered = b.booted = b.plucked = true;
		b.onionPresent = b.onionExpected = true;
		s.parts[18].source = Source::Loose;
		const RepairResult r = repairToFixedPoint(s);
		check(appliedHas(r, { Kind::SetUnlocked, Blue }) && appliedCount(r, Kind::ReplaceSeed, Blue) == 1, "K4: ownership first, then one blue seed");
		check(appliedCount(r, Kind::MovePartToAnchor) == 0, "K4: the water part is not teleported");
		ProgressionSnapshot noPluck = stage();
		firstSeedLost(noPluck, Blue);
		const RepairResult rn = repairToFixedPoint(noPluck);
		check(appliedCount(rn, Kind::SetUnlocked) == 0 && appliedCount(rn, Kind::ReplaceSeed, Blue) == 1, "K4: with no pluck evidence only the seed comes back");
		checkIdempotent("K4", r);
	}
	// K5: total extinction + missing acquired-Onion flags + absent Onion objects.
	{
		ProgressionSnapshot s = stage();
		for (int c : { int(Red), int(Yellow) }) {
			ColourState& cs = s.colours[c];
			cs              = ColourState {};
			cs.unlocked = cs.displayed = cs.discovered = cs.plucked = true;
			cs.onionExpected = true;
			cs.onionPresent  = false;
		}
		s.colours[Blue] = ColourState {};
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::RestoreOnionObject) == 2 && appliedCount(r, Kind::SetBooted) == 2 && appliedCount(r, Kind::ReplaceSeed) == 2, "K5: objects, boot flags, then one seed per colour");
		check(r.passes >= 3, "K5: dependent repairs take several passes");
		check(!r.final.colours[Blue].discovered && r.final.colours[Blue].pending == 0, "K5: undiscovered colours stay undiscovered");
		checkIdempotent("K5", r);
	}
	// K6: engine owned + zero counts + several areas closed + stale tutorial.
	{
		ProgressionSnapshot s = globalPhase();
		ownFirst(s, 12, false);
		s.currParts = 0; s.requiredParts = 0; s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
		s.tutorialMode = true;
		const RepairResult r = repairToFixedPoint(s);
		check(r.final.currParts == 12 && r.final.upgradeLevel == 3, "K6: counts and level rebuilt");
		check(r.final.courseOpen == 0x0F, "K6: every earned lower area, not only the highest branch");
		check(!r.final.tutorialMode, "K6: tutorial completed");
		checkIdempotent("K6", r);
	}
	// K7: full cache + consumed carrier + loose part absent + stale visit bit.
	{
		ProgressionSnapshot s = stage();
		s.generation.fault             = GenFault::VisitedWithoutCache;
		s.generation.sourceSetResolved = true;
		s.parts[12].source = Source::Missing; s.parts[12].why = MissingWhy::CarrierConsumed; s.parts[12].copies = 0;
		s.parts[13].source = Source::Missing; s.parts[13].why = MissingWhy::CacheRefused; s.parts[13].copies = 0;
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::RestorePartSource) == 2, "K7: one source per lost part");
		check(r.final.parts[12].copies == 1 && r.final.parts[13].copies == 1, "K7: no double source");
		ProgressionSnapshot u = s;
		u.generation.sourceSetResolved = false;
		const RepairResult ru = repairToFixedPoint(u);
		check(ru.applied.empty() && hasDisposition(ru.residual, Disposition::Defer, Reason::SourceSetUnresolved), "K7: no restore before one authoritative source set is chosen");
		checkIdempotent("K7", r);
	}
	// K8: missing blue seed + destroyed wall closed + finished bridge closed.
	{
		ProgressionSnapshot s = stage();
		firstSeedLost(s, Blue);
		s.obstacles = { { ObstacleKind::Wall, 1, true, true, false, false },
			            { ObstacleKind::Bridge, 2, true, true, false, false },
			            { ObstacleKind::Wall, 3, true, false, false, false },
			            { ObstacleKind::Bridge, 4, true, false, false, false } };
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::ReplaceSeed, Blue) == 1 && appliedCount(r, Kind::RebuildObstacleRoute) == 2, "K8: three independent repairs");
		check(!r.final.obstacles[2].routeOpen && !r.final.obstacles[3].routeOpen, "K8: the intact wall and unfinished bridge are unchanged");
		checkIdempotent("K8", r);
	}
	// K9: yellow unlock mismatch + bomb tutorial unseen + finished box route stale.
	{
		ProgressionSnapshot s = stage();
		ColourState& y        = s.colours[Yellow];
		y                     = ColourState {};
		y.booted = y.discovered = true;
		y.live = 2;
		y.onionPresent = y.onionExpected = true;
		s.obstacles = { { ObstacleKind::Box, 1, true, true, false, false } };
		const RepairResult r = repairToFixedPoint(s);
		check(appliedHas(r, { Kind::SetUnlocked, Yellow }) && appliedCount(r, Kind::RebuildObstacleRoute) == 1, "K9: yellow acquisition and the box route");
		for (const Action& a : r.applied) {
			check(a.kind != Kind::ReplaceSeed && a.kind != Kind::RestorePartSource, "K9: no grant of any kind");
		}
		checkIdempotent("K9", r);
	}
	// K10: Randomizer part carrier drop + wrong counters + all colours extinct.
	{
		ProgressionSnapshot s = stage();
		s.randomizer.active          = true;
		s.randomizer.manifestMatches = false;
		s.currParts = 1; s.requiredParts = 1; s.upgradeLevel = 0;
		s.parts[12].source = Source::Missing; s.parts[12].why = MissingWhy::CarrierConsumed; s.parts[12].copies = 0;
		s.colours[Red].stored = 0; s.colours[Yellow].stored = 0;
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::ReplaceSeed) == 2, "K10: the independent Onion repair still runs");
		check(r.final.currParts == 8, "K10: owned IDs still rebuild the counters");
		check(appliedCount(r, Kind::RestorePartSource) == 0 && hasDisposition(r.residual, Disposition::Diagnose, Reason::ManifestUnsupported),
		      "K10: speculative part restoration is blocked and diagnosed");
		checkIdempotent("K10", r);
	}
	// K11: repair requested twice, BonusPiki queued, pool full.
	{
		ProgressionSnapshot s = stage();
		firstSeedLost(s, Yellow);
		s.seedCapacity = false;
		s.parts[10].copies = 2;
		const RepairResult first = repairToFixedPoint(s);
		check(appliedCount(first, Kind::ReplaceSeed) == 0 && hasDisposition(first.residual, Disposition::Defer, Reason::AllocationUnavailable), "K11: pool full defers, no success claimed");
		ProgressionSnapshot freed = first.final;
		freed.seedCapacity        = true;
		const RepairResult second = repairToFixedPoint(freed);
		check(appliedCount(second, Kind::ReplaceSeed, Yellow) == 1, "K11: exactly one replacement once capacity returns");
		const RepairResult third = repairToFixedPoint(second.final);
		check(third.applied.empty(), "K11: a repeated request adds nothing");
		ProgressionSnapshot queued = stage();
		queued.colours[Blue] = ColourState {};
		queued.colours[Yellow].stored = 0;
		queued.colours[Yellow].bonusPathOpen = true;
		check(repairToFixedPoint(queued).applied.empty(), "K11: an already queued BonusPiki adds no second seed");
		check(appliedCount(first, Kind::CollapsePartDuplicates) == 1, "K11: the duplicate part collapses once");
	}
	// Review fix: a colour unlocked by this very plan must not also get a seed when the normal
	// extinction movie will supply one (the game would otherwise give it two).
	{
		ProgressionSnapshot s = stage();
		ColourState& y        = s.colours[Yellow];
		y                     = ColourState {};
		y.booted = y.discovered = y.plucked = true;
		y.onionPresent = y.onionExpected = true;
		y.bonusPathOpen = true;
		const RepairResult open = repairToFixedPoint(s);
		check(appliedHas(open, { Kind::SetUnlocked, Yellow }) && appliedCount(open, Kind::ReplaceSeed, Yellow) == 0, "unlock plus an open extinction path: the normal seed is the only one");
		checkIdempotent("unlock with open path", open);
		y.bonusPathOpen = false;
		const RepairResult closed = repairToFixedPoint(s);
		check(appliedCount(closed, Kind::ReplaceSeed, Yellow) == 1, "unlock with the extinction path closed (tutorial): one replacement seed");
	}
	// K12: day 29 + count mismatch + closed Final Trial + missing blue.
	{
		ProgressionSnapshot s = stage();
		ownFirst(s, 29, false);
		s.currParts = 20; s.requiredParts = 20; s.upgradeLevel = 3; s.shipEffect = 7; s.courseOpen = 0x0F;
		s.day = 29;
		firstSeedLost(s, Blue);
		const RepairResult r = repairToFixedPoint(s);
		check(r.final.currParts == 29 && r.final.upgradeLevel == 4 && (r.final.courseOpen & (1 << StageLast)), "K12: truthful counts and the Final Trial");
		check(appliedCount(r, Kind::ReplaceSeed, Blue) == 1, "K12: the blue seed");
		check(r.final.day == 29, "K12: no extra day");
		check(r.final.ownedCount() == 29, "K12: no unearned part");
		checkIdempotent("K12", r);
	}
	// K13: quit or crash between planning and saving.
	{
		ProgressionSnapshot s = stage();
		firstSeedLost(s, Yellow);
		s.currParts = 0;
		const std::uint64_t original = planHash(planRepairs(s));
		const RepairResult first     = repairToFixedPoint(s);
		const RepairResult crashed   = repairToFixedPoint(s); // the disk still holds the original
		check(planHash(planRepairs(s)) == original, "K13: planning is repeatable from the original disk state");
		check(appliedKeys(first) == appliedKeys(crashed), "K13: an unsaved crash repeats the same correction");
		check(repairToFixedPoint(first.final).applied.empty(), "K13: after the save the repaired state needs nothing");
		ProgressionSnapshot bad = s;
		bad.prov.formatValid    = false;
		check(repairToFixedPoint(bad).applied.empty(), "K13: an invalid write is never repaired from zero-based inference");
	}
}

// ---------------------------------------------------------------------------
// Thresholds, ownership, ending, population, sources.

void testThresholds()
{
	const int edges[] = { 0, 1, 5, 6, 11, 12, 28, 29, 30 };
	for (int n : edges) {
		ProgressionSnapshot s = globalPhase();
		ownFirst(s, n, false);
		s.currParts = 0; s.requiredParts = 0; s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
		const RepairResult r = repairToFixedPoint(s);
		int wantMask         = 1;
		if (n >= 1) wantMask |= 1 << StageForest;
		if (n >= 6) wantMask |= 1 << StageCave;
		if (n >= 12) wantMask |= 1 << StageYakushima;
		if (n >= 29) wantMask |= 1 << StageLast;
		check(r.final.courseOpen == wantMask, "threshold edge " + std::to_string(n) + ": area mask");
		const int level = n >= 30 ? 5 : n >= 29 ? 4 : n >= 12 ? 3 : n >= 6 ? 2 : n >= 1 ? 1 : 0;
		check(r.final.upgradeLevel == level && r.final.currParts == n, "threshold edge " + std::to_string(n) + ": level and count");
		checkIdempotent("threshold " + std::to_string(n), r);
	}
	// Custom loaded thresholds.
	{
		ProgressionSnapshot s = globalPhase();
		s.areaParts    = { 2, 4, 8, 10 };
		s.perfectParts = 12;
		ownFirst(s, 9, false);
		s.currParts = 0; s.requiredParts = 0; s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
		const RepairResult r = repairToFixedPoint(s);
		check(r.final.courseOpen == (1 | 2 | 4 | 8) && r.final.upgradeLevel == 3, "custom thresholds are read from the loaded values");
	}
	// Required versus optional identity.
	{
		auto required = [](int total, bool includeOptional) {
			ProgressionSnapshot s = globalPhase();
			for (int i = 0; i < kMaxParts; ++i) s.parts[i].vis = Vis::Uncollected;
			int placed = 0;
			if (includeOptional) {
				for (int i = 0; i < kMaxParts && placed < total; ++i) {
					if (!partIsRequired(i)) { s.parts[i].vis = Vis::Visible; ++placed; }
				}
			}
			for (int i = 0; i < kMaxParts && placed < total; ++i) {
				if (partIsRequired(i)) { s.parts[i].vis = Vis::Invisible; ++placed; } // invisible parts count
			}
			for (int i = 0; i < kMaxParts && placed < total; ++i) { // totals above 25 need the optional parts too
				if (s.parts[i].vis == Vis::Uncollected) { s.parts[i].vis = Vis::Visible; ++placed; }
			}
			s.currParts = 0; s.requiredParts = 0; s.upgradeLevel = 0; s.shipEffect = 8; s.courseOpen = 1;
			return repairToFixedPoint(s).final;
		};
		check(required(24, false).requiredParts == 24, "24 required parts");
		check(required(25, false).requiredParts == 25, "25 required parts");
		check(required(25, true).requiredParts == 20, "25 arbitrary parts are not the 25 required IDs");
		check(required(29, false).currParts == 29 && required(30, false).currParts == 30, "29 and 30 totals");
		check(required(30, false).upgradeLevel == 5, "30 is perfect");
	}
	// Days.
	for (int day : { 0, 31 }) {
		ProgressionSnapshot s = stage();
		s.day                 = day;
		const RepairResult r  = repairToFixedPoint(s);
		check(hasDisposition(r.residual, Disposition::Diagnose, Reason::InvalidDay) && r.final.day == day, "invalid day " + std::to_string(day) + " is diagnosed");
	}
	{
		ProgressionSnapshot s = stage();
		s.dayMatchesClock     = false;
		check(hasDisposition(planRepairs(s), Disposition::Diagnose, Reason::DayDisagreesWithClock), "day and clock disagreement is diagnosed");
	}
}

void testPopulationMatrix()
{
	struct Row {
		const char* name;
		std::function<void(ColourState&)> set;
		bool seed;
	};
	const Row rows[] = {
		{ "only stored", [](ColourState& c) { c.stored = 1; }, false },
		{ "only a loaded sprout", [](ColourState& c) { c.sproutsLoaded = 1; }, false },
		{ "only an off-stage saved sprout", [](ColourState& c) { c.sproutsOffStage = 1; }, false },
		{ "flight or queued emit", [](ColourState& c) { c.pending = 1; }, false },
		{ "only a live Pikmin", [](ColourState& c) { c.live = 1; }, false },
		{ "none anywhere", [](ColourState&) {}, true },
	};
	for (int colour : { int(Blue), int(Red), int(Yellow) }) {
		for (const Row& row : rows) {
			ProgressionSnapshot s = stage();
			firstSeedLost(s, colour);
			s.colours[colour].unlocked = colour == Red; // red is unlocked from the start
			s.colours[colour].displayed = colour == Red;
			s.colours[colour].plucked   = colour == Red;
			row.set(s.colours[colour]);
			const RepairResult r = repairToFixedPoint(s);
			const bool seeded    = appliedCount(r, Kind::ReplaceSeed, colour) == 1;
			check(seeded == row.seed, std::string("population ") + row.name + " for " + colourName(colour));
			for (int other = 0; other < kColours; ++other) {
				if (other != colour && other != Red && other != Yellow) check(appliedCount(r, Kind::ReplaceSeed, other) == 0, "no spillover to another colour");
			}
		}
	}
	// One colour extinct while the others are alive, and all colours extinct.
	{
		ProgressionSnapshot s = stage();
		s.colours[Yellow].stored = 0;
		check(appliedCount(repairToFixedPoint(s), Kind::ReplaceSeed, Yellow) == 1, "extinct yellow, red alive: one yellow seed");
		s.colours[Red].stored = 0;
		check(appliedCount(repairToFixedPoint(s), Kind::ReplaceSeed) == 2, "all acquired colours extinct: one seed each");
	}
	// Only red unlocked, seed-only colours do not unlock.
	{
		ProgressionSnapshot s = stage();
		s.colours[Yellow] = ColourState {};
		s.colours[Blue]   = ColourState {};
		check(repairToFixedPoint(s).applied.empty(), "only red unlocked: nothing happens");
	}
}

void testSourceMatrix()
{
	struct Row {
		Source src;
		bool complete;
		Kind expect; // Kind::Diagnose = nothing
		bool repairs;
	};
	const Row rows[] = {
		{ Source::Loose, true, Kind::Diagnose, false },
		{ Source::Carried, true, Kind::Diagnose, false },
		{ Source::CarrierAlive, true, Kind::Diagnose, false },
		{ Source::DropPending, true, Kind::Diagnose, false },
		{ Source::Absorbing, true, Kind::Diagnose, false },
		{ Source::FutureGenerator, true, Kind::Diagnose, false },
		{ Source::Unknown, true, Kind::Diagnose, false },
		{ Source::Missing, true, Kind::RestorePartSource, true },
		{ Source::Missing, false, Kind::Diagnose, false },
		{ Source::OutOfBounds, true, Kind::MovePartToAnchor, true },
		{ Source::Unsupported, true, Kind::Diagnose, false },
	};
	for (const Row& row : rows) {
		ProgressionSnapshot s = stage();
		s.parts[10].source = row.src;
		s.parts[10].copies = row.src == Source::Missing ? 0 : 1;
		s.sourceInventoryComplete = row.complete;
		const RepairResult r = repairToFixedPoint(s);
		check((r.applied.size() == (row.repairs ? 1u : 0u)), "source matrix " + std::to_string(int(row.src)) + (row.complete ? " complete" : " partial"));
		if (row.repairs) check(appliedHas(r, { row.expect, -1, 10 }), "source matrix applies the right kind");
	}
	// No anchor, incompatible catalogue, cannot-drop carrier: diagnose.
	{
		ProgressionSnapshot s = stage();
		s.parts[10].source = Source::Missing; s.parts[10].copies = 0; s.parts[10].anchorKnown = false;
		check(hasDisposition(planRepairs(s), Disposition::Diagnose, Reason::NoAnchor) && !planRepairs(s).hasRepairs(), "a missing anchor is diagnosed");
		s.parts[10].anchorKnown = true; s.catalogueCompatible = false;
		check(!planRepairs(s).hasRepairs(), "an unsupported catalogue is diagnosed, not restored");
		s.catalogueCompatible = true;
		s.parts[10].why = MissingWhy::CarrierConsumed; s.parts[10].carrierCanDrop = false;
		check(hasDisposition(planRepairs(s), Disposition::Diagnose, Reason::CarrierCannotDrop), "a carrier that cannot drop the identity is diagnosed");
	}
	// One part among several faults: an ambiguous part does not block a safe seed.
	{
		ProgressionSnapshot s = stage();
		firstSeedLost(s, Yellow);
		s.parts[10].source = Source::Unsupported;
		s.prov.bootstrapMismatch = true; // ambiguous, no provenance
		const RepairResult r = repairToFixedPoint(s);
		check(appliedCount(r, Kind::ReplaceSeed, Yellow) == 1, "an unrelated ambiguous fault does not prevent a safe fix");
	}
}

// ---------------------------------------------------------------------------
// Pairwise combinations of disjoint faults, shuffled order, failed allocations.

struct Fault {
	const char* id;
	std::function<void(ProgressionSnapshot&)> apply;
};

std::vector<Fault> disjointFaults()
{
	return {
		{ "S01 yellow first seed", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); } },
		{ "S06 blue whistle", [](ProgressionSnapshot& s) {
			  ColourState& c = s.colours[Blue];
			  c              = ColourState {};
			  c.booted = c.discovered = true;
			  c.stored = 2;
			  c.onionPresent = c.onionExpected = true;
		  } },
		{ "S09 red onion object", [](ProgressionSnapshot& s) { s.colours[Red].onionPresent = false; } },
		{ "S17 counters", [](ProgressionSnapshot& s) { s.currParts = 1; s.requiredParts = 2; s.upgradeLevel = 0; } },
		{ "S18 areas", [](ProgressionSnapshot& s) { s.courseOpen = 1; } },
		{ "S19 cache refused", [](ProgressionSnapshot& s) {
			  s.parts[10].source = Source::Missing; s.parts[10].why = MissingWhy::CacheRefused; s.parts[10].copies = 0;
		  } },
		{ "S22 duplicate", [](ProgressionSnapshot& s) { s.parts[11].copies = 2; } },
		{ "S23 out of bounds", [](ProgressionSnapshot& s) { s.parts[13].source = Source::OutOfBounds; } },
		{ "S24 jets", [](ProgressionSnapshot& s) {
			  s.parts[7].vis = Vis::Uncollected; s.parts[7].copies = 1; // swap one owned part for the jet: counters stay right
			  s.parts[kPartJet1].vis = Vis::Visible; s.parts[kPartJet1].copies = 0;
		  } },
		{ "S25 wall", [](ProgressionSnapshot& s) { s.obstacles.push_back({ ObstacleKind::Wall, 1, true, true, false, false }); } },
		{ "S26 bridge", [](ProgressionSnapshot& s) { s.obstacles.push_back({ ObstacleKind::Bridge, 2, true, true, false, false }); } },
		{ "S27 box", [](ProgressionSnapshot& s) { s.obstacles.push_back({ ObstacleKind::Box, 3, true, true, false, false }); } },
		{ "S28 limited source", [](ProgressionSnapshot& s) {
			  s.generation.fault = GenFault::LimitedFlagStale; s.generation.limitedProven = true; s.generation.stage = StageForest; s.generation.index = 2;
		  } },
	};
}

void testPairwise()
{
	const std::vector<Fault> faults = disjointFaults();
	std::map<std::string, std::set<std::string>> single;
	for (const Fault& f : faults) {
		ProgressionSnapshot s = stage();
		f.apply(s);
		const RepairResult r = repairToFixedPoint(s);
		check(!r.applied.empty(), std::string("pairwise base: ") + f.id + " is detected");
		single[f.id] = appliedKeys(r);
	}
	int pairs = 0;
	for (size_t i = 0; i < faults.size(); ++i) {
		for (size_t j = i + 1; j < faults.size(); ++j) {
			ProgressionSnapshot s = stage();
			faults[i].apply(s);
			faults[j].apply(s);
			const RepairResult r = repairToFixedPoint(s);
			std::set<std::string> want = single[faults[i].id];
			want.insert(single[faults[j].id].begin(), single[faults[j].id].end());
			// Counter faults overlap by design (S17 and S24 both set the counters): compare as sets.
			check(appliedKeys(r) == want, std::string("pairwise: ") + faults[i].id + " + " + faults[j].id);
			checkIdempotent(std::string("pair ") + faults[i].id + " + " + faults[j].id, r);

			// Shuffled enumeration order: the same sorted plan.
			ProgressionSnapshot shuffled = s;
			std::reverse(shuffled.obstacles.begin(), shuffled.obstacles.end());
			check(planHash(planRepairs(s)) == planHash(planRepairs(shuffled)), std::string("shuffle: ") + faults[i].id + " + " + faults[j].id);
			++pairs;
		}
	}
	check(pairs == int(faults.size() * (faults.size() - 1) / 2), "all pairs ran");

	// All faults at once.
	ProgressionSnapshot all = stage();
	for (const Fault& f : faults) f.apply(all);
	const RepairResult r = repairToFixedPoint(all);
	check(r.applied.size() >= faults.size(), "all disjoint faults resolve together");
	checkIdempotent("all faults", r);
	// Obstacles in every permutation of three.
	ProgressionSnapshot three = stage();
	three.obstacles           = { { ObstacleKind::Wall, 1, true, true, false, false }, { ObstacleKind::Bridge, 2, true, true, false, false }, { ObstacleKind::Box, 3, true, true, false, false } };
	const std::uint64_t ref   = planHash(planRepairs(three));
	std::sort(three.obstacles.begin(), three.obstacles.end(), [](const Obstacle& a, const Obstacle& b) { return int(a.kind) < int(b.kind); });
	do {
		check(planHash(planRepairs(three)) == ref, "obstacle order does not change the plan");
	} while (std::next_permutation(three.obstacles.begin(), three.obstacles.end(), [](const Obstacle& a, const Obstacle& b) { return int(a.kind) < int(b.kind); }));
}

struct RefusingExecutor : ActionExecutor {
	int refuse = 1;
	int calls  = 0;
	bool execute(const Action& a) override
	{
		++calls;
		if (a.kind == Kind::ReplaceSeed && refuse > 0) {
			--refuse;
			return false;
		}
		return true;
	}
};

void testExecutorFailures()
{
	ProgressionSnapshot s = stage();
	firstSeedLost(s, Yellow);
	s.obstacles.push_back({ ObstacleKind::Wall, 1, true, true, false, false });
	RefusingExecutor exec;
	const RepairResult first = repairToFixedPoint(s, &exec);
	check(first.failed.size() == 1 && first.failed[0].kind == Kind::ReplaceSeed, "a refused allocation is recorded");
	check(first.final.colours[Yellow].pending == 0, "no success marker after a refused allocation");
	check(appliedCount(first, Kind::RebuildObstacleRoute) == 1, "independent repairs still apply when one allocation fails");
	check(hasDisposition(first.residual, Disposition::Defer, Reason::AllocationUnavailable), "the refused seed stays deferred");
	const RepairResult retry = repairToFixedPoint(first.final, &exec);
	check(appliedCount(retry, Kind::ReplaceSeed, Yellow) == 1 && retry.failed.empty(), "the retry replaces exactly one seed");
	check(repairToFixedPoint(retry.final, &exec).applied.empty(), "nothing further after the retry");
}

void testKeysAndHash()
{
	ProgressionSnapshot a = stage();
	firstSeedLost(a, Yellow);
	a.prov.lineage = 0x1234;
	ProgressionSnapshot b = a;
	b.prov.lineage        = 0x9999;
	const RepairPlan pa   = planRepairs(a);
	const RepairPlan pb   = planRepairs(b);
	check(pa.actions.size() == 1 && pb.actions.size() == 1, "one action each");
	check(pa.actions[0].key != pb.actions[0].key, "the repair key includes the save lineage");
	check(pa.actions[0].key.rfind("v1/", 0) == 0, "the repair key carries a schema version");
	check(planHash(pa) == planHash(planRepairs(a)), "the plan hash is deterministic");
	check(planHash(pa) == planHash(pb), "the plan hash ignores the lineage (it hashes the decision, not the key)");

	// Fixed fixtures hash to fixed values so a platform difference would show.
	struct Fixture {
		const char* name;
		std::function<void(ProgressionSnapshot&)> make;
		std::uint64_t hash;
	};
	const Fixture fixtures[] = {
		{ "healthy", [](ProgressionSnapshot&) {}, 0xcbf29ce484222325ull },
		{ "S01", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); }, 0xbdb8e13ad095487dull },
		{ "K2", [](ProgressionSnapshot& s) { firstSeedLost(s, Yellow); firstSeedLost(s, Blue); }, 0xa958b62077c03240ull },
	};
	for (const Fixture& f : fixtures) {
		ProgressionSnapshot s = stage();
		f.make(s);
		const std::uint64_t h = planHash(planRepairs(s));
		if (f.hash != 0) check(h == f.hash, std::string("fixture hash ") + f.name);
		std::printf("plan hash %-8s %016llx\n", f.name, static_cast<unsigned long long>(h));
	}
}

void testNotes()
{
	ProgressionSnapshot s = stage();
	firstSeedLost(s, Yellow);
	firstSeedLost(s, Blue);
	s.currParts = 0;
	const RepairResult r = repairToFixedPoint(s);
	const std::vector<std::string> notes = describeApplied(r.applied);
	check(!notes.empty(), "a repair produces a note");
	std::set<std::string> unique(notes.begin(), notes.end());
	check(unique.size() == notes.size(), "note lines are not repeated");
	bool spoils = false;
	for (const std::string& n : notes) spoils |= n.find("Main Engine") != std::string::npos;
	check(!spoils, "notes name no hidden parts");
	check(describeApplied(repairToFixedPoint(r.final).applied).empty(), "a healthy repaired save produces no note");
}

void testScenarioCoverage()
{
	// Every S01-S32 appears in the table at least once.
	std::set<int> seen;
	for (const Scenario& sc : scenarios()) seen.insert(sc.id);
	for (int id = 1; id <= 32; ++id) check(seen.count(id) == 1, "scenario table covers S" + std::to_string(id));
	int c = 0;
	std::set<int> cSeen;
	for (const Scenario& sc : scenarios()) {
		if (sc.label == 'C') cSeen.insert(sc.id);
	}
	c = int(cSeen.size());
	check(c == 5, "five confirmed (C) mechanisms have legacy fixtures: " + std::to_string(c));
}

} // namespace

int main()
{
	testHealthy();
	testScenarios();
	testNegatives();
	testModes();
	testCombinations();
	testThresholds();
	testPopulationMatrix();
	testSourceMatrix();
	testPairwise();
	testExecutorFailures();
	testKeysAndHash();
	testNotes();
	testScenarioCoverage();
	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
