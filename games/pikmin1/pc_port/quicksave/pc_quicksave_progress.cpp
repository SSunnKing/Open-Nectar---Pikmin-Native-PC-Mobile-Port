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

} // namespace pc_quicksave
