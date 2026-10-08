// Standalone visual fixture. Includes the menu implementation to set page
// state without adding debug controls to the game or touching a save/config.
// The wrapper omits the production pc_settings.cpp object when linking.
#include <SDL.h>
#include <GL/gl.h>
#include "settings/pc_settings.cpp"

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    SDL_SetMainReady();
    if (!pc_window_init("Port menu verification", std::atoi(argv[2]), std::atoi(argv[3]))) return 2;
    gsys->Initialise();
    pc_settings_p2d_init();
    sConfig.applyDefaults();
    rebuildResolutionList();

    for (int page = 0; page < 17; ++page) {
        sPending = sConfig;
        sMenuOpen = page != 15;
        sSelection = ROW_DISPLAY_MODE;
        sControlSelection = sGamepadSelection = sModsSelection = 0;
        sWaitingForKey = sWaitingForButton = sNewGamePromptOpen = false;
        sInControlsSubmenu = page == 1;
        sInGamepadSubmenu = page == 2;
        sInAdvancedSubmenu = page == 3;
        sInResolutionSubmenu = page == 4;
        sInModsSubmenu = page == 5;
        sVideoConfirmActive = page == 6;
        sVideoConfirmStartMs = SDL_GetTicks();
        if (page == 4) openResolutionSubmenu();
        if (page == 9) {
            sSelection = ROW_FPS_MODE;
            sPending.fpsMode = 2;
        }
        if (page == 10) {
            sInControlsSubmenu = true;
            sControlSelection = PC_KEY_ACT_COUNT - 1;
        }
        if (page == 11) {
            sInModsSubmenu = true;
            sModsSelection = 3;
            sPending.pikiLimit = 500;
        }
        if (page == 12) {
            sInControlsSubmenu = true;
            sWaitingForKey = true;
        }
        if (page == 13) {
            sInGamepadSubmenu = true;
            sWaitingForButton = true;
        }
        if (page == 14) sSelection = ROW_SAVE;

        pc_gfx_begin_frame();
        glClearColor(0.15f, 0.25f, 0.4f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (page == 7 || page == 8) {
            sNewGamePromptOpen = true;
            sNewGamePromptChoice = page - 7;
            pc_newgame_prompt_draw();
        } else {
            pc_settings_draw();
        }
        pc_gfx_flush_batch();
        glFinish();
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int w = viewport[2], h = viewport[3];
        std::vector<unsigned char> pixels(w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(viewport[0], viewport[1], w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        char basename[32];
        std::snprintf(basename, sizeof(basename), "/page-%02d.ppm", page);
        const std::string name = std::string(argv[1]) + basename;
        FILE* file = std::fopen(name.c_str(), "wb");
        if (!file) return 3;
        std::fprintf(file, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) {
            if (std::fwrite(pixels.data() + y * w * 3, 1, w * 3, file) != size_t(w * 3)) return 4;
        }
        std::fclose(file);
    }
    // The standalone fixture does not run the game's application teardown.
    std::_Exit(0);
}
