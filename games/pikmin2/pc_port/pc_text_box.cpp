#include "pc_text_box.h"

#include <SDL2/SDL.h>

namespace {

PcTextAccept sAccept;
PcTextAccept sCancel;
bool sLatchedEnter = false;

bool portAcceptDown()
{
	int numKeys = 0;
	const Uint8* state = SDL_GetKeyboardState(&numKeys);
	auto bit = [&](int scancode) {
		return state && scancode >= 0 && scancode < numKeys && state[scancode] != 0;
	};
	// Return, a Mac portable's Return key, Return2, and Space. Left click too,
	// including Classic mode, where the mouse is not the A binding.
	if (sLatchedEnter || bit(SDL_SCANCODE_RETURN) || bit(SDL_SCANCODE_KP_ENTER) || bit(SDL_SCANCODE_RETURN2)
	    || bit(SDL_SCANCODE_SPACE))
		return true;
	int x = 0, y = 0;
	return (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
}

bool portCancelDown()
{
	int numKeys = 0;
	const Uint8* state = SDL_GetKeyboardState(&numKeys);
	const bool backspace = state && SDL_SCANCODE_BACKSPACE < numKeys && state[SDL_SCANCODE_BACKSPACE] != 0;
	int x = 0, y = 0;
	const Uint32 buttons = SDL_GetMouseState(&x, &y);
	return pc_text_cancel_wants(backspace, (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0,
	                            (buttons & SDL_BUTTON(SDL_BUTTON_X1)) != 0);
}

} // namespace

void pc_text_update_input(bool latchedEnter)
{
	sLatchedEnter = latchedEnter;
	if (!portAcceptDown())
		pc_text_take_accept(sAccept, false);
	if (!portCancelDown())
		pc_text_take_accept(sCancel, false);
}

bool pc_text_port_accept_pressed(void)
{
	return pc_text_take_accept(sAccept, portAcceptDown());
}

bool pc_text_port_cancel_pressed(void)
{
	return pc_text_take_accept(sCancel, portCancelDown());
}
