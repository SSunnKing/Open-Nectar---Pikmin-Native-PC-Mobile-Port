#pragma once

// Entradas de partículas (.pcr) que lee zen::particleGenerator::init y su
// pmSetDDF (particleGenerator.cpp), con bBoardColourAnimData::set para el bloque
// de color. Nada lee los bytes hasta que un efecto se crea, así que un contenido
// inválido pasa la carga y falla la primera vez que ese efecto dispara.
//
// Diseño, big-endian:
//   0-3   versión ("1-02" en los .pcr del disco; el puerto no la comprueba)
//   4-5   u16 desplazamiento del bloque DDF
//   6     no cero: billboard. 7 no se lee.
//   billboard: bloque de color en 8. Si no: f32 escala de largo en 8, f32 desvío
//             del pivote en Y en 12 y bloque de color en 16.
//   color: u8 modo de mezcla, u8 duración, u8 flags, u8 maxFrame, maxFrame f32
//          umbrales, maxFrame RGBA primarios, maxFrame RGBA de entorno.
//   DDF:   ver pcr.cpp. Con solo los campos fijos mide 136 bytes.
//
// Este escritor solo cubre los campos fijos del DDF: los flags de campos con
// claves (tasa, radio y velocidad, bits 6-11) y los bloques opcionales (hijos y
// campos, bits 13 y 16-23) se rechazan, porque el sustituto no los necesita.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Bit 12 de los flags del DDF: SetPtclsLife vuelve de inmediato y no nace
/// ninguna partícula (particleGenerator.cpp:505-507).
constexpr std::uint32_t kPcrDisableEmission = 0x1000;

struct PcrRgba {
	std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

struct PcrColourAnim {
	std::uint8_t blendMode = 0;
	std::uint8_t duration  = 30; ///< al menos 2: update divide por (duration - 1)
	std::uint8_t flags     = 0;
	/// maxFrame = thresholds.size(), de 2 a 255: update lee el umbral siguiente.
	std::vector<float> thresholds = { 0.0f, 1.0f };
	std::vector<PcrRgba> primary  = std::vector<PcrRgba>(2);
	std::vector<PcrRgba> env      = std::vector<PcrRgba>(2);
};

struct PcrParticle {
	std::string version = "1-02"; ///< 4 bytes
	bool billboard      = true;
	float lengthScale = 0, pivotOffsetY = 0; ///< solo si no es billboard
	PcrColourAnim colour;

	std::uint32_t flags = kPcrDisableEmission;
	float emitOffset[3] = { 0, 0, 0 }, emitDir[3] = { 0, 0, 0 }, emitBox[3] = { 0, 0, 0 };
	float spread = 0, emissionRate = 0, rateJitter = 0, radiusScale = 0, radius = 0, initVelocity = 0;
	float velocityJitter = 0, drag = 0, dragJitter = 0, maxVelocity = 0;
	float scaleThreshold1 = 0, scaleThreshold2 = 0, minScaleFactor1 = 0, minScaleFactor2 = 0, scaleSize = 0, sizeJitter = 0;
	float alphaThreshold1 = 0, alphaThreshold2 = 0, alphaJitter = 0, lifetimeJitter = 0;
	std::int16_t rotSpeedMin = 0, rotSpeedJitter = 0, rotAngle = 0, baseLifetime = 0, maxFrame = 0;
	std::uint8_t freeMotionTime = 0, unused = 0;
	/// 0 = el generador nunca termina (`if (mMaxPasses)`, particleGenerator.cpp:273-282)
	/// y el pool de 0x200 generadores se agota.
	std::uint8_t maxPasses = 0, blendFactor = 0, zMode = 0;
};

/// Tamaño del bloque DDF con solo campos fijos.
constexpr std::size_t kPcrDdfSize = 136;

/// false si el color o los flags salen de lo que cubre el escritor.
bool writePcr(const PcrParticle& p, std::vector<std::uint8_t>& out, std::string& error);

} // namespace nectar
