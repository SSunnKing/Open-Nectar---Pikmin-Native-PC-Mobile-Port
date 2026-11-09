#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace pikmin::launcher {
// The complete ZIP stays on disk while ZipSource extracts to .install-<id>.
// A 64 MiB stored-ZIP fixture measured ~2.0003x peak in the real CLI.
// Budget archive + equally sized extraction, plus 10% headroom.
// Catalog metadata is compressed bytes only: highly compressed packs can need
// more. This estimate is not an exact upper bound on uncompressed ZIP size.
inline constexpr double kTexturePackExtractionHeadroom = 1.1;
inline constexpr double kTexturePackHardFactor = 2.2;
// A 2x warning would sit below the 2.2x hard minimum and be unreachable. Keep
// an actionable warning band above the hard minimum instead.
inline constexpr double kTexturePackSoftFactor = 3.0;
enum class TexturePackStorage { Unknown, Enough, Confirm, Block };
inline std::uint64_t texturePackStorageBytes(std::uint64_t bytes, double factor) {
    const long double result = std::ceil(static_cast<long double>(bytes) * std::round(factor * 10.0) / 10.0L);
    return result >= std::numeric_limits<std::uint64_t>::max()
        ? std::numeric_limits<std::uint64_t>::max() : std::uint64_t(result);
}
inline TexturePackStorage texturePackStorageCheck(std::optional<std::uint64_t> packBytes,
                                                 std::optional<std::uint64_t> freeBytes) {
    if (!packBytes || !*packBytes || !freeBytes) return TexturePackStorage::Unknown;
    if (*freeBytes < texturePackStorageBytes(*packBytes, kTexturePackHardFactor)) return TexturePackStorage::Block;
    if (*freeBytes < texturePackStorageBytes(*packBytes, kTexturePackSoftFactor)) return TexturePackStorage::Confirm;
    return TexturePackStorage::Enough;
}
}
