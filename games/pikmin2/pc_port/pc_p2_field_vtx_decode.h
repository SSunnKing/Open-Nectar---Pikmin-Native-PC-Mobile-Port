#pragma once
#include <cstddef>
#include <cstdint>

namespace p2_field_vtx {
inline std::uint16_t readBE16(const std::uint8_t* bytes) {
    return (std::uint16_t(bytes[0]) << 8) | bytes[1];
}
// GX display lists retain GameCube byte order on native hosts. Read bytes
// without alignment assumptions and validate the whole primitive first.
template <class Visit>
bool decode(const std::uint8_t* data, std::size_t size, std::size_t stride,
            int positionOffset, int colorOffset, std::size_t positions,
            std::size_t colors, Visit visit) {
    if (!data || !stride || positionOffset < 0 || colorOffset < 0
        || std::size_t(positionOffset) + 2 > stride || std::size_t(colorOffset) + 2 > stride) return false;
    std::size_t at = 0;
    while (at < size) {
        if (data[at] == 0) return true; // GX_NOP padding
        const auto primitive = data[at] & 0xf8;
        if (primitive != 0x98 && primitive != 0xa0) return false;
        if (size - at < 3) return false;
        const std::size_t count = readBE16(data + at + 1);
        at += 3;
        if (count < 3 || count > (size - at) / stride) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto* v = data + at + i * stride;
            if (readBE16(v + positionOffset) >= positions || readBE16(v + colorOffset) >= colors) return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto* v = data + at + i * stride;
            visit(readBE16(v + positionOffset), readBE16(v + colorOffset));
        }
        at += count * stride;
    }
    return true;
}
}
