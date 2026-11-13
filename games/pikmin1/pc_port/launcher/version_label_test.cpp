// The version label next to the logo (#327): Game Bubble, glossy orange,
// rendered in the CPU at the requested pixel size.
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
        check(img.rgba[3] == 0 && img.rgba[img.rgba.size() - 1] == 0, "transparent around the label");
    }
    VersionLabelImage none;
    check(!renderVersionLabel(kFontGameBubble, kFontGameBubbleSize, "", 20.0f, none), "nothing to draw");
    check(!renderVersionLabel(nullptr, 0, "v1.3", 20.0f, none), "no font");
    std::cout << "version_label_test: ok\n";
}
