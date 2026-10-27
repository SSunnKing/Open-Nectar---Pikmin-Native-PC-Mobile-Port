#pragma once
/**
 * @file pc_exit_after.h
 * @brief PIKMIN_EXIT_AFTER_FRAMES=N: quit cleanly after N video frames.
 *
 * For smoke tests and scripted runs: the game boots, draws N retraces and
 * leaves through the normal quit path (exit code 0), so a harness can tell a
 * clean run from a hang without killing the process. Pikmin 1's counterpart is
 * PIKMIN_NETPLAY_EXIT_AFTER_TICKS. Unset, empty, 0 or malformed means off.
 */

#include <cstdint>

/// The switch's frame count, or 0 (off) for null, empty, zero, a sign,
/// trailing characters or a value past UINT32_MAX.
inline std::uint32_t pc_exit_after_frames_parse(const char* text)
{
	if (text == nullptr || *text == '\0') return 0;
	std::uint64_t n = 0;
	for (const char* p = text; *p != '\0'; ++p) {
		if (*p < '0' || *p > '9') return 0;
		n = n * 10u + std::uint64_t(*p - '0');
		if (n > UINT32_MAX) return 0;
	}
	return std::uint32_t(n);
}

/// True once @p retrace reaches @p limit; never when the switch is off (0).
inline bool pc_exit_after_frames_reached(std::uint32_t limit, std::uint32_t retrace)
{
	return limit != 0 && retrace >= limit;
}
