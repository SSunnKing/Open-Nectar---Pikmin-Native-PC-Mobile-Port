#include "pc_progression_repair.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <tuple>

namespace pc::progression {

int partIndexOf(std::uint32_t id)
{
	for (int i = 0; i < kMaxParts; ++i) {
		if (kPartIds[i] == id) return i;
	}
	return -1;
}

ProgressionSnapshot makeHealthySnapshot()
{
	ProgressionSnapshot s;
	s.day = 6;

	// Parts 0..7 are owned: the engine and the radar are among them.
	for (int i = 0; i < 8; ++i) s.parts[i].vis = Vis::Visible;
	for (int i = 8; i < kMaxParts; ++i) {
		s.parts[i].source = Source::Loose;
		s.parts[i].copies = 1;
	}
	for (int i = 0; i < 8; ++i) s.parts[i].copies = 0;
	s.currParts     = 8;
	s.requiredParts = 8;
	s.upgradeLevel  = 2;
	s.shipEffect    = kShipRadar;
	s.courseOpen    = (1 << StagePractice) | (1 << StageForest) | (1 << StageCave);

	auto grown = [](ColourState& c, int stored) {
		c.unlocked = c.booted = c.displayed = c.discovered = c.plucked = true;
		c.stored        = stored;
		c.onionPresent  = true;
		c.onionExpected = true;
	};
	grown(s.colours[Red], 30);
	grown(s.colours[Yellow], 12);
	// Blue is genuinely undiscovered: nothing set, no Onion yet.
	s.colours[Blue].onionExpected = false;

	s.tutorialMode = false;
	s.collectEngineFlag = true;
	return s;
}

// ---------------------------------------------------------------------------
// Names and canonical text.

const char* kindName(Kind kind)
{
	switch (kind) {
	case Kind::SetUnlocked: return "SetUnlocked";
	case Kind::SetBooted: return "SetBooted";
	case Kind::SetDisplayed: return "SetDisplayed";
	case Kind::SetDiscovered: return "SetDiscovered";
	case Kind::SetPlucked: return "SetPlucked";
	case Kind::SetCourseOpen: return "SetCourseOpen";
	case Kind::SetUpgradeLevel: return "SetUpgradeLevel";
	case Kind::SetShipEffectBits: return "SetShipEffectBits";
	case Kind::SetCurrParts: return "SetCurrParts";
	case Kind::SetRequiredParts: return "SetRequiredParts";
	case Kind::SetTutorialComplete: return "SetTutorialComplete";
	case Kind::ClearPrematureEngineFlag: return "ClearPrematureEngineFlag";
	case Kind::RestoreOnionObject: return "RestoreOnionObject";
	case Kind::ReplaceSeed: return "ReplaceSeed";
	case Kind::RestorePartSource: return "RestorePartSource";
	case Kind::MovePartToAnchor: return "MovePartToAnchor";
	case Kind::CollapsePartDuplicates: return "CollapsePartDuplicates";
	case Kind::SuppressPartReplica: return "SuppressPartReplica";
	case Kind::RebuildObstacleRoute: return "RebuildObstacleRoute";
	case Kind::NormalizeObstacle: return "NormalizeObstacle";
	case Kind::FixLimitedSource: return "FixLimitedSource";
	case Kind::Diagnose: return "Diagnose";
	case Kind::Defer: return "Defer";
	}
	return "?";
}

const char* colourName(int colour)
{
	switch (colour) {
	case Blue: return "Blue";
	case Red: return "Red";
	case Yellow: return "Yellow";
	}
	return "-";
}

const char* partName(int index)
{
	static const char* const names[kMaxParts] = {
		"Bowsprit", "Gluon Drive", "Anti-Dioxin Filter", "Eternal Fuel Dynamo", "Main Engine", "Whimsical Radar",
		"Interstellar Radio", "Guard Satellite", "Chronos Reactor", "Radiation Canopy", "Geiger Counter", "Sagittarius",
		"Libra", "Omega Stabilizer", "Ionium Jet", "Ionium Jet", "Shock Absorber", "Gravity Jumper", "Pilot Seat",
		"Nova Blaster", "Automatic Gear", "Zirconium Rotor", "Extraordinary Bolt", "Repair-type Bolt", "Space Float",
		"Massage Machine", "Secret Safe", "Positron Generator", "Analog Computer", "UV Lamp",
	};
	return index >= 0 && index < kMaxParts ? names[index] : "-";
}

std::string canonical(const Action& a)
{
	char buf[160];
	std::snprintf(buf, sizeof(buf), "S%02d %s d%d r%d c%d p%d v%d", a.scenario, kindName(a.kind), int(a.disposition), int(a.reason),
	              a.colour, a.part, a.value);
	return buf;
}

std::uint64_t planHash(const RepairPlan& plan)
{
	std::uint64_t h = 0xcbf29ce484222325ull;
	for (const Action& a : plan.actions) {
		const std::string line = canonical(a) + "\n";
		for (unsigned char ch : line) {
			h ^= ch;
			h *= 0x100000001b3ull;
		}
	}
	return h;
}

std::vector<const Action*> RepairPlan::repairs() const
{
	std::vector<const Action*> out;
	for (const Action& a : actions) {
		if (a.disposition == Disposition::Repair) out.push_back(&a);
	}
	return out;
}

bool RepairPlan::hasRepairs() const
{
	for (const Action& a : actions) {
		if (a.disposition == Disposition::Repair) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Planner.

namespace {

struct Planner {
	const ProgressionSnapshot& s;
	std::vector<Action> out;

	explicit Planner(const ProgressionSnapshot& snapshot)
	    : s(snapshot)
	{
	}

	void add(int scenario, Kind kind, Disposition disposition, Reason reason, int colour, int part, int value, const char* evidence)
	{
		Action a;
		a.scenario    = scenario;
		a.kind        = kind;
		a.disposition = disposition;
		a.reason      = reason;
		a.colour      = colour;
		a.part        = part;
		a.value       = value;
		a.evidence    = evidence;
		char key[96];
		std::snprintf(key, sizeof(key), "v1/%016llx/%s/%d/%08x/%d/%d", static_cast<unsigned long long>(s.prov.lineage), kindName(kind),
		              colour, part >= 0 && part < kMaxParts ? kPartIds[part] : 0u, value, int(reason));
		a.key = key;
		out.push_back(std::move(a));
	}
	void repair(int scenario, Kind kind, int colour, int part, int value, const char* evidence)
	{
		add(scenario, kind, Disposition::Repair, Reason::None, colour, part, value, evidence);
	}
	void defer(int scenario, Kind kind, Reason reason, int colour, int part, const char* evidence)
	{
		add(scenario, kind, Disposition::Defer, reason, colour, part, 0, evidence);
	}
	void diagnose(int scenario, Reason reason, int colour, int part, const char* evidence)
	{
		add(scenario, Kind::Diagnose, Disposition::Diagnose, reason, colour, part, 0, evidence);
	}

	int levelFor(int owned) const
	{
		if (owned >= s.perfectParts) return 5;
		if (owned >= s.areaParts[3]) return 4;
		if (owned >= s.areaParts[2]) return 3;
		if (owned >= s.areaParts[1]) return 2;
		if (owned >= s.areaParts[0]) return 1;
		return 0;
	}

	bool worldBuilt() const { return s.world == Known::Materialized; }
	bool populationBuilt() const { return s.population == Known::Materialized && worldBuilt(); }
	bool sourcesBlockedByProvenance() const
	{
		return s.generation.fault != GenFault::None && s.generation.fault != GenFault::LimitedFlagStale && !s.generation.sourceSetResolved;
	}

	// -- Ownership truth ---------------------------------------------------

	void planOwnership(bool& partsUsable)
	{
		partsUsable = false;
		if (!s.partsKnown) {
			defer(17, Kind::Defer, Reason::InventoryIncomplete, -1, -1, "the owned-part table was not fully decoded");
			return;
		}
		partsUsable = true;

		const int owned    = s.ownedCount();
		const int required = s.ownedRequiredCount();

		// S17: derived counters follow the owned parts. The count needs only the table; the
		// required count needs each slot's identity, which exists once the ship has registered them.
		if (s.currParts != owned) repair(17, Kind::SetCurrParts, -1, -1, owned, "part count disagrees with the owned parts");
		if (s.partIdsKnown && s.requiredParts != required) repair(17, Kind::SetRequiredParts, -1, -1, required, "required-part count disagrees");
		const int level = levelFor(owned);
		if (s.upgradeLevel != level) repair(17, Kind::SetUpgradeLevel, -1, -1, level, "ship level disagrees with the owned parts");

		// S18: every earned area, independently, never below its threshold.
		for (int i = 0; i < kAreaCount; ++i) {
			const int stage = i + 1;
			if (owned >= s.areaParts[i] && !(s.courseOpen & (1 << stage))) {
				repair(18, Kind::SetCourseOpen, -1, -1, stage, "the owned parts reach this area's threshold");
			}
		}

		if (!s.partIdsKnown) return;

		// S24: capability bits of the radar and jets, set only (other bits are kept).
		const std::uint8_t wanted = std::uint8_t((s.parts[kPartRadar].owned() ? kShipRadar : 0) | (s.parts[kPartJet1].owned() ? kShipLeftJet : 0)
		                                         | (s.parts[kPartJet2].owned() ? kShipRightJet : 0));
		const std::uint8_t missing = std::uint8_t(wanted & ~s.shipEffect);
		if (missing) repair(24, Kind::SetShipEffectBits, -1, -1, missing, "an owned part's ship capability bit is missing");

		// S15: tutorial flag still set although the engine is owned.
		if (s.tutorialMode && s.parts[kPartMainEngine].owned() && !s.endDayActive) {
			repair(15, Kind::SetTutorialComplete, -1, -1, 0, "the engine is owned but the tutorial flag is still set");
		}

		// S14: a first-delivery marker with no part behind it at all.
		if (s.collectEngineFlag && owned == 0 && !s.deliveryPending && worldBuilt()) {
			bool anyMissing = false;
			for (const PartState& p : s.parts) anyMissing |= p.source == Source::Missing;
			if (!anyMissing) {
				if (s.sourceInventoryComplete) {
					repair(14, Kind::ClearPrematureEngineFlag, -1, -1, 0, "first delivery was recorded but no part is owned or arriving");
				} else {
					defer(14, Kind::Defer, Reason::PrematureMarkerUnsafe, -1, -1, "the part inventory is incomplete");
				}
			}
		}
	}

	// -- Onions ------------------------------------------------------------

	bool trusted() const { return !s.prov.syntheticDemoFlags; }

	bool evidenceOf(int c, bool partsUsable) const
	{
		const ColourState& cs = s.colours[c];
		bool e                = cs.booted || cs.nonSeed() > 0 || (trusted() && (cs.discovered || cs.plucked));
		if (c == Red && partsUsable && s.ownedCount() > 0) e = true;
		if (c == Red && trusted() && s.collectEngineFlag) e = true;
		return e;
	}

	void planBootstrap()
	{
		// S16: partial Skip Tutorial / Randomizer bootstrap.
		if (!s.prov.bootstrapMismatch) return;
		if (!s.prov.bootstrapKnown) {
			diagnose(16, Reason::AmbiguousHistory, -1, -1, "the start-up transaction left no provenance to complete");
			return;
		}
		// Complete flags only. Parts and Pikmin come back through the individual rules, never as a grant.
		const ColourState& red = s.colours[Red];
		if (!red.unlocked) repair(16, Kind::SetUnlocked, Red, -1, 0, "start-up left red locked");
		if (!red.displayed) repair(16, Kind::SetDisplayed, Red, -1, 0, "start-up left red hidden");
		if (!red.discovered) repair(16, Kind::SetDiscovered, Red, -1, 0, "start-up left the red Onion undiscovered");
		if (!red.booted && worldBuilt() && red.onionPresent && !red.movieQueued) repair(16, Kind::SetBooted, Red, -1, 0, "start-up left the red Onion unbooted");
		if (s.tutorialMode && !s.endDayActive) repair(16, Kind::SetTutorialComplete, -1, -1, 0, "start-up left the tutorial flag set");
	}

	void planColours(bool partsUsable)
	{
		const int owned = partsUsable ? s.ownedCount() : 0;
		bool anyEvidence = false;
		bool anyViable   = false;
		for (int c = 0; c < kColours; ++c) anyViable |= s.colours[c].viable() > 0;

		for (int c = 0; c < kColours; ++c) {
			const ColourState& cs = s.colours[c];
			const bool evidence     = evidenceOf(c, partsUsable);
			anyEvidence |= evidence;
			const bool pluckHistory = trusted() && cs.plucked && (cs.discovered || cs.booted);

			// S05, S06, S07: unlock and display follow a proven first pluck.
			if (c != Red) {
				const int scn = c == Yellow ? 5 : 6;
				if (!cs.unlocked) {
					if (cs.nonSeed() > 0) {
						repair(scn, Kind::SetUnlocked, c, -1, 0, "this colour has Pikmin but its Onion is still locked");
						if (!cs.displayed) repair(scn, Kind::SetDisplayed, c, -1, 0, "the count was hidden");
						if (!cs.plucked) repair(scn, Kind::SetPlucked, c, -1, 0, "Pikmin of this colour exist, so a first pluck happened");
					} else if (pluckHistory) {
						repair(7, Kind::SetUnlocked, c, -1, 0, "the first pluck was recorded but the unlock was not");
						if (!cs.displayed) repair(7, Kind::SetDisplayed, c, -1, 0, "the count was hidden");
					}
				} else if (!cs.displayed && (cs.nonSeed() > 0 || pluckHistory)) {
					repair(7, Kind::SetDisplayed, c, -1, 0, "the count was hidden although the colour is in use");
				}
			} else {
				// S03: red is unlocked at new-game initialization, so a missing bit is a fault.
				if (!cs.unlocked && (evidence || owned > 0 || s.day >= 2)) {
					repair(3, Kind::SetUnlocked, c, -1, 0, "red is unlocked on every new game");
					if (!cs.displayed) repair(3, Kind::SetDisplayed, c, -1, 0, "red's count is shown on every new game");
				} else if (cs.unlocked && !cs.displayed && (evidence || owned > 0)) {
					repair(3, Kind::SetDisplayed, c, -1, 0, "red's count is shown on every new game");
				}
			}

			// Boot state: only for a colour with proven acquisition and a built Onion.
			if (evidence && !cs.booted && !cs.movieQueued && worldBuilt() && cs.onionPresent) {
				const int scn = (cs.discovered && cs.nonSeed() == 0 && !cs.plucked) ? 4 : (!anyViable ? 11 : 8);
				repair(scn, Kind::SetBooted, c, -1, 0, "the Onion was acquired but never booted");
			}
			if (cs.booted && !cs.discovered) {
				repair(8, Kind::SetDiscovered, c, -1, 0, "a booted Onion must not be discovered again");
			}

			// S09: an acquired Onion whose object is gone from the built stage.
			if (evidence && (cs.booted || cs.unlocked) && worldBuilt() && !cs.onionPresent) {
				if (!s.sourceInventoryComplete) {
					defer(9, Kind::Defer, Reason::InventoryIncomplete, c, -1, "the stage inventory is incomplete");
				} else if (sourcesBlockedByProvenance()) {
					defer(9, Kind::Defer, Reason::SourceSetUnresolved, c, -1, "no single authoritative source set yet");
				} else if (cs.onionExpected) {
					repair(9, Kind::RestoreOnionObject, c, -1, 0, "an acquired Onion is missing from the built stage");
				} else {
					defer(9, Kind::Defer, Reason::UnsupportedStage, c, -1, "this stage does not define the Onion");
				}
			}

			// Seeds: S01, S02, S03, S10, S11, S12, S13.
			if (cs.booted && evidence && populationBuilt() && cs.viable() == 0) {
				bool wants  = false;
				int scn     = 10;
				if (c != Red && !cs.unlocked) {
					wants = !cs.bonusSeedQueued;
					scn   = c == Yellow ? 1 : 2;
				} else {
					wants = !cs.bonusSeedQueued;
					scn   = (c == Red && !cs.plucked) ? 3 : 10;
				}
				if (wants) {
					if (!cs.onionPresent) {
						// Waits for RestoreOnionObject (S09) or reports why it cannot.
					} else if (!s.seedCapacity) {
						defer(13, Kind::Defer, Reason::AllocationUnavailable, c, -1, "no free seed slot right now");
					} else {
						repair(scn, Kind::ReplaceSeed, c, -1, 0, "no seed, sprout or Pikmin of this colour exists anywhere");
					}
				}
			}
		}

		// S30: after the repairs, a save with nothing that can grow stays a diagnosis, never a grant.
		if (populationBuilt() && !anyViable && anyEvidence) {
			bool seeding = false;
			for (const Action& a : out) {
				seeding |= (a.kind == Kind::ReplaceSeed && a.disposition == Disposition::Repair)
				        || (a.kind == Kind::Defer && a.reason == Reason::AllocationUnavailable);
			}
			bool onionPending = false;
			for (const Action& a : out) onionPending |= a.kind == Kind::RestoreOnionObject || a.kind == Kind::SetBooted;
			if (!seeding && !onionPending) diagnose(30, Reason::NoGrowthPath, -1, -1, "no Pikmin, seed or Onion that could start growth");
		}
		if (s.feasibility.settingsIncompatible) diagnose(30, Reason::SettingsIncompatible, -1, -1, "the active limits cannot carry the remaining parts");
		if (s.feasibility.noRenewableFood) diagnose(30, Reason::NoFood, -1, -1, "no renewable food source remains");
	}

	// -- Routes ------------------------------------------------------------

	void planObstacles()
	{
		if (!worldBuilt()) return;
		for (const Obstacle& o : s.obstacles) {
			const int scn = o.kind == ObstacleKind::Wall ? 25 : o.kind == ObstacleKind::Bridge ? 26 : 27;
			const int kindValue = int(o.kind) * 1000 + o.id;
			if (!o.dataValid) {
				diagnose(scn, Reason::InvalidData, -1, -1, "saved obstacle numbers fail validation");
			} else if (o.stalled && o.kind == ObstacleKind::Box) {
				repair(scn, Kind::NormalizeObstacle, -1, -1, kindValue, "an in-progress push state would stall the route");
			} else if (o.physicalDone != o.routeOpen) {
				repair(scn, Kind::RebuildObstacleRoute, -1, -1, kindValue, "the route disagrees with the saved physical state");
			}
		}
	}

	// -- Part sources ------------------------------------------------------

	void planParts(bool partsUsable, bool partsBlocked)
	{
		if (!partsUsable || !worldBuilt()) return;
		for (int i = 0; i < kMaxParts; ++i) {
			const PartState& p = s.parts[i];
			if (p.owned()) {
				// S22: collected truth stays authoritative; only the replicas go.
				if (p.replicas > 0) repair(22, Kind::SuppressPartReplica, -1, i, p.replicas, "a collected part still has a live or cached copy");
				continue;
			}
			if (partsBlocked) {
				if (p.source == Source::Missing || p.source == Source::OutOfBounds || p.copies > 1) {
					diagnose(29, Reason::ManifestUnsupported, -1, i, "the seeded part layout cannot be trusted");
				}
				continue;
			}
			if (p.copies > 1 && p.source != Source::Missing) {
				repair(22, Kind::CollapsePartDuplicates, -1, i, p.copies - 1, "an unowned part has more than one source");
				continue;
			}
			switch (p.source) {
			case Source::Missing:
			{
				const int scn = p.why == MissingWhy::CacheRefused ? 19 : p.why == MissingWhy::CarrierConsumed ? 21 : 20;
				if (!s.sourceInventoryComplete) {
					defer(scn, Kind::Defer, Reason::InventoryIncomplete, -1, i, "the stage inventory is incomplete");
				} else if (sourcesBlockedByProvenance()) {
					defer(scn, Kind::Defer, Reason::SourceSetUnresolved, -1, i, "no single authoritative source set yet");
				} else if (!s.catalogueCompatible || !p.anchorKnown) {
					diagnose(scn, Reason::NoAnchor, -1, i, "no compatible source anchor is known");
				} else if (p.why == MissingWhy::CarrierConsumed && !p.carrierCanDrop) {
					diagnose(scn, Reason::CarrierCannotDrop, -1, i, "the assigned carrier cannot drop this part");
				} else {
					repair(scn, Kind::RestorePartSource, -1, i, 0, "no source of this part exists anywhere on a complete inventory");
				}
				break;
			}
			case Source::OutOfBounds:
				if (s.sourceInventoryComplete && s.catalogueCompatible && p.anchorKnown) {
					repair(23, Kind::MovePartToAnchor, -1, i, 0, "the only copy of this part is outside the playable stage");
				} else {
					diagnose(23, Reason::NoAnchor, -1, i, "the part is out of bounds and no anchor is known");
				}
				break;
			case Source::Unsupported:
				diagnose(20, Reason::UnsupportedStage, -1, i, "this stage or asset is not in the catalogue");
				break;
			default: break;
			}
		}
	}

	void planGeneration()
	{
		switch (s.generation.fault) {
		case GenFault::None: break;
		case GenFault::VisitedWithoutCache:
		case GenFault::DuplicateCacheAndInit:
			if (!s.generation.sourceSetResolved) diagnose(28, Reason::SourceSetUnresolved, -1, -1, "cache and initial generators disagree");
			break;
		case GenFault::LimitedFlagStale:
			if (s.generation.limitedProven && s.generation.stage >= 0 && s.generation.index >= 0) {
				repair(28, Kind::FixLimitedSource, -1, -1, s.generation.stage * 256 + s.generation.index, "a limited source is wrongly marked used");
			} else {
				diagnose(28, Reason::AmbiguousHistory, -1, -1, "a limited source may be wrongly marked used");
			}
			break;
		}
	}

	void run()
	{
		// Gate: S32.
		if (!s.prov.formatValid) {
			diagnose(32, Reason::SaveInvalid, -1, -1, "the save failed its checksum or structure checks");
			return;
		}
		if (!s.prov.slotIdentityValid) {
			diagnose(32, Reason::SlotMismatch, -1, -1, "the save does not belong to this slot");
			return;
		}
		if (s.prov.permadeathTerminated) return;

		if (!s.dayValid()) diagnose(31, Reason::InvalidDay, -1, -1, "the day is outside the supported range");
		if (!s.dayMatchesClock) diagnose(31, Reason::DayDisagreesWithClock, -1, -1, "the saved day and the world clock disagree");

		bool partsBlocked = false;
		if (s.randomizer.active
		    && (!s.prov.rulesValid || !s.randomizer.versionKnown || !s.randomizer.manifestMatches || !s.randomizer.identitiesUnique)) {
			partsBlocked = true;
			diagnose(29, Reason::ManifestUnsupported, -1, -1, "the Randomizer rules or seeded layout do not match");
		}

		bool partsUsable = false;
		planOwnership(partsUsable);
		planBootstrap();
		planColours(partsUsable);
		planObstacles();
		planGeneration();
		planParts(partsUsable, partsBlocked);
	}
};

void sortAndDedupe(std::vector<Action>& actions)
{
	std::stable_sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) {
		return std::make_tuple(int(a.kind), a.colour, a.part, a.value, int(a.disposition), a.scenario, int(a.reason))
		     < std::make_tuple(int(b.kind), b.colour, b.part, b.value, int(b.disposition), b.scenario, int(b.reason));
	});
	// One entry per key: the first (lowest scenario number) wins.
	std::vector<Action> unique;
	std::set<std::string> seen;
	for (Action& a : actions) {
		if (seen.insert(a.key).second) unique.push_back(std::move(a));
	}
	actions = std::move(unique);
}

} // namespace

RepairPlan planRepairs(const ProgressionSnapshot& snapshot)
{
	RepairPlan plan;
	switch (snapshot.mode) {
	case Mode::Versus:
	case Mode::Mission:
	case Mode::Challenge:
	case Mode::Debug: return plan; // synthetic worlds never touch campaign state
	case Mode::Netplay:
	{
		Planner p(snapshot);
		p.diagnose(0, Reason::NetplayRefused, -1, -1, "no host-only repair in a synchronized session");
		plan.actions = std::move(p.out);
		return plan;
	}
	case Mode::Speedrun:
	{
		ProgressionSnapshot asStory = snapshot;
		asStory.mode                = Mode::Story;
		const RepairPlan inner      = planRepairs(asStory);
		if (inner.hasRepairs()) {
			Planner p(snapshot);
			p.diagnose(0, Reason::SpeedrunProtected, -1, -1, "a ranked or practice run is never changed silently");
			plan.actions = std::move(p.out);
		}
		return plan;
	}
	case Mode::Story:
	case Mode::CoopStory: break;
	}

	Planner planner(snapshot);
	planner.run();
	plan.actions = std::move(planner.out);
	sortAndDedupe(plan.actions);
	return plan;
}

// ---------------------------------------------------------------------------
// Projection and fixed point.

void projectAction(ProgressionSnapshot& s, const Action& a)
{
	auto colour = [&]() -> ColourState* { return a.colour >= 0 && a.colour < kColours ? &s.colours[a.colour] : nullptr; };
	auto part   = [&]() -> PartState* { return a.part >= 0 && a.part < kMaxParts ? &s.parts[a.part] : nullptr; };
	switch (a.kind) {
	case Kind::SetUnlocked: if (ColourState* c = colour()) c->unlocked = true; break;
	case Kind::SetBooted: if (ColourState* c = colour()) c->booted = true; break;
	case Kind::SetDisplayed: if (ColourState* c = colour()) c->displayed = true; break;
	case Kind::SetDiscovered: if (ColourState* c = colour()) c->discovered = true; break;
	case Kind::SetPlucked: if (ColourState* c = colour()) c->plucked = true; break;
	case Kind::SetCourseOpen: s.courseOpen = std::uint8_t(s.courseOpen | (1 << a.value)); break;
	case Kind::SetUpgradeLevel: s.upgradeLevel = std::uint8_t(a.value); break;
	case Kind::SetShipEffectBits: s.shipEffect = std::uint8_t(s.shipEffect | a.value); break;
	case Kind::SetCurrParts: s.currParts = a.value; break;
	case Kind::SetRequiredParts: s.requiredParts = a.value; break;
	case Kind::SetTutorialComplete: s.tutorialMode = false; break;
	case Kind::ClearPrematureEngineFlag: s.collectEngineFlag = false; break;
	case Kind::RestoreOnionObject: if (ColourState* c = colour()) c->onionPresent = true; break;
	case Kind::ReplaceSeed: if (ColourState* c = colour()) c->pending += 1; break;
	case Kind::RestorePartSource:
		if (PartState* p = part()) {
			p->source = Source::Loose;
			p->copies = 1;
			p->why    = MissingWhy::Unspecified;
		}
		break;
	case Kind::MovePartToAnchor: if (PartState* p = part()) p->source = Source::Loose; break;
	case Kind::CollapsePartDuplicates: if (PartState* p = part()) p->copies = 1; break;
	case Kind::SuppressPartReplica: if (PartState* p = part()) p->replicas = 0; break;
	case Kind::RebuildObstacleRoute:
		for (Obstacle& o : s.obstacles) {
			if (int(o.kind) * 1000 + o.id == a.value) o.routeOpen = o.physicalDone;
		}
		break;
	case Kind::NormalizeObstacle:
		for (Obstacle& o : s.obstacles) {
			if (int(o.kind) * 1000 + o.id == a.value) o.stalled = false;
		}
		break;
	case Kind::FixLimitedSource: s.generation.fault = GenFault::None; break;
	case Kind::Diagnose:
	case Kind::Defer: break;
	}
}

RepairResult repairToFixedPoint(const ProgressionSnapshot& snapshot, ActionExecutor* executor, int maxPasses)
{
	RepairResult result;
	result.final = snapshot;
	std::set<std::string> appliedKeys;
	std::set<std::string> failedKeys;

	for (int pass = 0; pass < maxPasses; ++pass) {
		result.passes             = pass + 1;
		const RepairPlan plan     = planRepairs(result.final);
		bool progressed           = false;
		for (const Action& a : plan.actions) {
			if (a.disposition != Disposition::Repair) continue;
			if (appliedKeys.count(a.key) || failedKeys.count(a.key)) continue;
			if (executor && !executor->execute(a)) {
				failedKeys.insert(a.key);
				result.failed.push_back(a);
				continue;
			}
			projectAction(result.final, a);
			appliedKeys.insert(a.key);
			result.applied.push_back(a);
			progressed = true;
		}
		if (!progressed) {
			result.stable = true;
			break;
		}
	}

	result.residual = planRepairs(result.final);
	// Actions the executor refused stay in the residual plan as deferred.
	std::vector<Action> residual;
	for (Action& a : result.residual.actions) {
		if (a.disposition == Disposition::Repair) {
			if (failedKeys.count(a.key)) {
				a.disposition = Disposition::Defer;
				a.reason      = Reason::AllocationUnavailable;
				residual.push_back(std::move(a));
			}
			// An applied Repair that still shows up would be an oscillation; the test suite asserts it never happens.
			continue;
		}
		residual.push_back(std::move(a));
	}
	result.residual.actions = std::move(residual);
	return result;
}

// ---------------------------------------------------------------------------
// Notes.

std::vector<std::string> describeApplied(const std::vector<Action>& applied)
{
	std::vector<std::string> lines;
	auto push = [&lines](const std::string& line) {
		if (std::find(lines.begin(), lines.end(), line) == lines.end()) lines.push_back(line);
	};
	for (const Action& a : applied) {
		switch (a.kind) {
		case Kind::ReplaceSeed: push(std::string("A ") + colourName(a.colour) + " seed was replaced at its Onion."); break;
		case Kind::SetUnlocked:
		case Kind::SetDisplayed:
		case Kind::SetPlucked:
			if (a.colour == Red) push("The red Onion's starting state was restored.");
			else push(std::string("The ") + colourName(a.colour) + " Onion was unlocked because you already have " + colourName(a.colour) + " Pikmin.");
			break;
		case Kind::SetBooted:
		case Kind::SetDiscovered: push(std::string("The ") + colourName(a.colour) + " Onion's start-up state was restored."); break;
		case Kind::RestoreOnionObject: push(std::string("The ") + colourName(a.colour) + " Onion was put back in this area."); break;
		case Kind::SetCourseOpen: push("An area that your ship parts already unlock was opened."); break;
		case Kind::SetUpgradeLevel:
		case Kind::SetCurrParts:
		case Kind::SetRequiredParts:
		case Kind::SetShipEffectBits: push("Ship part counters were rebuilt from the parts you own."); break;
		case Kind::SetTutorialComplete: push("A leftover tutorial flag was cleared."); break;
		case Kind::ClearPrematureEngineFlag: push("A first-delivery marker with no part behind it was cleared."); break;
		case Kind::RestorePartSource:
		case Kind::MovePartToAnchor: push("A lost ship part was put back for you to collect."); break;
		case Kind::CollapsePartDuplicates:
		case Kind::SuppressPartReplica: push("A duplicate copy of a ship part was removed."); break;
		case Kind::RebuildObstacleRoute:
		case Kind::NormalizeObstacle: push("A wall, bridge or box route was rebuilt from its saved progress."); break;
		case Kind::FixLimitedSource: push("A one-time source that was marked used by mistake was reset."); break;
		case Kind::Diagnose:
		case Kind::Defer: break;
		}
	}
	return lines;
}

} // namespace pc::progression
