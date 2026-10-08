#include "pc_visual_runtime.h"

#include "pc_render_phase.h"
#include "pc_visual_snapshot.h"

#include <algorithm>
#include <vector>

namespace {
PcVisualSnapshotStore sSnapshots;
bool sEnabled = false;

PcVisualMatrix copyMatrix(const float* source)
{
	PcVisualMatrix value {};
	std::copy_n(source, value.size(), value.begin());
	return value;
}
}

void pc_visual_set_enabled(bool enabled)
{
	if (sEnabled == enabled) return;
	sEnabled = enabled;
	sSnapshots.clear();
}

bool pc_visual_is_enabled()
{
	return sEnabled;
}

void pc_visual_begin_authoritative_tick()
{
	if (!sEnabled) return;
	sSnapshots.beginTick();
}

void pc_visual_capture_shape(const void* owner, const float* matrices, std::size_t count)
{
	if (!sEnabled || !owner || !matrices) return;
	for (std::size_t index = 0; index < count; ++index) {
		sSnapshots.capture(
		    { owner, static_cast<std::uint32_t>(index), PcVisualDomain::ShapeMatrix },
		    copyMatrix(matrices + index * 16));
	}
}

void pc_visual_prepare_presentation(double alpha)
{
	if (!sEnabled) return;
	sSnapshots.preparePresentation(alpha);
}

bool pc_visual_resolve_shape(const void* owner, float* matrices, std::size_t count)
{
	if (!sEnabled || !owner || !matrices) return false;
	std::vector<PcVisualMatrix> resolved;
	resolved.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		PcVisualMatrix value {};
		if (!sSnapshots.resolve(
		        { owner, static_cast<std::uint32_t>(index), PcVisualDomain::ShapeMatrix }, value)) {
			return false;
		}
		resolved.push_back(value);
	}
	for (std::size_t index = 0; index < resolved.size(); ++index) {
		std::copy(resolved[index].begin(), resolved[index].end(), matrices + index * 16);
	}
	return true;
}

void pc_visual_synchronize()
{
	if (!sEnabled) return;
	sSnapshots.synchronize();
}

void pc_visual_clear()
{
	sSnapshots.clear();
}

std::size_t pc_visual_current_matrix_count()
{
	return sEnabled ? sSnapshots.currentSize() : 0;
}

std::size_t pc_visual_previous_matrix_count()
{
	return sEnabled ? sSnapshots.previousSize() : 0;
}

PcVisualShapeOverride::PcVisualShapeOverride(const void* owner, float* matrices, std::size_t count)
    : mMatrices(matrices)
    , mActive(false)
{
	if (!sEnabled || pc_render_is_authoritative() || !owner || !matrices || count == 0) return;
	mAuthoritative.assign(matrices, matrices + count * 16);
	if (!pc_visual_resolve_shape(owner, matrices, count)) {
		mAuthoritative.clear();
		return;
	}
	mActive = true;
}

PcVisualShapeOverride::~PcVisualShapeOverride()
{
	if (!mActive) return;
	std::copy(mAuthoritative.begin(), mAuthoritative.end(), mMatrices);
}
