#include "pc_icon.h"

#include <SDL.h>

#include "pc_icon_data.h"

void pc_icon_apply(SDL_Window* window)
{
	if (!window) return;
	SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom((void*)kPcIconRgba, kPcIconW, kPcIconH, 32, kPcIconW * 4,
	                                                       SDL_PIXELFORMAT_RGBA32);
	if (!icon) return;
	SDL_SetWindowIcon(window, icon);
	SDL_FreeSurface(icon);
}
