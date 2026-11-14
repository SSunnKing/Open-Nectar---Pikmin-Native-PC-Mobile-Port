/**
 * @file pc_throw_arc_test.cpp
 * @brief Throw peak, flight time and landing at 30, 60 and 120 Hz.
 *
 * The disc throw sets one launch speed and then integrates it twice per tick:
 * a zeroed volatile pass (gravity on a zero velocity, so a drop of g*dt*dt)
 * and the real velocity, after subtracting g*dt. That is Creature::update's
 * two moveNew calls. Above 30 Hz the flight waits out each 1/30 s and takes
 * that same step, so 60 and 120 Hz follow the 30 Hz samples.
 */

#include "pc_throw_arc.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++gFailures;
	}
}

struct Body {
	float x    = 0.0f;
	float y    = 0.0f;
	float vx   = 0.0f;
	float vy   = 0.0f;
	float peak = 0.0f;
};

/// One tick of Creature::update's two movement passes, in the air.
void integrate(Body& b, float dt, float gravity)
{
	b.y += (-gravity * dt) * dt;
	b.vy -= gravity * dt;
	b.y += b.vy * dt;
	b.x += b.vx * dt;
	if (b.y > b.peak) {
		b.peak = b.y;
	}
}

struct Flight {
	float peakRise = 0.0f;
	float time     = 0.0f;
	float landX    = 0.0f;
	float landY    = 0.0f;
	int steps      = 0;
	bool landed    = false;
	std::vector<float> sampleX;
	std::vector<float> sampleY;
};

float launchVertical(float gravity, float height, float flightTime)
{
	const float half = 0.5f * flightTime;
	return gravity * 0.5f * half + (height / half);
}

float launchHorizontal(float distance, float flightTime)
{
	const float half = 0.5f * flightTime;
	return distance / (2.0f * half);
}

Body launched(float gravity, float height, float flightTime, float distance, float y0)
{
	Body b;
	b.y    = y0;
	b.peak = y0;
	b.vy   = launchVertical(gravity, height, flightTime);
	b.vx   = launchHorizontal(distance, flightTime);
	return b;
}

/// Integrate on every tick with that tick's dt. This is the arc before the fix.
Flight flyRaw(float frameDt, float gravity, float height, float flightTime, float distance, float y0)
{
	Body b = launched(gravity, height, flightTime, distance, y0);
	Flight out;
	float t = 0.0f;
	for (int i = 0; i < 20000; ++i) {
		if (i > 0) {
			t += frameDt;
		}
		integrate(b, frameDt, gravity);
		out.sampleX.push_back(b.x);
		out.sampleY.push_back(b.y);
		if (b.y <= 0.0f) {
			out.peakRise = b.peak - y0;
			out.time     = t;
			out.landX    = b.x;
			out.landY    = b.y;
			out.steps    = i + 1;
			out.landed   = true;
			return out;
		}
	}
	return out;
}

/// 30 Hz passes the frame dt through. 60 and 120 Hz step by 1/30 s.
Flight flyClock(float frameDt, int ticksPer30Hz, float gravity, float height, float flightTime, float distance, float y0)
{
	Body b = launched(gravity, height, flightTime, distance, y0);
	PcThrowArcClock clock;
	pc_throw_arc_prime(&clock);
	Flight out;
	float t = 0.0f;
	for (int i = 0; i < 40000; ++i) {
		if (i > 0) {
			t += frameDt;
		}
		const PcThrowArcAction action = pc_throw_arc_peek(clock, frameDt, ticksPer30Hz);
		pc_throw_arc_commit(&clock, frameDt, ticksPer30Hz);
		if (action == PcThrowArcAction::Hold) {
			continue;
		}
		const float step = action == PcThrowArcAction::DiscStep ? kPcThrowArcDiscDt : frameDt;
		integrate(b, step, gravity);
		out.sampleX.push_back(b.x);
		out.sampleY.push_back(b.y);
		++out.steps;
		if (b.y <= 0.0f) {
			out.peakRise = b.peak - y0;
			out.time     = t;
			out.landX    = b.x;
			out.landY    = b.y;
			out.landed   = true;
			return out;
		}
	}
	return out;
}

bool near(float a, float b, float tol)
{
	return std::fabs(a - b) <= tol;
}

void checkSchedule(float frameDt, int ticksPer30Hz, int stride, const char* name)
{
	PcThrowArcClock clock;
	pc_throw_arc_prime(&clock);
	int discSteps = 0;
	for (int i = 0; i < stride * 40; ++i) {
		const PcThrowArcAction action = pc_throw_arc_peek(clock, frameDt, ticksPer30Hz);
		pc_throw_arc_commit(&clock, frameDt, ticksPer30Hz);
		const bool wantStep = (i % stride) == 0;
		if (wantStep) {
			if (action != PcThrowArcAction::DiscStep) {
				std::printf("FAIL: %s frame %d should take the 30 Hz step\n", name, i);
				++gFailures;
				return;
			}
			++discSteps;
		} else if (action != PcThrowArcAction::Hold) {
			std::printf("FAIL: %s frame %d should hold\n", name, i);
			++gFailures;
			return;
		}
	}
	check(discSteps == 40, name);
}

void checkThrow(const char* name, float height, float flightTime, float distance)
{
	const float gravity = 550.0f;
	const float y0      = 10.0f;
	const Flight hz30   = flyRaw(1.0f / 30.0f, gravity, height, flightTime, distance, y0);
	const Flight raw60  = flyRaw(1.0f / 60.0f, gravity, height, flightTime, distance, y0);
	const Flight raw120 = flyRaw(1.0f / 120.0f, gravity, height, flightTime, distance, y0);
	const Flight fix30  = flyClock(1.0f / 30.0f, 1, gravity, height, flightTime, distance, y0);
	const Flight fix60  = flyClock(1.0f / 60.0f, 2, gravity, height, flightTime, distance, y0);
	const Flight fix120 = flyClock(1.0f / 120.0f, 4, gravity, height, flightTime, distance, y0);

	std::printf("%s  height %.1f  flight %.2f  distance %.1f\n", name, height, flightTime, distance);
	std::printf("  before  30 %+7.3f  t %.5f  x %8.3f\n", hz30.peakRise, hz30.time, hz30.landX);
	std::printf("  before  60 %+7.3f  t %.5f  x %8.3f\n", raw60.peakRise, raw60.time, raw60.landX);
	std::printf("  before 120 %+7.3f  t %.5f  x %8.3f\n", raw120.peakRise, raw120.time, raw120.landX);
	std::printf("  after   30 %+7.3f  t %.5f  x %8.3f\n", fix30.peakRise, fix30.time, fix30.landX);
	std::printf("  after   60 %+7.3f  t %.5f  x %8.3f\n", fix60.peakRise, fix60.time, fix60.landX);
	std::printf("  after  120 %+7.3f  t %.5f  x %8.3f\n", fix120.peakRise, fix120.time, fix120.landX);

	check(hz30.landed && raw60.landed && raw120.landed && fix30.landed && fix60.landed && fix120.landed, "throw lands");
	// The uncorrected 60 Hz step is what the game did: the same launch, every tick.
	check(raw60.peakRise > hz30.peakRise + 4.0f, "uncorrected 60 Hz peaks higher than 30 Hz");

	const float kPos = 0.001f;
	const float kTime = 0.0002f;
	check(near(fix30.peakRise, hz30.peakRise, kPos), "30 Hz peak stays the disc arc");
	check(near(fix30.time, hz30.time, kTime), "30 Hz flight time stays the disc arc");
	check(near(fix30.landX, hz30.landX, kPos), "30 Hz landing stays the disc arc");
	check(fix30.sampleY.size() == hz30.sampleY.size(), "30 Hz takes one step per tick");
	if (fix30.sampleY.size() == hz30.sampleY.size()) {
		for (size_t i = 0; i < hz30.sampleY.size(); ++i) {
			if (!near(fix30.sampleY[i], hz30.sampleY[i], kPos) || !near(fix30.sampleX[i], hz30.sampleX[i], kPos)) {
				std::printf("FAIL: 30 Hz sample %zu moved\n", i);
				++gFailures;
				break;
			}
		}
	}

	for (const Flight* fix : { &fix60, &fix120 }) {
		check(near(fix->peakRise, hz30.peakRise, kPos), "high rate peak matches 30 Hz");
		check(near(fix->time, hz30.time, kTime), "high rate flight time matches 30 Hz");
		check(near(fix->landX, hz30.landX, kPos), "high rate landing matches 30 Hz");
		check(near(fix->landY, hz30.landY, kPos), "high rate landing height matches 30 Hz");
		check(fix->sampleY.size() == hz30.sampleY.size(), "high rate takes the same 30 Hz steps");
		if (fix->sampleY.size() != hz30.sampleY.size()) {
			continue;
		}
		for (size_t i = 0; i < hz30.sampleY.size(); ++i) {
			if (!near(fix->sampleY[i], hz30.sampleY[i], kPos) || !near(fix->sampleX[i], hz30.sampleX[i], kPos)) {
				std::printf("FAIL: sample %zu differs (y %.4f vs %.4f)\n", i, fix->sampleY[i], hz30.sampleY[i]);
				++gFailures;
				break;
			}
		}
	}
}

void checkThirtyHzClockIsIdle()
{
	PcThrowArcClock clock;
	pc_throw_arc_prime(&clock);
	const PcThrowArcPhase primed = clock.phase;
	for (int i = 0; i < 8; ++i) {
		check(pc_throw_arc_peek(clock, 1.0f / 30.0f, 1) == PcThrowArcAction::Passthrough, "30 Hz does not replace the tick");
		pc_throw_arc_commit(&clock, 1.0f / 30.0f, 1);
	}
	check(clock.phase == primed && clock.bank == 0.0f, "30 Hz does not arm or bank a step");
}

} // namespace

int main()
{
	checkThirtyHzClockIsIdle();
	checkSchedule(1.0f / 60.0f, 2, 2, "60 Hz steps every second tick");
	checkSchedule(1.0f / 120.0f, 4, 4, "120 Hz steps every fourth tick");

	// Disc navi parms: both throw heights are 75, yellow is 115, flight time
	// and the level throw distance are 1 s and 140.
	checkThrow("red", 75.0f, 1.0f, 140.0f);
	checkThrow("yellow", 115.0f, 1.0f, 140.0f);
	checkThrow("short", 75.0f, 1.0f, 40.0f);

	if (gFailures == 0) {
		std::printf("pc_throw_arc_test ok\n");
		return 0;
	}
	std::printf("pc_throw_arc_test: %d failure(s)\n", gFailures);
	return 1;
}
