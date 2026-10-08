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
#include "launcher_platform.h"
#include "settings_model.h"
#include "release_update.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "font_fredoka.h"
#include "logo_open_nectar.h"

#include "../pc_icon.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

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
    Texture pill;              // w08_160: panel de cristal del HUD, para los botones
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
        pill = bti("w08_160");
        sparkle = bti("sun_64");
        loaded = worldMap.valid() || !logs.empty();
    }

    void release()
    {
        freeTexture(worldMap);
        for (Texture& t : logs) freeTexture(t);
        freeTexture(pill);
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
    s.FrameBorderSize = 2.0f;
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

    // Ejecuta el juego con esos argumentos y se queda con su respuesta.
    static void request(std::shared_ptr<SettingsClient> self, std::vector<std::string> arguments)
    {
        std::string output, failure;
        SettingsSnapshot parsed;
        const bool ok = platform::runAndCapture(self->game, arguments, self->directory, output, failure)
                     && parseSettingsDump(output, parsed, failure);
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

struct Hub {
    const HubState& state;
    HubResult& result;
    Fonts fonts;
    Texture logo;
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
    std::shared_ptr<SettingsClient> settingsClients[kHubGameCount];
    std::shared_ptr<SettingsClient> settings;
    int settingsGame = 0;
    int settingsGroup = 0;
    // Orden de las opciones tal como se vieron la primera vez: el juego lista
    // las cíclicas empezando por el valor actual, y el desplegable no debe
    // reordenarse cada vez que se cambia algo.
    std::map<std::pair<int, int>, std::vector<std::string>> optionOrder;
    float resetArmed = 0.0f; // segundos que queda activa la confirmación de Reset
    // Petición al juego apuntada mientras se dibuja con el candado de los
    // ajustes cogido; se lanza al soltarlo (start también lo coge).
    std::vector<std::string> pendingRequest;
    const char* pendingLabel = "Saving...";
    std::string moveTarget;  // carpeta nueva elegida
    std::string moveNote;    // aviso bajo el formulario
    std::shared_ptr<MoveJob> moveJob;
    std::string toast;
    float toastTime = 0.0f;
    bool chosen = false;

    Hub(const HubState& s, HubResult& r) : state(s), result(r) {}

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
        const std::string ids[2] = { state.games[0].discId.empty() ? "GPIE01" : state.games[0].discId,
                                     state.games[1].discId.empty() ? "GPVE01" : state.games[1].discId };
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

    // Botón de cristal como los del HUD del juego: la píldora w08_160 teñida
    // de verde, partida en tres para que las esquinas no se deformen. Sin la
    // textura, un degradado con brillo que imita el mismo cristal.
    bool glassButton(const char* label, ImVec2 size, bool selected = false, bool disabled = false)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(label, size);
        const bool hovered = ImGui::IsItemHovered() && !disabled;
        const bool held = ImGui::IsItemActive() && !disabled;
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(at.x + size.x, at.y + size.y);
        const ImVec4 tint = disabled ? ImVec4(0.35f, 0.42f, 0.36f, 1.0f)
                          : held      ? kLeafDark
                          : hovered   ? kLeafBright
                          : selected  ? kLeaf
                                      : ImVec4(0.16f, 0.45f, 0.20f, 1.0f);
        const float r = size.y * 0.5f;
        if (art.pill.valid()) {
            // La píldora es gris: el color lo pone el tinte. Tapas de 44 px de
            // 160 de ancho, escaladas a la altura del botón.
            dl->AddRectFilled(at, b, col(tint, 0.55f), r);
            const float cap = size.y * 44.0f / 88.0f;
            const float u = 44.0f / 160.0f;
            const ImU32 tintU32 = col(ImVec4(std::min(1.0f, tint.x * 1.6f), std::min(1.0f, tint.y * 1.6f),
                                             std::min(1.0f, tint.z * 1.6f), 1.0f));
            const ImTextureID id = art.pill.imgui();
            dl->AddImage(id, at, ImVec2(at.x + cap, b.y), ImVec2(0, 0), ImVec2(u, 1), tintU32);
            dl->AddImage(id, ImVec2(at.x + cap, at.y), ImVec2(b.x - cap, b.y), ImVec2(u, 0), ImVec2(1 - u, 1), tintU32);
            dl->AddImage(id, ImVec2(b.x - cap, at.y), b, ImVec2(1 - u, 0), ImVec2(1, 1), tintU32);
        } else {
            dl->AddRectFilled(at, b, col(tint, 0.9f), r);
            dl->AddRectFilled(ImVec2(at.x + r * 0.6f, at.y + 3.0f), ImVec2(b.x - r * 0.6f, at.y + size.y * 0.45f),
                              IM_COL32(255, 255, 255, disabled ? 18 : 48), r * 0.6f);
        }
        dl->AddRect(at, b, col(selected || hovered ? kOrange : kOrangeDeep, disabled ? 0.4f : 1.0f), r, 0, 2.0f);
        centredText(dl, fonts.body, 21.0f, ImVec2(at.x + size.x * 0.5f, at.y + size.y * 0.5f),
                    col(kOrange, disabled ? 0.45f : 1.0f), label);
        return clicked && !disabled;
    }

    void drawTabs(float y, float width)
    {
        const char* labels[] = { "Games", "Settings", "Move Install" };
        const float tabW = 150.0f, tabH = 42.0f, gap = 14.0f;
        const float total = 3.0f * tabW + 2.0f * gap;
        float x = (width - total) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (glassButton(labels[i], ImVec2(tabW, tabH), int(tab) == i)) tab = Tab(i);
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
        ImGui::PopID();
        if (hovered && available) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        CardState& card = cards[index];
        card.hover = approach(card.hover, hovered ? 1.0f : 0.0f, dt, 12.0f);
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
            dl->AddRectFilledMultiColor(a, b, col(kLeaf), col(kLeaf), col(kLeafDark), col(kLeafDark));
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
        dl->AddRect(a, b, col(hovered && available ? kOrange : kOrangeDeep), rounding, 0, 3.0f + card.hover);

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

    void drawGames(float top, ImVec2 size, float dt, float t)
    {
        const float cardW = std::clamp(size.x * 0.19f, 190.0f, 280.0f);
        const float cardH = cardW * 1.40f;
        const float gap = cardW * 0.42f;
        const float startX = (size.x - (2.0f * cardW + gap)) * 0.5f;
        const float cardY = top + 24.0f;
        const char* names[] = { "Pikmin", "Pikmin 2" };

        for (int i = 0; i < 2; ++i) {
            const float x = startX + float(i) * (cardW + gap);
            drawCard(i, ImVec2(x, cardY), ImVec2(cardW, cardH), dt, t);

            // Nombre del juego y los dos botones debajo de la portada.
            const float below = cardY + cardH + 22.0f;
            centredText(ImGui::GetWindowDrawList(), fonts.title, 28.0f, ImVec2(x + cardW * 0.5f, below + 14.0f),
                        col(kOrange), names[i]);
            const bool isPikmin2 = i == int(HubGame::Pikmin2);
            const bool comingSoon = false;
            // Ya instalado solo tiene sentido actualizar: un botón a lo ancho.
            const bool installed = state.games[i].installed;
            const float buttonW = installed ? cardW : (cardW - 12.0f) * 0.5f;
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ImVec2(x, below + 38.0f));
            if (!installed && glassButton("Install", ImVec2(buttonW, 44.0f), false, comingSoon)) choose(HubAction::Install, HubGame(i));
            ImGui::SetCursorScreenPos(ImVec2(installed ? x : x + buttonW + 12.0f, below + 38.0f));
            // Con versión nueva en GitHub el botón lo dice ("Update to 1.0").
            std::string updateLabel = isPikmin2 ? "Fusion build" : "Update";
            if (!isPikmin2 && installed && releaseCheck && releaseCheck->done) {
                std::lock_guard<std::mutex> lock(releaseCheck->mutex);
                if (releaseCheck->newer) updateLabel = "Update to " + releaseCheck->latest.version;
            }
            if (glassButton(updateLabel.c_str(), ImVec2(buttonW, 44.0f), updateLabel != "Update",
                            comingSoon || isPikmin2)) {
                choose(HubAction::Update, HubGame(i));
            }
            ImGui::PopID();
        }
    }

    void queue(std::vector<std::string> arguments, const char* label = "Saving...")
    {
        pendingRequest = std::move(arguments);
        pendingLabel = label;
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
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "TEXTURE PACKS");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 0.85f),
                           "Replacement textures for the whole game. Pick the one to use; it applies the next time the game starts.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::BeginDisabled(busy);
        const auto option = [&](const char* label, const std::string& name) {
            const bool active = snap.activePack == name;
            if (ImGui::RadioButton(label, active) && !active) {
                queue({ "--texpack-activate", name });
            }
        };
        option("None (original textures)", "");
        for (const std::string& pack : snap.texturePacks) option(pack.c_str(), pack);
        if (snap.texturePacks.empty()) ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "No packs installed yet.");
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 10));
        if (glassButton("Install a pack from a .zip", ImVec2(300.0f, 46.0f), false, busy)) {
            const fs::path zip = platform::askForFile("Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP");
            if (!zip.empty()) queue({ "--texpack-install", zip.string() }, "Installing the pack...");
        }
    }

    // Modelos HD: uno por fila, cada uno se instala desde su zip.
    void drawHdModels(const SettingsSnapshot& snap, bool busy)
    {
        const float buttonW = 190.0f;
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(kOrangeDeep, "HD MODELS");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 0.85f),
                           "Higher-detail characters, converted from the model zips. They apply the next time the game starts.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));

        for (const HdModel& model : snap.hdModels) {
            ImGui::PushID(model.row);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.80f, 1.0f), "%s", model.name.c_str());
            // Instalado: interruptor On/Off (apagar no borra nada, vuelve al
            // modelo original). Sin instalar: solo el aviso.
            const float toggleW = 120.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonW - toggleW - 12.0f);
            if (model.installed) {
                if (glassButton(model.enabled ? "On" : "Off", ImVec2(toggleW, 40.0f), model.enabled, busy)) {
                    queue({ "--hdmodel-enable", std::to_string(model.row), model.enabled ? "0" : "1" });
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "Not installed");
            }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonW);
            if (glassButton(model.installed ? "Replace..." : "Install...", ImVec2(buttonW, 40.0f), false, busy)) {
                const fs::path zip = platform::askForFile("Choose the zip for " + model.name, "Model zip", "*.zip *.ZIP");
                if (!zip.empty()) {
                    queue({ "--hdmodel-install", std::to_string(model.row), zip.string() },
                                          "Converting the model...");
                }
            }
            ImGui::PopID();
        }
    }

    void drawSettings(float top, ImVec2 size, float dt)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Un apartado por juego: cada uno con sus propias opciones.
        const char* gameNames[kHubGameCount] = { "Pikmin", "Pikmin 2" };
        const float pillW = 170.0f, pillGap = 12.0f;
        float pillX = (size.x - (float(kHubGameCount) * pillW + float(kHubGameCount - 1) * pillGap)) * 0.5f;
        for (int g = 0; g < kHubGameCount; ++g) {
            ImGui::SetCursorScreenPos(ImVec2(pillX, top + 16.0f));
            ImGui::PushID(1000 + g);
            const bool unavailable = !settingsClients[g];
            if (glassButton(gameNames[g], ImVec2(pillW, 38.0f), g == settingsGame, unavailable) && g != settingsGame) {
                settingsGame = g;
                settingsGroup = 0;
                optionOrder.clear();
            }
            ImGui::PopID();
            pillX += pillW + pillGap;
        }
        top += 54.0f;
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
        if (!pendingRequest.empty()) {
            SettingsClient::start(settings, std::move(pendingRequest), pendingLabel);
            pendingRequest.clear();
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
                ImGui::SetCursorScreenPos(ImVec2(size.x * 0.5f - 70.0f, top + 120.0f));
                if (glassButton("Retry", ImVec2(140.0f, 44.0f))) queue({ "--settings-dump" });
            }
            return;
        }
        const SettingsSnapshot& snap = settings->snapshot;
        const std::vector<SettingsGroup>& groups = snap.groups;
        if (groups.empty()) return;
        // Los paquetes antiguos de Pikmin 1 añaden dos páginas administrativas.
        // Fusion/Pikmin 2 declara exactF1: sus grupos vienen enteros del juego y
        // no se les debe mezclar ninguna página heredada.
        const int packsPage = int(groups.size()), modelsPage = packsPage + 1;
        const int lastPage = snap.exactF1 ? int(groups.size()) - 1 : modelsPage;
        settingsGroup = std::clamp(settingsGroup, 0, lastPage);

        const float margin = std::max(40.0f, (size.x - 1000.0f) * 0.5f);
        const float listW = 180.0f;
        const float panelX = margin + listW + 18.0f;
        const float panelW = size.x - margin - panelX;
        const float bottom = size.y - 64.0f;
        // Algunas opciones de desarrollo documentan varias teclas. Cuatro
        // líneas caben completas aquí tanto en Pikmin 1 como en Pikmin 2.
        const float helpH = 104.0f;

        // Grupos a la izquierda, como pestañas verticales.
        for (int g = 0; g <= lastPage; ++g) {
            const char* name = g == packsPage ? "Texture Packs" : g == modelsPage ? "HD Models" : groups[size_t(g)].name.c_str();
            ImGui::SetCursorScreenPos(ImVec2(margin, top + 24.0f + float(g) * 50.0f));
            ImGui::PushID(g);
            if (glassButton(name, ImVec2(listW, 40.0f), g == settingsGroup)) settingsGroup = g;
            ImGui::PopID();
        }
        // Restaurar: pide un segundo clic para confirmar.
        resetArmed = std::max(0.0f, resetArmed - dt);
        ImGui::SetCursorScreenPos(ImVec2(margin, bottom - 44.0f));
        if (glassButton(resetArmed > 0.0f ? "Sure? Click" : "Reset all", ImVec2(listW, 44.0f), false, busy)) {
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
        ImGui::BeginChild("rows", ImVec2(panelW, bottom - helpH - 12.0f - (top + 24.0f)), ImGuiChildFlags_Borders);
        int pick[2] = { -1, -1 }; // fila y opción elegidas este fotograma
        std::string help;
        const SettingsGroup* shownGroup = settingsGroup < packsPage ? &groups[size_t(settingsGroup)] : nullptr;
        if (!snap.exactF1 && settingsGroup == packsPage) drawTexturePacks(snap, busy);
        if (!snap.exactF1 && settingsGroup == modelsPage) drawHdModels(snap, busy);
        if (shownGroup) {
        const SettingsGroup& group = *shownGroup;
        const float controlW = std::min(300.0f, panelW * 0.45f);
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
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(row.enabled ? ImVec4(1.0f, 0.93f, 0.80f, 1.0f) : ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "%s",
                               row.label.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - controlW);
            ImGui::SetNextItemWidth(controlW);
            if (row.editable()) {
                ImGui::BeginDisabled(busy);
                if (ImGui::BeginCombo("##value", row.value.c_str())) {
                    for (const std::string& option : stableOptions(group.id, row)) {
                        const bool selected = option == row.value;
                        if (ImGui::Selectable(option.c_str(), selected) && !selected) {
                            const auto it = std::find(row.options.begin(), row.options.end(), option);
                            if (it != row.options.end()) { pick[0] = row.row; pick[1] = int(it - row.options.begin()); }
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
            } else {
                // Sin lista: se enseña el valor y, si hace falta, dónde cambiarlo.
                std::string shown = row.value;
                if (row.enabled && (row.picker || row.action)) shown += "   (in game: F1)";
                ImGui::TextColored(ImVec4(0.62f, 0.58f, 0.44f, 1.0f), "%s", shown.c_str());
            }
            // Ayuda de la fila bajo el ratón.
            const float rowH = ImGui::GetCursorPosY() - rowY;
            const ImVec2 rowMin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + rowY - ImGui::GetScrollY());
            if (ImGui::IsMouseHoveringRect(rowMin, ImVec2(rowMin.x + ImGui::GetWindowWidth(), rowMin.y + rowH))) help = row.help;
            ImGui::PopID();
        }
        }
        ImGui::EndChild();

        // Barra de ayuda debajo.
        const ImVec2 a(panelX, bottom - helpH), b(panelX + panelW, bottom);
        dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 215), 14.0f);
        dl->AddRect(a, b, col(kOrangeDeep, 0.6f), 14.0f, 0, 1.5f);
        // Prioridad: trabajando, error, resultado de instalar, ayuda de la fila.
        std::string note = help;
        ImVec4 noteColour(1.0f, 0.93f, 0.80f, 0.95f);
        if (busy) note = settings->busyLabel;
        else if (!settings->error.empty()) { note = settings->error; noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f); }
        else if (!snap.message.empty() && help.empty()) {
            note = snap.message;
            if (snap.messageIsError) noteColour = ImVec4(1.0f, 0.55f, 0.40f, 1.0f);
        }
        dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 16.0f, a.y + 8.0f), col(noteColour), note.c_str(), nullptr, panelW - 32.0f);

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
        const float width = std::min(size.x - 80.0f, 760.0f);
        const float x = (size.x - width) * 0.5f;
        float y = top + 30.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 panelA(x, y), panelB(x + width, y + 380.0f);
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
        pathBox(ImVec2(inner, y + 28.0f), ImVec2(inner + innerW - 132.0f, y + 72.0f), moveTarget, "No folder chosen");
        ImGui::SetCursorScreenPos(ImVec2(inner + innerW - 120.0f, y + 28.0f));
        if (glassButton("Browse", ImVec2(120.0f, 44.0f), false, running)) {
            const fs::path chosen = platform::askForInstallDirectory();
            if (!chosen.empty()) {
                fs::path target;
                moveNote.clear();
                if (resolveMoveTarget(chosen, target, moveNote)) moveTarget = target.string();
                else moveTarget.clear();
            }
        }
        y += 96.0f;

        if (running) {
            // Barra de progreso de la copia.
            const ImVec2 a(inner, y + 4.0f), b(inner + innerW - 190.0f, y + 34.0f);
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
                        nullptr, innerW - 200.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(inner + innerW - 170.0f, y));
        if (glassButton(running ? "Moving..." : "Move", ImVec2(170.0f, 44.0f), true, running || moveTarget.empty())) {
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
        const ImVec2 extent = fonts.body->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, text.c_str());
        const ImVec2 b(size.x - 18.0f, 58.0f), a(b.x - extent.x - 40.0f, 18.0f);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##release", ImVec2(b.x - a.x, b.y - a.y));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
        dl->AddRectFilled(ImVec2(a.x - 4, a.y - 4), ImVec2(b.x + 4, b.y + 4), col(kOrange, 0.10f + 0.12f * pulse), 24.0f);
        dl->AddRectFilled(a, b, hovered ? col(kLeafBright) : col(kLeaf), 20.0f);
        dl->AddRect(a, b, col(kOrange), 20.0f, 0, 2.5f);
        dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 20.0f, a.y + (b.y - a.y - 19.0f) * 0.5f), col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)),
                    text.c_str());
        if (clicked) choose(HubAction::Update, HubGame::Pikmin1);

        // "What's new" a su izquierda, si el release trae notas.
        std::string notes, version;
        {
            std::lock_guard<std::mutex> lock(releaseCheck->mutex);
            notes = releaseCheck->latest.notes;
            version = releaseCheck->latest.version;
        }
        if (notes.empty()) return;
        const float w = fonts.body->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, "What's new").x + 50.0f;
        ImGui::SetCursorScreenPos(ImVec2(a.x - w - 12.0f, a.y));
        if (glassButton("What's new", ImVec2(w, b.y - a.y))) {
            whatsNewOpen = true;
            whatsNewVersion = version;
            whatsNewNotes = notes;
        }
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
        ImGui::Begin("##whatsnew", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBackground);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(2, 12, 4, 170));
        const ImVec2 panel(std::min(size.x - 80.0f, 860.0f), size.y - 110.0f);
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
        ImGui::BeginChild("##notes", ImVec2(panel.x - 68.0f, panel.y - 80.0f - buttonsH - 44.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImGui::PushTextWrapPos(0.0f);
        drawNotes(whatsNewNotes);
        ImGui::PopTextWrapPos();
        ImGui::EndChild();

        const std::string updateLabel = "Update to " + whatsNewVersion;
        const float updateW = fonts.body->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, updateLabel.c_str()).x + 50.0f;
        const float closeW = 130.0f, y = b.y - buttonsH - 26.0f;
        ImGui::SetCursorScreenPos(ImVec2(b.x - 34.0f - updateW, y));
        if (glassButton(updateLabel.c_str(), ImVec2(updateW, buttonsH), true)) {
            whatsNewOpen = false;
            choose(HubAction::Update, HubGame::Pikmin1);
        }
        ImGui::SetCursorScreenPos(ImVec2(b.x - 34.0f - updateW - 14.0f - closeW, y));
        if (glassButton("Close", ImVec2(closeW, buttonsH)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            whatsNewOpen = false;
        }
        ImGui::End();
    }

    void drawFooter(ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const std::string where = state.games[0].directory.empty()
            ? std::string("Not installed yet")
            : "Installed in: " + state.games[0].directory;
        // Placa semitransparente para que el texto se lea sobre la hierba.
        const ImVec2 extent = fonts.caption->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, where.c_str());
        dl->AddRectFilled(ImVec2(12, size.y - 38), ImVec2(32 + extent.x, size.y - 10), IM_COL32(6, 26, 10, 200), 14.0f);
        dl->AddText(fonts.caption, 16.0f, ImVec2(22, size.y - 33), col(kOrange), where.c_str());

        if (toastTime > 0.0f) {
            const float alpha = std::min(1.0f, toastTime);
            const ImVec2 t = fonts.body->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, toast.c_str());
            const ImVec2 a((size.x - t.x) * 0.5f - 20.0f, size.y - 96.0f);
            const ImVec2 b((size.x + t.x) * 0.5f + 20.0f, size.y - 56.0f);
            dl->AddRectFilled(a, b, col(ImVec4(0.04f, 0.16f, 0.06f, 0.95f), alpha), 20.0f);
            dl->AddRect(a, b, col(kOrange, alpha), 20.0f, 0, 2.0f);
            dl->AddText(fonts.body, 19.0f, ImVec2(a.x + 20.0f, a.y + 9.0f), col(kOrange, alpha), toast.c_str());
        }
    }

    // `interactive` = false mientras hay un modal encima: se pinta igual, pero
    // no responde al ratón.
    void frame(ImVec2 size, float t, float dt, bool interactive = true)
    {
        collectCovers();
        drawBackground(ImGui::GetBackgroundDrawList(), size, dt);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##hub", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        ImGui::BeginDisabled(!interactive || whatsNewOpen);

        const float logoW = std::min(size.x * 0.56f, 700.0f);
        const float logoH = logoW / logo.aspect();
        const ImVec2 logoAt((size.x - logoW) * 0.5f, 18.0f);
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
            contentTop += 42.0f;
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
        ImGui::EndDisabled();
        ImGui::End();
        toastTime = std::max(0.0f, toastTime - dt);
        drawFooter(size);
        if (whatsNewOpen && interactive) drawWhatsNew(size);
    }

    void release()
    {
        for (Texture& cover : covers) freeTexture(cover);
        freeTexture(logo);
        slideshow.release();
        art.release();
    }
};

void loadFonts(Fonts& fonts)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
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
    bool startAtHome = false;  // la próxima elección de rutas empieza en la pantalla principal
    bool playRequested = false;
    HubGame installGame = HubGame::Pikmin1; // juego que instalan los modales de rutas y progreso
    std::function<HubResult()> homeLoop;   // ver setHomeLoop

    enum class Modal { None, Paths, Progress, Message };
    Modal modal = Modal::None;
    // Rutas
    std::string rom, installDirectory, pathsNote;
    int pathsAction = 0; // 1 examinar imagen, 2 examinar carpeta, 3 instalar, 4 cancelar
    // Progreso
    std::uint32_t percent = 0;
    std::string phase, currentFile;
    // Mensaje
    std::string messageTitle, messageText, primaryLabel, secondaryLabel;
    bool messageIsError = false;
    int answer = -1; // 0 principal, 1 secundario

    ~Impl()
    {
        if (!window) return;
        if (hub) hub->release();
        hub.reset();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    // Panel centrado del modal, con velo sobre la pantalla principal.
    // Devuelve la esquina superior izquierda del área de contenido.
    ImVec2 beginModal(ImVec2 size, ImVec2 panel)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
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
        const float y = at.y + 30.0f, h = 44.0f, buttonW = 120.0f;
        const ImVec2 a(at.x, y), b(at.x + width - buttonW - 12.0f, y + h);
        dl->AddRectFilled(a, b, IM_COL32(4, 22, 8, 235), 14.0f);
        dl->AddRect(a, b, col(kOrangeDeep, 0.8f), 14.0f, 0, 2.0f);
        const bool has = !value.empty();
        const std::string shown = has ? fitTail(hub->fonts.body, 19.0f, value, b.x - a.x - 28.0f) : empty;
        dl->AddText(hub->fonts.body, 19.0f, ImVec2(a.x + 14.0f, a.y + 11.0f), has ? col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)) : col(kOrange, 0.5f),
                    shown.c_str());
        ImGui::SetCursorScreenPos(ImVec2(b.x + 12.0f, y));
        ImGui::PushID(id);
        const bool clicked = hub->glassButton("Browse", ImVec2(buttonW, h));
        ImGui::PopID();
        return clicked;
    }

    void drawPaths(ImVec2 size)
    {
        const ImVec2 panel(std::min(size.x - 80.0f, 700.0f), 430.0f);
        const ImVec2 at = beginModal(size, panel);
        const float width = panel.x - 68.0f;
        const bool pikmin2 = installGame == HubGame::Pikmin2;
        title(at, pikmin2 ? "Install Pikmin 2" : "Install Pikmin");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(hub->fonts.caption, 16.0f, ImVec2(at.x, at.y + 42.0f), col(kOrange, 0.75f),
                    pikmin2 ? "Use your own disc: Pikmin 2 Europe (GPVP01). ISO or GCM; RVZ, WIA and GCZ need Dolphin."
                            : "Use your own disc: Pikmin USA (Rev 1) or Europe. ISO or GCM; RVZ, WIA and GCZ need Dolphin.");
        if (pathField(ImVec2(at.x, at.y + 80.0f), width, "Disc image", rom, "No disc image chosen", "rom")) pathsAction = 1;
        if (pathField(ImVec2(at.x, at.y + 170.0f), width, "Install to", installDirectory, "No folder chosen", "dir")) pathsAction = 2;
        if (!pathsNote.empty()) {
            dl->AddText(hub->fonts.caption, 16.0f, ImVec2(at.x, at.y + 262.0f), col(ImVec4(1.0f, 0.45f, 0.30f, 1.0f)), pathsNote.c_str());
        }
        const float buttonsY = at.y + panel.y - 110.0f;
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - 330.0f, buttonsY));
        if (hub->glassButton("Cancel", ImVec2(150.0f, 50.0f))) pathsAction = 4;
        ImGui::SetCursorScreenPos(ImVec2(at.x + width - 168.0f, buttonsY));
        if (hub->glassButton("Install", ImVec2(168.0f, 50.0f), true, rom.empty() || installDirectory.empty())) pathsAction = 3;
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
        const ImVec2 panel(width + 68.0f, 170.0f + textH);
        const ImVec2 at = beginModal(size, panel);
        title(at, messageTitle.c_str());
        ImGui::GetWindowDrawList()->AddText(hub->fonts.body, 20.0f, ImVec2(at.x, at.y + 52.0f),
                                            messageIsError ? col(ImVec4(1.0f, 0.78f, 0.62f, 1.0f)) : col(ImVec4(1.0f, 0.93f, 0.80f, 1.0f)),
                                            messageText.c_str(), nullptr, width);
        const float buttonsY = at.y + panel.y - 96.0f;
        float x = at.x + width;
        const float primaryW = std::max(150.0f, hub->fonts.body->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, primaryLabel.c_str()).x + 50.0f);
        x -= primaryW;
        ImGui::SetCursorScreenPos(ImVec2(x, buttonsY));
        if (hub->glassButton(primaryLabel.c_str(), ImVec2(primaryW, 50.0f), true)) answer = 0;
        if (!secondaryLabel.empty()) {
            const float secondaryW = std::max(130.0f, hub->fonts.body->CalcTextSizeA(21.0f, FLT_MAX, 0.0f, secondaryLabel.c_str()).x + 50.0f);
            x -= secondaryW + 14.0f;
            ImGui::SetCursorScreenPos(ImVec2(x, buttonsY));
            if (hub->glassButton(secondaryLabel.c_str(), ImVec2(secondaryW, 50.0f))) answer = 1;
        }
        endModal();
    }

    // Un fotograma: eventos, pantalla principal y el modal que toque.
    void frame()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        hub->frame(size, t, dt, modal == Modal::None);
        switch (modal) {
        case Modal::None: break;
        case Modal::Paths: drawPaths(size); break;
        case Modal::Progress: drawProgress(size, t); break;
        case Modal::Message: drawMessage(size); break;
        }
        ImGui::Render();

        int fbW = 0, fbH = 0;
        SDL_GL_GetDrawableSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.035f, 0.13f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Muestra un mensaje hasta que se pulsa un botón. -1 si se cierra la ventana.
    int message(const std::string& heading, const std::string& text, const std::string& primary,
                const std::string& secondary, bool isError)
    {
        modal = Modal::Message;
        messageTitle = heading;
        messageText = text;
        primaryLabel = primary;
        secondaryLabel = secondary;
        messageIsError = isError;
        answer = -1;
        while (answer < 0 && !quitRequested) frame();
        modal = Modal::None;
        return answer;
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Open Nectar", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 780,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetWindowMinimumSize(window, 960, 700);
    pc_icon_apply(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        error = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    m.window = window;
    m.context = context;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 130");
    applyTheme();

    m.hub = std::make_unique<Hub>(m.state, m.result);
    Hub& hub = *m.hub;
    loadFonts(hub.fonts);
    hub.logo = loadImageTexture(kLogoOpenNectar, kLogoOpenNectarSize, 160);
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
    if (!m.state.installerOnly && m.state.games[0].installed) {
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
                   const std::string& secondary, bool isError)
{
    Impl& m = *mImpl;
    return m.message(title, text, primary, secondary, isError);
}

bool HubWindow::choosePaths(const std::function<std::string()>& chooseRom,
                            const std::function<std::string()>& chooseInstallDirectory,
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
        m.pathsNote.clear();
        for (;;) {
            m.pathsAction = 0;
            m.frame();
            if (m.quitRequested) return false;
            if (m.pathsAction == 1) {
                const std::string selected = chooseRom();
                if (!selected.empty()) m.rom = selected;
            } else if (m.pathsAction == 2) {
                const std::string selected = chooseInstallDirectory();
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

void HubWindow::showComplete(const std::string& installDirectory, bool willLaunch)
{
    Impl& m = *mImpl;
    m.state.games[0].installed = true;
    m.state.games[0].directory = installDirectory;
    const std::string text = "Pikmin is installed in:\n" + installDirectory
                           + "\n\nIn game, F1 opens graphics, controls and gameplay settings."
                             "\nOpen nectar-launcher from that folder to play again.";
    m.message("Installation complete", text, willLaunch ? "Play now" : "OK", "", false);
    m.quitRequested = false;
}

} // namespace launcher
} // namespace pikmin
