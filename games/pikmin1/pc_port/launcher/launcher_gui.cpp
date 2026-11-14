// El online está apagado en todo el proyecto (pc_online_switch.h): sin
// botones de co-op/VS online aunque el launcher se compile con ellos.
#include "../netplay/pc_online_switch.h"
#if !PC_ONLINE_ENABLED
#undef PIKMIN_LAUNCHER_NETPLAY
#endif

// Ventana principal del launcher: Dear ImGui sobre SDL2 + OpenGL 3.
//
// Estética: fondo verde Pikmin, el logo de Open Nectar arriba y bordes y letras
// en el naranja de su rótulo. Solo el logo y la fuente van dentro del
// ejecutable. Todo lo que es de Nintendo llega de fuera, como en Dolphin:
//  - las portadas se descargan de GameTDB y se guardan en caché;
//  - la decoración (paisaje, fotos del diario de Olimar, burbujas de Pikmin,
//    botones de cristal) se lee de los datos que el usuario extrajo de su
//    propio disco. Sin instalación se dibuja una pradera con flores.
#include "launcher_gui.h"

#include "bti_decode.h"
#include "button_layout.h"
#include "settings_row_layout.h"
#include "button_style.h"
#if PIKMIN_LAUNCHER_NETPLAY
#include "launcher_coop.h"
#endif
#include "gci_save_import.h"
#include "install_directory.h"
#include "install_disc_id.h"
#include "install_note_layout.h"
#include "dolphin_stubs/pc_card_save_root.h"
#include "launcher_cursor.h"
#include "launcher_nav.h"
#include "launcher_platform.h"
#include "launcher_page_layout.h"
#include "launcher_sidebar_layout.h"
#include "launcher_scroll.h"
#include "settings_model.h"
#include "toast_layout.h"
#include "texture_pack_row.h"
#include "texture_pack_download.h"
#include "texture_pack_storage.h"
#include "hd_model_downloads.h"
#include "release_update.h"
#include "version_label.h"

#include "imgui.h"
#include "imgui_internal.h" // cola de eventos: escalar la posición del ratón
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <SDL2/SDL_opengl.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "font_fredoka.h"
#include "logo_open_nectar.h"
#include "font_game_bubble.h"

#include "../pc_icon.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

using buttons::ButtonSize;
namespace role = buttons::role;

// ── Paleta ──────────────────────────────────────────────────────────────────
// Verde de hoja de Pikmin como color dominante; naranja del rótulo del logo
// para bordes y letras.
const ImU32 kBgTop       = IM_COL32(9, 34, 14, 255);
const ImU32 kBgBottom    = IM_COL32(28, 84, 36, 255);
const ImVec4 kOrange     = ImVec4(1.00f, 0.63f, 0.16f, 1.00f); // ~#FFA029, el brillo del rótulo
const ImVec4 kOrangeDeep = ImVec4(0.94f, 0.53f, 0.08f, 1.00f); // ~#F08714, el cuerpo del rótulo
const ImVec4 kLeaf       = ImVec4(0.24f, 0.60f, 0.26f, 1.00f);
const ImVec4 kLeafBright = ImVec4(0.36f, 0.76f, 0.34f, 1.00f);
const ImVec4 kLeafDark   = ImVec4(0.13f, 0.38f, 0.16f, 1.00f);
// Grosor del borde de todos los controles: botones dibujados a mano y
// desplegables de ImGui (FrameBorderSize).
constexpr float kControlBorder = 2.0f;

ImU32 col(const ImVec4& c, float alpha = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

float approach(float value, float target, float dt, float speed)
{
    const float k = 1.0f - std::exp(-speed * dt);
    return value + (target - value) * k;
}

// ── Texturas ────────────────────────────────────────────────────────────────
struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    bool valid() const { return id != 0; }
    ImTextureID imgui() const { return (ImTextureID)(intptr_t)id; }
    float aspect() const { return height ? float(width) / float(height) : 1.0f; }
};

// Reduce a la mitad con un filtro de caja 2x2. Sin mipmaps, bajar en la CPU
// las imágenes grandes evita el dentado al pintarlas mucho más pequeñas.
void halve(std::vector<unsigned char>& pixels, int& width, int& height)
{
    const int w = width / 2, h = height / 2;
    std::vector<unsigned char> out(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](int sx, int sy) { return int(pixels[(size_t(sy) * width + sx) * 4 + c]); };
                out[(size_t(y) * w + x) * 4 + c]
                    = (unsigned char)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
            }
        }
    }
    pixels.swap(out);
    width = w;
    height = h;
}

Texture uploadTexture(std::vector<unsigned char> pixels, int w, int h, int maxHeight)
{
    Texture tex;
    if (pixels.empty() || w <= 0 || h <= 0) return tex;
    while (maxHeight > 0 && h > maxHeight * 2 && w > 2 && h > 2) halve(pixels, w, h);
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    tex.width = w;
    tex.height = h;
    return tex;
}

// PNG o JPEG en memoria.
Texture loadImageTexture(const unsigned char* data, std::size_t size, int maxHeight)
{
    int w = 0, h = 0, channels = 0;
    unsigned char* decoded = stbi_load_from_memory(data, int(size), &w, &h, &channels, 4);
    if (!decoded) return Texture();
    std::vector<unsigned char> pixels(decoded, decoded + size_t(w) * h * 4);
    stbi_image_free(decoded);
    return uploadTexture(std::move(pixels), w, h, maxHeight);
}

// Textura BTI del juego instalado.
Texture loadBtiTexture(const fs::path& path)
{
    RgbaImage image;
    if (!loadBtiFile(path.string(), image)) return Texture();
    return uploadTexture(std::move(image.pixels), image.width, image.height, 0);
}

void freeTexture(Texture& tex)
{
    if (tex.id) glDeleteTextures(1, &tex.id);
    tex = Texture();
}

// ── Portadas, como en Dolphin ───────────────────────────────────────────────
// GameTDB sirve la caja entera en alta resolución (coverfullHQ, 1024x680); la
// portada es la mitad derecha, a la derecha del lomo. Si no está, se usa la
// portada pequeña (cover, 160x224). Lo descargado queda en la caché del
// launcher y no se vuelve a pedir.
struct RemoteSource {
    std::string url;
    std::string cacheName; // nombre del fichero en la caché del launcher
    bool full = false;     // true = caja completa de GameTDB (recortar la portada)
};

struct RemoteImage {
    std::vector<RemoteSource> sources; // se prueban en orden
    std::mutex mutex;
    std::vector<unsigned char> bytes; // fichero descargado, listo para decodificar
    bool full = false;                 // true = caja completa (recortar la portada)
    std::atomic<bool> ready { false };
    std::atomic<bool> failed { false };
};

constexpr float kFrontCoverStart = 0.525f; // fracción del ancho donde empieza la portada en coverfullHQ

bool readFile(const fs::path& path, std::vector<unsigned char>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

void fetchRemote(std::shared_ptr<RemoteImage> request)
{
    const fs::path dir = platform::cacheDirectory() / "art";
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    for (const RemoteSource& source : request->sources) {
        const fs::path cached = dir / source.cacheName;
        std::vector<unsigned char> bytes;
        if (!readFile(cached, bytes)) {
            std::string error;
            if (!platform::downloadFile(source.url, cached, error) || !readFile(cached, bytes)) continue;
        }
        std::lock_guard<std::mutex> lock(request->mutex);
        request->bytes = std::move(bytes);
        request->full = source.full;
        request->ready = true;
        return;
    }
    request->failed = true;
}

// ── Decoración del juego instalado ──────────────────────────────────────────
struct GameArt {
    bool loaded = false;
    Texture worldMap;          // w_map: paisaje del mapa del mundo
    std::vector<Texture> logs; // *_snap: fotos de zona del diario de Olimar
    Texture sparkle;           // sun_64: destello

    void load(const std::string& dataDirectory)
    {
        if (dataDirectory.empty()) return;
        fs::path tex = fs::path(dataDirectory) / "screen" / "eng_tex";
        if (!fs::is_directory(tex)) tex = fs::path(dataDirectory) / "screen" / "otona_tex";
        if (!fs::is_directory(tex)) return;
        const auto bti = [&](const char* name) { return loadBtiTexture(tex / (std::string(name) + ".bti")); };
        worldMap = bti("w_map");
        for (const char* name : { "fo_snap", "ya_snap", "ga_snap", "pr_snap", "ca_snap" }) {
            Texture t = bti(name);
            if (t.valid()) logs.push_back(t);
        }
        sparkle = bti("sun_64");
        loaded = worldMap.valid() || !logs.empty();
    }

    void release()
    {
        freeTexture(worldMap);
        for (Texture& t : logs) freeTexture(t);
        freeTexture(sparkle);
    }
};

// ── Tema ────────────────────────────────────────────────────────────────────
void applyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 18.0f;
    s.ChildRounding = 16.0f;
    s.FrameRounding = 14.0f;
    s.PopupRounding = 14.0f;
    s.GrabRounding = 14.0f;
    s.TabRounding = 14.0f;
    s.ScrollbarRounding = 14.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 2.0f;
    s.FrameBorderSize = kControlBorder;
    s.PopupBorderSize = 2.0f;
    s.FramePadding = ImVec2(16.0f, 9.0f);
    s.ItemSpacing = ImVec2(12.0f, 12.0f);
    s.WindowPadding = ImVec2(20.0f, 18.0f);
    s.ScrollbarSize = 14.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = kOrange;
    c[ImGuiCol_TextDisabled]         = ImVec4(0.62f, 0.58f, 0.44f, 1.0f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.04f, 0.14f, 0.06f, 0.0f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.03f, 0.12f, 0.05f, 0.78f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.04f, 0.15f, 0.06f, 0.97f);
    c[ImGuiCol_Border]               = ImVec4(kOrangeDeep.x, kOrangeDeep.y, kOrangeDeep.z, 0.85f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = ImVec4(0.06f, 0.22f, 0.09f, 0.90f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.10f, 0.32f, 0.13f, 0.95f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.12f, 0.38f, 0.15f, 1.00f);
    c[ImGuiCol_Button]               = kLeaf;
    c[ImGuiCol_ButtonHovered]        = kLeafBright;
    c[ImGuiCol_ButtonActive]         = kLeafDark;
    c[ImGuiCol_Header]               = ImVec4(kLeaf.x, kLeaf.y, kLeaf.z, 0.70f);
    c[ImGuiCol_HeaderHovered]        = kLeafBright;
    c[ImGuiCol_HeaderActive]         = kLeafDark;
    c[ImGuiCol_CheckMark]            = kOrange;
    c[ImGuiCol_SliderGrab]           = kOrangeDeep;
    c[ImGuiCol_SliderGrabActive]     = kOrange;
    c[ImGuiCol_Separator]            = ImVec4(kOrangeDeep.x, kOrangeDeep.y, kOrangeDeep.z, 0.45f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0.15f);
    c[ImGuiCol_ScrollbarGrab]        = kLeaf;
    c[ImGuiCol_ScrollbarGrabHovered] = kLeafBright;
    c[ImGuiCol_ScrollbarGrabActive]  = kLeafDark;
    c[ImGuiCol_Tab]                  = ImVec4(kLeaf.x, kLeaf.y, kLeaf.z, 0.55f);
    c[ImGuiCol_TabHovered]           = kLeafBright;
    c[ImGuiCol_TabSelected]          = kLeaf;
    c[ImGuiCol_NavCursor]            = kOrange;
}

// Generador determinista: el fondo es el mismo en cada arranque.
struct Lcg {
    uint32_t state;
    float next() { state = state * 1664525u + 1013904223u; return float(state >> 8) / 16777216.0f; }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }
};

void drawFlower(ImDrawList* dl, ImVec2 c, float r, int petals, ImU32 petal, ImU32 heart, float spin)
{
    for (int i = 0; i < petals; ++i) {
        const float a = spin + float(i) * 6.2831853f / float(petals);
        const ImVec2 at(c.x + std::cos(a) * r * 0.62f, c.y + std::sin(a) * r * 0.62f);
        dl->AddEllipseFilled(at, ImVec2(r * 0.55f, r * 0.30f), petal, a, 16);
    }
    dl->AddCircleFilled(c, r * 0.34f, heart, 16);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.08f, c.y - r * 0.08f), r * 0.14f, IM_COL32(255, 236, 150, 255), 12);
}

// ── Fondo: capturas del juego con fundidos y movimiento lento ───────────────
// Cada imagen ocupa toda la ventana y se acerca o se aleja despacio mientras
// se desplaza (efecto Ken Burns); la siguiente entra con un fundido cruzado.
struct SlideMotion {
    float zoomFrom = 1.08f, zoomTo = 1.18f;
    ImVec2 panFrom { -0.6f, 0.0f }, panTo { 0.6f, 0.0f }; // -1..1 del margen que deja el zoom
};

struct Slideshow {
    static constexpr float kShow = 10.0f; // segundos por imagen, fundido incluido
    static constexpr float kFade = 2.5f;

    std::vector<Texture> slides;
    int current = 0;
    float clock = 0.0f;
    SlideMotion now, next;
    Lcg rng { 0x5EEDu };

    SlideMotion randomMotion()
    {
        SlideMotion m;
        const bool zoomIn = rng.next() < 0.5f;
        m.zoomFrom = zoomIn ? rng.range(1.04f, 1.10f) : rng.range(1.16f, 1.24f);
        m.zoomTo = zoomIn ? rng.range(1.16f, 1.24f) : rng.range(1.04f, 1.10f);
        const float angle = rng.range(0.0f, 6.2831853f);
        m.panFrom = ImVec2(std::cos(angle) * 0.8f, std::sin(angle) * 0.8f);
        m.panTo = ImVec2(-m.panFrom.x, -m.panFrom.y);
        return m;
    }

    void add(Texture slide)
    {
        if (slides.empty()) {
            now = randomMotion();
            next = randomMotion();
            clock = kFade; // la primera aparece ya visible
        }
        slides.push_back(slide);
    }

    static float ease(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static void drawSlide(ImDrawList* dl, const Texture& tex, ImVec2 size, float time, float alpha, const SlideMotion& m)
    {
        if (alpha <= 0.0f) return;
        const float p = ease(time / (kShow + kFade));
        const float zoom = m.zoomFrom + (m.zoomTo - m.zoomFrom) * p;
        const ImVec2 pan(m.panFrom.x + (m.panTo.x - m.panFrom.x) * p, m.panFrom.y + (m.panTo.y - m.panFrom.y) * p);
        // Ventana de UV que cubre la pantalla sin deformar, reducida por el zoom.
        const float screenAspect = size.x / size.y;
        float uw = 1.0f, uh = 1.0f;
        if (tex.aspect() > screenAspect) uw = screenAspect / tex.aspect();
        else uh = tex.aspect() / screenAspect;
        uw /= zoom;
        uh /= zoom;
        const float cx = 0.5f + pan.x * (0.5f - uw * 0.5f);
        const float cy = 0.5f + pan.y * (0.5f - uh * 0.5f);
        dl->AddImage(tex.imgui(), ImVec2(0, 0), size, ImVec2(cx - uw * 0.5f, cy - uh * 0.5f),
                     ImVec2(cx + uw * 0.5f, cy + uh * 0.5f), IM_COL32(255, 255, 255, int(255 * alpha)));
    }

    void draw(ImDrawList* dl, ImVec2 size, float dt)
    {
        if (slides.empty()) return;
        clock += dt;
        if (clock >= kShow) {
            clock -= kShow - kFade; // la siguiente lleva ya kFade segundos en pantalla
            current = (current + 1) % int(slides.size());
            now = next;
            next = randomMotion();
        }
        drawSlide(dl, slides[size_t(current)], size, clock, 1.0f, now);
        const float fadeStart = kShow - kFade;
        if (clock > fadeStart) {
            const int following = (current + 1) % int(slides.size());
            drawSlide(dl, slides[size_t(following)], size, clock - fadeStart, ease((clock - fadeStart) / kFade), next);
        }
    }

    void release()
    {
        for (Texture& slide : slides) freeTexture(slide);
        slides.clear();
    }
};

ImVec2 rotate(ImVec2 v, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return ImVec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

// Quad girado alrededor de `centre`, del tamaño `half` * 2.
void rotatedQuad(ImVec2 centre, ImVec2 half, float angle, ImVec2 out[4])
{
    const ImVec2 corners[4] = { { -half.x, -half.y }, { half.x, -half.y }, { half.x, half.y }, { -half.x, half.y } };
    for (int i = 0; i < 4; ++i) {
        const ImVec2 r = rotate(corners[i], angle);
        out[i] = ImVec2(centre.x + r.x, centre.y + r.y);
    }
}

// ── Portadas ────────────────────────────────────────────────────────────────
struct CardState {
    float hover = 0.0f; // 0..1, animado
};

// Recorta la portada al aspecto de la tarjeta sin deformarla. `u0` es el borde
// izquierdo de la parte útil (la portada dentro de la caja completa).
void coverUv(const Texture& tex, float u0, float cardAspect, ImVec2& uv0, ImVec2& uv1)
{
    const float texAspect = tex.aspect() * (1.0f - u0);
    uv0 = ImVec2(u0, 0);
    uv1 = ImVec2(1, 1);
    if (texAspect > cardAspect) {
        const float keep = (1.0f - u0) * cardAspect / texAspect;
        uv0.x = u0 + ((1.0f - u0) - keep) * 0.5f;
        uv1.x = uv0.x + keep;
    } else if (texAspect < cardAspect) {
        const float keep = texAspect / cardAspect;
        uv0.y = (1.0f - keep) * 0.5f;
        uv1.y = 1.0f - uv0.y;
    }
}

// Recorta una ruta por delante ("...carpeta/fichero.iso") para que quepa.
std::string fitTail(ImFont* font, float size, const std::string& text, float width)
{
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= width) return text;
    for (std::size_t cut = 1; cut < text.size(); ++cut) {
        const std::string candidate = "..." + text.substr(cut);
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= width) return candidate;
    }
    return "...";
}

void centredText(ImDrawList* dl, ImFont* font, float size, ImVec2 centre, ImU32 colour, const char* text)
{
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f), colour, text);
}

// Etiqueta en píldora (PLAY, Not installed...) sobre la parte baja de la portada.
void badge(ImDrawList* dl, ImFont* font, float size, ImVec2 centre, ImU32 fill, ImU32 text, const char* label)
{
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
    const ImVec2 pad(size * 0.9f, size * 0.35f);
    const ImVec2 a(centre.x - extent.x * 0.5f - pad.x, centre.y - extent.y * 0.5f - pad.y);
    const ImVec2 b(centre.x + extent.x * 0.5f + pad.x, centre.y + extent.y * 0.5f + pad.y);
    dl->AddRectFilled(a, b, fill, (b.y - a.y) * 0.5f);
    dl->AddRect(a, b, col(kOrange), (b.y - a.y) * 0.5f, 0, 2.0f);
    dl->AddText(font, size, ImVec2(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f), text, label);
}

// ── Ajustes del juego ───────────────────────────────────────────────────────
// El launcher no sabe nada de los ajustes: se los pide al juego
// (nectar --settings-dump) y le manda cada cambio (--settings-set), que los
// guarda en pikmin_settings.conf igual que el menú F1.
struct SettingsClient {
    std::string game;      // ejecutable del juego instalado
    std::string directory; // carpeta de la instalación (allí está el .conf)
    std::mutex mutex;
    SettingsSnapshot snapshot;
    std::string error;
    std::string busyLabel = "Saving..."; // lo que se enseña mientras trabaja
    std::atomic<bool> busy { false };
    bool loaded = false;
    std::string downloadingPack; // guarded by mutex, along with snapshot
    bool packResultReady = false;

    // Ejecuta el juego con esos argumentos y se queda con su respuesta.
    static void request(std::shared_ptr<SettingsClient> self, std::vector<std::string> arguments)
    {
        std::string output, failure;
        SettingsSnapshot parsed;
        const bool executed = platform::runAndCapture(self->game, arguments, self->directory, output, failure);
        // A rejected install/delete still returns a settings snapshot with a
        // useful error message. Do not replace it with "exit status 1".
        std::string parseFailure;
        const bool ok = parseSettingsDump(output, parsed, parseFailure);
        if (!ok && executed) failure = parseFailure;
        std::lock_guard<std::mutex> lock(self->mutex);
        if (ok) {
            self->snapshot = std::move(parsed);
            self->error.clear();
            self->loaded = true;
        } else {
            self->error = "Could not read the game's settings (" + failure + ").";
        }
        self->busy = false;
    }

    static void installPack(std::shared_ptr<SettingsClient> self, TexturePackInfo pack, TexturePackDownload download)
    {
        if (self->busy.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->downloadingPack = pack.id;
            self->packResultReady = false;
            self->busyLabel = "Downloading " + pack.name + "...";
            self->snapshot.message.clear();
        }
        std::thread([self, pack, download] {
            std::string failure;
            const bool ok = downloadTexturePack(download, self->directory, pack.id,
                [self](const fs::path& zip, const std::string& variant, std::string& error) {
                    std::string output;
                    const bool executed = platform::runAndCapture(self->game,
                        { "--texpack-install", zip.string(), variant }, self->directory, output, error);
                    SettingsSnapshot result;
                    std::string parseError;
                    if (parseSettingsDump(output, result, parseError) && result.messageIsError) {
                        error = result.message; return false;
                    }
                    return executed;
                }, platform::downloadFile,
                [self, pack](const std::string& phase) {
                    std::lock_guard<std::mutex> lock(self->mutex);
                    self->busyLabel = phase + " " + pack.name + "...";
                }, failure);
            // Refresh off-thread, without clearing the busy guard between the
            // transfer and refresh. A second click can never start another job.
            std::string output, refreshError, parseError;
            SettingsSnapshot refreshed;
            const bool read = platform::runAndCapture(self->game, { "--settings-dump" },
                self->directory, output, refreshError) && parseSettingsDump(output, refreshed, parseError);
            std::lock_guard<std::mutex> lock(self->mutex);
            if (read) self->snapshot = std::move(refreshed);
            self->snapshot.messageIsError = !ok;
            if (ok) {
                std::string size;
                for (const auto& installed : self->snapshot.texturePackDetails)
                    if (installed.id == pack.id && installed.installed) size = installed.size;
                self->snapshot.message = "Installed " + pack.name + (size.empty() ? "" : " (" + size + ")") + " to " + pack.path;
                if (!read) self->snapshot.message += ". Refresh failed; reopen Settings.";
            } else self->snapshot.message = "Could not install " + pack.name + ": " + failure;
            self->downloadingPack.clear();
            self->packResultReady = true;
            self->busy = false;
        }).detach();
    }

    static void start(std::shared_ptr<SettingsClient> self, std::vector<std::string> arguments,
                      const char* busyLabel = "Saving...")
    {
        if (self->busy.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->busyLabel = busyLabel;
            self->snapshot.message.clear();
        }
        std::thread(request, self, std::move(arguments)).detach();
    }
};

// ── Mover la instalación ────────────────────────────────────────────────────
// En el mismo disco basta con renombrar la carpeta. Entre discos se copia
// fichero a fichero y el launcher nuevo borra la carpeta vieja al arrancar
// (--moved-from): en Windows no se puede mientras este siga abierto.
struct MoveJob {
    enum State { Idle, Running, Done, Failed };
    std::atomic<int> state { Idle };
    std::atomic<float> progress { 0.0f };
    std::mutex mutex;
    std::string error;
    bool copied = false;

    static void run(std::shared_ptr<MoveJob> job, fs::path from, fs::path to)
    {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (!ec) {
            job->progress = 1.0f;
            job->state = Done;
            return;
        }
        // Otro disco (o Windows con la carpeta en uso): copiar.
        std::size_t total = 0, done = 0;
        for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            ++total;
        const auto fail = [&](const std::string& message) {
            std::error_code ignored;
            fs::remove_all(to, ignored);
            std::lock_guard<std::mutex> lock(job->mutex);
            job->error = message;
            job->state = Failed;
        };
        if (ec) return fail("Could not read the install folder: " + ec.message());
        fs::create_directories(to, ec);
        if (ec) return fail("Could not create the new folder: " + ec.message());
        for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path target = to / fs::relative(it->path(), from);
            if (it->is_symlink()) fs::copy_symlink(it->path(), target, ec);
            else if (it->is_directory()) fs::create_directories(target, ec);
            else fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("Could not copy " + it->path().filename().string() + ": " + ec.message());
            job->progress = total ? float(++done) / float(total) : 1.0f;
        }
        if (ec) return fail("Could not copy the install folder: " + ec.message());
        job->copied = true;
        job->state = Done;
    }
};

// ── ¿Hay versión nueva? ─────────────────────────────────────────────────────
// Se pregunta a GitHub en segundo plano al abrir el launcher instalado.
struct ReleaseCheck {
    std::mutex mutex;
    ReleaseInfo latest;
    std::atomic<bool> done { false };
    bool newer = false;

    static void run(std::shared_ptr<ReleaseCheck> self)
    {
        ReleaseInfo info;
        std::string error;
        const bool ok = fetchLatestRelease(info, error);
        std::lock_guard<std::mutex> lock(self->mutex);
        self->latest = info;
        self->newer = ok && isNewerVersion(info.version, currentVersion());
        self->done = true;
    }
};

struct Fonts {
    ImFont* body = nullptr;
    ImFont* title = nullptr;
    ImFont* caption = nullptr;
};

enum class Tab { Games, Settings, Install };

// The joystick subsystem is up, so ImGui may open a controller. Mouse and
// keyboard navigation stay as they were: only the gamepad flag is set.
bool gGamepadSubsystem = false;

void noteGamepadSubsystem(bool on) { gGamepadSubsystem = on; }

// `on` opens the first controller for ImGui nav. `off` closes it and ignores
// further pad input, so a game started beside the launcher can open the device.
// Only the co-op child keeps the launcher process alive; a normal Play execs.
#if PIKMIN_LAUNCHER_NETPLAY
void setGamepadNavigation(bool on)
{
    if (!gGamepadSubsystem || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().BackendPlatformUserData == nullptr)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (on) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoFirst, nullptr, 0);
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);
    }
}
#endif

// B (SDL_CONTROLLER_BUTTON_A is FaceDown; B is FaceRight).
bool gamepadBackPressed() { return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight); }

// True on the item just submitted while the gamepad focus ring is showing.
// A mouse click hides that ring, so this stays false for mouse use.
bool gamepadItemFocused()
{
    const ImGuiIO& io = ImGui::GetIO();
    return io.NavVisible && ImGui::IsItemFocused();
}

void drawGamepadFocusRing(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding)
{
    dl->AddRect(ImVec2(a.x - 4.0f, a.y - 4.0f), ImVec2(b.x + 4.0f, b.y + 4.0f), col(kOrange), rounding, 0, 3.0f);
}

struct Hub {
    const HubState& state;
    HubResult& result;
    Fonts fonts;
    Texture logo;
    // Versión junto al logo, ya pintada (se rehace si cambia de tamaño).
    Texture versionTex;
    VersionLabelImage versionImage;
    std::string versionText;
    float versionInk = 0.0f;
    GameArt art;
    Texture covers[2];
    float coverStart[2] = { 0.0f, 0.0f };
    std::shared_ptr<RemoteImage> coverRequests[2];
    std::vector<std::shared_ptr<RemoteImage>> slideRequests;
    Slideshow slideshow;
    CardState cards[2];
    Tab tab = Tab::Games;
    // Ajustes de cada juego, cada uno contra su propio ejecutable; `settings`
    // apunta al del juego elegido arriba de la pestaña.
    std::shared_ptr<ReleaseCheck> releaseCheck;
    // Panel "What's new": las notas del release nuevo, copiadas al abrirlo.
    bool whatsNewOpen = false;
    std::string whatsNewVersion, whatsNewNotes;
    float whatsNewNotesH = -1.0f; // alto de las notas del fotograma anterior
    std::shared_ptr<SettingsClient> settingsClients[kHubGameCount];
    std::shared_ptr<SettingsClient> settings;
    int settingsGame = 0; // kHubGameCount = the "Launcher" page
    std::string launcherMessage; // result of the last switch on that page
    bool launcherMessageIsError = false;
    SettingsFolderInfo launcherInfo;
    bool launcherInfoFresh = false;
    int settingsGroup = 0;
    // Page that asked for the request whose result is in snap.message: the
    // result only shows on that page, so it does not follow you to another.
    int messageGame = -1;
    int messageGroup = -1;
    int pickGame = -1, pickGroup = -1; // page that opened the file pick
    // Última altura y página con las que se ajustó el scroll de la lista de
    // páginas; al cambiar cualquiera se vuelve a dejar la página elegida a la vista.
    float pagesShownView = -1.0f;
    int pagesShownGroup = -1;
    // Orden de las opciones tal como se vieron la primera vez: el juego lista
    // las cíclicas empezando por el valor actual, y el desplegable no debe
    // reordenarse cada vez que se cambia algo.
    std::map<std::pair<int, int>, std::vector<std::string>> optionOrder;
    float resetArmed = 0.0f; // segundos que queda activa la confirmación de Reset
    // Rueda suavizada de las listas con scroll (Ajustes, novedades, co-op).
    SmoothScroll rowsScroll, notesScroll, coopScroll, launcherRowsScroll;
    // Petición al juego apuntada mientras se dibuja con el candado de los
    // ajustes cogido; se lanza al soltarlo (start también lo coge).
    std::vector<std::string> pendingRequest;
    const char* pendingLabel = "Saving...";
    std::string moveTarget;  // carpeta nueva elegida
    std::string moveNote;    // aviso bajo el formulario
    std::shared_ptr<MoveJob> moveJob;
    TexturePackInfo deletingPack;
    HdModel deletingModel;
    // Set by an Uninstall button; drawDeleteHdModel opens the confirm at its
    // own ID level (the buttons sit under PushID/child windows).
    bool uninstallPending = false;
#ifndef __ANDROID__
    // Get HD models (#189): see drawGetHdModels.
    struct HdJob {
        fs::path path;
        bool fromDownloads = false;
        enum Stage { Identify, Install } stage = Identify;
        int row = -1;
    };
    struct HdZipOffer {
        fs::path zip;
        std::string size;
    };
    DownloadsWatcher hdWatcher;
    float hdPollClock = 0.0f;
    std::vector<HdJob> hdFound;        // zips still to check, oldest first
    std::optional<HdJob> hdJob;        // the one the game is working on
    std::set<int> hdWaitingRows;       // pages opened, zip not in yet
    std::map<int, std::string> hdRowErrors;
    std::optional<HdZipOffer> hdZipOffer; // "delete the downloaded zip?"
    std::string hdGetNote;
    bool hdGetNoteError = false;
#endif
    TexturePackInfo installingPack;
    TexturePackDownload installingDownload {};
    std::optional<std::uint64_t> installFreeSpace;
    TexturePackStorage installStorage = TexturePackStorage::Unknown;
    std::string pendingPackInstall;
    bool awaitingPackResult = false;
    std::string toast;
    float toastTime = 0.0f;
    bool chosen = false;
    bool coopOpen = false;
#ifndef __ANDROID__
    std::function<int(const std::string&, const std::string&, const std::string&, const std::string&, bool,
                      const std::string&)>
        askModal;
    // Saves page: result of the last import (shown in the help box) and what
    // is on this game's memory card, re-read every couple of seconds.
    std::string gciNote;
    bool gciNoteIsError = false;
    std::string savesStatus;
    bool savesFolderExists = false;
    bool backupsFolderExists = false;
    int savesStatusGame = -1;
    float savesStatusAge = 0.0f;
    std::string savesBackupTime; // newest backup, "Oct 25, 8:37 PM"; empty when none
#endif
    // Work that may open a modal (which runs its own frame loop), queued from
    // inside a frame and run by HubWindow between frames.
    std::function<void()> betweenFrames;

    // A file or folder picker runs on its own thread so the window keeps
    // drawing while the dialog is open. The worker only calls the platform
    // dialog and writes `path`; everything else stays on the main thread. The
    // thread holds its own reference, so it may outlive the hub if the
    // launcher quits (HubWindow then closes the dialog, see ~Impl).
    struct FilePick {
        std::atomic<bool> done { false };
        fs::path path; // written by the worker before `done`
    };
    std::shared_ptr<FilePick> filePick;
    std::string filePickFor; // which button opened it, for its label
    std::function<void(const fs::path&)> filePickThen;

    // ── Botones "gota de rocío" (button_style.h) ───────────────────────────
    // Cada aspecto se hornea una vez por tamaño y escala de pantalla; cada
    // botón guarda su animación y, solo mientras se anima, una textura con la
    // mezcla del fotograma.
    using DewdropKey = std::tuple<int, int, int>; // ancho y alto (cuartos de unidad), escala (%)
    struct DewdropSet {
        float width = 0, height = 0, scale = 1;
        dewdrop::Bitmap bitmaps[dewdrop::kLookCount];
        Texture textures[dewdrop::kLookCount];
        unsigned wanted = 0; // aspectos que conviene tener ya horneados
    };
    struct DewdropButton {
        dewdrop::Anim anim;
        DewdropKey key;
        Texture blend;
        dewdrop::Bitmap scratch;
        int lastFrame = -1;
    };
    std::map<DewdropKey, DewdropSet> dewdropSets;
    std::map<ImGuiID, DewdropButton> dewdropButtons;
    Texture dewdropSparkle;
    float dewdropScale = 0.0f;
    int dewdropWarmFrame = -1;

    Hub(const HubState& s, HubResult& r) : state(s), result(r) {}

    bool filePickOpen() const { return filePick != nullptr; }

    std::string filePickLabel(const std::string& owner, const char* label,
                              const char* waiting = "Waiting for file picker...") const
    {
        return filePickFor == owner ? waiting : label;
    }

    // `dialog` runs on the worker, so it must only call the platform dialog
    // with values it owns (no references into the hub).
    void startPick(std::string owner, std::function<fs::path()> dialog, std::function<void(const fs::path&)> then)
    {
        if (filePick) return;
        messageGroup = -1; // a new pick hides the last result
        pickGame = settingsGame;
        pickGroup = settingsGroup;
        filePick = std::make_shared<FilePick>();
        filePickFor = std::move(owner);
        filePickThen = std::move(then);
        std::thread([pick = filePick, dialog = std::move(dialog)] {
            pick->path = dialog();
            pick->done.store(true, std::memory_order_release);
        }).detach();
    }

    void startFilePick(std::string owner, std::string title, std::string filterName, std::string patterns,
                       std::function<void(const fs::path&)> then)
    {
        startPick(std::move(owner),
                  [title = std::move(title), filterName = std::move(filterName), patterns = std::move(patterns)] {
                      return platform::askForFile(title, filterName, patterns);
                  },
                  std::move(then));
    }

    // Called by HubWindow between frames, never inside hub->frame(): the
    // continuation may open a modal, which runs its own frame loop.
    void finishFilePick()
    {
        if (!filePick || !filePick->done.load(std::memory_order_acquire)) return;
        const fs::path picked = filePick->path;
        std::function<void(const fs::path&)> then = std::move(filePickThen);
        filePick.reset();
        filePickFor.clear();
        filePickThen = nullptr;
        if (!picked.empty() && then) then(picked);
        // The request the pick queued answers the page that opened it, even
        // if another page is showing now.
        if (!pendingRequest.empty()) {
            messageGame = pickGame;
            messageGroup = pickGroup;
        }
    }

    void runBetweenFrames()
    {
        std::function<void()> work = std::move(betweenFrames);
        betweenFrames = nullptr;
        if (work) work();
    }

#if PIKMIN_LAUNCHER_NETPLAY
    // Online co-op panel (netplay builds of the game): the game runs as a
    // child while the panel shows and takes the connection codes.
    platform::GameProcess coopProcess;
    bool gamepadHeldByGame = false; // the child has the controller; nav is off
    std::unique_ptr<CoopFlow> coop;
    std::vector<char> coopInput = std::vector<char>(32768, '\0');
    std::string coopError;

    CoopFlow& coopFlow()
    {
        if (!coop) {
            coop = std::make_unique<CoopFlow>(
                [this](const std::vector<std::string>& args, std::string& error) {
                    const GameInstall& game = state.games[int(HubGame::Pikmin1)];
                    // Close the pad before fork so the game does not inherit a
                    // grabbed device. Navigation returns when the child exits.
                    setGamepadNavigation(false);
                    const bool started = platform::spawnGame(game.directory, game.executable, args, coopProcess, error);
                    if (started) gamepadHeldByGame = true;
                    else setGamepadNavigation(true);
                    return started;
                },
                [this](int& code) {
                    const bool running = platform::gameRunning(coopProcess, code);
                    if (gamepadHeldByGame && !running) {
                        gamepadHeldByGame = false;
                        setGamepadNavigation(true);
                    }
                    return running;
                });
        }
        return *coop;
    }

    static fs::path coopExchangeDir()
    {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        return platform::cacheDirectory() / "netplay"
             / ("session-" + std::to_string(platform::currentProcessId()) + "-" + std::to_string(stamp));
    }

    void openCoop(bool host, bool vs = false)
    {
        coopError.clear();
        std::fill(coopInput.begin(), coopInput.end(), '\0');
        CoopFlow& flow = coopFlow();
        if (host) {
            std::string err;
            flow.startHost(coopExchangeDir(), err, {}, vs);
        } else {
            flow.startJoin(coopExchangeDir(), {}, vs);
        }
        coopOpen = true;
    }
#endif

    void choose(HubAction action, HubGame game)
    {
        result.action = action;
        result.game = game;
        chosen = true;
    }

    void notify(const std::string& message)
    {
        toast = message;
        toastTime = 3.5f;
    }

    void startCoverDownloads()
    {
        const std::string ids[2] = { discIdOrInstalled(0, state.games[0].discId, state.games[0].directory),
                                     discIdOrInstalled(1, state.games[1].discId, state.games[1].directory) };
        for (int i = 0; i < 2; ++i) {
            const std::string region = ids[i][3] == 'P' ? "EN" : (ids[i][3] == 'J' ? "JA" : "US");
            const std::string base = "https://art.gametdb.com/wii/";
            auto request = std::make_shared<RemoteImage>();
            request->sources = { { base + "coverfullHQ/" + region + "/" + ids[i] + ".png", ids[i] + "_full.png", true },
                                 { base + "cover/" + region + "/" + ids[i] + ".png", ids[i] + ".png", false } };
            coverRequests[i] = request;
            // Suelto: si no hay conexión, cerrar la ventana no espera a curl.
            std::thread(fetchRemote, request).detach();
        }

        // Fondo: captura de partida y pantalla de título de cada juego, de las
        // miniaturas de libretro (las que usa RetroArch).
        const std::string libretro = "https://thumbnails.libretro.com/Nintendo%20-%20GameCube/";
        const std::string pikmin1 = ids[0] == "GPIP01" ? "Pikmin%20(Europe)%20(En,Fr,De,Es,It)" : "Pikmin%20(USA)";
        const std::pair<std::string, std::string> shots[] = {
            { "Named_Snaps/" + pikmin1, "p1_snap.png" },
            { "Named_Titles/" + pikmin1, "p1_title.png" },
            { "Named_Snaps/Pikmin%202%20(USA)", "p2_snap.png" },
            { "Named_Titles/Pikmin%202%20(USA)", "p2_title.png" },
        };
        for (const auto& shot : shots) {
            auto request = std::make_shared<RemoteImage>();
            request->sources = { { libretro + shot.first + ".png", shot.second, false } };
            slideRequests.push_back(request);
            std::thread(fetchRemote, request).detach();
        }
    }

    // Sube a la GPU las portadas que ya han llegado.
    void collectCovers()
    {
        for (int i = 0; i < 2; ++i) {
            auto& request = coverRequests[i];
            if (!request || !request->ready || covers[i].valid()) continue;
            std::lock_guard<std::mutex> lock(request->mutex);
            covers[i] = loadImageTexture(request->bytes.data(), request->bytes.size(), 440);
            coverStart[i] = request->full ? kFrontCoverStart : 0.0f;
            request->bytes.clear();
        }
        for (auto& request : slideRequests) {
            if (!request || !request->ready) continue;
            std::lock_guard<std::mutex> lock(request->mutex);
            Texture slide = loadImageTexture(request->bytes.data(), request->bytes.size(), 0);
            if (slide.valid()) slideshow.add(slide);
            request.reset();
        }
    }

    // ── Botones "gota de rocío" ─────────────────────────────────────────────
    static float dewdropScaleNow()
    {
        const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
        return std::max(1.0f, std::max(fb.x, fb.y));
    }

    void freeDewdrop()
    {
        for (auto& entry : dewdropSets)
            for (Texture& tex : entry.second.textures) freeTexture(tex);
        for (auto& entry : dewdropButtons) freeTexture(entry.second.blend);
        dewdropSets.clear();
        dewdropButtons.clear();
        freeTexture(dewdropSparkle);
    }

    void bakeLook(DewdropSet& set, dewdrop::Look look)
    {
        const int i = int(look);
        if (set.textures[i].valid()) return;
        set.bitmaps[i] = dewdrop::render(look, set.width, set.height, set.scale);
        const dewdrop::Bitmap& bmp = set.bitmaps[i];
        set.textures[i] = uploadTexture(bmp.rgba, bmp.width, bmp.height, 0);
    }

    // Hornea un aspecto que aún falte (como mucho uno por fotograma), para que
    // al pasar el ratón por primera vez no haya tirón.
    void warmDewdrop()
    {
        if (dewdropWarmFrame == ImGui::GetFrameCount()) return;
        dewdropWarmFrame = ImGui::GetFrameCount();
        for (auto& entry : dewdropSets) {
            DewdropSet& set = entry.second;
            for (int i = 0; i < dewdrop::kLookCount; ++i) {
                if ((set.wanted & (1u << i)) && !set.textures[i].valid()) {
                    bakeLook(set, dewdrop::Look(i));
                    return;
                }
            }
        }
    }

    void uploadBlend(Texture& tex, const dewdrop::Bitmap& bmp)
    {
        if (tex.valid() && tex.width == bmp.width && tex.height == bmp.height) {
            glBindTexture(GL_TEXTURE_2D, tex.id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bmp.width, bmp.height, GL_RGBA, GL_UNSIGNED_BYTE, bmp.rgba.data());
            return;
        }
        freeTexture(tex);
        tex = uploadTexture(bmp.rgba, bmp.width, bmp.height, 0);
    }

    // Texto crema con contorno verde oscuro y sombra suave: se lee sobre
    // cualquier parte del degradado.
    void dewdropText(ImDrawList* dl, float size, ImVec2 centre, const char* text, float alpha, float drop)
    {
        const ImVec2 extent = fonts.body->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const float scale = dewdropScaleNow();
        // A píxel de pantalla, para que el texto en reposo salga nítido.
        const ImVec2 pos(std::round((centre.x - extent.x * 0.5f) * scale) / scale,
                         std::round((centre.y - extent.y * 0.5f) * scale) / scale + drop);
        const ImU32 shadow = col(ImVec4(0.0f, 0.06f, 0.01f, 1.0f), 0.30f * alpha);
        dl->AddText(fonts.body, size, ImVec2(pos.x + 1.0f, pos.y + 2.0f), shadow, text);
        dl->AddText(fonts.body, size, ImVec2(pos.x + 1.5f, pos.y + 2.5f), shadow, text);
        const dewdrop::Rgb o = dewdrop::kTextOutline, t = dewdrop::kTextCream;
        const ImU32 outline = col(ImVec4(o.r, o.g, o.b, 1.0f), 0.8f * alpha);
        static const ImVec2 around[] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
        for (const ImVec2& d : around) dl->AddText(fonts.body, size, ImVec2(pos.x + d.x, pos.y + d.y), outline, text);
        // Fredoka a 21 px tiene trazos de ~1.5 px: a 1x el contorno se come la
        // letra. Una segunda pasada desplazada medio píxel (el atlas va con
        // sobremuestreo horizontal) engorda el trazo crema sin emborronarlo.
        const ImU32 cream = col(ImVec4(t.r, t.g, t.b, 1.0f), alpha);
        dl->AddText(fonts.body, size, pos, cream, text);
        dl->AddText(fonts.body, size, ImVec2(pos.x + 0.5f / scale, pos.y), cream, text);
    }

    // Marco común a todo lo que el launcher dibuja a mano (botones, aviso de
    // versión). Anima el estado del elemento `id` con el tiempo real (no
    // depende de los FPS) y pinta el fotograma; devuelve las capas para que
    // quien llama coloque el texto. Mientras algo se anima el bucle sigue
    // redibujando: el launcher pinta todos los fotogramas con vsync.
    dewdrop::Layers dewdropFrame(ImDrawList* dl, ImGuiID id, ImVec2 a, ImVec2 b, bool hovered, bool held, bool selected,
                                 bool disabled)
    {
        // En pasos de 0.5: la escala de escritorio cambia con cada píxel al
        // redimensionar y no se puede volver a hornear todo en cada fotograma.
        const float scale = bakeScaleFor(dewdropScaleNow());
        if (scale != dewdropScale) {
            // Ventana movida a otra pantalla (o escala nueva): se vuelve a hornear.
            freeDewdrop();
            dewdropScale = scale;
        }
        const float w = b.x - a.x, h = b.y - a.y;
        const DewdropKey key(int(std::lround(w * 4)), int(std::lround(h * 4)), int(std::lround(scale * 100)));
        DewdropSet& set = dewdropSets[key];
        if (set.width == 0) {
            set.width = w;
            set.height = h;
            set.scale = scale;
        }
        DewdropButton& button = dewdropButtons[id];
        if (button.key != key) {
            freeTexture(button.blend);
            button.key = key;
        }
        const int frame = ImGui::GetFrameCount();
        // Un botón que vuelve a aparecer (otra pestaña, otro diálogo) no
        // arrastra la animación de cuando se ocultó.
        if (button.lastFrame >= 0 && frame - button.lastFrame > 1) button.anim = dewdrop::Anim();
        const float dt = button.lastFrame == frame ? 0.0f : std::min(ImGui::GetIO().DeltaTime, 0.1f);
        button.lastFrame = frame;
        dewdrop::step(button.anim, hovered, held, selected, disabled, dt);
        const dewdrop::Layers layers = dewdrop::layersFor(button.anim);

        using dewdrop::Look;
        const auto bit = [](Look l) { return 1u << unsigned(l); };
        set.wanted |= disabled ? bit(Look::Disabled)
                    : selected ? bit(Look::Selected) | bit(Look::SelectedHovered) | bit(Look::SelectedPressed)
                               : bit(Look::Normal) | bit(Look::Hovered) | bit(Look::Pressed);
        warmDewdrop();

        const Texture* tex = nullptr;
        const Look settled = dewdrop::settledLook(layers);
        if (settled != Look::Count) {
            bakeLook(set, settled);
            tex = &set.textures[int(settled)];
            freeTexture(button.blend);
        } else {
            const dewdrop::Bitmap* looks[dewdrop::kLookCount] = {};
            for (int i = 0; i < dewdrop::kLookCount; ++i) {
                if (layers.weight[i] <= 0.0005f) continue;
                bakeLook(set, Look(i));
                looks[i] = &set.bitmaps[i];
            }
            dewdrop::composite(looks, layers.weight, button.scratch);
            uploadBlend(button.blend, button.scratch);
            tex = &button.blend;
        }
        if (tex && tex->valid()) {
            // El horneado empieza kPad unidades antes del botón; se encaja a
            // píxel de pantalla para que el borde salga nítido (Retina y 1x).
            const ImVec2 origin(std::round((a.x - dewdrop::kPad) * scale) / scale,
                                std::round((a.y - dewdrop::kPad) * scale) / scale);
            dl->AddImage(tex->imgui(), origin, ImVec2(origin.x + tex->width / scale, origin.y + tex->height / scale));
        }

        // Centelleo al entrar el ratón: una sola vez, sobre el destello.
        if (layers.twinkle > 0.01f) {
            if (!dewdropSparkle.valid()) {
                const dewdrop::Bitmap sparkle = dewdrop::renderSparkle(dewdrop::pixels(40.0f, scale));
                dewdropSparkle = uploadTexture(sparkle.rgba, sparkle.width, sparkle.height, 0);
            }
            float gx, gy;
            dewdrop::glintCentre(w, h, gx, gy);
            const ImVec2 c(a.x + gx, a.y + gy);
            const float half = 0.5f * 26.0f * (h / 44.0f) * layers.twinkleScale;
            const float cs = std::cos(layers.twinkleAngle) * half, sn = std::sin(layers.twinkleAngle) * half;
            dl->AddImageQuad(dewdropSparkle.imgui(), ImVec2(c.x - cs + sn, c.y - sn - cs), ImVec2(c.x + cs + sn, c.y + sn - cs),
                             ImVec2(c.x + cs - sn, c.y + sn + cs), ImVec2(c.x - cs - sn, c.y - sn + cs), ImVec2(0, 0), ImVec2(1, 0),
                             ImVec2(1, 1), ImVec2(0, 1), col(ImVec4(1, 1, 1, 1), layers.twinkle));
        }
        return layers;
    }

    // Rueda dentro de la ventana hija actual: una fila por paso, suavizado.
    // La ventana tiene que llevar ImGuiWindowFlags_NoScrollWithMouse para que
    // ImGui no aplique además su propio paso. Llamar antes de EndChild().
    static void wheelScroll(SmoothScroll& state)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float wheel = ImGui::IsWindowHovered() && !io.KeyCtrl ? io.MouseWheel : 0.0f;
        const float current = ImGui::GetScrollY();
        const float next = smoothScrollStep(state, current, ImGui::GetScrollMaxY(), wheel,
                                            ImGui::GetFrameHeightWithSpacing(), io.DeltaTime);
        if (next != current) ImGui::SetScrollY(next);
    }

    // Botón como una gota de rocío. Todos los botones del launcher pasan por
    // aquí, así que todos se ven y se animan igual.
    bool glassButton(const char* label, ImVec2 size, bool selected = false, bool disabled = false)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(label, size);
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered() && !disabled;
        // Gamepad focus uses the same glow as a mouse hover, plus a ring.
        // Mouse clicks hide the ring, so the button looks as it did before.
        const bool padFocus = gamepadItemFocused() && !disabled;
        const bool hot = hovered || padFocus;
        const bool held = ImGui::IsItemActive() && !disabled;
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(at.x + size.x, at.y + size.y);
        const dewdrop::Layers layers = dewdropFrame(dl, id, at, b, hot, held, selected, disabled);
        if (padFocus) drawGamepadFocusRing(dl, at, b, 16.0f);
        dewdropLabel(dl, at, size, label, buttons::kLabelSize, layers);
        return clicked && !disabled;
    }

    bool glassButton(const char* label, ButtonSize width, float height, bool selected = false, bool disabled = false)
    {
        return glassButton(label, ImVec2(buttons::buttonWidth(width), height), selected, disabled);
    }

    buttons::MeasureFn measureLabel() const
    {
        ImFont* font = fonts.body;
        return [font](const std::string& text, float size) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x; };
    }

    // Tamaño estándar para un texto que llega de fuera (diálogos).
    ButtonSize sizeFor(const std::string& label, float height, ButtonSize minimum = ButtonSize::Small) const
    {
        return buttons::sizeFor(label, height, measureLabel(), minimum);
    }

    // Texto de un botón: centrado en el rectángulo interior (dentro del borde,
    // con el mismo margen en todos y sin pisar la curva de los extremos). Si
    // no cabe, encoge la letra hasta un suelo y luego recorta con "..."; el
    // recorte final al rectángulo interior impide pisar el borde pase lo que
    // pase.
    void dewdropLabel(ImDrawList* dl, ImVec2 at, ImVec2 size, const char* label, float baseSize, const dewdrop::Layers& layers)
    {
        const char* hidden = std::strstr(label, "##"); // como ImGui: lo que va tras ## es solo el id
        const std::string text = hidden ? std::string(label, hidden) : std::string(label);
        const buttons::FittedLabel fit = buttons::fitLabel(text, size.x, size.y, measureLabel(), baseSize);
        const buttons::Rect inner = buttons::innerTextRect(size.x, size.y, fit.size);
        dl->PushClipRect(ImVec2(at.x + inner.x0, at.y + inner.y0 - 1.0f), ImVec2(at.x + inner.x1, at.y + inner.y1 + 1.0f), true);
        dewdropText(dl, fit.size, ImVec2(at.x + size.x * 0.5f, at.y + size.y * 0.5f), fit.text.c_str(), layers.textAlpha,
                    layers.textDrop);
        dl->PopClipRect();
    }

    void drawTabs(float y, float width)
    {
        const char* labels[] = { "Games", "Settings", "Move Install" };
        const float tabW = buttons::buttonWidth(role::kTab), tabH = 42.0f, gap = 14.0f;
        const float total = 3.0f * tabW + 2.0f * gap;
        float x = (width - total) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (glassButton(labels[i], role::kTab, tabH, int(tab) == i)) tab = Tab(i);
            x += tabW + gap;
        }
    }

    void drawBackground(ImDrawList* dl, ImVec2 size, float dt)
    {
        dl->AddRectFilledMultiColor(ImVec2(0, 0), size, kBgTop, kBgTop, kBgBottom, kBgBottom);

        if (!slideshow.slides.empty()) {
            slideshow.draw(dl, size, dt);
            // Velo verde encima para que la interfaz se lea y el verde mande.
            dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(6, 30, 10, 165), IM_COL32(6, 30, 10, 165),
                                        IM_COL32(14, 54, 20, 195), IM_COL32(14, 54, 20, 195));
        }

        // Luz suave detrás de las portadas.
        const ImVec2 centre(size.x * 0.5f, size.y * 0.55f);
        for (int i = 6; i >= 1; --i) dl->AddCircleFilled(centre, size.y * 0.09f * float(i), col(kLeafBright, 0.025f), 64);

    }

    void drawCard(int index, ImVec2 origin, ImVec2 size, float dt, float t)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Los dos juegos se instalan desde este launcher con su propio disco.
        const bool available = true;
        const bool installed = state.games[index].installed;

        ImGui::SetCursorScreenPos(origin);
        ImGui::PushID(index);
        const bool clicked = ImGui::InvisibleButton("cover", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool padFocus = gamepadItemFocused();
        ImGui::PopID();
        const bool hot = (hovered || padFocus) && available;
        if (hovered && available) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        CardState& card = cards[index];
        card.hover = approach(card.hover, hot ? 1.0f : 0.0f, dt, 12.0f);
        const float scale = 1.0f + 0.08f * card.hover;
        const ImVec2 centre(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        const ImVec2 half(size.x * 0.5f * scale, size.y * 0.5f * scale);
        const ImVec2 a(centre.x - half.x, centre.y - half.y), b(centre.x + half.x, centre.y + half.y);
        const float rounding = 18.0f * scale;

        // Sombra y resplandor naranja al pasar por encima.
        dl->AddRectFilled(ImVec2(a.x + 6, a.y + 10), ImVec2(b.x + 6, b.y + 10), IM_COL32(0, 0, 0, 90), rounding);
        for (int i = 1; i <= 4; ++i) {
            const float grow = float(i) * 4.0f;
            dl->AddRect(ImVec2(a.x - grow, a.y - grow), ImVec2(b.x + grow, b.y + grow),
                        col(kOrange, 0.16f * card.hover / float(i)), rounding + grow, 0, 4.0f);
        }

        if (covers[index].valid()) {
            ImVec2 uv0, uv1;
            coverUv(covers[index], coverStart[index], size.x / size.y, uv0, uv1);
            const ImU32 tint = available ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255);
            dl->AddImageRounded(covers[index].imgui(), a, b, uv0, uv1, tint, rounding);
        } else {
            // Sin portada todavía (descargando o sin conexión): tarjeta dibujada.
            // Todo redondeado: un degradado cuadrado asomaría por las esquinas
            // del borde.
            dl->AddRectFilled(a, b, col(kLeaf), rounding);
            dl->AddRectFilled(ImVec2(a.x, (a.y + b.y) * 0.5f), b, col(kLeafDark, 0.6f), rounding, ImDrawFlags_RoundCornersBottom);
            dl->AddRectFilled(a, b, col(kLeafDark, 0.35f), rounding);
            drawFlower(dl, ImVec2(centre.x, centre.y - half.y * 0.25f), half.x * 0.35f, 5, IM_COL32(255, 255, 250, 255),
                       IM_COL32(255, 196, 40, 255), t * 0.2f);
            centredText(dl, fonts.title, 30.0f * scale, ImVec2(centre.x, centre.y + half.y * 0.2f), col(kOrange),
                        index == 0 ? "Pikmin" : "Pikmin 2");
            const auto& request = coverRequests[index];
            if (request && !request->failed) {
                centredText(dl, fonts.caption, 15.0f, ImVec2(centre.x, centre.y + half.y * 0.38f), col(kOrange, 0.8f),
                            "Downloading cover...");
            }
        }
        if (!installed) {
            // Velo en la mitad baja para que la etiqueta se lea.
            dl->AddRectFilledMultiColor(ImVec2(a.x, (a.y + b.y) * 0.5f), ImVec2(b.x, b.y - rounding),
                                        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 170), IM_COL32(0, 0, 0, 170));
            dl->AddRectFilled(ImVec2(a.x, b.y - rounding), b, IM_COL32(0, 0, 0, 170), rounding, ImDrawFlags_RoundCornersBottom);
        }
        dl->AddRect(a, b, col(hot && available ? kOrange : kOrangeDeep), rounding, 0, 3.0f + card.hover);
        if (padFocus && available) drawGamepadFocusRing(dl, a, b, rounding);

        // Destellos del sol del juego en dos esquinas al pasar por encima.
        if (art.sparkle.valid() && card.hover > 0.02f && available) {
            const float s = 34.0f * card.hover, spin = t * 1.5f;
            const ImVec2 spots[2] = { ImVec2(a.x + 10.0f, a.y + 10.0f), ImVec2(b.x - 10.0f, b.y - 60.0f) };
            for (int i = 0; i < 2; ++i) {
                ImVec2 q[4];
                rotatedQuad(spots[i], ImVec2(s * 0.5f, s * 0.5f), spin + float(i), q);
                dl->AddImageQuad(art.sparkle.imgui(), q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1),
                                 ImVec2(0, 1), col(ImVec4(1, 1, 1, 1), card.hover));
            }
        }

        const ImVec2 badgeAt(centre.x, b.y - 34.0f * scale);
        if (!available) {
            badge(dl, fonts.body, 20.0f, badgeAt, IM_COL32(20, 50, 24, 235), col(kOrange), "Coming soon");
        } else if (!installed) {
            badge(dl, fonts.body, 20.0f, badgeAt, IM_COL32(20, 50, 24, 235), col(kOrange), "Not installed");
        } else if (card.hover > 0.02f) {
            badge(dl, fonts.title, 26.0f, badgeAt, col(kLeaf, card.hover), col(kOrange, card.hover), "PLAY");
        }

        if (clicked) {
            if (installed) choose(HubAction::Play, HubGame(index));
            else if (available) notify(index == 0 ? "Install Pikmin first: use the Install button below its cover."
                                                  : "Install Pikmin 2 first: use the Install button below its cover.");
        }
    }

    // Filas de botones bajo la portada de Pikmin 1 antes del cooperativo: una,
    // o dos con los dos editores (HD textures baja a la segunda).
    float editorRows() const
    {
        return (!state.mapEditor.empty() && !state.modelMaker.empty()) ? 2.0f : 1.0f;
    }

    // Borde inferior de las páginas Games y Move Install.
    // Por encima del pie ("Installed in: ...").
    static float pageBottom(ImVec2 size) { return hubPageBottom(size.y); }

    // Filas de botones de la columna más alta (la de Pikmin 1 si está instalado).
    float gamesRows() const
    {
        if (!state.games[int(HubGame::Pikmin1)].installed) return 1.0f;
        float rows = editorRows();
#if PIKMIN_LAUNCHER_NETPLAY
        if (state.netplay) rows += 2.0f;
#endif
        return rows;
    }

    // Una página que no cabe se dibuja dentro de una región con scroll que
    // ocupa de `top` al borde inferior; devuelve dónde empieza su contenido.
    float beginPageScroll(const char* id, float top, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(ImVec2(0.0f, top));
        // NavFlattened: the d-pad moves between the tabs and this page. The
        // mouse wheel still uses wheelScroll().
        ImGui::BeginChild(id, ImVec2(size.x, std::max(1.0f, pageBottom(size) - top)), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground);
        return ImGui::GetWindowPos().y - ImGui::GetScrollY();
    }

    void endPageScroll(float top, float content)
    {
        // Reserva el alto completo para que la región sepa cuánto puede desplazarse.
        ImGui::SetCursorScreenPos(ImVec2(0.0f, top + content + 12.0f));
        ImGui::Dummy(ImVec2(1.0f, 0.0f));
        ImGui::EndChild();
    }

    void drawGames(float top, ImVec2 size, float dt, float t)
    {
        const float rows = gamesRows();
        const bool hasUpdate = state.games[int(HubGame::Pikmin1)].installed;
        const GamesLayout layout = fitGames(size.x, pageBottom(size) - top, rows, hasUpdate);
        if (layout.scroll) top = beginPageScroll("##games", top, size);
        drawGamesBody(top, size, dt, t, layout);
        if (layout.scroll) endPageScroll(top, layout.content);
    }

    void drawGamesBody(float top, ImVec2 size, float dt, float t, const GamesLayout& layout)
    {
        // Las portadas encogen con la ventana; los botones conservan el ancho
        // de su columna.
        const float cardW = layout.columnW;
        // Bajo cada portada va una rejilla de dos botones medianos, más ancha
        // que la portada en ventanas pequeñas: separa las portadas lo justo
        // para que las dos rejillas no se toquen.
        const float toolW = buttons::buttonWidth(role::kGameTool);
        const float gridW = buttons::gameGridWidth();
        const float gap = std::max(layout.gap, buttons::gameCardGap(cardW));
        const float wideW = buttons::buttonWidth(role::kGameWide);
        const float startX = (size.x - (2.0f * cardW + gap)) * 0.5f;
        const float cardY = top + layout.space.top;
        const float buttonsGap = layout.space.buttons;
        const char* names[] = { "Pikmin", "Pikmin 2" };

        for (int i = 0; i < 2; ++i) {
            const float x = startX + float(i) * (cardW + gap);
            drawCard(i, ImVec2(x + (cardW - layout.coverW) * 0.5f, cardY), ImVec2(layout.coverW, layout.coverH), dt, t);

            // Nombre del juego y los dos botones debajo de la portada.
            const float below = cardY + layout.coverH + layout.space.name;
            centredText(ImGui::GetWindowDrawList(), fonts.title, 28.0f, ImVec2(x + cardW * 0.5f, below + 14.0f),
                        col(kOrange), names[i]);
            const bool isPikmin2 = i == int(HubGame::Pikmin2);
            const bool installed = state.games[i].installed;
            // Debajo de cada portada: Install o, ya instalado, HD textures
            // (y en Pikmin 1, Map editor a su lado).
            ImGui::PushID(i);
            // Columnas de la rejilla, centradas bajo la portada.
            const float centre = x + cardW * 0.5f;
            const float left = centre - gridW * 0.5f, right = left + toolW + buttons::kButtonGap;
            if (!installed) {
                ImGui::SetCursorScreenPos(ImVec2(centre - wideW * 0.5f, below + buttonsGap));
                if (glassButton("Install", role::kGameWide, 44.0f)) choose(HubAction::Install, HubGame(i));
            } else {
                const bool editor = !isPikmin2 && !state.mapEditor.empty();
                const bool models = !isPikmin2 && !state.modelMaker.empty();
                if (editor && models) {
                    // Los dos editores arriba; HD textures y Characters (instalar
                    // un personaje del creador) en la segunda fila.
                    ImGui::SetCursorScreenPos(ImVec2(left, below + buttonsGap));
                    if (glassButton("Map editor", role::kGameTool, 44.0f)) choose(HubAction::MapEditor, HubGame(i));
                    ImGui::SetCursorScreenPos(ImVec2(right, below + buttonsGap));
                    if (glassButton("Model maker", role::kGameTool, 44.0f)) choose(HubAction::ModelMaker, HubGame(i));
                    ImGui::SetCursorScreenPos(ImVec2(left, below + buttonsGap + 44.0f + 10.0f));
                    if (glassButton("HD textures", role::kGameTool, 44.0f)) choose(HubAction::TexturePack, HubGame(i));
                    ImGui::SetCursorScreenPos(ImVec2(right, below + buttonsGap + 44.0f + 10.0f));
                    if (glassButton("Characters", role::kGameTool, 44.0f)) choose(HubAction::InstallCharacter, HubGame(i));
                } else if (editor || models) {
                    ImGui::SetCursorScreenPos(ImVec2(left, below + buttonsGap));
                    if (editor && glassButton("Map editor", role::kGameTool, 44.0f)) choose(HubAction::MapEditor, HubGame(i));
                    if (models && glassButton("Model maker", role::kGameTool, 44.0f)) choose(HubAction::ModelMaker, HubGame(i));
                    ImGui::SetCursorScreenPos(ImVec2(right, below + buttonsGap));
                    if (glassButton("HD textures", role::kGameTool, 44.0f)) choose(HubAction::TexturePack, HubGame(i));
                } else {
                    ImGui::SetCursorScreenPos(ImVec2(centre - wideW * 0.5f, below + buttonsGap));
                    if (glassButton("HD textures", role::kGameWide, 44.0f)) choose(HubAction::TexturePack, HubGame(i));
                }
            }
#if PIKMIN_LAUNCHER_NETPLAY
            // Online co-op: two captains on two PCs (netplay builds).
            if (installed && !isPikmin2 && state.netplay) {
                const float rows  = below + buttonsGap + editorRows() * (44.0f + 10.0f);
                ImGui::SetCursorScreenPos(ImVec2(left, rows));
                if (glassButton("Host Co-op", role::kGameTool, 44.0f)) openCoop(true);
                ImGui::SetCursorScreenPos(ImVec2(right, rows));
                if (glassButton("Join Co-op", role::kGameTool, 44.0f)) openCoop(false);
                // Online VS: a 1v1 match on two PCs, on the host's VS map.
                ImGui::SetCursorScreenPos(ImVec2(left, rows + 44.0f + 10.0f));
                if (glassButton("Host VS", role::kGameTool, 44.0f)) openCoop(true, true);
                ImGui::SetCursorScreenPos(ImVec2(right, rows + 44.0f + 10.0f));
                if (glassButton("Join VS", role::kGameTool, 44.0f)) openCoop(false, true);
            }
#endif
            ImGui::PopID();
        }

        // Un solo Update, centrado entre los dos juegos y debajo de sus botones.
        if (state.games[int(HubGame::Pikmin1)].installed) {
            const float y = cardY + layout.coverH + layout.space.name + buttonsGap + 44.0f + layout.space.update
                          + (gamesRows() - 1.0f) * (44.0f + 10.0f);
            // Con versión nueva en GitHub el botón lo dice ("Update to 1.0").
            std::string updateLabel = "Update";
            if (releaseCheck && releaseCheck->done) {
                std::lock_guard<std::mutex> lock(releaseCheck->mutex);
                if (releaseCheck->newer) updateLabel = "Update to " + releaseCheck->latest.version;
            }
            ImGui::SetCursorScreenPos(ImVec2((size.x - wideW) * 0.5f, y));
            if (glassButton(updateLabel.c_str(), role::kGameWide, 44.0f, updateLabel != "Update"))
                choose(HubAction::Update, HubGame::Pikmin1);
        }
    }

    void queue(std::vector<std::string> arguments, const char* label = "Saving...")
    {
        if (!arguments.empty() && (arguments[0].find("--texpack-") == 0 || arguments[0] == "--hdmodel-delete"))
            awaitingPackResult = true;
        pendingRequest = std::move(arguments);
        pendingLabel = label;
        messageGame = settingsGame;
        messageGroup = settingsGroup;
    }

    const std::vector<std::string>& stableOptions(int group, const SettingsRow& row)
    {
        std::vector<std::string>& stored = optionOrder[{ group, row.row }];
        bool same = stored.size() == row.options.size();
        for (std::size_t i = 0; same && i < stored.size(); ++i)
            same = std::find(row.options.begin(), row.options.end(), stored[i]) != row.options.end();
        if (!same) stored = row.options;
        return stored;
    }

    // Packs de texturas: los instalados, cuál está activo, e instalar uno nuevo
    // desde su zip. Se aplica la próxima vez que arranca el juego.
    void drawTexturePacks(const SettingsSnapshot& snap, bool busy)
    {
        const bool compact = ImGui::GetContentRegionAvail().x < 420.0f;
        if (compact) {
            ImGui::PushFont(fonts.caption);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
        } else {
            ImGui::PushFont(fonts.caption);
            ImGui::TextColored(kOrangeDeep, "TEXTURE PACKS");
            ImGui::PopFont();
            ImGui::Separator();
        }
        ImGui::TextUnformatted(busy ? "Updating texture packs..." : "Applies next time the game starts.");
        ImGui::Dummy(ImVec2(0, compact ? 2.0f : 6.0f));
        ImGui::BeginDisabled(busy);
        const auto option = [&](const char* label, const std::string& name) {
            const bool active = snap.activePack == name;
            if (handRadioButton(label, active) && !active) {
                queue({ "--texpack-activate", name });
            }
        };
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float deleteHeight = compact ? 30.0f : 34.0f;
        const float deleteWidth = std::max(buttons::buttonWidth(role::kTexturePackDelete),
            buttons::fitWidth("Delete", deleteHeight, buttons::kLabelSize, measureLabel()));
        const ImVec2 actionSize(deleteWidth, deleteHeight);
        texturePackRow(rowWidth, actionSize, [&] { option("Off (original textures)", ""); }, [] {});
        if (snap.texturePackDetails.empty()) {
            for (const std::string& pack : snap.texturePacks)
                texturePackRow(rowWidth, actionSize, [&] { option(pack.c_str(), pack); }, [] {});
        } else for (const auto& pack : snap.texturePackDetails) {
            ImGui::PushID(pack.id.c_str());
            const std::string label = pack.name + (pack.installed ? " (" + pack.size + ")" : "");
            const bool working = settings && settings->downloadingPack == pack.id;
            const auto action = texturePackAction(pack, working);
            const char* actionLabel = action == TexturePackAction::Working ? "Working..."
                : action == TexturePackAction::Install ? "Install" : "Delete";
            std::optional<TexturePackDownload> downloadable;
            for (const auto& candidate : texturePackDownloads(settingsGame)) {
                const std::string id = std::string(candidate.label) == "4K" ? "hd-4k" : "hd-1080p";
#ifndef __ANDROID__
                if (id == pack.id) downloadable = candidate;
#endif
            }
            texturePackRow(rowWidth, actionSize, [&] {
                ImGui::BeginDisabled(!pack.installed);
                if (!pack.installed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1));
                if (pack.installed) option(label.c_str(), pack.id);
                else handRadioButton(label.c_str(), false);
                if (!pack.installed) ImGui::PopStyleColor();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    if (pack.installed)
                        ImGui::Text("%s\n%s\nApplies next time the game starts.", pack.path.c_str(), pack.size.c_str());
                    else ImGui::Text("Not installed. Use Install to download this pack.\n%s", pack.path.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }, [&] {
                const bool unavailable = pack.installed ? !pack.removable : !downloadable;
                if (glassButton(actionLabel, ImVec2(deleteWidth, deleteHeight), false, busy || unavailable)) {
                    if (pack.installed) {
                        deletingPack = pack;
                        ImGui::OpenPopup("Delete texture pack?");
                    } else if (downloadable) {
                        installingPack = pack;
                        installingDownload = *downloadable;
                        installFreeSpace.reset();
                        fs::path parent = fs::u8path(pack.path);
                        std::error_code ec;
                        while (!parent.empty() && !fs::exists(parent, ec)) {
                            if (ec && ec != std::errc::no_such_file_or_directory) break;
                            const auto next = parent.parent_path();
                            if (next == parent) break;
                            parent = next; ec.clear();
                        }
                        if (!ec) {
                            const auto space = fs::space(parent, ec);
                            if (!ec && space.available != std::uintmax_t(-1)) installFreeSpace = space.available;
                        }
                        installStorage = texturePackStorageCheck(installingDownload.bytes, installFreeSpace);
                        if (installStorage == TexturePackStorage::Confirm || installStorage == TexturePackStorage::Block)
                            ImGui::OpenPopup("Texture pack storage");
                        else {
                            if (installStorage == TexturePackStorage::Unknown)
                                std::fprintf(stderr, "Texture pack storage unknown for %s; proceeding.\n", pack.path.c_str());
                            pendingPackInstall = pack.id;
                        }
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    if (!pack.installed) {
                        if (downloadable && downloadable->bytes)
                            ImGui::Text("Download: %s", texturePackSizeText(downloadable->bytes).c_str());
                        else ImGui::TextUnformatted("Download size unavailable.");
                        ImGui::Text("Installs to: %s", pack.path.c_str());
                        ImGui::TextUnformatted(working ? "Download / install in progress." : "Installing does not select this pack.");
                    } else ImGui::Text("%s\n%s\n%s", pack.path.c_str(), pack.size.c_str(),
                        pack.removable ? "Deletes only this pack. You can download it again." : "Deletion unavailable: unsafe links or unreadable files.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            });
            ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, ImGui::GetIO().DisplaySize.x - 32.0f), 0), ImGuiCond_Always);
            if (ImGui::BeginPopupModal("Texture pack storage", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::PushFont(fonts.caption);
                const bool blocked = installStorage == TexturePackStorage::Block;
                ImGui::TextWrapped("%s", blocked ? "Not enough space to install this texture pack." : "Free space is below the recommended amount. Install anyway?");
                ImGui::Text("Pack download: %s", texturePackSizeText(installingDownload.bytes).c_str());
                ImGui::Text("Free space: %s", texturePackSizeText(installFreeSpace.value_or(0)).c_str());
                const auto needed = texturePackStorageBytes(installingDownload.bytes, blocked ? kTexturePackHardFactor : kTexturePackSoftFactor);
                ImGui::Text("%s: %s", blocked ? "Needed" : "Recommended", texturePackSizeText(needed).c_str());
                if (blocked) ImGui::Text("Free up at least: %s", texturePackSizeText(needed - installFreeSpace.value_or(0)).c_str());
                ImGui::TextWrapped("Destination: %s", installingPack.path.c_str());
                if (glassButton(blocked ? "OK" : "Cancel", ImVec2(110, 38), true)) ImGui::CloseCurrentPopup();
                ImGui::SetItemDefaultFocus();
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || gamepadBackPressed()) ImGui::CloseCurrentPopup();
                if (!blocked) {
                    ImGui::SameLine();
                    if (glassButton("Install anyway", ImVec2(160, 38))) {
                        pendingPackInstall = installingPack.id;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::PopFont();
                ImGui::EndPopup();
            }
            ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, ImGui::GetIO().DisplaySize.x - 32.0f), 0), ImGuiCond_Always);
            if (ImGui::BeginPopupModal("Delete texture pack?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::PushFont(fonts.caption);
                ImGui::Text("Delete %s (%s)?", deletingPack.name.c_str(), deletingPack.size.c_str());
                ImGui::TextUnformatted("Folder to permanently delete:");
                // Read-only, selectable path: long paths remain accessible by
                // scrolling/copying and in the full-path tooltip.
                ImGui::SetNextItemWidth(-1);
                std::vector<char> path(deletingPack.path.begin(), deletingPack.path.end()); path.push_back(0);
                ImGui::InputText("##deletePath", path.data(), path.size(), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", deletingPack.path.c_str());
                }
                ImGui::TextUnformatted(snap.activePack == deletingPack.id
                    ? "This pack is selected. Textures will switch to Off." : "Other installed packs will stay.");
                ImGui::TextUnformatted("Close the game first. Saves and discs stay.");
                if (glassButton("Cancel", ImVec2(110, 38)) || gamepadBackPressed()) ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                if (glassButton("Delete pack", ImVec2(130, 38), true)) {
                    queue({ "--texpack-delete", deletingPack.id }, "Deleting the pack...");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopFont();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (snap.texturePacks.empty()) ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "No packs installed yet.");
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 10));
        if (glassButton(filePickLabel("texpack", "Install a pack from a .zip").c_str(),
                        ImVec2(std::min(buttons::buttonWidth(role::kWide), ImGui::GetContentRegionAvail().x), compact ? 34.0f : 46.0f), false,
                        busy || filePickOpen())) {
            startFilePick("texpack", "Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP",
                          [this](const fs::path& zip) { queue({ "--texpack-install", zip.string() }, "Installing the pack..."); });
        }        if (compact) {
            ImGui::PopStyleVar(2);
            ImGui::PopFont();
        }

    }

    // Modelos HD: uno por fila, cada uno se instala desde su zip.
    void drawHdModels(const SettingsSnapshot& snap, bool busy)
    {
#ifndef __ANDROID__
        pumpHdImports(snap, busy);
#endif
        const float buttonW = buttons::buttonWidth(role::kSettingsRow);
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "HD MODELS");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 0.85f),
                           "Higher-detail characters, converted from the model zips. They apply the next time the game starts.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
#ifndef __ANDROID__
        if (glassButton("Get HD models",
                        ImVec2(std::min(buttons::buttonWidth(role::kWide), ImGui::GetContentRegionAvail().x), 44.0f))) {
            ImGui::OpenPopup("Get HD models");
            startHdWatch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ImGui::TextUnformatted("Lists each character with a Download button that opens its page in your browser. "
                                   "Zips you download there install by themselves.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        drawGetHdModels(snap, busy);
        ImGui::Dummy(ImVec2(0, 8));
#endif

        const float width = ImGui::GetContentRegionAvail().x;
        const float toggleW = buttons::buttonWidth(role::kToggle);
        const float deleteW = buttons::buttonWidth(role::kSmallAction);
        float nameW = 0.0f;
        for (const HdModel& model : snap.hdModels)
            nameW = std::max(nameW, ImGui::CalcTextSize(model.name.c_str()).x);
        // On/Off, Delete, Install/Replace. Too narrow even stacked: the
        // buttons go on two lines (toggle + delete, then install).
        const float controlsW = toggleW + 12.0f + deleteW + 12.0f + buttonW;
        const bool twoLines = controlsW > width;
        const auto layout = settingsRowLayout(width, nameW, twoLines ? std::max(toggleW + 12.0f + deleteW, buttonW) : controlsW);
        const bool stacked = layout.stacked || twoLines;
        float nameHeight = ImGui::GetTextLineHeight();
        for (const HdModel& model : snap.hdModels)
            nameHeight = std::max(nameHeight, ImGui::CalcTextSize(model.name.c_str(), nullptr, false,
                                                               stacked ? width : nameW).y);
        for (const HdModel& model : snap.hdModels) {
            ImGui::PushID(model.row);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float controlsY = origin.y + (stacked ? nameHeight + 8.0f : 0.0f);
            // Beside the 40 px buttons, centre the name on them.
            if (!stacked) ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + std::max(0.0f, (40.0f - nameHeight) * 0.5f)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (stacked ? width : nameW));
            ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 1.0f), "%s", model.name.c_str());
            ImGui::PopTextWrapPos();
            const float controlsX = twoLines ? 0.0f : (stacked ? width - controlsW : layout.controlsX);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + controlsX, controlsY));
            if (model.installed) {
                if (glassButton(model.enabled ? "On" : "Off", role::kToggle, 40.0f, model.enabled, busy)) {
                    queue({ "--hdmodel-enable", std::to_string(model.row), model.enabled ? "0" : "1" });
                }
                ImGui::SetCursorScreenPos(ImVec2(origin.x + controlsX + toggleW + 12.0f, controlsY));
                if (glassButton("Uninstall", role::kSmallAction, 40.0f, false, busy)) {
                    deletingModel = model;
                    uninstallPending = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    ImGui::Text("Frees %s.", model.size.empty() ? "its space" : model.size.c_str());
                    ImGui::Text("From: %s", model.path.c_str());
                    ImGui::TextUnformatted("Uninstalls only this model's files. You can install it again.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + controlsX, controlsY + (40.0f - ImGui::GetTextLineHeight()) * 0.5f));
                ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "Not installed");
            }
            const float installY = twoLines ? controlsY + 48.0f : controlsY;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (twoLines ? 0.0f : width - buttonW), installY));
            const std::string pickOwner = "model" + std::to_string(model.row);
            if (glassButton(filePickLabel(pickOwner, model.installed ? "Replace" : "Install").c_str(),
                            role::kSettingsRow, 40.0f, false, busy || filePickOpen())) {
                const int row = model.row;
                startFilePick(pickOwner, "Choose the zip for " + model.name, "Model zip", "*.zip *.ZIP",
                              [this, row](const fs::path& zip) {
                                  queue({ "--hdmodel-install", std::to_string(row), zip.string() }, "Converting the model...");
                              });
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Pick this model's zip (or its unzipped folder) yourself.");
                ImGui::EndTooltip();
            }
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(width, (installY - origin.y) + 40.0f + 8.0f));
            ImGui::PopID();
        }
        drawDeleteHdModel();
        drawPikminModels(snap, busy);
    }

    void drawDeleteHdModel()
    {
        if (uninstallPending) {
            uninstallPending = false;
            ImGui::OpenPopup("Uninstall HD model?");
        }
        ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, ImGui::GetIO().DisplaySize.x - 32.0f), 0), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal("Uninstall HD model?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;
        ImGui::PushFont(fonts.caption);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("Uninstall %s? This frees %s.", deletingModel.name.c_str(), deletingModel.size.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextUnformatted("Its files are permanently deleted from:");
        ImGui::SetNextItemWidth(-1);
        std::vector<char> path(deletingModel.path.begin(), deletingModel.path.end());
        path.push_back(0);
        ImGui::InputText("##deleteModelPath", path.data(), path.size(), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", deletingModel.path.c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Other models in that folder stay. The game uses the original model next time it starts.");
        ImGui::PopTextWrapPos();
        if (glassButton("Cancel", ImVec2(110, 38))) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || gamepadBackPressed()) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (glassButton("Uninstall", ImVec2(150, 38), true)) {
            queue({ "--hdmodel-delete", std::to_string(deletingModel.row) }, "Uninstalling the model...");
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }

#ifndef __ANDROID__
    // ── Get HD models (#189) ─────────────────────────────────────────────────
    // Download opens the character's page in the browser; while the window is
    // open the Downloads folder is polled and each new zip is first identified
    // by the game (--hdmodel-identify) and, if it is a model, converted
    // (--hdmodel-install -1). One request at a time, through queue().
    void startHdWatch()
    {
        hdGetNote.clear();
        if (!hdWatcher.watching()) hdWatcher.start(downloadsDirectory());
        hdPollClock = 0.0f;
    }

    void stopHdWatch()
    {
        if (!hdWatcher.watching() && hdWaitingRows.empty()) return;
        hdWatcher.stop();
        hdWaitingRows.clear();
        // Zips found but not started yet are dropped; one already running ends.
        hdFound.erase(std::remove_if(hdFound.begin(), hdFound.end(), [](const HdJob& job) { return job.fromDownloads; }),
                      hdFound.end());
    }

    void pumpHdImports(const SettingsSnapshot& snap, bool busy)
    {
        const bool idle = !busy && pendingRequest.empty();
        if (hdJob && idle) {
            HdJob job = *hdJob;
            hdJob.reset();
            const std::string name = job.path.filename().string();
            if (job.stage == HdJob::Identify) {
                if (snap.hdModelDetected >= 0) {
                    job.stage = HdJob::Install;
                    job.row = snap.hdModelDetected;
                    hdRowErrors.erase(job.row);
                    hdJob = job;
                    queue({ "--hdmodel-install", "-1", job.path.string() }, "Installing the model...");
                    return;
                }
                hdGetNote = job.fromDownloads ? "Skipped " + name + ": it is not one of the model zips. It was left as it is."
                                              : name + " is not one of the model zips listed here.";
                hdGetNoteError = !job.fromDownloads;
                if (!job.fromDownloads) notify(hdGetNote);
            } else if (snap.hdModelInstalled >= 0) {
                hdWaitingRows.erase(job.row);
                hdRowErrors.erase(job.row);
                notify(snap.message);
                toastTime = 8.0f;
                hdGetNote.clear();
                std::error_code ec;
                if (job.fromDownloads && fs::is_regular_file(fs::symlink_status(job.path, ec))) {
                    const auto bytes = fs::file_size(job.path, ec);
                    hdZipOffer = HdZipOffer { job.path, ec ? std::string() : texturePackSizeText(bytes) };
                }
            } else {
                hdRowErrors[job.row] = "Could not install " + name + ": "
                                     + (snap.message.empty() ? settings->error : snap.message)
                                     + " Try downloading it again.";
                notify(hdRowErrors[job.row]);
            }
        }
        if (hdWatcher.watching()) {
            hdPollClock += ImGui::GetIO().DeltaTime;
            if (hdPollClock >= 1.0f) {
                hdPollClock = 0.0f;
                for (const fs::path& zip : hdWatcher.poll()) hdFound.push_back({ zip, true });
            }
        }
        if (!hdJob && idle && !hdFound.empty()) {
            HdJob job = hdFound.front();
            hdFound.erase(hdFound.begin());
            hdJob = job;
            queue({ "--hdmodel-identify", job.path.string() }, "Checking the zip...");
        }
    }

    HdRowInput hdRowInput(const SettingsSnapshot& snap, int row) const
    {
        HdRowInput in;
        for (const HdModel& model : snap.hdModels)
            if (model.row == row) {
                in.installed = model.installed;
                in.size = model.installed ? model.size : std::string();
            }
        in.waiting = hdWaitingRows.count(row) > 0;
        in.importing = hdJob && hdJob->stage == HdJob::Install && hdJob->row == row;
        if (const auto it = hdRowErrors.find(row); it != hdRowErrors.end()) in.error = it->second;
        return in;
    }

    void drawGetHdModels(const SettingsSnapshot& snap, bool busy)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 popupSize(std::min(680.0f, display.x - 32.0f), std::min(820.0f, display.y - 32.0f));
        ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Get HD models", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
                                                                  | ImGuiWindowFlags_NoMove)) {
            stopHdWatch();
            return;
        }
        const ImVec4 cream(1.0f, 0.93f, 0.80f, 1.0f), dim(0.62f, 0.58f, 0.44f, 1.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(cream, "Click Download to open a character's page on The Models Resource, then download its zip "
                                  "there. Each model zip that lands in Downloads installs by itself.");
        ImGui::PopTextWrapPos();
        ImGui::PushFont(fonts.caption);
        ImGui::PushTextWrapPos(0.0f);
        if (hdWatcher.folder().empty())
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.40f, 1.0f), "Could not find your Downloads folder. Use Import a zip instead.");
        else
            ImGui::TextColored(dim, "Watching for new zips in %s", hdWatcher.folder().string().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Separator();

        // The rows scroll; the zip offer, the progress line and the buttons
        // below them always stay in view.
        const float fullW = ImGui::GetContentRegionAvail().x;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footW = buttons::buttonWidth(ButtonSize::Large);
        const bool footStacked = fullW < footW * 2.0f + 12.0f;
        std::string offerText, progressText;
        if (hdZipOffer)
            offerText = "Delete " + hdZipOffer->zip.filename().string()
                      + (hdZipOffer->size.empty() ? std::string() : " (" + hdZipOffer->size + ")")
                      + " from Downloads? The model no longer needs it.";
        if (hdJob)
            progressText = std::string(hdJob->stage == HdJob::Identify ? "Checking " : "Installing ")
                         + hdJob->path.filename().string() + "...";
        else progressText = hdGetNote;
        float footerH = 6.0f + (footStacked ? 42.0f * 2.0f + style.ItemSpacing.y : 42.0f) + style.ItemSpacing.y * 3.0f
                      + style.WindowPadding.y;
        if (!offerText.empty())
            footerH += 4.0f + style.ItemSpacing.y + ImGui::CalcTextSize(offerText.c_str(), nullptr, false, fullW).y
                     + style.ItemSpacing.y + 38.0f + style.ItemSpacing.y;
        if (!progressText.empty())
            footerH += ImGui::CalcTextSize(progressText.c_str(), nullptr, false, fullW).y + style.ItemSpacing.y;
        ImGui::BeginChild("##hdrows", ImVec2(0, -footerH), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        const float width = ImGui::GetContentRegionAvail().x;
        const float buttonW = buttons::buttonWidth(role::kSettingsRow);
        const bool stacked = width < buttonW + 12.0f + 220.0f;
        const float textW = stacked ? width : width - buttonW - 12.0f;
        for (const HdModelSource& source : hdModelSources()) {
            ImGui::PushID(source.row);
            const HdRowInput in = hdRowInput(snap, source.row);
            const HdRowState rowState = hdRowState(in);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
            ImGui::TextColored(cream, "%s", source.name);
            ImGui::PopTextWrapPos();
            const ImVec4 statusColour = rowState == HdRowState::Failed ? ImVec4(1.0f, 0.55f, 0.40f, 1.0f)
                                      : rowState == HdRowState::Installed ? ImVec4(0.60f, 0.92f, 0.50f, 1.0f)
                                      : rowState == HdRowState::NotInstalled ? dim : kOrange;
            ImGui::PushFont(fonts.caption);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
            ImGui::TextColored(statusColour, "%s", hdRowStatusText(in).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            const float textBottom = ImGui::GetCursorScreenPos().y;
            const ImVec2 buttonAt = stacked ? ImVec2(origin.x, textBottom + 4.0f)
                                            : ImVec2(origin.x + width - buttonW, origin.y + std::max(0.0f, (textBottom - origin.y - 40.0f) * 0.5f));
            ImGui::SetCursorScreenPos(buttonAt);
            const HdRowAction action = hdRowAction(in);
            if (action == HdRowAction::Uninstall) {
                if (glassButton("Uninstall", role::kSettingsRow, 40.0f, false, busy)) {
                    for (const HdModel& model : snap.hdModels)
                        if (model.row == source.row) deletingModel = model;
                    hdRowErrors.erase(source.row);
                    uninstallPending = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                    ImGui::Text("Frees %s. Uninstalls only this model's files; the game goes back to the original.",
                                in.size.empty() ? "its space" : in.size.c_str());
                    for (const HdModel& model : snap.hdModels)
                        if (model.row == source.row) ImGui::TextColored(dim, "From: %s", model.path.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            } else if (glassButton(hdRowActionLabel(action), role::kSettingsRow, 40.0f, false, action == HdRowAction::Busy)) {
                hdRowErrors.erase(source.row);
                hdWaitingRows.insert(source.row);
                if (!hdWatcher.watching()) startHdWatch();
                if (SDL_OpenURL(source.url) != 0) {
                    hdRowErrors[source.row] = std::string("Could not open the browser. Visit ") + source.url;
                    hdWaitingRows.erase(source.row);
                }
            }
            if (action != HdRowAction::Uninstall && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                ImGui::Text("Opens the %s page in your browser. Download the zip there (it has %s); this window installs it.",
                            source.name, source.marker);
                ImGui::TextColored(dim, "%s", source.url);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            const float bottom = std::max(textBottom, buttonAt.y + 40.0f);
            ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom + 6.0f));
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (hdZipOffer) {
            ImGui::Dummy(ImVec2(0, 4));
            const std::string name = hdZipOffer->zip.filename().string();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(cream, "%s", offerText.c_str());
            ImGui::PopTextWrapPos();
            if (glassButton("Delete zip", role::kSettingsRow, 38.0f)) {
                std::error_code ec;
                if (fs::is_regular_file(fs::symlink_status(hdZipOffer->zip, ec)) && fs::remove(hdZipOffer->zip, ec) && !ec)
                    hdGetNote = "Deleted " + name + " from Downloads.";
                else
                    hdGetNote = "Could not delete " + hdZipOffer->zip.string() + ".";
                hdGetNoteError = !!ec;
                hdZipOffer.reset();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hdZipOffer ? hdZipOffer->zip.string().c_str() : "");
            ImGui::SameLine(0.0f, 12.0f);
            if (glassButton("Keep it", role::kSettingsRow, 38.0f)) hdZipOffer.reset();
        }
        if (!progressText.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(hdJob ? kOrange : hdGetNoteError ? ImVec4(1.0f, 0.55f, 0.40f, 1.0f) : dim, "%s",
                               progressText.c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::Dummy(ImVec2(0, 6));
        if (glassButton(filePickLabel("hdget", "Import a zip").c_str(), ImVec2(footW, 42.0f), false, filePickOpen())) {
            startFilePick("hdget", "Choose a model zip", "Model zip", "*.zip *.ZIP",
                          [this](const fs::path& zip) { hdFound.push_back({ zip, false }); });
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("For a model zip you downloaded somewhere else.");
            ImGui::EndTooltip();
        }
        if (!footStacked) ImGui::SameLine(0.0f, 12.0f);
        if (glassButton("Close", ImVec2(footW, 42.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || gamepadBackPressed()) {
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops watching Downloads.");
        drawDeleteHdModel(); // Uninstall: the same confirm as the HD Models page
        ImGui::EndPopup();
    }
#endif

    // Modelos de Pikmin propios (creador de modelos): uno por color. Se
    // instalan con el botón Characters de la portada.
    void drawPikminModels(const SettingsSnapshot& snap, bool busy)
    {
        if (snap.pikminModels.empty()) return;
        ImGui::Dummy(ImVec2(0, 14));
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "PIKMIN MODELS");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 0.85f),
                           "Pikmin made with the Model Maker. Install them with the Characters button on the home screen; "
                           "the leaf, bud and flower stay the game's.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        const float width = ImGui::GetContentRegionAvail().x;
        const float comboW = std::min(300.0f, width);
        float nameW = 0.0f;
        for (const PikminModelChoice& choice : snap.pikminModels)
            nameW = std::max(nameW, ImGui::CalcTextSize(choice.name.c_str()).x);
        const auto layout = settingsRowLayout(width, nameW, comboW);
        for (const PikminModelChoice& choice : snap.pikminModels) {
            ImGui::PushID(choice.color.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 1.0f), "%s", choice.name.c_str());
            if (!layout.stacked) ImGui::SameLine(ImGui::GetCursorPosX() + layout.controlsX);
            ImGui::SetNextItemWidth(comboW);
            std::string current = "Game";
            for (const auto& [id, name] : choice.options)
                if (id == choice.selected) current = name;
            ImGui::BeginDisabled(busy || choice.options.size() < 2);
            if (handCombo("##model", current.c_str())) {
                for (const auto& [id, name] : choice.options) {
                    ImGui::PushID(id.c_str());
                    if (handSelectable(name.c_str(), id == choice.selected) && id != choice.selected)
                        queue({ "--pikmin-model", choice.color, id });
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

#ifndef __ANDROID__
    void setGciNote(std::string note, bool isError)
    {
        gciNote = std::move(note);
        gciNoteIsError = isError;
    }

    void tryImportGciSave()
    {
        gciNote.clear();
        const GameInstall& game = state.games[settingsGame];
        if (!game.installed || game.directory.empty()) {
            setGciNote("Install this game before importing a save.", true);
            return;
        }
        if (!platform::hasGraphicalDialogs()) {
            setGciNote("Import needs a graphical file picker (zenity or kdialog on Linux).", true);
            return;
        }
        const int gameIndex = settingsGame;
        // .gci exports, plus Open Nectar's own card files, which have no
        // extension ("Pikmin dataFile", "Pikmin2_SaveData").
        startFilePick("gci", "Import a save", "GameCube or Open Nectar save", "*.gci *.GCI *dataFile *_SaveData",
                      [this, gameIndex](const fs::path& picked) { importPickedSave(gameIndex, picked); });
    }

    // Runs between frames once the picker returns a file (see finishFilePick).
    void importPickedSave(int gameIndex, const fs::path& picked)
    {
        const GameInstall& game = state.games[gameIndex];
        const std::string discId = discIdOrInstalled(gameIndex, game.discId, game.directory);
        const fs::path card0 = card0DirectoryForInstall(fs::path(game.directory));
        PreparedSave save;
        std::string error;
        switch (prepareSaveImport(picked, discId, card0, save, error)) {
        case PrepareResult::Error: setGciNote(error, true); return;
        case PrepareResult::AlreadyCurrent: setGciNote(error, false); return;
        case PrepareResult::Ready: break;
        }

        std::error_code ec;
        bool overwrite = false;
        if (fs::exists(card0 / save.saveFileName, ec) || fs::exists(card0 / (".meta_" + save.saveFileName), ec)) {
            if (!askModal) {
                setGciNote("A save already exists; the launcher could not show a confirmation dialog.", true);
                return;
            }
            // One file holds every slot and the options, in both games.
            const std::string prompt =
                std::string("Importing replaces all save slots and options on this install's memory card with the "
                            "ones in ")
                + (save.native ? "the save file you picked." : "the .gci file.")
                + "\n\nYour current save is backed up first, and you can bring it back from this page with "
                  "\"Restore previous save\".";
            const int answer = askModal("Replace your save?", prompt, "Replace", "", false, "Cancel");
            if (answer != 0) return;
            overwrite = true;
        }
        CardBackup backup;
        if (!importPreparedSave(save, card0, overwrite, error, &backup)) {
            setGciNote(error, true);
            return;
        }
        toast = "Imported \"" + save.saveFileName + "\" to the memory card. It will appear on the file select screen.";
        toastTime = 4.5f;
        setGciNote(backup.dir.empty() ? toast : toast + " Your previous save was backed up.", false);
        savesStatusGame = -1; // re-read the card on the next frame
    }

    void tryRestoreSave()
    {
        gciNote.clear();
        const int gameIndex = settingsGame;
        betweenFrames = [this, gameIndex] { restorePreviousSave(gameIndex); };
    }

    // Runs between frames (see runBetweenFrames): it asks first.
    void restorePreviousSave(int gameIndex)
    {
        const fs::path card0 = card0DirectoryForInstall(fs::path(state.games[gameIndex].directory));
        const std::vector<CardBackup> backups = listCardBackups(card0);
        if (backups.empty()) {
            setGciNote("There is no backup to restore.", true);
            return;
        }
        const std::string when = describeBackupTime(backups.front());
        if (!askModal) {
            setGciNote("The launcher could not show a confirmation dialog.", true);
            return;
        }
        const std::string prompt =
            "This puts back the save that was backed up on " + when
            + ", replacing all save slots and options on this install's memory card.\n\nYour current save is "
              "backed up first, so you can undo this with \"Restore previous save\".";
        const int answer = askModal("Restore previous save?", prompt, "Restore", "", false, "Cancel");
        if (answer != 0) return;
        std::string error;
        if (!restoreLatestCardBackup(card0, error)) {
            setGciNote(error, true);
            return;
        }
        toast = "Restored the save from " + when + ". It will appear on the file select screen.";
        toastTime = 4.5f;
        setGciNote(toast + " The save it replaced was backed up.", false);
        savesStatusGame = -1;
    }

    // One line about this game's memory card: which save files are on it.
    void refreshSavesStatus(float dt)
    {
        savesStatusAge += dt;
        if (savesStatusGame == settingsGame && savesStatusAge < 2.0f) return;
        savesStatusGame = settingsGame;
        savesStatusAge = 0.0f;
        savesStatus.clear();
        savesBackupTime.clear();
        savesFolderExists = false;
        backupsFolderExists = false;
        const GameInstall& game = state.games[settingsGame];
        if (game.directory.empty()) return;
        const fs::path card0 = card0DirectoryForInstall(fs::path(game.directory));
        const std::vector<CardBackup> backups = listCardBackups(card0);
        if (!backups.empty()) savesBackupTime = describeBackupTime(backups.front());
        std::error_code backupsEc;
        backupsFolderExists = fs::is_directory(cardBackupRoot(card0), backupsEc);
        std::error_code ec;
        std::vector<std::string> names;
        if (fs::is_directory(card0, ec)) {
            savesFolderExists = true;
            for (const auto& entry : fs::directory_iterator(card0, ec)) {
                const std::string name = entry.path().filename().string();
                if (!name.empty() && name[0] != '.' && entry.is_regular_file(ec)) names.push_back(name);
            }
        }
        const char* gameName = settingsGame == 0 ? "Pikmin" : "Pikmin 2";
        savesStatus = names.empty()
                          ? "No save yet. An imported save will appear on the file select screen."
                          : std::string("You already have a ") + gameName
                                + " save. Importing will ask before replacing it.";
    }

    void openFolder(const fs::path& folder)
    {
        // file:// URL: forward slashes, a leading slash before a drive letter,
        // and the characters a URL can't carry as-is escaped.
        const std::string path = folder.generic_string();
        std::string url = path.empty() || path[0] != '/' ? "file:///" : "file://";
        for (const char c : path) {
            if (c == ' ' || c == '%' || c == '#' || c == '?') {
                char escaped[4];
                snprintf(escaped, sizeof(escaped), "%%%02X", static_cast<unsigned char>(c));
                url += escaped;
            } else {
                url += c;
            }
        }
        if (SDL_OpenURL(url.c_str()) != 0) setGciNote("Could not open the folder: " + folder.string(), true);
    }

    fs::path saveCardFolder() const
    {
        return card0DirectoryForInstall(fs::path(state.games[settingsGame].directory));
    }

    // Saves page, laid out like Texture Packs.
    void drawSaves(bool busy, float dt)
    {
        refreshSavesStatus(dt);
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "SAVE FILES");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 0.85f),
                           "Bring a save into this install: a GameCube export (.gci) from Dolphin or a real "
                           "memory card, or an Open Nectar save file from another install or a backup. It shows up "
                           "on the game's file select screen.");
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "%s", savesStatus.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 10));
        // Share a measured width, but stack button pairs when the panel is narrow.
        // Include the picker state so opening a picker cannot change the layout.
        float savesButtonW = 240.0f;
        for (const char* label : {"Import a save", "Open save folder", "Restore previous save", "Open backups folder",
                                  "Waiting for file picker..."})
            savesButtonW = std::max(savesButtonW, ImGui::CalcTextSize(label).x + 48.0f);
        const bool paired = settingsButtonsFit(ImGui::GetContentRegionAvail().x, savesButtonW);
        const ImVec2 savesButtonSize(std::min(savesButtonW, ImGui::GetContentRegionAvail().x), 46.0f);
        if (glassButton(filePickLabel("gci", "Import a save").c_str(), savesButtonSize, false,
                        busy || filePickOpen()))
            tryImportGciSave();
        if (paired) ImGui::SameLine(0.0f, 12.0f);
        if (glassButton("Open save folder", savesButtonSize, false, !savesFolderExists)) openFolder(saveCardFolder());

        ImGui::Dummy(ImVec2(0, 18));
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "BACKUPS");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f),
                           "Before a save is replaced, the old one is copied to a backup folder. The newest %d are kept.",
                           kCardBackupKeep);
        if (!savesBackupTime.empty()) {
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "Latest backup: %s", savesBackupTime.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 10));
        if (!savesBackupTime.empty()) {
            if (glassButton("Restore previous save", savesButtonSize, false, busy || filePickOpen()))
                tryRestoreSave();
            if (paired) ImGui::SameLine(0.0f, 12.0f);
        }
        if (glassButton("Open backups folder", savesButtonSize, false, !backupsFolderExists))
            openFolder(cardBackupRoot(saveCardFolder()));
    }
#endif

    void drawSettings(float top, ImVec2 size, float dt)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Un apartado por juego: cada uno con sus propias opciones.
        // Y un último apartado, "Launcher", con las opciones del propio launcher.
        const char* gameNames[kHubGameCount + 1] = { "Pikmin", "Pikmin 2", "Launcher" };
        const bool launcherPage = bool(state.settingsFolder);
        const int pills = kHubGameCount + (launcherPage ? 1 : 0);
        if (settingsGame >= pills) settingsGame = 0;
        const float pillW = buttons::buttonWidth(role::kSettingsGame), pillGap = buttons::kButtonGap;
        float pillX = (size.x - (float(pills) * pillW + float(pills - 1) * pillGap)) * 0.5f;
        for (int g = 0; g < pills; ++g) {
            ImGui::SetCursorScreenPos(ImVec2(pillX, top + 16.0f));
            ImGui::PushID(1000 + g);
            const bool unavailable = g < kHubGameCount && !settingsClients[g];
            if (glassButton(gameNames[g], role::kSettingsGame, 38.0f, g == settingsGame, unavailable) && g != settingsGame) {
                settingsGame = g;
                settingsGroup = 0;
                launcherInfoFresh = false;
                launcherMessage.clear();
                optionOrder.clear();
#ifndef __ANDROID__
                gciNote.clear();
#endif
            }
            ImGui::PopID();
            pillX += pillW + pillGap;
        }
        top += 54.0f;
        if (settingsGame == kHubGameCount) {
            settings = nullptr;
            drawLauncherSettings(top, size);
            return;
        }
        settings = settingsClients[settingsGame];
        if (!settings) {
            centredText(dl, fonts.body, 21.0f, ImVec2(size.x * 0.5f, top + 80.0f), col(kOrange),
                        "Settings are available once the game is installed.");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(settings->mutex);
            drawSettingsLocked(top, size, dt);
        }
        if (!pendingPackInstall.empty()) {
            for (const auto& download : texturePackDownloads(settingsGame)) {
                const std::string id = std::string(download.label) == "4K" ? "hd-4k" : "hd-1080p";
                if (id == pendingPackInstall) {
                    SettingsClient::installPack(settings, installingPack, download);
                    break;
                }
            }
            pendingPackInstall.clear();
        }
        if (!pendingRequest.empty()) {
            SettingsClient::start(settings, std::move(pendingRequest), pendingLabel);
            pendingRequest.clear();
        }
    }

    // Etiqueta de una fila de ajustes (ver fitSettingRow): en una línea, partida
    // en dos al lado del control o entera encima de él; nunca recortada. Deja el
    // cursor donde va el control y devuelve su ancho.
    float settingRowLabel(const std::string& text, const ImVec4& colour, float innerW)
    {
        const float lineH = std::max(1.0f, ImGui::GetTextLineHeight());
        auto lines = [&](float width) {
            return int(std::lround(ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y / lineH));
        };
        const SettingRowFit fit = fitSettingRow(innerW, ImGui::CalcTextSize(text.c_str()).x, lines);
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fit.labelMaxW);
        ImGui::TextColored(colour, "%s", text.c_str());
        ImGui::PopTextWrapPos();
        const float controlX = ImGui::GetContentRegionMax().x - fit.controlW;
        if (fit.mode == SettingRowMode::Stacked) ImGui::SetCursorPosX(controlX);
        else ImGui::SameLine(controlX);
        return fit.controlW;
    }

    // Settings > Launcher: where the launcher keeps its own settings. Same
    // layout as a game's settings page (list, rows, help bar).
    void drawLauncherSettings(float top, ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Read once per visit (and after a switch), not every frame.
        if (!launcherInfoFresh) {
            launcherInfo = state.settingsFolder();
            launcherInfoFresh = true;
        }
        const SettingsFolderInfo& info = launcherInfo;
        const float margin = std::max(40.0f, (size.x - 1000.0f) * 0.5f);
        const float listW = 180.0f;
        const float panelX = margin + listW + 18.0f;
        const float panelW = size.x - margin - panelX;
        const float bottom = size.y - 64.0f;
        // Como en las páginas de un juego: en ventanas bajas la ayuda cede ante las filas.
        const SettingsPanelFit panelFit = fitSettingsPanel(bottom - (top + 24.0f));
        const float helpH = panelFit.helpH;

        ImGui::SetCursorScreenPos(ImVec2(margin, top + 24.0f));
        glassButton("Folders", ImVec2(listW, 40.0f), true);

        ImGui::SetCursorScreenPos(ImVec2(panelX, top + 24.0f));
        ImGui::BeginChild("launcherRows", ImVec2(panelW, panelFit.rowsH),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollWithMouse);
        const float innerW = ImGui::GetContentRegionAvail().x;
        // Como en las filas de un juego (settingRowLabel).
        auto rowLabel = [&](const char* text, const ImVec4& colour) { return settingRowLabel(text, colour, innerW); };
        const ImVec4 label(1.0f, 0.93f, 0.80f, 1.0f), dim(0.62f, 0.58f, 0.44f, 1.0f);
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "%s", "Where settings are kept");
        ImGui::PopFont();
        ImGui::Separator();

        std::string help;
        int choice = -1; // 1 = on, 0 = off
        const char* options[2] = { "Off", "On" };
        {
            const float rowY = ImGui::GetCursorPosY();
            const float controlW = rowLabel("Keep settings in the game folder", info.canChange ? label : dim);
            ImGui::SetNextItemWidth(controlW);
            ImGui::BeginDisabled(!info.canChange);
            const int current = info.keepInGameFolder ? 1 : 0;
            if (handCombo("##keepInGameFolder", options[current])) {
                for (int i = 1; i >= 0; --i) {
                    if (handSelectable(options[i], i == current) && i != current) choice = i;
                    if (i == current) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            const float rowH = ImGui::GetCursorPosY() - rowY;
            const ImVec2 rowMin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + rowY - ImGui::GetScrollY());
            if (ImGui::IsMouseHoveringRect(rowMin, ImVec2(rowMin.x + ImGui::GetWindowWidth(), rowMin.y + rowH)))
                help = "On: the launcher's settings and cache live inside the game folder, so they move with it. "
                       "Off: they live in your user folder. Switching copies them across; nothing is deleted.";
            // Locked (portable.txt, NECTAR_PORTABLE, read-only folder...): say
            // why right under the row, not only in the help bar.
            if (!info.canChange && !info.note.empty()) {
                ImGui::PushFont(fonts.caption);
                ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
                ImGui::TextColored(dim, "%s", info.note.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 4));
            }
        }
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(label, "%s", "Settings folder");
            // La ruta entera en su propia línea, partida si hace falta: en el
            // ancho del control salía recortada por delante (issue #207).
            ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
            ImGui::TextColored(dim, "%s", info.folder.c_str());
            ImGui::PopTextWrapPos();
        }
        wheelScroll(launcherRowsScroll);
        ImGui::EndChild();

        if (choice >= 0 && state.setKeepSettingsInGameFolder) {
            launcherMessageIsError = !state.setKeepSettingsInGameFolder(choice == 1, launcherMessage);
            launcherInfoFresh = false;
        }

        const ImVec2 a(panelX, bottom - helpH), b(panelX + panelW, bottom);
        if (helpH > 0.0f) {
            dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 215), 14.0f);
            dl->AddRect(a, b, col(kOrangeDeep, 0.6f), 14.0f, 0, 1.5f);
            std::string note = info.note;
            ImVec4 noteColour(1.0f, 0.93f, 0.80f, 0.95f);
            // While locked the reason stays first, even when hovering the row.
            if (!help.empty()) note = info.canChange || note.empty() ? help : note + "\n" + help;
            else if (!launcherMessage.empty()) {
                note = launcherMessage;
                if (launcherMessageIsError) noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f);
            }
            dl->PushClipRect(a, b, true);
            dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 16.0f, a.y + 8.0f), col(noteColour), note.c_str(), nullptr, panelW - 32.0f);
            dl->PopClipRect();
        }
    }

    void drawSettingsLocked(float top, ImVec2 size, float dt)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool busy = settings->busy;
        if (!settings->loaded) {
            centredText(dl, fonts.body, 21.0f, ImVec2(size.x * 0.5f, top + 80.0f), col(kOrange),
                        settings->error.empty() ? "Reading the game's settings..." : settings->error.c_str());
            if (!settings->error.empty() && !busy) {
                ImGui::SetCursorScreenPos(ImVec2((size.x - buttons::buttonWidth(role::kSmallAction)) * 0.5f, top + 120.0f));
                if (glassButton("Retry", role::kSmallAction, 44.0f)) queue({ "--settings-dump" });
            }
            return;
        }
        const SettingsSnapshot& snap = settings->snapshot;
        if ((awaitingPackResult || settings->packResultReady) && !busy && pendingRequest.empty()) {
            if (!snap.message.empty()) notify(snap.message);
            else if (!settings->error.empty()) notify(settings->error);
            toastTime = 8.0f;
            awaitingPackResult = false;
            settings->packResultReady = false;
        }
        const std::vector<SettingsGroup>& groups = snap.groups;
        if (groups.empty()) return;
        // Los paquetes antiguos de Pikmin 1 añaden dos páginas administrativas.
        // Fusion/Pikmin 2 declara exactF1: sus grupos vienen enteros del juego y
        // no se les debe mezclar ninguna página heredada.
        const int packsPage = int(groups.size()), modelsPage = packsPage + 1;
#ifndef __ANDROID__
        // Saves: the one launcher page every game gets, exactF1 included.
        const int savesPage = snap.exactF1 ? int(groups.size()) : modelsPage + 1;
        const int lastPage = savesPage;
#else
        const int savesPage = -1;
        const int lastPage = snap.exactF1 ? int(groups.size()) - 1 : modelsPage;
#endif
        settingsGroup = std::clamp(settingsGroup, 0, lastPage);

        const float margin = std::max(40.0f, (size.x - 1000.0f) * 0.5f);
        const float listW = buttons::buttonWidth(role::kSettingsGroup);
        const float panelX = margin + listW + 18.0f;
        const float panelW = size.x - margin - panelX;
        const float bottom = size.y - 64.0f;
        // Algunas opciones de desarrollo documentan varias teclas: con sitio
        // caben cuatro líneas. En ventanas bajas la caja cede ante las filas.
        SettingsPanelFit panelFit = fitSettingsPanel(bottom - (top + 24.0f));
        if (!snap.exactF1 && settingsGroup == packsPage) {
            panelFit.helpH = 0;
            panelFit.rowsH = std::max(1.0f, bottom - (top + 24.0f));
        }
        const float helpH = panelFit.helpH;

        // Grupos a la izquierda, como pestañas verticales. Tienen que acabar por
        // encima de "Reset all" con cualquier altura y número de páginas.
        const float resetH = 44.0f;
        const float listTop = top + 24.0f;
        const float listSpace = bottom - resetH - 12.0f - listTop;
        const SidebarLayout sidebar = fitSidebar(lastPage + 1, listSpace);
        ImVec2 listOrigin(margin, listTop);
        if (sidebar.scroll) {
            ImGui::SetCursorScreenPos(listOrigin);
            ImGui::BeginChild("##pages", ImVec2(listW, std::max(1.0f, listSpace)), ImGuiChildFlags_NavFlattened,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            // Al encoger la ventana (o cambiar de página o de juego) la página
            // elegida podría quedar fuera; solo entonces se mueve la lista, para
            // no pelear con la rueda.
            if (listSpace != pagesShownView || settingsGroup != pagesShownGroup) {
                pagesShownView = listSpace;
                pagesShownGroup = settingsGroup;
                const float y = ImGui::GetScrollY();
                const float want = sidebarScrollToShow(settingsGroup, sidebar, listSpace, y);
                if (want != y) ImGui::SetScrollY(want);
            }
            listOrigin = ImGui::GetCursorScreenPos();
        } else {
            pagesShownView = -1.0f;
        }
        for (int g = 0; g <= lastPage; ++g) {
            const char* name = g == savesPage ? "Saves"
                             : g == packsPage ? "Texture Packs"
                             : g == modelsPage ? "HD Models"
                                               : groups[size_t(g)].name.c_str();
            ImGui::SetCursorScreenPos(ImVec2(listOrigin.x, listOrigin.y + float(g) * sidebar.step));
            ImGui::PushID(g);
            if (glassButton(name, ImVec2(listW, sidebar.height), g == settingsGroup)) settingsGroup = g;
            ImGui::PopID();
        }
        if (sidebar.scroll) {
            // Reserva el alto completo para que la región sepa cuánto puede desplazarse.
            ImGui::SetCursorScreenPos(ImVec2(listOrigin.x, listOrigin.y + sidebar.content));
            ImGui::Dummy(ImVec2(1.0f, 0.0f));
            ImGui::EndChild();
        }
        // Restaurar: pide un segundo clic para confirmar.
        resetArmed = std::max(0.0f, resetArmed - dt);
        ImGui::SetCursorScreenPos(ImVec2(margin, resetAllY(listTop, sidebar.content, sidebar.scroll, bottom, resetH)));
        if (glassButton(resetArmed > 0.0f ? "Sure? Click" : "Reset all", ImVec2(listW, resetH), false, busy)) {
            if (resetArmed > 0.0f) {
                resetArmed = 0.0f;
                optionOrder.clear();
                queue({ "--settings-reset" });
            } else {
                resetArmed = 3.0f;
            }
        }

        // Filas del grupo, con scroll.
        ImGui::SetCursorScreenPos(ImVec2(panelX, top + 24.0f));
        ImGui::BeginChild("rows", ImVec2(panelW, panelFit.rowsH),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollWithMouse);
        int pick[2] = { -1, -1 }; // fila y opción elegidas este fotograma
        std::string help;
        const SettingsGroup* shownGroup = settingsGroup < packsPage ? &groups[size_t(settingsGroup)] : nullptr;
        if (!snap.exactF1 && settingsGroup == packsPage) drawTexturePacks(snap, busy);
        if (!snap.exactF1 && settingsGroup == modelsPage) drawHdModels(snap, busy);
#ifndef __ANDROID__
        else stopHdWatch(); // the Get HD models window only lives on that page
#endif
#ifndef __ANDROID__
        if (settingsGroup == savesPage) drawSaves(busy, dt);
#endif
        if (shownGroup) {
        const SettingsGroup& group = *shownGroup;
        const float innerW = ImGui::GetContentRegionAvail().x;
        const ImVec4 dim(0.62f, 0.58f, 0.44f, 1.0f);
        for (const SettingsRow& row : group.rows) {
            if (!row.section.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushFont(fonts.caption);
                ImGui::TextColored(kOrangeDeep, "%s", row.section.c_str());
                ImGui::PopFont();
                ImGui::Separator();
            }
            ImGui::PushID(row.row);
            const float rowY = ImGui::GetCursorPosY();
            const float controlW = settingRowLabel(row.label, row.enabled ? ImVec4(1.0f, 0.93f, 0.80f, 1.0f) : dim, innerW);
            ImGui::SetNextItemWidth(controlW);
            if (row.editable()) {
                ImGui::BeginDisabled(busy);
                if (handCombo("##value", row.value.c_str())) {
                    for (const std::string& option : stableOptions(group.id, row)) {
                        const bool selected = option == row.value;
                        if (handSelectable(option.c_str(), selected) && !selected) {
                            const auto it = std::find(row.options.begin(), row.options.end(), option);
                            if (it != row.options.end()) { pick[0] = row.row; pick[1] = int(it - row.options.begin()); }
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
            } else {
                // Sin lista: se enseña el valor entero (partido si no cabe) y,
                // si hace falta, dónde cambiarlo en su propia línea debajo.
                // Antes iba todo en una línea y salía recortado (issue #191).
                ImGui::AlignTextToFramePadding();
                ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
                ImGui::TextColored(dim, "%s", row.value.c_str());
                if (row.enabled && (row.picker || row.action)) {
                    // Right under the value, not a whole frame-height line below.
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f));
                    ImGui::PushFont(fonts.caption);
                    ImGui::TextColored(dim, "%s", "In game: F1 or backtick");
                    ImGui::PopFont();
                }
                ImGui::PopTextWrapPos();
            }
            // Ayuda de la fila bajo el ratón.
            const float rowH = ImGui::GetCursorPosY() - rowY;
            const ImVec2 rowMin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + rowY - ImGui::GetScrollY());
            if (ImGui::IsMouseHoveringRect(rowMin, ImVec2(rowMin.x + ImGui::GetWindowWidth(), rowMin.y + rowH))) help = row.help;
            ImGui::PopID();
        }
        }
        wheelScroll(rowsScroll);
        ImGui::EndChild();

        // Barra de ayuda debajo (si cabe).
        const ImVec2 a(panelX, bottom - helpH), b(panelX + panelW, bottom);
        if (helpH > 0.0f) {
            dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 215), 14.0f);
            dl->AddRect(a, b, col(kOrangeDeep, 0.6f), 14.0f, 0, 1.5f);
            // Prioridad: trabajando, error, resultado de instalar, ayuda de la fila.
            std::string note = help;
            ImVec4 noteColour(1.0f, 0.93f, 0.80f, 0.95f);
            if (busy) note = settings->busyLabel;
            else if (!settings->error.empty()) { note = settings->error; noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f); }
#ifndef __ANDROID__
            else if (settingsGroup == savesPage && help.empty()) {
                note = gciNote;
                if (gciNoteIsError) noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f);
            }
#endif
            else if (!snap.message.empty() && help.empty() && messageGame == settingsGame && messageGroup == settingsGroup) {
                note = snap.message;
                if (snap.messageIsError) noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f);
            }
            // Vacía, la caja explica para qué sirve en vez de quedarse en blanco.
            if (note.empty() && shownGroup) {
                note = "Hover a setting to see what it does.";
                noteColour = ImVec4(0.62f, 0.58f, 0.44f, 1.0f);
            }
            dl->PushClipRect(a, b, true);
            dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 16.0f, a.y + 8.0f), col(noteColour), note.c_str(), nullptr, panelW - 32.0f);
            dl->PopClipRect();
        }

        if (pick[0] >= 0 && shownGroup) {
            queue({ "--settings-set", std::to_string(shownGroup->id), std::to_string(pick[0]),
                                              std::to_string(pick[1]) });
        }
    }

    // Caja de ruta de solo lectura.
    void pathBox(ImVec2 a, ImVec2 b, const std::string& value, const char* empty)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 235), 14.0f);
        dl->AddRect(a, b, col(kOrangeDeep, 0.8f), 14.0f, 0, 2.0f);
        const bool has = !value.empty();
        const std::string shown = has ? fitTail(fonts.body, 19.0f, value, b.x - a.x - 28.0f) : empty;
        dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 14.0f, a.y + (b.y - a.y - 19.0f) * 0.5f),
                    has ? col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)) : col(kOrange, 0.5f), shown.c_str());
    }

    // Comprueba la carpeta elegida y decide dónde irá la instalación: si ya
    // tiene cosas, dentro de una subcarpeta "Open Nectar".
    bool resolveMoveTarget(const fs::path& chosen, fs::path& target, std::string& problem)
    {
        std::error_code ec;
        const fs::path current = fs::weakly_canonical(state.games[0].directory, ec);
        target = fs::weakly_canonical(chosen, ec);
        if (fs::exists(target) && !fs::is_empty(target, ec)) target /= "Open Nectar";
        if (fs::exists(target) && !fs::is_empty(target, ec)) {
            problem = "That folder already has an \"Open Nectar\" folder in it. Choose another one.";
            return false;
        }
        const std::string from = current.string(), to = target.string();
        if (to == from || (to.size() > from.size() && to.compare(0, from.size(), from) == 0
                           && (to[from.size()] == '/' || to[from.size()] == '\\'))) {
            problem = "The new folder cannot be inside the current installation.";
            return false;
        }
        return true;
    }

    void drawMove(float top, ImVec2 size)
    {
        // El panel mide 380 px: en ventanas bajas primero se acerca a las
        // pestañas y, si ni así cabe, la página pasa a tener scroll.
        const float panelH = 380.0f;
        const PageFit fit = fitPage(panelH, pageBottom(size) - top, 30.0f, 12.0f);
        if (fit.scroll) top = beginPageScroll("##move", top, size);
        drawMovePanel(top + fit.top, size, panelH);
        if (fit.scroll) endPageScroll(top, fit.content);
    }

    void drawMovePanel(float y, ImVec2 size, float panelH)
    {
        const float width = std::min(size.x - 80.0f, 760.0f);
        const float x = (size.x - width) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 panelA(x, y), panelB(x + width, y + panelH);
        dl->AddRectFilled(panelA, panelB, IM_COL32(8, 32, 12, 215), 20.0f);
        dl->AddRect(panelA, panelB, col(kOrange), 20.0f, 0, 2.5f);
        const float inner = x + 30.0f, innerW = width - 60.0f;
        y += 22.0f;
        dl->AddText(fonts.title, 30.0f, ImVec2(inner, y), col(kOrange), "Move Install");
        y += 46.0f;
        dl->AddText(fonts.caption, 16.0f, ImVec2(inner, y), col(kOrange, 0.75f),
                    "Your saves, settings and texture packs move with the game.");
        y += 34.0f;
        dl->AddText(fonts.body, 20.0f, ImVec2(inner, y), col(kOrange), "Current folder");
        pathBox(ImVec2(inner, y + 28.0f), ImVec2(inner + innerW, y + 70.0f), state.games[0].directory, "");
        y += 88.0f;
        dl->AddText(fonts.body, 20.0f, ImVec2(inner, y), col(kOrange), "New folder");

        const int jobState = moveJob ? moveJob->state.load() : MoveJob::Idle;
        const bool running = jobState == MoveJob::Running;
        const float browseW = buttons::buttonWidth(role::kSmallAction), moveW = buttons::buttonWidth(role::kMove);
        pathBox(ImVec2(inner, y + 28.0f), ImVec2(inner + innerW - browseW - buttons::kButtonGap, y + 72.0f), moveTarget,
                "No folder chosen");
        ImGui::SetCursorScreenPos(ImVec2(inner + innerW - browseW, y + 28.0f));
        if (glassButton(filePickLabel("move", "Browse", "Waiting...").c_str(), role::kSmallAction, 44.0f, false,
                        running || filePickOpen())) {
            // Off the render thread, like the file pickers: the window keeps
            // drawing while the folder chooser is open.
            startPick("move", [] { return platform::askForInstallDirectory(); }, [this](const fs::path& chosen) {
                fs::path target;
                moveNote.clear();
                if (resolveMoveTarget(chosen, target, moveNote)) moveTarget = target.string();
                else moveTarget.clear();
            });
        }
        y += 96.0f;

        if (running) {
            // Barra de progreso de la copia.
            const ImVec2 a(inner, y + 4.0f), b(inner + innerW - moveW - 20.0f, y + 34.0f);
            const float fill = std::max(b.y - a.y, (b.x - a.x) * moveJob->progress.load());
            dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 235), 15.0f);
            dl->AddRectFilled(ImVec2(a.x + 3, a.y + 3), ImVec2(a.x + fill - 3, b.y - 3), col(kLeafBright), 12.0f);
            dl->AddRect(a, b, col(kOrange), 15.0f, 0, 2.0f);
        } else if (jobState == MoveJob::Failed) {
            std::lock_guard<std::mutex> lock(moveJob->mutex);
            moveNote = moveJob->error;
        }
        if (!moveNote.empty() && !running) {
            dl->AddText(fonts.caption, 16.0f, ImVec2(inner, y + 10.0f), col(ImVec4(1.0f, 0.55f, 0.40f, 1.0f)), moveNote.c_str(),
                        nullptr, innerW - moveW - 30.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(inner + innerW - moveW, y));
        if (glassButton(running ? "Moving..." : "Move", role::kMove, 44.0f, true, running || moveTarget.empty())) {
            moveNote.clear();
            moveJob = std::make_shared<MoveJob>();
            moveJob->state = MoveJob::Running;
            std::thread(MoveJob::run, moveJob, fs::path(state.games[0].directory), fs::path(moveTarget)).detach();
        }

        if (jobState == MoveJob::Done) {
            // Arranca el launcher desde su nuevo sitio. Si se copió, él borra
            // la carpeta vieja.
            const fs::path launcher = fs::path(moveTarget) / state.launcherName;
            std::vector<std::string> arguments;
            if (moveJob->copied) arguments = { "--moved-from", state.games[0].directory };
            platform::relaunch(launcher, arguments);
            moveJob.reset();
            moveNote = "Moved to " + moveTarget + ". Open the launcher from there.";
        }
    }

    // Aviso de versión nueva, arriba a la derecha; pulsarlo es como Update.
    void drawReleaseNotice(ImVec2 size, float t)
    {
        if (!releaseCheck || !releaseCheck->done) return;
        std::string text;
        {
            std::lock_guard<std::mutex> lock(releaseCheck->mutex);
            if (!releaseCheck->newer) return;
            text = "Open Nectar " + releaseCheck->latest.version + " is available";
        }
        // Píldora a la medida del texto, con el mismo margen que los botones.
        const ImVec2 b(size.x - 18.0f, 58.0f);
        const ImVec2 a(b.x - buttons::fitWidth(text, b.y - 18.0f, 19.0f, measureLabel()), 18.0f);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##release", ImVec2(b.x - a.x, b.y - a.y));
        const ImGuiID id = ImGui::GetItemID();
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
        dl->AddRectFilled(ImVec2(a.x - 4, a.y - 4), ImVec2(b.x + 4, b.y + 4), col(kOrange, 0.10f + 0.12f * pulse), 24.0f);
        const dewdrop::Layers layers = dewdropFrame(dl, id, a, b, hovered, held, true, false);
        dewdropLabel(dl, a, ImVec2(b.x - a.x, b.y - a.y), text.c_str(), 19.0f, layers);
        if (clicked) choose(HubAction::Update, HubGame::Pikmin1);

        // "What's new" a su izquierda, si el release trae notas.
        std::string notes, version;
        {
            std::lock_guard<std::mutex> lock(releaseCheck->mutex);
            notes = releaseCheck->latest.notes;
            version = releaseCheck->latest.version;
        }
        if (notes.empty()) return;
        const float w = buttons::buttonWidth(role::kAction);
        ImGui::SetCursorScreenPos(ImVec2(a.x - w - buttons::kButtonGap, a.y));
        if (glassButton("What's new", role::kAction, b.y - a.y)) {
            whatsNewOpen = true;
            whatsNewVersion = version;
            whatsNewNotes = notes;
            whatsNewNotesH = -1.0f;
        }
    }

    // Versión de este launcher (#327) a la derecha del logo, sobre la línea
    // base de sus letras y con su aspecto: Game Bubble en naranja, la mitad de
    // arriba más clara, brillos blancos y un contorno nítido. Se pinta en la
    // CPU (version_label.cpp) a los píxeles reales de la pantalla.
    void drawVersionLabel(ImVec2 size, ImVec2 logoAt, float logoW, float logoH)
    {
        const std::string text = versionLabelText(currentVersion());
        if (text.empty()) return;
        const float scale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x);
        const float ink = std::round(versionLabelInkHeight(logoH) * scale);
        if (text != versionText || ink != versionInk) {
            freeTexture(versionTex);
            versionText = text;
            versionInk = ink;
            if (renderVersionLabel(kFontGameBubble, kFontGameBubbleSize, text, ink, versionImage))
                versionTex = uploadTexture(versionImage.rgba, versionImage.width, versionImage.height, 0);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Siempre en la misma base; el aviso de actualización (y = 18 a 58, más
        // 4 px de halo) queda por encima en todos los tamaños (ver los tests).
        if (!versionTex.valid()) {
            // Sin la fuente: el texto del launcher, en el naranja del logo,
            // con su base en la del logo.
            const float textSize = std::max(16.0f, versionLabelInkHeight(logoH) * 1.4f);
            const float ascent = fonts.body->Ascent * textSize / fonts.body->FontSize;
            const ImVec2 t = fonts.body->CalcTextSizeA(textSize, FLT_MAX, 0.0f, text.c_str());
            const LabelRect r
                = placeVersionLabel(size.x, logoAt.x, logoAt.y, logoW, logoH, t.x, ascent);
            dl->AddText(fonts.body, textSize, ImVec2(std::round(r.x0), std::round(r.y0)), col(kOrange), text.c_str());
            return;
        }
        // La caja de tinta de las cifras se apoya en la base; el contorno sobresale.
        const VersionLabelImage& img = versionImage;
        const LabelRect r = placeVersionLabel(size.x, logoAt.x, logoAt.y, logoW, logoH,
                                              (img.inkRight - img.inkLeft) / scale, (img.inkBottom - img.inkTop) / scale);
        // A píxel entero de la pantalla, para que la imagen salga 1:1.
        const ImVec2 a(std::round(r.x0 * scale - img.inkLeft) / scale, std::round(r.y1 * scale - img.inkBottom) / scale);
        const ImVec2 b(a.x + float(img.width) / scale, a.y + float(img.height) / scale);
        dl->AddImage(versionTex.imgui(), a, b);
    }

    // Quita el marcado en línea de Markdown: **negrita**, `código` y
    // [texto](enlace) se quedan en su texto.
    static std::string plainInline(const std::string& in)
    {
        std::string out;
        for (size_t i = 0; i < in.size(); ++i) {
            if (in.compare(i, 2, "**") == 0) { ++i; continue; }
            if (in[i] == '`') continue;
            if (in[i] == '[') {
                const size_t close = in.find("](", i);
                const size_t end = close == std::string::npos ? close : in.find(')', close);
                if (end != std::string::npos) {
                    out += in.substr(i + 1, close - i - 1);
                    i = end;
                    continue;
                }
            }
            out += in[i];
        }
        return out;
    }

    // Notas del release (Markdown de GitHub) con el estilo del launcher:
    // títulos en naranja, viñetas con su sangría y párrafos.
    void drawNotes(const std::string& markdown)
    {
        struct Block {
            int kind = 0;   // 0 párrafo, 1 título, 2 subtítulo, 3 viñeta
            int indent = 0; // nivel de viñeta
            std::string text;
        };
        std::vector<Block> blocks;
        size_t pos = 0;
        while (pos <= markdown.size()) {
            size_t nl = markdown.find('\n', pos);
            if (nl == std::string::npos) nl = markdown.size();
            std::string line = markdown.substr(pos, nl - pos);
            pos = nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t lead = line.find_first_not_of(' ');
            if (lead == std::string::npos) { // línea en blanco: corta el bloque
                blocks.push_back(Block());
                continue;
            }
            const std::string body = line.substr(lead);
            if (body.rfind("## ", 0) == 0) { blocks.push_back({ 2, 0, body.substr(3) }); continue; }
            if (body.rfind("# ", 0) == 0) { blocks.push_back({ 1, 0, body.substr(2) }); continue; }
            if (body.rfind("- ", 0) == 0 || body.rfind("* ", 0) == 0) {
                blocks.push_back({ 3, int(lead / 2), body.substr(2) });
                continue;
            }
            // Continuación de la viñeta o el párrafo anterior.
            if (!blocks.empty() && (blocks.back().kind == 0 || blocks.back().kind == 3) && !blocks.back().text.empty()) {
                blocks.back().text += " " + body;
            } else {
                blocks.push_back({ 0, 0, body });
            }
        }

        const ImU32 text = col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f));
        for (const Block& block : blocks) {
            if (block.text.empty()) continue;
            const std::string shown = plainInline(block.text);
            switch (block.kind) {
            case 1:
            case 2:
                ImGui::Dummy(ImVec2(0, block.kind == 1 ? 4.0f : 10.0f));
                ImGui::PushFont(block.kind == 1 ? fonts.title : fonts.body);
                ImGui::PushStyleColor(ImGuiCol_Text, col(kOrange));
                ImGui::TextWrapped("%s", shown.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                break;
            case 3: {
                const float indent = 8.0f + 22.0f * block.indent;
                ImGui::Indent(indent);
                ImGui::PushStyleColor(ImGuiCol_Text, text);
                ImGui::Bullet();
                ImGui::TextWrapped("%s", shown.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(indent);
                break;
            }
            default:
                ImGui::Dummy(ImVec2(0, 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, text);
                ImGui::TextWrapped("%s", shown.c_str());
                ImGui::PopStyleColor();
                break;
            }
        }
    }

    // Panel "What's new in X": encima de todo, con el estilo de los modales.
    void drawWhatsNew(ImVec2 size)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("##whatsnew", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(2, 12, 4, 170));
        // Tan alto como sus notas (hasta casi toda la ventana).
        const float chrome = 80.0f + 50.0f + 44.0f;
        const ImVec2 panel(std::min(size.x - 80.0f, 860.0f),
                           fitDialogHeight(whatsNewNotesH, chrome, size.y - 110.0f, std::min(size.y - 110.0f, 320.0f)));
        const ImVec2 a((size.x - panel.x) * 0.5f, (size.y - panel.y) * 0.5f);
        const ImVec2 b(a.x + panel.x, a.y + panel.y);
        dl->AddRectFilled(ImVec2(a.x + 8, a.y + 12), ImVec2(b.x + 8, b.y + 12), IM_COL32(0, 0, 0, 110), 22.0f);
        dl->AddRectFilled(a, b, IM_COL32(10, 40, 15, 250), 22.0f);
        dl->AddRect(a, b, col(kOrange), 22.0f, 0, 3.0f);
        dl->AddRectFilled(ImVec2(a.x + 24, a.y + 8), ImVec2(b.x - 24, a.y + 22), IM_COL32(255, 255, 255, 22), 8.0f);

        const std::string heading = "What's new in " + whatsNewVersion;
        dl->AddText(fonts.title, 32.0f, ImVec2(a.x + 34.0f, a.y + 28.0f), col(kOrange), heading.c_str());

        // Notas con scroll (rueda o arrastrando la barra).
        const float buttonsH = 50.0f;
        ImGui::SetCursorScreenPos(ImVec2(a.x + 34.0f, a.y + 80.0f));
        ImGui::BeginChild("##notes", ImVec2(panel.x - 68.0f, panel.y - 80.0f - buttonsH - 44.0f), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushTextWrapPos(0.0f);
        drawNotes(whatsNewNotes);
        ImGui::PopTextWrapPos();
        wheelScroll(notesScroll);
        whatsNewNotesH = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
        ImGui::EndChild();

        const std::string updateLabel = "Update to " + whatsNewVersion;
        // Los dos botones de la fila, del mismo tamaño estándar.
        const ButtonSize rowSize = sizeFor(updateLabel, buttonsH, role::kAction);
        const float updateW = buttons::buttonWidth(rowSize), closeW = updateW, y = b.y - buttonsH - 26.0f;
        ImGui::SetCursorScreenPos(ImVec2(b.x - 34.0f - updateW, y));
        if (glassButton(updateLabel.c_str(), rowSize, buttonsH, true)) {
            whatsNewOpen = false;
            choose(HubAction::Update, HubGame::Pikmin1);
        }
        ImGui::SetCursorScreenPos(ImVec2(b.x - 34.0f - updateW - 14.0f - closeW, y));
        if (glassButton("Close", rowSize, buttonsH) || ImGui::IsKeyPressed(ImGuiKey_Escape) || gamepadBackPressed()) {
            whatsNewOpen = false;
        }
        ImGui::End();
    }

#if PIKMIN_LAUNCHER_NETPLAY
    // A connection code, read-only, wrapped, with a copy button under it.
    void coopCode(const char* id, const std::string& code, float width)
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(4, 22, 8, 255));
        std::vector<char> shown(code.begin(), code.end());
        shown.push_back('\0');
        ImGui::InputTextMultiline(id, shown.data(), shown.size(), ImVec2(width, 110.0f), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        if (glassButton("Copy code", role::kAction, 44.0f)) {
            SDL_SetClipboardText(code.c_str());
            notify("Code copied. Send it to the other player.");
        }
    }

    // Host Co-op / Join Co-op: the same overlay style as "What's new".
    void drawCoop(ImVec2 size)
    {
        CoopFlow& flow = coopFlow();
        flow.poll();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("##coop", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(2, 12, 4, 170));
        const ImVec2 panel(std::min(size.x - 80.0f, 820.0f), std::min(size.y - 80.0f, 560.0f));
        const ImVec2 a((size.x - panel.x) * 0.5f, (size.y - panel.y) * 0.5f);
        const ImVec2 b(a.x + panel.x, a.y + panel.y);
        dl->AddRectFilled(ImVec2(a.x + 8, a.y + 12), ImVec2(b.x + 8, b.y + 12), IM_COL32(0, 0, 0, 110), 22.0f);
        dl->AddRectFilled(a, b, IM_COL32(10, 40, 15, 250), 22.0f);
        dl->AddRect(a, b, col(kOrange), 22.0f, 0, 3.0f);
        const bool host = flow.role() == CoopFlow::Role::Host;
        dl->AddText(fonts.title, 32.0f, ImVec2(a.x + 34.0f, a.y + 28.0f), col(kOrange),
                    flow.vs() ? (host ? "Host VS" : "Join VS") : (host ? "Host Co-op" : "Join Co-op"));

        const float innerW = panel.x - 68.0f;
        ImGui::SetCursorScreenPos(ImVec2(a.x + 34.0f, a.y + 80.0f));
        ImGui::BeginChild("##coopbody", ImVec2(innerW, panel.y - 80.0f - 90.0f), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)));
        ImGui::TextWrapped("%s", flow.status().c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6.0f));
        const CoopFlow::Stage stage = flow.stage();
        const bool takesPaste = (!host && stage == CoopFlow::Stage::PasteOffer)
                             || (host && stage == CoopFlow::Stage::ShowOffer);
        if (host && (stage == CoopFlow::Stage::ShowOffer || stage == CoopFlow::Stage::Connecting)) {
            ImGui::TextUnformatted("Your offer code:");
            coopCode("##offer", flow.offer(), innerW);
            ImGui::Dummy(ImVec2(0, 6.0f));
        }
        if (!host && stage == CoopFlow::Stage::Connecting) {
            ImGui::TextUnformatted("Your answer code:");
            coopCode("##answer", flow.answer(), innerW);
        }
        if (takesPaste) {
            ImGui::TextUnformatted(host ? "The other player's answer code:" : "The host's offer code:");
            ImGui::InputTextMultiline("##paste", coopInput.data(), coopInput.size(), ImVec2(innerW, 90.0f));
            if (glassButton("Paste", role::kAction, 44.0f)) {
                if (char* clip = SDL_GetClipboardText()) {
                    std::snprintf(coopInput.data(), coopInput.size(), "%s", clip);
                    SDL_free(clip);
                }
            }
            ImGui::SameLine();
            if (glassButton(host ? "Connect" : "Join", role::kAction, 44.0f, true)) {
                std::string err;
                const bool ok = host ? flow.submitAnswer(coopInput.data(), err) : flow.submitOffer(coopInput.data(), err);
                coopError = ok ? std::string() : err;
            }
            if (!coopError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 130, 110, 255));
                ImGui::TextWrapped("%s", coopError.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::Dummy(ImVec2(0, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col(ImVec4(0.80f, 0.86f, 0.78f, 1.0f)));
        ImGui::TextWrapped("Both players need the same game version. If the games cannot reach each other, allow "
                           "the game through the firewall when Windows asks, or see NETPLAY.md for TURN and "
                           "Tailscale.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        wheelScroll(coopScroll);
        ImGui::EndChild();

        const float buttonsH = 50.0f, closeW = buttons::buttonWidth(role::kSmallAction), y = b.y - buttonsH - 26.0f;
        ImGui::SetCursorScreenPos(ImVec2(b.x - 34.0f - closeW, y));
        if (glassButton("Close", role::kSmallAction, buttonsH) || ImGui::IsKeyPressed(ImGuiKey_Escape) || gamepadBackPressed()) {
            // The game keeps running; closing only hides the panel.
            coopOpen = false;
        }
        ImGui::End();
    }
#endif

    // El pie va en la ventana del hub (debajo de modales y de "What's new"),
    // en su franja reservada; si la ruta no cabe se recorta por delante.
    void drawFooter(ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const std::string where = !state.dataFolder.empty()
            ? "Data folder: " + fitTail(fonts.caption, 16.0f, state.dataFolder,
                                        std::max(40.0f, size.x - 44.0f
                                            - fonts.caption->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, "Data folder: ").x))
            : state.games[0].directory.empty()
            ? std::string("Not installed yet")
            : "Installed in: " + fitTail(fonts.caption, 16.0f, state.games[0].directory,
                                         std::max(40.0f, size.x - 44.0f
                                             - fonts.caption->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, "Installed in: ").x));
        // Placa semitransparente para que el texto se lea sobre la hierba.
        const ImVec2 extent = fonts.caption->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, where.c_str());
        dl->AddRectFilled(ImVec2(12, size.y - kFooterTop), ImVec2(32 + extent.x, size.y - 10), IM_COL32(6, 26, 10, 200), 14.0f);
        dl->AddText(fonts.caption, 16.0f, ImVec2(22, size.y - kFooterTop + 5.0f), col(kOrange), where.c_str());
    }

    // Avisos breves, por encima de todo.
    void drawToast(ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (toastTime > 0.0f) {
            const float alpha = std::min(1.0f, toastTime);
            const float padding = std::min(20.0f, size.x * 0.04f);
            const float width = std::max(1.0f, size.x - padding * 2.0f - 24.0f);
            const auto measure = [&](const std::string& text) {
                return fonts.body->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, text.c_str()).x;
            };
            const std::string shown = fitToast(toast, width, measure);
            const ImVec2 t = fonts.body->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, shown.c_str());
            const ImVec2 a(std::max(4.0f, (size.x - t.x) * 0.5f - padding), size.y - 58.0f - t.y - 18.0f);
            const ImVec2 b(std::min(size.x - 4.0f, a.x + t.x + padding * 2.0f), a.y + t.y + 18.0f);
            dl->AddRectFilled(a, b, col(ImVec4(0.04f, 0.16f, 0.06f, 0.95f), alpha), 16.0f);
            dl->AddRect(a, b, col(kOrange, alpha), 16.0f, 0, 2.0f);
            dl->PushClipRect(a, b, true);
            dl->AddText(fonts.body, 19.0f, ImVec2(a.x + padding, a.y + 9.0f), col(kOrange, alpha), shown.c_str());
            dl->PopClipRect();
            if (ImGui::IsMouseHoveringRect(a, b)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(std::min(600.0f, size.x - 32.0f));
                ImGui::TextUnformatted(toast.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }

    // `interactive` = false mientras hay un modal encima: se pinta igual, pero
    // no responde al ratón.
    void frame(ImVec2 size, float t, float dt, bool interactive = true)
    {
#if PIKMIN_LAUNCHER_NETPLAY
        // Keep polling after the panel is closed: that is when the game still
        // has the controller, and when nav should come back.
        if (coop) coop->poll();
#endif
        collectCovers();
        drawBackground(ImGui::GetBackgroundDrawList(), size, dt);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##hub", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        ImGui::BeginDisabled(!interactive || whatsNewOpen || coopOpen);

        // En ventanas bajas el logo encoge también con la altura.
        const float logoH = hubLogoHeight(size.x, size.y, logo.aspect());
        const float logoW = logoH * logo.aspect();
        const ImVec2 logoAt((size.x - logoW) * 0.5f, kLogoTop);
        ImGui::GetWindowDrawList()->AddImage(logo.imgui(), logoAt, ImVec2(logoAt.x + logoW, logoAt.y + logoH));

        // El launcher del paquete (fuera de una instalación) solo instala o
        // actualiza: sin pestañas. Ajustes y mover la instalación son del
        // launcher que queda dentro de la carpeta instalada.
        const float tabsY = logoAt.y + logoH + 10.0f;
        float contentTop = tabsY;
        if (state.installerOnly) {
            tab = Tab::Games;
        } else {
            drawTabs(tabsY, size.x);
            contentTop += kTabsH;
        }

        switch (tab) {
        case Tab::Games: drawGames(contentTop, size, dt, t); break;
        case Tab::Settings:
            drawSettings(contentTop, size, dt);
            break;
        case Tab::Install:
            drawMove(contentTop, size);
            break;
        }

        drawReleaseNotice(size, t);
        drawVersionLabel(size, logoAt, logoW, logoH);
        if (!state.dataFolder.empty()) {
            // Abajo a la derecha, a la altura del rótulo de la carpeta.
            const char* label = "Change folder";
            const float h     = 40.0f;
            const float w     = buttons::fitWidth(label, h, buttons::kLabelSize, measureLabel());
            ImGui::SetCursorScreenPos(ImVec2(size.x - 18.0f - w, size.y - 10.0f - h));
            if (glassButton(label, ImVec2(w, h))) choose(HubAction::DataFolder, HubGame::Pikmin1);
        }
        ImGui::EndDisabled();
        // Fuera de las páginas con scroll: el cursor vuelve a la ventana del hub.
        drawFooter(size);
        ImGui::End();
        toastTime = std::max(0.0f, toastTime - dt);
        drawToast(size);
        if (whatsNewOpen && interactive) drawWhatsNew(size);
#if PIKMIN_LAUNCHER_NETPLAY
        if (coopOpen && interactive) drawCoop(size);
#endif
    }

    // Logo con resolución para la densidad de la pantalla: a 1x se reduce
    // en la CPU (sin mipmaps), en HiDPI se queda entero para no verse borroso.
    void loadLogo(float density)
    {
        freeTexture(logo);
        logo = loadImageTexture(kLogoOpenNectar, kLogoOpenNectarSize, logoTextureMaxHeight(density));
    }

    void release()
    {
        freeDewdrop();
        for (Texture& cover : covers) freeTexture(cover);
        freeTexture(logo);
        freeTexture(versionTex);
        slideshow.release();
        art.release();
    }
};

// Android: la interfaz se diseña para ~1180x780; en el móvil se dibuja a esa
// escala virtual multiplicada por gUiScale (fuentes rasterizadas a la
// densidad real para que el texto salga nítido).
float gUiScale = 1.0f;
// Escritorio: densidad a la que están rasterizadas las fuentes (escala de la
// interfaz por la de la pantalla), para que el texto ampliado salga nítido.
float gFontDensity = 1.0f;

void loadFonts(Fonts& fonts)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
#ifdef __ANDROID__
    cfg.RasterizerDensity = gUiScale;
#else
    cfg.RasterizerDensity = gFontDensity;
#endif
    cfg.FontDataOwnedByAtlas = false; // los datos viven en el ejecutable
    cfg.OversampleH = 3;
    auto* data = const_cast<unsigned char*>(kFontFredoka);
    fonts.body = io.Fonts->AddFontFromMemoryTTF(data, int(kFontFredokaSize), 21.0f, &cfg);
    fonts.title = io.Fonts->AddFontFromMemoryTTF(data, int(kFontFredokaSize), 30.0f, &cfg);
    fonts.caption = io.Fonts->AddFontFromMemoryTTF(data, int(kFontFredokaSize), 16.0f, &cfg);
    io.FontDefault = fonts.body;
}



} // namespace

// ── Ventana principal + modales del instalador ─────────────────────────────
struct HubWindow::Impl {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    HubState state;
    HubResult result;
    std::unique_ptr<Hub> hub;
    Uint64 start = 0, last = 0;
    bool quitRequested = false;
    bool gameController = false; // SDL_INIT_GAMECONTROLLER succeeded
    bool startAtHome = false;  // la próxima elección de rutas empieza en la pantalla principal
    bool playRequested = false;
    HubGame installGame = HubGame::Pikmin1; // juego que instalan los modales de rutas y progreso
    std::function<HubResult()> homeLoop;   // ver setHomeLoop

    enum class Modal { None, Paths, Progress, Message };
    Modal modal = Modal::None;
    // Rutas
    std::string rom, installDirectory;
    std::string pathsLegacyOverride;
    int pathsAction = 0; // 1 examinar imagen, 2 examinar carpeta, 3 instalar, 4 cancelar
    // Progreso
    std::uint32_t percent = 0;
    std::string phase, currentFile;
    // Mensaje
    std::string messageTitle, messageText, primaryLabel, secondaryLabel, cancelLabel;
    bool messageIsError = false;
    int answer = -1; // 0 principal, 1 secundario
    // Non-empty while waitForDialog() has a native dialog open: names it for
    // the "Waiting for the ..." veil, and the window takes no input.
    std::string dialogWaiting;

    ~Impl()
    {
        // A picker the hub started may still be open on its worker; close it
        // so it does not stay on screen after the launcher window is gone.
        if (hub && hub->filePickOpen()) platform::cancelDialogs();
        if (!window) return;
        if (hub) hub->release();
        hub.reset();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        // The game process is started after this window is destroyed. Close the
        // controller here so that process can open it (exec keeps leftover
        // grabs; on Windows the launcher waits in this process).
        if (gameController) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    // Panel centrado del modal, con velo sobre la pantalla principal.
    // Devuelve la esquina superior izquierda del área de contenido.
    ImVec2 beginModal(ImVec2 size, ImVec2 panel)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowFocus();
        ImGui::Begin("##modal", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(2, 12, 4, 170));
        const ImVec2 a((size.x - panel.x) * 0.5f, (size.y - panel.y) * 0.5f);
        const ImVec2 b(a.x + panel.x, a.y + panel.y);
        dl->AddRectFilled(ImVec2(a.x + 8, a.y + 12), ImVec2(b.x + 8, b.y + 12), IM_COL32(0, 0, 0, 110), 22.0f);
        dl->AddRectFilledMultiColor(a, b, IM_COL32(14, 52, 20, 250), IM_COL32(14, 52, 20, 250), IM_COL32(8, 32, 12, 250),
                                    IM_COL32(8, 32, 12, 250));
        // Esquinas redondeadas: el degradado es cuadrado, así que se tapa el
        // borde con un marco del color del velo y luego el borde naranja.
        dl->AddRect(ImVec2(a.x - 6, a.y - 6), ImVec2(b.x + 6, b.y + 6), IM_COL32(2, 12, 4, 255), 26.0f, 0, 12.0f);
        dl->AddRect(a, b, col(kOrange), 22.0f, 0, 3.0f);
        // Brillo de cristal arriba, como las burbujas del HUD.
        dl->AddRectFilled(ImVec2(a.x + 24, a.y + 8), ImVec2(b.x - 24, a.y + 22), IM_COL32(255, 255, 255, 22), 8.0f);
        return ImVec2(a.x + 34.0f, a.y + 28.0f);
    }

    void endModal() { ImGui::End(); }

    void title(ImVec2 at, const char* text)
    {
        ImGui::GetWindowDrawList()->AddText(hub->fonts.title, 32.0f, at, col(kOrange), text);
    }

    // Campo de ruta de solo lectura con su botón Browse.
    bool pathField(ImVec2 at, float width, const char* label, const std::string& value, const char* empty, const char* id)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(hub->fonts.body, 21.0f, at, col(kOrange), label);
        const float y = at.y + 30.0f, h = 44.0f, buttonW = buttons::buttonWidth(role::kSmallAction);
        const ImVec2 a(at.x, y), b(at.x + width - buttonW - 12.0f, y + h);
        dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 235), 14.0f);
        dl->AddRect(a, b, col(kOrangeDeep, 0.8f), 14.0f, 0, 2.0f);
        const bool has = !value.empty();
        const std::string shown = has ? fitTail(hub->fonts.body, 19.0f, value, b.x - a.x - 28.0f) : empty;
        dl->AddText(hub->fonts.body, 19.0f, ImVec2(a.x + 14.0f, a.y + 11.0f), has ? col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)) : col(kOrange, 0.5f),
                    shown.c_str());
        ImGui::SetCursorScreenPos(ImVec2(b.x + 12.0f, y));
        ImGui::PushID(id);
        const bool clicked = hub->glassButton("Browse", role::kSmallAction, h);
        ImGui::PopID();
        return clicked;
    }

    void drawPaths(ImVec2 size)
    {
        const float panelW = std::min(size.x - 80.0f, 700.0f);
        const float width = panelW - 68.0f;
        const float noteSize = 16.0f;
        const float noteLineStep = noteSize + 4.0f;
        const TextWidthMeasure measure = [&](const std::string& text) {
            return hub->fonts.caption->CalcTextSizeA(noteSize, FLT_MAX, 0.0f, text.c_str()).x;
        };
        std::vector<std::string> noteLines;
        if (!installDirectory.empty()) {
            const fs::path legacy = pathsLegacyOverride.empty()
                ? pc_card_legacy_save_migration_source(fs::u8path(installDirectory))
                : fs::u8path(pathsLegacyOverride);
            if (!legacy.empty()) noteLines = layoutInstallSaveMigrationNote(legacy, width, 2, measure);
        }
        const float noteBlock = noteLines.empty() ? 0.0f : 8.0f + float(noteLines.size()) * noteLineStep;
        const ImVec2 panel(panelW, 430.0f + noteBlock);
        const ImVec2 at = beginModal(size, panel);
        const bool pikmin2 = installGame == HubGame::Pikmin2;
        title(at, pikmin2 ? "Install Pikmin 2" : "Install Pikmin");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(hub->fonts.caption, 16.0f, ImVec2(at.x, at.y + 42.0f), col(kOrange, 0.75f),
                    pikmin2 ? "Use your own disc: Pikmin 2 USA or Europe. ISO or GCM; RVZ, WIA and GCZ need Dolphin."
                            : "Use your own disc: Pikmin USA (Rev 1) or Europe. ISO or GCM; RVZ, WIA and GCZ need Dolphin.");
        if (pathField(ImVec2(at.x, at.y + 80.0f), width, "Disc image", rom, "No disc image chosen", "rom")) pathsAction = 1;
        if (pathField(ImVec2(at.x, at.y + 170.0f), width, "Install to", installDirectory, "No folder chosen", "dir")) pathsAction = 2;
        if (!noteLines.empty()) {
            const ImU32 noteColour = col(ImVec4(1.0f, 0.45f, 0.30f, 1.0f));
            float y = at.y + 262.0f;
            for (const std::string& line : noteLines) {
                dl->AddText(hub->fonts.caption, noteSize, ImVec2(at.x, y), noteColour, line.c_str());
                y += noteLineStep;
            }
        }
        const float buttonsY = at.y + panel.y - 110.0f;
        const float actionW = buttons::buttonWidth(role::kAction);
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - 2.0f * actionW - 14.0f, buttonsY));
        if (hub->glassButton("Cancel", role::kAction, 50.0f) || gamepadBackPressed()) pathsAction = 4;
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - actionW, buttonsY));
        if (hub->glassButton("Install", role::kAction, 50.0f, true, rom.empty() || installDirectory.empty())) pathsAction = 3;
        endModal();
    }

    void drawProgress(ImVec2 size, float t)
    {
        const ImVec2 panel(std::min(size.x - 80.0f, 700.0f), 260.0f);
        const ImVec2 at = beginModal(size, panel);
        const float width = panel.x - 68.0f;
        title(at, installGame == HubGame::Pikmin2 ? "Installing Pikmin 2" : "Installing Pikmin");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool indeterminate = percent > 100;
        const std::string status = indeterminate ? phase + "..." : phase + "  " + std::to_string(percent) + "%";
        dl->AddText(hub->fonts.body, 21.0f, ImVec2(at.x, at.y + 54.0f), col(kOrange), status.c_str());

        // Barra: verde hoja sobre fondo oscuro, borde naranja y un brillo que
        // la recorre (o que va y viene si la fase no tiene porcentaje).
        const ImVec2 a(at.x, at.y + 92.0f), b(at.x + width, at.y + 126.0f);
        const float r = (b.y - a.y) * 0.5f;
        dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 235), r);
        if (indeterminate) {
            const float span = width * 0.28f;
            const float x = a.x + (width - span) * (0.5f + 0.5f * std::sin(t * 2.2f));
            dl->AddRectFilled(ImVec2(x, a.y + 3), ImVec2(x + span, b.y - 3), col(kLeafBright), r - 3.0f);
        } else {
            const float fill = std::max(b.y - a.y, width * float(percent) / 100.0f);
            dl->AddRectFilledMultiColor(ImVec2(a.x + 3, a.y + 3), ImVec2(a.x + fill - 3, b.y - 3), col(kLeafBright), col(kLeaf),
                                        col(kLeafDark), col(kLeafDark));
            const float shine = a.x + std::fmod(t * 180.0f, fill + 60.0f) - 30.0f;
            dl->PushClipRect(ImVec2(a.x + 3, a.y + 3), ImVec2(a.x + fill - 3, b.y - 3), true);
            dl->AddRectFilled(ImVec2(shine, a.y), ImVec2(shine + 30.0f, b.y), IM_COL32(255, 255, 255, 50));
            dl->PopClipRect();
        }
        dl->AddRect(a, b, col(kOrange), r, 0, 2.5f);
        dl->AddRectFilled(ImVec2(a.x + r, a.y + 5), ImVec2(b.x - r, a.y + 11), IM_COL32(255, 255, 255, 30), 3.0f);

        if (!currentFile.empty()) {
            const std::string file = fitTail(hub->fonts.caption, 16.0f, currentFile, width);
            dl->AddText(hub->fonts.caption, 16.0f, ImVec2(at.x, at.y + 142.0f), col(kOrange, 0.7f), file.c_str());
        }
        dl->AddText(hub->fonts.caption, 16.0f, ImVec2(at.x, at.y + 182.0f), col(kOrange, 0.55f),
                    "This takes a few minutes. Keep the window open.");
        endModal();
    }

    void drawMessage(ImVec2 size)
    {
        const float width = std::min(size.x - 80.0f, 640.0f) - 68.0f;
        const float textH = hub->fonts.body->CalcTextSizeA(20.0f, FLT_MAX, width, messageText.c_str()).y;
        // Todos los botones del diálogo con el mismo tamaño estándar: el
        // menor donde cabe el texto más largo. Si no caben en una fila, en dos
        // (o más).
        ButtonSize rowSize = hub->sizeFor(primaryLabel, 50.0f, role::kAction);
        for (const std::string* other : { &secondaryLabel, &cancelLabel })
            if (!other->empty()) rowSize = std::max(rowSize, hub->sizeFor(*other, 50.0f, role::kAction));
        const float rowW = buttons::buttonWidth(rowSize);
        std::vector<float> widths;
        std::vector<int> answers;
        if (!secondaryLabel.empty()) { widths.push_back(rowW); answers.push_back(1); }
        widths.push_back(rowW);
        answers.push_back(0);
        const float cancelW = cancelLabel.empty() ? 0.0f : rowW;
        int rows = 1;
        const std::vector<DialogButton> placed = fitDialogButtons(widths, cancelW, width, rows);
        const float rowStep = 50.0f + 12.0f;
        const ImVec2 panel(width + 68.0f, 170.0f + textH + float(rows - 1) * rowStep);
        const ImVec2 at = beginModal(size, panel);
        title(at, messageTitle.c_str());
        ImGui::GetWindowDrawList()->AddText(hub->fonts.body, 20.0f, ImVec2(at.x, at.y + 52.0f),
                                            messageIsError ? col(ImVec4(1.0f, 0.78f, 0.62f, 1.0f)) : col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)),
                                            messageText.c_str(), nullptr, width);
        const float buttonsY = at.y + panel.y - 96.0f - float(rows - 1) * rowStep;
        for (std::size_t k = 0; k < answers.size(); ++k) {
            const DialogButton& b = placed[k];
            ImGui::SetCursorScreenPos(ImVec2(at.x + b.x, buttonsY + float(b.row) * rowStep));
            const std::string& label = answers[k] == 0 ? primaryLabel : secondaryLabel;
            if (hub->glassButton(label.c_str(), ImVec2(b.w, 50.0f), answers[k] == 0)) answer = answers[k];
        }
        if (!cancelLabel.empty()) {
            // A la izquierda (o debajo), lejos de las acciones: cerrar sin elegir ninguna.
            const DialogButton& b = placed.back();
            ImGui::SetCursorScreenPos(ImVec2(at.x + b.x, buttonsY + float(b.row) * rowStep));
            if (hub->glassButton(cancelLabel.c_str(), ImVec2(b.w, 50.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                answer = 2;
        }
        // B dismisses. It does not press Play, Download, or the other actions.
        if (answer < 0 && gamepadBackPressed()) {
            const int back = launcherGamepadDialogBack(!cancelLabel.empty(), secondaryLabel);
            if (back >= 0) answer = back;
        }
        endModal();
    }

#ifndef __ANDROID__
    // Escritorio: la escala sigue al tamaño de la ventana. Si la densidad de
    // las fuentes cambia, se rehace el atlas (antes del fotograma).
    void updateDesktopScale()
    {
        int w = 0, h = 0, fbW = 0;
        SDL_GetWindowSize(window, &w, &h);
        SDL_GL_GetDrawableSize(window, &fbW, nullptr);
        if (w <= 0 || h <= 0) return;
        gUiScale = desktopUiScale(float(w), float(h));
        const float density = fontDensityFor(gUiScale, float(fbW) / float(w));
        if (density != gFontDensity) {
            gFontDensity = density;
            ImGui::GetIO().Fonts->Clear();
            loadFonts(hub->fonts);
            // El logo, a la misma densidad que el texto (la versión va a su lado).
            hub->loadLogo(density);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
        }
    }

    // Las posiciones del ratón llegan en puntos de la ventana; la interfaz
    // va en puntos virtuales (ventana / gUiScale). Se escalan en la cola de
    // ImGui una sola vez cada una (los eventos pueden esperar varios fotogramas).
    void scaleQueuedMouse()
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImU32 newest = lastScaledEvent;
        for (ImGuiInputEvent& e : g.InputEventsQueue) {
            if (e.EventId <= lastScaledEvent) continue;
            newest = std::max(newest, e.EventId);
            if (e.Type != ImGuiInputEventType_MousePos || gUiScale == 1.0f) continue;
            if (e.MousePos.PosX > -FLT_MAX && e.MousePos.PosY > -FLT_MAX) {
                e.MousePos.PosX /= gUiScale;
                e.MousePos.PosY /= gUiScale;
            }
        }
        lastScaledEvent = newest;
    }
    ImU32 lastScaledEvent = 0;
#endif

    // Un fotograma: eventos, pantalla principal y el modal que toque.
    void frame()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#ifdef __ANDROID__
            if (gUiScale != 1.0f) {
                // Coordenadas de la pantalla real → las de la interfaz virtual.
                const float inv = 1.0f / gUiScale;
                if (event.type == SDL_MOUSEMOTION) {
                    event.motion.x = int(event.motion.x * inv);
                    event.motion.y = int(event.motion.y * inv);
                    event.motion.xrel = int(event.motion.xrel * inv);
                    event.motion.yrel = int(event.motion.yrel * inv);
                } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                    event.button.x = int(event.button.x * inv);
                    event.button.y = int(event.button.y * inv);
                }
            }
#endif
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) quitRequested = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE
                && event.window.windowID == SDL_GetWindowID(window)) quitRequested = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            return;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float freq = float(SDL_GetPerformanceFrequency());
        const float dt = std::min(0.1f, float(now - last) / freq);
        const float t = float(now - start) / freq;
        last = now;

#ifndef __ANDROID__
        updateDesktopScale();
#endif
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
#ifndef __ANDROID__
        scaleQueuedMouse();
#endif
        launcherApplyGamepadNav();
        if (gUiScale != 1.0f) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplayFramebufferScale = ImVec2(io.DisplayFramebufferScale.x * gUiScale, io.DisplayFramebufferScale.y * gUiScale);
            io.DisplaySize = ImVec2(io.DisplaySize.x / gUiScale, io.DisplaySize.y / gUiScale);
        }
        ImGui::NewFrame();
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        // While a file picker is open the hub keeps animating but takes no input.
        const bool waiting = !dialogWaiting.empty();
        hub->frame(size, t, dt, modal == Modal::None && !hub->filePickOpen() && !waiting);
        // A native dialog from waitForDialog(): the modal stays visible but
        // cannot be clicked, so no second dialog opens from inside a frame.
        ImGui::BeginDisabled(waiting);
        switch (modal) {
        case Modal::None: break;
        case Modal::Paths: drawPaths(size); break;
        case Modal::Progress: drawProgress(size, t); break;
        case Modal::Message: drawMessage(size); break;
        }
        ImGui::EndDisabled();
        if (waiting) drawDialogVeil(size);
        ImGui::Render();

        int fbW = 0, fbH = 0;
        SDL_GL_GetDrawableSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.035f, 0.13f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        // Between frames: what follows a picked file may open a modal, which
        // calls frame() itself, so it must not run inside hub->frame().
        hub->finishFilePick();
        hub->runBetweenFrames();
    }

    void drawDialogVeil(ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(2, 12, 4, 150));
        const std::string text = "Waiting for the " + dialogWaiting + "...";
        const ImVec2 textSize = hub->fonts.body->CalcTextSizeA(22.0f, FLT_MAX, 0.0f, text.c_str());
        dl->AddText(hub->fonts.body, 22.0f, ImVec2((size.x - textSize.x) * 0.5f, size.y - textSize.y - 48.0f),
                    col(kOrange), text.c_str());
    }

    // Runs a blocking native dialog on a worker thread and keeps drawing the
    // window (dimmed, without input) until it returns. `what` names it for the
    // veil ("disc image picker"). If the window is closed meanwhile, the
    // dialog is closed too and this returns empty with quitRequested still
    // set. It never returns while the dialog runs, so `dialog` may capture the
    // caller's locals by reference. Only call it between frames.
    std::string waitForDialog(const std::function<std::string()>& dialog, const std::string& what)
    {
        if (!dialogWaiting.empty()) return {}; // never two at once
        std::atomic<bool> done { false };
        std::string result;
        dialogWaiting = what.empty() ? std::string("dialog") : what;
        std::thread worker([&] {
            result = dialog();
            done.store(true, std::memory_order_release);
        });
        bool cancelled = false;
        while (!done.load(std::memory_order_acquire)) {
            frame();
            if (quitRequested && !cancelled) {
                // POSIX: ends the dialog process, so `done` follows quickly.
                // Windows: the dialog belongs to this process; keep drawing
                // until the player closes it, as nothing else can.
                cancelled = true;
                platform::cancelDialogs();
            }
        }
        worker.join();
        dialogWaiting.clear();
        return quitRequested ? std::string() : result;
    }

    // Muestra un mensaje hasta que se pulsa un botón. -1 si se cierra la ventana.
    int message(const std::string& heading, const std::string& text, const std::string& primary,
                const std::string& secondary, bool isError, const std::string& cancel = std::string())
    {
        modal = Modal::Message;
        cancelLabel = cancel;
        messageTitle = heading;
        messageText = text;
        primaryLabel = primary;
        secondaryLabel = secondary;
        messageIsError = isError;
        answer = -1;
        while (answer < 0 && !quitRequested) frame();
        modal = Modal::None;
        cancelLabel.clear();
        return answer == 2 ? -1 : answer;
    }
};

HubWindow::HubWindow() : mImpl(new Impl) {}
HubWindow::~HubWindow() = default;

bool HubWindow::open(const HubState& state, std::string& error)
{
    Impl& m = *mImpl;
    m.state = state;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        error = SDL_GetError();
        return false;
    }
    // Controllers opened before or after launch. A failure here leaves the
    // mouse and keyboard launcher unchanged.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    m.gameController = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
    noteGamepadSubsystem(m.gameController);
#ifdef __ANDROID__
    // Android: OpenGL ES 3.0 a pantalla completa (la ventana es la actividad).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
#ifdef __APPLE__
    // macOS only offers a core profile at 3.2 or later. A 3.0 request without a
    // profile gets a legacy 2.1 context, which fails here.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __ANDROID__
    SDL_Window* window = SDL_CreateWindow("Open Nectar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
#else
    SDL_Window* window = SDL_CreateWindow("Open Nectar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 780,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
    if (!window) {
        error = SDL_GetError();
        if (m.gameController) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        m.gameController = false;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
#ifndef __ANDROID__
    // Por debajo de esto las páginas pasarían a tener scroll (ver los tests
    // de launcher_page_layout).
    SDL_SetWindowMinimumSize(window, 800, 600);
    pc_icon_apply(window);
#endif
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        error = SDL_GetError();
        SDL_DestroyWindow(window);
        if (m.gameController) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        m.gameController = false;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
#ifdef __ANDROID__
    {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        const float s = std::min(float(w) / 1180.0f, float(h) / 780.0f);
        gUiScale = std::max(1.0f, std::min(s, 4.0f));
    }
#endif
    m.window = window;
    m.context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // Keyboard navigation stays off, so arrows and shortcuts are unchanged.
    // D-pad and left stick move focus, A activates, B goes back.
    if (m.gameController) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL2_InitForOpenGL(window, context);
#ifdef __ANDROID__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#elif defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif
    applyTheme();

    m.hub = std::make_unique<Hub>(m.state, m.result);
    Hub& hub = *m.hub;
#ifndef __ANDROID__
    hub.askModal = [&m](const std::string& title, const std::string& text, const std::string& primary,
                        const std::string& secondary, bool isError, const std::string& cancel) {
        return m.message(title, text, primary, secondary, isError, cancel);
    };
#endif
    loadFonts(hub.fonts);
#ifdef __ANDROID__
    hub.loadLogo(gUiScale);
#else
    hub.loadLogo(gFontDensity);
#endif
    hub.art.load(m.state.gameDataDirectory);
    if (hub.art.worldMap.valid()) {
        // El paisaje del mapa del mundo entra en el pase de fondo.
        hub.slideshow.add(hub.art.worldMap);
        hub.art.worldMap = Texture();
    }
    // Las fotos del diario de Olimar también: ahora son fondos, no marcos.
    for (Texture& photo : hub.art.logs) hub.slideshow.add(photo);
    hub.art.logs.clear();
    hub.startCoverDownloads();
#ifdef __ANDROID__
    const bool checkReleases = m.state.games[0].installed || m.state.games[1].installed; // Android: sin pestañas, pero sí Update
#else
    const bool checkReleases = !m.state.installerOnly && m.state.games[0].installed;
#endif
    if (checkReleases) {
        hub.releaseCheck = std::make_shared<ReleaseCheck>();
        std::thread(ReleaseCheck::run, hub.releaseCheck).detach();
    }
    for (int g = 0; g < kHubGameCount && !m.state.installerOnly; ++g) {
        const GameInstall& game = m.state.games[g];
        if (!game.installed || game.executable.empty()) continue;
        // Se piden ya, en segundo plano, para que la pestaña esté lista al abrirla.
        auto client = std::make_shared<SettingsClient>();
        client->game = game.executable;
        client->directory = game.directory;
        hub.settingsClients[g] = client;
        SettingsClient::start(client, { "--settings-dump" });
    }
    m.start = m.last = SDL_GetPerformanceCounter();
    return true;
}

HubResult HubWindow::runHome()
{
    Impl& m = *mImpl;
    m.modal = Impl::Modal::None;
    m.hub->chosen = false;
    m.result = HubResult();
    while (!m.quitRequested && !m.hub->chosen) m.frame();
    if (m.quitRequested) m.result.action = HubAction::Quit;
    return m.result;
}

void HubWindow::setInstallGame(HubGame game, const std::string& suggestedDirectory)
{
    Impl& m = *mImpl;
    if (m.installGame != game) m.rom.clear();
    m.installGame = game;
    if (m.installDirectory.empty()) m.installDirectory = suggestedDirectory;
}

void HubWindow::setHomeLoop(std::function<HubResult()> loop) { mImpl->homeLoop = std::move(loop); }

void HubWindow::showNotice(const std::string& text)
{
    if (!mImpl->hub) return;
    mImpl->hub->notify(text);
    mImpl->hub->toastTime = 8.0f; // long enough to read before it fades
}

void HubWindow::markInstalled(HubGame game, const std::string& directory, const std::string& executable,
                              const std::string& discId)
{
    GameInstall& install = mImpl->state.games[int(game)];
    install.installed = true;
    install.directory = directory;
    install.executable = executable;
    install.discId = discId;
}

bool HubWindow::playRequested() const { return mImpl->playRequested; }

bool HubWindow::newerRelease(ReleaseInfo& release) const
{
    const auto& check = mImpl->hub ? mImpl->hub->releaseCheck : nullptr;
    if (!check || !check->done) return false;
    std::lock_guard<std::mutex> lock(check->mutex);
    if (!check->newer) return false;
    release = check->latest;
    return true;
}

int HubWindow::ask(const std::string& title, const std::string& text, const std::string& primary,
                   const std::string& secondary, bool isError, const std::string& cancel)
{
    Impl& m = *mImpl;
    return m.message(title, text, primary, secondary, isError, cancel);
}

bool HubWindow::choosePaths(const std::function<std::string()>& chooseRom,
                            const std::function<std::string(const std::string& currentInstallDirectory)>&
                                chooseInstallDirectory,
                            std::string& rom, std::string& installDirectory)
{
    Impl& m = *mImpl;
    for (;;) {
        if (m.startAtHome) {
            // Tras cancelar: de vuelta a la pantalla principal, y de ahí otra
            // vez al modal si pulsa Install.
            m.startAtHome = false;
            const HubResult choice = m.homeLoop ? m.homeLoop() : runHome();
            if (choice.action == HubAction::Quit) return false;
            if (choice.action == HubAction::Play) {
                m.playRequested = true;
                return false;
            }
        }
        m.modal = Impl::Modal::Paths;
        for (;;) {
            m.pathsAction = 0;
            m.frame();
            if (m.quitRequested) return false;
            if (m.pathsAction == 1) {
                const std::string selected = m.waitForDialog(chooseRom, "disc image picker");
                if (m.quitRequested) return false;
                if (!selected.empty()) m.rom = selected;
            } else if (m.pathsAction == 2) {
                const std::string selected = m.waitForDialog(
                    [&] {
                        if (chooseInstallDirectory) return chooseInstallDirectory(m.installDirectory);
                        return platform::askForInstallDirectory(
                                   "Choose the install folder",
                                   m.installDirectory.empty() ? fs::path {} : fs::u8path(m.installDirectory))
                            .u8string();
                    },
                    "folder picker");
                if (m.quitRequested) return false;
                if (!selected.empty()) m.installDirectory = selected;
            } else if (m.pathsAction == 3) {
                rom = m.rom;
                installDirectory = m.installDirectory;
                m.modal = Impl::Modal::Progress;
                m.percent = 101;
                m.phase = "Preparing";
                m.currentFile.clear();
                return true;
            } else if (m.pathsAction == 4) {
                // Pikmin 2 se instala desde el bucle de la pantalla principal:
                // al cancelar se vuelve allí.
                if (m.installGame == HubGame::Pikmin2) {
                    m.modal = Impl::Modal::None;
                    return false;
                }
                m.startAtHome = true;
                break;
            }
        }
    }
}

std::string HubWindow::runDialog(const std::function<std::string()>& dialog, const std::string& what)
{
    return mImpl->waitForDialog(dialog, what);
}

void HubWindow::updateProgress(std::uint32_t percent, const std::string& currentFile, const std::string& phase)
{
    Impl& m = *mImpl;
    m.modal = Impl::Modal::Progress;
    m.percent = percent;
    m.phase = phase;
    m.currentFile = currentFile;
    // Cerrar la ventana a mitad de instalación no se atiende: cortar la
    // extracción dejaría la carpeta a medias.
    m.frame();
    m.quitRequested = false;
}

void HubWindow::showError(const std::string& message)
{
    mImpl->message("Installation did not finish", message, "OK", "", true);
}

bool HubWindow::offerRetry(const std::string& message)
{
    Impl& m = *mImpl;
    const int answer = m.message("Installation did not finish", message, "Back to setup", "Close", true);
    if (answer < 0) return false;
    // Close vuelve a la pantalla principal en vez de cerrar el launcher.
    if (answer == 1) m.startAtHome = true;
    return true;
}

bool HubWindow::showComplete(const std::string& installDirectory, bool willLaunch)
{
    Impl& m = *mImpl;
    m.state.games[0].installed = true;
    m.state.games[0].directory = installDirectory;
    const std::string text = "Pikmin is installed in:\n" + installDirectory
                           + "\n\nIn game, F1 or backtick opens graphics, controls and gameplay settings."
                             "\nOpen nectar-launcher from that folder to play again.";
    const int answer = m.message("Installation complete", text, willLaunch ? "Play now" : "OK", willLaunch ? "Back" : "", false);
    m.quitRequested = false;
    if (answer != 1) return true;
    // Back (#172): a la pantalla principal, p. ej. para instalar Pikmin 2. Desde
    // ahí jugar lanza el juego por su cuenta; si vuelve, es que pidió salir.
    if (m.homeLoop) m.homeLoop();
    else if (runHome().action == HubAction::Play) return true;
    return false;
}

bool HubWindow::savePathsModalScreenshot(const std::string& path, int width, int height, const std::string& rom,
                                         const std::string& installDirectory, const std::string& legacySaveOverride)
{
    Impl& m = *mImpl;
    if (!m.window) return false;
    SDL_SetWindowSize(m.window, width, height);
    SDL_SetWindowPosition(m.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    m.rom                   = rom;
    m.installDirectory      = installDirectory;
    m.pathsLegacyOverride   = legacySaveOverride;
    m.modal                 = Impl::Modal::Paths;
    // Settle the resized window, then capture the back buffer before swapping.
    for (int frame = 0; frame < 3; ++frame) m.frame();
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glFinish();
    int fbW = 0, fbH = 0;
    SDL_GL_GetDrawableSize(m.window, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return false;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(fbW) * static_cast<std::size_t>(fbH) * 4);
    glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, fbW, fbH, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!surface) return false;
    const int pitch = surface->pitch;
    auto* dst       = static_cast<unsigned char*>(surface->pixels);
    for (int y = 0; y < fbH; ++y) {
        const int srcY = fbH - 1 - y;
        for (int x = 0; x < fbW; ++x) {
            const unsigned char* src = &rgba[(static_cast<std::size_t>(srcY) * static_cast<std::size_t>(fbW)
                                             + static_cast<std::size_t>(x))
                                            * 4];
            unsigned char* out       = dst + y * pitch + x * 4;
            out[0]                   = src[0];
            out[1]                   = src[1];
            out[2]                   = src[2];
            out[3]                   = 255;
        }
    }
    const bool ok = SDL_SaveBMP(surface, path.c_str()) == 0;
    SDL_FreeSurface(surface);
    m.pathsLegacyOverride.clear();
    return ok;
}

} // namespace launcher
} // namespace pikmin
