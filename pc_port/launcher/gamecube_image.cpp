#include "gamecube_image.h"
#include "sha256.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <iconv.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

/**
 * @brief Turns a raw FST name into a path component.
 *
 * Names in a GameCube FST are raw bytes with no declared encoding. This disc
 * carries a few left over in Shift-JIS -- "コピー ~ practice" and friends. On
 * Linux those bytes become the filename unchanged, which is why extraction has
 * always worked there. On Windows fs::path converts a narrow string through the
 * active code page and throws filesystem_error on an illegal sequence, aborting
 * the install a few percent in. macOS/APFS requires filenames to be valid
 * UTF-8 outright -- open()/ofstream just fails on an invalid byte sequence,
 * no exception to catch -- so raw bytes can't pass through unchanged there
 * the way they do on Linux either.
 *
 * Decode explicitly instead: UTF-8 first, then Shift-JIS, and finally a
 * byte-preserving widening (Windows) or hex-escaping (macOS) that cannot
 * fail. The last step keeps a file with an unrecognisable name rather than
 * losing the extraction.
 */
#if defined(__APPLE__)
namespace {
/// True if `text` is well-formed UTF-8 (the common case: most FST names are
/// plain ASCII, which is valid UTF-8 unchanged).
bool isValidUtf8(const std::string& text)
{
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + len > text.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}

/// Shift-JIS -> UTF-8 through the platform's own conversion tables, the same
/// role CP_UTF8/932 via MultiByteToWideChar plays on Windows below. Returns
/// false (rather than a partial result) on anything iconv can't decode.
bool shiftJisToUtf8(const std::string& input, std::string& output)
{
    if (input.empty()) { output.clear(); return true; }
    const iconv_t cd = iconv_open("UTF-8", "SHIFT_JIS");
    if (cd == reinterpret_cast<iconv_t>(-1)) return false;
    std::string result(input.size() * 4 + 16, '\0');
    char* inPtr = const_cast<char*>(input.data());
    std::size_t inBytesLeft = input.size();
    char* outPtr = result.data();
    std::size_t outBytesLeft = result.size();
    const std::size_t rc = iconv(cd, &inPtr, &inBytesLeft, &outPtr, &outBytesLeft);
    iconv_close(cd);
    if (rc == static_cast<std::size_t>(-1) || inBytesLeft != 0) return false;
    result.resize(result.size() - outBytesLeft);
    output = std::move(result);
    return true;
}
} // namespace
#endif

fs::path discNameToPath(const std::string& name)
{
#if defined(_WIN32)
	if (name.empty()) return fs::path();

	const auto tryCodePage = [&name](unsigned codePage, bool strict) -> std::wstring {
		const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
		const int needed = MultiByteToWideChar(codePage, flags, name.data(),
		                                       static_cast<int>(name.size()), nullptr, 0);
		if (needed <= 0) return std::wstring();
		std::wstring wide(static_cast<std::size_t>(needed), L'\0');
		const int written = MultiByteToWideChar(codePage, flags, name.data(),
		                                        static_cast<int>(name.size()), wide.data(), needed);
		if (written <= 0) return std::wstring();
		wide.resize(static_cast<std::size_t>(written));
		return wide;
	};

	std::wstring wide = tryCodePage(CP_UTF8, true);
	if (wide.empty()) wide = tryCodePage(932, true);   // Shift-JIS
	if (wide.empty()) {
		// Never fails: each byte becomes one character.
		wide.reserve(name.size());
		for (unsigned char byte : name) wide.push_back(static_cast<wchar_t>(byte));
	}
	return fs::path(wide);
#elif defined(__APPLE__)
	if (name.empty() || isValidUtf8(name)) return fs::path(name);

	std::string converted;
	if (shiftJisToUtf8(name, converted) && isValidUtf8(converted)) {
		return fs::path(converted);
	}

	// Never fails: each invalid byte becomes a unique, filesystem-legal
	// escape instead of a file that APFS refuses to create.
	static const char kHexDigits[] = "0123456789ABCDEF";
	std::string safe;
	safe.reserve(name.size() * 3);
	for (unsigned char byte : name) {
		if (byte >= 0x20 && byte < 0x7F && byte != '/' && byte != '\\') {
			safe += static_cast<char>(byte);
		} else {
			safe += '_';
			safe += kHexDigits[(byte >> 4) & 0xF];
			safe += kHexDigits[byte & 0xF];
		}
	}
	return fs::path(safe);
#else
	return fs::path(name);
#endif
}

/// Path text for messages. Windows' narrow conversion throws on characters the
/// active code page cannot express, so go through UTF-8, which always can.
std::string pathText(const fs::path& path)
{
#if defined(_WIN32)
	// u8string() returns std::string under C++17 and std::u8string under C++20;
	// the copy below works either way.
	const auto utf8 = path.u8string();
	return std::string(utf8.begin(), utf8.end());
#else
	return path.string();
#endif
}

constexpr std::uint64_t kFstOffsetField = 0x424;
constexpr std::uint64_t kFstSizeField   = 0x428;
constexpr std::size_t kCopyBufferSize   = 1024 * 1024;

std::uint32_t readBe32(const std::uint8_t* bytes)
{
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
         | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}

bool readAt(std::istream& input, std::uint64_t offset, void* output, std::size_t size)
{
    if (offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) return false;
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return false;
    input.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

bool safeComponent(const std::string& name)
{
    if (name.empty() || name == "." || name == "..") return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) {
        return c < 0x20 || c == '/' || c == '\\' || c == ':';
    });
}

struct FstEntry {
    bool directory = false;
    std::uint32_t nameOffset = 0;
    std::uint32_t offsetOrParent = 0;
    std::uint32_t sizeOrNext = 0;
};

bool parseFst(std::istream& input, std::uint64_t imageSize, std::uint64_t& fstOffset,
              std::vector<FstEntry>& entries, std::vector<std::uint8_t>& fst,
              std::string& error)
{
    std::array<std::uint8_t, 8> header {};
    if (!readAt(input, kFstOffsetField, header.data(), header.size())) {
        error = "Could not read the disc's file table header.";
        return false;
    }
    fstOffset = readBe32(header.data());
    const std::uint64_t fstSize = readBe32(header.data() + 4);
    if (fstSize < 12 || fstOffset > imageSize || fstSize > imageSize - fstOffset
        || fstSize > 256ULL * 1024ULL * 1024ULL) {
        error = "The disc's file table is not valid.";
        return false;
    }
    fst.resize(static_cast<std::size_t>(fstSize));
    if (!readAt(input, fstOffset, fst.data(), fst.size())) {
        error = "Could not read the disc's file table.";
        return false;
    }
    const std::uint32_t rootWord = readBe32(fst.data());
    const std::uint32_t entryCount = readBe32(fst.data() + 8);
    if ((rootWord >> 24) != 1 || entryCount == 0
        || std::uint64_t(entryCount) * 12ULL > fst.size()) {
        error = "The root of the disc's file table is not valid.";
        return false;
    }
    entries.resize(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint8_t* raw = fst.data() + std::size_t(i) * 12;
        const std::uint32_t typeAndName = readBe32(raw);
        entries[i].directory = (typeAndName >> 24) != 0;
        entries[i].nameOffset = typeAndName & 0x00FFFFFF;
        entries[i].offsetOrParent = readBe32(raw + 4);
        entries[i].sizeOrNext = readBe32(raw + 8);
        if (entries[i].directory && (entries[i].sizeOrNext <= i
                                     || entries[i].sizeOrNext > entryCount)) {
            error = "The disc's file table contains an invalid directory.";
            return false;
        }
    }
    return true;
}

bool entryName(const std::vector<std::uint8_t>& fst, std::size_t stringTable,
               const FstEntry& entry, std::string& name)
{
    const std::size_t begin = stringTable + entry.nameOffset;
    if (begin >= fst.size()) return false;
    const auto first = fst.begin() + static_cast<std::ptrdiff_t>(begin);
    const auto end = std::find(first, fst.end(), std::uint8_t(0));
    if (end == fst.end()) return false;
    name.assign(first, end);
    return safeComponent(name);
}

} // namespace

bool inspectGameCubeImage(const fs::path& image, DiscIdentity& identity, std::string& error)
{
    std::ifstream input(image, std::ios::binary);
    if (!input) {
        error = "Could not open the disc image.";
        return false;
    }
    return inspectGameCubeImage(input, identity, error);
}

bool inspectGameCubeImage(std::istream& input, DiscIdentity& identity, std::string& error)
{
    std::array<std::uint8_t, 8> header {};
    if (!readAt(input, 0, header.data(), header.size())) {
        error = "That file is too small to be a GameCube disc.";
        return false;
    }
    identity.gameId.assign(reinterpret_cast<const char*>(header.data()), 6);
    identity.revision = header[7];
    return true;
}

namespace {

// Los discos que el port sabe usar.
//
// Cada uno necesita un ejecutable compilado para su version: el juego se
// compila desde la decompilacion, y esa decompilacion es condicional segun la
// version en 404 sitios. El disco solo aporta los assets.
constexpr const char* kLanguagesEnglishOnly[] = { "en" };
constexpr const char* kLanguagesPal[] = { "en", "de", "fr", "es", "it" };

constexpr KnownDisc kKnownDiscs[] = {
    { "GPIE01", 1, "Pikmin USA Rev. 1",
      "db013398ec77299e307ef61ec33e82b07e4b21cb676b18a0be712fe55e9775f2",
      kLanguagesEnglishOnly, 1, "nectar" },
    // El disco europeo trae los cinco idiomas en el mismo dataDir; el juego
    // elige en ejecucion. Verificado sobre un volcado real: 4874 archivos, y
    // screen/ con eng, fre, ger, ita y spa.
    { "GPIP01", 0, "Pikmin Europa (En, Fr, De, Es, It)",
      "7c50b65545d2158e56545f7e9cdf0f0c0dbeabbcb4e1f080d3b0433f2f91c343",
      kLanguagesPal, 5, "nectar-pal" },
};

} // namespace

const KnownDisc* findKnownDisc(const DiscIdentity& identity)
{
    for (const KnownDisc& disc : kKnownDiscs) {
        if (identity.gameId == disc.gameId && identity.revision == disc.revision) {
            return &disc;
        }
    }
    return nullptr;
}

bool isSupportedPikminDisc(const DiscIdentity& identity, std::string& error)
{
    if (findKnownDisc(identity) != nullptr) return true;

    std::string known;
    for (const KnownDisc& disc : kKnownDiscs) {
        known += std::string("\n  - ") + disc.description + " (" + disc.gameId
               + ", revision " + std::to_string(disc.revision) + ")";
    }
    error = "Unrecognised disc: " + identity.gameId + ", revision "
          + std::to_string(identity.revision) + ".\n\nSupported discs:" + known;
    return false;
}

namespace {

// Volcado de referencia de Pikmin USA Rev. 1 (GPIE01, revisión 1), disco
// completo de 1.459.978.240 bytes. Una imagen íntegra de ese juego produce
// siempre este hash; cualquier diferencia significa que la copia está dañada,
// recortada o modificada.
constexpr const char* kPikminUsaRev1Sha256 =
    "db013398ec77299e307ef61ec33e82b07e4b21cb676b18a0be712fe55e9775f2";

} // namespace

bool hashImage(const fs::path& image, std::string& hexDigest, std::string& error,
               const std::function<void(std::uint32_t)>& progress)
{
    std::ifstream input(image, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "Could not open the disc image.";
        return false;
    }
    const std::uint64_t imageSize = static_cast<std::uint64_t>(input.tellg());
    input.seekg(0, std::ios::beg);

    Sha256 hash;
    std::vector<char> buffer(kCopyBufferSize);
    std::uint64_t done = 0;
    std::uint32_t lastPercent = 101; // fuerza el primer aviso

    while (done < imageSize) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(imageSize - done, buffer.size()));
        input.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (input.gcount() != static_cast<std::streamsize>(chunk)) {
            error = "Could not read the whole image: it may be damaged, or the drive "
                    "it is on is returning read errors.";
            return false;
        }
        hash.update(buffer.data(), chunk);
        done += chunk;

        if (progress) {
            const std::uint32_t percent = imageSize == 0
                ? 100u : static_cast<std::uint32_t>(done * 100ULL / imageSize);
            if (percent != lastPercent) {
                progress(percent);
                lastPercent = percent;
            }
        }
    }

    hexDigest = toHex(hash.finish());
    return true;
}

bool verifyImageIntegrity(const fs::path& image, std::string& error,
                          const std::function<void(std::uint32_t)>& progress)
{
    DiscIdentity identity;
    std::string ignored;
    const KnownDisc* disc = inspectGameCubeImage(image, identity, ignored)
                              ? findKnownDisc(identity)
                              : nullptr;
    const char* expected = disc ? disc->sha256 : kPikminUsaRev1Sha256;

    std::string digest;
    if (!hashImage(image, digest, error, progress)) return false;
    if (digest == expected) return true;

    error = std::string("This image does not match an intact dump of ")
          + (disc ? disc->description : "Pikmin USA Rev. 1") + ".\n"
            "Expected: " + std::string(expected) + "\n"
            "Found:    " + digest + "\n\n"
            "The likeliest cause is a copy that was damaged in transit. Copy the "
            "file again from the original and check the hash before installing. "
            "If you are sure your dump is good and simply differs from the "
            "reference one, --skip-verify skips this check.";
    return false;
}

bool extractGameCubeImage(const fs::path& image, const fs::path& destination,
                          std::string& error, ProgressCallback progress,
                          bool verifyWrites)
{
    std::ifstream input(image, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "Could not open the disc image.";
        return false;
    }
    const std::streamoff endPos = input.tellg();
    if (endPos <= 0) {
        error = "The disc image is empty.";
        return false;
    }
    return extractGameCubeImage(input, static_cast<std::uint64_t>(endPos), destination, error,
                                std::move(progress), verifyWrites);
}

bool extractGameCubeImage(std::istream& input, std::uint64_t imageSize,
                          const fs::path& destination, std::string& error,
                          ProgressCallback progress, bool verifyWrites)
{
    if (imageSize == 0) {
        error = "The disc image is empty.";
        return false;
    }

    std::uint64_t fstOffset = 0;
    std::vector<FstEntry> entries;
    std::vector<std::uint8_t> fst;
    if (!parseFst(input, imageSize, fstOffset, entries, fst, error)) return false;
    const std::size_t stringTable = entries.size() * 12;

    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        error = "Could not create the game data folder: " + ec.message();
        return false;
    }

    struct DirectoryFrame { std::uint32_t nextIndex; fs::path path; };
    std::vector<DirectoryFrame> stack;
    stack.push_back({ entries[0].sizeOrNext, destination });
    std::vector<char> buffer(kCopyBufferSize);

    for (std::uint32_t i = 1; i < entries.size(); ++i) {
        while (stack.size() > 1 && i >= stack.back().nextIndex) stack.pop_back();
        std::string name;
        if (!entryName(fst, stringTable, entries[i], name)) {
            error = "The disc's file table contains an unsafe or invalid file name.";
            return false;
        }
        const fs::path outputPath = stack.back().path / discNameToPath(name);
        if (progress) progress(i, static_cast<std::uint32_t>(entries.size() - 1),
                               pathText(outputPath.lexically_relative(destination)));

        if (entries[i].directory) {
            fs::create_directories(outputPath, ec);
            if (ec) {
                error = "No se pudo crear " + pathText(outputPath) + ": " + ec.message();
                return false;
            }
            stack.push_back({ entries[i].sizeOrNext, outputPath });
            continue;
        }

        const std::uint64_t fileOffset = entries[i].offsetOrParent;
        const std::uint64_t fileSize = entries[i].sizeOrNext;
        if (fileOffset > imageSize || fileSize > imageSize - fileOffset) {
            error = "The file table points outside the image: " + name;
            return false;
        }
        fs::create_directories(outputPath.parent_path(), ec);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not write " + pathText(outputPath) + ".";
            return false;
        }
        input.clear();
        input.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);
        std::uint64_t remaining = fileSize;
        Sha256 sourceHash;
        while (remaining != 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(buffer.data(), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                error = "Lectura incompleta al extraer " + name + ".";
                return false;
            }
            sourceHash.update(buffer.data(), chunk);
            output.write(buffer.data(), static_cast<std::streamsize>(chunk));
            if (!output) {
                error = "Escritura incompleta al extraer " + name + ".";
                return false;
            }
            remaining -= chunk;
        }

        // Cerrar de forma explícita: el destructor no informa de los errores de
        // vaciado, y un disco lleno se manifiesta justo aquí.
        output.close();
        if (!output) {
            error = "Could not finish writing " + name
                  + ": comprueba el espacio libre en el destino.";
            return false;
        }

        // Releer lo escrito y comparar con lo que salió de la imagen. Detecta
        // daños del medio de destino, que producen archivos del tamaño correcto
        // con contenido incorrecto y hacen fallar al juego mucho más tarde.
        if (verifyWrites) {
            std::ifstream written(outputPath, std::ios::binary);
            if (!written) {
                error = "No se pudo releer " + name + " para verificarlo.";
                return false;
            }
            Sha256 writtenHash;
            std::uint64_t verified = 0;
            while (verified < fileSize) {
                const std::size_t chunk = static_cast<std::size_t>(
                    std::min<std::uint64_t>(fileSize - verified, buffer.size()));
                written.read(buffer.data(), static_cast<std::streamsize>(chunk));
                if (written.gcount() != static_cast<std::streamsize>(chunk)) {
                    error = "No se pudo releer " + name + " completo para verificarlo.";
                    return false;
                }
                writtenHash.update(buffer.data(), chunk);
                verified += chunk;
            }
            if (writtenHash.finish() != sourceHash.finish()) {
                error = name + " was written incorrectly.\n\n"
                        "What was read back from the destination does not match "
                        "the image. That usually means a problem with the "
                        "storage -- a failing USB stick, a drive with errors -- "
                        "or that it ran out of room. Try installing to another "
                        "drive.";
                return false;
            }
        }
    }
    return true;
}

} // namespace launcher
} // namespace pikmin
