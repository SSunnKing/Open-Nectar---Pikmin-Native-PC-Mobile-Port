#include "version_label.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// stb_truetype, private to this file (ImGui keeps its copy static too).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace pikmin::launcher {
namespace {

struct Rgb {
    float r, g, b;
};

// Sampled from assets/logo.png: the letters' orange, the lighter orange of
// their upper half, the dark orange edge and the white shine marks.
constexpr Rgb kFill { 255.0f, 160.0f, 28.0f };
constexpr Rgb kFillTop { 255.0f, 200.0f, 70.0f };
constexpr Rgb kOutline { 176.0f, 66.0f, 2.0f };
constexpr Rgb kShine { 255.0f, 252.0f, 230.0f };

Rgb mix(Rgb a, Rgb b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

} // namespace

bool renderVersionLabel(const unsigned char* ttf, std::size_t ttfSize, const std::string& text, float inkHeight,
                        VersionLabelImage& out)
{
    out = VersionLabelImage();
    if (!ttf || ttfSize == 0 || text.empty() || !(inkHeight >= 1.0f)) return false;
    stbtt_fontinfo font;
    const int offset = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (offset < 0 || !stbtt_InitFont(&font, ttf, offset)) return false;

    // Scale so a digit is `inkHeight` tall.
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!stbtt_GetCodepointBox(&font, '1', &x0, &y0, &x1, &y1) || y1 <= y0) return false;
    const float scale = inkHeight / float(y1 - y0);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

    // Outline a whole number of pixels thick, so its edge stays crisp.
    const float outlineR = std::max(1.0f, std::round(inkHeight * 0.07f));
    const int pad = int(std::ceil(outlineR)) + 3;
    const float baseline = float(pad) + std::ceil(float(ascent) * scale);
    // A little room between letters so the outlines don't fuse at small sizes.
    const float tracking = std::max(1.0f, inkHeight * 0.06f);
    const auto advance = [&](std::size_t i, float& penX) {
        const int cp = static_cast<unsigned char>(text[i]);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        penX += float(adv) * scale + tracking;
        if (i + 1 < text.size())
            penX += float(stbtt_GetCodepointKernAdvance(&font, cp, static_cast<unsigned char>(text[i + 1]))) * scale;
    };
    float penX = float(pad);
    float textW = 0.0f;
    for (std::size_t i = 0; i < text.size(); ++i) {
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&font, static_cast<unsigned char>(text[i]), scale, scale,
                                            penX - std::floor(penX), 0.0f, &bx0, &by0, &bx1, &by1);
        textW = std::max(textW, std::floor(penX) + float(bx1));
        advance(i, penX);
    }
    const int w = int(std::ceil(std::max(textW, penX))) + pad;
    const int h = int(std::ceil(baseline - float(descent) * scale)) + pad;
    if (w <= 2 * pad || h <= 2 * pad) return false;

    std::vector<float> cover(std::size_t(w) * std::size_t(h), 0.0f);
    penX = float(pad);
    for (std::size_t i = 0; i < text.size(); ++i) {
        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, penX - std::floor(penX), 0.0f,
                                                              static_cast<unsigned char>(text[i]), &gw, &gh, &gx, &gy);
        if (bmp) {
            const int ox = int(std::floor(penX)) + gx, oy = int(baseline) + gy;
            for (int y = 0; y < gh; ++y)
                for (int x = 0; x < gw; ++x) {
                    const int px = ox + x, py = oy + y;
                    if (px < 0 || py < 0 || px >= w || py >= h) continue;
                    float& c = cover[std::size_t(py) * std::size_t(w) + std::size_t(px)];
                    c = std::max(c, float(bmp[y * gw + x]) / 255.0f);
                }
            stbtt_FreeBitmap(bmp, nullptr);
        }
        advance(i, penX);
    }

    // Ink box.
    int inkT = h, inkB = -1, inkL = w, inkR = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (cover[std::size_t(y) * std::size_t(w) + std::size_t(x)] > 0.0f) {
                inkT = std::min(inkT, y);
                inkB = std::max(inkB, y);
                inkL = std::min(inkL, x);
                inkR = std::max(inkR, x);
            }
    if (inkB < 0) return false;

    // Game Bubble draws its shine marks as small holes inside the letters:
    // they are filled in white, like the logo's highlights. Real counters,
    // like the one in a 0, are bigger and stay open.
    std::vector<std::uint8_t> shine(cover.size(), 0);
    {
        std::vector<std::uint8_t> seen(cover.size(), 0);
        std::vector<std::size_t> stack, members;
        const float inkH = float(inkB + 1 - inkT);
        const float maxShine = 0.035f * inkH * inkH;
        for (std::size_t start = 0; start < cover.size(); ++start) {
            if (cover[start] >= 0.5f || seen[start]) continue;
            members.clear();
            bool touchesBorder = false;
            stack.push_back(start);
            seen[start] = 1;
            while (!stack.empty()) {
                const std::size_t i = stack.back();
                stack.pop_back();
                members.push_back(i);
                const int x = int(i % std::size_t(w)), y = int(i / std::size_t(w));
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) touchesBorder = true;
                const int nx[4] = { x - 1, x + 1, x, x };
                const int ny[4] = { y, y, y - 1, y + 1 };
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                    const std::size_t j = std::size_t(ny[k]) * std::size_t(w) + std::size_t(nx[k]);
                    if (cover[j] >= 0.5f || seen[j]) continue;
                    seen[j] = 1;
                    stack.push_back(j);
                }
            }
            if (!touchesBorder && float(members.size()) <= maxShine)
                for (std::size_t i : members) shine[i] = 1;
        }
    }
    const auto solid = [&](std::size_t i) { return shine[i] ? 1.0f : cover[i]; };

    // Outline: the letters' coverage spread by a disk: full strength out to
    // outlineR pixels, fading over the next one.
    const int reach = int(std::ceil(outlineR + 1.0f));
    std::vector<float> outline(cover.size(), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float best = 0.0f;
            for (int dy = -reach; dy <= reach; ++dy)
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int qx = x + dx, qy = y + dy;
                    if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                    const float c = solid(std::size_t(qy) * std::size_t(w) + std::size_t(qx));
                    if (c <= best) continue;
                    best = std::max(best, c * clamp01(outlineR + 1.0f - std::sqrt(float(dx * dx + dy * dy))));
                }
            outline[std::size_t(y) * std::size_t(w) + std::size_t(x)] = best;
        }

    out.width = w;
    out.height = h;
    out.inkTop = float(inkT);
    out.inkBottom = float(inkB + 1);
    out.inkLeft = float(inkL);
    out.inkRight = float(inkR + 1);
    out.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    // Upper half lighter, with a one-pixel step at the middle of the digits.
    const float middle = float(inkT) + float(inkB + 1 - inkT) * 0.5f;
    for (int y = 0; y < h; ++y) {
        const Rgb row = mix(kFillTop, kFill, clamp01(float(y) + 1.0f - middle));
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            const Rgb fill = shine[i] && cover[i] < 0.5f ? kShine : row;
            const float fillA = solid(i);
            // Fill over the outline.
            const float a = fillA + outline[i] * (1.0f - fillA);
            Rgb c = kOutline;
            if (a > 0.0f) {
                const float wo = outline[i] * (1.0f - fillA);
                c = { (fill.r * fillA + kOutline.r * wo) / a, (fill.g * fillA + kOutline.g * wo) / a,
                      (fill.b * fillA + kOutline.b * wo) / a };
            }
            unsigned char* p = &out.rgba[i * 4];
            p[0] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.r)));
            p[1] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.g)));
            p[2] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.b)));
            p[3] = static_cast<unsigned char>(std::lround(clamp01(a) * 255.0f));
        }
    }
    return true;
}

} // namespace pikmin::launcher
