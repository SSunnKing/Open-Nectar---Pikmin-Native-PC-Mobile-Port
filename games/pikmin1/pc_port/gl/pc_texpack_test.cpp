// Exercise the real DDS/PNG loader and upload decisions without a GL context.
// Only GL/SDL dispatch and the netplay tripwire are replaced.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#define PIKMIN_PC_OPENGL_H
using GLenum = unsigned;
using GLuint = unsigned;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using PFNGLCOMPRESSEDTEXIMAGE2DPROC = void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
using PFNGLGENERATEMIPMAPPROC = void (*)(GLenum);
constexpr GLenum GL_RGBA8 = 0x8058, GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43, GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_BASE_LEVEL = 0x813C, GL_TEXTURE_MAX_LEVEL = 0x813D;
constexpr GLenum GL_EXTENSIONS = 0x1F03, GL_NUM_EXTENSIONS = 0x821D, GL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr GLenum GL_NO_ERROR = 0, GL_INVALID_OPERATION = 0x0502;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;
struct Uploaded {
    unsigned width, height;
    GLenum format;
    bool compressed;
    std::vector<uint8_t> bytes;
};
static std::map<int, Uploaded> uploaded;
static GLint maxLevel = 0, baseLevel = 0;
static unsigned generateCalls = 0;
static bool failGeneration = false;
static GLenum error = GL_NO_ERROR;
static void glTexImage2D(GLenum, GLint level, GLint format, GLsizei w, GLsizei h,
                         GLint, GLenum, GLenum, const void* pixels)
{
    const auto* data = static_cast<const uint8_t*>(pixels);
    uploaded[level] = {unsigned(w), unsigned(h), GLenum(format), false, {data, data + size_t(w) * h * 4}};
}
static void glCompressedTexImage2D(GLenum, GLint level, GLenum format, GLsizei w, GLsizei h,
                                   GLint, GLsizei bytes, const void* pixels)
{
    const auto* data = static_cast<const uint8_t*>(pixels);
    uploaded[level] = {unsigned(w), unsigned(h), format, true, {data, data + bytes}};
}
static void glTexParameteri(GLenum, GLenum pname, GLint value)
{
    if (pname == GL_TEXTURE_MAX_LEVEL) maxLevel = value;
    if (pname == GL_TEXTURE_BASE_LEVEL) baseLevel = value;
}
static GLenum glGetError() { const auto value = error; error = GL_NO_ERROR; return value; }
static void glGetIntegerv(GLenum, GLint* value) { *value = 8192; }
static const GLubyte* glGetString(GLenum) { return reinterpret_cast<const GLubyte*>(""); }
static void generate_mipmap(GLenum)
{
    ++generateCalls;
    if (failGeneration) { error = GL_INVALID_OPERATION; return; }
    const Uploaded source = uploaded.at(baseLevel);
    unsigned w = source.width, h = source.height;
    const size_t blockBytes = source.format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16;
    for (int i = baseLevel + 1; i <= maxLevel; ++i) {
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        uploaded[i] = {w, h, source.format, true,
            std::vector<uint8_t>(size_t((w + 3) / 4) * ((h + 3) / 4) * blockBytes)};
    }
}
[[maybe_unused]] static void* test_get_proc(const char* name)
{
    if (strcmp(name, "glGenerateMipmap") == 0) return reinterpret_cast<void*>(generate_mipmap);
    if (strcmp(name, "glCompressedTexImage2D") == 0) return reinterpret_cast<void*>(glCompressedTexImage2D);
    return nullptr;
}
#define SDL_GL_GetProcAddress test_get_proc
#include "pc_texpack.cpp"
extern "C" int pc_netplay_present_null_active() { return 0; }
extern "C" void pc_netplay_present_note_real() {}

static void write_dds(const fs::path& path, unsigned w, unsigned h, unsigned format = 28,
                      uint8_t value = 20, unsigned embedded = 1)
{
    const bool compressed = format != 28;
    const unsigned blockBytes = format == 71 ? 8 : 16;
    std::vector<uint8_t> file(148, 0);
    const auto put = [&](size_t off, unsigned v) {
        for (int b = 0; b < 4; ++b) file[off + b] = uint8_t(v >> (8 * b));
    };
    memcpy(file.data(), "DDS ", 4); put(4, 124); put(12, h); put(16, w);
    put(28, embedded); put(76, 32); put(84, 0x30315844); put(128, format);
    for (unsigned level = 0; level < embedded; ++level) {
        const size_t bytes = compressed ? size_t((w + 3) / 4) * ((h + 3) / 4) * blockBytes : size_t(w) * h * 4;
        const size_t start = file.size();
        file.resize(start + bytes, compressed ? 0 : value);
        if (format == 98 || format == 99) // Valid BC7 mode 6, black.
            for (size_t i = start; i < file.size(); i += 16) file[i] = 0x40;
        w = pc_texture_next_mip_size(w); h = pc_texture_next_mip_size(h);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("pc-texpack-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    sEnabled = true;
    const auto base = [&](unsigned w, unsigned h, unsigned format = 28, unsigned embedded = 1) {
        uploaded.clear(); sIndex.clear();
        const fs::path path = dir / "tex1_base.dds";
        write_dds(path, w, h, format, 20, embedded); sIndex["tex1_base"] = {path};
    };
    const auto sibling = [&](int level, unsigned w, unsigned h, unsigned format = 28) {
        const fs::path path = dir / ("tex1_base_mip" + std::to_string(level) + ".dds");
        write_dds(path, w, h, format, 80); sIndex["tex1_base"].push_back(path);
    };
    const auto upload = [&](int expected, unsigned w, unsigned h, bool compressed = false) {
        int levels = 0; size_t bytes = 0, actualBytes = 0;
        assert(pc_texpack_try_upload("tex1_base", 1, &levels, &bytes));
        assert(levels == expected && maxLevel == expected - 1);
        assert(uploaded.size() == size_t(expected));
        for (int i = 0; i < levels; ++i) {
            const Uploaded& level = uploaded.at(i);
            assert(level.width == w && level.height == h && level.compressed == compressed);
            actualBytes += level.bytes.size();
            w = pc_texture_next_mip_size(w); h = pc_texture_next_mip_size(h);
        }
        assert(bytes == actualBytes);
    };
    base(8, 8); upload(4, 8, 8); // Single level.
    assert(uploaded.at(3).bytes[0] == 20);
    base(8, 8); sibling(1, 2, 2); upload(4, 8, 8); // 8192 -> 2048 gap, scaled down.
    assert(uploaded.at(1).bytes[0] == 20);
    base(8, 8); sibling(2, 2, 2); upload(4, 8, 8); // Missing sibling.
    base(8, 8); sibling(1, 4, 4); upload(4, 8, 8); // Preserve authored prefix.
    assert(uploaded.at(1).bytes[0] == 80 && uploaded.at(3).bytes[0] == 80);
    base(8, 8); sibling(1, 4, 4); sibling(2, 4, 4); upload(4, 8, 8); // Repeated size.
    base(8, 2); sibling(1, 4, 1); upload(4, 8, 2); // One axis reaches 1 first.
    base(5, 3); upload(3, 5, 3);
    base(1, 1); upload(1, 1, 1);
    base(8, 8, 28, 4); upload(4, 8, 8); // Embedded DDS chain.
    base(8, 8, 28, 4);
    fs::resize_file(sIndex["tex1_base"].front(), 148 + 8 * 8 * 4);
    upload(4, 8, 8); // Missing declared embedded tail is rebuilt.
    base(8, 8); sibling(1, 4, 4, 71); upload(4, 8, 8); // Format mismatch.
    for (unsigned format : {71u, 74u, 77u, 98u}) {
        sS3tcAvailable = sBptcAvailable = false;
        base(8, 8, format); upload(4, 8, 8); // CPU decode BC1/2/3/7.
        sS3tcAvailable = sBptcAvailable = true;
        failGeneration = true;
        base(8, 8, format); upload(4, 8, 8); // Failed desktop generation also decodes.
        failGeneration = false;
        base(8, 8, format, 4); upload(4, 8, 8, true); // Complete supported chains stay compressed.
    }
#if !PIKI_USE_GLES
    sS3tcAvailable = true;
    base(8, 8, 71); sibling(1, 4, 4, 71); upload(4, 8, 8, true);
    assert(baseLevel == 0 && generateCalls > 0); // Generated tail, binding range restored.
#else
    assert(generateCalls == 0); // GLES never calls glGenerateMipmap on compressed data.
#endif
    sBptcAvailable = false;
    base(8, 8, 99); upload(4, 8, 8);
    assert(uploaded.at(0).format == GL_SRGB8_ALPHA8);

    const uint8_t png[] = {
        0x89, 0x50, 0x4e, 0x47, 0xd, 0xa, 0x1a, 0xa, 0x0, 0x0, 0x0, 0xd,
        0x49, 0x48, 0x44, 0x52, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x2,
        0x8, 0x6, 0x0, 0x0, 0x0, 0x72, 0xb6, 0xd, 0x24, 0x0, 0x0, 0x0,
        0xe, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x10, 0x81, 0x2, 0x6,
        0x18, 0x3, 0x0, 0xb, 0x52, 0x1, 0x41, 0xba, 0xf1, 0x30, 0x16, 0x0,
        0x0, 0x0, 0x0, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    const fs::path pngPath = dir / "tex1_base.png";
    { std::ofstream out(pngPath, std::ios::binary); out.write(reinterpret_cast<const char*>(png), sizeof(png)); }
    uploaded.clear(); sIndex["tex1_base"] = {pngPath}; upload(2, 2, 2);
    assert(uploaded.at(1).bytes[0] == 20);

    std::vector<uint8_t> pixels = {0, 0, 0, 0, 255, 255, 255, 255};
    assert(pc_texture_downsample_rgba8(pixels, 2, 1) == std::vector<uint8_t>({128, 128, 128, 128}));
    assert(pc_texture_downsample_rgba8(pixels, 2, 1, true) == std::vector<uint8_t>({188, 188, 188, 128}));
    pixels = {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    assert(pc_texture_downsample_rgba8(pixels, 3, 1)[0] == 85); // Odd edge included.
    assert(pc_texture_mip_count(8192, 8192) == 14);
    assert(pc_texture_mip_count(0, 8) == 0);
    fs::remove_all(dir);
    puts("pc_texpack_test: all checks passed");
}
