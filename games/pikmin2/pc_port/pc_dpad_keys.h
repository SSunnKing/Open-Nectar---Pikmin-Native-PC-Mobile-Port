#pragma once
/**
 * @file pc_dpad_keys.h
 * @brief Second keyboard binding for the D-pad actions.
 *
 * In play the D-pad sprays (up: bitter, down: spicy) and switches the Pikmin
 * type (left, right). The first bindings are the arrow keys, so every
 * keyboard player without arrow keys in reach could not do them. Number
 * keys 1 to 4 are the second bindings: 1 bitter, 2 spicy, 3 and 4 switch.
 *
 * A saved second key is kept. The extra key is filled in once, only where the
 * second key was never set and no other action already uses that number key.
 */

#include <SDL2/SDL.h>

// Action order: up, down, left, right.
inline int pc_dpad_default_second(int dpadIndex)
{
	static const SDL_Scancode keys[4] = { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4 };
	return dpadIndex >= 0 && dpadIndex < 4 ? keys[dpadIndex] : SDL_SCANCODE_UNKNOWN;
}

inline int pc_dpad_second_binding(int dpadIndex, int secondary, bool migrated, bool keyInUse)
{
	if (!migrated && !keyInUse && secondary == SDL_SCANCODE_UNKNOWN)
		return pc_dpad_default_second(dpadIndex);
	return secondary;
}
