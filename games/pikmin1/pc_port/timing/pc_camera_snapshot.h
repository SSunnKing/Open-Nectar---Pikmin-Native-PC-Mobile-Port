#pragma once

#include <cstddef>
#include <unordered_map>

struct PcCameraState {
	float position[3];
	float focus[3];
	float up[3];
	float fov;
	float aspect;
	float nearPlane;
	float farPlane;
	float blurAlpha;
};

class PcCameraSnapshotStore {
public:
	void beginTick();
	bool capture(const void* camera, const PcCameraState& state, bool discontinuity = false);
	void preparePresentation(double alpha);
	bool resolve(const void* camera, PcCameraState& state) const;
	void synchronize();
	void clear();

	std::size_t currentSize() const { return mCurrent.size(); }
	std::size_t previousSize() const { return mPrevious.size(); }

private:
	struct Sample {
		PcCameraState state;
		bool discontinuity;
	};
	using SampleMap = std::unordered_map<const void*, Sample>;

	SampleMap mPrevious;
	SampleMap mCurrent;
	SampleMap mPresentation;
};
