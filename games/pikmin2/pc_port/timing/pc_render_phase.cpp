#include "pc_render_phase.h"

#include <algorithm>

namespace {
PcRenderPhase sPhase = PcRenderPhase::Authoritative;
std::uint64_t sTickSerial = 0;
double sInterpolationAlpha = 1.0;
}

void pc_render_begin_authoritative_tick()
{
	sPhase = PcRenderPhase::Authoritative;
	sInterpolationAlpha = 1.0;
	++sTickSerial;
}

void pc_render_begin_presentation(double interpolationAlpha)
{
	sPhase = PcRenderPhase::Presentation;
	sInterpolationAlpha = std::clamp(interpolationAlpha, 0.0, 1.0);
}

PcRenderPhase pc_render_phase()
{
	return sPhase;
}

bool pc_render_is_authoritative()
{
	return sPhase == PcRenderPhase::Authoritative;
}

std::uint64_t pc_render_tick_serial()
{
	return sTickSerial;
}

double pc_render_interpolation_alpha()
{
	return sInterpolationAlpha;
}

void pc_render_phase_reset()
{
	sPhase = PcRenderPhase::Authoritative;
	sTickSerial = 0;
	sInterpolationAlpha = 1.0;
}
