#include "installer_ui.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace pikmin {
namespace launcher {
namespace {

using Glyph = std::array<std::uint8_t, 7>;

const Glyph& glyphFor(char input)
{
    static const std::unordered_map<char, Glyph> glyphs {
        { ' ', { 0, 0, 0, 0, 0, 0, 0 } }, { 'A', { 14, 17, 17, 31, 17, 17, 17 } },
        { 'B', { 30, 17, 17, 30, 17, 17, 30 } }, { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
        { 'D', { 30, 17, 17, 17, 17, 17, 30 } }, { 'E', { 31, 16, 16, 30, 16, 16, 31 } },
        { 'F', { 31, 16, 16, 30, 16, 16, 16 } }, { 'G', { 14, 17, 16, 23, 17, 17, 14 } },
        { 'H', { 17, 17, 17, 31, 17, 17, 17 } }, { 'I', { 31, 4, 4, 4, 4, 4, 31 } },
        { 'J', { 7, 2, 2, 2, 18, 18, 12 } }, { 'K', { 17, 18, 20, 24, 20, 18, 17 } },
        { 'L', { 16, 16, 16, 16, 16, 16, 31 } }, { 'M', { 17, 27, 21, 21, 17, 17, 17 } },
        { 'N', { 17, 25, 21, 19, 17, 17, 17 } }, { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
        { 'P', { 30, 17, 17, 30, 16, 16, 16 } }, { 'Q', { 14, 17, 17, 17, 21, 18, 13 } },
        { 'R', { 30, 17, 17, 30, 20, 18, 17 } }, { 'S', { 15, 16, 16, 14, 1, 1, 30 } },
        { 'T', { 31, 4, 4, 4, 4, 4, 4 } }, { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
        { 'V', { 17, 17, 17, 17, 17, 10, 4 } }, { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
        { 'X', { 17, 17, 10, 4, 10, 17, 17 } }, { 'Y', { 17, 17, 10, 4, 4, 4, 4 } },
        { 'Z', { 31, 1, 2, 4, 8, 16, 31 } }, { '0', { 14, 17, 19, 21, 25, 17, 14 } },
        { 'a', { 0, 0, 14, 1, 15, 17, 15 } }, { 'b', { 16, 16, 30, 17, 17, 17, 30 } },
        { 'c', { 0, 0, 14, 16, 16, 17, 14 } }, { 'd', { 1, 1, 15, 17, 17, 17, 15 } },
        { 'e', { 0, 0, 14, 17, 31, 16, 14 } }, { 'f', { 6, 9, 8, 28, 8, 8, 8 } },
        { 'g', { 0, 0, 15, 17, 15, 1, 14 } }, { 'h', { 16, 16, 30, 17, 17, 17, 17 } },
        { 'i', { 4, 0, 12, 4, 4, 4, 14 } }, { 'j', { 2, 0, 6, 2, 2, 18, 12 } },
        { 'k', { 16, 16, 18, 20, 24, 20, 18 } }, { 'l', { 12, 4, 4, 4, 4, 4, 14 } },
        { 'm', { 0, 0, 26, 21, 21, 17, 17 } }, { 'n', { 0, 0, 30, 17, 17, 17, 17 } },
        { 'o', { 0, 0, 14, 17, 17, 17, 14 } }, { 'p', { 0, 0, 30, 17, 30, 16, 16 } },
        { 'q', { 0, 0, 15, 17, 15, 1, 1 } }, { 'r', { 0, 0, 22, 25, 16, 16, 16 } },
        { 's', { 0, 0, 15, 16, 14, 1, 30 } }, { 't', { 8, 8, 28, 8, 8, 9, 6 } },
        { 'u', { 0, 0, 17, 17, 17, 19, 13 } }, { 'v', { 0, 0, 17, 17, 17, 10, 4 } },
        { 'w', { 0, 0, 17, 17, 21, 21, 10 } }, { 'x', { 0, 0, 17, 10, 4, 10, 17 } },
        { 'y', { 0, 0, 17, 17, 15, 1, 14 } }, { 'z', { 0, 0, 31, 2, 4, 8, 31 } },
        { '1', { 4, 12, 4, 4, 4, 4, 14 } }, { '2', { 14, 17, 1, 2, 4, 8, 31 } },
        { '3', { 30, 1, 1, 14, 1, 1, 30 } }, { '4', { 2, 6, 10, 18, 31, 2, 2 } },
        { '5', { 31, 16, 16, 30, 1, 1, 30 } }, { '6', { 14, 16, 16, 30, 17, 17, 14 } },
        { '7', { 31, 1, 2, 4, 8, 8, 8 } }, { '8', { 14, 17, 17, 14, 17, 17, 14 } },
        { '9', { 14, 17, 17, 15, 1, 1, 14 } }, { '.', { 0, 0, 0, 0, 0, 12, 12 } },
        { ',', { 0, 0, 0, 0, 4, 4, 8 } }, { '/', { 1, 2, 2, 4, 8, 8, 16 } }, { ':', { 0, 12, 12, 0, 12, 12, 0 } },
        { '-', { 0, 0, 0, 31, 0, 0, 0 } }, { '_', { 0, 0, 0, 0, 0, 0, 31 } },
        { '(', { 2, 4, 8, 8, 8, 4, 2 } }, { ')', { 8, 4, 2, 2, 2, 4, 8 } },
        { '%', { 17, 2, 4, 8, 17, 0, 0 } }, { '?', { 14, 17, 1, 2, 4, 0, 4 } }
    };
    auto found = glyphs.find(input);
    if (found != glyphs.end()) return found->second;
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
    found = glyphs.find(upper);
    return found == glyphs.end() ? glyphs.at('?') : found->second;
}

void setColour(SDL_Renderer* renderer, SDL_Color colour)
{
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
}

void fillRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color colour)
{
    setColour(renderer, colour);
    SDL_RenderFillRect(renderer, &rect);
}

void strokeRect(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color colour)
{
    setColour(renderer, colour);
    SDL_RenderDrawRect(renderer, &rect);
}

int textWidth(const std::string& text, int scale)
{
    return static_cast<int>(text.size()) * 6 * scale;
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text,
              int scale, SDL_Color colour)
{
    setColour(renderer, colour);
    for (char character : text) {
        const Glyph& glyph = glyphFor(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph[row] & (1u << (4 - column))) == 0) continue;
                SDL_Rect pixel { x + column * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        x += 6 * scale;
    }
}

bool contains(const SDL_Rect& rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

std::string fitPath(const std::string& path, int width, int scale)
{
    const int capacity = std::max(1, width / (6 * scale));
    if (static_cast<int>(path.size()) <= capacity) return path;
    if (capacity <= 3) return path.substr(path.size() - capacity);
    return "..." + path.substr(path.size() - (capacity - 3));
}

} // namespace

struct InstallerWindow::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::string rom;
    std::string installDirectory;
    std::string status = "Choose your disc image and where to install";
    std::uint32_t progress = 0;
    bool installing = false;

    const SDL_Rect romField { 45, 125, 520, 44 };
    const SDL_Rect romBrowse { 580, 125, 135, 44 };
    const SDL_Rect installField { 45, 218, 520, 44 };
    const SDL_Rect installBrowse { 580, 218, 135, 44 };
    const SDL_Rect installButton { 265, 300, 230, 54 };

    std::uint32_t lastRenderTicks = 0;

    void render()
    {
        if (!renderer) return;
        fillRect(renderer, { 0, 0, 760, 430 }, { 10, 16, 31, 255 });
        fillRect(renderer, { 0, 0, 760, 82 }, { 24, 42, 75, 255 });
        fillRect(renderer, { 0, 78, 760, 4 }, { 116, 185, 92, 255 });

        const std::string title = "Open Nectar Installer";
        drawText(renderer, (760 - textWidth(title, 3)) / 2, 29, title, 3, { 235, 244, 238, 255 });

        drawText(renderer, 45, 101, "Disc image", 2, { 174, 203, 221, 255 });
        drawText(renderer, 45, 194, "Install to", 2, { 174, 203, 221, 255 });

        fillRect(renderer, romField, { 238, 242, 246, 255 });
        strokeRect(renderer, romField, { 96, 121, 151, 255 });
        fillRect(renderer, installField, { 238, 242, 246, 255 });
        strokeRect(renderer, installField, { 96, 121, 151, 255 });
        drawText(renderer, romField.x + 10, romField.y + 14,
                 fitPath(rom.empty() ? "No disc image chosen" : rom, romField.w - 20, 2),
                 2, { 28, 37, 49, 255 });
        drawText(renderer, installField.x + 10, installField.y + 14,
                 fitPath(installDirectory.empty() ? "No folder chosen" : installDirectory,
                         installField.w - 20, 2),
                 2, { 28, 37, 49, 255 });

        int mouseX = 0, mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);
        drawButton(romBrowse, "Browse", contains(romBrowse, mouseX, mouseY));
        drawButton(installBrowse, "Browse", contains(installBrowse, mouseX, mouseY));
        drawButton(installButton, installing ? "Installing" : "Install",
                   contains(installButton, mouseX, mouseY), rom.empty() || installDirectory.empty() || installing);

        drawText(renderer, 45, 279, "In game: F1 opens graphics, controls and gameplay settings", 1,
                 { 180, 197, 214, 255 });

        if (installing) {
            SDL_Rect track { 45, 373, 670, 12 };
            fillRect(renderer, track, { 36, 50, 70, 255 });
            SDL_Rect bar = track;
            bar.w = static_cast<int>(track.w * std::min(progress, 100u) / 100u);
            if (progress > 100) {
                bar.w = 100;
                bar.x += static_cast<int>((SDL_GetTicks() / 8) % (track.w - bar.w));
            }
            fillRect(renderer, bar, { 116, 185, 92, 255 });
        }
        drawText(renderer, 45, 400, fitPath(status, 670, 1), 1, { 180, 197, 214, 255 });
        SDL_RenderPresent(renderer);
    }

    void drawButton(const SDL_Rect& rect, const std::string& label, bool hovered, bool disabled = false)
    {
        SDL_Color background = disabled ? SDL_Color { 56, 65, 78, 255 }
                                       : hovered ? SDL_Color { 137, 205, 106, 255 }
                                                 : SDL_Color { 91, 161, 76, 255 };
        fillRect(renderer, rect, background);
        strokeRect(renderer, rect, { 183, 225, 163, 255 });
        const int scale = rect.w > 150 ? 2 : 1;
        drawText(renderer, rect.x + (rect.w - textWidth(label, scale)) / 2,
                 rect.y + (rect.h - 7 * scale) / 2, label, scale,
                 disabled ? SDL_Color { 139, 148, 158, 255 } : SDL_Color { 8, 24, 12, 255 });
    }

    // Durante la instalación esto se llama una vez por archivo: más de tres mil
    // veces. Redibujar en cada llamada saturaba el hilo y dejaba la ventana sin
    // atender eventos, de modo que el escritorio la marcaba como "no responde"
    // y la instalación parecía colgada. Los eventos se sondean siempre, porque
    // es barato y es lo que mantiene la ventana viva; el redibujado, que es lo
    // caro, se limita a 30 por segundo.
    static constexpr std::uint32_t kRedrawIntervalMs = 33;

    // Redibuja solo si ha pasado el intervalo. Devuelve false si lo ha omitido,
    // para que quien llame sepa que queda un refresco pendiente.
    bool renderThrottled()
    {
        const std::uint32_t now = SDL_GetTicks();
        if (lastRenderTicks != 0 && now - lastRenderTicks < kRedrawIntervalMs) {
            return false;
        }
        lastRenderTicks = now;
        render();
        return true;
    }

    void pump()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) { }
        renderThrottled();
    }
};

InstallerWindow::InstallerWindow()
    : mImpl(std::make_unique<Impl>())
{
}

InstallerWindow::~InstallerWindow()
{
    if (mImpl->renderer) SDL_DestroyRenderer(mImpl->renderer);
    if (mImpl->window) SDL_DestroyWindow(mImpl->window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool InstallerWindow::open(std::string& error)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        error = SDL_GetError();
        return false;
    }
    mImpl->window = SDL_CreateWindow("Open Nectar Installer", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 760, 430, SDL_WINDOW_SHOWN);
    if (!mImpl->window) {
        error = SDL_GetError();
        return false;
    }
    // Sin PRESENTVSYNC: aquí no hay animación que sincronizar, y con vsync cada
    // presentación bloqueaba hasta el siguiente refresco de la pantalla, lo que
    // sumaba cerca de un minuto de espera pura a lo largo de la instalación.
    mImpl->renderer = SDL_CreateRenderer(mImpl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!mImpl->renderer) mImpl->renderer = SDL_CreateRenderer(mImpl->window, -1, SDL_RENDERER_SOFTWARE);
    if (!mImpl->renderer) {
        error = SDL_GetError();
        return false;
    }
    mImpl->render();
    return true;
}

bool InstallerWindow::choosePaths(const std::function<std::string()>& chooseRom,
                                  const std::function<std::string()>& chooseInstallDirectory,
                                  std::string& rom, std::string& installDirectory)
{
    mImpl->installing = false;
    mImpl->progress = 0;
    mImpl->status = "Choose your disc image and where to install";
    mImpl->render();
    SDL_Event event;
    bool pendingRedraw = false;
    for (;;) {
        // Espera con tiempo límite en vez de indefinida: si el ratón se detiene
        // justo después de un refresco omitido, el vencimiento lo dibuja y el
        // resaltado no se queda desfasado.
        if (!SDL_WaitEventTimeout(&event, static_cast<int>(Impl::kRedrawIntervalMs))) {
            if (pendingRedraw && mImpl->renderThrottled()) pendingRedraw = false;
            continue;
        }
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_MOUSEMOTION) {
            // Antes se redibujaba en cada evento de movimiento. Son cientos por
            // segundo, y cada redibujado dibuja el texto punto a punto, así que
            // la ventana se quedaba sin atender eventos y parecía congelada.
            pendingRedraw = !mImpl->renderThrottled();
            continue;
        }
        if (event.type != SDL_MOUSEBUTTONUP || event.button.button != SDL_BUTTON_LEFT) continue;
        const int x = event.button.x;
        const int y = event.button.y;
        if (contains(mImpl->romBrowse, x, y)) {
            const std::string selected = chooseRom();
            if (!selected.empty()) mImpl->rom = selected;
        } else if (contains(mImpl->installBrowse, x, y)) {
            const std::string selected = chooseInstallDirectory();
            if (!selected.empty()) mImpl->installDirectory = selected;
        } else if (contains(mImpl->installButton, x, y)) {
            if (mImpl->rom.empty() || mImpl->installDirectory.empty()) {
                mImpl->status = "Choose a disc image and a folder first";
            } else {
                mImpl->installing = true;
                mImpl->status = "Preparing...";
                rom = mImpl->rom;
                installDirectory = mImpl->installDirectory;
                mImpl->render();
                return true;
            }
        }
        mImpl->lastRenderTicks = SDL_GetTicks();
        mImpl->render();
    }
    return false;
}

void InstallerWindow::updateProgress(std::uint32_t percent, const std::string& currentFile,
                                     const std::string& phase)
{
    mImpl->installing = true;
    mImpl->progress = percent;
    mImpl->status = phase + (percent <= 100 ? " (" + std::to_string(percent) + "%)" : "...");
    if (!currentFile.empty()) {
        const int remaining = 670 - textWidth(mImpl->status + ": ", 1);
        mImpl->status += ": " + fitPath(currentFile, remaining, 1);
    }
    mImpl->pump();
}

bool InstallerWindow::offerRetry(const std::string& message)
{
    mImpl->installing = false;
    mImpl->status = "Installation did not finish";
    mImpl->render();
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Close" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Back to setup" }
    };
    const SDL_MessageBoxData data { SDL_MESSAGEBOX_ERROR, mImpl->window, "Open Nectar Installer",
                                   message.c_str(), 2, buttons, nullptr };
    int selected = 0;
    return SDL_ShowMessageBox(&data, &selected) == 0 && selected == 1;
}

void InstallerWindow::showError(const std::string& message)
{
    mImpl->installing = false;
    mImpl->status = "Installation did not finish";
    mImpl->render();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open Nectar Installer", message.c_str(), mImpl->window);
}

void InstallerWindow::showComplete(const std::string& installDirectory, bool willLaunch)
{
    mImpl->progress = 100;
    mImpl->status = "Installation complete";
    mImpl->render();
    std::string message = "Open Nectar is installed in:\n" + installDirectory;
    message += "\n\nF1 opens graphics, controls and gameplay settings."
               "\nRun nectar-launcher from this folder to play again.";
    if (willLaunch) message += "\n\nThe game will start now.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Installation complete", message.c_str(), mImpl->window);
}

} // namespace launcher
} // namespace pikmin
