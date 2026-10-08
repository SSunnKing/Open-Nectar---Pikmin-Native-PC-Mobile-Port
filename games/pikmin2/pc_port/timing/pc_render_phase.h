#pragma once

#include <cstdint>

enum class PcRenderPhase {
	Authoritative,
	Presentation
};

#ifdef __cplusplus
extern "C" {
#endif

/// Returns true if the current render pass is evaluating the canonical snapshot.
bool pc_render_is_authoritative();

/// Prepares the thread-local presentation context with the given interpolation alpha.
void pc_render_begin_presentation(double alpha);

/// Prepares the thread-local context for authoritative evaluation.
void pc_render_begin_authoritative_tick();

/// Retrieves the current interpolation alpha.
double pc_render_get_alpha();

/// Retrieves the current interpolation alpha (alias for compatibility).
double pc_render_interpolation_alpha();

/// Returns the current render phase.
PcRenderPhase pc_render_phase();

/// Returns the current tick serial.
std::uint64_t pc_render_tick_serial();

/// Clears process-global state for deterministic offline tests and hard reset.
void pc_render_phase_reset();

#ifdef __cplusplus
}
#endif
