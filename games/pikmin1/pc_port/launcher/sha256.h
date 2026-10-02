#pragma once

// SHA-256 (FIPS 180-4) autocontenido.
//
// El launcher lo usa para dos comprobaciones de integridad: la imagen de disco
// antes de extraer, y cada archivo escrito después de extraerlo. Se implementa
// aquí en vez de depender de OpenSSL porque el port se distribuye como paquete
// autocontenido y debe compilar igual con MinGW para Windows.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pikmin {
namespace launcher {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void update(const void* data, std::size_t size);
    Sha256Digest finish();

private:
    void compress(const std::uint8_t block[64]);

    std::uint32_t mState[8];
    std::uint64_t mBitCount;
    std::uint8_t mBuffer[64];
    std::size_t mBufferSize;
};

// Representación hexadecimal en minúsculas, como la que imprime sha256sum.
std::string toHex(const Sha256Digest& digest);

} // namespace launcher
} // namespace pikmin
