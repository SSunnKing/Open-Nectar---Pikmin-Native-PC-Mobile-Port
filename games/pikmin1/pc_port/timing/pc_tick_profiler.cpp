#include "pc_tick_profiler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// A tick at 30 Hz is 33 ms, so this window covers roughly the last minute of
// play. Bounded on purpose: the interesting number is what the machine is
// doing now, in this area, not an average diluted by the title screen.
constexpr size_t kWindow = 2048;

struct RegionSamples {
	double values[kWindow] = {};
	size_t count           = 0; // samples held, saturating at kWindow
	size_t next            = 0; // write cursor
};

RegionSamples& samples_for(PcTickRegion region)
{
	static RegionSamples storage[kPcTickRegionCount];
	return storage[region];
}

// Nearest-rank: the smallest retained sample that at least `p` of the samples
// fall at or below. No interpolation, so a reported figure is always a tick
// that actually happened.
double percentile(const std::vector<double>& sorted, double p)
{
	if (sorted.empty()) {
		return 0.0;
	}
	size_t rank = static_cast<size_t>(p * static_cast<double>(sorted.size()) + 0.999999);
	if (rank == 0) {
		rank = 1;
	}
	if (rank > sorted.size()) {
		rank = sorted.size();
	}
	return sorted[rank - 1];
}

} // namespace

const char* pc_tick_region_name(PcTickRegion region)
{
	switch (region) {
	case kPcTickUpdate:     return "update";
	case kPcTickRenderAll:  return "renderall";
	case kPcTickDoneRender: return "doneRender";
	case kPcTickWhole:      return "tick";
	case kPcTickWorldSim:   return "  world sim";
	case kPcTickGfxUniforms:     return "  gl:uniforms";
	case kPcTickGfxVbo:          return "  gl:vbo";
	case kPcTickGfxDraw:         return "  gl:draw";
	case kPcTickGfxDisplayList:  return "  gl:dl parse+xform";
	case kPcTickGfxStateKey:     return "    gl:statekey";
	case kPcTickGfxProgram:      return "    gl:program";
	case kPcTickGfxTexBind:      return "    gl:texbind";
	case kPcTickGfxMeshDraws:    return "  gl:mesh draws/frame";
	case kPcTickGfxMeshVerts:    return "  gl:mesh verts/frame";
	case kPcTickGfxShaderBuild:  return "  gl:shader build";
	case kPcTickGfxMeshBuilds:   return "  gl:mesh builds/frame";
	case kPcTickGfxDrawCount:    return "  gl:draws/frame";
	case kPcTickGfxVertsPerDraw: return "  gl:verts/draw";
	case kPcTickGfxPrimCount:    return "  gl:prims/frame";
	case kPcTickGfxRunLength:    return "  gl:prims/batch";
	case kPcTickGfxRunLongest:   return "  gl:longest run";
	case kPcTickGfxGlBreakPct:   return "  gl:%breaks=glstate";
	case kPcTickGxDlDesync:      return "  gx:dl DESYNC/frame";
	case kPcTickGxBadMtxIdx:     return "  gx:bad mtxidx/frame";
	case kPcTickGxWildVerts:     return "  gx:wild verts/frame";
	case kPcTickGpuScene:        return "gpu:scene";
	case kPcTickGpuBlit:         return "gpu:blit";
	default:                return "?";
	}
}

bool pc_tick_profiler_enabled()
{
	// Read once. An earlier probe in this port called getenv about 1600 times
	// per frame and became the cost it was measuring.
	static const bool enabled = [] {
		const char* value = getenv("PIKMIN_TICK_STATS");
		return (value != nullptr && value[0] == '1') || pc_tick_profiler_hud_enabled();
	}();
	return enabled;
}

bool pc_tick_profiler_hud_enabled()
{
	static const bool enabled = [] {
		const char* value = getenv("PIKMIN_PERF_HUD");
		return value != nullptr && value[0] == '1';
	}();
	return enabled;
}

void pc_tick_profiler_record(PcTickRegion region, double milliseconds)
{
	if (region < 0 || region >= kPcTickRegionCount) {
		return;
	}
	// A clock that jumps backwards would otherwise poison the window.
	if (!(milliseconds >= 0.0)) {
		return;
	}

	RegionSamples& s = samples_for(region);
	s.values[s.next] = milliseconds;
	s.next           = (s.next + 1) % kWindow;
	if (s.count < kWindow) {
		s.count++;
	}
}

void pc_tick_profiler_reset()
{
	for (int i = 0; i < kPcTickRegionCount; i++) {
		samples_for(static_cast<PcTickRegion>(i)) = RegionSamples();
	}
}

PcTickStats pc_tick_profiler_stats(PcTickRegion region, double budgetMs, size_t lastN)
{
	PcTickStats stats;
	if (region < 0 || region >= kPcTickRegionCount) {
		return stats;
	}

	const RegionSamples& s = samples_for(region);
	if (s.count == 0) {
		return stats;
	}

	std::vector<double> sorted;
	if (lastN == 0 || lastN >= s.count) {
		sorted.assign(s.values, s.values + s.count);
	} else {
		// The ring's write cursor is one past the newest sample; walk back.
		sorted.reserve(lastN);
		for (size_t i = 0; i < lastN; i++) {
			sorted.push_back(s.values[(s.next + kWindow - 1 - i) % kWindow]);
		}
	}
	double sum   = 0.0;
	size_t over  = 0;
	for (double value : sorted) {
		sum += value;
		if (value > budgetMs) {
			over++;
		}
	}
	std::sort(sorted.begin(), sorted.end());

	stats.samples    = static_cast<uint32_t>(sorted.size());
	stats.mean       = sum / static_cast<double>(sorted.size());
	stats.median     = percentile(sorted, 0.50);
	stats.p95        = percentile(sorted, 0.95);
	stats.p99        = percentile(sorted, 0.99);
	stats.worst      = sorted.back();
	stats.overBudget = static_cast<double>(over) / static_cast<double>(sorted.size());
	return stats;
}

std::string pc_tick_profiler_report(double budgetMs)
{
	const PcTickStats whole = pc_tick_profiler_stats(kPcTickWhole, budgetMs);
	if (whole.samples == 0) {
		return "[PC tick] no samples yet\n";
	}

	std::string out;
	char line[256];

	snprintf(line, sizeof(line),
	         "[PC tick] last %u ticks, budget %.1f ms (ms, except the gl: rows marked as counts)\n",
	         whole.samples, budgetMs);
	out += line;

	for (int i = 0; i < kPcTickRegionCount; i++) {
		const PcTickRegion region = static_cast<PcTickRegion>(i);
		const PcTickStats s       = pc_tick_profiler_stats(region, budgetMs);
		snprintf(line, sizeof(line),
		         "           %-16s mean %8.2f  p50 %8.2f  p95 %8.2f  p99 %8.2f  worst %8.2f\n",
		         pc_tick_region_name(region), s.mean, s.median, s.p95, s.p99, s.worst);
		out += line;
	}

	// The verdict, stated plainly: the fraction of ticks that would already
	// have missed the deadline at the target rate.
	snprintf(line, sizeof(line), "           %.1f%% of ticks exceeded %.1f ms\n",
	         whole.overBudget * 100.0, budgetMs);
	out += line;
	return out;
}
