#include "nectar_assets/image.h"

// El decodificador de tiles es el del launcher (se compila con esta librería).
#include "bti_decode.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// Third-party: stb_image_write calls sprintf, which the macOS SDK marks deprecated.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <fstream>
#include <iterator>

namespace nectar {

namespace {

bool fromLauncher(const pikmin::launcher::RgbaImage& in, Image& out)
{
	if (in.width <= 0 || in.height <= 0) return false;
	out.width  = in.width;
	out.height = in.height;
	out.rgba   = in.pixels;
	return true;
}

// Formato de .mod -> código GX que entiende decodeBti.
int gxFormat(int modFormat)
{
	switch (modFormat) {
	case MOD_TEX_I4: return 0;
	case MOD_TEX_I8: return 1;
	case MOD_TEX_IA4: return 2;
	case MOD_TEX_IA8: return 3;
	case MOD_TEX_RGB565: return 4;
	case MOD_TEX_RGB5A3: return 5;
	case MOD_TEX_RGBA8: return 6;
	case MOD_TEX_CMPR: return 14;
	default: return -1;
	}
}

} // namespace

bool decodeBti(const std::uint8_t* data, std::size_t size, Image& out)
{
	pikmin::launcher::RgbaImage img;
	return pikmin::launcher::decodeBti(data, size, img) && fromLauncher(img, out);
}

bool loadBti(const std::string& path, Image& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return decodeBti(bytes.data(), bytes.size(), out);
}

bool decodeModTexture(int format, int width, int height, const std::uint8_t* data, std::size_t size, Image& out)
{
	const int gx = gxFormat(format);
	if (gx < 0 || width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) return false;
	// Cabecera BTI mínima delante de los datos: formato, tamaño y desplazamiento.
	std::vector<std::uint8_t> bti(0x20 + size, 0);
	bti[0]    = std::uint8_t(gx);
	bti[2]    = std::uint8_t(width >> 8);
	bti[3]    = std::uint8_t(width);
	bti[4]    = std::uint8_t(height >> 8);
	bti[5]    = std::uint8_t(height);
	bti[0x1f] = 0x20;
	std::copy(data, data + size, bti.begin() + 0x20);
	return decodeBti(bti.data(), bti.size(), out);
}

Image shrinkToFit(const Image& src, int maxW, int maxH)
{
	if (src.empty() || (src.width <= maxW && src.height <= maxH)) return src;
	const double k = std::min(double(maxW) / src.width, double(maxH) / src.height);
	Image out;
	out.resize(std::max(1, int(src.width * k)), std::max(1, int(src.height * k)));
	for (int y = 0; y < out.height; y++) {
		const int y0 = y * src.height / out.height, y1 = std::max(y0 + 1, (y + 1) * src.height / out.height);
		for (int x = 0; x < out.width; x++) {
			const int x0 = x * src.width / out.width, x1 = std::max(x0 + 1, (x + 1) * src.width / out.width);
			unsigned sum[4] = { 0, 0, 0, 0 }, n = 0;
			for (int sy = y0; sy < y1; sy++) {
				for (int sx = x0; sx < x1; sx++) {
					const std::uint8_t* p = src.at(sx, sy);
					for (int c = 0; c < 4; c++) sum[c] += p[c];
					n++;
				}
			}
			for (int c = 0; c < 4; c++) out.at(x, y)[c] = std::uint8_t(sum[c] / n);
		}
	}
	return out;
}

bool writePng(const std::string& path, const Image& img)
{
	if (img.empty()) return false;
	return stbi_write_png(path.c_str(), img.width, img.height, 4, img.rgba.data(), img.width * 4) != 0;
}

} // namespace nectar
