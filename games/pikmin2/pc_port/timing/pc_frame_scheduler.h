#pragma once

#include <cstdint>

struct PcFrameSchedule {
	int logicalTicks;
	double fixedDelta;
	double interpolationAlpha;
	double nextDeadline;
	std::uint64_t discardedTicks;
};

// Platform-independent fixed-step scheduler. Times are monotonic seconds from
// an injected clock; the class deliberately has no SDL or rendering dependency.
class PcFrameScheduler {
public:
	explicit PcFrameScheduler(int maxCatchUpTicks = 4, double suspendThreshold = 0.5);

	void reset(double now, int frameClamp);
	PcFrameSchedule advance(double now, int frameClamp);

	std::uint64_t totalTicks() const { return mTotalTicks; }
	std::uint64_t discardedTicks() const { return mDiscardedTicks; }
	double fixedDelta() const { return mFixedDelta; }
	double nextDeadline() const { return mNextDeadline; }

private:
	static double deltaForClamp(int frameClamp);

	bool mInitialised;
	int mFrameClamp;
	int mMaxCatchUpTicks;
	double mSuspendThreshold;
	double mFixedDelta;
	double mLastTime;
	double mAccumulator;
	double mNextDeadline;
	std::uint64_t mTotalTicks;
	std::uint64_t mDiscardedTicks;
};
