#pragma once

// Escritores de texturas del juego: .bti (TexImg::importBti) y .txe
// (TexImg::importTxe), ambos en graphics.cpp. Los dos tienen cabecera de 32
// bytes big-endian seguida de UNA imagen; los píxeles van tal cual, en bloques
// GX, sin invertir el orden de bytes (PIKI_USE_DGX).
//
// OJO: los dos formatos numeran distinto los formatos de píxel. RGB5A3 es 5 en
// un .bti (código GX) y 2 en un .txe (TexImgFormat). Cada escritor usa su tabla.
//
// Sin E/S de ficheros: se devuelven los bytes y quien llama decide dónde van.

#include "nectar_assets/image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Código de formato del byte 0 de un .bti (GX_TF_*). Las paletizadas (8-10) y
/// los códigos 7-13 no se escriben: el juego los convierte a TEX_FMT_NULL.
enum BtiFormat {
	BTI_I4     = 0,
	BTI_I8     = 1,
	BTI_IA4    = 2,
	BTI_IA8    = 3,
	BTI_RGB565 = 4,
	BTI_RGB5A3 = 5,
	BTI_RGBA8  = 6,
	BTI_CMPR   = 14,
};

/// Bytes de datos de una imagen, igual que TexImg::calcDataSize, o 0 si el
/// formato no se escribe, el tamaño es <= 0 o los lados no son múltiplos del
/// tamaño de bloque que exige el juego (I4 8x8, I8 e IA4 8x4, el resto 4x4).
/// `format` es un BtiFormat.
std::size_t btiDataBytes(int format, int width, int height);

/// Lo mismo para un .txe: `format` es un ModTexFormat (image.h). El juego no
/// comprueba los lados de un .txe, pero el decodificador GL supone bloques
/// enteros, así que se exigen igual. Z8 vale aquí y no en un .bti.
std::size_t txeDataBytes(int format, int width, int height);

struct BtiInput {
	int format = BTI_RGB5A3;
	int width  = 0;
	int height = 0;
	bool alpha = true;   ///< byte 1 de la cabecera; el juego no lo lee
	bool repeatS = false; ///< false: TEX_CLAMP_S
	bool repeatT = false;
	std::vector<std::uint8_t> data; ///< bloques GX; btiDataBytes() bytes exactos
};

/// El .bti listo para guardar: cabecera de 0x20 bytes, numImages 1, sin mipmaps
/// ni paleta, offset de datos 0x20, y los datos justo después. false (con
/// `error`) si el formato o los lados no valen o `data` no tiene el tamaño justo.
bool writeBti(const BtiInput& in, std::vector<std::uint8_t>& out, std::string& error);

struct TxeInput {
	int format = MOD_TEX_RGB5A3; ///< ModTexFormat
	int width  = 0;
	int height = 0;
	/// Bits altos del "short" de formato (mTexFlags): solo caben TEX_CLAMP_S (1),
	/// TEX_MIRROR_S (2) y TEX_Unk2 (4). TEX_CLAMP_T y TEX_MIRROR_T no se pueden
	/// codificar en un .txe.
	int flags = 0;
	std::vector<std::uint8_t> data;
};

bool writeTxe(const TxeInput& in, std::vector<std::uint8_t>& out, std::string& error);

/// Comprueba un .bti con las reglas de importBti: cabecera de 32 bytes, formato
/// conocido, lados múltiplos del bloque, offset de datos 0x20, numImages 1 y
/// tamaño total exacto (0x20 + calcDataSize, ni un byte más).
bool checkBti(const std::uint8_t* data, std::size_t size, std::string& error);

/// Lo mismo para un .txe: 0x20 bytes de cabecera + calcDataSize exactos.
bool checkTxe(const std::uint8_t* data, std::size_t size, std::string& error);

} // namespace nectar
