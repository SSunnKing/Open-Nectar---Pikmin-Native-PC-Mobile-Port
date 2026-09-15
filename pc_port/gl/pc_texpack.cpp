#include "pc_texpack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// PNG packs (la edición "Android & iOS" de Henriko es PNG). stb_image se
// compila aquí, una sola vez, y sale a RGBA8: sin soporte de compresión
// móvil, pero GL_RGBA8 existe en cualquier GLES3 (plan: fase 3 usará ASTC).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Descodificador BC1/BC2/BC3/BC7 en CPU (bcdec, MIT / Unlicense). Es el
// respaldo para GPUs sin S3TC ni BPTC: casi todas las de Android (Adreno,
// Mali, PowerVR). Sin él, un pack DDS en el móvil no cambiaba ni una textura
// y el menú seguía marcándolo como activo.
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace fs = std::filesystem;

// En GLES / drivers antiguos faltan las constantes BPTC y S3TC sRGB; los
// valores son los mismos que en GL 4.2 / las extensiones correspondientes.
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

// GameIDs de Pikmin 1: el launcher conoce la región; aquí se admite el id de 6
// letras, el de 3 (el prefijo de región libre que Dolphin acepta, incluyendo
// el "GPI" a secas que usan los packs de la calle) y "all" (comodín), sin
// distinguir mayúsculas/minúsculas.
static const char* const kGameIdCandidates[] = {
    "GPIE01", "GPIP01", "GPIJ01", "GPIE", "GPIP", "GPIJ", "GPI", "all",
};

static bool sRequested = false;
static bool sEnabled = false;
static bool sBptcAvailable = false;
static bool sS3tcAvailable = false;
static bool sS3tcSrgbAvailable = false;
static GLint sMaxTextureSize = 8192;
static size_t sIndexedDds = 0;
static size_t sIndexedPng = 0;
static char sStatus[160] = "off";
static size_t sStatReplaced = 0, sStatMissing = 0, sStatFailed = 0;
// Pack elegido desde el menú F1 (fase 2): nombre de carpeta bajo
// Load/Textures/ con el que se restringe el índice al arrancar. Vacío = toda
// carpeta de GameID (comportamiento del `--texture-pack` de la fase 1).
static std::string sSelectedPack;

static std::unordered_map<std::string, std::vector<fs::path>> sIndex;
static std::unordered_set<std::string> sLoggedFailures;

static void log_once(const std::string& name, const char* why)
{
    if (sLoggedFailures.insert(name).second)
        printf("[PC TexPack] %s: %s\n", name.c_str(), why);
}

static bool has_gl_extension(const char* name)
{
    const char* all = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!all) return false;
    const char* found = strstr(all, name);
    if (!found) return false;
    const char* end = found + strlen(name);
    return (*end == ' ' || *end == '\0');
}

// Indice del pack: un fichero por nombre tex1_*; el sufijo _mipN marca el
// nivel. Dolphin retira "_arb" (mipmaps arbitrarios) antes de indexar.
static std::string lower_ext(const fs::path& path)
{
    std::string ext = path.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

static void index_texture_file(const fs::path& path)
{
    std::string stem = path.stem().string();
    std::string base = stem;
    size_t mip = base.rfind("_mip");
    if (mip != std::string::npos && mip + 4 < base.size()) {
        bool digits = true;
        for (size_t i = mip + 4; i < base.size() && digits; ++i)
            if (base[i] < '0' || base[i] > '9') digits = false;
        if (digits) base.erase(mip);
    }
size_t arb = base.rfind("_arb");
    if (arb != std::string::npos) base.erase(arb, 4);
    if (base.compare(0, 5, "tex1_") != 0) return;
    sIndex[base].push_back(path);
    if (lower_ext(path) == ".dds") ++sIndexedDds; else ++sIndexedPng;
}

// Nivel de un fichero _mipN (1..), o 0 para el fichero base.
static int mip_level_of(const std::string& stem)
{
    size_t mip = stem.rfind("_mip");
    if (mip == std::string::npos || mip + 4 >= stem.size()) return 0;
    char* end = nullptr;
    const long parsed = strtol(stem.c_str() + mip + 4, &end, 10);
    if (!end || *end != '\0' || parsed < 1) return 0;
    for (size_t i = mip + 4; i < stem.size(); ++i)
        if (stem[i] < '0' || stem[i] > '9') return 0;
    return static_cast<int>(parsed);
}

void pc_texpack_request_enable(void)
{
    sRequested = true;
}

void pc_texpack_request_disable(void)
{
    sRequested = false;
    sEnabled = false;
}

bool pc_texpack_enabled(void)
{
    return sEnabled;
}

void pc_texpack_select_pack(const char* folder)
{
    sSelectedPack = folder ? folder : "";
}

const std::string& pc_texpack_selected_pack(void)
{
    return sSelectedPack;
}

std::vector<std::string> pc_texpack_list_packs(void)
{
    std::vector<std::string> packs;
    std::error_code ec;
    const fs::path root = fs::path("Load") / "Textures";
    if (!fs::is_directory(root, ec)) return packs;
    fs::directory_iterator dit(root, ec);
    for (; dit != fs::directory_iterator(); dit.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!dit->is_directory()) continue;
        std::string name = dit->path().filename().string();
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* cand : kGameIdCandidates) {
            std::string candidate = cand;
            for (char& c : candidate) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == candidate) { packs.push_back(name); break; }
        }
    }
    return packs;
}

const char* pc_texpack_status(void)
{
    return sStatus;
}

void pc_texpack_stats(size_t* replaced, size_t* missing, size_t* failed)
{
    if (replaced) *replaced = sStatReplaced;
    if (missing) *missing = sStatMissing;
    if (failed) *failed = sStatFailed;
}

void pc_texpack_init(void)
{
    sEnabled = false;
    if (!sRequested) {
        printf("[PC TexPack] off\n");
        snprintf(sStatus, sizeof(sStatus), "off");
        return;
    }
    // La misma capacidad tiene dos nombres: ARB en escritorio, EXT en GLES
    // (donde de todas formas casi nunca está). S3TC es EXT en ambos; NVIDIA
    // en Android lo expone con su propio prefijo.
    sBptcAvailable = has_gl_extension("GL_ARB_texture_compression_bptc")
        || has_gl_extension("GL_EXT_texture_compression_bptc");
    sS3tcAvailable = has_gl_extension("GL_EXT_texture_compression_s3tc")
        || has_gl_extension("GL_NV_texture_compression_s3tc");
    sS3tcSrgbAvailable = sS3tcAvailable
        && (has_gl_extension("GL_EXT_texture_sRGB")
            || has_gl_extension("GL_EXT_texture_compression_s3tc_srgb"));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &sMaxTextureSize);
    printf("[PC TexPack] driver: BPTC %s, S3TC %s, max texture %d\n",
           sBptcAvailable ? "yes" : "no (CPU decode)", sS3tcAvailable ? "yes" : "no (CPU decode)",
           int(sMaxTextureSize));

    std::error_code ec;
    fs::path root = fs::path("Load") / "Textures";
    if (!fs::is_directory(root, ec)) {
        printf("[PC TexPack] Load/Textures not found under %s\n",
               fs::current_path().c_str());
        snprintf(sStatus, sizeof(sStatus), "Load/Textures not found");
        return;
    }

    // Cada carpeta de primer nivel es un candidato de GameID. Con un pack
    // elegido en el menú F1 (sSelectedPack), solo ese se indexa: activar un
    // pack desactiva cualquier otro, y así la opción significa lo que dice.
    std::string selectedLower = sSelectedPack;
    for (char& c : selectedLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<fs::path> packDirs;
    fs::directory_iterator dit(root, ec);
    for (; dit != fs::directory_iterator(); dit.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!dit->is_directory()) continue;
        std::string name = dit->path().filename().string();
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool matchesSelected = selectedLower.empty() || lower == selectedLower;
        for (const char* cand : kGameIdCandidates) {
            std::string candidate = cand;
            for (char& c : candidate) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower != candidate || !matchesSelected) continue;
            packDirs.push_back(dit->path());
            break;
        }
    }
    if (packDirs.empty()) {
        printf("[PC TexPack] no GPIE01/GPI*/all folder under %s\n", root.string().c_str());
        snprintf(sStatus, sizeof(sStatus), "pack folder %s not found",
                 sSelectedPack.empty() ? "GPI*" : sSelectedPack.c_str());
        return;
    }

    size_t folders = 0;
    for (const fs::path& dir : packDirs) {
        fs::recursive_directory_iterator rit(dir, fs::directory_options::skip_permission_denied, ec);
        for (; rit != fs::recursive_directory_iterator(); rit.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!rit->is_regular_file()) continue;
            const std::string ext = lower_ext(rit->path());
            if (ext == ".dds" || ext == ".png")
                index_texture_file(rit->path());
        }
        ++folders;
    }
    sEnabled = !sIndex.empty();
    {
        char packTag[96] = {};
        if (!sSelectedPack.empty()) snprintf(packTag, sizeof(packTag), ", pack %s", sSelectedPack.c_str());
        printf("[PC TexPack] %zu folder(s)%s, %zu base names indexed\n", folders, packTag, sIndex.size());
    }
    if (!sIndex.empty() && !sSelectedPack.empty())
        printf("[PC TexPack] active pack: %s\n", sSelectedPack.c_str());
    if (!sBptcAvailable && !sS3tcAvailable)
        printf("[PC TexPack] WARNING: no BC7/S3TC on this GPU; compressed DDS will be decoded on the CPU (RGBA8, 4x the VRAM)\n");
    if (sEnabled && sIndex.size() < 5)
        printf("[PC TexPack] WARNING: only %zu textures indexed; is this a complete pack?\n",
               sIndex.size());
    if (!sEnabled) {
        snprintf(sStatus, sizeof(sStatus), "no tex1_* files in %s",
                 sSelectedPack.empty() ? "Load/Textures" : sSelectedPack.c_str());
    } else {
        const char* ddsHow = sIndexedDds == 0 ? "" : (sBptcAvailable && sS3tcAvailable)
            ? ", DDS on GPU" : ", DDS decoded on CPU (no BC7 on GPU)";
        snprintf(sStatus, sizeof(sStatus), "%zu textures indexed (%zu dds, %zu png)%s",
                 sIndex.size(), sIndexedDds, sIndexedPng, ddsHow);
    }
}

// ── DDS ──────────────────────────────────────────────────────────────────────

struct DdsFile {
    GLenum internalFormat = GL_RGBA8;
    bool bgra = false;         // B8G8R8A8 sin comprimir: se reordena en CPU
    bool compressed = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::vector<uint8_t>> levels;  // un búfer por nivel
};

static uint32_t read_le32(const std::vector<uint8_t>& b, size_t off)
{
    return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) | (uint32_t(b[off + 2]) << 16)
        | (uint32_t(b[off + 3]) << 24);
}

static bool load_dds(const fs::path& path, DdsFile& out)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 128 || memcmp(b.data(), "DDS ", 4) != 0) return false;

    const uint32_t height = read_le32(b, 12);
    const uint32_t width = read_le32(b, 16);
    const uint32_t mipCount = read_le32(b, 28);
    const uint32_t fourCC = read_le32(b, 84);

    GLenum internal = GL_RGBA8;
    bool bgra = false;
    bool compressed = false;
    uint32_t blockBytes = 0;
    size_t dataStart = 128;
    if (fourCC == 0x30315844) {  // "DX10" + DXGI_FORMAT
        if (b.size() < 148) return false;
        const uint32_t dxgi = read_le32(b, 128);
        dataStart = 148;
        switch (dxgi) {
            case 98:  internal = GL_COMPRESSED_RGBA_BPTC_UNORM; compressed = true; blockBytes = 16; break;
            case 99:  internal = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM; compressed = true; blockBytes = 16; break;
            case 71:  internal = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; compressed = true; blockBytes = 8; break;
            case 72:  internal = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT; compressed = true; blockBytes = 8; break;
            case 74:  internal = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; compressed = true; blockBytes = 16; break;
            case 75:  internal = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT; compressed = true; blockBytes = 16; break;
            case 77:  internal = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; compressed = true; blockBytes = 16; break;
            case 78:  internal = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT; compressed = true; blockBytes = 16; break;
            case 28:  break;                                // R8G8B8A8_UNORM
            case 87:  bgra = true; break;                   // B8G8R8A8_UNORM
            default:  return false;
        }
    } else if (fourCC == 0x31545844) {  // DXT1
        internal = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; compressed = true; blockBytes = 8;
    } else if (fourCC == 0x33545844) {  // DXT3
        internal = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; compressed = true; blockBytes = 16;
    } else if (fourCC == 0x35545844) {  // DXT5
        internal = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; compressed = true; blockBytes = 16;
    } else {
        return false;  // nada más se acepta en la fase 1; el original gana
    }

    out.width = width;
    out.height = height;
    out.internalFormat = internal;
    out.bgra = bgra;
    out.compressed = compressed;
    out.levels.clear();

    const uint32_t mips = mipCount ? mipCount : 1;
    size_t offset = dataStart;
    uint32_t w = width, h = height;
    for (uint32_t i = 0; i < mips; ++i) {
        const size_t levelBytes = compressed
            ? (size_t((w + 3) / 4) * size_t((h + 3) / 4) * blockBytes)
            : size_t(w) * size_t(h) * 4;
        if (offset + levelBytes > b.size()) return false;
        out.levels.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(offset),
                                b.begin() + static_cast<std::ptrdiff_t>(offset + levelBytes));
        offset += levelBytes;
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
    }
    return true;
}

// ── Subida ────────────────────────────────────────────────────────────────────

struct PackLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
};

struct PackImage {
    GLenum internalFormat = GL_RGBA8;
    GLenum sourceFormat = GL_RGBA;
    bool compressed = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<PackLevel> levels;
};

// Carga un fichero base o _mipN: DDS (tal cual, comprimido o RGBA8) o PNG
// (decodificado a RGBA8 con stb_image). El nivel 0 de un PNG no trae cadena:
// los _mipN del pack la completan, igual que con los DDS de un solo nivel.
static bool load_pack_image(const fs::path& path, PackImage& out)
{
    if (path.extension() == ".png") {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (file.empty()) return false;
        int w = 0, h = 0, channels = 0;
        unsigned char* px = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
                                                  &w, &h, &channels, 4);
        if (!px || w <= 0 || h <= 0) return false;
        PackLevel base;
        base.width = static_cast<uint32_t>(w);
        base.height = static_cast<uint32_t>(h);
        base.data.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        stbi_image_free(px);
        out = PackImage{};
        out.internalFormat = GL_RGBA8;
        out.sourceFormat = GL_RGBA;
        out.compressed = false;
        out.width = base.width;
        out.height = base.height;
        out.levels.emplace_back(std::move(base));
        return true;
    }

    DdsFile dds;
    if (!load_dds(path, dds) || dds.levels.empty()) return false;
    out = PackImage{};
    out.internalFormat = dds.internalFormat;
    out.sourceFormat = GL_RGBA;
    out.compressed = dds.compressed;
    out.width = dds.width;
    out.height = dds.height;
    uint32_t w = dds.width;
    uint32_t h = dds.height;
    for (std::vector<uint8_t>& data : dds.levels) {
        // GL_BGRA como formato de origen no existe en GLES (solo como
        // GL_BGRA_EXT con internalFormat BGRA); reordenar aquí vale en todos.
        if (dds.bgra)
            for (size_t i = 0; i + 3 < data.size(); i += 4) std::swap(data[i], data[i + 2]);
        PackLevel level;
        level.width = w;
        level.height = h;
        level.data = std::move(data);
        out.levels.emplace_back(std::move(level));
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
    }
    return true;
}

static bool capabilities_ok(const PackImage& image)
{
    if (!image.compressed) return true;
    switch (image.internalFormat) {
        case GL_COMPRESSED_RGBA_BPTC_UNORM:
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
            return sBptcAvailable;
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            return sS3tcSrgbAvailable;
        default:
            return sS3tcAvailable;
    }
}

// Descodifica un nivel comprimido a RGBA8 con bcdec. Los bloques del borde
// (anchura o altura no múltiplo de 4, habitual en los _mipN pequeños) se
// descodifican aparte y se recortan.
static bool decode_bc_level(GLenum internalFormat, uint32_t w, uint32_t h,
                            const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    void (*decode)(const void*, void*, int) = nullptr;
    size_t blockBytes = 16;
    switch (internalFormat) {
        case GL_COMPRESSED_RGBA_BPTC_UNORM:
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
            decode = bcdec_bc7; break;
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            decode = bcdec_bc1; blockBytes = 8; break;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
            decode = bcdec_bc2; break;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            decode = bcdec_bc3; break;
        default:
            return false;
    }
    if (w == 0 || h == 0) return false;
    const uint32_t blocksW = (w + 3) / 4, blocksH = (h + 3) / 4;
    if (in.size() < size_t(blocksW) * blocksH * blockBytes) return false;
    out.assign(size_t(w) * h * 4, 0);
    const int pitch = static_cast<int>(w * 4);
    const uint8_t* src = in.data();
    uint8_t tmp[4 * 4 * 4];
    for (uint32_t by = 0; by < blocksH; ++by) {
        for (uint32_t bx = 0; bx < blocksW; ++bx, src += blockBytes) {
            const uint32_t x0 = bx * 4, y0 = by * 4;
            if (x0 + 4 <= w && y0 + 4 <= h) {
                decode(src, out.data() + (size_t(y0) * w + x0) * 4, pitch);
                continue;
            }
            decode(src, tmp, 16);
            const uint32_t cw = std::min<uint32_t>(4, w - x0), ch = std::min<uint32_t>(4, h - y0);
            for (uint32_t y = 0; y < ch; ++y)
                memcpy(out.data() + (size_t(y0 + y) * w + x0) * 4, tmp + y * 16, cw * 4);
        }
    }
    return true;
}

// Deja la imagen en un formato que este driver acepte: tal cual si tiene la
// extensión, o descodificada a RGBA8 si no. Devuelve false si no hay manera.
static bool make_uploadable(PackImage& image)
{
    if (capabilities_ok(image)) return true;
    for (PackLevel& level : image.levels) {
        std::vector<uint8_t> rgba;
        if (!decode_bc_level(image.internalFormat, level.width, level.height, level.data, rgba))
            return false;
        level.data = std::move(rgba);
    }
    image.compressed = false;
    image.internalFormat = GL_RGBA8;
    image.sourceFormat = GL_RGBA;
    return true;
}

static void upload_level(const PackImage& image, int level, uint32_t w, uint32_t h,
                         const std::vector<uint8_t>& data)
{
    if (image.compressed)
        glCompressedTexImage2D(GL_TEXTURE_2D, level, image.internalFormat, w, h, 0,
                               static_cast<GLsizei>(data.size()), data.data());
    else
        glTexImage2D(GL_TEXTURE_2D, level, image.internalFormat, w, h, 0,
                     image.sourceFormat, GL_UNSIGNED_BYTE, data.data());
}

bool pc_texpack_try_upload(const char* name, GLuint texId, int* glLevels, size_t* gpuBytes)
{
    if (glLevels) *glLevels = 0;
    if (gpuBytes) *gpuBytes = 0;
    if (!sEnabled || !name) return false;

    auto it = sIndex.find(name);
    if (it == sIndex.end()) { ++sStatMissing; return false; }

    // Nivel 0: preferir el DDS; el PNG se usa si no hay DDS (pack "Android &
    // iOS" de Henriko) o si el DDS no lo soporta el driver.
    const fs::path* basePath = nullptr;
    for (const fs::path& p : it->second) {
        if (mip_level_of(p.stem().string()) != 0) continue;
        if (p.extension() == ".dds" && !basePath) basePath = &p;
    }
    if (!basePath) {
        for (const fs::path& p : it->second) {
            if (mip_level_of(p.stem().string()) != 0) continue;
            if (p.extension() == ".png") { basePath = &p; break; }
        }
    }
    if (!basePath) {
        log_once(name, "no base .dds/.png");
        ++sStatFailed;
        return false;
    }

    PackImage base;
    if (!load_pack_image(*basePath, base) || base.levels.empty()) {
        log_once(name, "unreadable base image");
        ++sStatFailed;
        return false;
    }
    if (base.width > uint32_t(sMaxTextureSize) || base.height > uint32_t(sMaxTextureSize)) {
        log_once(name, "larger than GL_MAX_TEXTURE_SIZE");
        ++sStatFailed;
        return false;
    }
    // DDS comprimido que el driver no puede subir: se descodifica en CPU
    // (bcdec). Solo si ni eso vale cae al camino de siempre.
    if (!make_uploadable(base)) {
        log_once(name, "compressed format not supported by driver nor decoder");
        ++sStatFailed;
        return false;
    }

    // Niveles: el base y, si el pack trae ficheros _mipN consecutivos, estos
    // reemplazan a cualquier nivel interno del base (pack de Henriko: un
    // fichero por nivel; permiten un PNG de un solo nivel como base). El resto
    // de formatos heredan la cadena interna.
    std::vector<std::pair<int, fs::path>> mipFiles;
    for (const fs::path& p : it->second) {
        const int lev = mip_level_of(p.stem().string());
        if (lev > 0 && (p.extension() == ".dds" || p.extension() == ".png"))
            mipFiles.emplace_back(lev, p);
    }
    std::sort(mipFiles.begin(), mipFiles.end());

    int levels = 0;
    size_t bytes = 0;
    const bool useMipFiles = !mipFiles.empty();
    const auto push = [&](int level, uint32_t w, uint32_t h, const std::vector<uint8_t>& data) {
        upload_level(base, level, w, h, data);
        bytes += data.size();
        ++levels;
    };

    while (glGetError() != GL_NO_ERROR) {}

    push(0, base.width, base.height, base.levels[0].data);
    if (useMipFiles) {
        int expected = 1;
        for (const auto& entry : mipFiles) {
            if (entry.first != expected) break;  // hueco: la cadena acaba ahí
            PackImage mip;
            if (!load_pack_image(entry.second, mip) || mip.levels.empty()) break;
            if (!make_uploadable(mip)) break;
            // Un nivel PNG bajo una base DDS (o al revés) no se puede mezclar
            // en la misma textura GL: la cadena acaba aquí.
            if (mip.compressed != base.compressed || mip.internalFormat != base.internalFormat) break;
            // El pack de Henriko vuelca varios _mipN a la dimensión del original
            // GX, que solo encaja en un nivel concreto de la mitad: si este
            // nivel no tiene el tamaño esperado por la cadena, la cadena para
            // aquí (subirlo con otra dimensión sería GL_INVALID_VALUE).
            if (mip.width != (base.width >> expected) || mip.height != (base.height >> expected)) break;
            push(expected, mip.width, mip.height, mip.levels[0].data);
            ++expected;
        }
    } else {
        for (size_t i = 1; i < base.levels.size(); ++i)
            push(static_cast<int>(i), std::max<uint32_t>(1, base.width >> i),
                 std::max<uint32_t>(1, base.height >> i), base.levels[i].data);
    }

    // La cadena del pack casi nunca llega hasta 1x1 (Henriko para en _mip3 o
    // se queda en la dimensión GX original). Sin acotar el último nivel, un
    // filtro MIPMAP deja la textura incompleta y GL la muestra negra.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        // La textura quedó en un estado impredecible; mejor decirle al llamador
        // que no suba la original y anotar la causa.
        static char why[64];
        snprintf(why, sizeof(why), "GL upload error 0x%04X; texture left as-is", unsigned(err));
        log_once(name, why);
        ++sStatFailed;
        return true;
    }
    ++sStatReplaced;
    if (glLevels) *glLevels = levels;
    if (gpuBytes) *gpuBytes = bytes;
    (void)texId;  // el texId ya está vinculado por el llamador
    return true;
}