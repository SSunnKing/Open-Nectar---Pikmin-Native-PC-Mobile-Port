#ifndef PC_TICK_PROFILER_H
#define PC_TICK_PROFILER_H

// CPU cost of one logical tick, split into the parts that would have to run
// twice as often for 60 Hz gameplay.
//
// The GPU side is already measured (PIKMIN_PERF_STATS) and is comfortable at
// 1080p. The open question for 60 FPS gameplay is the CPU: a tick has to fit
// in 16.6 ms, and nothing so far says whether it does. This answers that.
//
// It reports the worst case as prominently as the mean. A mean of 9 ms with a
// p99 of 24 ms is not a 60 Hz tick; it is a 60 Hz tick that stutters, which is
// worse to play than an honest 30.
//
// Off unless PIKMIN_TICK_STATS=1. The accumulator holds no clock of its own so
// it can be tested without one: call sites time the span and hand over the
// milliseconds.

#include <cstddef>
#include <cstdint>
#include <string>

enum PcTickRegion {
	kPcTickUpdate = 0,     // game logic: AI, physics, navigation
	kPcTickRenderAll,      // building the frame's GX display lists
	kPcTickDoneRender,     // submitting them and presenting
	kPcTickWhole,          // the three above end to end, per tick

	// The world simulation (Node::update + GameCore::updateAI) runs from
	// GameCoreSection::draw, i.e. inside renderall, so kPcTickUpdate is near
	// zero in play. This is the game logic proper: what update would be if
	// the original had split them. Counted inside renderall, not on top.
	kPcTickWorldSim,

	// Inside renderall, where the cost turned out to be. Per frame, summed
	// over every GX primitive: the port emits one draw per primitive, each
	// preceded by ~150 uniform writes.
	kPcTickGfxUniforms,    // ms/frame writing uniforms
	kPcTickGfxVbo,         // ms/frame uploading vertices
	kPcTickGfxDraw,        // ms/frame in glDrawArrays itself
	kPcTickGfxDisplayList, // ms/frame parsing + transforming display lists on the CPU (excludes the three above)
	// Inside gl:uniforms, which parts of a state change cost what.
	kPcTickGfxStateKey,    // ms/frame hashing the state key (every primitive, batched or not)
	kPcTickGfxProgram,     // ms/frame choosing/binding the program for the state
	kPcTickGfxTexBind,     // ms/frame binding textures
	kPcTickGfxMeshDraws,   // resident-mesh draws per frame (count): display lists served from the GPU arena
	kPcTickGfxMeshVerts,   // vertices per frame drawn from the arena (count)
	kPcTickGfxShaderBuild, // ms/frame building programs (compile+link, or binary load)
	kPcTickGfxMeshBuilds,  // resident meshes built (first parse + upload) per frame (count)
	kPcTickGfxDrawCount,   // draws per frame — a count, not milliseconds
	kPcTickGfxVertsPerDraw,// mean vertices per draw — a count, not milliseconds

	// PERF-NATIVE-002 step 1: how much consecutive work actually shares state.
	// Batching only pays if draws arrive in runs; these say how long the runs
	// are and what breaks them. Counts, not milliseconds.
	kPcTickGfxPrimCount,   // GX primitives per frame, i.e. draws before batching
	kPcTickGfxRunLength,   // mean primitives per batch (draws per run of identical state)
	kPcTickGfxRunLongest,  // longest such run in the frame
	kPcTickGfxGlBreakPct,  // % of run breaks caused by GL pipeline state, not material

	// Display-list faults per frame, split by severity because they are not
	// the same thing. A desync (unsupported opcode, truncated vertex stream)
	// abandons the rest of the list and can draw garbage; an out-of-range
	// PNMTXIDX is a per-vertex warning the parser recovers from, and a model
	// carrying one logs thousands per frame while rendering correctly.
	kPcTickGxDlDesync,     // fatal: parser lost the stream
	kPcTickGxBadMtxIdx,    // benign: PNMTXIDX out of range, per vertex

	// Vertices whose position is not a finite, plausible coordinate. Reading
	// from a stale or freed vertex array yields exactly this, and it draws as
	// triangles stretching off screen -- 3D wrecked, 2D overlays untouched.
	kPcTickGxWildVerts,

	// GPU time per frame from asynchronous timer queries (EXT_disjoint_timer_
	// query on GLES, ARB on desktop). Recorded a few frames late, when the
	// result becomes available, so the window is offset from the CPU rows by
	// the depth of the query ring; p50/p99 are unaffected. Zero samples means
	// the driver has no timer queries.
	kPcTickGpuScene,       // ms/frame the GPU spent on the 3D scene + UI
	kPcTickGpuBlit,        // ms/frame in post-process and the blit to screen

	kPcTickRegionCount,
};

const char* pc_tick_region_name(PcTickRegion region);

// Reads PIKMIN_TICK_STATS once (PIKMIN_PERF_HUD=1 implies it: the on-screen
// HUD reads the same samples). Call sites test this before timing anything so
// the profiler costs nothing when it is off.
bool pc_tick_profiler_enabled();

// PIKMIN_PERF_HUD=1: draw CPU/GPU milliseconds in a corner of the screen, for
// devices where nobody is watching the console.
bool pc_tick_profiler_hud_enabled();

void pc_tick_profiler_record(PcTickRegion region, double milliseconds);
void pc_tick_profiler_reset();

struct PcTickStats {
	uint32_t samples = 0;
	double mean      = 0.0;
	double median    = 0.0;
	double p95       = 0.0;
	double p99       = 0.0;
	double worst     = 0.0;
	// Share of samples that did not fit the budget handed to the query, 0..1.
	double overBudget = 0.0;
};

// budgetMs is what a tick is allowed to cost: 16.6 for 60 Hz, 33.3 for 30 Hz.
// lastN limits the statistics to the most recent samples (0 = the whole
// window): the HUD wants "now", the report wants the last minute.
PcTickStats pc_tick_profiler_stats(PcTickRegion region, double budgetMs, size_t lastN = 0);

// One block of text for the console, including the verdict against budgetMs.
std::string pc_tick_profiler_report(double budgetMs);

#endif // PC_TICK_PROFILER_H
