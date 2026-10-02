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
	pc_render_phase_reset();
	check(pc_render_is_authoritative(), "reset must start in authoritative phase");
	check(pc_render_tick_serial() == 0, "reset must clear tick serial");
	check(std::fabs(pc_render_interpolation_alpha() - 1.0) < 1e-12,
	      "authoritative phase must expose current state alpha");

	pc_render_begin_authoritative_tick();
	check(pc_render_tick_serial() == 1, "authoritative tick must increment serial exactly once");
	pc_render_begin_presentation(0.25);
	check(pc_render_phase() == PcRenderPhase::Presentation, "presentation phase must be observable");
	check(pc_render_tick_serial() == 1, "presentation must not advance logical tick serial");
	check(std::fabs(pc_render_interpolation_alpha() - 0.25) < 1e-12,
	      "presentation must preserve interpolation alpha");

	pc_render_begin_presentation(-2.0);
	check(pc_render_interpolation_alpha() == 0.0, "presentation alpha must clamp at zero");
	pc_render_begin_presentation(4.0);
	check(pc_render_interpolation_alpha() == 1.0, "presentation alpha must clamp at one");

	pc_render_begin_authoritative_tick();
	check(pc_render_is_authoritative() && pc_render_tick_serial() == 2,
	      "next authoritative tick must restore phase and increment serial");

	std::printf("PcRenderPhase: %s\n", failures ? "FAILED" : "all tests passed");
	return failures ? 1 : 0;
}
