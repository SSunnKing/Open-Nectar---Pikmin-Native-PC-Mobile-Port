#pragma once

#if PIKI_SCENARIOS

// rgbaBottomUp: OpenGL readPixels layout (first row is bottom). Writes PNG top-down.
bool pc_gfx_png_write_rgba_flip_y(const char* path, int width, int height, const unsigned char* rgbaBottomUp, int rowStride);

#endif
