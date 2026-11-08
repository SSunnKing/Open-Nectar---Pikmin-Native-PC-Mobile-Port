#pragma once

/**
 * @file pc_progression_repair.h
 * @brief Decides which saved progression is inconsistent and how to repair it.
 *
 * A save can remember "I showed you this" without remembering the thing the
 * scene was meant to give. This file holds the engine-independent decision
 * function: a ProgressionSnapshot goes in, a RepairPlan comes out. There is no
 * SDL, filesystem, RNG, asset, achievement or movie code here, so it runs in
 * host tests and on every platform.
 *
 * The rules, in the order the plan applies them:
 *   1. Gate: the save must be a complete valid decode of a story run. A
 *      partially decoded inventory is unknown, never zero.
 *   2. Ownership truth: the owned part IDs rebuild the part counters, the ship
 *      level, the ship capability bits and every earned area bit.
 *   3. Onion state: boot, discovery, unlock and display, from proven evidence.
 *   4. Population: one seed per colour that has none anywhere, only after the
 *      stage is fully built, and only when the normal extinction seed is not
 *      already coming.
 *   5. Routes and part sources, from the authoritative stage inventory.
 *   6. Diagnostics for what cannot be decided safely.
 *
 * It never grants extra days, Pikmin or easier settings. A missing seed is
 * replaced instead of unlocking the colour. A missing ship part is restored as
 * a source for the player to collect, never marked collected, and only when the
 * whole stage inventory proves it is gone.
 *
 * repairToFixedPoint() repeats plan, apply, plan until nothing changes. On a
 * healthy snapshot the plan is empty, and applying a plan then planning again
 * proposes nothing new, so running it twice changes nothing.
 */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pc::progression {

// ---------------------------------------------------------------------------
// Fixed facts from the original game (colours, parts, areas).

/** Same values as the game's PikiColor enum (and the Onion event sender order). */
enum Colour : int { Blue = 0, Red = 1, Yellow = 2 };
constexpr int kColours = 3;

constexpr int kMaxParts       = 30;
constexpr int kMaxDays        = 30;
constexpr int kStageCount     = 5; // Impact Site, Forest of Hope, Navel, Spring, Final Trial
constexpr int kAreaCount      = 4; // areas that open by part count
constexpr int kRequiredParts  = 25;

constexpr std::uint32_t fourcc(char a, char b, char c, char d)
{
	return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) | (std::uint32_t(std::uint8_t(c)) << 8)
	     | std::uint32_t(std::uint8_t(d));
}

/** The 30 ship parts, in include/Pellet.h order. Index in this table is the part index used everywhere below. */
constexpr std::array<std::uint32_t, kMaxParts> kPartIds = {
	fourcc('u', 's', 't', '1'), fourcc('u', 's', 't', '2'), fourcc('u', 's', 't', '3'), fourcc('u', 's', 't', '4'),
	fourcc('u', 's', 't', '5'), // Main Engine
	fourcc('u', 'f', '0', '1'), // Whimsical Radar
	fourcc('u', 'f', '0', '2'), fourcc('u', 'f', '0', '3'), fourcc('u', 'f', '0', '4'), fourcc('u', 'f', '0', '5'),
	fourcc('u', 'f', '0', '6'), fourcc('u', 'f', '0', '7'), fourcc('u', 'f', '0', '8'), fourcc('u', 'f', '0', '9'),
	fourcc('u', 'f', '1', '0'), // Ionium Jet 1
	fourcc('u', 'f', '1', '1'), // Ionium Jet 2
	fourcc('u', 'n', '0', '1'), fourcc('u', 'n', '0', '2'), fourcc('u', 'n', '0', '3'),
	fourcc('u', 'n', '0', '4'), // Nova Blaster (optional)
	fourcc('u', 'n', '0', '5'), fourcc('u', 'n', '0', '6'), fourcc('u', 'n', '0', '7'), fourcc('u', 'n', '0', '8'),
	fourcc('u', 'n', '0', '9'), // Space Float (optional)
	fourcc('u', 'n', '1', '0'), // Massage Machine (optional)
	fourcc('u', 'n', '1', '1'), // Secret Safe (optional)
	fourcc('u', 'n', '1', '2'), fourcc('u', 'n', '1', '3'),
	fourcc('u', 'n', '1', '4'), // UV Lamp (optional)
};

constexpr int kPartMainEngine = 4;
constexpr int kPartRadar      = 5;
constexpr int kPartJet1       = 14;
constexpr int kPartJet2       = 15;

/** The five parts that do not count toward the 25 the happy ending needs. */
constexpr bool partIsRequired(int index)
{
	return !(index == 19 || index == 24 || index == 25 || index == 26 || index == 29);
}

/** Part index for a part ID, or -1. */
int partIndexOf(std::uint32_t id);

/** Stage IDs (StageID in the game): the area bits of the course-open mask. */
enum Stage : int { StagePractice = 0, StageForest = 1, StageCave = 2, StageYakushima = 3, StageLast = 4 };

// Ship capability bits (mShipEffectPartFlag).
constexpr std::uint8_t kShipRadar     = 1;
constexpr std::uint8_t kShipLeftJet   = 2;
constexpr std::uint8_t kShipRightJet  = 4;

// ---------------------------------------------------------------------------
// Snapshot: everything the decision may look at.

enum class Mode : std::uint8_t {
	Story,        ///< ordinary single-player story run
	CoopStory,    ///< local co-op story run (deterministic common plan)
	Versus,       ///< VS arena (synthetic world, story ownership untouched)
	Mission,      ///< custom map (synthetic world)
	Challenge,    ///< Challenge Mode
	Debug,        ///< debug/test worlds that set every demo flag
	Netplay,      ///< synchronized session: no host-only repair
	Speedrun      ///< Speedrun and its practice saves: never mutated silently
};

/** How much of a measurement is known. Absence is only actionable when Materialized. */
enum class Known : std::uint8_t {
	Unknown,      ///< not decoded, or decoded partially
	Saved,        ///< decoded from the save only (global phase)
	Materialized  ///< the stage is fully built and its inventory is complete
};

struct Provenance {
	bool formatValid          = true;  ///< checksum, length and section bounds all passed
	bool slotIdentityValid    = true;  ///< slot and save identity match
	bool rulesValid           = true;  ///< rule block (Randomizer, Hard) present and understood
	bool permadeathTerminated = false; ///< a run that is intentionally over
	bool syntheticDemoFlags   = false; ///< debug/challenge/VS set every demo bit: flags prove nothing
	bool bootstrapKnown       = false; ///< Skip Tutorial / Randomizer bootstrap provenance is recorded
	bool bootstrapMismatch    = false; ///< the state disagrees with that bootstrap's canonical result
	std::uint64_t lineage     = 0;     ///< save lineage, part of every repair key
};

struct RandomizerInfo {
	bool active          = false;
	bool versionKnown    = true;
	bool manifestMatches = true;
	bool identitiesUnique = true;
};

struct ColourState {
	bool unlocked   = false; ///< hasContainer
	bool booted     = false; ///< hasBootContainer
	bool displayed  = false; ///< setDisplayPikiCount
	bool discovered = false; ///< DEMOFLAG_Discover*Onyon
	bool plucked    = false; ///< DEMOFLAG_Pluck*Pikmin
	bool movieQueued = false; ///< a discovery/boot movie for this colour is still queued

	int stored          = 0; ///< Pikmin in the Onion (leaf, bud, flower)
	int live            = 0; ///< Pikmin out on the field
	int sproutsLoaded   = 0; ///< seeds and sprouts in the loaded stage, flying ones included
	int sproutsOffStage = 0; ///< saved sprouts of the other stages
	int pending         = 0; ///< committed emissions not yet landed (replacement seeds reserved by this pass)

	bool bonusPathOpen = false; ///< the normal extinction movie would run for this colour once it is unlocked and empty (not tutorial, not already played, no one left)
	bool onionPresent    = false; ///< the Onion object exists in the loaded stage
	bool onionExpected   = false; ///< the loaded stage is known to define this Onion

	int nonSeed() const { return stored + live; }
	int viable() const { return stored + live + sproutsLoaded + sproutsOffStage + pending; }
};

enum class Vis : std::uint8_t { Uncollected = 0, Visible = 1, Invisible = 2 };

/** Where an unowned part currently is. Anything but Missing means it is not lost. */
enum class Source : std::uint8_t {
	Unknown,         ///< the inventory is incomplete: no decision
	Loose,           ///< loose part, cached or live
	Carried,         ///< inside a carrier on its way to the ship
	CarrierAlive,    ///< inside a live enemy or boss
	DropPending,     ///< dead carrier, drop still settling
	Absorbing,       ///< half absorbed by the ship
	FutureGenerator, ///< a day-limited or later generator will still produce it
	Missing,         ///< none of the above, on a complete inventory
	OutOfBounds,     ///< exists but its position is non-finite or outside the stage
	Unsupported      ///< custom stage or asset the catalogue does not know
};

/** Why a Missing part is missing. Only the label of the scenario changes. */
enum class MissingWhy : std::uint8_t { Unspecified, CacheRefused, OneShotConsumed, CarrierConsumed };

struct PartState {
	Vis vis                 = Vis::Uncollected;
	Source source           = Source::Loose;
	MissingWhy why          = MissingWhy::Unspecified;
	int copies              = 1;     ///< recoverable sources for an unowned part
	int replicas            = 0;     ///< live or cached copies of an owned part
	bool anchorKnown        = true;  ///< the compatible catalogue names a safe source anchor
	bool carrierCanDrop     = true;  ///< Randomizer: the assigned carrier can drop this identity
	bool owned() const { return vis != Vis::Uncollected; }
};

enum class ObstacleKind : std::uint8_t { Wall, Bridge, Box };

struct Obstacle {
	ObstacleKind kind = ObstacleKind::Wall;
	int id            = 0;
	bool dataValid    = true;  ///< saved numbers pass bounds validation
	bool physicalDone = false; ///< saved physical state says it is finished (wall destroyed, bridge done, box pushed)
	bool routeOpen    = false; ///< the route/waypoint binding after load says open
	bool stalled      = false; ///< in-progress state is invalid and would stall the route (box)
};

enum class GenFault : std::uint8_t {
	None,
	VisitedWithoutCache, ///< visit bit set but no usable cache
	DuplicateCacheAndInit,
	LimitedFlagStale     ///< a specific limited progression source is wrongly suppressed
};

struct GenProvenance {
	GenFault fault         = GenFault::None;
	bool sourceSetResolved = true;  ///< one authoritative source set (cache or init) is chosen
	bool limitedProven     = false; ///< the wrongly suppressed record is identified
	int stage              = -1;
	int index              = -1;
};

struct Feasibility {
	bool settingsIncompatible = false; ///< carry minimum cannot be met under the active limits
	bool noRenewableFood      = false; ///< no food source left that could grow the army
};

struct ProgressionSnapshot {
	Mode mode = Mode::Story;
	Provenance prov;
	RandomizerInfo randomizer;

	// Day: one-based. Valid is 1..kMaxDays.
	int day              = 2;
	bool dayMatchesClock = true;

	// Active thresholds (loaded AICONST values), Hope/Navel/Spring/Final Trial, and the perfect count.
	std::array<int, kAreaCount> areaParts = { 1, 6, 12, 29 };
	int perfectParts                      = 30;

	// Ownership and derived counters.
	bool partsKnown = true;   ///< the whole owned table decoded (counts are trustworthy)
	bool partIdsKnown = true; ///< each table slot is matched to its part ID (registered after the ship loads)
	std::array<PartState, kMaxParts> parts {};
	int currParts            = 0;
	int requiredParts        = 0;
	std::uint8_t upgradeLevel = 0;
	std::uint8_t shipEffect   = 8; ///< bit 8 is the tutorial camera flag, always preserved
	std::uint8_t courseOpen   = 1 << StagePractice;

	// Story flags.
	bool tutorialMode      = false;
	bool collectEngineFlag = false;
	bool deliveryPending   = false; ///< a first-delivery absorption is in progress
	bool endDayActive      = false;

	// Onions.
	std::array<ColourState, kColours> colours {};
	bool seedCapacity = true; ///< one more seed can be allocated right now

	// World, only meaningful when world == Materialized.
	Known population         = Known::Saved;
	Known world              = Known::Saved;
	bool sourceInventoryComplete = false;
	bool catalogueCompatible = false;
	GenProvenance generation;
	std::vector<Obstacle> obstacles;
	Feasibility feasibility;

	int ownedCount() const
	{
		int n = 0;
		for (const PartState& p : parts) n += p.owned();
		return n;
	}
	int ownedRequiredCount() const
	{
		int n = 0;
		for (int i = 0; i < kMaxParts; ++i) n += parts[i].owned() && partIsRequired(i);
		return n;
	}
	bool dayValid() const { return day >= 1 && day <= kMaxDays; }
};

/** Snapshot of an untouched healthy mid-game save, used as a base for fixtures and tests. */
ProgressionSnapshot makeHealthySnapshot();

// ---------------------------------------------------------------------------
// Plan: typed, keyed, sorted list.

enum class Kind : std::uint8_t {
	// Factual assignments (applied to the loaded save).
	SetUnlocked,
	SetBooted,
	SetDisplayed,
	SetDiscovered,
	SetPlucked,
	SetCourseOpen,
	SetUpgradeLevel,
	SetShipEffectBits,
	SetCurrParts,
	SetRequiredParts,
	SetTutorialComplete,
	ClearPrematureEngineFlag,
	// Requests for the stage (applied to the built world).
	RestoreOnionObject,
	ReplaceSeed,
	RestorePartSource,
	MovePartToAnchor,
	CollapsePartDuplicates,
	SuppressPartReplica,
	RebuildObstacleRoute,
	NormalizeObstacle,
	FixLimitedSource,
	// No mutation.
	Diagnose,
	Defer
};

enum class Disposition : std::uint8_t { Repair, Defer, Diagnose };

enum class Reason : std::uint8_t {
	None,
	SaveInvalid,
	SlotMismatch,
	NetplayRefused,
	SpeedrunProtected,
	InvalidDay,
	ManifestUnsupported,
	AmbiguousHistory,
	InventoryIncomplete,
	AllocationUnavailable,
	UnsupportedStage,
	NoAnchor,
	InvalidData,
	SourceSetUnresolved,
	NoGrowthPath,
	SettingsIncompatible,
	NoFood,
	CarrierCannotDrop,
	DayDisagreesWithClock,
	PrematureMarkerUnsafe
};

struct Action {
	int scenario        = 0; ///< S01..S32 of the spec, 0 for none
	Kind kind           = Kind::Diagnose;
	Disposition disposition = Disposition::Diagnose;
	Reason reason       = Reason::None;
	int colour          = -1;
	int part            = -1;
	int value           = 0;
	std::string key;      ///< stable repair key (lineage, scenario, kind, colour, part, value, schema)
	std::string evidence; ///< short non-spoiling explanation
};

struct RepairPlan {
	std::vector<Action> actions;

	std::vector<const Action*> repairs() const;
	bool hasRepairs() const;
	bool empty() const { return actions.empty(); }
};

/** The decision function. Pure: reads only the snapshot. */
RepairPlan planRepairs(const ProgressionSnapshot& snapshot);

/** Project one Repair action onto a snapshot (used by the fixed point and by tests). */
void projectAction(ProgressionSnapshot& snapshot, const Action& action);

// ---------------------------------------------------------------------------
// Fixed point.

/** Runs a request against the real world. Returns false when it could not be done (no allocation, failed asset). */
class ActionExecutor {
public:
	virtual ~ActionExecutor() = default;
	virtual bool execute(const Action& action) = 0;
};

struct RepairResult {
	ProgressionSnapshot final;
	std::vector<Action> applied; ///< successful Repair actions in application order
	std::vector<Action> failed;  ///< Repair actions the executor refused (kept as deferred)
	RepairPlan residual;         ///< what the final snapshot still reports: defers and diagnostics
	int passes   = 0;
	bool stable  = false;        ///< true when the loop ended because nothing was left to apply
};

constexpr int kMaxPasses = 8;

RepairResult repairToFixedPoint(const ProgressionSnapshot& snapshot, ActionExecutor* executor = nullptr, int maxPasses = kMaxPasses);

// ---------------------------------------------------------------------------
// Text helpers: stable canonical forms for hashing, logs and notes.

const char* kindName(Kind kind);
const char* colourName(int colour);
const char* partName(int index);
std::string canonical(const Action& action);
std::uint64_t planHash(const RepairPlan& plan);

/**
 * Plain, non-spoiling lines for the "Progression repaired" note. Repairs only;
 * one line per colour or part, no save contents.
 */
std::vector<std::string> describeApplied(const std::vector<Action>& applied);

} // namespace pc::progression
