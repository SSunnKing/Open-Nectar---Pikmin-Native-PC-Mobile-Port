#include "settings/pc_glass_menu.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

#include "Colour.h"
#include "Graphics.h"
#include "Matrix4f.h"
#include "Geometry.h"
#include "gl/pc_gfx.h"
#include "settings/pc_settings.h"
#include "settings/pc_settings_p2d.h"
#include "settings/pc_settings_rows.h"
#include "system.h"

namespace {

bool sOpen = false;
int sGroup = 0;
int sRowSel = 0;
int sScroll = 0; // primera fila visible
float sDragAccum = 0.0f; // arrastre táctil pendiente de convertir en filas
// Selector abierto desde una fila (p.ej. resoluciones): se apila sobre el
// grupo y B vuelve a él.
int sParentGroup = -1, sParentRowSel = 0, sParentScroll = 0;

// Geometría en el espacio 640x480 centrado (mapeo 4:3 sin barras).
// Misma zona que el panel de option.blo (yoko en 323,324; cristal 400x196
// en 127,231; placa de título 225x65 en 211,214), algo más alto para las
// filas con valor.
constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kListPanelX = 127, kListPanelY = 231, kListPanelW = 400, kListPanelH = 196;
constexpr int kTitleX = 238, kTitleY = 220, kTitleW = 164, kTitleH = 46;
constexpr int kListRowH = 24, kListVisible = 6;
// Ayuda de la fila seleccionada, en una placa bajo el cristal (dentro no cabe).
constexpr int kHelpX = kListPanelX, kHelpY = kListPanelY + kListPanelH + 3, kHelpW = kListPanelW, kHelpH = 44;
constexpr int kFontW = 12, kFontH = 17;
constexpr int kBigFontW = 26, kBigFontH = 30;

const Colour kSelDark(255, 150, 0, 255);
const Colour kText(205, 240, 255, 255);
const Colour kDim(150, 175, 200, 255);
const Colour kHelp(160, 185, 215, 255);
const Colour kOff(95, 115, 135, 255);     // fila desactivada
const Colour kSelOff(170, 120, 60, 255);  // fila desactivada y seleccionada

int textW(const char* t, int fw) { return pc_settings_p2d_text_width(t, fw); }

void textCentered(int cx, int y, const char* t, Colour c, int fw, int fh)
{
	pc_settings_p2d_text(cx - textW(t, fw) / 2, y, t, c, fw, fh);
}

int listCount() { return pc_settings_rows_count(sGroup); }

void clampScroll()
{
	const int n = listCount();
	if (sRowSel < 0) sRowSel = 0;
	if (sRowSel >= n) sRowSel = n > 0 ? n - 1 : 0;
	if (sRowSel < sScroll) sScroll = sRowSel;
	if (sRowSel >= sScroll + kListVisible) sScroll = sRowSel - kListVisible + 1;
	const int maxScroll = n > kListVisible ? n - kListVisible : 0;
	if (sScroll > maxScroll) sScroll = maxScroll;
	if (sScroll < 0) sScroll = 0;
}

// Rectángulos (para dibujo y toque) ------------------------------------------

struct Rect { int x, y, w, h; };

Rect listPanel() { return { kListPanelX, kListPanelY, kListPanelW, kListPanelH }; }
bool hasScrollbar() { return listCount() > kListVisible; }
Rect listRow(int visibleIndex)
{
	Rect p = listPanel();
	return { p.x + 20, p.y + 30 + visibleIndex * kListRowH, p.w - 40 - (hasScrollbar() ? 18 : 0), kListRowH };
}
Rect scrollTrack()
{
	Rect p = listPanel();
	return { p.x + p.w - 26, p.y + 30, 8, kListVisible * kListRowH };
}

// Toque en coordenadas 0..1 de la ventana -> espacio 640x480 centrado 4:3.
bool tapToPanelSpace(float tx, float ty, int* outX, int* outY)
{
	int dw = 0, dh = 0;
	pc_gfx_get_drawable_size(&dw, &dh);
	const float aspect = dh > 0 ? float(dw) / float(dh) : 4.0f / 3.0f;
	*outX = int(tx * aspect * 480.0f - (aspect * 480.0f - 640.0f) * 0.5f);
	*outY = int(ty * 480.0f);
	return true;
}
bool inRect(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// Entrada ----------------------------------------------------------------------

const char* listTitle() { return pc_settings_group_name(sGroup); }

void openPicker(int picker)
{
	sParentGroup  = sGroup;
	sParentRowSel = sRowSel;
	sParentScroll = sScroll;
	sGroup        = picker;
	sRowSel       = pc_settings_picker_current(picker);
	sScroll       = 0;
	clampScroll();
}

void closePicker()
{
	sGroup  = sParentGroup;
	sRowSel = sParentRowSel;
	sScroll = sParentScroll;
	sParentGroup = -1;
}

void inputList(const PcNavEdges& e)
{
	if (pc_settings_restart_prompt_active()) {
		if (e.ok) pc_settings_restart_prompt_answer(true);
		else if (e.cancel) pc_settings_restart_prompt_answer(false);
		return;
	}
	if (pc_settings_capture_active()) {
		// Captura de tecla/botón: la lista se queda quieta hasta que termina.
		pc_settings_capture_poll();
		return;
	}
	if (pc_settings_video_confirm_active()) {
		// Modal: A mantiene, B revierte, y al agotarse la cuenta se revierte.
		if (pc_settings_video_confirm_seconds_left() <= 0) pc_settings_video_confirm(false);
		else if (e.ok) pc_settings_video_confirm(true);
		else if (e.cancel) pc_settings_video_confirm(false);
		return;
	}
	bool left = e.left, right = e.right, ok = e.ok;
	if (e.tap) {
		int x, y;
		tapToPanelSpace(e.tapX, e.tapY, &x, &y);
		const int n = listCount();
		for (int v = 0; v < kListVisible && sScroll + v < n; v++)
			if (inRect(listRow(v), x, y)) { sRowSel = sScroll + v; right = true; }
		Rect track = scrollTrack();
		if (n > kListVisible && inRect({ track.x - 6, track.y, track.w + 12, track.h }, x, y)) {
			// Página arriba/abajo según el lado del pulgar tocado.
			const int thumbY = track.y + track.h * sScroll / n;
			sRowSel += y < thumbY ? -kListVisible : kListVisible;
			sScroll += y < thumbY ? -kListVisible : kListVisible;
		}
	}
	if (e.up) sRowSel--;
	if (e.down) sRowSel++;
	// Arrastre táctil: desplaza la lista (el dedo arrastra el contenido) y
	// mantiene la fila seleccionada dentro de lo visible.
	if (e.dragY != 0.0f && hasScrollbar()) {
		sDragAccum -= e.dragY;
		const int rows = int(sDragAccum / kListRowH);
		if (rows != 0) {
			sDragAccum -= rows * kListRowH;
			const int n = listCount();
			const int maxScroll = n - kListVisible;
			sScroll += rows;
			if (sScroll < 0) sScroll = 0;
			if (sScroll > maxScroll) sScroll = maxScroll;
			if (sRowSel < sScroll) sRowSel = sScroll;
			if (sRowSel >= sScroll + kListVisible) sRowSel = sScroll + kListVisible - 1;
		}
	}
	clampScroll();
	if (e.cancel) {
		if (sParentGroup >= 0) closePicker();
		else sOpen = false; // vuelve al panel "Advanced" del título
		return;
	}
	const int picker = pc_settings_row_opens_picker(sGroup, sRowSel);
	if (picker && (ok || right)) { openPicker(picker); return; }
	// Las filas desactivadas no cambian (la ayuda dice qué activar antes).
	if (!pc_settings_row_enabled(sGroup, sRowSel)) return;
	// Las acciones (packs, modelos, exportar, calibrar...) solo responden a A.
	if (pc_settings_row_is_action(sGroup, sRowSel)) {
		if (ok) pc_settings_row_change(sGroup, sRowSel, 0, true);
		return;
	}
	if (left) pc_settings_row_change(sGroup, sRowSel, -1, false);
	else if (right) pc_settings_row_change(sGroup, sRowSel, +1, false);
	else if (ok) pc_settings_row_change(sGroup, sRowSel, 0, true);
}

// Dibujo -----------------------------------------------------------------------

// Explicación de la fila seleccionada, partida en dos líneas si no cabe.
void drawRowHelp()
{
	const char* text = pc_settings_row_help(sGroup, sRowSel);
	if (!text || !text[0]) return;
	constexpr int fw = 10, fh = 14;
	const int maxW = kHelpW - 24;
	char l1[200], l2[200];
	l1[0] = l2[0] = '\0';
	if (textW(text, fw) <= maxW) {
		snprintf(l1, sizeof(l1), "%s", text);
	} else {
		size_t cut = 0;
		for (size_t i = 0; text[i] && i < sizeof(l1) - 1; i++) {
			if (text[i] != ' ') continue;
			snprintf(l1, sizeof(l1), "%.*s", (int)i, text);
			if (textW(l1, fw) > maxW) break;
			cut = i;
		}
		if (cut == 0) cut = strlen(text);
		snprintf(l1, sizeof(l1), "%.*s", (int)cut, text);
		snprintf(l2, sizeof(l2), "%s", text[cut] ? text + cut + 1 : "");
	}
	pc_settings_p2d_plate(kHelpX, kHelpY, kHelpW, kHelpH, 1);
	const bool on = pc_settings_row_enabled(sGroup, sRowSel);
	const Colour c = on ? kText : Colour(255, 190, 110, 255);
	textCentered(kHelpX + kHelpW / 2, kHelpY + (l2[0] ? 6 : 14), l1, c, fw, fh);
	if (l2[0]) textCentered(kHelpX + kHelpW / 2, kHelpY + 23, l2, c, fw, fh);
}

void drawList()
{
	Rect p = listPanel();
	pc_settings_p2d_plate(p.x, p.y, p.w, p.h, 1);
	// Placa de título como la de "Options" en option.blo.
	pc_settings_p2d_plate(kTitleX, kTitleY, kTitleW, kTitleH, 1);
	textCentered(kTitleX + kTitleW / 2, kTitleY + 10, listTitle(), kText, 22, 26);

	const int n = listCount();
	// Con el modal de confirmación no se pintan las filas: son panes P2D que
	// se volcarían encima del cristal del modal y se mezclarían con su texto.
	const bool modal = pc_settings_video_confirm_active() || pc_settings_restart_prompt_active();
	for (int v = 0; !modal && v < kListVisible && sScroll + v < n; v++) {
		const int i  = sScroll + v;
		Rect r       = listRow(v);
		const bool s = i == sRowSel;
		const bool on = pc_settings_row_enabled(sGroup, i);
		if (s) pc_settings_p2d_plate(r.x - 4, r.y, r.w + 8, r.h - 2, 2);
		char value[128];
		pc_settings_row_value(sGroup, i, value, sizeof(value));
		const Colour label = !on ? (s ? kSelOff : kOff) : (s ? kSelDark : kText);
		const Colour val   = !on ? (s ? kSelOff : kOff) : (s ? kSelDark : kDim);
		pc_settings_p2d_text(r.x + 6, r.y + 2, pc_settings_row_label(sGroup, i), label, kFontW, kFontH);
		pc_settings_p2d_text(r.x + r.w - 6 - textW(value, kFontW), r.y + 2, value, val, kFontW, kFontH);
	}
	if (hasScrollbar() && !modal) {
		// Carril + pulgar proporcional (visibles/total), como en una página web.
		Rect t = scrollTrack();
		// Estilo 1 (cristal): el 0 reajusta los márgenes de texto de la capa
		// P2D al ancho de la placa y con un carril estrecho anula el texto.
		pc_settings_p2d_plate(t.x, t.y, t.w, t.h, 1);
		const int thumbH = t.h * kListVisible / n < 16 ? 16 : t.h * kListVisible / n;
		const int thumbY = t.y + (t.h - thumbH) * sScroll / (n - kListVisible);
		pc_settings_p2d_plate(t.x, thumbY, t.w, thumbH, 2);
	}

	if (pc_settings_restart_prompt_active()) {
		const int mw = 340, mh = 84, mx = p.x + p.w / 2 - mw / 2, my = p.y + p.h / 2 - mh / 2;
		pc_gfx_blur_gx_rect(mx + 6, my + 6, mw - 12, mh - 12, 4);
		pc_settings_p2d_plate(mx, my, mw, mh, 1);
		textCentered(mx + mw / 2, my + 16, "Restart the game to apply?", Colour(255, 190, 28, 255), 13, 18);
		textCentered(mx + mw / 2, my + 46, "A: restart now      B: later", kText, 12, 17);
	} else if (pc_settings_video_confirm_active()) {
		// Modal encima de la lista: placa amarilla con la pregunta y la cuenta.
		const int mw = 340, mh = 84, mx = p.x + p.w / 2 - mw / 2, my = p.y + p.h / 2 - mh / 2;
		pc_gfx_blur_gx_rect(mx + 6, my + 6, mw - 12, mh - 12, 4);
		pc_settings_p2d_plate(mx, my, mw, mh, 1);
		char buf[64];
		snprintf(buf, sizeof(buf), "Keep these settings?  (%d s)", pc_settings_video_confirm_seconds_left());
		textCentered(mx + mw / 2, my + 16, buf, Colour(255, 190, 28, 255), 13, 18);
		textCentered(mx + mw / 2, my + 46, "A: keep      B: revert", kText, 12, 17);
	} else {
		char notice[256];
		bool isError = false;
		const bool bindings = sGroup == PC_SET_PICKER_KEYBOARD || sGroup == PC_SET_PICKER_GAMEPAD;
		const char* help = bindings ? "A: rebind    Left/Right: default    B/Esc: back"
		                 : (sGroup >= PC_SET_PICKER_RESOLUTION || pc_settings_row_is_action(sGroup, sRowSel))
		                     ? "A: select    Up/Down: move    B/Esc: back"
		                     : "Left/Right: change    A: open    B/Esc: back";
		if (pc_settings_notice(notice, sizeof(notice), &isError))
			textCentered(p.x + p.w / 2, p.y + p.h - 20, notice, isError ? Colour(255, 150, 140, 255) : Colour(160, 240, 180, 255), 10, 14);
		else
			textCentered(p.x + p.w / 2, p.y + p.h - 20, help, kHelp, 10, 14);
		drawRowHelp();
	}
}

} // namespace

void pc_glass_menu_open_list(int group)
{
	sGroup        = group;
	sRowSel       = 0;
	sScroll       = 0;
	sParentGroup  = -1;
	sOpen         = true;
}

void pc_glass_menu_close(void) { sOpen = false; }

bool pc_glass_menu_active(void) { return sOpen; }

void pc_glass_menu_input(void)
{
	if (!sOpen) return;
	const PcNavEdges e = pc_settings_read_nav_edges();
	inputList(e);
}

void pc_glass_menu_draw(void)
{
	if (!sOpen || !gsys || !gsys->mDGXGfx) return;
	DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
	// Mismo mapeo que el overlay F1: 4:3 centrado sin barras, oscurecido
	// completo detrás (en el título el cristal se lee mejor así).
	pc_gfx_set_menu_clip_43(0);
	pc_gfx_set_hud_wide(0);
	pc_gfx_set_ui_43_no_bars(0);
	// Se declara antes del frame: su destructor corre después del volcado P2D.
	struct Unbind { ~Unbind() { pc_gfx_set_ui_43_no_bars(0); } } unbind;
	PcSettingsP2DFrame frame(kScreenW, kScreenH);
	if (!pc_settings_p2d_active()) {
		// Sin la capa P2D (fuente/placas no cargadas) no hay menú de cristal.
		sOpen = false;
		return;
	}
	// Sin oscurecido (es un apartado del título), pero con el fondo
	// desenfocado bajo el cristal para que texto y barra se lean.
	pc_gfx_set_ui_43_no_bars(1);
	pc_gfx_blur_gx_rect(kListPanelX + 6, kListPanelY + 6, kListPanelW - 12, kListPanelH - 12, 3);
	pc_gfx_blur_gx_rect(kTitleX + 8, kTitleY + 8, kTitleW - 16, kTitleH - 16, 3);
	pc_gfx_blur_gx_rect(kHelpX + 6, kHelpY + 6, kHelpW - 12, kHelpH - 12, 3);
	Matrix4f ortho;
	gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, kScreenW, kScreenH));
	drawList();
}
