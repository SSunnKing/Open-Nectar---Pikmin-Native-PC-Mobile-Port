// Quick load progress. See pc_quicksave_progress.h.

#include "quicksave/pc_quicksave_progress.h"

#include <cmath>
#include <cstdio>

namespace pc_quicksave {

LoadProgress load_progress(uint64_t fed, uint64_t total, double secs)
{
	LoadProgress p;
	if (total == 0 || fed >= total) {
		p.percent        = 100;
		p.remainingKnown = true;
		return p;
	}
	p.percent           = (int)((fed * 100) / total);
	const uint64_t enough = total / 50 > 60 ? total / 50 : 60;
	if (fed >= enough && fed > 0 && secs >= 0.5) {
		p.remaining      = secs * (double)(total - fed) / (double)fed;
		p.remainingKnown = true;
	}
	return p;
}

void format_clock(double secs, char* out, size_t n)
{
	if (out == nullptr || n == 0) return;
	long s = secs > 0.0 ? (long)std::ceil(secs) : 0;
	if (s >= 3600) {
		std::snprintf(out, n, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
	} else {
		std::snprintf(out, n, "%ld:%02ld", s / 60, s % 60);
	}
}

// What a load of a day-`saved` quick save replaces, on a file now on day
// `fileDay` (-1: not known here). False when it cannot be this file's.
bool describe_load(int saved, int fileDay, bool inDay, char* out, size_t n)
{
	if (fileDay >= 0 && saved > fileDay) {
		std::snprintf(out, n, "Quick Load refused: the quick save is from day %d, later than this file (day %d).", saved, fileDay);
		return false;
	}
	if (fileDay < 0) {
		std::snprintf(out, n,
		              "Load the quick save from day %d? If its file was saved past day %d, that later progress is replaced "
		              "when day %d ends. Press A again to load.",
		              saved, saved, saved);
	} else if (saved == fileDay) {
		std::snprintf(out, n, "Load the quick save from earlier in day %d? Play since then is lost. Press A again to load.", saved);
	} else if (saved + 1 == fileDay) {
		std::snprintf(out, n,
		              "Load the quick save from day %d? This file is on day %d. When day %d ends, its save replaces your saved "
		              "day %d%s. Press A again to load.",
		              saved, fileDay, saved, saved, inDay ? ", and today's play is lost" : "");
	} else {
		std::snprintf(out, n,
		              "Load the quick save from day %d? This file is on day %d. When day %d ends, its save replaces your saved "
		              "days %d to %d%s. Press A again to load.",
		              saved, fileDay, saved, saved, fileDay - 1, inDay ? ", and today's play is lost" : "");
	}
	return true;
}

} // namespace pc_quicksave
