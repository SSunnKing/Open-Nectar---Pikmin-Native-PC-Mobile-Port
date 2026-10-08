#pragma once

#include <cstddef>
#include <vector>

// Adapter between game matrices and the platform-independent snapshot store.
// Each matrix is a contiguous 4x4 block of floats.
void pc_visual_set_enabled(bool enabled);
bool pc_visual_is_enabled();
void pc_visual_begin_authoritative_tick();
void pc_visual_capture_shape(const void* owner, const float* matrices, std::size_t count);
void pc_visual_prepare_presentation(double alpha);
bool pc_visual_resolve_shape(const void* owner, float* matrices, std::size_t count);
void pc_visual_synchronize();
void pc_visual_clear();

std::size_t pc_visual_current_matrix_count();
std::size_t pc_visual_previous_matrix_count();

// Temporarily replaces a complete shape pose during Presentation and restores
// the authoritative buffer on every exit path. Partial replacement is never
// exposed: if any slot is missing, the original pose remains untouched.
class PcVisualShapeOverride {
public:
	PcVisualShapeOverride(const void* owner, float* matrices, std::size_t count);
	~PcVisualShapeOverride();

	PcVisualShapeOverride(const PcVisualShapeOverride&) = delete;
	PcVisualShapeOverride& operator=(const PcVisualShapeOverride&) = delete;

	bool active() const { return mActive; }

private:
	float* mMatrices;
	std::vector<float> mAuthoritative;
	bool mActive;
};
