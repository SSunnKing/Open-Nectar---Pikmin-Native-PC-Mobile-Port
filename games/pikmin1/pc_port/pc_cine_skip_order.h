#pragma once

/**
 * @file pc_cine_skip_order.h
 * @brief Order in which a skipped cutscene replays its remaining keys.
 *
 * A .cin file lists keys per actor, not by frame: demo113 lists the fade-in
 * at frame 758 before the fade-out at frame 730. Playing the movie fires them
 * by frame, so the skip replay must too, or a skipped scene can end in the
 * opposite state from a watched one. Keys on the same frame keep file order.
 */

#include <algorithm>

template <typename It, typename FrameOf>
inline void pc_cine_sort_skip_keys(It begin, It end, FrameOf frameOf)
{
	std::stable_sort(begin, end, [&](const auto& a, const auto& b) { return frameOf(a) < frameOf(b); });
}
