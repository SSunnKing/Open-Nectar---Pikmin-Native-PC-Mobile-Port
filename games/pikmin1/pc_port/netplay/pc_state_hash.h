#pragma once
// Curated per-tick simulation hash for the netplay determinism harness.
//
// Log format (PIKMIN_STATE_HASH_LOG=<file>): after every tick, one text line:
//   <tick> <total> <navi> <piki> <teki> <item> <world> <rng> <mode>
// <tick> is the 1-based tick count in decimal; every other column is a 64-bit
// hash printed as 16 lowercase hex digits.
//
// What is hashed (stable simulation data only, never pointers):
//   navi:  per Navi in manager order: mNaviID; position, rotation, velocity
//     and drive (mTargetVelocity) as float bits; mFaceDirection; mHealth;
//     current state id (-1 when null).
//   piki:  per Piki in manager order: mColor, mHappa; position, rotation,
//     velocity, drive; mFaceDirection; mHealth; current state id.
//   teki:  per Teki in manager order: mTekiType; position, rotation,
//     velocity, drive; mFaceDirection; mHealth; mStateID.
//   item:  per itemMgr object: mObjType; position, rotation, velocity,
//     drive; mFaceDirection; for Onions (OBJTYPE_Goal, static_cast) also
//     mOnionColour and mHeldPikis[3]; then the Onion counts per colour via
//     itemMgr->getContainer(c); then per Pellet: config id and type,
//     position, rotation, velocity, drive, mFaceDirection, state ids and
//     carrier count; then per Boss: current/next state, life, position,
//     rotation, velocity, drive, mFaceDirection.
//   world: gameflow.mWorldClock time of day and day, plus the playerState
//     living/born/dead/plucked counters (0 when null).
//   rng:   pc_sim_rng_state().
//   mode:  a mode-specific hash from the source registered with
//     pc_state_hash_set_mode_fn (VS state, for instance); 0 when none.
//   total: FNV-1a 64 over the seven sub-hashes in the order above.
// A null manager contributes 0. naviMgr == nullptr means no live stage:
// exitStage nulls naviMgr while the other managers may still point at the
// released stage, so navi/piki/teki/item are then 0 instead of walking
// stale objects.
//
// Runtime behaviour:
//   PIKMIN_STATE_HASH_LOG=<file> (or --state-hash-log <file>): write one
//     line per tick, flushed every 300 ticks and on exit.
//   PIKMIN_NETPLAY_EXIT_AFTER_TICKS=<n> (or --netplay-exit-after-ticks <n>):
//     after tick n is hashed, flush every log and push SDL_QUIT so the main
//     loop exits normally. If it has not exited 600 ticks later, exit(0).
//   PIKMIN_NETPLAY_TEST_PREROLL_RAND=<n>: before the first tick, call rand()
//     n times (and pc_sim_rand() n times in deterministic mode).
//   With none of these set, pc_state_hash_tick_end() returns immediately:
//     no managers are walked, no files opened, nothing printed.

#include <cstdint>

// argv must be captured before the first tick; env vars are read lazily.
void pc_state_hash_notify_argv(int argc, char** argv);

// Called once by pc_input_log_tick(), before the first tick's simulation.
void pc_state_hash_before_first_tick(void);

// Per-tick hook: call after app->idle() returns.
void pc_state_hash_tick_end(void);

void pc_state_hash_flush(void);

// Current 1-based tick count (0 before the first hashed tick).
uint64_t pc_state_hash_tick(void);

// Lockstep capture: when on, tick_end computes and remembers the sub-hashes
// every tick even with no log file. pc_state_hash_current() returns the last
// total, the seven sub-hashes (navi, piki, teki, item, world, rng, mode) and
// the tick that produced them; false when nothing has been computed yet.
void pc_state_hash_set_netplay_capture(bool on);
bool pc_state_hash_current(uint64_t* total, uint64_t subs[7], uint64_t* tick);

// Quick save: the total hash of the sim as it is now (the same value the log
// writes for a tick's end), without counting a tick or writing the log.
uint64_t pc_state_hash_compute_total(void);
// Quick save load: sets the tick counter, so the log and the exit-after switch
// count a re-entered day's ticks with the recorded day's numbers.
void pc_state_hash_set_tick(uint64_t tick);

// Source for the mode column. Null (the default) hashes 0.
typedef uint64_t (*PcStateHashModeFn)(void);
void pc_state_hash_set_mode_fn(PcStateHashModeFn fn);

// Prints one "[netplay-navi] tick=<tick> navi=<id> pos=(x y z)" line per
// captain (test evidence for netplay pair runs). Nothing without a stage.
void pc_state_hash_print_navi_positions(uint64_t tick);
