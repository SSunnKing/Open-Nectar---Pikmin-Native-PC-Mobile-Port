#include "gamecube_image.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {
void putBe32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
{
    data[offset] = value >> 24; data[offset + 1] = value >> 16;
    data[offset + 2] = value >> 8; data[offset + 3] = value;
}
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "pikmin-gc-image-test";
    const fs::path image = root / "test.iso";
    const fs::path output = root / "out";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    constexpr std::uint32_t fstOffset = 0x500;
    constexpr std::uint32_t fileOffset = 0x800;
    const std::string strings = std::string("dataDir\0parms\0gamePrms.bin\0", 27);
    const std::uint32_t fstSize = 4 * 12 + strings.size();
    std::vector<std::uint8_t> disc(0x900, 0);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("GPIE01"), 6, disc.begin());
    disc[7] = 1;
    putBe32(disc, 0x424, fstOffset); putBe32(disc, 0x428, fstSize);
    putBe32(disc, fstOffset + 0, 0x01000000); putBe32(disc, fstOffset + 8, 4);
    putBe32(disc, fstOffset + 12, 0x01000000); putBe32(disc, fstOffset + 16, 0); putBe32(disc, fstOffset + 20, 4);
    putBe32(disc, fstOffset + 24, 0x01000008); putBe32(disc, fstOffset + 28, 1); putBe32(disc, fstOffset + 32, 4);
    putBe32(disc, fstOffset + 36, 0x0000000E); putBe32(disc, fstOffset + 40, fileOffset); putBe32(disc, fstOffset + 44, 4);
    std::copy(strings.begin(), strings.end(), disc.begin() + fstOffset + 48);
    disc[fileOffset] = 1; disc[fileOffset + 1] = 2; disc[fileOffset + 2] = 3; disc[fileOffset + 3] = 4;
    std::ofstream imageOutput(image, std::ios::binary);
    imageOutput.write(reinterpret_cast<const char*>(disc.data()), disc.size());
    imageOutput.close();

    pikmin::launcher::DiscIdentity identity;
    std::string error;
    int failures = 0;
    if (!pikmin::launcher::inspectGameCubeImage(image, identity, error)
        || identity.gameId != "GPIE01" || identity.revision != 1) ++failures;
    if (!pikmin::launcher::isSupportedPikminDisc(identity, error)) ++failures;
    pikmin::launcher::DiscIdentity unsupported = identity;
    unsupported.revision = 0;
    if (pikmin::launcher::isSupportedPikminDisc(unsupported, error)) ++failures;
    if (!pikmin::launcher::extractGameCubeImage(image, output, error)) {
        std::cerr << error << '\n'; ++failures;
    }
    std::array<unsigned char, 4> bytes {};
    std::ifstream extracted(output / "dataDir/parms/gamePrms.bin", std::ios::binary);
    extracted.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (bytes != std::array<unsigned char, 4> { 1, 2, 3, 4 }) ++failures;

    // A hostile FST name must not be able to escape the chosen destination.
    std::copy_n(reinterpret_cast<const std::uint8_t*>("../evil"), 7,
                disc.begin() + fstOffset + 48);
    imageOutput.open(image, std::ios::binary | std::ios::trunc);
    imageOutput.write(reinterpret_cast<const char*>(disc.data()), disc.size());
    imageOutput.close();
    if (pikmin::launcher::extractGameCubeImage(image, root / "unsafe", error)) ++failures;
    if (fs::exists(root / "evil")) ++failures;

    fs::remove_all(root, ec);
    std::cout << "gamecube_image_test: failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
