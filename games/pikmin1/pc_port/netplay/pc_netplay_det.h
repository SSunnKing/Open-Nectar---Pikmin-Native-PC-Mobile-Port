#pragma once

// Deterministic fixed-step mode for netplay.
//
// Opt-in: unless --netplay-deterministic is passed or
// PIKMIN_NETPLAY_DETERMINISTIC=1 is set, every function below is inert and
// the game behaves exactly as before.
//
// In deterministic mode mDeltaTime is the fixed tick period, both RNG
// streams are reseeded per day (pc_sim_rng.h) and the FP environment is
// pinned at the start of every tick.

void pc_netplay_det_init(int argc, char** argv); // call once from main(), before the game starts
bool pc_netplay_deterministic(void);            // true if --netplay-deterministic or PIKMIN_NETPLAY_DETERMINISTIC=1
bool pc_netplay_unthrottled(void);              // det mode only: PIKMIN_NETPLAY_UNTHROTTLED=1 (fast replays)
unsigned pc_netplay_tick(void);                 // logical ticks executed so far (app->idle calls), in any mode
void pc_netplay_on_tick_begin(void);            // called by System::run right before app->idle()

// Turns deterministic mode on at runtime (for a netplay session).
void pc_netplay_det_force_on(void);

// Quick save (pc_quicksave.h): turns deterministic mode on at launch, before
// the first tick, for a run whose days are recorded.
void pc_netplay_det_enable_for_recording(void);
// Quick save load: while on, deterministic mode runs unthrottled (one tick
// per loop, no frame pacing) as PIKMIN_NETPLAY_UNTHROTTLED=1 does, so a
// recorded day catches up as fast as it can. Off again at the handoff.
void pc_netplay_det_set_catchup(bool on);
bool pc_netplay_det_catchup(void);
// Quick save load: re-enters a recorded day at the logical tick it began on,
// so every value the sim derives from the tick count (the VS clock, the swarm
// timer) is the recorded one. The next pc_netplay_on_tick_begin counts on
// from here.
void pc_netplay_det_set_tick(unsigned tick);

// Logical tick period in seconds for a setFrameClamp value: 1/30 at clamp 2,
// 1/60 at clamp 1, 1/120 at clamp 0 (mirrors PcFrameScheduler::deltaForClamp).
float pc_netplay_fixed_dt(int frameClamp);

// Reseeds both RNG streams for a new GameCoreSection (day). No-op unless
// deterministic mode is on. The seed is FNV-1a over PIKMIN_NETPLAY_SEED (u32,
// default 0), the day index and the stage id.
void pc_netplay_det_reseed_for_new_day(int dayIndex, int stageId);
// The session seed that reseed reads (PIKMIN_NETPLAY_SEED, default 0).
unsigned pc_netplay_det_session_seed(void);

// PIKMIN_NETPLAY_PROFILE_LOG value, or nullptr when unset/empty. Cached.
const char* pc_netplay_det_profile_path(void);
// World-sim / whole-tick cost report every 600 ticks when
// PIKMIN_NETPLAY_PROFILE_LOG is set (defined in system.cpp). No-op otherwise.
void pc_netplay_det_profile_note_tick(void);
