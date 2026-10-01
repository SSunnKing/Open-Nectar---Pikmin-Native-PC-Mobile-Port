#ifndef PC_PAD_BINDINGS_H
#define PC_PAD_BINDINGS_H

/**
 * @file pc_pad_bindings.h
 * @brief What a gamepad's physical inputs do, given the stored bindings.
 *
 * The F1 menu stores one value per action: PC_GP_DEFAULT (use the stock
 * mapping), PC_GP_UNBOUND (cleared on purpose), or a button / axis binding.
 * This resolves those values into the routing pc_window_read_gamepad applies,
 * without touching SDL, so the whole mapping can be unit tested.
 *
 * The point of the routing: the analog triggers and sticks used to reach the
 * game whatever the bindings said. Remapping or clearing L therefore still left
 * the left trigger driving the game's analog L reads (the camera follows the
 * player when it is held partway), and a cleared stick direction still moved
 * the stick. Here an analog source only feeds an action while that action is
 * actually bound to it.
 */

#include <iosfwd>
#include <string>

#include "pc_window.h"

/// Physical state of one controller, in SDL's units (triggers 0..32767).
struct PcPadRaw {
	bool button[SDL_CONTROLLER_BUTTON_MAX];
	int  axis[SDL_CONTROLLER_AXIS_MAX];
};

/// Axis magnitude at which an axis binding counts as "held" (a press).
#define PC_PAD_AXIS_HELD 12000

/// Whether a button or axis binding is held. `button(int)` and `axis(int)` read
/// the physical state; a template so SDL and the unit test share one rule.
template <class ButtonFn, class AxisFn>
inline bool pc_pad_bind_held(int bind, ButtonFn button, AxisFn axis)
{
	if (bind < 0)
		return false; // -1 / PC_GP_UNBOUND: nothing to hold
	if (bind < SDL_CONTROLLER_BUTTON_MAX)
		return button(bind);
	if (bind >= PC_GP_AXIS_BIND) {
		const int a        = (bind - PC_GP_AXIS_BIND) / 2;
		const int positive = (bind - PC_GP_AXIS_BIND) & 1;
		if (a < 0 || a >= SDL_CONTROLLER_AXIS_MAX)
			return false;
		const int v = axis(a);
		return positive ? v > PC_PAD_AXIS_HELD : v < -PC_PAD_AXIS_HELD;
	}
	return false;
}

bool pc_pad_raw_bind_held(const PcPadRaw& raw, int bind);

/// Directions of a stick, as the pad sees them (after the invert option).
enum { PC_PAD_DIR_LEFT, PC_PAD_DIR_RIGHT, PC_PAD_DIR_UP, PC_PAD_DIR_DOWN, PC_PAD_DIR_COUNT };

struct PcPadRoute {
	/// What each action tests with pc_pad_raw_bind_held: a button, a
	/// PC_GP_AXIS_BIND value, or -1 for nothing.
	int bind[PC_KEY_ACT_COUNT];
	/// SDL trigger axis whose analog value feeds triggerLeft / triggerRight, or
	/// -1 when nothing does. The stock mapping feeds each from its own trigger.
	int triggerAxis[2];
	/// Whether the left / right stick still drives each direction by itself.
	/// Only a cleared direction switches it off: a button bound to a stick
	/// direction has always been an addition to the stick, not a replacement.
	bool stick[PC_PAD_DIR_COUNT];
	bool cstick[PC_PAD_DIR_COUNT];
	/// Free Camera's stock Swarm button (D-pad Down) applies only while the
	/// Swarm binding has not been changed.
	bool freeCamSwarmDpad;
};

/// `stored` is the PC_KEY_ACT_COUNT stored values of one player's pad.
/// `stickInvert` / `cstickInvert` are the invert options (bit 0 = X, bit 1 = Y)
/// so a binding to a physical stick direction is matched to the right direction.
/// A default L / R trigger or stick direction yields to another action that is
/// explicitly bound to the same physical input; stock bindings claim nothing,
/// so they route exactly as before.
void pc_pad_route_build(const int* stored, PcPadRoute* route, int stickInvert = 0, int cstickInvert = 0);

/// Feeds the L and R actions: digital bindings, then the analog triggers. Same
/// order and scaling the port always used, so stock bindings are unchanged.
/// `deadZoneRaw` is the stick dead zone in SDL units.
void pc_pad_route_triggers(const PcPadRoute& route, const PcPadRaw& raw, int deadZoneRaw,
                           u16* button, u8* triggerL, u8* triggerR);

/// Whether a stick axis value (SDL units, already inverted if the option is on)
/// may drive its direction. Zero never drives anything.
bool pc_pad_route_stick_live(const bool dir[PC_PAD_DIR_COUNT], int value, bool vertical);

/// Settings file: the three binding tables as key_N / gp_N / gp2_N lines.
void pc_pad_bindings_write(std::ostream& out, const int* keyboard, const int* pad, const int* padP2);
/// Consumes one "key = value" settings line when the key is a binding. Returns
/// true if it was a binding key, even if the value was rejected (the table then
/// keeps what it had, which is how old or damaged files fall back to defaults).
bool pc_pad_bindings_parse(const std::string& key, const std::string& val, int* keyboard, int* pad, int* padP2);
/// Whether a stored gamepad value is one this build understands.
bool pc_pad_bind_value_valid(int value);

#endif // PC_PAD_BINDINGS_H
