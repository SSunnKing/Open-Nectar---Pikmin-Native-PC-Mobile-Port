#pragma once

/**
 * @file pc_progression_game.h
 * @brief Runs the stuck-save repair inside the game (see pc_progression_repair.h for the rules).
 *
 * Four hooks, all on the normal load and save path:
 *
 *   loadCurrentGame     pc_progression_note_loaded_image   remember the file as it was loaded
 *   readCurrentGame     pc_progression_global_phase        counters and area access, before any map is chosen
 *   finalSetup          pc_progression_stage_phase         Onions and seeds, once the stage and its sprouts exist
 *   saveCurrentGame     pc_progression_before_save         back the loaded file up first; refuse the save if that fails
 *
 * Nothing is written to disk by the repair itself. The repaired state reaches
 * the disk through the ordinary day-end save. Speedrun and practice runs,
 * Versus, custom maps, Challenge Mode, debug worlds and netplay are never
 * changed (the planner reports and skips them).
 */

#include <cstddef>

void pc_progression_note_loaded_image(const void* data, std::size_t size);
void pc_progression_global_phase(void);
void pc_progression_stage_phase(void);

/** Returns false when the save must be refused because the backup could not be made. */
bool pc_progression_before_save(void);
void pc_progression_after_save(bool succeeded);

/** The note for the screen: lines of plain text and how long it has been up. nullptr when there is none. */
const char* const* pc_progression_note_lines(int* count, float* ageSeconds);
