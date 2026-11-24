#include "pc_dpad_keys.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "Failed: %s at line %d\n", #expr, __LINE__); std::abort(); } } while (0)

int main() {
    CHECK(pc_dpad_default_second(0) == SDL_SCANCODE_1 && pc_dpad_default_second(3) == SDL_SCANCODE_4);
    CHECK(pc_dpad_default_second(4) == SDL_SCANCODE_UNKNOWN && pc_dpad_default_second(-1) == SDL_SCANCODE_UNKNOWN);
    // An old config with no second key gains the number key once.
    CHECK(pc_dpad_second_binding(1, SDL_SCANCODE_UNKNOWN, false, false) == SDL_SCANCODE_2);
    // Already migrated: a cleared binding stays cleared.
    CHECK(pc_dpad_second_binding(1, SDL_SCANCODE_UNKNOWN, true, false) == SDL_SCANCODE_UNKNOWN);
    // A saved second key is kept.
    CHECK(pc_dpad_second_binding(1, SDL_SCANCODE_K, false, false) == SDL_SCANCODE_K);
    // A number key another action uses is not taken.
    CHECK(pc_dpad_second_binding(1, SDL_SCANCODE_UNKNOWN, false, true) == SDL_SCANCODE_UNKNOWN);
}
