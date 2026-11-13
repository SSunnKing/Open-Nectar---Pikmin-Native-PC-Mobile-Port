// The version label next to the logo (#327): Game Bubble, flat orange with a
// lighter top and a crisp outline, rendered in the CPU at the requested size.
#include "version_label.h"

#include "font_game_bubble.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace pikmin::launcher;

static void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    for (float ink : { 12.0f, 21.0f, 42.0f, 85.0f }) {
        VersionLabelImage img;
        check(renderVersionLabel(kFontGameBubble, kFontGameBubbleSize, "v1.3", ink, img), "the font renders");
        check(img.width > 0 && img.height > 0, "the label has pixels");
        check(img.rgba.size() == std::size_t(img.width) * img.height * 4, "one RGBA pixel each");
        // Anti-aliased rows and the round tops of v/3 can add a pixel or two.
        check(std::fabs((img.inkBottom - img.inkTop) - ink) <= 1.0f + ink * 0.03f, "digits are as tall as asked");
        check(img.inkLeft >= 0.0f && img.inkRight <= float(img.width) && img.inkTop >= 0.0f
                  && img.inkBottom <= float(img.height),
              "ink inside the image");
        // Opaque orange in the middle of the letters, transparent corners.
        int opaque = 0, orange = 0;
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
            if (img.rgba[i + 3] < 250) continue;
            ++opaque;
            if (img.rgba[i] > 200 && img.rgba[i + 1] > 60 && img.rgba[i + 2] < 120) ++orange;
        }
        check(opaque > 0 && orange * 2 > opaque, "mostly orange letters");
        // Crisp dark-orange outline, lighter upper half, and no soft halo:
        // nothing faint more than a couple of pixels outside the outline.
        int outline = 0, faint = 0;
        double topG = 0, bottomG = 0;
        int topN = 0, bottomN = 0;
        const float mid = (img.inkTop + img.inkBottom) * 0.5f;
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const unsigned char* p = &img.rgba[(std::size_t(y) * img.width + x) * 4];
                if (p[3] == 255 && p[0] < 200) ++outline;
                if (p[3] > 0 && p[3] < 255 && (x < img.inkLeft - ink * 0.07f - 3 || x >= img.inkRight + ink * 0.07f + 3))
                    ++faint;
                if (p[3] == 255 && p[0] == 255) {
                    if (y + 1 < mid) { topG += p[1]; ++topN; }
                    else if (y > mid) { bottomG += p[1]; ++bottomN; }
                }
            }
        check(outline > 0, "dark outline");
        check(faint == 0, "no halo");
        check(topN > 0 && bottomN > 0 && topG / topN > bottomG / bottomN + 10.0, "upper half lighter");
        check(img.rgba[3] == 0 && img.rgba[img.rgba.size() - 1] == 0, "transparent around the label");
    }
    VersionLabelImage none;
    check(!renderVersionLabel(kFontGameBubble, kFontGameBubbleSize, "", 20.0f, none), "nothing to draw");
    check(!renderVersionLabel(nullptr, 0, "v1.3", 20.0f, none), "no font");
    std::cout << "version_label_test: ok\n";
}
