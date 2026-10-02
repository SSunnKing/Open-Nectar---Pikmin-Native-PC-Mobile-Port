#include "pc_visual_runtime.h"
#include "pc_render_phase.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

int main()
{
	pc_visual_clear();
	check(!pc_visual_is_enabled(), "runtime must default off to avoid capture overhead before integration");
	pc_visual_set_enabled(true);
	check(pc_visual_is_enabled(), "runtime must support explicit activation");
	int actorA = 0;
	int actorB = 0;
	float firstA[32];
	float secondA[32];
	float secondB[32];
	float output[32] {};
	for (int i = 0; i < 32; ++i) {
		firstA[i] = static_cast<float>(i);
		secondA[i] = static_cast<float>(i + 20);
		secondB[i] = static_cast<float>(i + 100);
	}

	pc_visual_begin_authoritative_tick();
	pc_visual_capture_shape(&actorA, firstA, 2);
	pc_visual_begin_authoritative_tick();
	pc_visual_capture_shape(&actorB, secondB, 2);
	pc_visual_capture_shape(&actorA, secondA, 2); // Deliberately reversed actor order.
	pc_visual_prepare_presentation(0.25);
	pc_render_begin_presentation(0.25);

	check(pc_visual_resolve_shape(&actorA, output, 2), "known actor matrices must resolve");
	check(std::fabs(output[0] - 5.0f) < 1e-5f && std::fabs(output[31] - 36.0f) < 1e-5f,
	      "every matrix slot must interpolate for the same actor");
	check(pc_visual_resolve_shape(&actorB, output, 2) && std::fabs(output[0] - 100.0f) < 1e-5f,
	      "new actor must use current matrices without history");
	check(pc_visual_current_matrix_count() == 4 && pc_visual_previous_matrix_count() == 2,
	      "runtime must expose complete matrix generations");

	float authoritative[32];
	for (int i = 0; i < 32; ++i) authoritative[i] = static_cast<float>(i + 200);
	{
		PcVisualShapeOverride override(&actorA, authoritative, 2);
		check(override.active(), "complete presentation pose must activate scoped override");
		check(std::fabs(authoritative[0] - 5.0f) < 1e-5f && std::fabs(authoritative[31] - 36.0f) < 1e-5f,
		      "scoped override must expose interpolated matrices");
	}
	check(std::fabs(authoritative[0] - 200.0f) < 1e-5f && std::fabs(authoritative[31] - 231.0f) < 1e-5f,
	      "scoped override must restore every authoritative matrix");

	int unknownActor = 0;
	{
		PcVisualShapeOverride override(&unknownActor, authoritative, 2);
		check(!override.active(), "missing actor history must not expose a partial override");
	}
	check(std::fabs(authoritative[0] - 200.0f) < 1e-5f,
	      "failed override must leave authoritative matrices untouched");
	float incomplete[48];
	for (int i = 0; i < 48; ++i) incomplete[i] = static_cast<float>(i + 300);
	{
		PcVisualShapeOverride override(&actorA, incomplete, 3); // Actor A only captured two slots.
		check(!override.active(), "missing trailing slot must reject the complete pose transaction");
	}
	check(std::fabs(incomplete[0] - 300.0f) < 1e-5f && std::fabs(incomplete[47] - 347.0f) < 1e-5f,
	      "transactional resolve must not write leading slots before a later miss");

	pc_visual_clear();
	pc_visual_set_enabled(false);
	pc_render_phase_reset();
	std::printf("PcVisualRuntime: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
