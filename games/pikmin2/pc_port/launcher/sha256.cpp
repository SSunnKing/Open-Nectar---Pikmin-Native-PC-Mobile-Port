#include "sha256.h"

#include <cstring>

namespace pikmin {
namespace launcher {

namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline std::uint32_t rotr(std::uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

void Sha256::reset()
{
    mState[0] = 0x6a09e667u; mState[1] = 0xbb67ae85u;
    mState[2] = 0x3c6ef372u; mState[3] = 0xa54ff53au;
    mState[4] = 0x510e527fu; mState[5] = 0x9b05688cu;
    mState[6] = 0x1f83d9abu; mState[7] = 0x5be0cd19u;
    mBitCount = 0;
    mBufferSize = 0;
}

void Sha256::compress(const std::uint8_t block[64])
{
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16)
             | (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
    std::uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    mState[0] += a; mState[1] += b; mState[2] += c; mState[3] += d;
    mState[4] += e; mState[5] += f; mState[6] += g; mState[7] += h;
}

void Sha256::update(const void* data, std::size_t size)
{
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    mBitCount += std::uint64_t(size) * 8;

    if (mBufferSize != 0) {
        const std::size_t needed = 64 - mBufferSize;
        const std::size_t take = size < needed ? size : needed;
        std::memcpy(mBuffer + mBufferSize, bytes, take);
        mBufferSize += take;
        bytes += take;
        size -= take;
        if (mBufferSize < 64) return;
        compress(mBuffer);
        mBufferSize = 0;
    }

    while (size >= 64) {
        compress(bytes);
        bytes += 64;
        size -= 64;
    }

    if (size != 0) {
        std::memcpy(mBuffer, bytes, size);
        mBufferSize = size;
    }
}

Sha256Digest Sha256::finish()
{
    const std::uint64_t bitCount = mBitCount;

    // Relleno: un bit a 1, ceros hasta dejar 8 bytes libres, y la longitud del
    // mensaje en bits como big-endian de 64. Se manipula el búfer directamente
    // porque update() contaría el relleno como parte del mensaje.
    mBuffer[mBufferSize++] = 0x80;
    if (mBufferSize > 56) {
        while (mBufferSize < 64) mBuffer[mBufferSize++] = 0x00;
        compress(mBuffer);
        mBufferSize = 0;
    }
    while (mBufferSize < 56) mBuffer[mBufferSize++] = 0x00;
    for (int i = 0; i < 8; ++i) {
        mBuffer[56 + i] = static_cast<std::uint8_t>((bitCount >> (56 - i * 8)) & 0xFF);
    }
    compress(mBuffer);
    mBufferSize = 0;

    Sha256Digest digest {};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4]     = static_cast<std::uint8_t>((mState[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<std::uint8_t>((mState[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<std::uint8_t>((mState[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(mState[i] & 0xFF);
    }
    return digest;
}

std::string toHex(const Sha256Digest& digest)
{
    static const char* const kDigits = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (std::uint8_t byte : digest) {
        result.push_back(kDigits[byte >> 4]);
        result.push_back(kDigits[byte & 0x0F]);
    }
    return result;
}

} // namespace launcher
} // namespace pikmin
