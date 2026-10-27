#pragma once
// Quick Save (experimental): save at any point of a day and later load that
// exact point, by replaying the day.
//
// With the Quick Save setting on (or PIKMIN_QUICKSAVE=1) the game runs in
// deterministic mode from launch and keeps the running day's input in
// memory, one PKNI v4 record per tick from the tick after the day starts.
//
// Save (the Quick Save action, F6 by default, while a day is being played):
// <save root>/quicksave/<slot>.qsv (pc_quicksave_file.h) gets the day's
// start (the card block, the clock and the sim state that carries over from
// day to day outside the card, such as the throw preferences), every input
// since, the state hash at this tick and the view and window state. The
// game itself is untouched.
//
// Load (the Quick Load action, F7 by default: inside a day the running file's
// save; on the title screen or in the file select the newest story save; or
// --quick-load <file> at launch): the file is checked against the running
// game (CRC, build, region, sim settings, test switches, mode) before
// anything changes. The running day is left, the recorded day is entered
// through the normal day start (so the RNG reseeds), its input is replayed
// unthrottled with the audio muted, behind a load screen and at a lower
// render resolution, and at the saved tick the state hash is compared. On a
// match the view, the window state and the resolution come back and the next
// tick is live input; recording carries on, so the day can be saved again. On
// a mismatch the game stops with a message: the day is not the recorded one.
// Escape cancels a load: the day is left for the file select (VS: the
// title), as after "Continue from last save".
//
// A load re-enters a story day from its card block, as the file select loads
// a save. A day the game reached by continuing the previous one in memory,
// without loading the file, starts from more exact state than that block
// holds: the save's precision block (pc_quicksave_carry.h) keeps the buried
// Pikmin as memory held them and the last day's living Pikmin count, and a
// load puts them back.
//
// The state hash does not cover buried Pikmin until one is plucked; the
// day-start blocks are compared byte for byte when the recorded day starts
// again.
//
// Story mode and VS only; not with Permadeath, Speedrun, the Randomizer, the
// Challenge Mode or a netplay session. With the feature off nothing here does
// anything: no deterministic mode, no recording, the actions do nothing.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc_quicksave_exit {
constexpr int kRefused    = 6; // --quick-load: the file cannot be loaded into this game
constexpr int kLoadFailed = 7; // a load did not reproduce the recorded day
} // namespace pc_quicksave_exit

// Launch (pc_main, after the settings are loaded): reads the setting,
// PIKMIN_QUICKSAVE and --quick-load <file>. A change of the setting takes
// effect at the next launch.
void pc_quicksave_init(int argc, char** argv);
bool pc_quicksave_enabled(void);

// The bound actions (pc_window) and the F1 menu's rows. A save request acts
// at the end of the tick. The result (or why nothing happened) goes to the
// console, the menus' notice line and pc_quicksave_toast.
void pc_quicksave_request_save(void);
// In a day: the running file's save. On the title screen or in the file
// select: the newest story save, by the --quick-load route.
void pc_quicksave_request_load(void);
void pc_quicksave_request_load_file(const char* path);
// Escape during a load (pc_window).
void pc_quicksave_request_cancel(void);
bool pc_quicksave_load_active(void);

// The load screen (pc_settings_draw_quick_load).
struct PcQuickLoadStatus {
	bool replaying     = false; // the recorded day is being replayed (else: on the way into it)
	bool cancelling    = false;
	uint32_t fed       = 0;     // ticks replayed
	uint32_t total     = 0;     // ticks to replay
	double secs        = 0.0;   // since the load began
	double replaySecs  = 0.0;   // since the replay began
	int day            = 0;
};
bool pc_quicksave_load_status(PcQuickLoadStatus* out); // false when no load is under way
// The last result for a few seconds after it: false when there is none.
bool pc_quicksave_toast(char* out, size_t n, bool* isError, float* age);
// Why the save or load action does nothing now ("" when it works), for the
// F1 rows.
const char* pc_quicksave_save_unavailable(void);
const char* pc_quicksave_load_unavailable(void);

// Game flow hooks. Each one does nothing with the feature off.
void pc_quicksave_day_prepare(void);  // NewPikiGameSection: before the day is built
void pc_quicksave_day_begin(void);    // GameCoreSection: right after the RNG reseed
void pc_quicksave_note_running(void); // RunningModeState::update, every tick
// RunningModeState: a load wants the running day left. 0 no; 1 to the card
// select (a story day, or a cancelled story load); 2 straight into the day
// again (VS, as a rematch); 3 to the title (a cancelled VS load).
int pc_quicksave_take_leave(void);
// CardSelect: a load from the file select wants the title (1), whose route
// then takes it as --quick-load's.
int pc_quicksave_take_card_exit(void);
bool pc_quicksave_boot_pending(void); // logo: a --quick-load is waiting (no logo pause)
bool pc_quicksave_take_title(void);   // title: a story load starts its run
bool pc_quicksave_take_setup(void);   // CardSelect: skip the menus for a story load
int pc_quicksave_setup_slot(void);    // ...and the card file to commit (1-3, 0 none)
// CardSelect exit: puts the recorded day's start back (the card block, the
// clock, the stage). True when it did; the next section is the day.
bool pc_quicksave_take_enter(void);
void pc_quicksave_tick_end(void); // System::run, after the state hash

// Defined by the game (gameCoreSection.cpp): the cameras of the running day,
// written and read back as an opaque block.
bool pc_gamecore_view_state_save(std::vector<uint8_t>& out);
bool pc_gamecore_view_state_restore(const uint8_t* data, size_t len);
