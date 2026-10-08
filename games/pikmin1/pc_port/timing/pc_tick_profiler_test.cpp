#include "pc_tick_profiler.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool condition, const char* message) {
	if (!condition) {
		std::printf("FAIL: %s\n", message);
		failures++;
	}
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
	// Test 1: nothing recorded yet
	pc_tick_profiler_reset();
	check(pc_tick_profiler_stats(kPcTickWhole, 16.6).samples == 0, "empty profiler has no samples");
	check(pc_tick_profiler_report(16.6).find("no samples") != std::string::npos,
	      "empty report says so instead of printing zeros");

	// Test 2: the plain statistics of a known set
	pc_tick_profiler_reset();
	for (int i = 1; i <= 100; i++) {
		pc_tick_profiler_record(kPcTickUpdate, static_cast<double>(i));
	}
	PcTickStats s = pc_tick_profiler_stats(kPcTickUpdate, 1000.0);
	check(s.samples == 100, "100 samples retained");
	check(near(s.mean, 50.5), "mean of 1..100 is 50.5");
	check(near(s.worst, 100.0), "worst is the largest sample");
	check(near(s.median, 50.0), "nearest-rank median of 1..100 is 50");
	check(near(s.p95, 95.0), "nearest-rank p95 of 1..100 is 95");
	check(near(s.p99, 99.0), "nearest-rank p99 of 1..100 is 99");

	// Test 3: the budget verdict, which is the whole point of the tool
	s = pc_tick_profiler_stats(kPcTickUpdate, 90.0);
	check(near(s.overBudget, 0.10), "10 of 100 samples exceed a budget of 90");
	s = pc_tick_profiler_stats(kPcTickUpdate, 100.0);
	check(near(s.overBudget, 0.0), "a sample equal to the budget is not over it");

	// Test 4: regions are independent, so update cost never contaminates render cost
	pc_tick_profiler_reset();
	pc_tick_profiler_record(kPcTickUpdate, 5.0);
	pc_tick_profiler_record(kPcTickRenderAll, 11.0);
	check(near(pc_tick_profiler_stats(kPcTickUpdate, 16.6).mean, 5.0), "update keeps its own samples");
	check(near(pc_tick_profiler_stats(kPcTickRenderAll, 16.6).mean, 11.0), "renderall keeps its own samples");
	check(pc_tick_profiler_stats(kPcTickDoneRender, 16.6).samples == 0, "untouched region stays empty");

	// Test 5: a bounded window, reporting the recent past rather than an
	// average diluted by everything since boot
	pc_tick_profiler_reset();
	for (int i = 0; i < 5000; i++) {
		pc_tick_profiler_record(kPcTickWhole, 1.0);
	}
	check(pc_tick_profiler_stats(kPcTickWhole, 16.6).samples == 2048, "window is capped");
	for (int i = 0; i < 2048; i++) {
		pc_tick_profiler_record(kPcTickWhole, 40.0);
	}
	s = pc_tick_profiler_stats(kPcTickWhole, 16.6);
	check(near(s.mean, 40.0), "old samples are evicted, not averaged in forever");
	check(near(s.overBudget, 1.0), "a whole window over budget reports as such");

	// Test 6: a backwards clock must not poison the window
	pc_tick_profiler_reset();
	pc_tick_profiler_record(kPcTickWhole, 8.0);
	pc_tick_profiler_record(kPcTickWhole, -3.0);
	pc_tick_profiler_record(kPcTickWhole, std::nan(""));
	check(pc_tick_profiler_stats(kPcTickWhole, 16.6).samples == 1, "negative and NaN samples are dropped");

	// Test 7: out-of-range regions are ignored rather than corrupting memory
	pc_tick_profiler_record(static_cast<PcTickRegion>(-1), 5.0);
	pc_tick_profiler_record(kPcTickRegionCount, 5.0);
	check(pc_tick_profiler_stats(kPcTickWhole, 16.6).samples == 1, "bad region indices are ignored");

	// Test 8: every region is named, so a report never prints "?"
	for (int i = 0; i < kPcTickRegionCount; i++) {
		const char* name = pc_tick_region_name(static_cast<PcTickRegion>(i));
		check(name != nullptr && name[0] != '\0' && name[0] != '?', "region has a name");
	}

	// Test 9: the report carries the numbers a decision needs
	pc_tick_profiler_reset();
	for (int i = 0; i < 10; i++) {
		pc_tick_profiler_record(kPcTickWhole, 20.0);
		pc_tick_profiler_record(kPcTickUpdate, 12.0);
	}
	const std::string report = pc_tick_profiler_report(16.6);
	check(report.find("update") != std::string::npos, "report names the update region");
	check(report.find("tick") != std::string::npos, "report names the whole tick");
	check(report.find("100.0% of ticks exceeded") != std::string::npos,
	      "report states the share of ticks over budget");

	std::printf("PcTickProfiler: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
