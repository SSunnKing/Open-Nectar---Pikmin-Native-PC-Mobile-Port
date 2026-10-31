#include "installer_ui.h"

#include <SDL.h>
#include <cstdio>
#include <string>

int main()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    pikmin::launcher::InstallerWindow window;
    std::string error;
    if (!window.open(error)) {
        std::fprintf(stderr, "install_dialog_snapshot: %s\n", error.c_str());
        return 1;
    }
    const std::string rom = "/home/player/Games/Pikmin (USA).iso";
    if (!window.saveSetupScreenshot("/opt/cursor/artifacts/install_dialog_before.bmp", rom, {})) return 1;
    if (!window.saveSetupScreenshot("/opt/cursor/artifacts/install_dialog_after.bmp", rom,
                                    "/home/player/.local/share/pikmin-native")) {
        return 1;
    }
    std::puts("install_dialog_snapshot: saved before/after BMPs");
    return 0;
}
