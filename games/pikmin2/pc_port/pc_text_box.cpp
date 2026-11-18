#include "pc_text_box.h"

#include <SDL2/SDL.h>

namespace {

PcTextAccept sAccept;
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

} // namespace

void pc_text_update_input(bool latchedEnter)
{
	sLatchedEnter = latchedEnter;
	if (!portAcceptDown())
		pc_text_take_accept(sAccept, false);
}

bool pc_text_port_accept_pressed(void)
{
	return pc_text_take_accept(sAccept, portAcceptDown());
}
