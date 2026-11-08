#pragma once

/**
 * @file pc_progression_saved.h
 * @brief The decoded save record, the world inventory, and the load adapters around the planner.
 *
 * pc_progression_repair.h decides; this file is the boundary to the rest of
 * the program. Two adapters feed it:
 *
 *   - SavedProgression: the persisted progression of a loaded save (flags,
 *     part table, counters, per-colour populations). The game fills it from
 *     PlayerState right after the card loads. Tests fill it from portable
 *     scenario files, so no disc, asset or real save is involved.
 *   - WorldInventory: what the built stage contains (live Pikmin, seeds,
 *     Onions, part sources, obstacles). The game fills it at the start of
 *     finalSetup, after the saved sprouts have been restored.
 *
 * buildSnapshot() joins them into a ProgressionSnapshot. applyToSaved() writes
 * a repair's factual assignments back into the record, which is the same set
 * of fields the game writes back into PlayerState.
 *
 * The scenario text format (parseScenario, serializeSaved) is the "decoded
 * save fixture": small, human-readable and free of copyrighted data.
 */

#include "pc_progression_repair.h"

#include <array>
#include <string>
#include <vector>

namespace pc::progression {

struct SavedColour {
	bool unlocked   = false;
	bool booted     = false;
	bool displayed  = false;
	bool discovered = false;
	bool plucked    = false;
	int stored      = 0; ///< Pikmin in the Onion (all growth stages)
	int stageSprouts = 0; ///< serialized sprouts of the loaded stage, restored when the stage is built
	int offStage    = 0; ///< serialized sprouts of the other stages
};

struct SavedProgression {
	Mode mode = Mode::Story;
	Provenance prov;
	RandomizerInfo randomizer;

	int day               = 2;
	bool dayMatchesClock  = true;
	std::array<int, kAreaCount> areaParts = { 1, 6, 12, 29 };
	int perfectParts      = 30;

	bool partsKnown   = true;
	bool partIdsKnown = true;
	std::array<std::uint8_t, kMaxParts> partVis {}; ///< Vis per slot (per part index when partIdsKnown)
	int currParts             = 0;
	int requiredParts         = 0;
	std::uint8_t upgradeLevel = 0;
	std::uint8_t shipEffect   = 8;
	std::uint8_t courseOpen   = 1;

	bool tutorialMode  = false;
	bool collectEngine = false;
	bool endDayActive  = false;

	std::array<SavedColour, kColours> colours {};
};

struct WorldColour {
	int live             = 0;
	int sprouts          = 0; ///< seeds and sprouts on the stage, flying ones included
	int pending          = 0;
	bool bonusPathOpen   = false; ///< the extinction movie will supply a seed once the colour is unlocked and empty
	bool onionPresent    = false;
	bool onionExpected   = false;
};

struct WorldInventory {
	std::array<WorldColour, kColours> colours {};
	std::array<PartState, kMaxParts> parts {}; ///< source, copies, replicas and anchors (visibility comes from the save)
	bool sourceInventoryComplete = true;
	bool catalogueCompatible     = true;
	bool seedCapacity            = true;
	bool deliveryPending         = false; ///< a first-delivery absorption is in progress
	GenProvenance generation;
	std::vector<Obstacle> obstacles;
	Feasibility feasibility;
};

/** Join a save record and, when the stage is built, its inventory into a snapshot. */
ProgressionSnapshot buildSnapshot(const SavedProgression& saved, const WorldInventory* world);

/** Write one Repair action's factual assignment into the record. Requests (seeds, sources, routes) are not saved state. */
void applyToSaved(SavedProgression& saved, const Action& action);

/** applyToSaved for each action in order. */
void applyAllToSaved(SavedProgression& saved, const std::vector<Action>& applied);

/** True when the action edits saved state (as opposed to asking the world to do something). */
bool isFactual(Kind kind);

// ---------------------------------------------------------------------------
// Scenario files.

struct ScenarioRecord {
	std::string name;
	SavedProgression saved;
	bool hasWorld = false;
	WorldInventory world;
};

/** Parse a scenario file. Returns false and fills error (with a line number) on malformed input. */
bool parseScenario(const std::string& text, ScenarioRecord& out, std::string& error);

/** Canonical text of the [saved] section: what a normal save would persist. Parsing it gives the same record back. */
std::string serializeSaved(const SavedProgression& saved);

} // namespace pc::progression
