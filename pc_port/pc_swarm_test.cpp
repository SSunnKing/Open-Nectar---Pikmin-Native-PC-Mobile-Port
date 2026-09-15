#include "pc_swarm.h"
#include <cmath>
#include <cstdio>

static bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

int main()
{
	float x = 0.0f;
	float z = 0.0f;
	if (!pc_swarm_cursor_direction(0.0f, 100.0f, 0.0f, &x, &z) || !near(x, 0.0f) || !near(z, 1.0f)) return 1;
	if (!pc_swarm_cursor_direction(100.0f, 0.0f, 0.0f, &x, &z) || !near(x, 1.0f) || !near(z, 0.0f)) return 1;
	if (!pc_swarm_cursor_direction(100.0f, 0.0f, 1.57079632679f, &x, &z) || !near(x, 0.0f) || !near(z, 1.0f)) return 1;
	if (pc_swarm_cursor_direction(0.0f, 0.0f, 0.0f, &x, &z)) return 1;
	return 0;
}
