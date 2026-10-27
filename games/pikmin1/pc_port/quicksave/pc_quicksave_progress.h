#pragma once
// Quick load progress for the load screen (pc_quicksave.h): how far the
// replay of the recorded day is, and an estimate of the time left. Pure; the
// host test covers it (pc_quicksave_helpers_test).

#include <cstddef>
#include <cstdint>

namespace pc_quicksave {

struct LoadProgress {
	int percent         = 0;     // 0-100, rounded down
	double remaining    = 0.0;   // seconds, when known
	bool remainingKnown = false; // false until enough of the day has replayed to tell
};

// After `secs` seconds, `fed` of `total` ticks have been replayed. The time
// left is the rate so far applied to the ticks left, once at least 2% of the
// day (and 60 ticks) and half a second are behind it.
LoadProgress load_progress(uint64_t fed, uint64_t total, double secs);

// "m:ss", or "h:mm:ss" from an hour; seconds rounded up, so a wait that is
// not over never shows 0:00. Negative times show as 0:00.
void format_clock(double secs, char* out, size_t n);

} // namespace pc_quicksave
