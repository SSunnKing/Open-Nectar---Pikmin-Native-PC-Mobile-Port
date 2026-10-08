#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

// A render value is keyed by the persistent object which owns it and by a
// semantic slot within that object.  The address of a transient matrix buffer
// and the order in which GX commands happen are deliberately not part of the
// identity.
enum class PcVisualDomain : std::uint8_t {
	ShapeMatrix,
	CreatureMatrix,
	Camera,
};

struct PcVisualKey {
	const void* owner;
	std::uint32_t slot;
	PcVisualDomain domain;

	bool operator==(const PcVisualKey& other) const
	{
		return owner == other.owner && slot == other.slot && domain == other.domain;
	}
};

struct PcVisualKeyHash {
	std::size_t operator()(const PcVisualKey& key) const;
};

// Kept independent of Matrix4f so the lifecycle and interpolation rules remain
// testable without linking the game or any GameCube compatibility code.
using PcVisualMatrix = std::array<float, 16>;

class PcVisualSnapshotStore {
public:
	// Starts collection of the next complete authoritative state.
	void beginTick();

	// Capture copies the value: callers may safely reuse their matrix storage.
	// A discontinuity (teleport, scene transition, newly rebound object) forces
	// presentation to use the current value instead of blending from history.
	bool capture(const PcVisualKey& key, const PcVisualMatrix& value, bool discontinuity = false);

	// Builds a read-only presentation set. Alpha is clamped to [0, 1].
	void preparePresentation(double alpha);
	bool resolve(const PcVisualKey& key, PcVisualMatrix& value) const;

	// Use at pause/map/load/scene boundaries where previous and current state
	// must become visually identical before presentation resumes.
	void synchronize();
	void clear();

	std::size_t currentSize() const { return mCurrent.size(); }
	std::size_t previousSize() const { return mPrevious.size(); }
	std::size_t presentationSize() const { return mPresentation.size(); }

private:
	struct Sample {
		PcVisualMatrix value;
		bool discontinuity;
	};
	using SampleMap = std::unordered_map<PcVisualKey, Sample, PcVisualKeyHash>;

	SampleMap mPrevious;
	SampleMap mCurrent;
	SampleMap mPresentation;
};
