#pragma once
// Scripted gameplay scenarios (--scenario <name>), compiled only when
// PIKMIN_BUILD_SCENARIOS is ON (PIKI_SCENARIOS=1). A scenario boots straight
// into a stage, feeds pad input through an in-process provider, reads game
// objects directly and exits with a verdict.
//
// Exit codes: 0 pass, 1 assertion failed (one stderr line), 2 unknown scenario
// or bad arguments (before any window exists), 4 window or GL context creation
// failed, 77 skip because no game data (PIKMIN_SCENARIO_DATA_DIR). After a
// --scenario-handoff, closing the window exits 0.

#if PIKI_SCENARIOS

#include "netplay/pc_input_log.h"

namespace zen {
struct DrawGameInfo;
}

enum {
	PC_SCENARIO_EXIT_PASS  = 0,
	PC_SCENARIO_EXIT_FAIL  = 1,
	PC_SCENARIO_EXIT_USAGE = 2,
	PC_SCENARIO_EXIT_NO_GL = 4,
	PC_SCENARIO_EXIT_SKIP  = 77,
};

// One scenario. Ticks are counted from the first tick after the stage is ready
// (tick 0), at the rate chosen with --scenario-tick-rate.
class PcScenario {
public:
	virtual ~PcScenario() { }
	virtual const char* name() const = 0;
	// Day the stage starts on (the day is consumed by GameCoreSection's constructor).
	virtual int startDay() const { return 1; }
	virtual bool supportsRate(int /*ticksPerSecond*/) const { return true; }
	// True for runner self-tests (exit_zero): selectable by name, left out of the usage list.
	virtual bool testOnly() const { return false; }
	// True to start in challenge mode: pc_scenario_pre_stage then takes CardSelect's
	// challenge path (gameflow.mIsChallengeMode, PlayerState::setChallengeMode) instead of
	// forcing story mode (coverage/terminals.md T17, T18).
	virtual bool challengeMode() const { return false; }
	// Hard mode: pending flag is set before pc_hardmode_begin_new_run (coverage T22, EC-56).
	virtual bool hardMode() const { return false; }
	// Speedrun mode: pc_speedrun_set_active(true) before the stage (coverage T23).
	virtual bool speedrunMode() const { return false; }
	// True to own the container and boot bits of all three Onions before the stage loads, so
	// each Onion is born booted (GOAL_Wait, route open) instead of in BootInit with its route
	// closed (GoalItem::startAI). Challenge mode sets every Onion too, with its other flags.
	virtual bool ownsAllOnions() const { return false; }
	// Runs once when the stage is ready, before tick 0.
	virtual void onStageReady() { }
	// Fills pad 0 for this tick; pads 1-3 arrive neutral. Called every tick, before the sim.
	virtual void providePads(unsigned /*tick*/, PcInputPad& /*pad0*/) { }
	// Runs after each tick's sim. Report a verdict with scenarioPass() / scenarioFail().
	virtual void onTick(unsigned tick) = 0;
	// Local co-op: two captains in split screen. Pad 1 is connected and drives
	// the second captain (providePad1). The runner arms co-op before the stage
	// is built and draws the split screen's two views (see pc_scenario.cpp).
	virtual bool coop() const { return false; }
	// Fills pad 1 (the second captain's) in a co-op scenario, after providePads.
	virtual void providePad1(unsigned /*tick*/, PcInputPad& /*pad1*/) { }
	// True for a scenario that calls scenarioHandOff(); only those accept --scenario-handoff.
	virtual bool handsOff() const { return false; }

	// Scenarios that cross a day end into the next stage (onion_day3). With this
	// true, a soft reset seen after onTick without a verdict does not end the run:
	// the runner stops ticking the scenario, waits for the next stage, runs the
	// intro gate again and calls onStageReady() once more (pc_scenario_stage_number
	// says which). Ticks keep counting across stages. Off by default, so every
	// other scenario keeps its single-stage behaviour.
	virtual bool crossesDays() const { return false; }
	// Called when OnePlayerSection builds MapSelect for a crossesDays() scenario.
	// Return the stages.ini `file` of the course to enter: the runner then does what
	// MapSelectSetupSection::enterCourse and its exit do and goes to NewPikiGame
	// without building the world map (fast path when a scenario returns a course).
	// Return nullptr to build MapSelectSection and DrawWorldMap (stand-in or disc);
	// drive it with providePadsBetweenStages and/or pc_scenario_request_map_course.
	// providePadsBetweenStages.
	virtual const char* onMapSelect() { return nullptr; }
	// Pad 0 between a soft reset and the next stage ready (the real world map).
	virtual void providePadsBetweenStages(unsigned /*ticksSinceReset*/, PcInputPad& /*pad0*/) { }
	// Pad 0 while title or file select runs before the first stage (title_boot).
	virtual void providePadsDuringBoot(unsigned /*bootTicks*/, PcInputPad& /*pad0*/) { }
	// Whether the intro gate pulses START over the opening movie of stage `stageNumber`
	// (1 = the first). False lets the movie run to its end; A still closes text.
	virtual bool introSkipsMovies(int /*stageNumber*/) const { return true; }
	// Whether the runner skips the title and file-select sections on the first boot
	// (ninLogo.cpp, gameSetup.cpp). False for title_boot; after the first skip, or
	// after a soft reset to title or card select, the real screens run.
	virtual bool skipBootMenus() const { return true; }
	// Whether the runner's GameStat invariant check runs after this tick. A scenario
	// turns it off only where the original game's own counting breaks it (say why).
	virtual bool checksGameStat() const { return true; }
	// When non-null, the runner loads this stages.ini entry unless PIKMIN_SCENARIO_STAGE is set.
	virtual const char* stageFile() const { return nullptr; }
	// Above 0: the field Pikmin limit for this run (pc_settings_scenario_set_piki_limit,
	// before the stage build sizes PikiMgr, PikiHeadMgr and CPlate). 0 keeps the default 100.
	virtual int pikiLimit() const { return 0; }
};

// Returns -1 to continue booting (no --scenario), else the process exit code.
int pc_scenario_parse_argv(int argc, char** argv);
bool pc_scenario_active(void);
// True during normal scenario sim ticks (after intro, before a verdict).
bool pc_scenario_screenshot_frame_active(void);
unsigned pc_scenario_sim_tick(void);
bool pc_scenario_uses_disc_free_assets(void);
// While >0, colour-only creature materials draw unlit (Pikmin tint on material 0 only).
void pc_scenario_push_creature_unlit_draw(void);
void pc_scenario_pop_creature_unlit_draw(void);
bool pc_scenario_creature_unlit_draw(void);
struct PcScenarioDebugCamera {
	bool active = false;
	float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
	float focusX = 0.0f, focusY = 0.0f, focusZ = 0.0f;
	float fov = 30.0f;
};
void pc_scenario_set_debug_camera(const PcScenarioDebugCamera& cam);
void pc_scenario_clear_debug_camera(void);
class Camera;
bool pc_scenario_apply_debug_camera(Camera* cam);
// Exit code for pc_main to return once the game loop ends.
int pc_scenario_exit_code(void);
// Called by pc_main when the window or GL context failed: records exit 4 as the
// verdict so the exit handler (which main's return also runs) reports it.
void pc_scenario_on_no_gl(void);

// The memory-card thread (issue #125). Screens such as ogScrMakeDefaultMgr poll
// MemoryCard::hasCardFinished() once per tick, so on the game's own path the tick a
// card operation is seen to end follows the card thread's wall-clock speed. While a
// scenario runs (until a hand-off), CardUtilCommand waits for the card thread before
// it returns, so every poll sees the operation done and the outcome depends only on
// the tick count. --scenario-card-async turns that wait off (the negative control).
bool pc_scenario_card_sync(void);
// Called by the card thread before it runs each command: sleeps for
// --scenario-card-delay-ms (none by default), a slow card for the determinism check.
void pc_scenario_card_thread_delay(void);

// Hooks in the boot path.
void pc_scenario_on_course_init(void); // PlayerState::initCourse, after initGame has reset the flags, before the stage generators run
void pc_scenario_pre_stage(void);      // OnePlayerSection, before NewPikiGameSection is built
void pc_scenario_on_stage_ready(void); // after GameCoreSection::finalSetup
void pc_scenario_on_tick_begin(void);  // System::run, before idle on each scenario sim tick
void pc_scenario_on_tick_end(void);    // System::run, after every tick including boot
// OnePlayerSection, case ONEPLAYER_MapSelect: true when a crossesDays() scenario
// chose the next course itself (gameflow.mNextOnePlayerSectionID is then NewPikiGame).
bool pc_scenario_on_map_select(void);
// After onMapSelect returns nullptr the world map runs; a scenario may request a
// course from MapSelect once (providePadsBetweenStages, ~245 ticks at 30 Hz).
void pc_scenario_request_map_course(const char* stageFile);
const char* pc_scenario_consume_map_course(void);
// True while the first stage is still booting and the scenario wants title/card skipped.
bool pc_scenario_skip_boot_menus(void);

#if PIKI_SCENARIOS
void pc_scenario_register_draw_game_info(zen::DrawGameInfo* hud);
void pc_scenario_unregister_draw_game_info(void);
bool pc_scenario_check_hud_layout(const char** why);
#endif

// For scenarios.
int pc_scenario_stage_number(void); // 1 for the first stage, 2 after the first soft reset, ...
// PIKMIN_SCENARIO_STAGE at run start, or the runner default (stages/stage1.ini).
const char* pc_scenario_requested_stage_file(void);

// For scenarios.
int pc_scenario_ticks_per_second(void); // 30 or 60
// Ticks the intro gate waited after the stage was ready (cutscene and text), before tick 0.
unsigned pc_scenario_intro_ticks(void);
void scenarioPass(void);
void scenarioFail(unsigned tick, const char* expected, const char* observed);
// printf-style: records failure with expected/observed text.
void scenarioFailf(unsigned tick, const char* expected, const char* fmt, ...);
// The scenario's hand-off point (issue #76). In a --scenario-handoff run the
// scripted pad is detached, deterministic and unthrottled mode end, the
// watchdog and every check stop, and the real keyboard, mouse and controllers
// drive the game until the window is closed (exit 0). The scratch save
// directory and the settings layer stay. In any other run it is a pass: the
// scripted part reached the state it promises.
void scenarioHandOff(void);
// True once a --scenario-handoff run has handed the game to the player.
bool pc_scenario_handed_off(void);
// Scenario tick index during the sim (after intro, before on_tick_end increments).
unsigned pc_scenario_sim_tick(void);
// The disc's parms/key.bin bindings (throw A, cursor B, pluck A, attack A,
// disband X, menu Y). The stand-in tree has no key.bin, and KeyConfig's code
// defaults put pluck on B. Read from the disc 2026-10-28 (scenario_onion_unlock.cpp).
void scenarioApplyDiscKeyBindings(void);

// Scenario preconditions (issue #66), run against the game's own objects.
// Both fail through scenarioFailf and return false on a violated precondition.
// For every info in the shared Pikmin AnimMgr: the animation exists, has
// mTotalFrameCount > 0 and is not the "Null Anim" dummy. The runner calls this
// once when the stage is ready, before the scenario's onStageReady.
bool scenarioCheckAnimMgrPreconditions(unsigned tick);
// While the captain is in ThrowWait: the upper animator's motion is
// PIKIANIM_ThrowWait and info 63 has a type-2 info key whose anim-key frame is
// below the animator's frame count.
class Navi;
bool scenarioCheckThrowWaitPreconditions(unsigned tick, Navi* navi);

// Read access to NewPikiGame's file-static windows (defined in newPikiGame.cpp, scenario
// builds only). Null when the game has no such window at the moment.
namespace zen {
struct DrawGameOver;
struct DrawFinalResult;
struct DrawCMresult;
} // namespace zen
zen::DrawGameOver* pc_scenario_gameover_window(void);
zen::DrawFinalResult* pc_scenario_total_window(void);
zen::DrawCMresult* pc_scenario_challenge_window(void);
// Text windows createTutorialWindow has opened since the process started; with a
// non-null lastTextId, the EnumTutorial of the last one (-1 before the first).
int pc_scenario_text_windows_opened(int* lastTextId);
// The live GameCoreSection (null outside a running NewPikiGame) and the countdown
// window RunningModeState draws from mNightCountdown.
struct GameCoreSection;
namespace zen {
struct DrawCountDown;
} // namespace zen
GameCoreSection* pc_scenario_gamecore(void);
zen::DrawCountDown* pc_scenario_countdown_window(void);
// The map and controller menu (Y, MOVIECMD_CreateMenuWindow): null while it is closed.
namespace zen {
struct ogScrMenuMgr;
} // namespace zen
zen::ogScrMenuMgr* pc_scenario_menu_window(void);

#endif
