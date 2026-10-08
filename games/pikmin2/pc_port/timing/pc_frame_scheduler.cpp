#include "pc_frame_scheduler.h"

#include <algorithm>
#include <cmath>

PcFrameScheduler::PcFrameScheduler(int maxCatchUpTicks, double suspendThreshold)
    : mInitialised(false)
    , mFrameClamp(1)
    , mMaxCatchUpTicks(std::max(1, maxCatchUpTicks))
    , mSuspendThreshold(std::max(0.0, suspendThreshold))
    , mFixedDelta(1.0 / 60.0)
    , mLastTime(0.0)
    , mAccumulator(0.0)
    , mNextDeadline(0.0)
    , mTotalTicks(0)
    , mDiscardedTicks(0)
{
}

double PcFrameScheduler::deltaForClamp(int frameClamp)
{
	// Special case: frameClamp 0 means 120 Hz (0.5 frame periods at 60 Hz base)
	if (frameClamp == 0) return 1.0 / 120.0;
	if (frameClamp == 1) return 1.0 / 60.0;
	if (frameClamp == 2) return 2.0 / 60.0;  // 30 Hz
	return frameClamp / 60.0;
}

void PcFrameScheduler::reset(double now, int frameClamp)
{
	mInitialised   = true;
	mFrameClamp    = frameClamp;
	mFixedDelta    = deltaForClamp(mFrameClamp);
	mLastTime      = now;
	mAccumulator   = 0.0;
	mNextDeadline  = now + mFixedDelta;
}

PcFrameSchedule PcFrameScheduler::advance(double now, int frameClamp)
{
	if (!mInitialised || frameClamp != mFrameClamp || now < mLastTime) {
		reset(now, frameClamp);
		return { 0, mFixedDelta, 0.0, mNextDeadline, mDiscardedTicks };
	}

	double realDelta = now - mLastTime;
	mLastTime        = now;
	if (realDelta >= mSuspendThreshold) {
		// Focus loss, debugging and loading pauses do not become simulation debt.
		mAccumulator  = 0.0;
		mNextDeadline = now + mFixedDelta;
		return { 0, mFixedDelta, 0.0, mNextDeadline, mDiscardedTicks };
	}

	mAccumulator += realDelta;
	const int available = static_cast<int>(std::floor((mAccumulator + 1e-12) / mFixedDelta));
	const int ticks     = std::min(available, mMaxCatchUpTicks);
	if (available > ticks) {
		mDiscardedTicks += static_cast<std::uint64_t>(available - ticks);
		mAccumulator -= available * mFixedDelta;
	} else {
		mAccumulator -= ticks * mFixedDelta;
	}
	if (mAccumulator < 0.0) mAccumulator = 0.0;

	mTotalTicks += static_cast<std::uint64_t>(ticks);
	mNextDeadline = now + (mFixedDelta - mAccumulator);
	const double alpha = std::min(1.0, mAccumulator / mFixedDelta);
	return { ticks, mFixedDelta, alpha, mNextDeadline, mDiscardedTicks };
}
