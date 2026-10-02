#include "pc_visual_snapshot.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

std::size_t PcVisualKeyHash::operator()(const PcVisualKey& key) const
{
	const std::size_t owner = std::hash<const void*> {}(key.owner);
	const std::size_t slot = std::hash<std::uint32_t> {}(key.slot);
	const std::size_t domain = std::hash<std::uint8_t> {}(static_cast<std::uint8_t>(key.domain));
	return owner ^ (slot + 0x9e3779b9u + (owner << 6) + (owner >> 2)) ^ (domain << 1);
}

void PcVisualSnapshotStore::beginTick()
{
	mPrevious = std::move(mCurrent);
	mCurrent.clear();
	mPresentation.clear();
}

bool PcVisualSnapshotStore::capture(const PcVisualKey& key, const PcVisualMatrix& value, bool discontinuity)
{
	if (!key.owner) return false;
	for (float component : value) {
		if (!std::isfinite(component)) return false;
	}
	mCurrent.insert_or_assign(key, Sample { value, discontinuity });
	return true;
}

void PcVisualSnapshotStore::preparePresentation(double alpha)
{
	const float blend = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
	mPresentation.clear();
	mPresentation.reserve(mCurrent.size());

	for (const auto& entry : mCurrent) {
		const PcVisualKey& key = entry.first;
		const Sample& current = entry.second;
		Sample output { current.value, false };
		const auto previous = mPrevious.find(key);
		// discontinuity describes the incoming edge to the current sample. Once
		// that sample becomes previous, the following edge may interpolate again.
		if (!current.discontinuity && previous != mPrevious.end()) {
			for (std::size_t i = 0; i < output.value.size(); ++i) {
				output.value[i] = previous->second.value[i]
				                + (current.value[i] - previous->second.value[i]) * blend;
			}
		}
		mPresentation.emplace(key, output);
	}
}

bool PcVisualSnapshotStore::resolve(const PcVisualKey& key, PcVisualMatrix& value) const
{
	const auto found = mPresentation.find(key);
	if (found == mPresentation.end()) return false;
	value = found->second.value;
	return true;
}

void PcVisualSnapshotStore::synchronize()
{
	mPrevious = mCurrent;
	mPresentation.clear();
}

void PcVisualSnapshotStore::clear()
{
	mPrevious.clear();
	mCurrent.clear();
	mPresentation.clear();
}
