#include "pc_input_script.h"

namespace {
struct ScriptPad {
	bool active = false;
	unsigned buttons = 0;
	signed char stickX = 0;
	signed char stickY = 0;
};
ScriptPad sScriptPad[4];
} // namespace

void pc_input_script_set(unsigned playerNum, unsigned buttons, int stickX, int stickY)
{
	if (playerNum < 1 || playerNum > 4) {
		return;
	}
	ScriptPad& pad = sScriptPad[playerNum - 1];
	pad.active = true;
	pad.buttons = buttons;
	pad.stickX = static_cast<signed char>(stickX);
	pad.stickY = static_cast<signed char>(stickY);
}

void pc_input_script_clear(unsigned playerNum)
{
	if (playerNum >= 1 && playerNum <= 4) {
		sScriptPad[playerNum - 1] = ScriptPad{};
	}
}

void pc_input_script_clear_all()
{
	for (ScriptPad& pad : sScriptPad) {
		pad = ScriptPad{};
	}
}

bool pc_input_script_override(unsigned playerNum, unsigned* buttons, signed char* stickX, signed char* stickY)
{
	if (playerNum < 1 || playerNum > 4) {
		return false;
	}
	const ScriptPad& pad = sScriptPad[playerNum - 1];
	if (!pad.active) {
		return false;
	}
	if (buttons) {
		*buttons = pad.buttons;
	}
	if (stickX) {
		*stickX = pad.stickX;
	}
	if (stickY) {
		*stickY = pad.stickY;
	}
	return true;
}
