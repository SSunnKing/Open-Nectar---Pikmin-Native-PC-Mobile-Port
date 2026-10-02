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
int sGroup = 0;  // pestaña (PcSettingsGroup) o selector abierto desde una fila
int sRowSel = 0;
int sScroll = 0; // primera entrada visible de la columna (cuenta cabeceras)
float sDragAccum = 0.0f; // arrastre táctil pendiente de convertir en filas
// Selector abierto desde una fila (p.ej. resoluciones): se apila sobre la
// pestaña y B vuelve a ella.
int sParentGroup = -1, sParentRowSel = 0, sParentScroll = 0;
// Cursor en la columna de opciones: A en una fila con lista entra, arriba/
// abajo recorre, A aplica la marcada y B vuelve a las filas.
bool sOptFocus = false;
int sOptSel = 0;

// Geometría en el espacio 640x480 centrado (mapeo 4:3 sin barras). Mismo
// formato que F1 con placas de cristal sueltas: barra de pestañas, filas
// del grupo a la izquierda, opciones y explicación a la derecha, y pie.
// Ocupa el sitio del panel de option.blo, que el título oculta mientras
// este menú está abierto.
constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kTabBarX = 40, kTabBarY = 158, kTabBarW = 560, kTabBarH = 34;
constexpr int kCloseW = 26;
constexpr int kTabX = kTabBarX + 12;
constexpr int kTabW = (kTabBarW - 24 - kCloseW) / PC_SET_GROUP_COUNT;
constexpr int kColY = kTabBarY + kTabBarH + 6, kColH = 234;
constexpr int kLeftX = kTabBarX, kLeftW = 322;
constexpr int kRightX = kLeftX + kLeftW + 8, kRightW = kTabBarX + kTabBarW - kRightX;
constexpr int kFootX = kTabBarX, kFootY = kColY + kColH + 6, kFootW = kTabBarW, kFootH = 28;
constexpr int kItemH = 22;
constexpr int kVisible = (kColH - 16) / kItemH; // cabeceras y filas visibles a la vez
constexpr int kOptH = 22, kOptVisible = 6;
constexpr int kFontW = 11, kFontH = 16;

const Colour kSelDark(255, 150, 0, 255);
const Colour kText(205, 240, 255, 255);
const Colour kDim(150, 175, 200, 255);
const Colour kHelp(160, 185, 215, 255);
const Colour kOff(95, 115, 135, 255);     // fila desactivada
const Colour kSelOff(170, 120, 60, 255);  // fila desactivada y seleccionada
const Colour kSection(255, 207, 75, 255); // cabecera de sección

int textW(const char* t, int fw) { return pc_settings_p2d_text_width(t, fw); }

void textCentered(int cx, int y, const char* t, Colour c, int fw, int fh)
{
	pc_settings_p2d_text(cx - textW(t, fw) / 2, y, t, c, fw, fh);
}

bool inPicker() { return sGroup >= PC_SET_PICKER_RESOLUTION; }
// Pestaña resaltada: la propia o, dentro de un selector, la que lo abrió.
int activeTab() { return inPicker() ? sParentGroup : sGroup; }
int listCount() { return pc_settings_rows_count(sGroup); }

// Entradas de la columna: cabecera de sección (-1 - fila a la que precede)
// o fila (>= 0). Los selectores no llevan cabeceras.
int items(int* out, int max)
{
	int k = 0;
	const int n = listCount();
	for (int r = 0; r < n && k < max; r++) {
		if (!inPicker() && pc_settings_row_section(sGroup, r) && k < max) out[k++] = -1 - r;
		if (k < max) out[k++] = r;
	}
	return k;
}
constexpr int kMaxItems = 160;

void clampScroll()
{
	const int n = listCount();
	if (sRowSel < 0) sRowSel = 0;
	if (sRowSel >= n) sRowSel = n > 0 ? n - 1 : 0;
	int it[kMaxItems];
	const int count = items(it, kMaxItems);
	int at = 0;
	for (int k = 0; k < count; k++) if (it[k] == sRowSel) { at = k; break; }
	// Con la cabecera de su sección si la fila la encabeza.
	const int top = (at > 0 && it[at - 1] == -1 - sRowSel) ? at - 1 : at;
	if (top < sScroll) sScroll = top;
	if (at >= sScroll + kVisible) sScroll = at - kVisible + 1;
	const int maxScroll = count > kVisible ? count - kVisible : 0;
	if (sScroll > maxScroll) sScroll = maxScroll;
	if (sScroll < 0) sScroll = 0;
}

// Opciones de la fila en la columna derecha: ventana alrededor de la actual.
struct OptWindow { int count, current, first, shown; };
OptWindow optWindow()
{
	OptWindow w = {};
	w.current = -1;
	w.count = inPicker() ? 0 : pc_settings_row_options(sGroup, sRowSel, &w.current);
	if (w.count <= 0 || w.current < 0) { w.count = 0; w.current = -1; return w; }
	if (sOptFocus && sOptSel >= w.count) sOptSel = w.count - 1;
	const int center = sOptFocus ? sOptSel : w.current;
	w.shown = w.count < kOptVisible ? w.count : kOptVisible;
	w.first = center - w.shown / 2;
	if (w.first > w.count - w.shown) w.first = w.count - w.shown;
	if (w.first < 0) w.first = 0;
	return w;
}

// Rectángulos (para dibujo y toque) ------------------------------------------

struct Rect { int x, y, w, h; };

bool hasScrollbar()
{
	int it[kMaxItems];
	return items(it, kMaxItems) > kVisible;
}
Rect listRow(int visibleIndex)
{
	return { kLeftX + 12, kColY + 8 + visibleIndex * kItemH, kLeftW - 24 - (hasScrollbar() ? 16 : 0), kItemH };
}
Rect scrollTrack() { return { kLeftX + kLeftW - 22, kColY + 8, 8, kVisible * kItemH }; }
Rect optRow(int visibleIndex) { return { kRightX + 12, kColY + 8 + visibleIndex * kOptH, kRightW - 24, kOptH }; }
Rect tabRect(int tab) { return { kTabX + tab * kTabW, kTabBarY + 4, kTabW, kTabBarH - 8 }; }
Rect closeRect() { return { kTabBarX + kTabBarW - 12 - kCloseW, kTabBarY + 4, kCloseW, kTabBarH - 8 }; }

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

void openTab(int tab)
{
	if (inPicker()) sParentGroup = -1;
	sOptFocus = false;
	sGroup  = tab;
	sRowSel = 0;
	sScroll = 0;
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
	if (sOptFocus && !e.tabPrev && !e.tabNext && !e.tap) {
		const OptWindow w = optWindow();
		if (w.count > 0) {
			if (e.cancel || e.left) sOptFocus = false;
			else if (e.up) sOptSel = (sOptSel + w.count - 1) % w.count;
			else if (e.down) sOptSel = (sOptSel + 1) % w.count;
			else if (e.ok) {
				pc_settings_row_pick_option(sGroup, sRowSel, sOptSel);
				sOptFocus = false;
			}
			return;
		}
		sOptFocus = false;
	}
	if (e.tabPrev || e.tabNext) {
		const int tab = activeTab();
		openTab((tab + (e.tabNext ? 1 : -1) + PC_SET_GROUP_COUNT) % PC_SET_GROUP_COUNT);
		return;
	}
	bool left = e.left, right = e.right, ok = e.ok;
	if (e.tap) {
		int x, y;
		tapToPanelSpace(e.tapX, e.tapY, &x, &y);
		if (inRect(closeRect(), x, y)) { sOpen = false; return; } // vuelve al panel "Advanced"
		for (int t = 0; t < PC_SET_GROUP_COUNT; t++)
			if (inRect(tabRect(t), x, y)) { if (t != activeTab() || inPicker()) openTab(t); return; }
		int it[kMaxItems];
		const int count = items(it, kMaxItems);
		for (int v = 0; v < kVisible && sScroll + v < count; v++) {
			if (!inRect(listRow(v), x, y) || it[sScroll + v] < 0) continue;
			// Tocar una fila la selecciona; tocar la ya seleccionada es A.
			if (it[sScroll + v] == sRowSel && !sOptFocus) ok = true;
			else { sRowSel = it[sScroll + v]; sOptFocus = false; clampScroll(); return; }
		}
		const OptWindow w = optWindow();
		for (int v = 0; v < w.shown; v++)
			if (inRect(optRow(v), x, y)) {
				pc_settings_row_pick_option(sGroup, sRowSel, w.first + v);
				sOptFocus = false;
				return;
			}
		// Filas sin lista: la placa de su valor hace de botón.
		if (w.count == 0 && inRect(optRow(0), x, y)) ok = true;
		Rect track = scrollTrack();
		if (count > kVisible && inRect({ track.x - 6, track.y, track.w + 12, track.h }, x, y)) {
			// Página arriba/abajo según el lado del pulgar tocado.
			const int thumbY = track.y + track.h * sScroll / count;
			sScroll += y < thumbY ? -kVisible : kVisible;
			const int maxScroll = count - kVisible;
			if (sScroll < 0) sScroll = 0;
			if (sScroll > maxScroll) sScroll = maxScroll;
			// La selección salta a la primera fila visible.
			for (int v = 0; v < kVisible && sScroll + v < count; v++)
				if (it[sScroll + v] >= 0) { sRowSel = it[sScroll + v]; break; }
			return;
		}
	}
	const int n = listCount();
	if (n > 0) {
		if (e.up) { sRowSel = (sRowSel + n - 1) % n; clampScroll(); }
		if (e.down) { sRowSel = (sRowSel + 1) % n; clampScroll(); }
	}
	// Arrastre táctil: desplaza la lista (el dedo arrastra el contenido) y
	// mantiene la fila seleccionada dentro de lo visible.
	if (e.dragY != 0.0f && hasScrollbar()) {
		sDragAccum -= e.dragY;
		const int rows = int(sDragAccum / kItemH);
		if (rows != 0) {
			sDragAccum -= rows * kItemH;
			int it[kMaxItems];
			const int count = items(it, kMaxItems);
			const int maxScroll = count - kVisible;
			sScroll += rows;
			if (sScroll < 0) sScroll = 0;
			if (sScroll > maxScroll) sScroll = maxScroll;
			int firstRow = -1, lastRow = -1;
			for (int v = 0; v < kVisible && sScroll + v < count; v++)
				if (it[sScroll + v] >= 0) { if (firstRow < 0) firstRow = it[sScroll + v]; lastRow = it[sScroll + v]; }
			if (firstRow >= 0 && sRowSel < firstRow) sRowSel = firstRow;
			if (lastRow >= 0 && sRowSel > lastRow) sRowSel = lastRow;
		}
	}
	if (e.cancel) {
		if (sParentGroup >= 0) closePicker();
		else sOpen = false; // vuelve al panel "Advanced" del título
		return;
	}
	if (e.up || e.down || n <= 0) return;
	if (optWindow().count > 0) {
		// Filas con lista de opciones: derecha (o A) lleva el cursor a la
		// columna derecha; izquierda no hace nada aquí, es la que vuelve.
		if (ok || right) {
			sOptFocus = true;
			sOptSel   = optWindow().current;
		}
		return;
	}
	const int picker = pc_settings_row_opens_picker(sGroup, sRowSel);
	// La resolución se elige en la columna derecha; izquierda/derecha salta
	// a la contigua. Los demás selectores se abren con A o derecha.
	if (picker && picker != PC_SET_PICKER_RESOLUTION && (ok || right)) { openPicker(picker); return; }
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

// Texto partido por palabras en el ancho w; para en maxY.
void textWrapped(int x, int y, int w, const char* text, Colour c, int fw, int fh, int maxY)
{
	char line[256] = "";
	size_t len = 0;
	const char* p = text ? text : "";
	while (*p && y + fh <= maxY) {
		if (*p == '\n') {
			// Salto de línea explícito (p. ej. la ayuda de los logros).
			if (len) pc_settings_p2d_text(x, y, line, c, fw, fh);
			y += len ? fh + 2 : (fh + 2) / 2;
			len = 0;
			p++;
			continue;
		}
		const char* end = p;
		while (*end && *end != ' ' && *end != '\n') end++;
		char trial[256];
		snprintf(trial, sizeof(trial), "%.*s%s%.*s", (int)len, line, len ? " " : "", (int)(end - p), p);
		if (len && textW(trial, fw) > w) {
			pc_settings_p2d_text(x, y, line, c, fw, fh);
			y += fh + 2;
			len = 0;
			continue; // la misma palabra abre la línea siguiente
		}
		snprintf(line, sizeof(line), "%s", trial);
		len = strlen(line);
		p = *end == ' ' ? end + 1 : end;
	}
	if (len && y + fh <= maxY) pc_settings_p2d_text(x, y, line, c, fw, fh);
}

void drawTabs()
{
	pc_settings_p2d_plate(kTabBarX, kTabBarY, kTabBarW, kTabBarH, 1);
	const int active = activeTab();
	for (int t = 0; t < PC_SET_GROUP_COUNT; t++) {
		const Rect r = tabRect(t);
		const bool on = t == active;
		if (on) pc_settings_p2d_plate(r.x + 2, r.y, r.w - 4, r.h, 2);
		textCentered(r.x + r.w / 2, r.y + 3, pc_settings_group_name(t), on ? kSelDark : kDim, 10, 15);
	}
	const Rect c = closeRect();
	textCentered(c.x + c.w / 2, c.y + 3, "X", kDim, 11, 16);
}

void drawRows()
{
	pc_settings_p2d_plate(kLeftX, kColY, kLeftW, kColH, 1);
	int it[kMaxItems];
	const int count = items(it, kMaxItems);
	for (int v = 0; v < kVisible && sScroll + v < count; v++) {
		const int item = it[sScroll + v];
		const Rect r   = listRow(v);
		if (item < 0) {
			pc_settings_p2d_text(r.x + 2, r.y + 6, pc_settings_row_section(sGroup, -1 - item), kSection, 9, 13);
			continue;
		}
		const bool s  = item == sRowSel;
		const bool on = pc_settings_row_enabled(sGroup, item);
		if (s) pc_settings_p2d_plate(r.x - 4, r.y, r.w + 8, r.h - 2, sOptFocus ? 1 : 2);
		char value[128];
		pc_settings_row_value(sGroup, item, value, sizeof(value));
		const Colour label = !on ? (s ? kSelOff : kOff) : (s ? kSelDark : kText);
		const Colour val   = !on ? (s ? kSelOff : kOff) : (s ? kSelDark : kDim);
		const char* name = pc_settings_row_label(sGroup, item);
		// Valores largos (p. ej. la resolución) se encogen para no pisar la etiqueta.
		const int room = r.w - 12 - textW(name, kFontW) - 12;
		int fw = kFontW;
		while (fw > 7 && textW(value, fw) > room) fw--;
		// Etiquetas largas (títulos de logros): se encogen y, si aún no caben,
		// se recortan con "..." para no pisar el valor ni el panel derecho.
		const int labelRoom = r.w - 12 - (value[0] ? textW(value, fw) + 12 : 0);
		int lw = kFontW;
		while (lw > 7 && textW(name, lw) > labelRoom) lw--;
		char shortName[256];
		snprintf(shortName, sizeof(shortName), "%s", name);
		if (textW(shortName, lw) > labelRoom) {
			size_t n = strlen(shortName);
			while (n > 0) {
				snprintf(shortName, sizeof(shortName), "%.*s...", (int)--n, name);
				if (textW(shortName, lw) <= labelRoom) break;
			}
		}
		pc_settings_p2d_text(r.x + 6, r.y + 2 + (kFontW - lw), shortName, label, lw, kFontH * lw / kFontW);
		pc_settings_p2d_text(r.x + r.w - 6 - textW(value, fw), r.y + 2 + (kFontW - fw), value, val, fw, kFontH * fw / kFontW);
	}
	if (count > kVisible) {
		// Carril + pulgar proporcional (visibles/total), como en una página web.
		Rect t = scrollTrack();
		// Estilo 1 (cristal): el 0 reajusta los márgenes de texto de la capa
		// P2D al ancho de la placa y con un carril estrecho anula el texto.
		pc_settings_p2d_plate(t.x, t.y, t.w, t.h, 1);
		const int thumbH = t.h * kVisible / count < 16 ? 16 : t.h * kVisible / count;
		const int thumbY = t.y + (t.h - thumbH) * sScroll / (count - kVisible);
		pc_settings_p2d_plate(t.x, thumbY, t.w, thumbH, 2);
	}
}

void drawOptions()
{
	pc_settings_p2d_plate(kRightX, kColY, kRightW, kColH, 1);
	const int n = listCount();
	if (n <= 0) return;
	int y = kColY + 8;
	const OptWindow w = optWindow();
	if (w.count > 0) {
		// Sin cursor aquí se marca la opción actual; con cursor, la placa lo
		// sigue y la actual queda en dorado.
		const int mark = sOptFocus ? sOptSel : w.current;
		for (int v = 0; v < w.shown; v++) {
			const Rect r   = optRow(v);
			const bool sel = w.first + v == mark;
			const bool cur = w.first + v == w.current;
			if (sel) pc_settings_p2d_plate(r.x - 4, r.y, r.w + 8, r.h - 2, 2);
			textCentered(r.x + r.w / 2, r.y + 3, pc_settings_row_option(w.first + v),
			             sel ? kSelDark : cur ? kSection : kText, 10, 15);
		}
		y += w.shown * kOptH;
		if (w.count > w.shown) {
			char pos[32];
			snprintf(pos, sizeof(pos), "%d / %d", mark + 1, w.count);
			textCentered(kRightX + kRightW / 2, y + 2, pos, kDim, 9, 13);
			y += 16;
		}
	} else {
		// Acción, selector o fila desactivada: solo su valor, que hace de botón.
		const Rect r = optRow(0);
		char value[128];
		pc_settings_row_value(sGroup, sRowSel, value, sizeof(value));
		const bool on = pc_settings_row_enabled(sGroup, sRowSel);
		pc_settings_p2d_plate(r.x - 4, r.y, r.w + 8, r.h - 2, on ? 2 : 1);
		textCentered(r.x + r.w / 2, r.y + 3, value[0] ? value : pc_settings_row_label(sGroup, sRowSel),
		             on ? kSelDark : kOff, 10, 15);
		y += kOptH;
	}
	// Explicación de la fila; si está desactivada dice qué activar antes.
	const bool on = pc_settings_row_enabled(sGroup, sRowSel);
	textWrapped(kRightX + 14, y + 10, kRightW - 28, pc_settings_row_help(sGroup, sRowSel),
	            on ? kHelp : Colour(255, 190, 110, 255), 10, 14, kColY + kColH - 6);
}

void drawFooter()
{
	pc_settings_p2d_plate(kFootX, kFootY, kFootW, kFootH, 1);
	char notice[256];
	bool isError = false;
	const int cx = kFootX + kFootW / 2, y = kFootY + 7;
	if (pc_settings_notice(notice, sizeof(notice), &isError)) {
		textCentered(cx, y, notice, isError ? Colour(255, 150, 140, 255) : Colour(160, 240, 180, 255), 10, 14);
		return;
	}
	const bool bindings = sGroup == PC_SET_PICKER_KEYBOARD || sGroup == PC_SET_PICKER_GAMEPAD;
	const char* help = sOptFocus ? "Up/Down: choose    A: apply    Left/B: back"
	                 : bindings ? "A: rebind    Left/Right: default    B/Esc: back"
	                 : inPicker() ? "L/R: tab    A: select    Up/Down: move    B/Esc: back"
	                 : pc_settings_row_is_action(sGroup, sRowSel)
	                     ? "L/R: tab    A: select    Up/Down: move    B/Esc: back"
	                     : "L/R: tab    Right: options    Up/Down: move    B/Esc: back";
	textCentered(cx, y, help, kHelp, 10, 14);
}

void drawModal()
{
	const int mw = 340, mh = 84, mx = kScreenW / 2 - mw / 2, my = kColY + kColH / 2 - mh / 2;
	pc_gfx_blur_gx_rect(mx + 6, my + 6, mw - 12, mh - 12, 4);
	pc_settings_p2d_plate(mx, my, mw, mh, 1);
	if (pc_settings_restart_prompt_active()) {
		textCentered(mx + mw / 2, my + 16, "Restart the game to apply?", Colour(255, 190, 28, 255), 13, 18);
		textCentered(mx + mw / 2, my + 46, "A: restart now      B: later", kText, 12, 17);
	} else {
		char buf[64];
		snprintf(buf, sizeof(buf), "Keep these settings?  (%d s)", pc_settings_video_confirm_seconds_left());
		textCentered(mx + mw / 2, my + 16, buf, Colour(255, 190, 28, 255), 13, 18);
		textCentered(mx + mw / 2, my + 46, "A: keep      B: revert", kText, 12, 17);
	}
}

void drawPage()
{
	drawTabs();
	// Con el modal no se pintan las filas: son panes P2D que se volcarían
	// encima del cristal del modal y se mezclarían con su texto.
	if (pc_settings_video_confirm_active() || pc_settings_restart_prompt_active()) {
		drawModal();
		return;
	}
	clampScroll();
	drawRows();
	drawOptions();
	drawFooter();
}

} // namespace

void pc_glass_menu_open_list(int group)
{
	sGroup        = group;
	sRowSel       = 0;
	sScroll       = 0;
	sParentGroup  = -1;
	sOptFocus     = false;
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
	pc_gfx_blur_gx_rect(kTabBarX + 6, kTabBarY + 6, kTabBarW - 12, kTabBarH - 12, 3);
	pc_gfx_blur_gx_rect(kLeftX + 6, kColY + 6, kLeftW - 12, kColH - 12, 3);
	pc_gfx_blur_gx_rect(kRightX + 6, kColY + 6, kRightW - 12, kColH - 12, 3);
	pc_gfx_blur_gx_rect(kFootX + 6, kFootY + 6, kFootW - 12, kFootH - 12, 3);
	Matrix4f ortho;
	gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, kScreenW, kScreenH));
	drawPage();
}
