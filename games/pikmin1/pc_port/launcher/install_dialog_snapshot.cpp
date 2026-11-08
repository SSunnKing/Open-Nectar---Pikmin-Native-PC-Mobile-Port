#include "installer_ui.h"

#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
    const std::filesystem::path output = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(output);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    pikmin::launcher::InstallerWindow window;
    std::string error;
    if (!window.open(error)) {
        std::fprintf(stderr, "install_dialog_snapshot: %s\n", error.c_str());
        return 1;
    }
    const std::string rom = "/home/player/Games/Pikmin (USA).iso";
    if (!window.saveSetupScreenshot((output / "install_dialog_before.bmp").string(), rom, {})) return 1;
    if (!window.saveSetupScreenshot((output / "install_dialog_after.bmp").string(), rom,
                                    "/home/player/.local/share/pikmin-native")) {
        return 1;
    }
    std::puts("install_dialog_snapshot: saved before/after BMPs");
    return 0;
}
