// Private integration smoke: real System loop, SDL events and presentation.
// No game scene, save, enemy or campaign state is created.
#include "BaseApp.h"
#include "Node.h"
#include "system.h"
#include "pc_window.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool pass, const char* message)
{
    if (!pass) { std::printf("FAST_FORWARD_FAIL %s\n", message); std::fflush(stdout); std::_Exit(1); }
}

static void key(Uint32 type)
{
    SDL_Event event{};
    event.type = type;
    event.key.keysym.scancode = SDL_SCANCODE_F10;
    event.key.keysym.sym = SDLK_F10;
    require(SDL_PushEvent(&event) == 1, "queue F10");
}

class TimingApp : public BaseApp {
    int phase = 0, ticks = 0;
    Uint64 start = 0;
    double simulation = 0;
public:
    int idle() override
    {
        require(std::fabs(gsys->getFrameTime() - 1.0 / 30.0) < 1e-7, "fixed delta");
        if (phase < 3) {
            const double speed = phase == 1 ? 2.0 : 1.0;
            if (pc_window_simulation_speed(gsys->mFrameRate) != speed) {
                SDL_Window* window = SDL_GetWindowFromID(1);
                std::printf("FAST_FORWARD_DIAGNOSTIC phase=%d clamp=%d speed=%.1f flags=%u focus=%p\n",
                    phase, gsys->mFrameRate, pc_window_simulation_speed(gsys->mFrameRate),
                    window ? SDL_GetWindowFlags(window) : 0, (void*)SDL_GetKeyboardFocus());
            }
            require(pc_window_simulation_speed(gsys->mFrameRate) == speed, "hold/release speed or window focus");
            if (ticks == 0) start = SDL_GetPerformanceCounter();
            else simulation += gsys->getFrameTime();
            if (++ticks == 61) {
                const double wall = double(SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                const double ratio = simulation / wall;
                std::printf("FAST_FORWARD_PHASE %d simulated=%.6f wall=%.6f ratio=%.6f\n", phase, simulation, wall, ratio);
                require(std::fabs(ratio - speed) < speed * .15, "measured speed outside 15 percent tolerance");
                key(phase == 0 ? SDL_KEYDOWN : phase == 1 ? SDL_KEYUP : SDL_KEYDOWN);
                ++phase; ticks = 0; simulation = 0;
            }
        } else if (phase == 3) {
            require(pc_window_simulation_speed(gsys->mFrameRate) == 2, "held before settings");
            require(pc_window_simulation_speed(1) == 1 && pc_window_simulation_speed(0) == 1,
                    "held key cannot accelerate 60/120 Hz even with stale presentation clamp");
            pc_window_set_settings_menu_open(true);
            require(pc_window_simulation_speed(gsys->mFrameRate) == 1, "settings cancels hold");
            pc_window_set_settings_menu_open(false);
            require(pc_window_simulation_speed(gsys->mFrameRate) == 1, "settings close does not resume hold");
            key(SDL_KEYDOWN); ++phase;
        } else if (phase == 4) {
            require(pc_window_simulation_speed(gsys->mFrameRate) == 2, "re-press after settings");
            SDL_Event event{}; event.type = SDL_WINDOWEVENT;
            event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            require(SDL_PushEvent(&event) == 1, "queue focus loss");
            ++phase;
        } else {
            require(pc_window_simulation_speed(gsys->mFrameRate) == 1, "focus event cancels hold");
            std::puts("FAST_FORWARD_PASS normal/2x/release/settings/focus; no gameplay claim");
            std::fflush(stdout); std::_Exit(0);
        }
        pc_window_swap_buffers();
        return 0;
    }
};

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!pc_window_init("Fast-forward timing smoke (about 6 seconds)", 640, 360)) return 2;
    SDL_Window* window = SDL_GetWindowFromID(1);
    require(window != nullptr, "test window exists");
    SDL_RaiseWindow(window);
    SDL_SetWindowInputFocus(window);
    gsys->Initialise();
    nodeMgr = new NodeMgr();
    gsys->setFrameClamp(2);
    pc_window_set_swap_interval(2); // Normally supplied by DGXGraphics presentation.
    gsys->run(new TimingApp());
    return 3;
}
