#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// GL mip dimensions floor each axis independently, stopping at 1x1.
inline uint32_t pc_texture_next_mip_size(uint32_t size)
{
    return std::max(1u, size / 2);
}

inline int pc_texture_mip_count(uint32_t width, uint32_t height)
{
    if (!width || !height) return 0;
    int count = 1;
    while (width > 1 || height > 1) {
        width = pc_texture_next_mip_size(width);
        height = pc_texture_next_mip_size(height);
        ++count;
    }
    return count;
}

// Box filter all texels, including the last row/column of odd-sized images.
// Alpha stays linear; sRGB colour is averaged in linear light.
inline std::vector<uint8_t> pc_texture_downsample_rgba8(const std::vector<uint8_t>& source,
                                                       uint32_t width, uint32_t height,
                                                       bool srgb = false)
{
    const uint32_t w = pc_texture_next_mip_size(width), h = pc_texture_next_mip_size(height);
    std::vector<uint8_t> result(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t y0 = uint64_t(y) * height / h, y1 = uint64_t(y + 1) * height / h;
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t x0 = uint64_t(x) * width / w, x1 = uint64_t(x + 1) * width / w;
            const uint32_t samples = (x1 - x0) * (y1 - y0);
            for (unsigned channel = 0; channel < 4; ++channel) {
                if (!srgb || channel == 3) {
                    uint32_t sum = 0;
                    for (uint32_t sy = y0; sy < y1; ++sy)
                        for (uint32_t sx = x0; sx < x1; ++sx)
                            sum += source[(size_t(sy) * width + sx) * 4 + channel];
                    result[(size_t(y) * w + x) * 4 + channel] = uint8_t((sum + samples / 2) / samples);
                    continue;
                }
                float sum = 0;
                for (uint32_t sy = y0; sy < y1; ++sy) {
                    for (uint32_t sx = x0; sx < x1; ++sx) {
                        float value = source[(size_t(sy) * width + sx) * 4 + channel] / 255.0f;
                        value = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
                        sum += value;
                    }
                }
                float value = sum / samples;
                value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
                result[(size_t(y) * w + x) * 4 + channel] = static_cast<uint8_t>(std::min(255.0f, value * 255 + 0.5f));
            }
        }
    }
    return result;
}
