/**
 * @file pc_pause_key_test.cpp
 * @brief Left Option is Pause only when the player has not chosen a second key.
 */

#include "pc_pause_key.h"

#include <cstdio>

namespace {

int sFailures = 0;

void expect(int actual, int expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %d, expected %d\n", what, actual, expected);
		++sFailures;
	}
}

} // namespace

int main()
{
	expect(pc_pause_second_binding(SDL_SCANCODE_RETURN, SDL_SCANCODE_UNKNOWN, false, false), SDL_SCANCODE_LALT,
	       "default Pause gains Left Option");
	expect(pc_pause_second_binding(SDL_SCANCODE_RETURN, SDL_SCANCODE_P, false, false), SDL_SCANCODE_P,
	       "a saved second Pause key stays");
	expect(pc_pause_second_binding(SDL_SCANCODE_P, SDL_SCANCODE_UNKNOWN, false, false), SDL_SCANCODE_UNKNOWN,
	       "a rebound Pause key is left alone");
	expect(pc_pause_second_binding(SDL_SCANCODE_RETURN, SDL_SCANCODE_UNKNOWN, true, false), SDL_SCANCODE_UNKNOWN,
	       "a cleared second key stays clear after migration");
	expect(pc_pause_second_binding(SDL_SCANCODE_RETURN, SDL_SCANCODE_UNKNOWN, false, true), SDL_SCANCODE_UNKNOWN,
	       "a key used by another action is not assigned to Pause");

	if (sFailures == 0) {
		std::printf("pc_pause_key_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_pause_key_test: %d failure(s)\n", sFailures);
	return 1;
}
