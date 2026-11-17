#pragma once
/**
 * @file pc_pause_key.h
 * @brief Second keyboard binding for Pause.
 *
 * Pikmin 2's pause menu is also the go-to-sunset menu, so one key opens
 * both. Enter stays Pause, matching Pikmin 1's default Start binding, and
 * inside that menu Enter confirms the highlighted row. Left Option is the
 * extra Pause key: it is what a Mac keyboard uses for the key beside the
 * space bar, and it does not have to be the menu's accept key.
 *
 * A saved second key is kept. The extra key is filled in only when Pause is
 * still Enter and its second key was never set.
 */

#include <SDL2/SDL.h>

inline int pc_pause_second_binding(int primary, int secondary, bool migrated, bool altInUse)
{
	if (!migrated && !altInUse && primary == SDL_SCANCODE_RETURN && secondary == SDL_SCANCODE_UNKNOWN)
		return SDL_SCANCODE_LALT;
	return secondary;
}
