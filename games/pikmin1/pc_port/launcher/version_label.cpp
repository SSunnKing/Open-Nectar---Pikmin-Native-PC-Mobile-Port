#include "version_label.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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

// Sampled from assets/logo.png: yellow highlight, orange body, deep orange
// bottom, brown edge and the halo around the letters.
constexpr Rgb kHighlight { 255.0f, 250.0f, 215.0f };
constexpr Rgb kTop { 255.0f, 214.0f, 70.0f };
constexpr Rgb kBody { 255.0f, 168.0f, 36.0f };
constexpr Rgb kBottom { 246.0f, 116.0f, 8.0f };
constexpr Rgb kEdge { 214.0f, 96.0f, 6.0f };
constexpr Rgb kOutline { 170.0f, 70.0f, 4.0f };
constexpr Rgb kHalo { 236.0f, 128.0f, 20.0f };

Rgb mix(Rgb a, Rgb b, float t)
{
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

float smooth(float e0, float e1, float x)
{
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// Distance (in pixels) from every pixel to the nearest pixel where `inside`
// is true: two-pass chamfer with 1 / sqrt(2) steps.
std::vector<float> distanceTo(const std::vector<std::uint8_t>& inside, int w, int h)
{
    const float inf = std::numeric_limits<float>::max() / 4.0f;
    const float d1 = 1.0f, d2 = 1.41421356f;
    std::vector<float> d(std::size_t(w) * std::size_t(h));
    for (std::size_t i = 0; i < d.size(); ++i) d[i] = inside[i] ? 0.0f : inf;
    const auto at = [&](int x, int y) -> float& { return d[std::size_t(y) * std::size_t(w) + std::size_t(x)]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float& v = at(x, y);
            if (x > 0) v = std::min(v, at(x - 1, y) + d1);
            if (y > 0) {
                v = std::min(v, at(x, y - 1) + d1);
                if (x > 0) v = std::min(v, at(x - 1, y - 1) + d2);
                if (x + 1 < w) v = std::min(v, at(x + 1, y - 1) + d2);
            }
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            float& v = at(x, y);
            if (x + 1 < w) v = std::min(v, at(x + 1, y) + d1);
            if (y + 1 < h) {
                v = std::min(v, at(x, y + 1) + d1);
                if (x + 1 < w) v = std::min(v, at(x + 1, y + 1) + d2);
                if (x > 0) v = std::min(v, at(x - 1, y + 1) + d2);
            }
        }
    return d;
}

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

    // Glyph coverage, with room around it for the outline and the halo.
    const float outlineR = std::max(1.0f, inkHeight * 0.06f);
    const float haloR = std::max(1.5f, inkHeight * 0.16f);
    const int pad = int(std::ceil(outlineR + haloR)) + 2;
    const float baseline = float(pad) + std::ceil(float(ascent) * scale);
    // A little extra room between letters: the halo would fuse them at small sizes.
    const float tracking = std::max(1.0f, inkHeight * 0.06f);
    float penX = float(pad);
    float textW = 0.0f;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int cp = static_cast<unsigned char>(text[i]);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&font, cp, scale, scale, penX - std::floor(penX), 0.0f, &bx0, &by0, &bx1, &by1);
        textW = std::max(textW, std::floor(penX) + float(bx1));
        penX += float(adv) * scale + tracking;
        if (i + 1 < text.size()) penX += float(stbtt_GetCodepointKernAdvance(&font, cp, static_cast<unsigned char>(text[i + 1]))) * scale;
    }
    const int w = int(std::ceil(std::max(textW, penX))) + pad;
    const int h = int(std::ceil(baseline - float(descent) * scale)) + pad;
    if (w <= 2 * pad || h <= 2 * pad) return false;

    std::vector<float> cover(std::size_t(w) * std::size_t(h), 0.0f);
    penX = float(pad);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int cp = static_cast<unsigned char>(text[i]);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        const float shift = penX - std::floor(penX);
        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, shift, 0.0f, cp, &gw, &gh, &gx, &gy);
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
        penX += float(adv) * scale + tracking;
        if (i + 1 < text.size()) penX += float(stbtt_GetCodepointKernAdvance(&font, cp, static_cast<unsigned char>(text[i + 1]))) * scale;
    }

    // Ink box.
    int inkT = h, inkB = -1, inkL = w, inkR = -1;
    std::vector<std::uint8_t> ink(cover.size()), outside(cover.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            ink[i] = cover[i] >= 0.5f;
            outside[i] = !ink[i];
            if (cover[i] > 0.0f) {
                inkT = std::min(inkT, y);
                inkB = std::max(inkB, y);
                inkL = std::min(inkL, x);
                inkR = std::max(inkR, x);
            }
        }
    if (inkB < 0) return false;

    // Small holes inside the letters (Game Bubble draws its shine marks as
    // cut-outs) are filled as highlights; real counters, like the one in a
    // 0, stay open.
    std::vector<std::uint8_t> shine(cover.size(), 0);
    {
        std::vector<int> label(cover.size(), 0);
        std::vector<std::size_t> stack;
        int next = 0;
        const float maxShine = 0.035f * float(inkB + 1 - inkT) * float(inkB + 1 - inkT);
        for (std::size_t start = 0; start < cover.size(); ++start) {
            if (ink[start] || label[start]) continue;
            ++next;
            std::vector<std::size_t> members;
            bool touchesBorder = false;
            stack.push_back(start);
            label[start] = next;
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
                    if (ink[j] || label[j]) continue;
                    label[j] = next;
                    stack.push_back(j);
                }
            }
            if (!touchesBorder && float(members.size()) <= maxShine)
                for (std::size_t i : members) shine[i] = 1;
        }
        for (std::size_t i = 0; i < cover.size(); ++i)
            if (shine[i]) {
                ink[i] = 1;
                outside[i] = 0;
            }
    }
    const std::vector<float> fromInk = distanceTo(ink, w, h);     // outside: how far from the letters
    const std::vector<float> fromEdge = distanceTo(outside, w, h); // inside: how deep into a stroke

    // Stroke half-width, for the tube shading: the deepest point of the ink.
    float deepest = 1.0f;
    for (std::size_t i = 0; i < cover.size(); ++i)
        if (ink[i]) deepest = std::max(deepest, fromEdge[i]);

    out.width = w;
    out.height = h;
    out.inkTop = float(inkT);
    out.inkBottom = float(inkB + 1);
    out.inkLeft = float(inkL);
    out.inkRight = float(inkR + 1);
    out.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    const float inkH = std::max(1.0f, float(inkB + 1 - inkT));
    // Small labels get a fainter halo so they stay crisp.
    const float haloStrength = 0.42f * (0.35f + 0.65f * smooth(10.0f, 22.0f, inkHeight));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            // Halo, then outline, then the glossy fill, each one "over".
            const float dOut = ink[i] ? 0.0f : fromInk[i];
            const float haloA = haloStrength * (1.0f - smooth(outlineR, outlineR + haloR, dOut));
            const float outlineA = clamp01(outlineR + 0.5f - dOut);
            const float fillA = shine[i] ? 1.0f : cover[i];

            // Fill: vertical gradient, darker toward the stroke edges, and a
            // highlight along the middle of the upper half of each stroke.
            const float t = clamp01((float(y) + 0.5f - float(inkT)) / inkH);
            Rgb fill = t < 0.45f ? mix(kTop, kBody, t / 0.45f) : mix(kBody, kBottom, (t - 0.45f) / 0.55f);
            const float depth = ink[i] ? clamp01(fromEdge[i] / deepest) : 0.0f;
            fill = mix(kEdge, fill, smooth(0.0f, 0.6f, depth));
            const float gloss = smooth(0.4f, 0.85f, depth) * (1.0f - smooth(0.2f, 0.5f, t));
            fill = mix(fill, kHighlight, gloss);
            if (shine[i]) fill = mix(fill, kHighlight, 1.0f - cover[i]);

            float a = haloA;
            Rgb c = kHalo;
            const auto over = [&](Rgb top, float topA) {
                const float na = topA + a * (1.0f - topA);
                if (na <= 0.0f) return;
                c = { (top.r * topA + c.r * a * (1.0f - topA)) / na, (top.g * topA + c.g * a * (1.0f - topA)) / na,
                      (top.b * topA + c.b * a * (1.0f - topA)) / na };
                a = na;
            };
            over(kOutline, outlineA);
            over(fill, fillA);
            unsigned char* p = &out.rgba[i * 4];
            p[0] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.r)));
            p[1] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.g)));
            p[2] = static_cast<unsigned char>(std::lround(std::min(255.0f, c.b)));
            p[3] = static_cast<unsigned char>(std::lround(clamp01(a) * 255.0f));
        }
    return true;
}

} // namespace pikmin::launcher
