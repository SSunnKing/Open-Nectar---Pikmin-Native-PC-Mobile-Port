/**
 * @file pc_menu_repeat.cpp
 * @brief Menu input repeat. See pc_menu_repeat.h for why it exists.
 */

#include "pc_menu_repeat.h"

namespace {
constexpr int kSlots = 6;
bool sHeld[kSlots]                 = {};
std::uint32_t sNextRepeat[kSlots]  = {};
} // namespace

bool pc_menu_edge(bool pressed, int slot, std::uint32_t nowMs)
{
	if (slot < 0 || slot >= kSlots) {
		return false;
	}
	if (!pressed) {
		sHeld[slot] = false;
		return false;
	}
	if (!sHeld[slot]) {
		sHeld[slot]       = true;
		sNextRepeat[slot] = nowMs + kPcMenuRepeatDelayMs;
		return true;
	}
	if (nowMs < sNextRepeat[slot]) {
		return false;
	}
	sNextRepeat[slot] = nowMs + kPcMenuRepeatRateMs;
	return true;
}

void pc_menu_edge_reset(void)
{
	for (int i = 0; i < kSlots; i++) {
		sHeld[i]       = false;
		sNextRepeat[i] = 0;
	}
}
