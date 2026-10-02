#include "bti_decode.h"

#include <fstream>
#include <iterator>

namespace pikmin {
namespace launcher {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;

struct Rgba {
    u8 r, g, b, a;
};

u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }

Rgba rgb565(u16 v)
{
    return { u8((v >> 11) * 255 / 31), u8(((v >> 5) & 63) * 255 / 63), u8((v & 31) * 255 / 31), 255 };
}

Rgba rgb5a3(u16 v)
{
    if (v & 0x8000) {
        return { u8(((v >> 10) & 31) * 255 / 31), u8(((v >> 5) & 31) * 255 / 31), u8((v & 31) * 255 / 31), 255 };
    }
    return { u8(((v >> 8) & 15) * 17), u8(((v >> 4) & 15) * 17), u8((v & 15) * 17), u8(((v >> 12) & 7) * 255 / 7) };
}

// Recorre la textura por bloques de bw x bh, en el orden en que GX los guarda.
// `block` consume bytes a partir de `at` y devuelve cuántos ha usado.
template <typename Block>
bool forEachBlock(int width, int height, int bw, int bh, std::size_t available, Block block)
{
    std::size_t at = 0;
    for (int by = 0; by < height; by += bh) {
        for (int bx = 0; bx < width; bx += bw) {
            const std::size_t used = block(bx, by, at);
            if (used == 0 || at + used > available) return false;
            at += used;
        }
    }
    return true;
}

} // namespace

bool decodeBti(const u8* data, std::size_t size, RgbaImage& out)
{
    if (!data || size < 0x20) return false;
    const int format = data[0];
    const int w = be16(data + 2), h = be16(data + 4);
    const std::size_t offset = (std::size_t(data[0x1c]) << 24) | (std::size_t(data[0x1d]) << 16)
                             | (std::size_t(data[0x1e]) << 8) | std::size_t(data[0x1f]);
    if (w <= 0 || h <= 0 || offset >= size) return false;
    const u8* px = data + offset;
    const std::size_t available = size - offset;

    out.width = w;
    out.height = h;
    out.pixels.assign(std::size_t(w) * h * 4, 0);
    const auto put = [&](int x, int y, Rgba c) {
        if (x >= w || y >= h) return;
        u8* p = &out.pixels[(std::size_t(y) * w + x) * 4];
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
    };
    const auto grey = [](int v, int a) { return Rgba { u8(v), u8(v), u8(v), u8(a) }; };

    switch (format) {
    case 0: // I4
        return forEachBlock(w, h, 8, 8, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; x += 2) {
                    const u8 b = px[i++];
                    put(bx + x, by + y, grey((b >> 4) * 17, 255));
                    put(bx + x + 1, by + y, grey((b & 15) * 17, 255));
                }
            return 32;
        });
    case 1: // I8
        return forEachBlock(w, h, 8, 4, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x) put(bx + x, by + y, grey(px[i++], 255));
            return 32;
        });
    case 2: // IA4
        return forEachBlock(w, h, 8, 4, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x) {
                    const u8 b = px[i++];
                    put(bx + x, by + y, grey((b & 15) * 17, (b >> 4) * 17));
                }
            return 32;
        });
    case 3: // IA8
        return forEachBlock(w, h, 4, 4, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x, i += 2) put(bx + x, by + y, grey(px[i + 1], px[i]));
            return 32;
        });
    case 4: // RGB565
    case 5: // RGB5A3
        return forEachBlock(w, h, 4, 4, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x, i += 2) {
                    const u16 v = be16(px + i);
                    put(bx + x, by + y, format == 4 ? rgb565(v) : rgb5a3(v));
                }
            return 32;
        });
    case 6: // RGBA8: 32 bytes AR y luego 32 bytes GB por bloque de 4x4
        return forEachBlock(w, h, 4, 4, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 64 > available) return 0;
            const u8* ar = px + i;
            const u8* gb = px + i + 32;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int k = (y * 4 + x) * 2;
                    put(bx + x, by + y, { ar[k + 1], gb[k], gb[k + 1], ar[k] });
                }
            return 64;
        });
    case 14: // CMPR: cuatro subbloques DXT1 de 4x4 por bloque de 8x8
        return forEachBlock(w, h, 8, 8, available, [&](int bx, int by, std::size_t i) -> std::size_t {
            if (i + 32 > available) return 0;
            for (int sy = 0; sy < 8; sy += 4)
                for (int sx = 0; sx < 8; sx += 4, i += 8) {
                    const u16 c0 = be16(px + i), c1 = be16(px + i + 2);
                    Rgba pal[4] = { rgb565(c0), rgb565(c1), {}, {} };
                    if (c0 > c1) {
                        pal[2] = { u8((2 * pal[0].r + pal[1].r) / 3), u8((2 * pal[0].g + pal[1].g) / 3),
                                   u8((2 * pal[0].b + pal[1].b) / 3), 255 };
                        pal[3] = { u8((pal[0].r + 2 * pal[1].r) / 3), u8((pal[0].g + 2 * pal[1].g) / 3),
                                   u8((pal[0].b + 2 * pal[1].b) / 3), 255 };
                    } else {
                        pal[2] = { u8((pal[0].r + pal[1].r) / 2), u8((pal[0].g + pal[1].g) / 2),
                                   u8((pal[0].b + pal[1].b) / 2), 255 };
                        pal[3] = { 0, 0, 0, 0 };
                    }
                    for (int y = 0; y < 4; ++y) {
                        const u8 row = px[i + 4 + y];
                        for (int x = 0; x < 4; ++x) put(bx + sx + x, by + sy + y, pal[(row >> (6 - 2 * x)) & 3]);
                    }
                }
            return 32;
        });
    default:
        return false;
    }
}

bool loadBtiFile(const std::string& path, RgbaImage& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<u8> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeBti(data.data(), data.size(), out);
}

} // namespace launcher
} // namespace pikmin
