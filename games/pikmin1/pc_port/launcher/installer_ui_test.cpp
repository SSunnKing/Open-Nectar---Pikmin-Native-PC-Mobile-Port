#include "installer_ui.h"

#include <SDL.h>
#include <cstdio>
#include <string>

int main()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    std::string error;
    pikmin::launcher::InstallerWindow window;
    if (!window.open(error)) {
        std::fprintf(stderr, "installer_ui_test: %s\n", error.c_str());
        return 1;
    }
    window.updateProgress(50, "dataDir/test/file.bin");
    window.updateProgress(101, "Preparing a temporary ISO", "Converting disc");
    window.updateProgress(50, "", "Checking disc");
    window.updateProgress(100, "", "Finishing");

    const auto click = [](int x, int y) {
        SDL_Event event {};
        event.type = SDL_MOUSEBUTTONUP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        SDL_PushEvent(&event);
    };
    click(600, 140); // Choose the original compressed source.
    click(600, 230);
    click(300, 320);
    std::string rom, directory;
    if (!window.choosePaths([] { return "original.rvz"; }, [] { return "install"; }, rom, directory)
        || rom != "original.rvz" || directory != "install") return 1;
    // A failed attempt must return to setup with its original choices intact,
    // not with the temporary ISO path used internally during conversion.
    window.updateProgress(25, "", "Checking disc");
    click(300, 320);
    if (!window.choosePaths([] { return "unexpected"; }, [] { return "unexpected"; }, rom, directory)
        || rom != "original.rvz" || directory != "install") return 1;
    std::puts("installer_ui_test: phases and setup selection retention passed");
    return 0;
}
