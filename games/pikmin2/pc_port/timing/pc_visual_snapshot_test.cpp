#include "pc_visual_snapshot.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

static PcVisualMatrix filled(float value)
{
	PcVisualMatrix matrix {};
	matrix.fill(value);
	return matrix;
}

static bool near(float actual, float expected)
{
	return std::fabs(actual - expected) < 1e-5f;
}

int main()
{
	PcVisualSnapshotStore store;
	int shapeA = 0;
	int shapeB = 0;
	const PcVisualKey a0 { &shapeA, 0, PcVisualDomain::ShapeMatrix };
	const PcVisualKey a1 { &shapeA, 1, PcVisualDomain::ShapeMatrix };
	const PcVisualKey b0 { &shapeB, 0, PcVisualDomain::ShapeMatrix };
	PcVisualMatrix result {};

	store.beginTick();
	check(store.capture(a0, filled(10.0f)), "first stable sample must be accepted");
	store.preparePresentation(0.25);
	check(store.resolve(a0, result) && near(result[0], 10.0f), "first sample must present without interpolation");

	store.beginTick();
	check(store.capture(a1, filled(40.0f)), "second slot must be accepted");
	check(store.capture(a0, filled(20.0f)), "existing slot must be accepted in any capture order");
	check(store.capture(b0, filled(80.0f)), "second owner must be accepted");
	store.preparePresentation(0.25);
	check(store.resolve(a0, result) && near(result[7], 12.5f), "stable owner+slot must interpolate independent of order");
	check(store.resolve(a1, result) && near(result[0], 40.0f), "new slot must use current state");
	check(store.resolve(b0, result) && near(result[0], 80.0f), "new owner must use current state");

	// Removing an object from the current state must also remove it from the
	// presentation set; stale actors may never survive through history alone.
	store.beginTick();
	store.capture(a0, filled(30.0f));
	store.preparePresentation(0.5);
	check(!store.resolve(b0, result), "removed owner must not resolve from previous state");
	check(!store.resolve(a1, result), "removed slot must not resolve from previous state");

	store.beginTick();
	store.capture(a0, filled(1000.0f), true);
	store.preparePresentation(0.1);
	check(store.resolve(a0, result) && near(result[3], 1000.0f), "discontinuity must suppress interpolation");

	store.beginTick();
	store.capture(a0, filled(2000.0f));
	store.preparePresentation(0.1);
	check(store.resolve(a0, result) && near(result[3], 1100.0f),
	      "tick after a discontinuity must interpolate from the accepted current state");
	store.synchronize();
	store.preparePresentation(0.1);
	check(store.resolve(a0, result) && near(result[3], 2000.0f), "synchronize must collapse visual history");

	store.beginTick();
	store.capture(a0, filled(3000.0f));
	store.preparePresentation(-2.0);
	check(store.resolve(a0, result) && near(result[0], 2000.0f), "negative alpha must clamp to previous state");
	store.preparePresentation(4.0);
	check(store.resolve(a0, result) && near(result[0], 3000.0f), "alpha above one must clamp to current state");

	PcVisualMatrix invalid = filled(0.0f);
	invalid[5] = std::numeric_limits<float>::quiet_NaN();
	check(!store.capture({ nullptr, 0, PcVisualDomain::Camera }, filled(1.0f)), "null owner must be rejected");
	check(!store.capture(a0, invalid), "non-finite matrix must be rejected");

	store.clear();
	check(store.currentSize() == 0 && store.previousSize() == 0 && store.presentationSize() == 0,
	      "clear must remove every generation");

	std::printf("PcVisualSnapshotStore: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
