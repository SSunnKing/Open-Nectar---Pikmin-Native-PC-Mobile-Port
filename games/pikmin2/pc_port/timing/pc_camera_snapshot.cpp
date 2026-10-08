#include "pc_camera_snapshot.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool finiteState(const PcCameraState& state)
{
	for (float value : state.position) if (!std::isfinite(value)) return false;
	for (float value : state.focus) if (!std::isfinite(value)) return false;
	for (float value : state.up) if (!std::isfinite(value)) return false;
	if (!std::isfinite(state.fov) || !std::isfinite(state.aspect) || !std::isfinite(state.nearPlane)
	    || !std::isfinite(state.farPlane) || !std::isfinite(state.blurAlpha)) return false;
	return state.aspect > 0.0f && state.fov > 0.0f && state.fov < 180.0f
	    && state.nearPlane > 0.0f && state.farPlane > state.nearPlane;
}

float lerp(float previous, float current, float alpha)
{
	return previous + (current - previous) * alpha;
}

void normalizeUp(PcCameraState& state, const PcCameraState& fallback)
{
	const float length = std::sqrt(state.up[0] * state.up[0] + state.up[1] * state.up[1] + state.up[2] * state.up[2]);
	if (length > 1e-6f) {
		state.up[0] /= length;
		state.up[1] /= length;
		state.up[2] /= length;
	} else {
		state.up[0] = fallback.up[0];
		state.up[1] = fallback.up[1];
		state.up[2] = fallback.up[2];
	}
}
}

void PcCameraSnapshotStore::beginTick()
{
	mPrevious = std::move(mCurrent);
	mCurrent.clear();
	mPresentation.clear();
}

bool PcCameraSnapshotStore::capture(const void* camera, const PcCameraState& state, bool discontinuity)
{
	if (!camera || !finiteState(state)) return false;
	mCurrent.insert_or_assign(camera, Sample { state, discontinuity });
	return true;
}

void PcCameraSnapshotStore::preparePresentation(double alpha)
{
	const float blend = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
	mPresentation.clear();
	for (const auto& entry : mCurrent) {
		const void* camera = entry.first;
		const Sample& current = entry.second;
		PcCameraState output = current.state;
		const auto previous = mPrevious.find(camera);
		if (!current.discontinuity && previous != mPrevious.end()) {
			const PcCameraState& from = previous->second.state;
			for (int i = 0; i < 3; ++i) {
				output.position[i] = lerp(from.position[i], current.state.position[i], blend);
				output.focus[i] = lerp(from.focus[i], current.state.focus[i], blend);
				output.up[i] = lerp(from.up[i], current.state.up[i], blend);
			}
			output.fov = lerp(from.fov, current.state.fov, blend);
			output.aspect = lerp(from.aspect, current.state.aspect, blend);
			output.nearPlane = lerp(from.nearPlane, current.state.nearPlane, blend);
			output.farPlane = lerp(from.farPlane, current.state.farPlane, blend);
			output.blurAlpha = lerp(from.blurAlpha, current.state.blurAlpha, blend);
			normalizeUp(output, current.state);
		}
		mPresentation.emplace(camera, Sample { output, false });
	}
}

bool PcCameraSnapshotStore::resolve(const void* camera, PcCameraState& state) const
{
	const auto found = mPresentation.find(camera);
	if (found == mPresentation.end()) return false;
	state = found->second.state;
	return true;
}

void PcCameraSnapshotStore::synchronize()
{
	mPrevious = mCurrent;
	mPresentation.clear();
}

void PcCameraSnapshotStore::clear()
{
	mPrevious.clear();
	mCurrent.clear();
	mPresentation.clear();
}
