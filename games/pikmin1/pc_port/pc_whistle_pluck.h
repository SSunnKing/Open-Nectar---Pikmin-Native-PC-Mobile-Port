#pragma once

struct Navi;

// "Whistle Pluck" mod: convert the one sprout nearest the whistle cursor, if
// any. No target is retained between calls. Returns true if one was plucked.
bool pc_whistle_pluck(Navi* navi, float radius);

// Seconds between plucks while the whistle is held.
constexpr float PC_WHISTLE_PLUCK_INTERVAL = 0.08f;
