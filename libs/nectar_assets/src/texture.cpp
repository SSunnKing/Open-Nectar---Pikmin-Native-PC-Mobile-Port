#include "nectar_assets/texture.h"

#include "be_writer.h"

namespace nectar {

namespace {

constexpr std::size_t HEADER_BYTES = 0x20;

struct Layout {
	int tileX, tileY;
	int num, den; ///< bytes por píxel = num / den
};

// calcDataSize (graphics.cpp) y getTileSize, por ModTexFormat (TexImgFormat).
bool layoutOf(int modFormat, Layout& l)
{
	switch (modFormat) {
	case MOD_TEX_RGB565:
	case MOD_TEX_RGB5A3:
	case MOD_TEX_IA8: l = { 4, 4, 2, 1 }; return true;
	case MOD_TEX_CMPR: l = { 4, 4, 1, 2 }; return true;
	case MOD_TEX_I4: l = { 8, 8, 1, 2 }; return true;
	case MOD_TEX_I8:
	case MOD_TEX_IA4: l = { 8, 4, 1, 1 }; return true;
	case MOD_TEX_RGBA8: l = { 4, 4, 4, 1 }; return true;
	case MOD_TEX_Z8: l = { 4, 4, 1, 1 }; return true;
	default: return false;
	}
}

// Código GX de un .bti -> ModTexFormat; -1 si no se escribe.
int modFormatOfBti(int btiFormat)
{
	switch (btiFormat) {
	case BTI_I4: return MOD_TEX_I4;
	case BTI_I8: return MOD_TEX_I8;
	case BTI_IA4: return MOD_TEX_IA4;
	case BTI_IA8: return MOD_TEX_IA8;
	case BTI_RGB565: return MOD_TEX_RGB565;
	case BTI_RGB5A3: return MOD_TEX_RGB5A3;
	case BTI_RGBA8: return MOD_TEX_RGBA8;
	case BTI_CMPR: return MOD_TEX_CMPR;
	default: return -1;
	}
}

std::size_t dataBytes(int modFormat, int width, int height)
{
	Layout l;
	if (modFormat < 0 || !layoutOf(modFormat, l)) return 0;
	if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) return 0;
	if (width % l.tileX || height % l.tileY) return 0;
	return std::size_t(width) * std::size_t(height) * std::size_t(l.num) / std::size_t(l.den);
}

} // namespace

std::size_t btiDataBytes(int format, int width, int height) { return dataBytes(modFormatOfBti(format), width, height); }

std::size_t txeDataBytes(int format, int width, int height) { return dataBytes(format, width, height); }

bool writeBti(const BtiInput& in, std::vector<std::uint8_t>& out, std::string& error)
{
	const std::size_t need = btiDataBytes(in.format, in.width, in.height);
	if (need == 0) {
		error = "bti: format " + std::to_string(in.format) + " or size " + std::to_string(in.width) + "x" + std::to_string(in.height)
		      + " is not writable (tile multiples: I4 8x8, I8/IA4 8x4, others 4x4)";
		return false;
	}
	if (in.data.size() != need) {
		error = "bti: " + std::to_string(in.data.size()) + " data bytes, the game reads exactly " + std::to_string(need);
		return false;
	}
	BeWriter w;
	w.u8(std::uint8_t(in.format)); // 0x00
	w.u8(in.alpha ? 1 : 0);        // 0x01
	w.u16(std::uint16_t(in.width));
	w.u16(std::uint16_t(in.height));
	w.u8(in.repeatS ? 1 : 0); // 0x06
	w.u8(in.repeatT ? 1 : 0); // 0x07
	w.u32(0);                 // 0x08 sin paleta: _08/_09, nº de entradas
	w.u32(0);                 // 0x0C offset de paleta
	w.u32(0);                 // 0x10 sin mipmaps ni LOD
	w.u8(1);                  // 0x14 filtro de reducción: lineal, como los de disco
	w.u8(1);                  // 0x15 filtro de ampliación
	w.u16(0);                 // 0x16 LOD mínimo/máximo
	w.u8(1);                  // 0x18 numImages
	w.u8(0);                  // 0x19
	w.u16(0);                 // 0x1A sesgo de LOD
	w.u32(HEADER_BYTES);      // 0x1C offset de datos
	w.bytes(in.data);
	out = std::move(w.b);
	return true;
}

bool writeTxe(const TxeInput& in, std::vector<std::uint8_t>& out, std::string& error)
{
	const std::size_t need = txeDataBytes(in.format, in.width, in.height);
	if (need == 0) {
		error = "txe: format " + std::to_string(in.format) + " or size " + std::to_string(in.width) + "x" + std::to_string(in.height)
		      + " is not writable";
		return false;
	}
	if (in.data.size() != need) {
		error = "txe: " + std::to_string(in.data.size()) + " data bytes, the game reads exactly " + std::to_string(need);
		return false;
	}
	if (in.flags < 0 || in.flags > 0xFF) {
		error = "txe: flags do not fit the high byte of the format short";
		return false;
	}
	BeWriter w;
	w.u16(std::uint16_t(in.width));
	w.u16(std::uint16_t(in.height));
	w.u16(std::uint16_t((in.flags << 8) | in.format)); // mTexFlags = fmt >> 8
	w.u16(0);                                           // sin uso
	w.u32(0);                                           // sin uso
	for (int i = 0; i < 10; i++) w.u16(0);              // sin uso
	w.bytes(in.data);
	out = std::move(w.b);
	return true;
}

bool checkBti(const std::uint8_t* d, std::size_t size, std::string& error)
{
	if (!d || size < HEADER_BYTES) {
		error = "bti: shorter than the 32-byte header";
		return false;
	}
	const int w = (d[2] << 8) | d[3], h = (d[4] << 8) | d[5];
	const std::uint32_t offset = (std::uint32_t(d[0x1C]) << 24) | (std::uint32_t(d[0x1D]) << 16) | (std::uint32_t(d[0x1E]) << 8) | d[0x1F];
	const std::size_t need = btiDataBytes(d[0], w, h);
	if (need == 0) {
		error = "bti: format or size not accepted by importBti";
		return false;
	}
	if (offset != HEADER_BYTES) {
		error = "bti: image data offset is not 0x20";
		return false;
	}
	if (d[0x18] != 1) {
		error = "bti: numImages is not 1";
		return false;
	}
	if (size != HEADER_BYTES + need) {
		error = "bti: file size is not 0x20 + calcDataSize";
		return false;
	}
	return true;
}

bool checkTxe(const std::uint8_t* d, std::size_t size, std::string& error)
{
	if (!d || size < HEADER_BYTES) {
		error = "txe: shorter than the 32-byte header";
		return false;
	}
	const int w = (d[0] << 8) | d[1], h = (d[2] << 8) | d[3];
	const int format = d[5]; // low byte of the format short
	const std::size_t need = txeDataBytes(format, w, h);
	if (need == 0) {
		error = "txe: format or size not accepted";
		return false;
	}
	if (size != HEADER_BYTES + need) {
		error = "txe: file size is not 0x20 + calcDataSize";
		return false;
	}
	return true;
}

} // namespace nectar
