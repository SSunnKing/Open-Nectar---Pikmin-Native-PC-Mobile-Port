#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pikmin {
namespace launcher {

// Imagen RGBA8 lineal, fila a fila.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Decodifica una textura BTI de GameCube (I4, I8, IA4, IA8, RGB565, RGB5A3,
// RGBA8 y CMPR; las paletizadas no). Mismo algoritmo que tools/bti2png.py.
// El launcher la usa para decorarse con las texturas del juego que el usuario
// extrajo de su propio disco: no lleva ninguna dentro.
bool decodeBti(const std::uint8_t* data, std::size_t size, RgbaImage& out);
bool loadBtiFile(const std::string& path, RgbaImage& out);

} // namespace launcher
} // namespace pikmin
