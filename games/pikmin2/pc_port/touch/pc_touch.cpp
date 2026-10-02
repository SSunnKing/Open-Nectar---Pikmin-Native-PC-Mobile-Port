#include "pc_touch.h"
#include "../pc_photo_mode.h"

#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Dolphin/pad.h"
#include "gl/pc_gfx.h"
#include "pc_window.h"
#include "settings/pc_settings.h"

// stb_image se compila una sola vez, en pc_port/gl/pc_texpack.cpp (plan
// TEXTURAS_HD). Aquí solo se declara la API (PNG, sin stdio).
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace {

// ── Disposición ──────────────────────────────────────────────────────────────
// Todo se mide en fracciones de la altura de la ventana (H), que es lo que
// vale igual en un móvil y en una tablet apaisados. Los botones grandes
// quedan por encima del contador de Pikmin (franja inferior del HUD) y los
// pequeños lejos del día y del sol (esquina y centro superiores).

enum ButtonId {
	BTN_THROW,
	BTN_WHISTLE,
	BTN_DISBAND,
	BTN_PAUSE,
	BTN_MAP,
	BTN_CAMERA_ANGLE,
	BTN_CAMERA_CENTER,
	BTN_SETTINGS,
	BTN_LAYOUT,
	BTN_PHOTO,
	BTN_BACK,
	BTN_MENU_CONFIRM,
	BTN_COUNT
};

struct ButtonSpec {
	const char* art;   // glifo
	float cx, cy;      // centro: x en fracciones de H desde el borde indicado, y en fracciones de H
	bool fromRight;    // x medido desde el borde derecho
	float radius;      // fracción de H
	u16 pad;           // botón de mando que sintetiza (0 = acción propia)
};

const ButtonSpec kButtons[BTN_COUNT] = {
	{ "throw", 0.15f, 0.70f, true, 0.085f, PAD_BUTTON_A },
	{ "whistle", 0.45f, 0.79f, true, 0.085f, PAD_BUTTON_B },
	{ "disband", 0.27f, 0.52f, true, 0.055f, PAD_BUTTON_X },
	{ "pause", 0.08f, 0.09f, false, 0.05f, PAD_BUTTON_START },
	{ "map", 0.08f, 0.17f, true, 0.05f, PAD_BUTTON_Y },
	{ "camera_angle", 0.08f, 0.53f, true, 0.05f, PAD_TRIGGER_Z },
	{ "camera_center", 0.08f, 0.35f, true, 0.05f, PAD_TRIGGER_L },
	{ "settings", 0.25f, 0.09f, false, 0.045f, 0 },
	{ "layout", 0.37f, 0.09f, false, 0.045f, 0 },
	{ "photo_mode", 0.49f, 0.09f, false, 0.045f, 0 },
	{ "back", 0.10f, 0.84f, false, 0.055f, PAD_BUTTON_B },
	{ nullptr, 0.10f, 0.84f, true, 0.055f, PAD_BUTTON_A },
};

constexpr float kStickRadius = 0.115f; // fracción de H
constexpr float kStickDead = 0.12f;   // fracción del radio
constexpr float kLayerAlpha = 0.85f;
constexpr float kButtonScale = 1.60f;

// ── Disposición personalizable ───────────────────────────────────────────────
// El jugador puede mover y escalar cada botón desde el editor (botón "layout",
// junto al de ajustes). Se guarda en un archivo propio, junto al de ajustes,
// con las mismas unidades que kButtons: fracciones de H desde el borde más
// cercano, así la misma disposición sirve en cualquier resolución.
constexpr const char* kLayoutFilename = "pikmin_touch_layout.conf";
constexpr float kScaleMin = 0.60f, kScaleMax = 1.60f, kScaleStep = 0.10f;
constexpr int kGridCells = 16; // celdas por alto de pantalla

struct ButtonLayout {
	float cx, cy;   // como ButtonSpec
	bool fromRight;
	float scale;    // multiplicador del radio por defecto
};
ButtonLayout sLayout[BTN_COUNT];
bool sLayoutLoaded = false;
bool sLayoutDirty = true; // recalcular píxeles

bool sEditMode = false;
bool sEditGrid = false;
int sEditSelected = -1;      // botón resaltado (recibe +/−)
int sEditDragButton = -1;    // botón que se arrastra
long long sEditDragFinger = -1;
float sEditDragOffX = 0, sEditDragOffY = 0;
bool sEditDragMoved = false;
long long sEditToolFinger = -1;
int sEditToolPressed = -1;

enum EditTool { TOOL_GRID, TOOL_MINUS, TOOL_PLUS, TOOL_RESET, TOOL_COUNT };
struct ToolState { float cx = 0, cy = 0, r = 0; };
ToolState sTools[TOOL_COUNT];

bool button_editable(int i) { return i != BTN_BACK && i != BTN_MENU_CONFIRM; }

struct ButtonState {
	long long finger = -1; // dedo que lo mantiene, o -1
	bool tapLatch = false; // pulsado y soltado entre dos frames: cuenta un frame
	float cx = 0, cy = 0, r = 0; // en píxeles, para la disposición actual
	float lastX = 0, lastY = 0;
};

// Disolver hace doble función: un toque es X (disolver), pero mantenerlo y
// arrastrar es el C-stick (el grupo va hacia donde se arrastra). Hasta que
// el pulgar se aleja del punto de apoyo no se sabe cuál de las dos es, así
// que la X se entrega al soltar, y solo si no hubo arrastre.
struct GroupDrag {
	float ax = 0, ay = 0; // punto de apoyo
	float dx = 0, dy = 0; // desplazamiento actual, acotado
	bool dragging = false;
};
GroupDrag sGroupDrag;
constexpr float kGroupDragStart = 0.025f; // fracción de H: a partir de aquí es arrastre
constexpr float kGroupDragFull = 0.12f;   // fracción de H: deflexión máxima del C-stick

struct Stick {
	long long finger = -1;
	float ax = 0, ay = 0; // anclaje
	float dx = 0, dy = 0; // desplazamiento actual, ya acotado al radio
	float radius = 0;
};

struct GestureFinger {
	long long finger = -1;
	float x = 0, y = 0;
	bool moved = false;
};

// Deslizamiento en menús del juego: el menú de pausa cambia de página con
// L/R, que no tienen botón en pantalla. Se recuerda dónde apoyó cada dedo y,
// al levantarlo, un arrastre horizontal claro cuenta como L o R.
struct SwipeFinger {
	long long finger = -1;
	float x0 = 0, y0 = 0;
};
SwipeFinger sSwipes[6];
constexpr float kSwipeMinFraction = 0.10f; // del ancho de pantalla
u16 sSwipeButtons = 0;   // L/R pendientes de entregar
int sSwipeHoldFrames = 0;

// Icono del Pikmin siguiente (HUD): rectángulo en ventana registrado al
// dibujarlo. Caduca en pocos ticks para que no responda con el HUD oculto.
struct ColorIcon {
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	int age = 999;
};
ColorIcon sColorIcon;
int sColorTaps = 0;
long long sColorTapFinger = -1;

bool in_color_icon(float x, float y)
{
	if (sColorIcon.age > 3) return false;
	// Un margen alrededor: el icono es pequeño y el pulgar no es preciso.
	const float pad = (sColorIcon.y1 - sColorIcon.y0) * 0.35f;
	return x >= sColorIcon.x0 - pad && x <= sColorIcon.x1 + pad && y >= sColorIcon.y0 - pad && y <= sColorIcon.y1 + pad;
}

// Iconos en línea (texto de tutoriales): posición ya proyectada a ventana.
struct InlineIcon {
	int button;
	float cx, cy, size;
};
std::vector<InlineIcon> sInlineIcons;

// Iconos de la página de controles: igual, pero con las etiquetas extra.
struct HelpIcon {
	char tag;
	float cx, cy, diam, alpha; // diam = diámetro de la burbuja en ventana
};
std::vector<HelpIcon> sHelpIcons;

// Botones que un menú ha "pedido" al mostrar su icono (mensajes, copiar/
// borrar partida...): mientras el menú reclama la capa se dibujan y se pueden
// pulsar además de B/atrás. Máscara de bits por índice BTN_*, calculada al
// dibujar el frame y usada por los toques del siguiente.
unsigned sMenuWantedButtons = 0;

int button_for_tag(char tag)
{
	switch (tag) {
	case 'a': return BTN_THROW;
	case 'b': return BTN_WHISTLE;
	case 'x': return BTN_DISBAND;
	case 'y': return BTN_MAP;
	case 'z': return BTN_CAMERA_ANGLE;
	case 'l': return BTN_CAMERA_CENTER;
	default: return -1; // 'r' (zoom: pellizco) y 'c' (C-stick): texto
	}
}

// Botón de la capa que representa un icono de ayuda/botón (o -1).
int menu_button_for_help_tag(char tag)
{
	if (tag == 'p') return BTN_PAUSE;
	if (tag == 'c') return BTN_DISBAND;
	return button_for_tag(tag);
}

ButtonState sButtons[BTN_COUNT];
Stick sStick;
GestureFinger sRightGesture[2];
float sPinchDistance = -1.0f;
bool sVisible = false;
bool sTouchedThisFrame = false;
bool sSettingsRequested = false;
bool sLayoutRequested = false;
u16 sPreviousMenuButtons = 0;
u16 sQueuedGameButtons = 0;
int sLayoutW = 0, sLayoutH = 0;
int sGameMenuClaimFrames = 0;
int sPortMenuClaimFrames = 0;

// Con un menú reclamando la capa: B/atrás, A/confirmar del menú F1 y los
// botones cuyo icono está en pantalla.
bool menu_button_allowed(int i)
{
	if (i == BTN_BACK) return true;
	if (i == BTN_MENU_CONFIRM) return sPortMenuClaimFrames > 0;
	return (sMenuWantedButtons >> i) & 1u;
}
bool sGameMenuTapPending = false;
int sGameMenuTapAge = 0;
float sGameMenuTapX = 0.0f, sGameMenuTapY = 0.0f;

// ── Arte ─────────────────────────────────────────────────────────────────────
struct Art {
	unsigned bubble = 0, bubblePressed = 0, ring = 0, knob = 0, pinch = 0, swipeL = 0, swipeR = 0;
	unsigned glyph[BTN_COUNT] = {};
	unsigned white = 0; // 1x1 para rectángulos (cuadrícula y glifos del editor)
	bool loaded = false;
};
Art sArt;

unsigned load_texture(const char* name)
{
	// En Android una ruta relativa se lee de los assets del APK; en
	// escritorio, del árbol de fuentes (o de PIKMIN_TOUCH_ART).
	std::vector<std::string> candidates;
	candidates.push_back(std::string("art/") + name + ".png");
	if (const char* dir = std::getenv("PIKMIN_TOUCH_ART")) candidates.insert(candidates.begin(), std::string(dir) + "/" + name + ".png");
	candidates.push_back(std::string("pc_port/touch/assets/art/") + name + ".png");
	candidates.push_back(std::string("../pc_port/touch/assets/art/") + name + ".png");
	for (const std::string& path : candidates) {
		SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
		if (!rw) continue;
		const Sint64 size = SDL_RWsize(rw);
		if (size <= 0) { SDL_RWclose(rw); continue; }
		std::vector<unsigned char> bytes((size_t)size);
		const size_t got = SDL_RWread(rw, bytes.data(), 1, (size_t)size);
		SDL_RWclose(rw);
		if (got != (size_t)size) continue;
		int w = 0, h = 0, comp = 0;
		unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
		if (!rgba) {
			printf("[Touch] %s: %s\n", path.c_str(), stbi_failure_reason());
			continue;
		}
		const unsigned tex = pc_gfx_overlay_texture_create(w, h, rgba);
		stbi_image_free(rgba);
		return tex;
	}
	printf("[Touch] missing art: %s\n", name);
	return 0;
}

void ensure_art()
{
	if (sArt.loaded) return;
	sArt.loaded = true;
	sArt.bubble = load_texture("bubble");
	sArt.bubblePressed = load_texture("bubble_pressed");
	sArt.ring = load_texture("stick_ring");
	sArt.knob = load_texture("stick_knob");
	sArt.pinch = load_texture("pinch");
	sArt.swipeL = load_texture("swipe_l");
	sArt.swipeR = load_texture("swipe_r");
	{
		const unsigned char px[4] = { 255, 255, 255, 255 };
		sArt.white = pc_gfx_overlay_texture_create(1, 1, px);
	}
	for (int i = 0; i < BTN_COUNT; ++i)
		sArt.glyph[i] = kButtons[i].art ? load_texture(kButtons[i].art) : 0;
}

// ── Disposición en píxeles ───────────────────────────────────────────────────
void layout_defaults()
{
	for (int i = 0; i < BTN_COUNT; ++i) {
		sLayout[i].cx = kButtons[i].cx;
		sLayout[i].cy = kButtons[i].cy;
		sLayout[i].fromRight = kButtons[i].fromRight;
		sLayout[i].scale = 1.0f;
	}
	sLayoutDirty = true;
}

void layout_save()
{
	FILE* out = fopen(kLayoutFilename, "w");
	if (!out) {
		printf("[Touch] cannot write %s\n", kLayoutFilename);
		return;
	}
	fprintf(out, "# Open Nectar touch layout: name = x y fromRight scale (x,y in fractions of screen height)\n");
	fprintf(out, "grid = %d\n", sEditGrid ? 1 : 0);
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (!kButtons[i].art || !button_editable(i)) continue;
		fprintf(out, "%s = %.4f %.4f %d %.2f\n", kButtons[i].art, sLayout[i].cx, sLayout[i].cy, sLayout[i].fromRight ? 1 : 0, sLayout[i].scale);
	}
	fclose(out);
	printf("[Touch] saved %s\n", kLayoutFilename);
}

void layout_load()
{
	if (sLayoutLoaded) return;
	sLayoutLoaded = true;
	layout_defaults();
	FILE* in = fopen(kLayoutFilename, "r");
	if (!in) return;
	char line[256];
	while (fgets(line, sizeof(line), in)) {
		char name[64];
		float cx = 0, cy = 0, scale = 1;
		int fromRight = 0, grid = 0;
		if (sscanf(line, "grid = %d", &grid) == 1) {
			sEditGrid = grid != 0;
			continue;
		}
		if (sscanf(line, "%63s = %f %f %d %f", name, &cx, &cy, &fromRight, &scale) != 5) continue;
		for (int i = 0; i < BTN_COUNT; ++i) {
			if (!kButtons[i].art || !button_editable(i) || strcmp(kButtons[i].art, name) != 0) continue;
			sLayout[i].cx = std::fmax(0.0f, cx);
			sLayout[i].cy = std::fmax(0.0f, cy);
			sLayout[i].fromRight = fromRight != 0;
			sLayout[i].scale = std::fmax(kScaleMin, std::fmin(kScaleMax, scale));
		}
	}
	fclose(in);
	printf("[Touch] loaded %s\n", kLayoutFilename);
}

void layout(int w, int h)
{
	layout_load();
	if (w == sLayoutW && h == sLayoutH && !sLayoutDirty) return;
	sLayoutW = w;
	sLayoutH = h;
	sLayoutDirty = false;
	const float H = (float)h;
	for (int i = 0; i < BTN_COUNT; ++i) {
		const ButtonLayout& l = sLayout[i];
		sButtons[i].cx = l.fromRight ? (float)w - l.cx * H : l.cx * H;
		sButtons[i].cy = l.cy * H;
		sButtons[i].r = kButtons[i].radius * H * kButtonScale * l.scale;
	}
	sStick.radius = kStickRadius * H;
	// Barra de herramientas del editor: centrada arriba, lejos de los botones
	// de pausa/ajustes (izquierda) y del mapa (derecha).
	const float toolR = 0.045f * H * kButtonScale;
	const float gap = toolR * 2.4f;
	const float x0 = (float)w * 0.5f - gap * (TOOL_COUNT - 1) * 0.5f;
	for (int t = 0; t < TOOL_COUNT; ++t) {
		sTools[t].cx = x0 + gap * t;
		sTools[t].cy = 0.09f * H;
		sTools[t].r = toolR;
	}
}

// Devuelve el botón a la posición (píxeles) dada, guardando en fracciones de
// H desde el borde más cercano y acotando al área visible.
void layout_place(int i, float px, float py)
{
	const float H = (float)sLayoutH, W = (float)sLayoutW;
	const float r = sButtons[i].r;
	px = std::fmax(r, std::fmin(W - r, px));
	py = std::fmax(r, std::fmin(H - r, py));
	if (sEditGrid) {
		const float cell = H / (float)kGridCells;
		px = std::round(px / cell) * cell;
		py = std::round(py / cell) * cell;
		px = std::fmax(r, std::fmin(W - r, px));
		py = std::fmax(r, std::fmin(H - r, py));
	}
	sLayout[i].fromRight = px > W * 0.5f;
	sLayout[i].cx = (sLayout[i].fromRight ? W - px : px) / H;
	sLayout[i].cy = py / H;
	sButtons[i].cx = px;
	sButtons[i].cy = py;
}

int tool_at(float x, float y)
{
	for (int t = 0; t < TOOL_COUNT; ++t) {
		const float dx = x - sTools[t].cx, dy = y - sTools[t].cy;
		if (dx * dx + dy * dy <= sTools[t].r * sTools[t].r * 1.3f) return t;
	}
	return -1;
}

int editable_button_at(float x, float y)
{
	int best = -1;
	float bestD = 0;
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (!button_editable(i)) continue;
		const float dx = x - sButtons[i].cx, dy = y - sButtons[i].cy;
		const float d = dx * dx + dy * dy;
		if (d <= sButtons[i].r * sButtons[i].r && (best < 0 || d < bestD)) {
			best = i;
			bestD = d;
		}
	}
	return best;
}

void edit_set_scale(int i, float scale)
{
	if (i < 0 || !button_editable(i)) return;
	sLayout[i].scale = std::fmax(kScaleMin, std::fmin(kScaleMax, scale));
	sLayoutDirty = true;
	layout(sLayoutW, sLayoutH);
	// Que al crecer no se salga de la pantalla.
	layout_place(i, sButtons[i].cx, sButtons[i].cy);
}

void edit_enter()
{
	sEditMode = true;
	sEditSelected = -1;
	sEditDragButton = -1;
	sEditDragFinger = -1;
	sEditToolFinger = -1;
	sEditToolPressed = -1;
	// Soltar todo lo que estuviera pulsado: el juego no debe recibir nada.
	for (ButtonState& b : sButtons) {
		b.finger = -1;
		b.tapLatch = false;
	}
	sStick.finger = -1;
	sStick.dx = sStick.dy = 0.0f;
	for (GestureFinger& g : sRightGesture) g.finger = -1;
	sPinchDistance = -1.0f;
	sGroupDrag.dragging = false;
}

void edit_exit()
{
	sEditMode = false;
	sEditSelected = -1;
	sEditDragButton = -1;
	sEditDragFinger = -1;
	layout_save();
}

// Entrada mientras se edita: arrastrar mueve, la barra de arriba escala y
// alterna la cuadrícula, y un toque en el botón "layout" cierra y guarda.
void edit_on_finger(long long fingerId, PcTouchPhase phase, float x, float y)
{
	switch (phase) {
	case PC_TOUCH_DOWN: {
		const int tool = tool_at(x, y);
		if (tool >= 0 && sEditToolFinger < 0) {
			sEditToolFinger = fingerId;
			sEditToolPressed = tool;
			break;
		}
		const int btn = editable_button_at(x, y);
		if (btn >= 0 && sEditDragFinger < 0) {
			sEditDragFinger = fingerId;
			sEditDragButton = btn;
			sEditSelected = btn;
			sEditDragOffX = x - sButtons[btn].cx;
			sEditDragOffY = y - sButtons[btn].cy;
			sEditDragMoved = false;
		}
		break;
	}
	case PC_TOUCH_MOVE:
		if (fingerId == sEditDragFinger && sEditDragButton >= 0) {
			const float tx = x - sEditDragOffX, ty = y - sEditDragOffY;
			if (!sEditDragMoved) {
				const float dx = tx - sButtons[sEditDragButton].cx, dy = ty - sButtons[sEditDragButton].cy;
				if (dx * dx + dy * dy > 0.0004f * float(sLayoutH) * float(sLayoutH)) sEditDragMoved = true;
			}
			if (sEditDragMoved) layout_place(sEditDragButton, tx, ty);
		}
		break;
	case PC_TOUCH_UP:
		if (fingerId == sEditToolFinger) {
			sEditToolFinger = -1;
			if (tool_at(x, y) == sEditToolPressed) {
				switch (sEditToolPressed) {
				case TOOL_GRID: sEditGrid = !sEditGrid; break;
				case TOOL_MINUS:
					if (sEditSelected >= 0) edit_set_scale(sEditSelected, sLayout[sEditSelected].scale - kScaleStep);
					break;
				case TOOL_PLUS:
					if (sEditSelected >= 0) edit_set_scale(sEditSelected, sLayout[sEditSelected].scale + kScaleStep);
					break;
				case TOOL_RESET: layout_defaults(); layout(sLayoutW, sLayoutH); break;
				}
			}
			sEditToolPressed = -1;
		}
		if (fingerId == sEditDragFinger) {
			const int btn = sEditDragButton;
			sEditDragFinger = -1;
			sEditDragButton = -1;
			if (btn == BTN_LAYOUT && !sEditDragMoved) edit_exit();
		}
		break;
	}
}

int button_at(float x, float y)
{
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (sGameMenuClaimFrames > 0) {
			if (!menu_button_allowed(i)) continue;
		} else if (i == BTN_BACK || i == BTN_MENU_CONFIRM) {
			continue;
		}
		const float dx = x - sButtons[i].cx, dy = y - sButtons[i].cy;
		const float reach = sButtons[i].r * 1.15f; // algo más que el dibujo: el pulgar no es preciso
		if (dx * dx + dy * dy <= reach * reach) return i;
	}
	return -1;
}

bool in_stick_zone(float x, float y)
{
	return x < sLayoutW * 0.5f && y > sLayoutH * 0.18f;
}

void update_stick(float x, float y)
{
	float dx = x - sStick.ax, dy = y - sStick.ay;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len > sStick.radius && len > 0.0f) {
		dx *= sStick.radius / len;
		dy *= sStick.radius / len;
	}
	sStick.dx = dx;
	sStick.dy = dy;
}

float pinch_distance()
{
	const float dx = sRightGesture[0].x - sRightGesture[1].x;
	const float dy = sRightGesture[0].y - sRightGesture[1].y;
	return std::sqrt(dx * dx + dy * dy);
}

} // namespace

// ── Entrada ──────────────────────────────────────────────────────────────────
void pc_touch_on_finger(long long fingerId, PcTouchPhase phase, float x, float y)
{
	int w = 0, h = 0;
	pc_gfx_get_drawable_size(&w, &h);
	if (w <= 0 || h <= 0) return;
	layout(w, h);
	sTouchedThisFrame = true;

	if (sEditMode) {
		edit_on_finger(fingerId, phase, x, y);
		return;
	}

	switch (phase) {
	case PC_TOUCH_DOWN: {
		for (SwipeFinger& s : sSwipes) {
			if (s.finger >= 0) continue;
			s.finger = fingerId;
			s.x0 = x;
			s.y0 = y;
			break;
		}
		const int btn = button_at(x, y);
		if (btn < 0 && sGameMenuClaimFrames <= 0 && sColorTapFinger < 0 && in_color_icon(x, y)) {
			// Va antes que el stick: el icono cae dentro de su zona.
			sColorTapFinger = fingerId;
			++sColorTaps;
		} else if (btn >= 0) {
			if (sButtons[btn].finger < 0) {
				sButtons[btn].finger = fingerId;
				sButtons[btn].tapLatch = btn != BTN_DISBAND; // disolver decide al soltar
				sButtons[btn].lastX = x;
				sButtons[btn].lastY = y;
				if (btn == BTN_SETTINGS) sSettingsRequested = true;
				if (btn == BTN_LAYOUT && sGameMenuClaimFrames <= 0) sLayoutRequested = true;
				// Modo foto: el mismo conmutador que F3 en escritorio.
				if (btn == BTN_PHOTO && sGameMenuClaimFrames <= 0) pc_photo_mode_request_toggle();
				if (btn == BTN_DISBAND) {
					sGroupDrag.ax = x;
					sGroupDrag.ay = y;
					sGroupDrag.dx = sGroupDrag.dy = 0.0f;
					sGroupDrag.dragging = false;
				}
			}
		} else if (sStick.finger < 0 && in_stick_zone(x, y)) {
			sStick.finger = fingerId;
			sStick.ax = x;
			sStick.ay = y;
			sStick.dx = sStick.dy = 0.0f;
		} else if (x >= sLayoutW * 0.5f) {
			for (GestureFinger& g : sRightGesture) {
				if (g.finger >= 0) continue;
				g.finger = fingerId;
				g.x = x;
				g.y = y;
				g.moved = false;
				break;
			}
			if (sRightGesture[0].finger >= 0 && sRightGesture[1].finger >= 0)
				sPinchDistance = pinch_distance();
		}
		break;
	}
	case PC_TOUCH_MOVE:
		if (fingerId == sStick.finger) update_stick(x, y);
		for (GestureFinger& g : sRightGesture) {
			if (g.finger != fingerId) continue;
			const float previousX = g.x;
			const float previousY = g.y;
			if (std::fabs(x - g.x) + std::fabs(y - g.y) > 3.0f) g.moved = true;
			g.x = x;
			g.y = y;
			if (sRightGesture[0].finger >= 0 && sRightGesture[1].finger >= 0) {
				const float distance = pinch_distance();
				if (sPinchDistance >= 0.0f && sLayoutH > 0) {
					// Separar los dedos acerca la cámara; juntarlos la aleja.
					pc_window_add_touch_zoom(-(distance - sPinchDistance) * 1.5f / float(sLayoutH));
					sRightGesture[0].moved = sRightGesture[1].moved = true;
				}
				sPinchDistance = distance;
			} else if (sGameMenuClaimFrames <= 0 && sLayoutH > 0) {
				if (pc_photo_mode_active()) {
					// Modo foto: el arrastre orienta la cámara libre en ambos ejes.
					pc_photo_mode_add_touch_look((x - previousX) / float(sLayoutH), (y - previousY) / float(sLayoutH));
				} else {
					// Un dedo en una zona libre funciona como un trackpad de cámara.
					pc_window_add_touch_camera_drag((x - previousX) / float(sLayoutH));
				}
			}
		}
		for (int i = 0; i < BTN_COUNT; ++i) {
			ButtonState& b = sButtons[i];
			if (b.finger != fingerId) continue;
			// Lanzar y silbar conservan el botón mientras el mismo pulgar apunta.
			if (i == BTN_THROW || i == BTN_WHISTLE) {
				pc_window_add_cursor_delta(x - b.lastX, y - b.lastY);
			}
			if (i == BTN_DISBAND && sLayoutH > 0) {
				float dx = x - sGroupDrag.ax, dy = y - sGroupDrag.ay;
				const float len = std::sqrt(dx * dx + dy * dy);
				if (len > kGroupDragStart * float(sLayoutH)) sGroupDrag.dragging = true;
				const float full = kGroupDragFull * float(sLayoutH);
				if (len > full && len > 0.0f) {
					dx *= full / len;
					dy *= full / len;
				}
				sGroupDrag.dx = dx;
				sGroupDrag.dy = dy;
			}
			b.lastX = x;
			b.lastY = y;
		}
		break;
	case PC_TOUCH_UP:
		// Los menús se manejan tocando lo que dibujan. Se encola antes de
		// liberar el dedo; si era un botón de la capa, su A/B también sirve.
		bool gestureMoved = false;
		for (SwipeFinger& s : sSwipes) {
			if (s.finger != fingerId) continue;
			s.finger = -1;
			const float dx = x - s.x0, dy = y - s.y0;
			if (sGameMenuClaimFrames > 0 && button_at(s.x0, s.y0) < 0
			    && std::fabs(dx) > kSwipeMinFraction * float(sLayoutW) && std::fabs(dx) > 2.0f * std::fabs(dy)) {
				// Deslizar hacia la izquierda avanza (R), hacia la derecha
				// retrocede (L), como pasar páginas.
				sSwipeButtons |= dx < 0.0f ? PAD_TRIGGER_R : PAD_TRIGGER_L;
				sSwipeHoldFrames = 2; // dos frames: que keyClick lo vea seguro
				gestureMoved = true;  // y que no cuente además como toque
			}
		}
		for (GestureFinger& g : sRightGesture) {
			if (g.finger != fingerId) continue;
			gestureMoved = g.moved;
			g.finger = -1;
			g.moved = false;
			sPinchDistance = -1.0f;
		}
		if (!gestureMoved)
		{
			pc_settings_touch_tap(x / float(sLayoutW), y / float(sLayoutH));
			sGameMenuTapX = x / float(sLayoutW);
			sGameMenuTapY = y / float(sLayoutH);
			sGameMenuTapPending = true;
			sGameMenuTapAge = 0;
		}
		if (fingerId == sStick.finger) {
			sStick.finger = -1;
			sStick.dx = sStick.dy = 0.0f;
		}
		if (fingerId == sColorTapFinger) sColorTapFinger = -1;
		for (int i = 0; i < BTN_COUNT; ++i) {
			if (sButtons[i].finger != fingerId) continue;
			sButtons[i].finger = -1;
			if (i == BTN_DISBAND) {
				// Sin arrastre fue un toque: ahora sí, X durante un frame.
				if (!sGroupDrag.dragging) sButtons[i].tapLatch = true;
				sGroupDrag.dragging = false;
				sGroupDrag.dx = sGroupDrag.dy = 0.0f;
			}
		}
		break;
	}
}

bool pc_touch_merge_pad(u16* button, s8* stickX, s8* stickY, s8* substickX, s8* substickY)
{
	if (sLayoutRequested) {
		sLayoutRequested = false;
		if (!sEditMode) edit_enter();
	}
	if (sEditMode) {
		// Editando: los dedos mueven botones, el juego no ve nada.
		(void)button; (void)stickX; (void)stickY; (void)substickX; (void)substickY;
		sSettingsRequested = false;
		const bool touched = sTouchedThisFrame;
		sTouchedThisFrame = false;
		return touched;
	}
	u16 touchButtons = 0;
	*button |= sQueuedGameButtons;
	touchButtons |= sQueuedGameButtons;
	sQueuedGameButtons = 0;
	if (sSwipeHoldFrames > 0) {
		*button |= sSwipeButtons;
		touchButtons |= sSwipeButtons;
		if (--sSwipeHoldFrames == 0) sSwipeButtons = 0;
	}
	for (int i = 0; i < BTN_COUNT; ++i) {
		ButtonState& b = sButtons[i];
		// Disolver no cuenta mientras se mantiene: puede acabar en arrastre.
		const bool held = (b.finger >= 0 && i != BTN_DISBAND) || b.tapLatch;
		b.tapLatch = false; // un toque breve vale exactamente un frame
		if (held && kButtons[i].pad) {
			*button |= kButtons[i].pad;
			touchButtons |= kButtons[i].pad;
		}
	}
	if (sButtons[BTN_DISBAND].finger >= 0 && sGroupDrag.dragging && sLayoutH > 0 && *substickX == 0 && *substickY == 0) {
		const float full = kGroupDragFull * float(sLayoutH);
		const float nx = sGroupDrag.dx / full, ny = -sGroupDrag.dy / full; // GC: Y hacia arriba
		*substickX = (s8)std::lround(std::fmax(-1.0f, std::fmin(1.0f, nx)) * 127.0f);
		*substickY = (s8)std::lround(std::fmax(-1.0f, std::fmin(1.0f, ny)) * 127.0f);
	}
	// Modo foto: el stick táctil mueve la cámara libre (y también el real,
	// que ya viene en stickX/Y). En ese modo el juego está en pausa y no lee
	// el stick, así que no hay conflicto.
	{
		float mx = 0.0f, my = 0.0f;
		if (sStick.finger >= 0 && sStick.radius > 0.0f) {
			mx = sStick.dx / sStick.radius;
			my = -sStick.dy / sStick.radius;
		} else {
			mx = float(*stickX) / 127.0f;
			my = float(*stickY) / 127.0f;
		}
		const float mag = std::sqrt(mx * mx + my * my);
		if (mag <= kStickDead) mx = my = 0.0f;
		pc_photo_mode_set_touch_move(std::fmax(-1.0f, std::fmin(1.0f, mx)), std::fmax(-1.0f, std::fmin(1.0f, my)));
	}
	if (sStick.finger >= 0 && sStick.radius > 0.0f && *stickX == 0 && *stickY == 0) {
		float nx = sStick.dx / sStick.radius, ny = -sStick.dy / sStick.radius; // GC: Y hacia arriba
		const float mag = std::sqrt(nx * nx + ny * ny);
		if (mag > kStickDead) {
			// Reescalar para que justo fuera de la zona muerta empiece a andar
			// y el borde del anillo sea correr.
			const float scaled = (mag - kStickDead) / (1.0f - kStickDead);
			nx *= scaled / mag;
			ny *= scaled / mag;
			*stickX = (s8)std::lround(std::fmax(-1.0f, std::fmin(1.0f, nx)) * 127.0f);
			*stickY = (s8)std::lround(std::fmax(-1.0f, std::fmin(1.0f, ny)) * 127.0f);
		}
		// El menú F1 no lee el PAD emulado del juego, así que además se le
		// entregan direcciones digitales con detección de flanco.
		if (nx < -0.55f) touchButtons |= PAD_BUTTON_LEFT;
		if (nx >  0.55f) touchButtons |= PAD_BUTTON_RIGHT;
		if (ny < -0.55f) touchButtons |= PAD_BUTTON_DOWN;
		if (ny >  0.55f) touchButtons |= PAD_BUTTON_UP;
	}
	const u16 menuEdges = touchButtons & ~sPreviousMenuButtons;
	if (menuEdges) pc_settings_touch_buttons(menuEdges);
	sPreviousMenuButtons = touchButtons;
	if (sSettingsRequested) {
		sSettingsRequested = false;
		pc_settings_request_toggle();
	}
	const bool touched = sTouchedThisFrame;
	sTouchedThisFrame = false;
	if (sGameMenuClaimFrames > 0) --sGameMenuClaimFrames;
	if (sPortMenuClaimFrames > 0) --sPortMenuClaimFrames;
	if (sGameMenuTapPending && ++sGameMenuTapAge > 12) sGameMenuTapPending = false;
	if (sColorIcon.age < 999) ++sColorIcon.age;
	return touched;
}

void pc_touch_mark_color_icon(float x0, float y0, float x1, float y1)
{
	sColorIcon.x0 = std::fmin(x0, x1);
	sColorIcon.x1 = std::fmax(x0, x1);
	sColorIcon.y0 = std::fmin(y0, y1);
	sColorIcon.y1 = std::fmax(y0, y1);
	sColorIcon.age = 0;
}

int pc_touch_take_color_taps(void)
{
	const int taps = sColorTaps;
	sColorTaps = 0;
	return taps;
}

bool pc_touch_has_icon_for_tag(char tag) { return sVisible && button_for_tag(tag) >= 0; }

void pc_touch_mark_inline_icon(char tag, float x, float y, float size)
{
	const int btn = button_for_tag(tag);
	if (btn < 0) return;
	float wx0 = 0, wy0 = 0, wx1 = 0, wy1 = 0;
	// Dos esquinas del hueco: así el tamaño en ventana sale de la misma
	// proyección (escala del pane y del blit incluidas). La `y` del texto es
	// la base de la letra y el glifo crece hacia arriba: el hueco va de y-size a y.
	if (!pc_gfx_project_current(x, y - size, 0.0f, &wx0, &wy0) || !pc_gfx_project_current(x + size, y, 0.0f, &wx1, &wy1)) return;
	InlineIcon icon;
	icon.button = btn;
	icon.cx = (wx0 + wx1) * 0.5f;
	icon.cy = (wy0 + wy1) * 0.5f;
	icon.size = std::fabs(wx1 - wx0);
	// El texto se dibuja a veces dos veces (sombra + relleno): un solo icono.
	for (const InlineIcon& other : sInlineIcons)
		if (other.button == icon.button && std::fabs(other.cx - icon.cx) < icon.size * 0.5f && std::fabs(other.cy - icon.cy) < icon.size * 0.5f) return;
	if (sInlineIcons.size() < 64) sInlineIcons.push_back(icon);
}

static void push_help_icon(char tag, float cx, float cy, float diam, float alpha)
{
	if (diam <= 0.0f || alpha <= 0.0f || sHelpIcons.size() >= 32) return;
	// Un pane se dibuja a veces por duplicado (sombra, máscara de color): un
	// solo icono por sitio.
	for (const HelpIcon& other : sHelpIcons)
		if (other.tag == tag && std::fabs(other.cx - cx) < diam * 0.25f && std::fabs(other.cy - cy) < diam * 0.25f) return;
	HelpIcon icon;
	icon.tag = tag;
	icon.cx = cx;
	icon.cy = cy;
	icon.diam = diam;
	icon.alpha = alpha;
	sHelpIcons.push_back(icon);
}

void pc_touch_mark_help_icon(char tag, float cx, float cy, float size)
{
	// La página de controles pasa el ancho del hueco del botón; la burbuja
	// sale algo mayor. 'L'/'R' son el gesto de deslizar, sin burbuja.
	push_help_icon(tag, cx, cy, (tag == 'L' || tag == 'R') ? size : size * 1.7f, 1.0f);
}

char pc_touch_button_tag_for_texture(const char* texName)
{
	if (!texName) return 0;
	// Solo el nombre de fichero, sin carpeta.
	const char* name = texName;
	for (const char* p = texName; *p; ++p)
		if (*p == '/' || *p == '\\') name = p + 1;
	struct Entry {
		const char* file;
		char tag;
	};
	// Texturas de botón de GameCube de screen/*_tex: los dibujos completos
	// (*_btn, a_40) y las siluetas que colorea cada pantalla (*_base).
	static const Entry kEntries[] = {
		{ "a_40.bti", 'a' },    { "a_btn.bti", 'a' },  { "a_base.bti", 'a' },  { "b_btn.bti", 'b' },   { "b_base.bti", 'b' },
		{ "x_btn.bti", 'x' },   { "x_base.bti", 'x' }, { "y_btn.bti", 'y' },   { "y_base.bti", 'y' },  { "z_btn.bti", 'z' },
		{ "z_base.bti", 'z' },  { "l_btn.bti", 'l' },  { "r_btn.bti", 'r' },   { "lr_base.bti", 'l' }, { "c_btn.bti", 'c' },
		{ "c_base.bti", 'c' },  { "3d_btn.bti", 's' }, { "3d_base.bti", 's' }, { "st_btn.bti", 'p' },  { "st_base.bti", 'p' },
	};
	for (const Entry& e : kEntries)
		if (strcmp(name, e.file) == 0) return e.tag;
	return 0;
}

void pc_touch_mark_button_icon(char tag, float cx, float cy, float size, float alpha)
{
	if (!sVisible) return;
	// Mismo hueco que el dibujo original, un poco más grande para que tape la
	// letra que algunas pantallas escriben encima de la silueta.
	push_help_icon(tag, cx, cy, size * 1.35f, alpha);
}

void pc_touch_set_visible(bool visible) { sVisible = visible; }
bool pc_touch_visible(void) { return sVisible; }

void pc_touch_claim_game_menu(void) { sGameMenuClaimFrames = 3; }

void pc_touch_claim_port_menu(void)
{
	sGameMenuClaimFrames = 3;
	sPortMenuClaimFrames = 3;
}

bool pc_touch_take_game_menu_tap(float* x, float* y)
{
	if (!sGameMenuTapPending) return false;
	if (x) *x = sGameMenuTapX;
	if (y) *y = sGameMenuTapY;
	sGameMenuTapPending = false;
	return true;
}

void pc_touch_queue_game_button(u16 button) { sQueuedGameButtons |= button; }

void pc_touch_init(void) { }

// ── Dibujo ───────────────────────────────────────────────────────────────────
void pc_touch_draw(void)
{
	// En juego la capa depende del último dispositivo usado. En un menú real
	// siempre se dibuja el único control permitido: B/atrás.
	if (!sVisible && sGameMenuClaimFrames <= 0) {
		sInlineIcons.clear();
		sHelpIcons.clear();
		return;
	}
	int w = 0, h = 0;
	pc_gfx_get_drawable_size(&w, &h);
	if (w <= 0 || h <= 0) return;
	layout(w, h);
	ensure_art();

	// Los iconos de botón que el juego ha registrado este frame piden su
	// botón en la capa (vale para el frame siguiente, cuando llegan los toques).
	sMenuWantedButtons = 0;
	for (const InlineIcon& icon : sInlineIcons) sMenuWantedButtons |= 1u << icon.button;
	for (const HelpIcon& icon : sHelpIcons) {
		const int btn = menu_button_for_help_tag(icon.tag);
		if (btn >= 0) sMenuWantedButtons |= 1u << btn;
	}

	pc_gfx_overlay_begin();
	auto sprite = [](unsigned tex, float cx, float cy, float size, float alpha) {
		if (!tex) return;
		pc_gfx_overlay_sprite(tex, cx - size * 0.5f, cy - size * 0.5f, size, size, 1.0f, 1.0f, 1.0f, alpha, 0.0f);
	};
	// Rectángulo relleno del color de la línea del arte (marrón oscuro).
	auto rect = [](float x, float y, float w, float h, float alpha, float angle = 0.0f) {
		if (!sArt.white) return;
		pc_gfx_overlay_sprite(sArt.white, x, y, w, h, 0.23f, 0.165f, 0.118f, alpha, angle);
	};
	if (sEditMode) {
		// Fondo oscurecido y, si toca, la cuadrícula a la que se pegan los botones.
		if (sArt.white) pc_gfx_overlay_sprite(sArt.white, 0, 0, (float)w, (float)h, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f);
		if (sEditGrid) {
			const float cell = (float)h / (float)kGridCells;
			for (float gx = 0; gx <= (float)w; gx += cell)
				if (sArt.white) pc_gfx_overlay_sprite(sArt.white, gx - 0.5f, 0, 1.0f, (float)h, 1, 1, 1, 0.25f, 0.0f);
			for (float gy = 0; gy <= (float)h; gy += cell)
				if (sArt.white) pc_gfx_overlay_sprite(sArt.white, 0, gy - 0.5f, (float)w, 1.0f, 1, 1, 1, 0.25f, 0.0f);
		}
	}
	for (int i = 0; i < BTN_COUNT; ++i) {
		if (sEditMode) {
			if (!button_editable(i)) continue;
		} else if (sGameMenuClaimFrames > 0) {
			if (!menu_button_allowed(i)) continue;
		} else if (i == BTN_BACK || i == BTN_MENU_CONFIRM) {
			continue;
		}
		const ButtonState& b = sButtons[i];
		const bool pressed = sEditMode ? i == sEditDragButton : b.finger >= 0;
		const float d = b.r * 2.0f;
		if (sEditMode && i == sEditSelected) {
			// Halo amarillo: es el botón al que afectan + y −.
			if (sArt.bubble) pc_gfx_overlay_sprite(sArt.bubble, b.cx - d * 0.62f, b.cy - d * 0.62f, d * 1.24f, d * 1.24f, 1.0f, 0.85f, 0.3f, 0.55f, 0.0f);
		}
		sprite(pressed ? sArt.bubblePressed : sArt.bubble, b.cx, b.cy, d, kLayerAlpha);
		// El glifo un poco más pequeño que la burbuja, y "hundido" al pulsar.
		sprite(sArt.glyph[i], b.cx, b.cy + (pressed ? d * 0.02f : 0.0f), d * (pressed ? 0.62f : 0.66f), kLayerAlpha);
	}
	if (sEditMode) {
		// Barra de herramientas: cuadrícula, −, +, restablecer. Los glifos se
		// pintan con rectángulos para no depender de más arte.
		for (int t = 0; t < TOOL_COUNT; ++t) {
			const ToolState& tool = sTools[t];
			const bool pressed = sEditToolPressed == t || (t == TOOL_GRID && sEditGrid);
			const float d = tool.r * 2.0f;
			sprite(pressed ? sArt.bubblePressed : sArt.bubble, tool.cx, tool.cy, d, 1.0f);
			const float g = d * 0.30f; // medio lado del glifo
			const float bar = std::fmax(2.0f, d * 0.07f);
			switch (t) {
			case TOOL_GRID:
				for (int k = -1; k <= 1; ++k) {
					rect(tool.cx + k * g * 0.66f - bar * 0.5f, tool.cy - g, bar, g * 2.0f, 1.0f);
					rect(tool.cx - g, tool.cy + k * g * 0.66f - bar * 0.5f, g * 2.0f, bar, 1.0f);
				}
				break;
			case TOOL_MINUS:
				rect(tool.cx - g, tool.cy - bar, g * 2.0f, bar * 2.0f, 1.0f);
				break;
			case TOOL_PLUS:
				rect(tool.cx - g, tool.cy - bar, g * 2.0f, bar * 2.0f, 1.0f);
				rect(tool.cx - bar, tool.cy - g, bar * 2.0f, g * 2.0f, 1.0f);
				break;
			case TOOL_RESET: {
				// "|<—": volver a la disposición por defecto.
				rect(tool.cx - g, tool.cy - g * 0.8f, bar * 2.0f, g * 1.6f, 1.0f);
				rect(tool.cx - g * 0.6f, tool.cy - bar, g * 1.6f, bar * 2.0f, 1.0f);
				// Punta: dos barras giradas 45° que parten del extremo izquierdo.
				const float head = g * 0.9f, tipX = tool.cx - g * 0.6f;
				rect(tipX + head * 0.35f - head * 0.5f, tool.cy - head * 0.35f - bar, head, bar * 2.0f, 1.0f, 0.785f);
				rect(tipX + head * 0.35f - head * 0.5f, tool.cy + head * 0.35f - bar, head, bar * 2.0f, 1.0f, -0.785f);
				break;
			}
			}
		}
	}
	if (sStick.finger >= 0) {
		sprite(sArt.ring, sStick.ax, sStick.ay, sStick.radius * 2.3f, 0.7f);
		sprite(sArt.knob, sStick.ax + sStick.dx, sStick.ay + sStick.dy, sStick.radius * 0.9f, 0.9f);
	}
	// Iconos en línea del texto: burbuja pequeña con el glifo, del alto de la
	// letra. Se registran al dibujar el texto y se consumen aquí.
	for (const InlineIcon& icon : sInlineIcons) {
		// Algo mayor que la letra (como los glifos de botón originales, que
		// eran de 32x28 sobre texto de 24) y opaco para que se lea.
		sprite(sArt.bubble, icon.cx, icon.cy, icon.size * 1.6f, 1.0f);
		sprite(sArt.glyph[icon.button], icon.cx, icon.cy, icon.size * 1.15f, 1.0f);
	}
	sInlineIcons.clear();
	// Página de controles: burbuja con el glifo del botón táctil, o el gesto
	// cuando no hay botón (pellizco, stick).
	for (const HelpIcon& icon : sHelpIcons) {
		const float a = icon.alpha;
		if (icon.tag == 'L' || icon.tag == 'R') {
			// Deslizar hacia la derecha va a la página anterior (L) y hacia
			// la izquierda a la siguiente (R): el dedo apunta hacia donde
			// hay que arrastrar.
			sprite(icon.tag == 'L' ? sArt.swipeR : sArt.swipeL, icon.cx, icon.cy, icon.diam, a);
			continue;
		}
		const float d = icon.diam;
		sprite(sArt.bubble, icon.cx, icon.cy, d, a);
		const int btn = icon.tag == 'p' ? BTN_PAUSE : button_for_tag(icon.tag == 'c' ? 'x' : icon.tag);
		if (btn >= 0) {
			sprite(sArt.glyph[btn], icon.cx, icon.cy, d * 0.66f, a);
		} else if (icon.tag == 'r') {
			sprite(sArt.pinch, icon.cx, icon.cy, d * 0.66f, a);
		} else if (icon.tag == 's') {
			sprite(sArt.ring, icon.cx, icon.cy, d * 0.80f, 0.9f * a);
			sprite(sArt.knob, icon.cx + d * 0.10f, icon.cy - d * 0.08f, d * 0.34f, a);
		}
	}
	sHelpIcons.clear();
	pc_gfx_overlay_end();
}
