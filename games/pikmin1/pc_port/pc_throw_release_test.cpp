/**
 * @file pc_throw_release_test.cpp
 * @brief Throw grab timing without a captain, a controller, or a window.
 *
 * Navi::update advances the animation and delivers KEY_Action0 before
 * Navi::doAI runs the state. Motion speed 30 times the tick length is one
 * animation frame at 30 Hz, half a frame at 60 Hz, and a quarter at 120 Hz
 * (fpsMode 0, 1, 2). The entry tick starts the motion and does not advance
 * it; the first advance is the next tick.
 *
 * The disc's ThrowWait and Throw keyframe indexes are not in the repo, so
 * the counts below are for chosen indexes. They show how an early release
 * lines up with the keyframe. A Dolphin count needs those indexes.
 */

#include "pc_throw_release.h"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++gFailures;
	}
}

/// The ThrowWait condition before it called pc_throw_wait_lost_piki.
bool referenceLost(bool alive, bool grabReached, bool stateNormal, bool stateHanged, bool stateGoHang)
{
	const bool awaitingGrab = !grabReached && stateNormal;
	return !alive || (!awaitingGrab && !stateHanged && !stateGoHang);
}

void checkDecisions()
{
	check(!pc_throw_release_starts_throw(true, false), "an early release does not throw");
	check(!pc_throw_release_starts_throw(false, true), "holding A after the keyframe keeps charging");
	check(pc_throw_release_starts_throw(true, true), "a release after the keyframe throws");
	check(!pc_throw_release_starts_throw(false, false), "a held button before the keyframe waits");

	const bool bits[] = { false, true };
	for (bool alive : bits) {
		for (bool grab : bits) {
			for (bool normal : bits) {
				for (bool hanged : bits) {
					for (bool goHang : bits) {
						const bool got = pc_throw_wait_lost_piki(alive, grab, normal, hanged, goHang);
						const bool want = referenceLost(alive, grab, normal, hanged, goHang);
						check(got == want, "lost-piki decision matches ThrowWait");
					}
				}
			}
		}
	}
	check(!pc_throw_wait_lost_piki(true, false, true, false, false), "a normal Pikmin awaits the grab");
	check(pc_throw_wait_lost_piki(false, false, true, false, false), "a dead Pikmin ends the grab");
	check(!pc_throw_wait_lost_piki(true, false, false, false, true), "a Pikmin walking over stays");
	check(!pc_throw_wait_lost_piki(true, true, false, true, false), "a hanged Pikmin stays");
	check(pc_throw_wait_lost_piki(true, false, false, false, false), "any other state ends the grab");

	bool queued = false;
	check(!pc_throw_consume_queued_press(false, true, &queued) && queued, "a click during the wind-up is stored");
	check(!pc_throw_consume_queued_press(false, false, &queued) && queued, "the stored click waits for the release");
	check(pc_throw_consume_queued_press(true, false, &queued) && !queued, "the release tick starts the next grab");

	queued = false;
	check(pc_throw_consume_queued_press(true, true, &queued) && !queued, "a click on the release tick starts the next grab");
	check(!pc_throw_consume_queued_press(false, false, &queued) && !queued, "no click leaves the queue empty");
	check(!pc_throw_consume_queued_press(true, false, &queued) && !queued, "the release tick with an empty queue waits");
}

enum Phase { PhaseWalk, PhaseWait, PhaseThrow };

enum Policy { PolicyWait, PolicyComplete, PolicyGameCube, PolicyWaitDrop };

enum Writer { WriterTap, WriterAlternate, WriterHold };

struct Sim {
	Phase phase;
	Policy policy;
	Writer writer;
	double step;
	double grabFrame;
	double throwFrame;
	double throwEnd;
	bool motionLive;
	double counter;
	bool holding;
	bool keyframeLatched;
	bool hasThrown;
	bool queued;
	bool prevDown;
	bool stateNormal;
	bool stateHanged;
	bool stateGoHang;
	int throws;
	int firstThrowTick;
	bool threwBeforeKeyframe;
	bool chainedOnReleaseTick;
};

Sim makeSim(Policy policy, Writer writer, double step, double grabFrame, double throwFrame)
{
	Sim s{};
	s.phase            = PhaseWalk;
	s.policy           = policy;
	s.writer           = writer;
	s.step             = step;
	s.grabFrame        = grabFrame;
	s.throwFrame       = throwFrame;
	s.throwEnd         = throwFrame + 20.0;
	s.firstThrowTick   = -1;
	s.stateNormal      = true;
	return s;
}

bool desiredDown(const Sim& s, int tick)
{
	if (s.writer == WriterTap) {
		return tick == 0;
	}
	if (s.writer == WriterAlternate) {
		return (tick % 2) == 0;
	}
	// Hold through the grab. Once the keyframe has latched, let go so the
	// throw starts, and press again during the throw wind-up.
	if (s.phase == PhaseWait && s.holding) {
		return false;
	}
	if (s.phase == PhaseThrow) {
		return !s.hasThrown;
	}
	return true;
}

void enterWait(Sim& s)
{
	s.phase           = PhaseWait;
	s.counter         = 0.0;
	s.motionLive      = false;
	s.holding         = false;
	s.keyframeLatched = false;
	s.stateNormal     = true;
	s.stateHanged     = false;
	s.stateGoHang     = false;
}

void enterThrow(Sim& s, int tick)
{
	if (s.throws == 0) {
		s.firstThrowTick = tick;
	}
	if (!s.keyframeLatched) {
		s.threwBeforeKeyframe = true;
	}
	++s.throws;
	s.phase      = PhaseThrow;
	s.counter    = 0.0;
	s.motionLive = false;
	s.hasThrown  = false;
	s.queued     = false;
}

void tick(Sim& s, int tickIndex)
{
	const bool down  = desiredDown(s, tickIndex);
	const bool click = down && !s.prevDown;
	const bool keyUp = !down;

	bool skipThrowExec = false;
	if (s.motionLive && (s.phase == PhaseWait || s.phase == PhaseThrow)) {
		s.counter += s.step;
		if (s.phase == PhaseWait && !s.keyframeLatched && s.counter >= s.grabFrame) {
			s.keyframeLatched = true;
			s.holding         = true;
			s.stateNormal     = false;
			s.stateHanged     = true;
			s.stateGoHang     = false;
		}
		if (s.phase == PhaseThrow && !s.hasThrown && s.counter >= s.throwFrame) {
			s.hasThrown = true;
		}
		if (s.phase == PhaseThrow && s.counter >= s.throwEnd) {
			s.phase      = PhaseWalk;
			s.motionLive = false;
			skipThrowExec = true;
		}
	}

	if (s.phase == PhaseWalk) {
		if (click) {
			enterWait(s);
		}
	} else if (s.phase == PhaseWait) {
		const bool lost = (s.policy == PolicyGameCube)
		                      ? (!s.stateHanged && !s.stateGoHang)
		                      : pc_throw_wait_lost_piki(true, s.holding, s.stateNormal, s.stateHanged, s.stateGoHang);
		if (lost) {
			s.phase      = PhaseWalk;
			s.motionLive = false;
		} else {
			if (s.policy == PolicyComplete && keyUp && !s.holding && s.stateNormal) {
				s.holding     = true;
				s.stateNormal = false;
				s.stateHanged = true;
			}
			if (s.policy == PolicyGameCube) {
				if (keyUp) {
					enterThrow(s, tickIndex);
				}
			} else if (pc_throw_release_starts_throw(keyUp, s.holding)) {
				enterThrow(s, tickIndex);
			}
		}
	} else if (s.phase == PhaseThrow && !skipThrowExec) {
		// The queued press is the PC path. The non-PC branch, and PolicyWaitDrop,
		// accept a click only once the Pikmin has already left the hand.
		if (s.policy == PolicyWait || s.policy == PolicyComplete) {
			if (pc_throw_consume_queued_press(s.hasThrown, click, &s.queued)) {
				if (s.hasThrown) {
					s.chainedOnReleaseTick = true;
				}
				enterWait(s);
			}
		} else if (s.hasThrown && click) {
			enterWait(s);
		}
	}

	if (s.phase == PhaseWait || s.phase == PhaseThrow) {
		s.motionLive = true;
	}
	s.prevDown = down;
}

Sim run(Policy policy, Writer writer, double step, double grabFrame, double throwFrame, int ticks)
{
	Sim s = makeSim(policy, writer, step, grabFrame, throwFrame);
	for (int i = 0; i < ticks; ++i) {
		tick(s, i);
	}
	return s;
}

int ticksToReach(double frame, double step)
{
	return static_cast<int>(std::ceil(frame / step));
}

void checkTap(double grabFrame, double step, const char* label)
{
	const int horizon = ticksToReach(grabFrame, step) + 5;
	const Sim waited = run(PolicyWait, WriterTap, step, grabFrame, grabFrame + 4.0, horizon);
	const Sim completed = run(PolicyComplete, WriterTap, step, grabFrame, grabFrame + 4.0, horizon);
	const Sim cube = run(PolicyGameCube, WriterTap, step, grabFrame, grabFrame + 4.0, horizon);
	const int keyTick = ticksToReach(grabFrame, step);

	check(!waited.threwBeforeKeyframe, label);
	check(waited.firstThrowTick == keyTick, label);
	check(waited.throws >= 1, label);
	check(completed.threwBeforeKeyframe, label);
	check(completed.firstThrowTick == 1, label);
	check(cube.throws == 0, label);
}

void checkHoldAgrees(double grabFrame, double step)
{
	const int horizon = ticksToReach(grabFrame, step) + 5;
	const Sim waited = run(PolicyWait, WriterHold, step, grabFrame, 10.0, horizon);
	const Sim completed = run(PolicyComplete, WriterHold, step, grabFrame, 10.0, horizon);
	check(waited.firstThrowTick == completed.firstThrowTick, "holding through the grab throws on the same tick");
	check(!waited.threwBeforeKeyframe && !completed.threwBeforeKeyframe, "holding through the grab reaches the keyframe");
	check(waited.firstThrowTick == ticksToReach(grabFrame, step) + 1, "the release is the tick after the keyframe latches");
}

void checkRates()
{
	const double frames[] = { 4.0, 5.0, 8.0, 12.0 };
	const double steps[] = { 1.0, 0.5, 0.25 };
	const char* names[] = { "30 Hz", "60 Hz", "120 Hz" };
	for (double frame : frames) {
		double seconds[3];
		for (int i = 0; i < 3; ++i) {
			checkTap(frame, steps[i], names[i]);
			const Sim waited = run(PolicyWait, WriterTap, steps[i], frame, frame + 4.0, ticksToReach(frame, steps[i]) + 2);
			const double frameTime = steps[i] / 30.0;
			seconds[i] = waited.firstThrowTick * frameTime;
		}
		check(std::fabs(seconds[0] - seconds[1]) < 1e-9, "30 Hz and 60 Hz throw on the same real time");
		check(std::fabs(seconds[0] - seconds[2]) < 1e-9, "30 Hz and 120 Hz throw on the same real time");
		checkHoldAgrees(frame, 1.0);
	}
}

struct Count {
	int throws;
	bool before;
	bool chained;
};

Count countMash(Policy policy, Writer writer, double step, int ticks, double grabFrame, double throwFrame)
{
	const Sim s = run(policy, writer, step, grabFrame, throwFrame, ticks);
	return { s.throws, s.threwBeforeKeyframe, s.chainedOnReleaseTick };
}

void checkMashes()
{
	const double grabFrame  = 8.0;
	const double throwFrame = 10.0;
	const struct {
		const char* name;
		double step;
		int ticks;
	} modes[] = {
		{ "fpsMode 0", 1.0, 300 },
		{ "fpsMode 1", 0.5, 600 },
		{ "fpsMode 2", 0.25, 1200 },
	};

	std::printf("throws in 10 s, grab frame %.0f, throw release frame %.0f (indexes are not from the disc)\n",
	            grabFrame, throwFrame);
	std::printf("  %-12s %8s %8s %8s %8s\n", "mode", "wait", "on-release", "no-queue", "non-PC");
	for (const auto& mode : modes) {
		const Count wait = countMash(PolicyWait, WriterAlternate, mode.step, mode.ticks, grabFrame, throwFrame);
		const Count early = countMash(PolicyComplete, WriterAlternate, mode.step, mode.ticks, grabFrame, throwFrame);
		const Count dropped = countMash(PolicyWaitDrop, WriterAlternate, mode.step, mode.ticks, grabFrame, throwFrame);
		const Count cube = countMash(PolicyGameCube, WriterAlternate, mode.step, mode.ticks, grabFrame, throwFrame);
		std::printf("  %-12s %8d %8d %8d %8d\n", mode.name, wait.throws, early.throws, dropped.throws, cube.throws);
		check(!wait.before, "waiting never throws before the grab keyframe");
		check(early.before, "completing the grab on release throws before the keyframe");
		check(early.throws > wait.throws, "skipping the grab keyframe throws more often");
		check(cube.throws == 0, "the non-PC branch cancels a tap that is still Normal");
		check(wait.throws > 0, "waiting still throws on a mash");
		check(wait.chained, "the queued press starts the next grab on the release tick");
		check(wait.throws > dropped.throws, "remembering the wind-up click throws more often than dropping it");
	}

	const Count holdQueue = countMash(PolicyWait, WriterHold, 1.0, 300, grabFrame, throwFrame);
	const Count holdDrop = countMash(PolicyWaitDrop, WriterHold, 1.0, 300, grabFrame, throwFrame);
	std::printf("hold through the grab at 30 Hz for 10 s: queued %d, click dropped %d\n", holdQueue.throws,
	            holdDrop.throws);
	check(!holdQueue.before && !holdDrop.before, "holding through the grab reaches the keyframe");
	check(holdQueue.throws > holdDrop.throws, "a held mash still throws sooner when the wind-up click is kept");
}

} // namespace

int main()
{
	checkDecisions();
	checkRates();
	checkMashes();
	if (gFailures != 0) {
		std::printf("%d failure%s\n", gFailures, gFailures == 1 ? "" : "s");
		return 1;
	}
	std::printf("pc_throw_release_test ok\n");
	return 0;
}
