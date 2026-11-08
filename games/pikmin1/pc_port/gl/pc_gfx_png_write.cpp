// PNG writer for scenario screenshots (uses nectar_assets/stb via writePng).
#if PIKI_SCENARIOS

#include "pc_gfx_png_write.h"

#include "nectar_assets/image.h"

#include <cstdio>

bool pc_gfx_png_write_rgba_flip_y(const char* path, int width, int height, const unsigned char* rgbaBottomUp, int rowStride)
{
	if (path == nullptr || width <= 0 || height <= 0 || rgbaBottomUp == nullptr) return false;
	if (rowStride <= 0) rowStride = width * 4;
	nectar::Image img;
	img.resize(width, height);
	for (int y = 0; y < height; ++y) {
		const unsigned char* src = rgbaBottomUp + size_t(height - 1 - y) * rowStride;
		for (int x = 0; x < width; ++x) {
			const unsigned char* p = src + x * 4;
			std::uint8_t* d        = img.at(x, y);
			d[0] = p[0];
			d[1] = p[1];
			d[2] = p[2];
			d[3] = p[3];
		}
	}
	if (!nectar::writePng(path, img)) {
		std::fprintf(stderr, "[scenario-screenshot] writePng failed for %s\n", path);
		return false;
	}
	return true;
}

#endif
