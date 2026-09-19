#pragma once

// Reusable scripted-pad input for private fixtures (menus, section transitions).
//
// A fixture sets the virtual pad state it wants for a player port; the core
// ControllerMgr uses it instead of the physical pad while active. This lets a
// fixture drive ANY menu/section (results, map select, title) without reaching
// into section internals. Inert in production: with no script set,
// pc_input_script_override() returns false and the physical pad is used.
//
// Shared-semantics note (#186): this is an additive, default-off input test
// hook. Only fixtures call pc_p2_input_script_set().
void pc_input_script_set(unsigned playerNum, unsigned buttons, int stickX = 0, int stickY = 0);
void pc_input_script_clear(unsigned playerNum);
void pc_input_script_clear_all();

// Returns true and fills the virtual pad when a script is active for this 1-based
// player port. Returns false when the physical pad should be used.
bool pc_input_script_override(unsigned playerNum, unsigned* buttons, signed char* stickX, signed char* stickY);
