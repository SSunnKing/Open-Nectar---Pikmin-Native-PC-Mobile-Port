#include "app.h"

#include "launcher_platform.h"
#include "obj_import.h"

#include "nectar_assets/raster.h"
#include "nectar_assets/image.h"
#include "nectar_assets/mesh_mod.h"
#include "nectar_mapcore/pack.h"
#include "stb_image.h"
#include "nectar_mapcore/map.h"

#include "pc_vs_default_map.h" // la arena de serie (maps/vs/impact_arena/map.json)

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <queue>
#include <set>
#include <sstream>

#include <SDL.h>

#include "nectar_mapcore/i18n.h"

namespace nm {

using nectar::tr;

namespace fs = std::filesystem;
using nectar::MapDoc;

namespace {

constexpr double kAutosaveSeconds = 120.0;
constexpr float kTopBarH = 64.0f, kStatusBarH = 30.0f, kRailW = 60.0f, kToolRowH = 46.0f, kDefaultToolboxW = 300.0f, kDefaultSideW = 360.0f;
constexpr float kMinToolboxW = 240.0f, kMinSideW = 280.0f, kMinViewportW = 420.0f;

const ImVec4 kGood(0.57f, 0.84f, 0.37f, 1), kWarn(0.96f, 0.76f, 0.28f, 1), kBad(0.96f, 0.40f, 0.30f, 1), kDim(0.59f, 0.62f, 0.56f, 1);

double now() { return double(SDL_GetTicks64()) / 1000.0; }

bool readText(const std::string& path, std::string& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool writeText(const std::string& path, const std::string& text)
{
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary);
		if (!f) return false;
		f << text;
		if (!f) return false;
	}
	fs::rename(tmp, path, ec);
	return !ec;
}

void copyStr(char* dst, std::size_t n, const std::string& s) { std::snprintf(dst, n, "%s", s.c_str()); }

/// Nombre de carpeta a partir del nombre del mapa: minúsculas, sin espacios.
std::string slug(const std::string& name)
{
	std::string out;
	for (unsigned char c : name) {
		if (std::isalnum(c)) out += char(std::tolower(c));
		else if ((c == ' ' || c == '-' || c == '_') && !out.empty() && out.back() != '_') out += '_';
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out.empty() ? "mi_mapa" : out.substr(0, 48);
}

/// Clave sencilla para búsquedas de interfaz: minúsculas y vocales españolas
/// sin tilde, de modo que "decoracion" encuentre "Decoración".
std::string searchKey(std::string text)
{
	const std::pair<const char*, const char*> folds[] = { { "á", "a" }, { "é", "e" }, { "í", "i" }, { "ó", "o" }, { "ú", "u" }, { "ü", "u" }, { "ñ", "n" },
	                                                       { "Á", "a" }, { "É", "e" }, { "Í", "i" }, { "Ó", "o" }, { "Ú", "u" }, { "Ü", "u" }, { "Ñ", "n" } };
	for (const auto& fold : folds) {
		std::size_t at = 0;
		while ((at = text.find(fold.first, at)) != std::string::npos) {
			text.replace(at, std::strlen(fold.first), fold.second);
			at += std::strlen(fold.second);
		}
	}
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return text;
}

std::map<int, std::vector<int>> routeGraph(const nectar::Routes& routes)
{
	std::map<int, std::vector<int>> graph;
	for (const nectar::RoutePoint& p : routes.points) graph[p.index];
	for (const auto& link : routes.links) {
		graph[link.first].push_back(link.second);
		graph[link.second].push_back(link.first);
	}
	return graph;
}

int nearestRoutePoint(const nectar::Routes& routes, double x, double z)
{
	int best = -1;
	double bestDistance = 1e30;
	for (const nectar::RoutePoint& p : routes.points) {
		const double dx = p.pos.x - x, dz = p.pos.z - z, distance = dx * dx + dz * dz;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = p.index;
		}
	}
	return best;
}

std::map<int, int> routeComponents(const nectar::Routes& routes)
{
	const auto graph = routeGraph(routes);
	std::map<int, int> component;
	int next = 0;
	for (const auto& node : graph) {
		if (component.count(node.first)) continue;
		std::queue<int> pending;
		pending.push(node.first);
		component[node.first] = next;
		while (!pending.empty()) {
			const int at = pending.front();
			pending.pop();
			const auto found = graph.find(at);
			if (found == graph.end()) continue;
			for (int other : found->second) {
				if (component.emplace(other, next).second) pending.push(other);
			}
		}
		next++;
	}
	return component;
}

std::vector<int> routePath(const nectar::Routes& routes, int start, int goal)
{
	if (start < 0 || goal < 0) return {};
	const auto graph = routeGraph(routes);
	std::map<int, int> previous;
	std::queue<int> pending;
	previous[start] = start;
	pending.push(start);
	while (!pending.empty() && !previous.count(goal)) {
		const int at = pending.front();
		pending.pop();
		const auto found = graph.find(at);
		if (found == graph.end()) continue;
		for (int other : found->second) {
			if (previous.emplace(other, at).second) pending.push(other);
		}
	}
	if (!previous.count(goal)) return {};
	std::vector<int> path;
	for (int at = goal;; at = previous[at]) {
		path.push_back(at);
		if (at == start) break;
	}
	std::reverse(path.begin(), path.end());
	return path;
}

/// Texto gris y con salto de línea: las explicaciones.
void hint(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, kDim);
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

/// Botón que ocupa todo el ancho.
bool wideButton(const char* label, float h = 36.0f) { return ImGui::Button(label, ImVec2(-1, h)); }

/// Botón que se queda encendido cuando `on`.
bool choiceButton(const char* label, bool on, ImVec2 size = ImVec2(0, 0))
{
	if (on) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.38f, 0.18f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.49f, 0.22f, 1));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 0.88f, 1));
	}
	const bool pressed = ImGui::Button(label, size);
	if (on) ImGui::PopStyleColor(3);
	return pressed;
}

/// Emblema vegetal sencillo, dibujado a mano para no depender de recursos
/// externos. Se usa como firma visual en la barra superior.
void drawSprout(ImDrawList* dl, ImVec2 c, float scale)
{
	const ImU32 stem = IM_COL32(170, 196, 74, 255);
	const ImU32 leaf = IM_COL32(103, 166, 67, 255);
	const ImU32 glow = IM_COL32(225, 218, 111, 255);
	dl->AddCircleFilled(ImVec2(c.x, c.y + 8 * scale), 13 * scale, IM_COL32(48, 75, 35, 255));
	dl->AddLine(ImVec2(c.x, c.y + 7 * scale), ImVec2(c.x, c.y - 12 * scale), stem, 3.0f * scale);
	dl->AddEllipseFilled(ImVec2(c.x - 7 * scale, c.y - 13 * scale), ImVec2(10 * scale, 5 * scale), leaf, -0.55f);
	dl->AddEllipseFilled(ImVec2(c.x + 7 * scale, c.y - 9 * scale), ImVec2(9 * scale, 4.5f * scale), glow, 0.45f);
}

/// Cabecera compacta para los paneles fijos del editor.
void panelHeading(const char* title, const char* subtitle)
{
	const ImVec2 p = ImGui::GetCursorScreenPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float titleW = ImGui::CalcTextSize(title).x;
	dl->AddCircleFilled(ImVec2(p.x + 5, p.y + 9), 3.5f, IM_COL32(166, 190, 70, 255));
	dl->AddLine(ImVec2(p.x + 26 + titleW, p.y + 9), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 9), IM_COL32(67, 76, 67, 220), 1.0f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18);
	ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.84f, 1), "%s", title);
	if (subtitle && *subtitle) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18);
		ImGui::PushStyleColor(ImGuiCol_Text, kDim);
		ImGui::TextUnformatted(subtitle);
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();
}

const char* courseName(const std::string& path)
{
	if (path == "courses/practice/practice.mod") return tr("Zona del Impacto");
	if (path == "courses/stage1/forest.mod") return tr("Bosque de la Esperanza");
	if (path == "courses/stage2/cave.mod") return tr("El Ombligo del Bosque");
	if (path == "courses/stage3/yakusima.mod") return tr("La Fuente Lejana");
	if (path == "courses/laststage/garden.mod") return tr("La Prueba Final");
	if (path == "courses/route/route.mod") return tr("Mapa del viaje");
	return nullptr;
}

std::string sceneName(const std::string& path)
{
	if (path == "stages/chal0.ini" || path == "stages/practice.ini") return tr("Zona del Impacto");
	if (path.find("stage1") != std::string::npos || path.find("chal1") != std::string::npos) return tr("Bosque de la Esperanza");
	if (path.find("stage2") != std::string::npos || path.find("chal2") != std::string::npos) return tr("El Ombligo del Bosque");
	if (path.find("stage3") != std::string::npos || path.find("chal3") != std::string::npos) return tr("La Fuente Lejana");
	if (path.find("last") != std::string::npos || path.find("chal4") != std::string::npos) return tr("La Prueba Final");
	return path;
}

// ── Iconos dibujados a trazos (sin imágenes ni emojis) ──
enum class Icon { Move, Place, Floor, Height, Plateau, Paint, Water, Walls, Parts, Pellets, Enemies, Bosses, Structures, Decor, More, Ramp, Trace };

/// Dibuja `icon` centrado en `c`, dentro de un cuadrado de lado 2·h.
void drawIcon(ImDrawList* dl, ImVec2 c, float h, Icon icon, ImU32 col)
{
	const float t = std::max(1.5f, h * 0.16f);
	auto P        = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
	switch (icon) {
	case Icon::Ramp: { // dos alturas unidas por una cuesta
		const ImVec2 pts[] = { P(-0.95f, 0.7f), P(-0.35f, 0.7f), P(0.35f, -0.45f), P(0.95f, -0.45f) };
		dl->AddPolyline(pts, 4, col, 0, t * 1.2f);
		dl->AddLine(P(-0.95f, 0.85f), P(0.95f, 0.85f), col, t * 0.7f);
		dl->AddLine(P(0.95f, -0.45f), P(0.95f, 0.85f), col, t * 0.7f);
		break;
	}
	case Icon::Trace: // camino de puntos
		dl->AddBezierCubic(P(-0.85f, 0.7f), P(-0.4f, -0.9f), P(0.3f, 0.9f), P(0.85f, -0.7f), col, t);
		dl->AddCircleFilled(P(-0.85f, 0.7f), h * 0.18f, col);
		dl->AddCircleFilled(P(0.0f, 0.0f), h * 0.18f, col);
		dl->AddCircleFilled(P(0.85f, -0.7f), h * 0.18f, col);
		break;
	case Icon::Move: // cruz de flechas
		dl->AddLine(P(-0.9f, 0), P(0.9f, 0), col, t);
		dl->AddLine(P(0, -0.9f), P(0, 0.9f), col, t);
		for (int k = 0; k < 4; k++) {
			const float dx[] = { 1, -1, 0, 0 }, dy[] = { 0, 0, 1, -1 };
			const ImVec2 tip = P(dx[k] * 0.95f, dy[k] * 0.95f);
			dl->AddTriangleFilled(tip, P(dx[k] * 0.55f - dy[k] * 0.3f, dy[k] * 0.55f - dx[k] * 0.3f), P(dx[k] * 0.55f + dy[k] * 0.3f, dy[k] * 0.55f + dx[k] * 0.3f), col);
		}
		break;
	case Icon::Place: // chincheta: cabeza redonda y punta
		dl->AddCircleFilled(P(0, -0.35f), h * 0.5f, col);
		dl->AddTriangleFilled(P(-0.3f, -0.05f), P(0.3f, -0.05f), P(0, 0.95f), col);
		break;
	case Icon::Floor: // trozo de suelo con un borde suave
		dl->AddBezierCubic(P(-0.9f, 0.2f), P(-0.4f, -0.6f), P(0.3f, 0.4f), P(0.9f, -0.3f), col, t);
		dl->AddLine(P(0.9f, -0.3f), P(0.9f, 0.8f), col, t);
		dl->AddLine(P(0.9f, 0.8f), P(-0.9f, 0.8f), col, t);
		dl->AddLine(P(-0.9f, 0.8f), P(-0.9f, 0.2f), col, t);
		break;
	case Icon::Height: // montañas
		dl->AddTriangleFilled(P(-0.95f, 0.75f), P(-0.25f, -0.6f), P(0.45f, 0.75f), col);
		dl->AddTriangle(P(0.0f, 0.75f), P(0.5f, -0.15f), P(0.95f, 0.75f), col, t);
		break;
	case Icon::Plateau: { // meseta con escalón
		const ImVec2 pts[] = { P(-0.95f, 0.7f), P(-0.45f, 0.7f), P(-0.45f, -0.3f), P(0.45f, -0.3f), P(0.45f, 0.7f), P(0.95f, 0.7f) };
		dl->AddPolyline(pts, 6, col, 0, t);
		dl->AddLine(P(0, -0.95f), P(0, -0.55f), col, t);
		dl->AddTriangleFilled(P(-0.22f, -0.7f), P(0.22f, -0.7f), P(0, -1.0f), col);
		break;
	}
	case Icon::Paint: // rodillo
		dl->AddRectFilled(P(-0.8f, -0.85f), P(0.6f, -0.35f), col, h * 0.12f);
		dl->AddLine(P(0.6f, -0.6f), P(0.85f, -0.6f), col, t);
		dl->AddLine(P(0.85f, -0.6f), P(0.85f, 0.0f), col, t);
		dl->AddLine(P(0.85f, 0.0f), P(0.0f, 0.1f), col, t);
		dl->AddLine(P(0.0f, 0.1f), P(0.0f, 0.95f), col, t * 1.6f);
		break;
	case Icon::Water: // gota
		dl->PathArcTo(P(0, 0.3f), h * 0.6f, 3.14159f * -0.2f, 3.14159f * 1.2f, 20);
		dl->PathLineTo(P(0, -0.95f));
		dl->PathFillConvex(col);
		break;
	case Icon::Walls: // ladrillos
		for (int r = 0; r < 3; r++) {
			const float y0 = -0.8f + r * 0.55f;
			dl->AddRect(P(-0.9f, y0), P(0.9f, y0 + 0.5f), col, 0, 0, t * 0.8f);
			const float off = (r % 2) ? 0.0f : 0.45f;
			dl->AddLine(P(-0.45f + off, y0), P(-0.45f + off, y0 + 0.5f), col, t * 0.8f);
			if (!off) dl->AddLine(P(0.45f, y0), P(0.45f, y0 + 0.5f), col, t * 0.8f);
		}
		break;
	case Icon::Parts: // engranaje
		for (int k = 0; k < 8; k++) {
			const float a = k * 3.14159f / 4.0f;
			dl->AddLine(P(std::cos(a) * 0.5f, std::sin(a) * 0.5f), P(std::cos(a) * 0.95f, std::sin(a) * 0.95f), col, t * 1.8f);
		}
		dl->AddCircle(c, h * 0.55f, col, 20, t * 1.4f);
		dl->AddCircleFilled(c, h * 0.18f, col);
		break;
	case Icon::Pellets: // pastilla con su número
		dl->AddCircle(c, h * 0.85f, col, 24, t);
		dl->AddLine(P(-0.12f, -0.3f), P(0.05f, -0.45f), col, t);
		dl->AddLine(P(0.05f, -0.45f), P(0.05f, 0.45f), col, t);
		break;
	case Icon::Enemies: // bicho: cuerpo, ojos y patas
		dl->AddCircleFilled(P(0, 0.05f), h * 0.6f, col);
		dl->AddCircleFilled(P(-0.25f, -0.55f), h * 0.2f, col);
		dl->AddCircleFilled(P(0.25f, -0.55f), h * 0.2f, col);
		dl->AddLine(P(-0.4f, 0.55f), P(-0.55f, 0.95f), col, t);
		dl->AddLine(P(0.4f, 0.55f), P(0.55f, 0.95f), col, t);
		break;
	case Icon::Bosses: { // corona
		const ImVec2 pts[] = { P(-0.9f, 0.6f), P(-0.9f, -0.5f), P(-0.45f, 0.0f), P(0, -0.8f), P(0.45f, 0.0f), P(0.9f, -0.5f), P(0.9f, 0.6f) };
		dl->AddPolyline(pts, 7, col, ImDrawFlags_Closed, t);
		break;
	}
	case Icon::Structures: // puente
		dl->AddLine(P(-0.95f, -0.1f), P(0.95f, -0.1f), col, t * 1.4f);
		dl->PathArcTo(P(0, 0.75f), h * 0.6f, 3.14159f, 3.14159f * 2.0f, 16);
		dl->PathStroke(col, 0, t);
		dl->AddLine(P(-0.6f, -0.1f), P(-0.6f, 0.75f), col, t);
		dl->AddLine(P(0.6f, -0.1f), P(0.6f, 0.75f), col, t);
		break;
	case Icon::Decor: // flor
		for (int k = 0; k < 5; k++) {
			const float a = k * 3.14159f * 2.0f / 5.0f - 1.5708f;
			dl->AddCircleFilled(P(std::cos(a) * 0.38f, -0.25f + std::sin(a) * 0.38f), h * 0.26f, col);
		}
		dl->AddLine(P(0, 0.1f), P(0, 0.95f), col, t);
		break;
	case Icon::More:
		for (int k = -1; k <= 1; k++) dl->AddCircleFilled(P(k * 0.6f, 0), h * 0.17f, col);
		break;
	}
}

/// CollapsingHeader con icono tras la flecha.
bool iconHeader(const char* label, Icon icon, ImGuiTreeNodeFlags flags = 0)
{
	const std::string padded = std::string("      ") + label;
	const bool open          = ImGui::CollapsingHeader(padded.c_str(), flags);
	const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
	const float x  = a.x + ImGui::GetStyle().FramePadding.x + ImGui::GetFontSize() + 14.0f;
	drawIcon(ImGui::GetWindowDrawList(), ImVec2(x, (a.y + b.y) * 0.5f), (b.y - a.y) * 0.32f, icon, ImGui::GetColorU32(ImGuiCol_Text));
	return open;
}

/// Selector de idioma (cada opción en su propio idioma). True si cambia.
bool languagePicker(Config& cfg)
{
	bool changed = false;
	ImGui::TextUnformatted("Language / Idioma");
	ImGui::SameLine();
	const struct {
		const char* code;
		const char* name;
	} langs[] = { { "en", "English" }, { "es", "Español" } };
	for (const auto& l : langs) {
		ImGui::SameLine();
		if (ImGui::RadioButton(l.name, cfg.language == l.code) && cfg.language != l.code) {
			cfg.language = l.code;
			nectar::setLanguage(nectar::languageFromCode(cfg.language));
			cfg.save();
			changed = true;
		}
	}
	return changed;
}

Rgba playerColour(int p) { return p == 0 ? Rgba { 0.95f, 0.3f, 0.25f, 1 } : Rgba { 0.25f, 0.5f, 1.0f, 1 }; }

M4 xformMatrix(const Xform& x)
{
	const float s = float(x.scale);
	return M4::srt(V3(s, s, s), V3(float(x.rx), float(x.ry), float(x.rz)) * kDeg, V3(float(x.x), float(x.y), float(x.z)));
}

// Tramos de un muro (los arcos, como los despliega el compilador).
struct Segment {
	double ax, az, bx, bz;
};
std::vector<Segment> wallSegments(const nectar::DocWall& w)
{
	std::vector<Segment> out;
	if (!w.isArc) {
		out.push_back({ w.ax, w.az, w.bx, w.bz });
		return out;
	}
	const int n = std::max(1, w.segments);
	for (int k = 0; k < n; k++) {
		const double a0 = (w.from + k * (w.to - w.from) / n) * 3.14159265358979 / 180.0;
		const double a1 = (w.from + (k + 1) * (w.to - w.from) / n) * 3.14159265358979 / 180.0;
		out.push_back({ w.cx + w.radius * std::cos(a0), w.cz + w.radius * std::sin(a0), w.cx + w.radius * std::cos(a1), w.cz + w.radius * std::sin(a1) });
	}
	return out;
}

M4 segmentMatrix(const Segment& s, float y)
{
	const float ang = float(-std::atan2(s.bz - s.az, s.bx - s.ax));
	return M4::srt(V3(1, 1, 1), V3(0, ang, 0), V3(float((s.ax + s.bx) * 0.5), y, float((s.az + s.bz) * 0.5)));
}

void fallbackBox(ItemType t, V3& lo, V3& hi)
{
	if (t == ItemType::Rocket || t == ItemType::Ship) {
		lo = V3(-45, 0, -45);
		hi = V3(45, 150, 45);
	} else {
		lo = V3(-30, 0, -30);
		hi = V3(30, 60, 30);
	}
}

// Rayo contra caja en el espacio de `m`; t del rayo del mundo o -1.
float rayBox(const Ray& ray, const M4& m, const V3& lo, const V3& hi)
{
	const M4 inv = m.inverse();
	const V3 o = inv.point(ray.origin), e = inv.point(ray.origin + ray.dir);
	const V3 d = e - o;
	float t0 = -1e30f, t1 = 1e30f;
	const float os[3] = { o.x, o.y, o.z }, ds[3] = { d.x, d.y, d.z }, ls[3] = { lo.x, lo.y, lo.z }, hs[3] = { hi.x, hi.y, hi.z };
	for (int i = 0; i < 3; i++) {
		if (std::fabs(ds[i]) < 1e-9f) {
			if (os[i] < ls[i] || os[i] > hs[i]) return -1;
			continue;
		}
		float a = (ls[i] - os[i]) / ds[i], b = (hs[i] - os[i]) / ds[i];
		if (a > b) std::swap(a, b);
		t0 = std::max(t0, a);
		t1 = std::min(t1, b);
		if (t0 > t1) return -1;
	}
	if (t1 < 0) return -1;
	return t0 >= 0 ? t0 : t1;
}

void boxLines(std::vector<V3>& out, const M4& m, const V3& lo, const V3& hi)
{
	V3 c[8];
	for (int i = 0; i < 8; i++) c[i] = m.point(V3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
	const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	for (const auto& k : e) {
		out.push_back(c[k[0]]);
		out.push_back(c[k[1]]);
	}
}

// Ruido de valor suave en [0, 1] (pincel de ruido del terreno).
float hashCell(int x, int z)
{
	unsigned h = unsigned(x) * 374761393u + unsigned(z) * 668265263u;
	h          = (h ^ (h >> 13)) * 1274126177u;
	return float((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

float valueNoise(float x, float z)
{
	const float fx = std::floor(x), fz = std::floor(z);
	const int ix = int(fx), iz = int(fz);
	float tx = x - fx, tz = z - fz;
	tx       = tx * tx * (3 - 2 * tx);
	tz       = tz * tz * (3 - 2 * tz);
	const float a = hashCell(ix, iz), b = hashCell(ix + 1, iz), c = hashCell(ix, iz + 1), d = hashCell(ix + 1, iz + 1);
	return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
}

/// Ruido de -1 a 1 con detalle (dos octavas), de `scale` unidades.
float terrainNoise2(float x, float z, float scale) { return (valueNoise(x / scale, z / scale) * 0.65f + valueNoise(x / scale * 2.3f, z / scale * 2.3f) * 0.35f) * 2.0f - 1.0f; }

float pointSegmentDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b)
{
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float l2 = dx * dx + dy * dy;
	if (l2 <= 0.0001f) return std::hypot(p.x - a.x, p.y - a.y);
	const float t = std::max(0.0f, std::min(1.0f, ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2));
	return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

std::string pelletId(int colour, int value)
{
	static const char cs[] = { 'r', 'b', 'y' };
	char buf[8];
	std::snprintf(buf, sizeof(buf), "p%c%02d", cs[std::max(0, std::min(2, colour))], value);
	return buf;
}

const nectar::EnemyInfo* enemyInfo(const std::string& id)
{
	for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
		if (id == e.id) return &e;
	}
	return nullptr;
}

const nectar::ObjectInfo* objectInfo(const std::string& id)
{
	for (const nectar::ObjectInfo& o : nectar::objectKinds()) {
		if (id == o.id) return &o;
	}
	return nullptr;
}

const nectar::ShipPartInfo* partInfo(const std::string& id)
{
	for (const nectar::ShipPartInfo& p : nectar::shipParts()) {
		if (id == p.id) return &p;
	}
	return nullptr;
}

int pelletColour(const std::string& id) { return id.size() > 1 ? (id[1] == 'b' ? 1 : id[1] == 'y' ? 2 : 0) : 0; }
int pelletValue(const std::string& id) { return id.size() == 4 ? std::atoi(id.c_str() + 2) : 1; }
bool validPelletId(const std::string& id)
{
	return id.size() == 4 && id[0] == 'p' && (id[1] == 'r' || id[1] == 'b' || id[1] == 'y')
	    && (id.compare(2, 2, "01") == 0 || id.compare(2, 2, "05") == 0 || id.compare(2, 2, "10") == 0 || id.compare(2, 2, "20") == 0);
}

} // namespace

// ── Arranque ────────────────────────────────────────────────────────────────

App::App() = default;

App::~App()
{
	if (catalogThread_.joinable()) catalogThread_.join();
	for (auto& kv : thumbs_) {
		GLuint t = kv.second;
		if (t) glDeleteTextures(1, &t);
	}
	for (auto& kv : swatches_) {
		GLuint t = kv.second;
		if (t) glDeleteTextures(1, &t);
	}
}

bool App::init(const std::string& openPath, std::string& error)
{
	openAtStart_ = openPath;
	if (!renderer_.init(error)) return false;
	cfg_.load();
	nectar::setLanguage(nectar::languageFromCode(cfg_.language));
	std::string why;
	const std::string assets = resolveAssetsDir(cfg_.assetsDir, why);
	copyStr(assetsBuf_, sizeof(assetsBuf_), assets.empty() ? guessAssetsDir() : assets);
	copyStr(gameBuf_, sizeof(gameBuf_), cfg_.gameExe.empty() ? guessGameExe() : cfg_.gameExe);
	if (cfg_.gameExe.empty()) cfg_.gameExe = gameBuf_;
	if (!assets.empty()) {
		// Ya configurado: solo hay que poner al día el catálogo (rápido si no
		// ha cambiado nada).
		cfg_.assetsDir = assets;
		startCatalog();
	}
	return true;
}

void App::startCatalog()
{
	if (catalogThread_.joinable()) catalogThread_.join();
	catalogFinished_ = false;
	catalogDone_     = 0;
	catalogTotal_    = 0;
	catalogError_.clear();
	screen_                 = Screen::Building;
	const std::string data  = cfg_.dataDir();
	const std::string cache = nectar::defaultCacheDir();
	catalogThread_          = std::thread([this, data, cache] {
        nectar::Catalog cat;
        std::string err;
        const bool ok = nectar::buildCatalog(data, cache, cat, err, [this](std::size_t done, std::size_t total, const std::string&) {
            catalogDone_  = int(done);
            catalogTotal_ = int(total);
        });
        if (ok) catalog_ = std::move(cat);
        else catalogError_ = err;
        catalogFinished_ = true;
    });
}

void App::frame()
{
	thumbLoadsThisFrame_ = 0;
	switch (screen_) {
	case Screen::Setup: drawSetup(); break;
	case Screen::Building: drawBuilding(); break;
	case Screen::Welcome: drawWelcome(); break;
	case Screen::Editor: drawEditor(); break;
	case Screen::Campaign: drawCampaign(); break;
	}
}

void App::drawSetup()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(720, 0));
	ImGui::Begin("Nectar Map Maker", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	ImGui::TextWrapped("%s", tr("¡Bienvenido! Con este programa puedes crear tus propios mapas para el modo VS de Open Nectar."));
	hint(tr("Usa los datos del juego que ya tienes instalado: ni el programa ni tus mapas llevan nada de Nintendo dentro."));
	languagePicker(cfg_);
	ImGui::Spacing();
	ImGui::SeparatorText(tr("1. ¿Dónde está el juego?"));
	hint(tr("Elige la carpeta «assets» del juego (la crea el launcher al instalarlo)."));
	ImGui::SetNextItemWidth(560);
	ImGui::InputText("##assets", assetsBuf_, sizeof(assetsBuf_));
	ImGui::SameLine();
	if (ImGui::Button(tr("Buscar...##a"))) {
		const fs::path p = pikmin::launcher::platform::askForInstallDirectory(tr("Elige la carpeta assets del juego"));
		if (!p.empty()) copyStr(assetsBuf_, sizeof(assetsBuf_), p.string());
	}
	std::string why;
	const std::string assets = resolveAssetsDir(assetsBuf_, why);
	ImGui::PushStyleColor(ImGuiCol_Text, assets.empty() ? kBad : kGood);
	ImGui::TextWrapped("%s", assets.empty() ? (tr("Todavía no: ") + why).c_str() : tr("¡Encontrado!"));
	ImGui::PopStyleColor();

	ImGui::Spacing();
	ImGui::SeparatorText(tr("2. El programa del juego (para probar tus mapas)"));
	hint(tr("Normalmente se encuentra solo. Sin él puedes crear y guardar mapas, pero no probarlos."));
	ImGui::SetNextItemWidth(560);
	ImGui::InputText("##game", gameBuf_, sizeof(gameBuf_));
	ImGui::SameLine();
	if (ImGui::Button(tr("Buscar...##g"))) {
		const fs::path p = pikmin::launcher::platform::askForFile(tr("Elige el programa del juego"), tr("Juego"), "*");
		if (!p.empty()) copyStr(gameBuf_, sizeof(gameBuf_), p.string());
	}
	std::error_code ec;
	if (gameBuf_[0] && !fs::is_regular_file(gameBuf_, ec)) ImGui::TextColored(kWarn, "%s", tr("No existe ese archivo."));

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::BeginDisabled(assets.empty());
	if (ImGui::Button(tr("Empezar"), ImVec2(180, 40))) {
		cfg_.assetsDir = assets;
		cfg_.gameExe   = gameBuf_;
		cfg_.save();
		startCatalog();
	}
	ImGui::EndDisabled();
	ImGui::End();
}

void App::drawBuilding()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(560, 0));
	ImGui::Begin(tr("Preparando"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	if (!catalogFinished_) {
		const int done = catalogDone_, total = catalogTotal_;
		ImGui::Text("%s", tr("Preparando las cosas del juego..."));
		char overlay[64];
		if (total > 0) std::snprintf(overlay, sizeof(overlay), "%d / %d", done, total);
		else std::snprintf(overlay, sizeof(overlay), "%s", tr("Buscando..."));
		ImGui::ProgressBar(total > 0 ? float(done) / float(total) : 0.0f, ImVec2(-1, 0), overlay);
		hint(tr("Solo tarda la primera vez."));
	} else {
		if (catalogThread_.joinable()) catalogThread_.join();
		if (!catalogError_.empty()) {
			ImGui::TextColored(kBad, tr("No se pudo leer el juego: %s"), catalogError_.c_str());
			if (ImGui::Button(tr("Elegir otra carpeta"))) screen_ = Screen::Setup;
		} else {
			ImGui::End();
			enterEditor();
			return;
		}
	}
	ImGui::End();
}

void App::enterEditor()
{
	preview_.start(cfg_.dataDir());
	setTemplateDataDir(cfg_.dataDir()); // plantas y hierba del juego en las plantillas
	lastAutosave_ = now();
	// Al arrancar, la bienvenida (o el mapa que se pasó al abrir el programa);
	// si ya había un mapa abierto, a él.
	if (ed_.generation == 0 && !openAtStart_.empty()) {
		openMap(openAtStart_);
		openAtStart_.clear();
	}
	screen_ = ed_.generation == 0 ? Screen::Welcome : Screen::Editor;
	if (screen_ == Screen::Editor && !cfg_.tutorialDone) tutorialStep_ = 0;
}

bool App::shapeButton(const char* id, nectar::TerrainShape shape, const char* label, float size)
{
	ImGui::BeginGroup();
	ImGui::PushID(id);
	const bool pressed  = ImGui::Button("##shape", ImVec2(size, size));
	const ImVec2 a      = ImGui::GetItemRectMin();
	const ImVec2 c      = ImVec2(a.x + size * 0.5f, a.y + size * 0.5f);
	ImDrawList* dl      = ImGui::GetWindowDrawList();
	const ImU32 green   = IM_COL32(110, 190, 90, 255);
	const float w = size * 0.4f, h = size * 0.26f;
	switch (shape) {
	case nectar::TerrainShape::Rectangle: dl->AddRectFilled(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), green, 3); break;
	case nectar::TerrainShape::Oval: dl->AddEllipseFilled(c, ImVec2(w, h), green); break;
	case nectar::TerrainShape::Cross:
		dl->AddRectFilled(ImVec2(c.x - w, c.y - h * 0.38f), ImVec2(c.x + w, c.y + h * 0.38f), green);
		dl->AddRectFilled(ImVec2(c.x - w * 0.22f, c.y - h), ImVec2(c.x + w * 0.22f, c.y + h), green);
		break;
	case nectar::TerrainShape::TwoIslands:
		dl->AddEllipseFilled(ImVec2(c.x - w * 0.55f, c.y), ImVec2(w * 0.45f, h * 0.9f), green);
		dl->AddEllipseFilled(ImVec2(c.x + w * 0.55f, c.y), ImVec2(w * 0.45f, h * 0.9f), green);
		dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.14f), ImVec2(c.x + w * 0.5f, c.y + h * 0.14f), green);
		break;
	case nectar::TerrainShape::Ring:
		dl->AddEllipse(c, ImVec2(w * 0.82f, h * 0.82f), green, 0.0f, 0, h * 0.36f);
		dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.12f), ImVec2(c.x + w * 0.5f, c.y + h * 0.12f), green);
		break;
	}
	ImGui::PopID();
	const float tw = ImGui::CalcTextSize(label).x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (size - tw) * 0.5f));
	ImGui::TextUnformatted(label);
	ImGui::EndGroup();
	return pressed;
}

void App::drawWelcome()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	const float welcomeW = std::max(600.0f, std::min(1080.0f, vp->WorkSize.x - 48.0f));
	const float welcomeH = std::max(540.0f, std::min(900.0f, vp->WorkSize.y - 48.0f));
	ImGui::SetNextWindowSize(ImVec2(welcomeW, welcomeH));
	const std::string title = std::string(startMode_ ? tr("Mapa nuevo") : tr("¿Qué quieres hacer?")) + "###welcome";
	ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		drawSprout(dl, ImVec2(p.x + 30, p.y + 29), 1.15f);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 62);
		ImGui::TextColored(ImVec4(0.90f, 0.87f, 0.62f, 1), "NECTAR MAP MAKER");
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 62);
		hint(tr("Crea mapas para VS, misiones y campañas."));
		ImGui::Spacing();
	}
	if (startMode_ == 0)
		drawStartHome();
	else
		drawStartNew();
	ImGui::End();
}

// Paso 1: como en Blender o Roblox, a la izquierda lo nuevo (primero el modo
// de juego, que decide qué se puede poner) y lo que ya tienes; a la derecha,
// los recientes.
void App::drawStartHome()
{
	const float avail  = ImGui::GetContentRegionAvail().x;
	const float rightW = std::max(250.0f, std::min(380.0f, avail * 0.36f));
	const float leftW  = avail - rightW - 24.0f;
	ImGui::BeginChild("##home_left", ImVec2(leftW, 0));
	ImGui::SeparatorText(tr("Crear un mapa nuevo"));
	hint(tr("Primero, ¿a qué se va a jugar en este mapa? Después eliges cómo empezar."));
	ImGui::Spacing();
	const unsigned modes[] = { nectar::MAPMODE_VS, nectar::MAPMODE_SOLO, nectar::MAPMODE_COOP, kStartCampaign };
	const float cardW      = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;
	for (int i = 0; i < 4; i++) {
		if (i % 2) ImGui::SameLine(0, 16);
		if (modeCard(int(modes[i]), cardW, 118.0f)) startMode_ = modes[i];
		if (i % 2) ImGui::Dummy(ImVec2(0, 6));
	}
	ImGui::Spacing();
	ImGui::SeparatorText(tr("O trae uno que ya tienes"));
	if (ImGui::Button(tr("Abrir un archivo de mapa..."), ImVec2(320, 34))) openMapFile();
	hint(tr("Un map.json de cualquier carpeta: tuyo o que te hayan pasado."));
	ImGui::Spacing();
	if (ImGui::Button(tr("Importar un modelo (OBJ o DAE)..."), ImVec2(320, 34))) newMapFromModel();
	hint(tr("Un escenario hecho en Blender o sacado de otro juego: su malla es el suelo y encima pones cebollas, piezas y enemigos."));
	ImGui::EndChild();
	ImGui::SameLine(0, 24);
	ImGui::BeginChild("##home_right", ImVec2(rightW, 0));
	recentList(rightW);
	ImGui::EndChild();
}

bool App::modeCard(int mode, float w, float h)
{
	const char* title;
	const char* blurb;
	ImU32 accent;
	switch (unsigned(mode)) {
	case nectar::MAPMODE_VS: title = "VS", blurb = tr("Dos jugadores, uno contra el otro."), accent = IM_COL32(214, 92, 74, 255); break;
	case nectar::MAPMODE_SOLO:
		title = tr("Solo"), blurb = tr("Una misión para un jugador: capitán, nave, cebollas y un objetivo."), accent = IM_COL32(90, 140, 220, 255);
		break;
	case nectar::MAPMODE_COOP: title = tr("Coop"), blurb = tr("La misma misión, pero para dos jugadores a la vez."), accent = IM_COL32(110, 190, 90, 255); break;
	default: title = tr("Campaña"), blurb = tr("Varios niveles (zonas) que se van abriendo, como la historia del juego."), accent = IM_COL32(225, 200, 90, 255); break;
	}
	ImGui::PushID(mode);
	const ImVec2 p     = ImGui::GetCursorScreenPos();
	const bool pressed = ImGui::Button("##card", ImVec2(w, h));
	ImDrawList* dl     = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, ImVec2(p.x + 6, p.y + h), accent, 4.0f, ImDrawFlags_RoundCornersLeft);
	// Dibujo: cuántos juegan y cómo.
	const ImVec2 c(p.x + 52, p.y + h * 0.5f + 6);
	switch (unsigned(mode)) {
	case nectar::MAPMODE_VS:
		drawSprout(dl, ImVec2(c.x - 18, c.y), 0.8f);
		drawSprout(dl, ImVec2(c.x + 18, c.y), 0.8f);
		dl->AddText(nullptr, ImGui::GetFontSize() * 0.9f, ImVec2(c.x - 8, c.y - 34), accent, "vs");
		break;
	case nectar::MAPMODE_SOLO: drawSprout(dl, c, 1.1f); break;
	case nectar::MAPMODE_COOP:
		drawSprout(dl, ImVec2(c.x - 13, c.y), 0.85f);
		drawSprout(dl, ImVec2(c.x + 13, c.y + 4), 0.85f);
		break;
	default:
		drawSprout(dl, ImVec2(c.x - 22, c.y + 6), 0.6f);
		drawSprout(dl, ImVec2(c.x, c.y - 6), 0.6f);
		drawSprout(dl, ImVec2(c.x + 22, c.y + 6), 0.6f);
		break;
	}
	const float tx = p.x + 104;
	dl->AddText(nullptr, ImGui::GetFontSize() * 1.35f, ImVec2(tx, p.y + 16), IM_COL32(236, 230, 200, 255), title);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + 16 + ImGui::GetFontSize() * 1.6f), IM_COL32(190, 190, 180, 255), blurb, nullptr,
	            std::max(40.0f, w - (tx - p.x) - 12));
	ImGui::PopID();
	return pressed;
}

void App::recentList(float w)
{
	ImGui::SeparatorText(tr("Mis mapas"));
	struct Recent {
		std::string name, path, modes, when;
		fs::file_time_type at {};
	};
	static std::vector<Recent> sList;
	static double sAt = -10.0;
	if (now() - sAt > 3.0) {
		sAt = now();
		sList.clear();
		// Como en Roblox Studio: todos los mapas de mi carpeta más los abiertos
		// de otros sitios, el más reciente primero.
		std::vector<std::string> paths = cfg_.recentMaps;
		if (!cfg_.lastMap.empty() && std::find(paths.begin(), paths.end(), cfg_.lastMap) == paths.end()) paths.insert(paths.begin(), cfg_.lastMap);
		std::error_code ec;
		for (fs::directory_iterator it(cfg_.userMapsDir(), ec), end; !ec && it != end; it.increment(ec)) {
			const std::string f = (it->path() / "map.json").string();
			if (fs::exists(f, ec) && std::find(paths.begin(), paths.end(), f) == paths.end()) paths.push_back(f);
		}
		for (const std::string& path : paths) {
			if (!fs::exists(path, ec) || sList.size() >= 60) continue;
			Recent r;
			r.at   = fs::last_write_time(path, ec);
			r.path = path;
			r.name = fs::path(path).parent_path().filename().string();
			std::string text, err;
			MapDoc d;
			if (readText(path, text) && nectar::parseMapDoc(text, d, err)) {
				r.name = d.name;
				for (unsigned m : { unsigned(nectar::MAPMODE_VS), unsigned(nectar::MAPMODE_SOLO), unsigned(nectar::MAPMODE_COOP) }) {
					if (!(d.modes & m)) continue;
					if (!r.modes.empty()) r.modes += " · ";
					r.modes += m == nectar::MAPMODE_VS ? "VS" : m == nectar::MAPMODE_SOLO ? tr("Solo") : tr("Coop");
				}
			}
			const auto ft = fs::last_write_time(path, ec);
			if (!ec) {
				const auto sys  = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
				const auto days = std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now() - sys).count() / 24;
				char buf[64];
				if (days <= 0)
					r.when = tr("hoy");
				else if (days == 1)
					r.when = tr("ayer");
				else
					std::snprintf(buf, sizeof(buf), tr("hace %d días"), int(days)), r.when = buf;
			}
			sList.push_back(r);
		}
		std::stable_sort(sList.begin(), sList.end(), [](const Recent& a, const Recent& b) { return a.at > b.at; });
	}
	if (sList.empty()) {
		hint(tr("Aquí saldrán tus mapas: los que crees, guardes o abras."));
		return;
	}
	ImGui::SetNextItemWidth(w - 8);
	static char sFilter[64] = "";
	ImGui::InputTextWithHint("##maps_filter", tr("Buscar..."), sFilter, sizeof(sFilter));
	ImGui::BeginChild("##maps_list");
	const float rowH = 76.0f;
	auto icontains = [](const std::string& hay, const char* needle) {
		if (!*needle) return true;
		auto lower = [](std::string v) {
			for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
			return v;
		};
		return lower(hay).find(lower(needle)) != std::string::npos;
	};
	for (std::size_t i = 0; i < sList.size(); i++) {
		if (!icontains(sList[i].name, sFilter)) continue;
		const Recent& r = sList[i];
		ImGui::PushID(int(i));
		const ImVec2 p     = ImGui::GetCursorScreenPos();
		const bool pressed = ImGui::Selectable("##recent", false, 0, ImVec2(w - 8, rowH));
		ImGui::SetItemTooltip("%s", r.path.c_str());
		ImDrawList* dl   = ImGui::GetWindowDrawList();
		const ImVec2 a(p.x + 4, p.y + 6), b(p.x + 4 + rowH - 12, p.y + rowH - 6);
		const std::string thumbPath = (fs::path(r.path).parent_path() / "thumb.png").string();
		std::error_code ec;
		const unsigned tex = fs::exists(thumbPath, ec) ? thumb(thumbPath) : 0;
		if (tex)
			dl->AddImage(ImTextureID(std::intptr_t(tex)), a, b);
		else
			dl->AddRectFilled(a, b, IM_COL32(60, 80, 50, 255), 4.0f);
		const float tx = b.x + 12, lh = ImGui::GetFontSize() + 4;
		dl->PushClipRect(ImVec2(tx, p.y), ImVec2(p.x + w - 12, p.y + rowH), true);
		dl->AddText(ImVec2(tx, p.y + 10), IM_COL32(236, 230, 200, 255), r.name.c_str());
		dl->AddText(ImVec2(tx, p.y + 10 + lh), IM_COL32(170, 200, 140, 255), r.modes.c_str());
		dl->AddText(ImVec2(tx, p.y + 10 + lh * 2), IM_COL32(150, 150, 140, 255), r.when.c_str());
		dl->PopClipRect();
		ImGui::PopID();
		if (pressed) {
			openMap(r.path);
			if (ed_.path == r.path) screen_ = Screen::Editor;
			break;
		}
	}
	ImGui::EndChild();
}

// Paso 2: como en Unreal, ya con el modo elegido; siempre hay uno en blanco.
void App::drawStartNew()
{
	if (ImGui::Button(tr("< Cambiar modo"), ImVec2(170, 34))) {
		startMode_ = 0;
		return;
	}
	ImGui::SameLine(0, 16);
	const char* modeName = startMode_ == nectar::MAPMODE_VS     ? "VS"
	                       : startMode_ == nectar::MAPMODE_SOLO ? tr("Solo")
	                       : startMode_ == nectar::MAPMODE_COOP ? tr("Coop")
	                                                             : tr("Campaña");
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
	ImGui::TextColored(ImVec4(0.90f, 0.87f, 0.62f, 1), "%s%s", tr("Mapa nuevo para: "), modeName);
	ImGui::Spacing();
	if (startMode_ == kStartCampaign) {
		drawCampaignPickers();
		return;
	}
	ImGui::SeparatorText(tr("Empieza en blanco"));
	hint(tr("Elige el tamaño y la forma del suelo para empezar. Después puedes cambiarlos como quieras."));
	// Tamaño del mapa en blanco: el de siempre (mediano) o a escala.
	static const double kSizeScale[3] = { 0.75, 1.0, 1.5 };
	{
		const char* sizes[3] = { tr("Pequeño"), tr("Mediano"), tr("Grande") };
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(tr("Tamaño"));
		for (int i = 0; i < 3; i++) {
			ImGui::SameLine(0, i ? 8 : 16);
			if (choiceButton(sizes[i], startSize_ == i, ImVec2(130, 30))) startSize_ = i;
		}
		ImGui::SameLine(0, 16);
		char dims[64];
		std::snprintf(dims, sizeof(dims), "%.0f × %.0f", 3200.0 * kSizeScale[startSize_], 1800.0 * kSizeScale[startSize_]);
		hint(dims);
	}
	ImGui::Spacing();
	struct S {
		const char* id;
		nectar::TerrainShape shape;
		const char* label;
	};
	const S shapes[] = { { "r", nectar::TerrainShape::Rectangle, tr("Rectángulo") }, { "o", nectar::TerrainShape::Oval, tr("Óvalo") },
		                 { "c", nectar::TerrainShape::Cross, tr("Cruz") },          { "i", nectar::TerrainShape::TwoIslands, tr("Dos islas") },
		                 { "a", nectar::TerrainShape::Ring, tr("Anillo") } };
	const float shapeSize = std::max(96.0f, std::min(150.0f, (ImGui::GetContentRegionAvail().x - 72.0f) / 5.0f));
	for (int i = 0; i < 5; i++) {
		if (i) ImGui::SameLine(0, 18);
		if (shapeButton(shapes[i].id, shapes[i].shape, shapes[i].label, shapeSize)) {
			newMap(shapes[i].shape, kSizeScale[startSize_], (startMode_ & nectar::MAPMODE_VS) != 0);
			applyStartMode();
			screen_ = Screen::Editor;
		}
	}
	ImGui::Spacing();
	const float templateSize = std::max(112.0f, std::min(148.0f, (ImGui::GetContentRegionAvail().x - 80.0f) / 6.0f));
	if (templatePickers(templateSize, startMode_)) {
		applyStartMode();
		screen_ = Screen::Editor;
	}
	ImGui::Spacing();
	if (coursePickers()) {
		applyStartMode();
		screen_ = Screen::Editor;
	}
	if (startMode_ == nectar::MAPMODE_VS) {
		ImGui::Spacing();
		ImGui::SeparatorText(tr("O bien"));
		if (ImGui::Button(tr("Copiar la arena del juego"), ImVec2(240, 40))) {
			openBuiltIn();
			screen_ = Screen::Editor;
		}
	}
}

void App::applyStartMode()
{
	if (!startMode_ || startMode_ == kStartCampaign) return;
	MapDoc d = ed_.doc;
	// La misión se coloca mirando dónde empieza el Jugador 1 en VS: antes de quitarlo.
	if (startMode_ & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP)) nm::ensureStory(d, startMode_ == nectar::MAPMODE_COOP);
	d.modes = startMode_;
	ed_.reset(d, ed_.path);
}

void App::rememberRecent(const std::string& path)
{
	auto& list = cfg_.recentMaps;
	list.erase(std::remove(list.begin(), list.end(), path), list.end());
	list.insert(list.begin(), path);
	if (list.size() > 12) list.resize(12);
}

void App::openMapFile()
{
	const fs::path file = pikmin::launcher::platform::askForFile(tr("Elige un mapa"), tr("Mapa (map.json)"), "*.json");
	if (file.empty()) return;
	openMap(file.string());
	if (ed_.path == file.string()) screen_ = Screen::Editor;
}

// ── Archivo ─────────────────────────────────────────────────────────────────

void App::notify(int level, const std::string& text)
{
	toasts_.push_back({ level, text, now() + (level == 2 ? 7.0 : 4.0) });
	if (toasts_.size() > 4) toasts_.erase(toasts_.begin());
	fprintf(stderr, "[mapmaker] %s\n", text.c_str());
}

void App::newMap(nectar::TerrainShape shape, double scale, bool vsWalls)
{
	editingZoneOf_.clear();
	MapDoc d = nectar::defaultMapDoc();
	if (!vsWalls) d.walls.clear(); // las murallas de las bases solo pintan algo en VS
	if (scale != 1.0) {
		// Otro tamaño: todo a escala (bordes, bases, cebollas, piezas y
		// murallas) y el terreno rehecho del tamaño nuevo.
		for (const ItemRef& r : allItems(d)) {
			if (r.type == ItemType::Wall) continue;
			Xform x = getXform(d, r);
			x.x *= scale;
			x.z *= scale;
			setXform(d, r, x);
		}
		for (nectar::DocWall& w : d.walls) {
			w.ax *= scale, w.az *= scale, w.bx *= scale, w.bz *= scale;
			w.cx *= scale, w.cz *= scale, w.radius *= scale;
		}
		d.minX *= scale, d.maxX *= scale, d.minZ *= scale, d.maxZ *= scale;
		d.terrain = nectar::DocTerrain();
		nectar::makeTerrainGrid(d);
	}
	if (shape != nectar::TerrainShape::Rectangle) {
		nectar::applyTerrainShape(d, shape);
		// Que salidas, cebollas y piezas no se queden sin suelo con la forma.
		nectar::DocTerrain& g = d.terrain;
		for (const ItemRef& r : allItems(d)) {
			const Xform x = getXform(d, r);
			// También su copia del otro jugador (mapa espejo).
			for (int side = 0; side < 2; side++) {
				const double px = side ? -x.x : x.x, pz = side ? -x.z : x.z;
				const int cx = int(std::lround((px - d.minX) / d.cell)), cz = int(std::lround((pz - d.minZ) / d.cell));
				for (int dz = -3; dz <= 3; dz++) {
					for (int dx = -3; dx <= 3; dx++) {
						const int ix = cx + dx, iz = cz + dz;
						if (ix < 1 || iz < 1 || ix >= g.nx || iz >= g.nz || dx * dx + dz * dz > 10) continue;
						g.floor[std::size_t(g.vertexIndex(ix, iz))] = 255;
					}
				}
			}
		}
	}
	d.name = tr("Mi mapa");
	ed_.reset(d, "");
	hiddenItems_.clear();
	lockedItems_.clear();
	cam_.target   = V3(0, 0, 0);
	cam_.distance = 4000;
	cam_.topDown  = false;
	tool_         = Tool::Move;
	if (!cfg_.tutorialDone) tutorialStep_ = 0;
}

void App::newMapFromModel()
{
	const fs::path file = pikmin::launcher::platform::askForFile(tr("Elige el nivel"), tr("Malla OBJ o DAE"), "*.obj *.OBJ *.dae *.DAE");
	if (file.empty()) return;
	// Primero su tamaño, para que quepa como un escenario del juego: el lado
	// mayor en planta, a 4000 (los del juego van de 2000 a 5000).
	nectar::MeshModInput mesh;
	ObjInfo info;
	std::string err;
	if (!readMesh(file.string(), 1.0f, mesh, info, err)) {
		notify(2, tr("No se pudo importar: ") + err);
		return;
	}
	const float side  = std::max(info.max.x - info.min.x, info.max.z - info.min.z);
	const float scale = side > 1.0f ? 4000.0f / side : 1.0f;

	// La carpeta del mapa (las mallas van en ella), como al guardar.
	const std::string name = file.stem().string();
	std::string path;
	std::error_code ec;
	for (int n = 1;; n++) {
		const std::string folder = n == 1 ? slug(name) : slug(name) + "_" + std::to_string(n);
		path                     = (fs::path(cfg_.userMapsDir()) / folder / "map.json").string();
		if (!fs::exists(path, ec)) break;
	}
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::string asset;
	if (!importObj(file.string(), fs::path(path).parent_path().string(), scale, asset, info, err)) {
		notify(2, tr("No se pudo importar: ") + err);
		return;
	}
	models_.erase(assetFile(asset));

	newMap(nectar::TerrainShape::Rectangle);
	MapDoc d      = ed_.doc;
	d.name        = name;
	d.terrain     = nectar::DocTerrain();
	d.terrain.none = true;
	// Centrado en el origen, con el contorno justo alrededor.
	const double cx = (info.min.x + info.max.x) * 0.5, cz = (info.min.z + info.max.z) * 0.5;
	const double hx = (info.max.x - info.min.x) * 0.5 + 50.0, hz = (info.max.z - info.min.z) * 0.5 + 50.0;
	d.minX = -hx, d.maxX = hx, d.minZ = -hz, d.maxZ = hz;
	nectar::DocProp level;
	level.asset   = asset;
	level.x       = -cx;
	level.z       = -cz;
	level.collide = true;
	level.ground  = true;
	d.props.push_back(level);
	ed_.reset(d, path); // con la ruta ya puesta, la malla "map:" se encuentra al compilar
	if (!saveMap(path)) return;
	cam_.distance = 6000;
	screen_       = Screen::Editor;
	char buf[256];
	std::snprintf(buf, sizeof(buf), tr("Nivel importado: %zu triángulos, %.0f × %.0f × %.0f (escala %.3f). Coloca las cosas encima."), info.triangles,
	              info.max.x - info.min.x, info.max.y - info.min.y, info.max.z - info.min.z, scale);
	notify(info.warnings.empty() ? 0 : 1, std::string(buf) + (info.warnings.empty() ? "" : "\n" + info.warnings));
}

unsigned App::templateThumb(const TemplateInfo& t)
{
	// Se dibujan una vez (una por fotograma) y se guardan en la caché.
	const std::string path = (fs::path(catalog_.cacheDir) / "templates" / (std::string(t.id) + "_v4.png")).string();
	std::error_code ec;
	if (!fs::exists(path, ec)) {
		static int lastFrame = -1;
		if (lastFrame == ImGui::GetFrameCount()) return 0;
		lastFrame = ImGui::GetFrameCount();
		PreviewResult r;
		Preview::compileNow(cfg_.dataDir(), t.build(), r);
		if (!r.ok) return 0;
		fs::create_directories(fs::path(path).parent_path(), ec);
		nectar::RenderOptions o;
		o.size  = 200;
		o.pitch = 58.0f;
		o.yaw   = 0.0f;
		nectar::writePng(path, nectar::renderModel(r.full, o));
	}
	return thumb(path);
}

bool App::templatePickers(float size, unsigned modes)
{
	// Las sencillas primero, para que quien empieza sepa por dónde ir.
	std::vector<const TemplateInfo*> order;
	for (const TemplateInfo& t : mapTemplates()) {
		if (t.modes & modes) order.push_back(&t);
	}
	if (order.empty()) return false;
	ImGui::SeparatorText(tr("O empieza con una plantilla"));
	hint(tr("Mapas ya hechos, listos para jugar o para cambiarlos a tu gusto."));
	const TemplateInfo* chosen = nullptr;
	const int perRow           = std::max(1, int(ImGui::GetContentRegionAvail().x / (size + 16.0f)));
	int col                    = 0;
	std::stable_sort(order.begin(), order.end(), [](const TemplateInfo* a, const TemplateInfo* b) { return a->easy > b->easy; });
	for (const TemplateInfo* tp : order) {
		const TemplateInfo& t = *tp;
		ImGui::PushID(t.id);
		ImGui::BeginGroup();
		const unsigned tex = templateThumb(t);
		const bool pressed = tex ? ImGui::ImageButton("##t", ImTextureID(std::intptr_t(tex)), ImVec2(size, size * 0.75f))
		                         : ImGui::Button("...", ImVec2(size + 8, size * 0.75f + 8));
		{
			const char* tag  = t.easy ? tr("Fácil") : tr("Completa");
			const ImVec2 at  = ImVec2(ImGui::GetItemRectMin().x + 6, ImGui::GetItemRectMin().y + 6);
			const ImVec2 ts  = ImGui::CalcTextSize(tag);
			ImDrawList* dl   = ImGui::GetWindowDrawList();
			dl->AddRectFilled(at, ImVec2(at.x + ts.x + 12, at.y + ts.y + 4), t.easy ? IM_COL32(70, 150, 70, 230) : IM_COL32(170, 120, 40, 230), 8.0f);
			dl->AddText(ImVec2(at.x + 6, at.y + 2), IM_COL32(255, 255, 255, 255), tag);
		}
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size + 8);
		ImGui::TextUnformatted(tr(t.name));
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
		ImGui::SetItemTooltip("%s\n\n%s", tr(t.blurb), t.easy ? tr("Fácil: sencilla, buena para empezar.") : tr("Completa: con de todo, para quien ya sabe."));
		if (pressed) chosen = &t;
		ImGui::PopID();
		if (++col % perRow) ImGui::SameLine(0, 12);
	}
	if (col % perRow) ImGui::NewLine();
	if (!chosen) return false;
	const MapDoc d = chosen->build();
	ed_.reset(d, "");
	hiddenItems_.clear();
	lockedItems_.clear();
	cam_.target   = V3(0, 0, 0);
	cam_.distance = 4000;
	cam_.topDown  = false;
	tool_         = Tool::Move;
	notify(0, std::string(tr("Plantilla cargada: ")) + d.name);
	return true;
}

bool App::coursePickers()
{
	ImGui::SeparatorText(tr("O un escenario del juego"));
	hint(tr("El nivel tal cual, con su vegetación y sus enemigos: puedes moverlos, quitarlos y añadir lo tuyo. El suelo del nivel no se puede cambiar."));
	const CourseChoice* chosen = nullptr;
	const struct {
		const char* label;
		const std::vector<CourseChoice>* list;
	} groups[] = { { tr("Historia"), &storyCourses() }, { tr("Desafío"), &challengeCourses() } };
	for (int k = 0; k < 2; k++) {
		if (k) ImGui::SameLine();
		ImGui::SetNextItemWidth(240);
		ImGui::PushID(k);
		if (ImGui::BeginCombo("##course", groups[k].label)) {
			for (const CourseChoice& c : *groups[k].list) {
				if (ImGui::Selectable(tr(c.name))) chosen = &c;
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();
	}
	if (!chosen) return false;
	MapDoc d;
	std::string err;
	if (!importCourse(cfg_.dataDir(), *chosen, d, err)) {
		notify(2, std::string(tr("No se pudo cargar ese escenario: ")) + err);
		return false;
	}
	ed_.reset(d, "");
	hiddenItems_.clear();
	lockedItems_.clear();
	cam_.target   = V3(float(d.bases[0].x), 0, float(d.bases[0].z));
	cam_.distance = 2500;
	cam_.topDown  = false;
	cam_.topZoom  = float(std::max(d.maxX - d.minX, d.maxZ - d.minZ)) * 0.45f;
	tool_         = Tool::Move;
	notify(0, std::string(tr("Escenario cargado: ")) + d.name);
	return true;
}

void App::openMap(const std::string& path)
{
	editingZoneOf_.clear(); // (editCampaignZone lo vuelve a poner)
	std::string text, error;
	MapDoc doc;
	if (!readText(path, text)) {
		notify(2, tr("No se puede abrir ese mapa."));
		return;
	}
	if (!nectar::parseMapDoc(text, doc, error)) {
		notify(2, tr("Ese archivo de mapa está dañado: ") + error);
		return;
	}
	ed_.reset(doc, path);
	hiddenItems_.clear();
	lockedItems_.clear();
	cfg_.lastMap = path;
	rememberRecent(path);
	cfg_.save();
	cam_.target   = V3(float((doc.minX + doc.maxX) * 0.5), 0, float((doc.minZ + doc.maxZ) * 0.5));
	cam_.distance = float(std::max(doc.maxX - doc.minX, doc.maxZ - doc.minZ)) * 1.25f;
	cam_.topZoom  = float(std::max(doc.maxX - doc.minX, doc.maxZ - doc.minZ)) * 0.45f;
	notify(0, tr("Abierto «") + doc.name + "».");
}

void App::openBuiltIn()
{
	MapDoc doc;
	std::string error;
	if (!nectar::parseMapDoc(kPcVsDefaultMap, doc, error)) return;
	doc.name = tr("Mi arena");
	doc.author.clear();
	ed_.reset(doc, "");
	hiddenItems_.clear();
	lockedItems_.clear();
	cam_.target   = V3(0, 0, 0);
	cam_.distance = 4000;
	notify(0, tr("Copia de la arena del juego: cámbiala como quieras."));
}

bool App::saveMap(const std::string& path)
{
	if (!writeText(path, nectar::writeMapDoc(ed_.doc))) {
		notify(2, tr("No se pudo guardar (¿falta permiso en la carpeta del juego?)."));
		return false;
	}
	// Miniatura para el menú del juego: el mapa entero visto desde arriba.
	PreviewResult r;
	Preview::compileNow(cfg_.dataDir(), docToCompile(), r);
	if (r.ok) {
		nectar::RenderOptions o;
		o.size  = 256;
		o.pitch = 90.0f;
		o.yaw   = 0.0f;
		nectar::writePng((fs::path(path).parent_path() / "thumb.png").string(), nectar::renderModel(r.full, o));
	}
	ed_.path     = path;
	ed_.dirty    = false;
	lastSaved_   = now();
	cfg_.lastMap = path;
	rememberRecent(path);
	cfg_.save();
	notify(0, tr("Guardado. Lo encontrarás en el juego: modo VS > Mapa."));
	return true;
}

void App::saveOrAsk(bool thenTest)
{
	if (ed_.path.empty()) {
		copyStr(saveName_, sizeof(saveName_), ed_.doc.name);
		testAfterSave_ = thenTest;
		openSaveAs_    = true;
		return;
	}
	if (saveMap(ed_.path) && thenTest) testInGame();
}

void App::testInGame(bool fromHere)
{
	if (ed_.path.empty() || ed_.dirty) {
		saveOrAsk(true);
		return;
	}
	for (const Issue& i : checkMap()) {
		if (i.blocking) {
			notify(2, tr("Antes de probar: ") + i.text);
			return;
		}
	}
	PreviewResult r;
	if (!Preview::compileNow(cfg_.dataDir(), docToCompile(), r)) {
		notify(2, tr("Todavía no se puede jugar: ") + r.error);
		return;
	}
	std::string exe = cfg_.gameExe.empty() ? guessGameExe() : cfg_.gameExe;
	std::error_code ec;
	if (exe.empty() || !fs::is_regular_file(exe, ec)) {
		notify(2, tr("No encuentro el juego. Indícalo en Ayuda > Cambiar carpetas."));
		return;
	}
	std::string err;
	std::string mapPath = fs::absolute(ed_.path, ec).string();
	if (fromHere) {
		// Una copia del mapa (en su carpeta, por sus mallas) con el capitán en
		// el centro de la vista.
		if (!ed_.doc.story.present || !(ed_.doc.modes & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP))) {
			notify(1, tr("Probar desde la cámara es para solo y coop."));
			return;
		}
		const float x = cam_.target.x, z = cam_.target.z;
		if (!docSolid(x, z)) {
			notify(1, tr("En el centro de la vista no hay suelo: mira a un sitio donde se pueda estar."));
			return;
		}
		MapDoc copy                = ed_.doc;
		copy.story.captains[0].x = x;
		copy.story.captains[0].z = z;
		copy.story.captains[1].x = x;
		copy.story.captains[1].z = z + 120;
		const fs::path tmp        = fs::path(ed_.path).parent_path() / "test_here.json";
		if (!writeText(tmp.string(), nectar::writeMapDoc(copy))) {
			notify(2, tr("No se pudo preparar la prueba (¿falta permiso en la carpeta del mapa?)."));
			return;
		}
		mapPath = fs::absolute(tmp, ec).string();
		if (testMode_ == nectar::MAPMODE_VS) testMode_ = (ed_.doc.modes & nectar::MAPMODE_SOLO) ? nectar::MAPMODE_SOLO : nectar::MAPMODE_COOP;
	}
	// El último modo elegido si el mapa lo tiene; si no, el primero que tenga.
	if (!(ed_.doc.modes & testMode_) && !fromHere) {
		for (unsigned m = nectar::MAPMODE_VS; m <= nectar::MAPMODE_COOP; m <<= 1) {
			if (ed_.doc.modes & m) {
				testMode_ = m;
				break;
			}
		}
	}
	std::vector<std::string> args = { "--vs-map", mapPath };
	if (testMode_ == nectar::MAPMODE_SOLO) args = { "--mission-map", mapPath, "--mission-players", "1" };
	if (testMode_ == nectar::MAPMODE_COOP) args = { "--mission-map", mapPath, "--mission-players", "2" };
	if (!launchGame(exe, cfg_.gameDir(), args, err)) {
		notify(2, err);
		return;
	}
	notify(0, testMode_ == nectar::MAPMODE_SOLO ? tr("Abriendo el juego en solo... Teclado o mando 1.")
	                                             : tr("Abriendo el juego... Jugador 1: teclado o mando 1. Jugador 2: mando 2."));
}

void App::autosave()
{
	if (!ed_.dirty || now() - lastAutosave_ < kAutosaveSeconds) return;
	lastAutosave_ = now();
	writeText((fs::path(Config::directory()) / "autosave.json").string(), nectar::writeMapDoc(ed_.doc));
}

bool App::requestQuit()
{
	if (screen_ != Screen::Editor || !ed_.dirty) {
		quit_ = true;
		return true;
	}
	confirmQuit_ = true;
	return false;
}

void App::refreshOpenList()
{
	openList_.clear();
	std::error_code ec;
	for (fs::directory_iterator it(cfg_.userMapsDir(), ec), end; !ec && it != end; it.increment(ec)) {
		const fs::path f = it->path() / "map.json";
		if (!fs::exists(f, ec)) continue;
		std::string text, err, name = it->path().filename().string();
		MapDoc d;
		if (readText(f.string(), text) && nectar::parseMapDoc(text, d, err)) name = d.name;
		openList_.push_back({ name, f.string() });
	}
	std::sort(openList_.begin(), openList_.end());
}

// ── Edición ─────────────────────────────────────────────────────────────────

double App::snap(double v) const { return cfg_.snap ? std::round(v / cfg_.snapStep) * cfg_.snapStep : v; }

void App::deleteSelection()
{
	std::vector<ItemRef> del;
	for (const ItemRef& r : ed_.selection) {
		if (itemCaps(r.type).removable) del.push_back(r);
	}
	if (del.empty()) {
		if (!ed_.selection.empty()) notify(1, tr("Eso no se puede borrar: el juego lo necesita. Muévelo a donde quieras."));
		return;
	}
	remapSessionItemsAfterRemoval(del);
	removeItems(ed_.doc, del);
	ed_.clearSelection();
	ed_.touch();
}

void App::copySelection()
{
	clipboard_    = copyItems(ed_.doc, ed_.selection);
	hasClipboard_ = !(clipboard_.pellets.empty() && clipboard_.pieces.empty() && clipboard_.guards.empty() && clipboard_.props.empty()
	                  && clipboard_.walls.empty() && clipboard_.ponds.empty() && clipboard_.enemies.empty() && clipboard_.objects.empty());
}

void App::paste()
{
	if (!hasClipboard_) return;
	ed_.selection = pasteItems(ed_.doc, clipboard_, 60, 60);
	ed_.touch();
}

void App::duplicateSelection()
{
	const MapDoc clip                 = copyItems(ed_.doc, ed_.selection);
	const std::vector<ItemRef> pasted = pasteItems(ed_.doc, clip, 60, 60);
	if (pasted.empty()) {
		if (!ed_.selection.empty()) notify(1, tr("Eso no se puede duplicar: cada jugador tiene solo uno."));
		return;
	}
	ed_.selection = pasted;
	ed_.touch();
}

void App::focusSelection()
{
	if (ed_.selection.empty()) return;
	const Xform x = getXform(ed_.doc, ed_.selection.back());
	cam_.target   = V3(float(x.x), docHeight(float(x.x), float(x.z)), float(x.z));
	cam_.distance = 700;
	cam_.topZoom  = 500;
}

void App::rotateSelection(double degrees)
{
	bool any = false;
	for (const ItemRef& r : ed_.selection) {
		if (!itemCaps(r.type).rotateY) continue;
		Xform x = getXform(ed_.doc, r);
		x.ry += degrees;
		setXform(ed_.doc, r, x);
		ItemRef other;
		if (mirrorEdits_ && ed_.doc.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(ed_.doc, r);
		any = true;
	}
	if (any) ed_.touch();
}

void App::liftSelection(double dy)
{
	bool any = false;
	for (const ItemRef& r : ed_.selection) {
		if (!itemCaps(r.type).height) continue;
		Xform x = getXform(ed_.doc, r);
		x.y += dy;
		setXform(ed_.doc, r, x);
		ItemRef other;
		if (mirrorEdits_ && ed_.doc.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(ed_.doc, r);
		any = true;
	}
	if (any) ed_.touch();
}

bool App::selectionCanGround() const
{
	for (const ItemRef& r : ed_.selection) {
		if (r.type == ItemType::Prop || r.type == ItemType::Gen || r.type == ItemType::Object) return true;
	}
	return false;
}

void App::placeSelectionOnGround()
{
	bool changed = false;
	for (const ItemRef& r : ed_.selection) {
		if (!itemExists(ed_.doc, r)) continue;
		Xform x = getXform(ed_.doc, r);
		double target = x.y;
		if (r.type == ItemType::Object) target = 0.0; // su Y se guarda como altura sobre el suelo
		else if (r.type == ItemType::Prop || r.type == ItemType::Gen) target = docHeight(float(x.x), float(x.z));
		else continue;
		if (std::fabs(x.y - target) < 0.001) continue;
		x.y = target;
		setXform(ed_.doc, r, x);
		ItemRef other;
		if (mirrorEdits_ && ed_.doc.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(ed_.doc, r);
		changed = true;
	}
	if (changed) ed_.touch();
}

bool App::itemHidden(const ItemRef& r) const
{
	if (std::find(hiddenItems_.begin(), hiddenItems_.end(), r) != hiddenItems_.end()) return true;
	// «Ver el día N»: los enemigos que ese día no salen.
	if (viewDay_ > 0 && r.type == ItemType::Enemy && itemExists(ed_.doc, r)) {
		const nectar::DocEnemy& e = ed_.doc.enemies[size_t(r.index)];
		if ((e.fromDay && viewDay_ < e.fromDay) || (e.untilDay && viewDay_ > e.untilDay)) return true;
	}
	return false;
}
bool App::itemLocked(const ItemRef& r) const { return std::find(lockedItems_.begin(), lockedItems_.end(), r) != lockedItems_.end(); }

void App::toggleHidden(const ItemRef& r)
{
	auto it = std::find(hiddenItems_.begin(), hiddenItems_.end(), r);
	if (it == hiddenItems_.end()) hiddenItems_.push_back(r);
	else hiddenItems_.erase(it);
	if (itemHidden(r)) ed_.selection.erase(std::remove(ed_.selection.begin(), ed_.selection.end(), r), ed_.selection.end());
}

void App::toggleLocked(const ItemRef& r)
{
	auto it = std::find(lockedItems_.begin(), lockedItems_.end(), r);
	if (it == lockedItems_.end()) lockedItems_.push_back(r);
	else lockedItems_.erase(it);
	if (itemLocked(r)) ed_.selection.erase(std::remove(ed_.selection.begin(), ed_.selection.end(), r), ed_.selection.end());
}

void App::remapSessionItemsAfterRemoval(const std::vector<ItemRef>& removed)
{
	(void)removed;
	// ItemRef usa índices: al borrar una entrada, las posteriores cambian de
	// identidad. Es más seguro limpiar estos estados efímeros que bloquear u
	// ocultar accidentalmente otro elemento (también tras deshacer el borrado).
	hiddenItems_.clear();
	lockedItems_.clear();
}

// Fuente de arrastre: lo elegido viaja con el ratón hasta el visor, que lo
// pone donde se suelte (como arrastrar del Toolbox de Roblox Studio).
void App::beginPlaceDrag(const PlaceChoice& c, unsigned thumbTex, const std::string& name, int flags)
{
	if (!ImGui::BeginDragDropSource(flags | ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) return;
	PlacePayload pl {};
	pl.type = int(c.type);
	std::snprintf(pl.asset, sizeof(pl.asset), "%s", c.asset.c_str());
	std::snprintf(pl.label, sizeof(pl.label), "%s", c.label.c_str());
	ImGui::SetDragDropPayload("NM_PLACE", &pl, sizeof(pl));
	if (thumbTex) ImGui::Image(ImTextureID(std::intptr_t(thumbTex)), ImVec2(56, 56));
	ImGui::SameLine();
	ImGui::TextUnformatted(name.c_str());
	ImGui::TextColored(kDim, "%s", tr("Suelta sobre el mapa para ponerlo"));
	ImGui::EndDragDropSource();
	// Mientras tanto, la herramienta de poner enseña la vista previa.
	place_ = c;
	tool_  = Tool::Place;
}

std::string App::placeAsset(const PlaceChoice& c) const
{
	switch (c.type) {
	case ItemType::Prop: return c.asset;
	case ItemType::Pellet: return "objects/pellets/white1.mod";
	case ItemType::Piece:
	case ItemType::ShipPart: {
		const nectar::ShipPartInfo* info = partInfo(c.label);
		return info ? info->model : "objects/ufoparts/fuzoku1.mod";
	}
	case ItemType::StoryOnion: return "objects/goal/goal.mod";
	case ItemType::Guard: return "tekis/swallow/swallow.mod";
	case ItemType::Enemy: {
		const nectar::EnemyInfo* info = enemyInfo(c.label);
		return info ? info->model : "";
	}
	case ItemType::Object: {
		const nectar::ObjectInfo* info = objectInfo(c.label);
		return info ? info->model : "";
	}
	case ItemType::Gen: {
		const GameGenChoice* g = findGameGen(cfg_.dataDir(), c.label);
		return g ? g->gen.model : "";
	}
	default: return "";
	}
}

void App::placeAt(const V3& at)
{
	// Lo de un modo que el mapa no tiene (p. ej. lo elegido antes de cambiarlo).
	if (isVsItem(place_.type) && !(ed_.doc.modes & nectar::MAPMODE_VS)) return;
	if (isStoryItem(place_.type) && !ed_.doc.story.present) return;
	const double x = snap(at.x), z = snap(at.z);
	ItemRef r;
	if (place_.type == ItemType::Prop) {
		r                      = addItem(ed_.doc, ItemType::Prop, x, z, place_.asset);
		ed_.doc.props.back().y = docHeight(float(x), float(z));
		// Mallas propias: partes del nivel, se pisan.
		if (place_.asset.rfind("map:", 0) == 0 || place_.asset.rfind("cut:", 0) == 0) ed_.doc.props.back().collide = ed_.doc.props.back().ground = true;
		if (const OfficialPiece* piece = findOfficialPiece(place_.label)) {
			if (place_.asset == piece->asset()) {
				ed_.doc.props.back().collide = piece->collide;
				ed_.doc.props.back().ground  = false;
			}
		}
	} else if (place_.type == ItemType::Gen) {
		const GameGenChoice* c = findGameGen(cfg_.dataDir(), place_.label);
		if (!c) return;
		nectar::DocGen g = c->gen;
		g.x              = x;
		g.z              = z;
		g.y              = docHeight(float(x), float(z));
		g.mirror         = ed_.doc.symmetric && (ed_.doc.modes & nectar::MAPMODE_VS);
		ed_.doc.gens.push_back(g);
		r = { ItemType::Gen, int(ed_.doc.gens.size()) - 1, 0 };
	} else if (place_.type == ItemType::Enemy || place_.type == ItemType::Object || place_.type == ItemType::StoryOnion
	           || place_.type == ItemType::ShipPart) {
		r = addItem(ed_.doc, place_.type, x, z, place_.label);
	} else {
		r = addItem(ed_.doc, place_.type, x, z);
		if (place_.type == ItemType::Pellet) ed_.doc.pellets.back().id = place_.label;
		if (place_.type == ItemType::Piece) ed_.doc.pieces.back().part = place_.label;
	}
	ed_.selection = { r };
	ed_.touch();
}

std::vector<App::Issue> App::checkMap() const
{
	std::vector<Issue> out;
	const MapDoc& d = ed_.doc;
	// Cosas sin suelo o fuera del mapa: las dice por su nombre.
	for (const ItemRef& r : allItems(d)) {
		if (r.type == ItemType::Wall) continue;
		const Xform x = getXform(d, r);
		if (x.x <= d.minX || x.x >= d.maxX || x.z <= d.minZ || x.z >= d.maxZ) {
			out.push_back({ true, itemName(d, r) + tr(" está fuera del mapa."), true, r, IssueFix::ClampInside });
		} else if (r.type != ItemType::Pond && !docSolid(float(x.x), float(x.z))) {
			out.push_back({ true, itemName(d, r) + tr(" está sobre un hueco: ponle suelo debajo o muévelo."), true, r, IssueFix::NearestGround });
		}
		// Con el mapa espejo, también su gemelo del otro lado.
		if (d.symmetric && itemCaps(r.type).twinned && r.type != ItemType::Pond && !docSolid(float(-x.x), float(-x.z))
		    && x.x > d.minX && x.x < d.maxX) {
			out.push_back({ true, tr("La copia de «") + itemName(d, r) + tr("» del lado del Jugador 2 cae sobre un hueco."), true, r, IssueFix::NearestGround });
		}
	}
	const int k = d.symmetric ? 2 : 1;
	const bool vs = (d.modes & nectar::MAPMODE_VS) != 0;
	if (vs && d.pieces.empty()) out.push_back({ true, tr("Pon al menos una pieza de la nave para que haya algo que recoger."), false, {} });
	if (vs && int(d.pieces.size()) * k > nectar::kMapMaxPieces) out.push_back({ true, tr("Hay demasiadas piezas de la nave (el máximo son 32 en total)."), false, {} });
	// La misión: lo que pide cada modo (el resto lo dice el compilador).
	if (d.story.present && (d.modes & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP))) {
		if ((d.modes & nectar::MAPMODE_COOP) && d.story.captainCount < 2)
			out.push_back({ true, tr("Para coop hacen falta dos capitanes."), false, {} });
		if (d.story.rules.objective == "parts" && d.story.parts.empty())
			out.push_back({ true, tr("La misión pide piezas de la nave: pon alguna (Poner cosas > Misión)."), false, {} });
		bool onion[3] = {};
		for (const nectar::DocStoryOnion& o : d.story.onions) {
			if (o.color >= 0 && o.color < 3) {
				if (onion[o.color]) out.push_back({ true, tr("Hay dos cebollas del mismo color en la misión."), false, {} });
				onion[o.color] = true;
			}
		}
		for (int c = 0; c < 3; c++) {
			if (d.story.rules.startPikmin[c] > 0 && !onion[c])
				out.push_back({ true, tr("Hay Pikmin al empezar de un color sin su cebolla en la misión."), false, {} });
		}
	}
	if (int(d.pellets.size()) * k > nectar::kMapMaxPellets) out.push_back({ true, tr("Hay demasiadas pastillas (el máximo son 48 en total)."), false, {} });
	if (int(d.guards.size()) * k > nectar::kMapMaxGuards) out.push_back({ true, tr("Hay demasiados Bulborbs (el máximo son 16 en total)."), false, {} });
	for (std::size_t index = 0; index < d.props.size(); index++) {
		const nectar::DocProp& prop = d.props[index];
		if (!std::isfinite(prop.scale) || prop.scale <= 0.0 || prop.scale > 20.0)
			out.push_back({ true, itemName(d, { ItemType::Prop, int(index), 0 }) + tr(" tiene una escala inválida."), true,
			                { ItemType::Prop, int(index), 0 }, IssueFix::NormalizeScale });
		bool missing = false;
		const auto loadedError = modelErrors_.find(prop.asset);
		if (loadedError != modelErrors_.end() && !loadedError->second.empty()) missing = true;
		const bool mapAsset = prop.asset.rfind("map:", 0) == 0;
		const bool cutAsset = prop.asset.rfind("cut:", 0) == 0;
		if (mapAsset) {
			// Las mallas importadas pertenecen al mapa, no al catálogo del juego.
			// Validarlas contra catalog_.entries las marcaba siempre como ausentes.
			std::error_code ec;
			const std::string file = assetFile(prop.asset);
			missing = missing || file.empty() || !fs::is_regular_file(file, ec);
		} else if (catalogFinished_ && !cutAsset) {
			bool found = false;
			for (const nectar::CatalogEntry& entry : catalog_.entries) {
				if (entry.path != prop.asset) continue;
				found = true;
				missing = missing || !entry.error.empty();
				break;
			}
			missing = missing || !found;
		}
		if (missing) out.push_back({ true, itemName(d, { ItemType::Prop, int(index), 0 }) + tr(" usa un asset inexistente o dañado."), true,
		                             { ItemType::Prop, int(index), 0 }, IssueFix::DeleteItem });
	}
	if (compiled_.generation == ed_.liveGeneration && !compiled_.routes.points.empty()) {
		const std::map<int, int> components = routeComponents(compiled_.routes);
		// Desde la salida del Jugador 1 (VS) o el primer capitán (misión).
		const bool fromBase = (d.modes & nectar::MAPMODE_VS) || !d.story.present;
		const int start     = fromBase ? nearestRoutePoint(compiled_.routes, d.bases[0].x, d.bases[0].z)
		                               : nearestRoutePoint(compiled_.routes, d.story.captains[0].x, d.story.captains[0].z);
		for (const ItemRef& r : allItems(d)) {
			if (r.type != ItemType::Pellet && r.type != ItemType::Piece && r.type != ItemType::BigPiece && r.type != ItemType::ShipPart) continue;
			const Xform x = getXform(d, r);
			const int goal = nearestRoutePoint(compiled_.routes, x.x, x.z);
			bool reachable = start >= 0 && goal >= 0 && components.count(start) && components.count(goal) && components.at(start) == components.at(goal);
			for (const nectar::RoutePoint& p : compiled_.routes.points) {
				if (p.index != goal) continue;
				const double distance = std::hypot(p.pos.x - x.x, p.pos.z - x.z);
				reachable = reachable && distance <= std::max(300.0, d.routeStep * 2.0);
				break;
			}
			if (!reachable) out.push_back({ true, itemName(d, r) + tr(" no tiene una ruta alcanzable desde la base."), true, r });
		}
	}
	// Lo que diga el compilador (tamaño, archivos que faltan...).
	if (!compiled_.playable && !compiled_.error.empty() && compiled_.generation == ed_.liveGeneration) {
		bool known = false;
		for (const Issue& i : out) known = known || i.blocking;
		if (!known) {
			std::string text = compiled_.error;
			if (text.find(tr("demasiado grande")) != std::string::npos) text = tr("El mapa es demasiado grande para el juego. Hazlo más pequeño o quita decoración.");
			out.push_back({ true, text, false, {} });
		}
	}
	if (d.pellets.empty()) out.push_back({ false, tr("Consejo: pon algunas pastillas para que los jugadores consigan más Pikmin."), false, {} });
	return out;
}

void App::applyIssueFix(const Issue& issue)
{
	if (!issue.hasItem || !itemExists(ed_.doc, issue.item) || issue.fix == IssueFix::None) return;
	if (issue.fix == IssueFix::DeleteItem) {
		ed_.selection = { issue.item };
		deleteSelection();
		return;
	}
	MapDoc& d = ed_.doc;
	Xform x = getXform(d, issue.item);
	if (issue.fix == IssueFix::NormalizeScale) {
		x.scale = 1.0;
		setXform(d, issue.item, x);
		ed_.selection = { issue.item };
		ed_.touch();
		return;
	}
	const double oldGround = docHeight(float(x.x), float(x.z));
	if (issue.fix == IssueFix::ClampInside) {
		const double margin = std::max(10.0, d.cell * 0.5);
		x.x = std::max(d.minX + margin, std::min(d.maxX - margin, x.x));
		x.z = std::max(d.minZ + margin, std::min(d.maxZ - margin, x.z));
	} else {
		const double step = std::max(25.0, d.cell * 0.5);
		const double maxRadius = std::max(d.maxX - d.minX, d.maxZ - d.minZ);
		bool found = false;
		double bestX = x.x, bestZ = x.z;
		for (double radius = 0; radius <= maxRadius && !found; radius += step) {
			for (double dz = -radius; dz <= radius && !found; dz += step) {
				for (double dx = -radius; dx <= radius; dx += step) {
					if (radius > 0 && std::fabs(dx) < radius && std::fabs(dz) < radius) continue;
					const double cx = x.x + dx, cz = x.z + dz;
					if (cx <= d.minX || cx >= d.maxX || cz <= d.minZ || cz >= d.maxZ) continue;
					if (!docSolid(float(cx), float(cz))) continue;
					if (d.symmetric && itemCaps(issue.item.type).twinned && !docSolid(float(-cx), float(-cz))) continue;
					bestX = cx;
					bestZ = cz;
					found = true;
					break;
				}
			}
		}
		if (!found) {
			notify(1, tr("No se encontró suelo válido cercano."));
			return;
		}
		x.x = bestX;
		x.z = bestZ;
	}
	if (issue.item.type == ItemType::Prop || issue.item.type == ItemType::Gen) x.y += docHeight(float(x.x), float(x.z)) - oldGround;
	setXform(d, issue.item, x);
	ItemRef other;
	if (mirrorEdits_ && d.symmetric && counterpart(issue.item, other) && !ed_.selected(other)) mirrorToCounterpart(d, issue.item);
	ed_.selection = { issue.item };
	ed_.touch();
	focusSelection();
}

void App::createGroundUnder(const ItemRef& item)
{
	if (!itemExists(ed_.doc, item) || !ensureTerrain()) return;
	MapDoc& d = ed_.doc;
	nectar::DocTerrain& terrain = d.terrain;
	const Xform x = getXform(d, item);
	auto fill = [&](double wx, double wz) {
		const int cx = std::max(0, std::min(terrain.nx, int(std::lround((wx - d.minX) / d.cell))));
		const int cz = std::max(0, std::min(terrain.nz, int(std::lround((wz - d.minZ) / d.cell))));
		for (int iz = std::max(0, cz - 1); iz <= std::min(terrain.nz, cz + 1); iz++) {
			for (int ix = std::max(0, cx - 1); ix <= std::min(terrain.nx, cx + 1); ix++) {
				const std::size_t vertex = std::size_t(terrain.vertexIndex(ix, iz));
				terrain.floor[vertex] = 255;
				if (!terrain.layers.empty()) {
					terrain.layers[0][vertex] = 255;
					for (std::size_t layer = 1; layer < terrain.layers.size(); layer++) terrain.layers[layer][vertex] = 0;
				}
			}
		}
	};
	fill(x.x, x.z);
	if (d.symmetric && itemCaps(item.type).twinned) fill(-x.x, -x.z);
	ed_.selection = { item };
	ed_.touch();
	focusSelection();
}

// ── Recursos ────────────────────────────────────────────────────────────────

std::string App::mapDir() const { return ed_.path.empty() ? std::string() : fs::path(ed_.path).parent_path().string(); }

std::string App::assetFile(const std::string& asset) const
{
	if (asset.rfind("map:", 0) == 0) {
		const std::string rel = asset.substr(4);
		if (mapDir().empty() || rel.find("..") != std::string::npos) return std::string();
		return (fs::path(mapDir()) / rel).string();
	}
	return cfg_.dataDir() + "/" + asset;
}

nectar::MapDoc App::docToCompile() const
{
	nectar::MapDoc d = ed_.doc;
	d.dir            = mapDir();
	return d;
}

std::shared_ptr<GpuModel> App::model(const std::string& asset)
{
	// Las mallas propias, por su archivo (dos mapas pueden tener la misma ruta).
	const std::string key = asset.rfind("map:", 0) == 0 ? assetFile(asset) : asset;
	auto it               = models_.find(key);
	if (it != models_.end()) return it->second;
	nectar::Model m;
	std::string err;
	std::shared_ptr<GpuModel> gpu;
	if (asset.rfind("cut:", 0) == 0) {
		if (std::shared_ptr<nectar::Model> cut = cutModel(asset)) gpu = renderer_.upload(*cut);
		else modelErrors_[asset] = tr("no se puede sacar el recorte");
	} else if (nectar::loadMod(assetFile(asset), m, err)) {
		gpu = renderer_.upload(m);
	} else {
		modelErrors_[asset] = err;
	}
	models_[key] = gpu;
	return gpu;
}

unsigned App::thumb(const std::string& path)
{
	if (path.empty()) return 0;
	auto it = thumbs_.find(path);
	if (it != thumbs_.end()) return it->second;
	if (thumbLoadsThisFrame_ >= 6) return 0; // el resto, en los siguientes fotogramas
	thumbLoadsThisFrame_++;
	const GLuint t = loadPngTexture(path);
	thumbs_[path]  = t;
	return t;
}

std::shared_ptr<nectar::Model> App::cutModel(const std::string& asset)
{
	auto it = cutModels_.find(asset);
	if (it != cutModels_.end()) return it->second;
	std::shared_ptr<nectar::Model> out;
	std::string mod, err;
	nectar::Vec3 lo, hi;
	bool object = false;
	if (nectar::parseCutAsset(asset, mod, lo, hi, &object)) {
		if (std::shared_ptr<nectar::Model> course = courseModel(mod)) {
			std::vector<std::uint8_t> bytes;
			nectar::ModRegionInfo info;
			auto m = std::make_shared<nectar::Model>();
			if (nectar::extractModRegion(*course, lo, hi, bytes, info, err, object) && nectar::parseMod(bytes.data(), bytes.size(), *m, err)) out = m;
		}
	}
	cutModels_[asset] = out;
	return out;
}

unsigned App::cutThumb(const std::string& asset)
{
	const std::string path = (fs::path(catalog_.cacheDir) / "pieces" / (std::to_string(std::hash<std::string>()(asset)) + ".png")).string();
	std::error_code ec;
	if (!fs::exists(path, ec)) {
		std::shared_ptr<nectar::Model> m = cutModel(asset);
		if (!m) return 0;
		fs::create_directories(fs::path(path).parent_path(), ec);
		nectar::RenderOptions o;
		o.size = 128;
		nectar::writePng(path, nectar::renderModel(*m, o));
	}
	return thumb(path);
}

bool App::thumbCard(unsigned tex, const std::string& name, bool on, float w, bool favourite)
{
	const float lineH = ImGui::GetFontSize() + 2.0f;
	const float h     = w + lineH * 2 + 8.0f;
	const ImVec2 p    = ImGui::GetCursorScreenPos();
	const bool pressed = ImGui::InvisibleButton("##card", ImVec2(w, h));
	const bool hot     = ImGui::IsItemHovered();
	ImDrawList* dl     = ImGui::GetWindowDrawList();
	const ImVec2 q(p.x + w, p.y + h);
	const ImU32 bg = on ? IM_COL32(74, 98, 42, 255) : hot ? IM_COL32(54, 64, 50, 255) : favourite ? IM_COL32(58, 66, 40, 255) : IM_COL32(42, 50, 40, 255);
	dl->AddRectFilled(p, q, bg, 8.0f);
	if (on || hot) dl->AddRect(p, q, on ? IM_COL32(183, 207, 75, 255) : IM_COL32(120, 140, 100, 255), 8.0f, 0, on ? 2.0f : 1.0f);
	// La miniatura va sobre un fondo claro degradado: así se distinguen también
	// los modelos oscuros.
	const ImVec2 ia(p.x + 5, p.y + 5), ib(q.x - 5, p.y + w - 5);
	dl->AddRectFilled(ia, ib, IM_COL32(128, 140, 120, 255), 6.0f);
	dl->AddRectFilledMultiColor(ImVec2(ia.x, ia.y + (ib.y - ia.y) * 0.5f), ib, IM_COL32(128, 140, 120, 255), IM_COL32(128, 140, 120, 255), IM_COL32(88, 100, 82, 255), IM_COL32(88, 100, 82, 255));
	if (tex) {
		dl->AddImage(ImTextureID(std::intptr_t(tex)), ImVec2(ia.x + 2, ia.y + 2), ImVec2(ib.x - 2, ib.y - 2));
	} else {
		// Sin miniatura: la inicial, grande.
		char initial[5] = { 0 };
		std::snprintf(initial, sizeof(initial), "%c", name.empty() ? '?' : char(std::toupper(static_cast<unsigned char>(name[0]))));
		const float fs    = ImGui::GetFontSize() * 2.2f;
		const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, initial);
		dl->AddText(nullptr, fs, ImVec2((ia.x + ib.x - size.x) * 0.5f, (ia.y + ib.y - size.y) * 0.5f), IM_COL32(30, 38, 28, 255), initial);
	}
	if (favourite) dl->AddText(ImVec2(ib.x - ImGui::GetFontSize() - 2, ia.y + 2), IM_COL32(255, 215, 80, 255), "*");
	// Nombre: hasta dos líneas, recortado.
	dl->PushClipRect(ImVec2(p.x + 4, ib.y), ImVec2(q.x - 4, q.y - 3), true);
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 6, ib.y + 4), on ? IM_COL32(236, 244, 196, 255) : IM_COL32(205, 210, 195, 255), name.c_str(), nullptr, w - 12);
	dl->PopClipRect();
	return pressed;
}

unsigned App::assetThumb(const std::string& asset)
{
	if (asset.rfind("cut:", 0) == 0) return cutThumb(asset);
	for (const nectar::CatalogEntry& e : catalog_.entries) {
		if (e.path == asset) return thumb(e.thumb);
	}
	return 0;
}

std::string App::markerAsset(const ItemRef& r) const
{
	switch (r.type) {
	case ItemType::Base: return r.index == 0 ? "pikis/nv1Model.mod" : "pikis/nv2Model.mod";
	case ItemType::Rocket: return ""; // la nave depende del TEV y sale negra: una columna del color del jugador
	case ItemType::Gate: return "objects/gate/gt_hard.mod";
	case ItemType::BombPile: return "objects/bomb/bomb.mod";
	case ItemType::Onion: return "objects/goal/goal.mod";
	case ItemType::Pellet: {
		const int v = pelletValue(ed_.doc.pellets[std::size_t(r.index)].id);
		return v == 5 ? "objects/pellets/white2.mod" : v == 10 ? "objects/pellets/white3.mod" : v == 20 ? "objects/pellets/white4.mod" : "objects/pellets/white1.mod";
	}
	case ItemType::Piece: {
		const nectar::DocPiece& p = ed_.doc.pieces[std::size_t(r.index)];
		if (const nectar::ShipPartInfo* info = partInfo(p.part)) return info->model;
		return "objects/ufoparts/fuzoku" + std::to_string(1 + std::max(0, p.kind)) + ".mod";
	}
	case ItemType::Enemy: {
		const nectar::EnemyInfo* info = enemyInfo(ed_.doc.enemies[std::size_t(r.index)].type);
		return info ? info->model : "";
	}
	case ItemType::Object: {
		const nectar::ObjectInfo* info = objectInfo(ed_.doc.objects[std::size_t(r.index)].kind);
		return info ? info->model : "";
	}
	case ItemType::Gen: return ed_.doc.gens[std::size_t(r.index)].model;
	case ItemType::BigPiece: return "objects/ufoparts/fuzoku1.mod";
	case ItemType::Guard: return "tekis/chappy/chappy.mod";
	case ItemType::Prop: return ed_.doc.props[std::size_t(r.index)].asset;
	case ItemType::Captain: return r.index == 0 ? "pikis/nv1Model.mod" : "pikis/nv2Model.mod";
	case ItemType::Ship: return ""; // como el cohete: una columna
	case ItemType::StoryOnion: return "objects/goal/goal.mod";
	case ItemType::ShipPart: {
		if (const nectar::ShipPartInfo* info = partInfo(ed_.doc.story.parts[std::size_t(r.index)].id)) return info->model;
		return "objects/ufoparts/fuzoku1.mod";
	}
	default: return "";
	}
}

Rgba App::markerTint(const ItemRef& r) const
{
	auto colourOf = [](int c) {
		return c == 1 ? Rgba { 0.45f, 0.6f, 1.0f, 1 } : c == 2 ? Rgba { 1.0f, 0.9f, 0.35f, 1 } : Rgba { 1.0f, 0.45f, 0.4f, 1 };
	};
	if (r.type == ItemType::Onion) return colourOf(r.sub == 0 ? 1 : r.sub == 1 ? 0 : 2);
	if (r.type == ItemType::StoryOnion) {
		const int c = ed_.doc.story.onions[std::size_t(r.index)].color;
		return colourOf(c == 0 ? 1 : c == 1 ? 0 : 2);
	}
	if (r.type == ItemType::Pellet) return colourOf(pelletColour(ed_.doc.pellets[std::size_t(r.index)].id));
	return Rgba();
}

M4 App::itemMatrix(const ItemRef& r, bool twin) const
{
	Xform x = getXform(ed_.doc, r);
	if (twin) {
		x.x  = -x.x;
		x.z  = -x.z;
		x.ry = x.ry + 180.0;
		if (r.type == ItemType::Prop) x.y += docHeight(float(x.x), float(x.z)) - docHeight(float(-x.x), float(-x.z));
	}
	if (r.type != ItemType::Prop && r.type != ItemType::Gen) {
		x.scale = 1;
		// La puerta con cerradura, al tamaño del juego (DoorItem, kDoorScale).
		constexpr double kKeyDoorScale = 6.0;
		if (r.type == ItemType::Object && r.index >= 0 && r.index < int(ed_.doc.objects.size()) && ed_.doc.objects[std::size_t(r.index)].kind == "key_door")
			x.scale = kKeyDoorScale;
		// Apoyados en el terreno (las construcciones, a su altura sobre él).
		x.y = docHeight(float(x.x), float(x.z)) + (r.type == ItemType::Object ? x.y : 0.0);
	}
	return xformMatrix(x);
}

// ── Editor ──────────────────────────────────────────────────────────────────

void App::drawEditor()
{
	if (courseMode() && courseField_.course != ed_.doc.course) buildCourseField(cfg_.dataDir(), ed_.doc.course, courseField_);
	// Resultados de la compilación en segundo plano.
	PreviewResult r;
	if (preview_.poll(r)) {
		if (r.ok) {
			baseGpu_ = renderer_.upload(r.base);
			// La superficie del agua (la capa 15 de las charcas), para dibujarla en azul.
			waterTris_.clear();
			for (const nectar::BakedTri& t : nectar::bakeModel(r.base)) {
				if (t.material != 15) continue;
				for (const nectar::Vec3& p : t.p) waterTris_.push_back(V3(p.x, p.y + 1, p.z));
			}
			collisionTris_.clear();
			for (const nectar::BakedTri& t : nectar::bakeModel(r.full)) {
				for (const nectar::Vec3& p : t.p) collisionTris_.push_back(V3(p.x, p.y + 2, p.z));
			}
		}
		compiled_ = std::move(r);
		if (hasGroundProps() && compiled_.ok) buildModelField(compiled_.full, meshField_);
		else meshField_ = CourseField();
	}
	if (requestedLive_ != ed_.liveGeneration && !preview_.busy() && now() - lastRequest_ > 0.16) {
		requestedLive_ = ed_.liveGeneration;
		lastRequest_   = now();
		preview_.request(docToCompile(), ed_.liveGeneration);
	}

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const float X = vp->WorkPos.x, Y = vp->WorkPos.y, W = vp->WorkSize.x, H = vp->WorkSize.y;
	// Los dos laterales son ajustables, pero el lienzo siempre conserva un
	// mínimo útil. Las preferencias antiguas no tienen estas claves y reciben
	// los tamaños por defecto desde Config.
	// A la izquierda, la columna de iconos (ancho fijo); a la derecha, el
	// panel de propiedades (ajustable); en medio, la fila de opciones de la
	// herramienta, el visor y, si está abierto, el catálogo.
	const float toolW = cfg_.showToolbox ? kRailW : 0.0f;
	float sideW       = cfg_.showSidePanel ? std::max(kMinSideW, cfg_.sidePanelWidth) : 0.0f;
	if (cfg_.showSidePanel) {
		sideW               = std::max(kMinSideW, std::min(sideW, W - toolW - kMinViewportW));
		cfg_.sidePanelWidth = sideW;
	}
	const float bodyH   = H - kTopBarH - kStatusBarH;
	const float viewW   = W - toolW - sideW;
	const bool drawer   = catalogOpen_ && tool_ == Tool::Place;
	const float drawerH = drawer ? std::min(320.0f, (bodyH - kToolRowH) * 0.45f) : 0.0f;
	drawTopBar({ X, Y, W, kTopBarH });
	if (cfg_.showToolbox) drawToolbox({ X, Y + kTopBarH, toolW, bodyH });
	drawToolHeader({ X + toolW, Y + kTopBarH, viewW, kToolRowH });
	drawViewport({ X + toolW, Y + kTopBarH + kToolRowH, viewW, bodyH - kToolRowH - drawerH });
	if (drawer) drawCatalogDrawer({ X + toolW, Y + kTopBarH + bodyH - drawerH, viewW, drawerH });
	if (cfg_.showSidePanel) drawSidePanel({ X + W - sideW, Y + kTopBarH, sideW, bodyH });
	drawStatusBar({ X, Y + H - kStatusBarH, W, kStatusBarH });

	// Captura directa por coordenadas: no depende del orden de hover de las
	// ventanas del visor y los paneles, que comparten exactamente este borde.
	auto splitter = [&](int which, float x, float& value, float sign, float defaultValue) {
		ImGuiIO& io = ImGui::GetIO();
		const bool insideY = io.MousePos.y >= Y + kTopBarH && io.MousePos.y <= Y + H - kStatusBarH;
		const bool hovered = insideY && std::fabs(io.MousePos.x - x) <= 7.0f;
		if (splitterDrag_ == 0 && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) splitterDrag_ = which;
		const bool active = splitterDrag_ == which;
		if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) value += io.MouseDelta.x * sign;
		if (active && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			splitterDrag_ = 0;
			cfg_.save();
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			value = defaultValue;
			cfg_.save();
		}
		if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

		ImDrawList* fg = ImGui::GetForegroundDrawList();
		const ImU32 col = active ? IM_COL32(215, 222, 98, 255) : hovered ? IM_COL32(171, 196, 76, 255) : IM_COL32(94, 117, 49, 190);
		fg->AddLine(ImVec2(x, Y + kTopBarH), ImVec2(x, Y + H - kStatusBarH), col, active || hovered ? 3.0f : 2.0f);
		const float cy = Y + kTopBarH + (H - kTopBarH - kStatusBarH) * 0.5f;
		fg->AddRectFilled(ImVec2(x - 4, cy - 24), ImVec2(x + 4, cy + 24), col, 4.0f);
		for (int i = -1; i <= 1; i++) fg->AddLine(ImVec2(x - 2, cy + i * 7.0f), ImVec2(x + 2, cy + i * 7.0f), IM_COL32(26, 45, 20, 230), 1.0f);
	};
	if (cfg_.showSidePanel) splitter(2, X + W - sideW, cfg_.sidePanelWidth, -1.0f, kDefaultSideW);
	drawModals();
	drawTutorial();
	drawRadialMenu();
	handleShortcuts();

	// Nombre del cambio en curso, para el historial.
	if (viewportHovered_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		ed_.label = tool_ != Tool::Move                            ? toolName(tool_)
		          : transformMode_ == TransformMode::Rotate ? tr("Girar")
		          : transformMode_ == TransformMode::Scale  ? tr("Escalar")
		                                                    : tr("Mover");
	} else if (ImGui::IsAnyItemActive()) {
		ed_.label = tr("Ajustes");
	}
	ed_.commit(ImGui::IsAnyItemActive() || dragging_ || drawingWall_ || ImGui::IsMouseDown(ImGuiMouseButton_Left));
	autosave();
}

void App::drawTopBar(const Rect& r)
{
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
	ImGui::Begin("##top", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 wp = ImGui::GetWindowPos();
	// Barra de aplicación sobria: las acciones destacan sobre una superficie
	// neutra y una línea Nectar identifica el producto sin teñir todo el editor.
	dl->AddRectFilled(wp, ImVec2(wp.x + r.w, wp.y + r.h), IM_COL32(24, 28, 25, 255));
	// Línea inferior del color del modo (como en la pantalla de inicio).
	const unsigned modes = ed_.doc.modes;
	const ImU32 modeCol  = modes == nectar::MAPMODE_VS ? IM_COL32(214, 92, 74, 255) : modes == nectar::MAPMODE_SOLO ? IM_COL32(90, 140, 220, 255)
	                     : modes == nectar::MAPMODE_COOP ? IM_COL32(110, 190, 90, 255) : IM_COL32(123, 155, 56, 255);
	dl->AddRectFilled(ImVec2(wp.x, wp.y + r.h - 3), ImVec2(wp.x + r.w, wp.y + r.h), modeCol);
	drawSprout(dl, ImVec2(wp.x + 28, wp.y + 30), 0.86f);
	dl->AddText(ImVec2(wp.x + 49, wp.y + 12), IM_COL32(232, 235, 220, 255), "NECTAR");
	dl->AddText(ImVec2(wp.x + 49, wp.y + 33), IM_COL32(139, 153, 126, 255), "MAP MAKER");

	ImGui::SetCursorPos(ImVec2(168, 13));
	// Editando el nivel de una zona: volver a su campaña (guardando el nivel).
	if (!editingZoneOf_.empty()) {
		if (ImGui::Button(tr("< Campaña"), ImVec2(104, 34))) {
			if (ed_.dirty && !ed_.path.empty()) saveMap(ed_.path);
			openCampaign(editingZoneOf_);
		}
		ImGui::SetItemTooltip("%s", tr("Guarda este nivel y vuelve al mapa de la campaña"));
		ImGui::SameLine();
	}
	// Archivo: nuevo, abrir, guardar y exportar, en un menú.
	if (ImGui::Button((std::string(tr("Archivo")) + "  v").c_str(), ImVec2(112, 34))) ImGui::OpenPopup("##file_menu");
	ImGui::SetItemTooltip("%s", tr("Nuevo, abrir, guardar y exportar"));
	if (ImGui::BeginPopup("##file_menu")) {
		if (ImGui::MenuItem(tr("Nuevo..."))) openNew_ = true;
		if (ImGui::MenuItem(tr("Abrir..."))) {
			refreshOpenList();
			openOpen_ = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(tr("Guardar"), "Ctrl+S")) saveOrAsk(false);
		if (ImGui::MenuItem(tr("Guardar como..."))) {
			copyStr(saveName_, sizeof(saveName_), ed_.doc.name);
			testAfterSave_ = false;
			openSaveAs_    = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(tr("Exportar para compartir (.nectarpack)"))) {
			if (ed_.path.empty() || ed_.dirty) saveOrAsk(false);
			if (!ed_.path.empty() && !ed_.dirty) exportPackOf(fs::path(ed_.path).parent_path().string());
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine(0, 14);
	// Deshacer y rehacer, como iconos (flecha curva).
	auto arrowButton = [&](const char* id, bool redo, bool enabled, const char* tip) {
		ImGui::BeginDisabled(!enabled);
		const bool pressed = ImGui::Button(id, ImVec2(40, 34));
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", tip);
		const ImVec2 a = ImGui::GetItemRectMin(), bb = ImGui::GetItemRectMax();
		const ImVec2 c((a.x + bb.x) * 0.5f, (a.y + bb.y) * 0.5f + 3);
		const ImU32 col = enabled ? IM_COL32(230, 232, 214, 255) : IM_COL32(105, 112, 100, 255);
		const float rr  = 8.0f;
		dl->PathArcTo(c, rr, 3.14159f, 6.28318f, 16);
		dl->PathStroke(col, 0, 2.2f);
		const float ex = redo ? c.x + rr : c.x - rr;
		dl->AddTriangleFilled(ImVec2(ex - 4.5f, c.y - 1), ImVec2(ex + 4.5f, c.y - 1), ImVec2(ex, c.y + 5), col);
		return pressed;
	};
	if (arrowButton("##undo", false, ed_.canUndo(), tr("Deshacer (Ctrl+Z)"))) ed_.undo();
	ImGui::SameLine(0, 4);
	if (arrowButton("##redo", true, ed_.canRedo(), tr("Rehacer (Ctrl+Y)"))) ed_.redo();
	ImGui::SameLine(0, 4);
	drawHistoryMenu();
	// A la derecha: comprobaciones, vista, ayuda y probar.
	const float testW = 170, arrowW = 30, helpW = 82, viewW = 78;
	int blocking = 0;
	for (const Issue& i : issues()) blocking += i.blocking ? 1 : 0;
	const char* readyText = blocking ? (blocking == 1 ? tr("1 cosa por arreglar") : tr("%d cosas por arreglar")) : tr("Listo para jugar");
	const float readyW    = ImGui::CalcTextSize(readyText).x + 54;
	const float rightX    = r.w - testW - arrowW - helpW - viewW - readyW - 50;
	const float mapX = ImGui::GetItemRectMax().x - wp.x + 18.0f;
	if (mapX < rightX - 160.0f) {
		ImGui::SameLine(0, 18);
		ImGui::SetCursorPosY(13);
		// El nombre (se cambia aquí mismo) y sus modos, con el color de cada uno.
		char nameBuf[128];
		copyStr(nameBuf, sizeof(nameBuf), ed_.doc.name);
		ImGui::SetNextItemWidth(std::max(120.0f, std::min(280.0f, rightX - mapX - 140.0f)));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.11f, 0.13f, 0.11f, 1));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9, (34 - ImGui::GetFontSize()) * 0.5f));
		if (ImGui::InputText("##mapname", nameBuf, sizeof(nameBuf))) {
			ed_.doc.name = nameBuf;
			ed_.touch();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("%s", tr("Nombre del mapa: pulsa para cambiarlo"));
		if (ed_.dirty) {
			ImGui::SameLine(0, 6);
			ImGui::SetCursorPosY(20);
			ImGui::TextColored(kWarn, "*");
			ImGui::SetItemTooltip("%s", tr("Cambios sin guardar"));
		}
		ImGui::SetCursorPosY(20);
		const struct {
			unsigned mode;
			const char* label;
			ImVec4 col;
		} chips[] = { { nectar::MAPMODE_VS, "VS", ImVec4(0.84f, 0.36f, 0.29f, 1) }, { nectar::MAPMODE_SOLO, "SOLO", ImVec4(0.35f, 0.55f, 0.86f, 1) },
			          { nectar::MAPMODE_COOP, "COOP", ImVec4(0.43f, 0.75f, 0.35f, 1) } };
		for (const auto& c : chips) {
			if (!(modes & c.mode)) continue;
			ImGui::SameLine(0, 10);
			ImGui::TextColored(c.col, "%s", c.label);
		}
	}
	ImGui::SameLine(rightX);
	ImGui::SetCursorPosY(13);
	drawReadyPill();
	ImGui::SameLine(0, 10);
	if (ImGui::Button(tr("Vista"), ImVec2(viewW, 34))) ImGui::OpenPopup("##view_menu");
	if (ImGui::BeginPopup("##view_menu")) {
		bool changed = false;
		changed |= ImGui::Checkbox(tr("Panel de herramientas"), &cfg_.showToolbox);
		changed |= ImGui::Checkbox(tr("Panel de propiedades"), &cfg_.showSidePanel);
		ImGui::SeparatorText(tr("Capas del visor"));
		ImGui::Checkbox(tr("Colisión compilada"), &showCollision_);
		ImGui::Checkbox(tr("Rutas de los Pikmin"), &showRoutes_);
		ImGui::Checkbox(tr("Pendientes no transitables"), &showSlopesOverlay_);
		ImGui::Checkbox(tr("Superficie del agua"), &showWaterOverlay_);
		ImGui::Checkbox(tr("Nombres de elementos"), &showLabels_);
		ImGui::Checkbox(tr("Minimapa (abajo a la derecha)"), &showMinimap_);
		ImGui::Separator();
		if (ImGui::MenuItem(tr("Restablecer disposición"))) {
			cfg_.toolboxWidth  = kDefaultToolboxW;
			cfg_.sidePanelWidth = kDefaultSideW;
			cfg_.showToolbox   = true;
			cfg_.showSidePanel = true;
			changed            = true;
		}
		if (changed) cfg_.save();
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(tr("Ayuda"), ImVec2(helpW, 34))) openHelp_ = true;
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.59f, 0.20f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.70f, 0.27f, 1));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.07f, 0.11f, 0.04f, 1));
	if (ImGui::Button(tr("Probar en el juego"), ImVec2(testW, 34))) {
		// Con varios modos, se elige en cuál.
		const unsigned m = ed_.doc.modes;
		if ((m & (m - 1)) != 0) ImGui::OpenPopup("##test_mode");
		else testInGame();
	}
	const ImVec2 tmin = ImGui::GetItemRectMin(), tmax = ImGui::GetItemRectMax();
	testButtonRect_   = { tmin.x, tmin.y, tmax.x - tmin.x, tmax.y - tmin.y };
	ImGui::SameLine(0, 2);
	if (ImGui::Button("v##test_more", ImVec2(arrowW, 34))) ImGui::OpenPopup("##test_more");
	ImGui::PopStyleColor(3);
	ImGui::SetItemTooltip("%s", tr("Más formas de probar"));
	if (ImGui::BeginPopup("##test_more")) {
		if (ImGui::Selectable(tr("Probar desde el principio (F5)"))) testInGame();
		ImGui::BeginDisabled(!ed_.doc.story.present || !(ed_.doc.modes & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP)));
		if (ImGui::Selectable(tr("Probar desde donde mira la cámara (Mayús+F5)"))) testInGame(true);
		ImGui::EndDisabled();
		ImGui::TextColored(kDim, "%s", tr("Desde la cámara: en solo y coop, el capitán empieza en el centro de la vista."));
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##test_mode")) {
		const struct {
			unsigned mode;
			const char* label;
		} modes[] = { { nectar::MAPMODE_VS, tr("VS (2 jugadores)") }, { nectar::MAPMODE_SOLO, tr("Solo (1 jugador)") }, { nectar::MAPMODE_COOP, tr("Coop (2 jugadores)") } };
		for (const auto& m : modes) {
			if (!(ed_.doc.modes & m.mode)) continue;
			if (ImGui::Selectable(m.label, testMode_ == m.mode)) {
				testMode_ = m.mode;
				testInGame();
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SetItemTooltip("%s", tr("Guarda el mapa y lo abre en el juego para jugarlo (F5)"));
	ImGui::End();
}

void App::drawToolbox(const Rect& r)
{
	// Columna de iconos (como la barra de herramientas de Blender): el nombre
	// y su tecla salen al pasar el ratón; las opciones van en la fila de
	// encima del visor y, las detalladas, en la pestaña «Herramienta».
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 10));
	ImGui::Begin("##tools", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();
	toolboxRect_ = { r.x, r.y, r.w, r.h };
	struct T {
		Tool tool;
		const char* label;
		const char* tip;
		bool terrain;
		Icon icon;
		int key; ///< tecla numérica (-1 = ninguna)
	};
	const T tools[] = { { Tool::Move, tr("Mover y girar"), tr("Coge cosas del mapa y arrástralas"), false, Icon::Move, 1 },
		                { Tool::Floor, tr("Dibujar suelo"), tr("Da forma al mapa: añade suelo o haz huecos"), true, Icon::Floor, 3 },
		                { Tool::Height, tr("Montañas y hoyos"), tr("Sube y baja el terreno con suavidad"), true, Icon::Height, 4 },
		                { Tool::Plateau, tr("Elevar o hundir zona"), tr("Plataformas y fosos planos, con bordes de acantilado"), true, Icon::Plateau, 5 },
		                { Tool::Ramp, tr("Rampas"), tr("Une dos alturas con una pendiente transitable"), true, Icon::Ramp, 9 },
		                { Tool::Water, tr("Agua"), tr("Pinta lagos (solo los Pikmin azules entran)"), true, Icon::Water, 7 },
		                { Tool::Paint, tr("Pintar el suelo"), tr("Cambia el aspecto del suelo"), true, Icon::Paint, 6 },
		                { Tool::Trace, tr("Trazos"), tr("Caminos, ríos y calzadas por puntos, o zonas de suelo con contorno a mano"), true, Icon::Trace, -1 },
		                { Tool::Place, tr("Poner cosas"), tr("Piezas, pastillas, enemigos y decoración"), false, Icon::Place, 2 },
		                { Tool::Scatter, tr("Decorar"), tr("Esparce hierbas, flores y piedras pintando"), false, Icon::Decor, 0 },
		                { Tool::Walls, tr("Muros"), tr("Dibuja muros arrastrando"), false, Icon::Walls, 8 } };
	// Nombre corto bajo el icono (como en Roblox Studio) y el grupo encima.
	const char* shortLabels[] = { tr("Mover"), tr("Suelo"), tr("Relieve"), tr("Zona"), tr("Rampa"), tr("Agua"), tr("Pintar"), tr("Trazos"), tr("Poner"), tr("Decorar"), tr("Muros") };
	// Por grupos: elegir, dar forma al terreno y poner cosas.
	const int groupStart[]     = { 0, 1, 8 };
	const char* groupLabels[]  = { tr("ELEGIR"), tr("TERRENO"), tr("PONER") };
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float bw = r.w - 16.0f, bh = 52.0f;
	const float smallFs = ImGui::GetFontSize() * 0.68f;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
	for (int k = 0; k < 11; k++) {
		for (int g = 0; g < 3; g++) {
			if (k != groupStart[g]) continue;
			const ImVec2 p = ImGui::GetCursorScreenPos();
			if (g) {
				dl->AddLine(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + bw - 6, p.y + 6), IM_COL32(80, 96, 70, 255), 1.0f);
				ImGui::Dummy(ImVec2(bw, 9));
			}
			const ImVec2 q = ImGui::GetCursorScreenPos();
			const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, groupLabels[g]);
			dl->AddText(nullptr, smallFs, ImVec2(q.x + (bw - ts.x) * 0.5f, q.y), IM_COL32(140, 160, 110, 255), groupLabels[g]);
			ImGui::Dummy(ImVec2(bw, smallFs + 3));
		}
		const T& t = tools[k];
		ImGui::PushID(k);
		const bool on     = tool_ == t.tool;
		const bool locked = courseMode() && (t.terrain || t.tool == Tool::Walls || t.tool == Tool::Scatter);
		if (locked) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
		if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.36f, 0.47f, 0.19f, 1));
		const bool pressed = ImGui::Button("##tool", ImVec2(bw, bh));
		if (on) ImGui::PopStyleColor();
		if (locked) ImGui::PopStyleVar();
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		if (on) dl->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y), IM_COL32(183, 207, 75, 255), 2.0f);
		const ImU32 fg = on ? IM_COL32(255, 255, 240, 255) : ImGui::GetColorU32(ImGuiCol_Text);
		drawIcon(dl, ImVec2((a.x + b.x) * 0.5f, a.y + 19.0f), 12.0f, t.icon, fg);
		{
			const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(smallFs, FLT_MAX, 0.0f, shortLabels[k]);
			dl->PushClipRect(ImVec2(a.x + 2, a.y), ImVec2(b.x - 2, b.y), true);
			dl->AddText(nullptr, smallFs, ImVec2((a.x + b.x - ts.x) * 0.5f, b.y - smallFs - 5.0f), on ? fg : IM_COL32(190, 198, 180, 255), shortLabels[k]);
			dl->PopClipRect();
		}
		if (t.key >= 0) {
			char key[4];
			std::snprintf(key, sizeof(key), "%d", t.key);
			dl->AddText(nullptr, ImGui::GetFontSize() * 0.62f, ImVec2(b.x - 9.0f, a.y + 2.0f), IM_COL32(130, 138, 120, 255), key);
		}
		if (t.key >= 0) ImGui::SetItemTooltip("%s  (%d)\n%s", t.label, t.key, t.tip);
		else ImGui::SetItemTooltip("%s\n%s", t.label, t.tip);
		if (pressed) {
			if (courseMode() && (t.tool == Tool::Walls || t.tool == Tool::Scatter)) notify(1, tr("En los escenarios del juego todavía no se pueden poner muros ni decoración."));
			else if (!t.terrain || ensureTerrain()) {
				tool_ = t.tool;
				if (tool_ != Tool::Ramp) drawingRamp_ = false;
			}
		}
		if (t.tool == Tool::Floor) floorButtonRect_ = { a.x, a.y, b.x - a.x, b.y - a.y };
		if (t.tool == Tool::Place) placeButtonRect_ = { a.x, a.y, b.x - a.x, b.y - a.y };
		ImGui::PopID();
	}
	ImGui::PopStyleVar();
	ImGui::End();
}

void App::drawToolHeader(const Rect& r)
{
	// Fila de opciones de la herramienta, sobre el visor (como la de Blender):
	// lo que se cambia a menudo. Lo detallado, en «Más opciones».
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
	ImGui::Begin("##toolrow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), IM_COL32(30, 35, 31, 255));
	dl->AddLine(ImVec2(r.x, r.y + r.h - 1), ImVec2(r.x + r.w, r.y + r.h - 1), IM_COL32(60, 72, 58, 255));
	const float H = 30.0f;
	auto next     = [&](float spacing) {
        ImGui::SameLine(0, spacing);
        ImGui::SetCursorPosY((r.h - H) * 0.5f);
	};
	// Nombre de la herramienta.
	ImGui::SetCursorPosY((r.h - ImGui::GetFontSize()) * 0.5f);
	ImGui::TextColored(ImVec4(0.90f, 0.87f, 0.62f, 1), "%s", toolName(tool_));
	ImGui::SameLine(0, 18);
	ImGui::SetCursorPosY((r.h - H) * 0.5f);
	auto seg = [&](const char* label, bool on, float w = 0.0f) {
		const bool p = choiceButton(label, on, ImVec2(w > 0 ? w : ImGui::CalcTextSize(label).x + 22, H));
		next(4);
		return p;
	};
	auto gap = [&] {
		next(14);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddLine(ImVec2(p.x - 7, r.y + 9), ImVec2(p.x - 7, r.y + r.h - 9), IM_COL32(70, 84, 62, 255), 1.0f);
	};
	auto slider = [&](const char* id, float* v, float lo, float hi, const char* fmt, const char* tip, float w = 150) {
		ImGui::SetNextItemWidth(w);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, (H - ImGui::GetFontSize()) * 0.5f));
		ImGui::SliderFloat(id, v, lo, hi, fmt);
		ImGui::PopStyleVar();
		if (tip) ImGui::SetItemTooltip("%s", tip);
		next(4);
	};
	auto check = [&](const char* label, bool* v, const char* tip) {
		ImGui::SetCursorPosY((r.h - ImGui::GetFrameHeight()) * 0.5f);
		const bool c = ImGui::Checkbox(label, v);
		if (tip) ImGui::SetItemTooltip("%s", tip);
		next(10);
		return c;
	};
	auto brush = [&] {
		gap();
		slider("##brush", &brushRadius_, 25, 1000, tr("Pincel %.0f"), tr("Tamaño del pincel ([ y ], o Ctrl + rueda)"), 130);
		if (seg(tr("Círculo"), brushShape_ == BrushShape::Circle)) brushShape_ = BrushShape::Circle;
		if (seg(tr("Cuadrado"), brushShape_ == BrushShape::Square)) brushShape_ = BrushShape::Square;
		gap();
		if (seg(tr("Suave"), brushFalloff_ == BrushFalloff::Smooth)) brushFalloff_ = BrushFalloff::Smooth;
		if (seg(tr("Duro"), brushFalloff_ == BrushFalloff::Hard)) brushFalloff_ = BrushFalloff::Hard;
	};
	switch (tool_) {
	case Tool::Move:
		if (seg(tr("Mover (W)"), transformMode_ == TransformMode::Move)) transformMode_ = TransformMode::Move;
		if (seg(tr("Girar (E)"), transformMode_ == TransformMode::Rotate)) transformMode_ = TransformMode::Rotate;
		if (seg(tr("Escalar (R)"), transformMode_ == TransformMode::Scale)) transformMode_ = TransformMode::Scale;
		gap();
		if (check(tr("Cuadrícula"), &cfg_.snap, tr("Encajar en la cuadrícula"))) cfg_.save();
		if (check(tr("Ejes locales"), &cfg_.localGizmo, tr("Orienta X/Z según el último objeto seleccionado, en vez de usar los ejes del mapa."))) cfg_.save();
		if (check(tr("Ignorar decoración"), &cfg_.ignoreDecorSelection, tr("Permite elegir objetivos y construcciones a través de plantas y adornos."))) cfg_.save();
		ImGui::BeginDisabled(!selectionCanGround());
		if (ImGui::Button(tr("Al suelo"), ImVec2(0, H))) placeSelectionOnGround();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", tr("Baja al suelo la decoración y los objetos seleccionados que estén flotando o enterrados."));
		next(4);
		break;
	case Tool::Place:
		if (seg(catalogOpen_ ? tr("Cerrar catálogo") : tr("Catálogo (C)"), catalogOpen_)) catalogOpen_ = !catalogOpen_;
		gap();
		if (check(tr("Cuadrícula"), &cfg_.snap, tr("Encajar en la cuadrícula"))) cfg_.save();
		break;
	case Tool::Floor:
		if (seg(tr("Añadir suelo"), !floorErase_)) floorErase_ = false;
		if (seg(tr("Hacer hueco"), floorErase_)) floorErase_ = true;
		brush();
		break;
	case Tool::Height: {
		const char* ops[] = { tr("Subir"), tr("Bajar"), tr("Allanar"), tr("Suavizar"), tr("Ruido"), tr("Colina"), tr("Cráter"), tr("Meseta") };
		int op            = int(heightOp_);
		ImGui::SetNextItemWidth(140);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, (H - ImGui::GetFontSize()) * 0.5f));
		if (ImGui::Combo("##heightop", &op, ops, 8)) heightOp_ = HeightOp(op);
		ImGui::PopStyleVar();
		next(4);
		slider("##strength", &brushStrength_, 20, 600, tr("Fuerza %.0f"), nullptr, 120);
		brush();
		break;
	}
	case Tool::Plateau: {
		if (seg(tr("Elevar"), plateauUp_)) plateauUp_ = true;
		if (seg(tr("Hundir"), !plateauUp_)) plateauUp_ = false;
		gap();
		const struct {
			const char* label;
			float amount;
		} amounts[] = { { tr("Poco"), 50 }, { tr("Medio"), 100 }, { tr("Mucho"), 200 }, { tr("Muchísimo"), 400 } };
		for (const auto& a : amounts) {
			if (seg(a.label, plateauAmount_ == a.amount)) plateauAmount_ = a.amount;
		}
		brush();
		break;
	}
	case Tool::Paint: {
		// El suelo con el que se pinta (los demás, en «Más opciones»).
		nectar::DocTerrain& g = ed_.doc.terrain;
		if (!g.palette.empty()) {
			paintMaterial_   = std::max(0, std::min(paintMaterial_, int(g.palette.size()) - 1));
			const unsigned t = swatch(g.palette[std::size_t(paintMaterial_)].source, g.palette[std::size_t(paintMaterial_)].index);
			if (t ? ImGui::ImageButton("##paintmat", ImTextureID(std::intptr_t(t)), ImVec2(H - 6, H - 6)) : ImGui::Button("?", ImVec2(H, H))) focusToolTab_ = true;
			ImGui::SetItemTooltip("%s", tr("El suelo con el que pintas: pulsa para elegir otro"));
			next(4);
		}
		brush();
		break;
	}
	case Tool::Water:
		if (seg(tr("Poner agua"), !waterErase_)) waterErase_ = false;
		if (seg(tr("Quitar agua"), waterErase_)) waterErase_ = true;
		brush();
		break;
	case Tool::Walls:
		if (seg(tr("Bajo"), wallHeight_ == 60)) wallHeight_ = 60;
		if (seg(tr("Medio"), wallHeight_ == 150)) wallHeight_ = 150;
		if (seg(tr("Alto"), wallHeight_ == 300)) wallHeight_ = 300;
		gap();
		slider("##thick", &wallThickness_, 10, 200, tr("Grosor %.0f"), nullptr, 130);
		break;
	case Tool::Ramp:
		ImGui::SetCursorPosY((r.h - ImGui::GetFontSize()) * 0.5f);
		ImGui::TextColored(drawingRamp_ ? kGood : kWarn, "%s", drawingRamp_ ? tr("Paso 2 de 2: clic en el extremo alto") : tr("Paso 1 de 2: clic en el extremo bajo"));
		ImGui::SameLine(0, 12);
		ImGui::SetCursorPosY((r.h - H) * 0.5f);
		slider("##rampw", &rampWidth_, 100, 1000, tr("Anchura %.0f"), nullptr, 140);
		if (seg(tr("Suavizado"), rampSmooth_)) rampSmooth_ = true;
		if (seg(tr("Lineal"), !rampSmooth_)) rampSmooth_ = false;
		if (drawingRamp_ && seg(tr("Cancelar"), false)) drawingRamp_ = false;
		break;
	case Tool::Scatter:
		if (seg(tr("Poner"), !scatterErase_)) scatterErase_ = false;
		if (seg(tr("Quitar"), scatterErase_)) scatterErase_ = true;
		gap();
		slider("##density", &scatterDensity_, 1, 30, tr("Cuánta %.0f"), nullptr, 120);
		brush();
		break;
	case Tool::Trace: {
		const char* modes[] = { tr("Camino"), tr("Río"), tr("Calzada"), tr("Zona de suelo"), tr("Hueco"), tr("Unir caminos"), tr("Sin caminos") };
		int m               = int(traceMode_);
		ImGui::SetNextItemWidth(150);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, (H - ImGui::GetFontSize()) * 0.5f));
		if (ImGui::Combo("##tracemode", &m, modes, 7)) traceMode_ = TraceMode(m);
		ImGui::PopStyleVar();
		next(4);
		const bool line = traceMode_ == TraceMode::Path || traceMode_ == TraceMode::River || traceMode_ == TraceMode::Causeway;
		if (line) slider("##tracew", &traceWidth_, 30, 600, tr("Anchura %.0f"), nullptr, 130);
		gap();
		const std::size_t need = line || traceMode_ == TraceMode::RouteLink ? 2 : 3;
		ImGui::BeginDisabled(traceNodes_.size() < need);
		if (seg(tr("Aplicar (Intro)"), false)) applyTrace();
		ImGui::EndDisabled();
		if (seg(tr("Borrar puntos"), false)) traceNodes_.clear();
		break;
	}
	}
	// A la derecha: lo detallado.
	const char* more = tr("Más opciones");
	const float moreW = ImGui::CalcTextSize(more).x + 26;
	if (ImGui::GetCursorPosX() < r.w - moreW - 12) ImGui::SameLine(r.w - moreW - 10);
	else ImGui::SameLine(0, 10);
	ImGui::SetCursorPosY((r.h - H) * 0.5f);
	if (ImGui::Button(more, ImVec2(moreW, H))) focusToolTab_ = true;
	ImGui::SetItemTooltip("%s", tr("Todas las opciones de la herramienta, en el panel de la derecha"));
	ImGui::End();
}

void App::drawToolOptions()
{
	switch (tool_) {
	case Tool::Move: {
		ImGui::SeparatorText(tr("Mover y girar"));
		const float modeW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
		if (choiceButton(tr("Mover (W)"), transformMode_ == TransformMode::Move, ImVec2(modeW, 32))) transformMode_ = TransformMode::Move;
		ImGui::SameLine();
		if (choiceButton(tr("Girar (E)"), transformMode_ == TransformMode::Rotate, ImVec2(modeW, 32))) transformMode_ = TransformMode::Rotate;
		ImGui::SameLine();
		if (choiceButton(tr("Escalar (R)"), transformMode_ == TransformMode::Scale, ImVec2(modeW, 32))) transformMode_ = TransformMode::Scale;
		hint(tr("Haz clic en algo para elegirlo y arrástralo por el suelo."));
		ImGui::TextDisabled("(?) %s", tr("Más atajos"));
		ImGui::SetItemTooltip("%s\n%s\n%s", tr("Con Ctrl eliges varias cosas a la vez; arrastrando en vacío, todas las de un recuadro."),
		                      tr("W mueve por los ejes, E gira con el aro y R cambia el tamaño de la decoración."),
		                      tr("Alt + clic recorre las cosas superpuestas. Q gira por pasos. Supr borra. Ctrl+D duplica."));
		if (ImGui::Checkbox(tr("Ejes locales"), &cfg_.localGizmo)) cfg_.save();
		ImGui::SetItemTooltip("%s", tr("Orienta X/Z según el último objeto seleccionado, en vez de usar los ejes del mapa."));
		ImGui::BeginDisabled(!selectionCanGround());
		if (ImGui::Button(tr("Colocar sobre el terreno"), ImVec2(-1, 32))) placeSelectionOnGround();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", tr("Baja al suelo la decoración y los objetos seleccionados que estén flotando o enterrados."));
		if (ImGui::Checkbox(tr("Encajar en la cuadrícula"), &cfg_.snap)) cfg_.save();
		ImGui::SetItemTooltip(tr("Las cosas se colocan alineadas, a saltos de %.0f."), cfg_.snapStep);
		if (ImGui::Checkbox(tr("Ignorar decoración al seleccionar"), &cfg_.ignoreDecorSelection)) cfg_.save();
		ImGui::SetItemTooltip("%s", tr("Permite elegir objetivos y construcciones a través de plantas y adornos."));
		break;
	}
	case Tool::Place:
		if (catalogOpen_) {
			ImGui::SeparatorText(tr("Poner cosas"));
			hint(tr("El catálogo está abierto abajo, a lo ancho del mapa."));
			if (ImGui::Button(tr("Traerlo aquí (C)"), ImVec2(-1, 32))) catalogOpen_ = false;
		} else {
			if (ImGui::Button(tr("Abrir el catálogo grande (C)"), ImVec2(-1, 30))) catalogOpen_ = true;
			drawPlaceOptions();
		}
		break;
	case Tool::Floor: drawFloorOptions(); break;
	case Tool::Height: drawHeightOptions(); break;
	case Tool::Plateau: drawPlateauOptions(); break;
	case Tool::Paint: drawPaintOptions(); break;
	case Tool::Water: drawWaterOptions(); break;
	case Tool::Walls: drawWallOptions(); break;
	case Tool::Ramp: drawRampOptions(); break;
	case Tool::Scatter: drawScatterOptions(); break;
	case Tool::Trace: drawTraceOptions(); break;
	}
}

void App::drawPlaceOptions()
{
	ImGui::SeparatorText(tr("¿Qué quieres poner?"));
	hint(tr("Elige algo y haz clic en el mapa. Puedes poner varios seguidos."));
	auto choose = [&](const PlaceChoice& c) {
		place_ = c;
		if (c.type != ItemType::Prop || c.asset.empty()) return;
		cfg_.recents.erase(std::remove(cfg_.recents.begin(), cfg_.recents.end(), c.asset), cfg_.recents.end());
		cfg_.recents.insert(cfg_.recents.begin(), c.asset);
		if (cfg_.recents.size() > 16) cfg_.recents.resize(16);
		cfg_.save();
	};
	auto favorite = [&](const std::string& asset) { return std::find(cfg_.favorites.begin(), cfg_.favorites.end(), asset) != cfg_.favorites.end(); };
	auto toggleFavorite = [&](const std::string& asset) {
		auto it = std::find(cfg_.favorites.begin(), cfg_.favorites.end(), asset);
		if (it == cfg_.favorites.end()) cfg_.favorites.push_back(asset);
		else cfg_.favorites.erase(it);
		cfg_.save();
	};
	// Rejilla de miniaturas de modelos del juego.
	auto grid = [&](const char* id, const std::vector<std::pair<PlaceChoice, std::string>>& items, const char* tipFmt) {
		ImGui::PushID(id);
		int col            = 0;
		const float cardW  = 88.0f, gap = 6.0f;
		const int perRow   = std::max(2, int((ImGui::GetContentRegionAvail().x + gap) / (cardW + gap)));
		for (const auto& it : items) {
			const PlaceChoice& c = it.first;
			ImGui::PushID(c.label.c_str());
			const unsigned t = assetThumb(placeAsset(c));
			const bool on    = place_.type == c.type && place_.label == c.label;
			// Nombre (la primera línea de la descripción) debajo de la miniatura.
			const std::string name = it.second.substr(0, it.second.find('\n'));
			const bool pressed     = thumbCard(t, name, on, cardW);
			beginPlaceDrag(c, t, name);
			if (pressed) choose(c);
			ImGui::SetItemTooltip(tipFmt, it.second.c_str());
			ImGui::PopID();
			if (++col < perRow) ImGui::SameLine(0, gap);
			else col = 0;
		}
		if (col) ImGui::NewLine();
		ImGui::PopID();
	};

	if (ed_.doc.story.present && iconHeader(tr("Misión: piezas para la nave"), Icon::Parts, ImGuiTreeNodeFlags_DefaultOpen)) {
		hint(tr("En solo y coop: hay que llevarlas a la nave. Cada pieza solo puede estar una vez."));
		std::vector<std::pair<PlaceChoice, std::string>> parts;
		for (const nectar::ShipPartInfo& p : nectar::shipParts()) {
			bool used = false;
			for (const nectar::DocShipPart& sp : ed_.doc.story.parts) used = used || sp.id == p.id;
			if (used) continue;
			char carry[96], text[160];
			std::snprintf(carry, sizeof(carry), tr("La levantan %d Pikmin (más rápido con %d)"), p.carryMin, p.carryMax);
			std::snprintf(text, sizeof(text), "%s\n%s", tr(p.label), carry);
			parts.push_back({ { ItemType::ShipPart, "", p.id }, text });
		}
		grid("storyparts", parts, "%s");
	}
	if (ed_.doc.story.present && iconHeader(tr("Misión: cebollas"), Icon::Pellets)) {
		hint(tr("En solo y coop: una de cada color como mucho. Hace falta la de cada color de Pikmin del principio."));
		const char* colours[] = { tr("Azul"), tr("Roja"), tr("Amarilla") };
		for (int c = 0; c < 3; c++) {
			bool used = false;
			for (const nectar::DocStoryOnion& o : ed_.doc.story.onions) used = used || o.color == c;
			if (c) ImGui::SameLine();
			ImGui::BeginDisabled(used);
			const std::string id = std::to_string(c);
			if (choiceButton(colours[c], place_.type == ItemType::StoryOnion && place_.label == id, ImVec2(90, 32))) place_ = { ItemType::StoryOnion, "", id };
			ImGui::EndDisabled();
		}
	}
	if ((ed_.doc.modes & nectar::MAPMODE_VS) && iconHeader(tr("Piezas de la nave"), Icon::Parts, ImGuiTreeNodeFlags_DefaultOpen)) {
		hint(tr("Lo que hay que llevar al cohete. Las grandes necesitan muchos Pikmin y dan más puntos."));
		std::vector<std::pair<PlaceChoice, std::string>> parts;
		for (const nectar::ShipPartInfo& p : nectar::shipParts()) {
			char carry[96], text[160];
			std::snprintf(carry, sizeof(carry), tr("La levantan %d Pikmin (más rápido con %d)"), p.carryMin, p.carryMax);
			std::snprintf(text, sizeof(text), "%s\n%s", tr(p.label), carry);
			parts.push_back({ { ItemType::Piece, "", p.id }, text });
		}
		grid("parts", parts, "%s");
	}
	if (iconHeader(tr("Pastillas"), Icon::Pellets)) {
		static int colour = 0, value = 1;
		const char* colours[] = { tr("Roja"), tr("Azul"), tr("Amarilla") };
		for (int c = 0; c < 3; c++) {
			if (c) ImGui::SameLine();
			if (choiceButton(colours[c], colour == c, ImVec2(80, 0))) colour = c;
		}
		const int values[] = { 1, 5, 10, 20 };
		for (int i = 0; i < 4; i++) {
			if (i) ImGui::SameLine();
			char lbl[8];
			std::snprintf(lbl, sizeof(lbl), "%d", values[i]);
			if (choiceButton(lbl, value == values[i], ImVec2(58, 0))) value = values[i];
		}
		const std::string pid = pelletId(colour, value);
		const bool on         = place_.type == ItemType::Pellet && place_.label == pid;
		if (choiceButton((tr("Pastilla ") + std::string(colours[colour]) + tr(" de ") + std::to_string(value)).c_str(), on, ImVec2(-1, 32)))
			place_ = { ItemType::Pellet, "", pid };
		hint(tr("Al llevarla a la cebolla de su color salen Pikmin nuevos (tantos como su número)."));
	}
	if (iconHeader(tr("Enemigos"), Icon::Enemies)) {
		std::vector<std::pair<PlaceChoice, std::string>> list;
		for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
			if (e.boss >= 0 && e.boss <= 3) continue;
			list.push_back({ { ItemType::Enemy, "", e.id }, std::string(tr(e.label)) + "\n" + tr(e.hint) });
		}
		grid("enemies", list, "%s");
	}
	if (iconHeader(tr("Jefes"), Icon::Bosses)) {
		hint(tr("Enormes y muy peligrosos: mejor en mapas grandes."));
		std::vector<std::pair<PlaceChoice, std::string>> list;
		for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
			if (e.boss < 0 || e.boss > 3) continue;
			list.push_back({ { ItemType::Enemy, "", e.id }, std::string(tr(e.label)) + "\n" + tr(e.hint) });
		}
		grid("bosses", list, "%s");
	}
	if (iconHeader(tr("Construcciones"), Icon::Structures)) {
		hint(tr("Funcionan como en el juego: los Pikmin construyen puentes y rampas, empujan bloques y derriban puertas."));
		std::vector<std::pair<PlaceChoice, std::string>> list;
		for (const nectar::ObjectInfo& o : nectar::objectKinds()) list.push_back({ { ItemType::Object, "", o.id }, std::string(tr(o.label)) + "\n" + tr(o.hint) });
		grid("objects", list, "%s");
	}
	{
		const std::vector<GameGenChoice>& gens = gameGenCatalog(cfg_.dataDir());
		if (!gens.empty() && iconHeader(tr("Objetos del juego (como en la historia)"), Icon::More)) {
			hint(tr("Los mismos generadores que los niveles originales: el juego los crea y los recuerda de un día a otro. Al elegir uno, puedes cambiar su radio, cuántos salen o su giro."));
			std::string category;
			std::vector<std::pair<PlaceChoice, std::string>> list;
			auto flush = [&] {
				if (list.empty()) return;
				ImGui::TextColored(kDim, "%s", tr(category.c_str()));
				grid(category.c_str(), list, "%s");
				list.clear();
			};
			for (const GameGenChoice& c : gens) {
				if (c.category != category) {
					flush();
					category = c.category;
				}
				list.push_back({ { ItemType::Gen, "", c.key }, c.gen.label });
			}
			flush();
		}
	}

	if (courseMode()) {
		drawCutOptions();
		hint(tr("En los escenarios del juego todavía no se puede añadir decoración: el nivel ya trae la suya."));
		return;
	}
	if (iconHeader(tr("Piezas de los niveles del juego"), Icon::More)) {
		hint(tr("Latas, tocones, raíces, botellas, rocas... sacadas de los escenarios del juego (de tu copia: el mapa solo guarda de dónde)."));
		std::vector<std::pair<PlaceChoice, std::string>> list;
		for (const OfficialPiece& p : officialPieces()) list.push_back({ { ItemType::Prop, p.asset(), p.id }, tr(p.label) });
		grid("officialpieces", list, "%s");
	}
	if (!cfg_.pieces.empty() && iconHeader(tr("Mis piezas (recortes de escenarios)"), Icon::More)) {
		hint(tr("Trozos de los niveles del juego que has guardado (troncos, raíces, latas, rocas...). Se pisan y se ven igual que en el nivel. Con «Quitar» sale de la biblioteca."));
		std::vector<std::pair<PlaceChoice, std::string>> list;
		for (const auto& p : cfg_.pieces) list.push_back({ { ItemType::Prop, p.second, p.first }, p.first });
		grid("pieces", list, "%s");
		for (std::size_t i = 0; i < cfg_.pieces.size(); i++) {
			ImGui::PushID(int(i));
			if (ImGui::SmallButton(tr("Quitar"))) {
				cfg_.pieces.erase(cfg_.pieces.begin() + long(i));
				cfg_.save();
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(cfg_.pieces[i].first.c_str());
			ImGui::PopID();
		}
	}
	if (iconHeader(tr("Mallas propias (OBJ o DAE)"), Icon::More)) {
		hint(tr("Partes del nivel hechas en Blender u otro programa (.obj con su .mtl, o .dae, y texturas PNG o JPG): suelos, troncos, cuevas, puentes... Se guardan en la carpeta del mapa y viajan con él."));
		if (ed_.path.empty()) {
			hint(tr("Guarda el mapa primero: las mallas van en su carpeta."));
		} else {
			static float importScale = 1.0f;
			ImGui::SetNextItemWidth(160);
			ImGui::DragFloat(tr("Escala al importar"), &importScale, 0.1f, 0.01f, 1000.0f, "%.2f");
			if (ImGui::Button(tr("Importar OBJ o DAE..."))) {
				const fs::path p = pikmin::launcher::platform::askForFile(tr("Elige la malla"), tr("Malla OBJ o DAE"), "*.obj *.OBJ *.dae *.DAE");
				if (!p.empty()) {
					std::string asset, err;
					ObjInfo info;
					if (importObj(p.string(), mapDir(), importScale, asset, info, err)) {
						models_.erase(assetFile(asset));
						char buf[256];
						std::snprintf(buf, sizeof(buf), tr("Malla importada: %zu triángulos, mide %.0f × %.0f × %.0f. Haz clic en el mapa para ponerla."), info.triangles,
						              info.max.x - info.min.x, info.max.y - info.min.y, info.max.z - info.min.z);
						notify(info.warnings.empty() ? 0 : 1, std::string(buf) + (info.warnings.empty() ? "" : "\n" + info.warnings));
						place_ = { ItemType::Prop, asset, asset };
					} else {
						notify(2, tr("No se pudo importar: ") + err);
					}
				}
			}
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(fs::path(mapDir()) / "meshes", ec)) {
				if (e.path().extension() != ".mod") continue;
				const std::string asset = "map:meshes/" + e.path().filename().string();
				const bool on           = place_.type == ItemType::Prop && place_.asset == asset;
				if (ImGui::Selectable(e.path().stem().string().c_str(), on)) place_ = { ItemType::Prop, asset, asset };
			}
		}
	}
	{
		const ImVec2 at = ImGui::GetCursorScreenPos();
		drawIcon(ImGui::GetWindowDrawList(), ImVec2(at.x + 9.0f, at.y + ImGui::GetFontSize() * 0.55f), 8.0f, Icon::Decor, ImGui::GetColorU32(ImGuiCol_Text));
		ImGui::Indent(24.0f);
		ImGui::SeparatorText(tr("Decoración"));
		ImGui::Unindent(24.0f);
	}
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##buscar", tr("Buscar..."), search_, sizeof(search_));
	const std::string q = searchKey(search_);
	static int assetFilter = 0;
	static bool assetGrid = true;
	static int assetCategoryFilter = 0;
	static int assetCostFilter = 0;
	static bool assetCollisionOnly = false;
	if (choiceButton(tr("Todos"), assetFilter == 0, ImVec2(72, 0))) assetFilter = 0;
	ImGui::SameLine();
	if (choiceButton(tr("Favoritos"), assetFilter == 1, ImVec2(88, 0))) assetFilter = 1;
	ImGui::SameLine();
	if (choiceButton(tr("Recientes"), assetFilter == 2, ImVec2(88, 0))) assetFilter = 2;
	if (choiceButton(tr("Cuadrícula"), assetGrid, ImVec2(100, 0))) assetGrid = true;
	ImGui::SameLine();
	if (choiceButton(tr("Lista"), !assetGrid, ImVec2(80, 0))) assetGrid = false;
	const char* categoryLabels[] = { tr("Todas las categorías"), tr("Plantas"), tr("Objetos"), tr("Otros") };
	ImGui::SetNextItemWidth(-1);
	ImGui::Combo(tr("Categoría"), &assetCategoryFilter, categoryLabels, 4);
	const char* costLabels[] = { tr("Cualquier coste"), tr("Ligero"), tr("Medio") };
	ImGui::SetNextItemWidth(-1);
	ImGui::Combo(tr("Coste"), &assetCostFilter, costLabels, 3);
	ImGui::Checkbox(tr("Solo con colisión"), &assetCollisionOnly);
	auto catalogEntry = [&](const std::string& asset) -> const nectar::CatalogEntry* {
		for (const nectar::CatalogEntry& e : catalog_.entries) if (e.path == asset) return &e;
		return nullptr;
	};
	auto passesFilter = [&](const std::string& asset) {
		if (assetFilter == 1 && !favorite(asset)) return false;
		if (assetFilter == 2 && std::find(cfg_.recents.begin(), cfg_.recents.end(), asset) == cfg_.recents.end()) return false;
		const nectar::CatalogEntry* entry = catalogEntry(asset);
		const std::string category = entry ? entry->category : nectar::assetCategory(asset, nectar::AssetKind::Model);
		if (assetCategoryFilter == 1 && category != "plants") return false;
		if (assetCategoryFilter == 2 && category != "objects") return false;
		if (assetCategoryFilter == 3 && (category == "plants" || category == "objects")) return false;
		if (assetCollisionOnly && (!entry || entry->collisionTris <= 0)) return false;
		if (assetCostFilter == 1 && entry && entry->triangles > 1000) return false;
		if (assetCostFilter == 2 && entry && entry->triangles > 5000) return false;
		return true;
	};
	std::string group;
	int col = 0;
	for (const DecorItem& it : decorItems()) {
		std::string name = tr(it.name);
		const std::string haystack = searchKey(name + " " + it.path + " " + tr(it.group));
		if (!q.empty() && haystack.find(q) == std::string::npos) continue;
		if (!passesFilter(it.path)) continue;
		if (group != it.group) {
			if (col) ImGui::NewLine();
			col   = 0;
			group = it.group;
			ImGui::TextColored(kDim, "%s", tr(it.group));
		}
		ImGui::PushID(it.path);
		const unsigned t = assetThumb(it.path);
		const bool on    = place_.type == ItemType::Prop && place_.asset == it.path;
		if (!assetGrid && (on || favorite(it.path))) ImGui::PushStyleColor(ImGuiCol_Header, on ? ImVec4(0.52f, 0.66f, 0.25f, 1) : ImVec4(0.37f, 0.43f, 0.20f, 1));
		const bool pressed = assetGrid ? thumbCard(t, name, on, 88.0f, favorite(it.path))
		                               : ImGui::Selectable((std::string(favorite(it.path) ? "[F] " : "") + tr(it.name) + "##" + it.path).c_str(), on);
		if (!assetGrid && (on || favorite(it.path))) ImGui::PopStyleColor();
		if (pressed) choose({ ItemType::Prop, it.path, tr(it.name) });
		if (const nectar::CatalogEntry* e = catalogEntry(it.path)) {
			const char* warning = e->triangles > 10000 ? tr("Aviso: modelo muy pesado") : (e->boundsMax.x - e->boundsMin.x > 3000 || e->boundsMax.y - e->boundsMin.y > 3000 || e->boundsMax.z - e->boundsMin.z > 3000) ? tr("Aviso: modelo muy grande") : "";
			ImGui::SetItemTooltip("%s\n%s\n%s: %d · %s: %d%s%s", tr(it.name), it.path, tr("Triángulos"), e->triangles, tr("Triángulos de colisión"), e->collisionTris,
			                      *warning ? "\n" : "", warning);
		} else ImGui::SetItemTooltip("%s\n%s", tr(it.name), it.path);
		if (ImGui::BeginPopupContextItem("##asset_ctx")) {
			if (ImGui::MenuItem(favorite(it.path) ? tr("Quitar de favoritos") : tr("Añadir a favoritos"))) toggleFavorite(it.path);
			ImGui::EndPopup();
		}
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("NM_ASSET", it.path, std::strlen(it.path) + 1);
			ImGui::Text("%s", tr(it.name));
			ImGui::EndDragDropSource();
		}
		ImGui::PopID();
		if (assetGrid) {
			if (++col < 3) ImGui::SameLine();
			else col = 0;
		}
	}
	if (col) ImGui::NewLine();
	col = 0;
	if (iconHeader(tr("Más cosas del juego (avanzado)"), Icon::More)) {
		hint(tr("Cualquier modelo del juego como decoración. Algunos se ven raros o son muy grandes."));
		for (const nectar::CatalogEntry& e : catalog_.entries) {
			if (e.kind != nectar::AssetKind::Model || !e.error.empty()) continue;
			// Fuera lo que ya tiene su sitio (piezas, enemigos…) o no sirve
			// de adorno (pruebas, flechas, colisiones, personajes).
			static const char* const kSkipCat[] = { "terrain", "effects", "ship_parts", "pellets", "enemies", "pikmin" };
			static const char* const kSkipPath[] = { "shapes/", "testdata/", "intro/", "objects/gate/", "objects/bridge/", "objects/slimeEff",
				                                     "objects/water/", "objects/fl_water/", "objects/goal/", "objects/pikihead/",
				                                     "objects/rocket/rocket", "objects/ufo/", "cinemas/titles/" };
			bool skip = friendlyAssetName(e.path) != nullptr;
			for (const char* c : kSkipCat) skip |= e.category == c;
			for (const char* p : kSkipPath) skip |= e.path.rfind(p, 0) == 0;
			if (skip) continue;
			if (!q.empty() && searchKey(e.path + " " + e.category).find(q) == std::string::npos) continue;
			if (!passesFilter(e.path)) continue;
			const bool on = place_.type == ItemType::Prop && place_.asset == e.path;
			ImGui::PushID(e.path.c_str());
			if (!assetGrid && (on || favorite(e.path))) ImGui::PushStyleColor(ImGuiCol_Header, on ? ImVec4(0.52f, 0.66f, 0.25f, 1) : ImVec4(0.37f, 0.43f, 0.20f, 1));
			const unsigned t = assetThumb(e.path);
			const bool pressed = assetGrid ? thumbCard(t, fs::path(e.path).stem().string(), on, 88.0f, favorite(e.path))
			                               : ImGui::Selectable((std::string(favorite(e.path) ? "[F] " : "") + e.path).c_str(), on);
			if (!assetGrid && (on || favorite(e.path))) ImGui::PopStyleColor();
			if (pressed) choose({ ItemType::Prop, e.path, e.path });
			const char* warning = e.triangles > 10000 ? tr("Aviso: modelo muy pesado") : (e.boundsMax.x - e.boundsMin.x > 3000 || e.boundsMax.y - e.boundsMin.y > 3000 || e.boundsMax.z - e.boundsMin.z > 3000) ? tr("Aviso: modelo muy grande") : "";
			ImGui::SetItemTooltip("%s\n%s: %d\n%s: %d\n%s: %.0f × %.0f × %.0f%s%s", e.path.c_str(), tr("Triángulos"), e.triangles,
			                      tr("Triángulos de colisión"), e.collisionTris, tr("Dimensiones"), e.boundsMax.x - e.boundsMin.x,
			                      e.boundsMax.y - e.boundsMin.y, e.boundsMax.z - e.boundsMin.z, *warning ? "\n" : "", warning);
			if (ImGui::BeginPopupContextItem("##catalog_ctx")) {
				if (ImGui::MenuItem(favorite(e.path) ? tr("Quitar de favoritos") : tr("Añadir a favoritos"))) toggleFavorite(e.path);
				ImGui::EndPopup();
			}
			if (ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("NM_ASSET", e.path.c_str(), e.path.size() + 1);
				ImGui::TextUnformatted(e.path.c_str());
				ImGui::EndDragDropSource();
			}
			ImGui::PopID();
			if (assetGrid) {
				if (++col < 3) ImGui::SameLine();
				else col = 0;
			}
		}
		if (assetGrid && col) ImGui::NewLine();
	}
}

void App::drawSwatches(bool allowAdd)
{
	nectar::DocTerrain& g = ed_.doc.terrain;
	paintMaterial_        = std::max(0, std::min(paintMaterial_, int(g.palette.size()) - 1));
	for (std::size_t k = 0; k < g.palette.size(); k++) {
		ImGui::PushID(int(k));
		const unsigned t = swatch(g.palette[k].source, g.palette[k].index);
		const bool sel   = paintMaterial_ == int(k);
		if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.66f, 0.25f, 1));
		if (t ? ImGui::ImageButton("##p", ImTextureID(std::intptr_t(t)), ImVec2(56, 56)) : ImGui::Button("?", ImVec2(64, 64))) paintMaterial_ = int(k);
		if (sel) ImGui::PopStyleColor();
		const char* course = courseName(g.palette[k].source);
		ImGui::SetItemTooltip(tr("Suelo %zu (de %s)"), k + 1, course ? course : g.palette[k].source.c_str());
		if (ImGui::BeginPopupContextItem("ctx")) {
			if (ImGui::MenuItem(tr("Cambiar por otro"))) {
				matBrowserTarget_ = int(k);
				matBrowserCourse_ = g.palette[k].source;
				openMatBrowser_   = true;
			}
			if (g.palette.size() > 1 && ImGui::MenuItem(tr("Quitar de la lista"))) {
				// Donde mandaba, pasa a mandar lo que quede (o el primero).
				g.palette.erase(g.palette.begin() + long(k));
				g.layers.erase(g.layers.begin() + long(k));
				for (std::size_t v = 0; v < g.vertexCount(); v++) {
					bool any = false;
					for (const auto& l : g.layers) any = any || l[v] > 0;
					if (!any) g.layers[0][v] = 255;
				}
				ed_.touch();
				ImGui::EndPopup();
				ImGui::PopID();
				return;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		if ((k + 1) % 4) ImGui::SameLine();
	}
	if (allowAdd && g.palette.size() < 250) {
		if (ImGui::Button("+", ImVec2(64, 64))) {
			matBrowserTarget_ = -1;
			openMatBrowser_   = true;
		}
		ImGui::SetItemTooltip("%s", tr("Añadir otro suelo de los escenarios del juego"));
	} else {
		ImGui::NewLine();
	}
	hint(tr("Clic derecho en un suelo para cambiarlo o quitarlo."));
}

void App::drawBrushSettings()
{
	ImGui::SliderFloat(tr("Pincel"), &brushRadius_, 25, 1000, "%.0f");
	ImGui::SetItemTooltip("%s", tr("Tamaño del pincel (también con Ctrl + rueda del ratón)."));
	ImGui::TextColored(kDim, "%s", tr("Forma"));
	ImGui::SameLine(78);
	if (choiceButton(tr("Circular"), brushShape_ == BrushShape::Circle, ImVec2(88, 0))) brushShape_ = BrushShape::Circle;
	ImGui::SameLine();
	if (choiceButton(tr("Cuadrado"), brushShape_ == BrushShape::Square, ImVec2(88, 0))) brushShape_ = BrushShape::Square;
	ImGui::TextColored(kDim, "%s", tr("Borde"));
	ImGui::SameLine(78);
	if (choiceButton(tr("Suave"), brushFalloff_ == BrushFalloff::Smooth, ImVec2(88, 0))) brushFalloff_ = BrushFalloff::Smooth;
	ImGui::SameLine();
	if (choiceButton(tr("Duro"), brushFalloff_ == BrushFalloff::Hard, ImVec2(88, 0))) brushFalloff_ = BrushFalloff::Hard;
}

void App::drawFloorOptions()
{
	ImGui::SeparatorText(tr("Dibujar suelo"));
	if (choiceButton(tr("Añadir suelo"), !floorErase_, ImVec2(130, 34))) floorErase_ = false;
	ImGui::SameLine();
	if (choiceButton(tr("Hacer hueco"), floorErase_, ImVec2(130, 34))) floorErase_ = true;
	hint(floorErase_ ? tr("Pinta para quitar suelo. Alrededor de los huecos se ponen paredes solas.")
	                 : tr("Pinta para añadir suelo. Mantén Mayús para hacer huecos."));
	drawBrushSettings();
	ImGui::SeparatorText(tr("¿Qué suelo?"));
	drawSwatches(true);

	ImGui::SeparatorText(tr("Tamaño del mapa"));
	ImGui::Text("%s", tr("Ancho"));
	ImGui::SameLine(80);
	if (ImGui::Button(tr("Más##w"), ImVec2(80, 0))) growMap(4, 0);
	ImGui::SameLine();
	if (ImGui::Button(tr("Menos##w"), ImVec2(80, 0))) growMap(-4, 0);
	ImGui::Text("%s", tr("Largo"));
	ImGui::SameLine(80);
	if (ImGui::Button(tr("Más##l"), ImVec2(80, 0))) growMap(0, 4);
	ImGui::SameLine();
	if (ImGui::Button(tr("Menos##l"), ImVec2(80, 0))) growMap(0, -4);
	const MapDoc& d = ed_.doc;
	ImGui::TextColored(kDim, tr("Ahora: %.0f x %.0f"), d.maxX - d.minX, d.maxZ - d.minZ);
	ImGui::Text("%s", tr("Detalle"));
	ImGui::SameLine(80);
	const double cells[] = { 50.0, 25.0 };
	const char* cellNames[] = { tr("Normal"), tr("Fino") };
	for (int i = 0; i < 2; i++) {
		if (i) ImGui::SameLine();
		if (choiceButton(cellNames[i], std::fabs(d.cell - cells[i]) < 0.5, ImVec2(80, 0)) && std::fabs(d.cell - cells[i]) >= 0.5 && ensureTerrain()) {
			if (nectar::resampleTerrain(ed_.doc, cells[i])) ed_.touch();
			else notify(1, tr("El mapa es demasiado grande para ese detalle (más de 400 celdas por lado)."));
		}
	}
	hint(tr("Fino: bordes y laderas con el doble de detalle (gasta más vértices)."));

	ImGui::SeparatorText(tr("Formas rápidas"));
	hint(tr("Cambian dónde hay suelo de golpe (se puede deshacer)."));
	struct S {
		const char* id;
		nectar::TerrainShape shape;
		const char* label;
	};
	const S shapes[] = { { "r", nectar::TerrainShape::Rectangle, tr("Rectángulo") }, { "o", nectar::TerrainShape::Oval, tr("Óvalo") },
		                 { "c", nectar::TerrainShape::Cross, tr("Cruz") },          { "i", nectar::TerrainShape::TwoIslands, tr("Dos islas") },
		                 { "a", nectar::TerrainShape::Ring, tr("Anillo") } };
	for (int i = 0; i < 5; i++) {
		if (i % 3) ImGui::SameLine();
		if (shapeButton(shapes[i].id, shapes[i].shape, shapes[i].label, 76)) {
			nectar::applyTerrainShape(ed_.doc, shapes[i].shape);
			ed_.touch();
		}
	}
	ImGui::SeparatorText(tr("Paredes del borde"));
	nectar::DocTerrain& g = ed_.doc.terrain;
	const unsigned t      = swatch(g.border.source, g.border.index);
	if (t ? ImGui::ImageButton("##border", ImTextureID(std::intptr_t(t)), ImVec2(48, 48)) : ImGui::Button("?", ImVec2(56, 56))) {
		matBrowserTarget_ = -2;
		matBrowserCourse_ = g.border.source;
		openMatBrowser_   = true;
	}
	ImGui::SetItemTooltip("%s", tr("Pulsa para elegir otro aspecto"));
	ImGui::SameLine();
	ImGui::BeginGroup();
	float h = float(ed_.doc.wallHeight);
	ImGui::SetNextItemWidth(150);
	if (ImGui::SliderFloat(tr("Altura"), &h, 20, 600, "%.0f")) {
		ed_.doc.wallHeight = h;
		ed_.touch();
	}
	ImGui::EndGroup();
}

void App::drawHeightOptions()
{
	ImGui::SeparatorText(tr("Montañas y hoyos"));
	const char* ops[]  = { tr("Subir"), tr("Bajar"), tr("Allanar"), tr("Suavizar"), tr("Ruido"), tr("Colina"), tr("Cráter"), tr("Meseta") };
	const char* tips[] = { tr("Pinta para levantar el terreno (montañas, rampas)."), tr("Pinta para hundir el terreno (hoyos, valles)."),
		                   tr("Deja el terreno a la altura donde empiezas a pintar."), tr("Quita picos y escalones."),
		                   tr("Pinta para que el terreno salga irregular, como en los niveles del juego."),
		                   tr("Un clic: una colina redonda del tamaño del pincel (la fuerza es su altura)."),
		                   tr("Un clic: un cráter con su borde levantado (la fuerza es su hondura)."),
		                   tr("Un clic: una meseta plana con laderas (la fuerza es su altura).") };
	for (int i = 0; i < 8; i++) {
		if (i % 2) ImGui::SameLine();
		if (choiceButton(ops[i], int(heightOp_) == i, ImVec2(130, 34))) heightOp_ = HeightOp(i);
	}
	hint(tips[int(heightOp_)]);
	if (heightOp_ == HeightOp::Raise || heightOp_ == HeightOp::Lower) hint(tr("Con Mayús hace lo contrario."));
	drawBrushSettings();
	ImGui::SliderFloat(tr("Fuerza"), &brushStrength_, 20, 600, "%.0f");
	ImGui::Spacing();
	ImGui::TextColored(kWarn, "%s", tr("Las zonas rojas son tan empinadas"));
	ImGui::TextColored(kWarn, "%s", tr("que los Pikmin resbalan."));
	ImGui::Spacing();
	if (ImGui::Button(tr("Allanar todo el mapa"), ImVec2(-1, 0))) {
		std::fill(ed_.doc.terrain.heights.begin(), ed_.doc.terrain.heights.end(), std::int16_t(0));
		ed_.touch();
	}
}

void App::drawPaintOptions()
{
	ImGui::SeparatorText(tr("Pintar el suelo"));
	hint(tr("Elige un suelo y pinta encima del mapa. Solo cambia el aspecto: la forma no."));
	drawBrushSettings();
	hint(tr("Alt + clic toma el material del suelo."));
	drawSwatches(true);
	ImGui::SeparatorText(tr("Acantilados"));
	hint(tr("Pinta del suelo elegido (mejor una roca) todas las pendientes fuertes del mapa, como las paredes de los niveles del juego."));
	ImGui::SliderFloat(tr("Desde la pendiente"), &cliffSlope_, 0.4f, 3.0f, "%.1f");
	if (ImGui::Button(tr("Pintar acantilados"), ImVec2(-1, 30))) paintCliffs();
	if (ImGui::CollapsingHeader(tr("Ajustes del suelo elegido (avanzado)"))) {
		nectar::DocTerrain& g = ed_.doc.terrain;
		nectar::DocMaterial& m = g.palette[std::size_t(paintMaterial_)];
		const char* const kinds[] = { tr("Sólido"), tr("Roca"), tr("Hierba"), tr("Madera"), tr("Barro") };
		if (ImGui::Combo(tr("Al pisarlo suena a"), &m.attribute, kinds, 5)) ed_.touch();
		float uv = float(m.uvScale * 1000.0);
		if (ImGui::SliderFloat(tr("Tamaño del dibujo"), &uv, 0.0f, 10.0f, uv > 0 ? "%.2f" : tr("Normal"))) {
			m.uvScale = uv / 1000.0;
			ed_.touch();
		}
	}
}

void App::drawWaterOptions()
{
	ImGui::SeparatorText(tr("Agua"));
	if (choiceButton(tr("Poner agua"), !waterErase_, ImVec2(130, 34))) waterErase_ = false;
	ImGui::SameLine();
	if (choiceButton(tr("Quitar agua"), waterErase_, ImVec2(130, 34))) waterErase_ = true;
	hint(waterErase_ ? tr("Pinta para secar el agua.") : tr("Pinta sobre el suelo para hacer un lago. Mantén Mayús para quitar agua."));
	drawBrushSettings();
	hint(tr("Cada lago queda plano, a la altura de su orilla más baja."));
	hint(tr("Solo los Pikmin azules pueden entrar en el agua: es buen sitio para esconder piezas."));
}

void App::drawPlateauOptions()
{
	ImGui::SeparatorText(tr("Elevar o hundir zona"));
	if (choiceButton(tr("Elevar"), plateauUp_, ImVec2(130, 34))) plateauUp_ = true;
	ImGui::SameLine();
	if (choiceButton(tr("Hundir"), !plateauUp_, ImVec2(130, 34))) plateauUp_ = false;
	hint(plateauUp_ ? tr("Pinta una zona: queda plana y más alta que donde empiezas a pintar (una plataforma).")
	                : tr("Pinta una zona: queda plana y más baja que donde empiezas a pintar (un foso)."));
	hint(tr("Mantén Mayús para hacer lo contrario."));
	ImGui::Text("%s", tr("¿Cuánto?"));
	const struct {
		const char* label;
		float amount;
	} amounts[] = { { tr("Poco"), 50 }, { tr("Medio"), 100 }, { tr("Mucho"), 200 }, { tr("Muchísimo"), 400 } };
	for (int i = 0; i < 4; i++) {
		if (i % 2) ImGui::SameLine();
		if (choiceButton(amounts[i].label, plateauAmount_ == amounts[i].amount, ImVec2(130, 0))) plateauAmount_ = amounts[i].amount;
	}
	drawBrushSettings();
	ImGui::Spacing();
	ImGui::TextColored(kWarn, "%s", tr("Los bordes son acantilados:"));
	ImGui::TextColored(kWarn, "%s", tr("los Pikmin no pueden subirlos."));
	hint(tr("Para unir alturas pon una rampa (Poner cosas > Puentes y rampas) o suaviza el borde con «Montañas y hoyos»."));
}

void App::drawScatterOptions()
{
	ImGui::SeparatorText(tr("Decorar"));
	if (choiceButton(tr("Poner"), !scatterErase_, ImVec2(110, 34))) scatterErase_ = false;
	ImGui::SameLine();
	if (choiceButton(tr("Quitar"), scatterErase_, ImVec2(110, 34))) scatterErase_ = true;
	hint(scatterErase_ ? tr("Pinta para quitar la decoración elegida abajo (sin nada elegido, toda).")
	                   : tr("Pinta para esparcir lo elegido abajo, girado y de tamaños distintos. Mantén Mayús para quitar."));
	drawBrushSettings();
	ImGui::SliderFloat(tr("Cuánta"), &scatterDensity_, 1.0f, 30.0f, "%.0f");
	ImGui::SliderFloat(tr("Variedad de tamaño"), &scatterSizeVar_, 0.0f, 0.6f, "%.2f");
	ImGui::Checkbox(tr("Los Pikmin no lo atraviesan"), &scatterCollide_);
	hint(tr("Para hierbas y flores, mejor sin marcar: así no cortan el paso."));

	// Mezclas rápidas.
	ImGui::SeparatorText(tr("¿Qué pones?"));
	const struct {
		const char* label;
		std::vector<std::string> set;
	} mixes[] = {
		{ tr("Pradera"), { "objects/weeds/weed1.mod", "objects/plants/hutaba.mod", "objects/plants/hae.mod" } },
		{ tr("Flores"), { "objects/plants/tanpopo.mod", "objects/plants/ooinu_l.mod", "objects/plants/ooinu_s.mod", "objects/plants/clover.mod" } },
		{ tr("Rocas"), { "objects/weeds/stone1.mod", "objects/weeds/stone2.mod", "objects/weeds/stone3.mod" } },
		{ tr("Orilla"), { "objects/plants/mizukusa.mod", "objects/plants/ine.mod", "objects/plants/wakame_s.mod" } },
		{ tr("Bosque"), { "objects/plants/hae.mod", "objects/plants/chidome.mod", "objects/plants/kinokolt.mod", "objects/weeds/weed1.mod" } },
	};
	int k = 0;
	for (const auto& m : mixes) {
		if (k++ % 3) ImGui::SameLine();
		if (choiceButton(m.label, scatterSet_ == m.set, ImVec2(86, 30))) scatterSet_ = m.set;
	}
	hint(tr("O elige uno a uno (puedes marcar varios):"));
	int col = 0;
	for (const DecorItem& it : decorItems()) {
		const std::string grp = it.group;
		if (grp != "Plantas" && grp != "Rocas y obstáculos") continue;
		ImGui::PushID(it.path);
		auto pos      = std::find(scatterSet_.begin(), scatterSet_.end(), std::string(it.path));
		const bool on = pos != scatterSet_.end();
		if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.52f, 0.66f, 0.25f, 1));
		const unsigned t   = assetThumb(it.path);
		const bool pressed = t ? ImGui::ImageButton("##s", ImTextureID(std::intptr_t(t)), ImVec2(52, 52)) : ImGui::Button(tr(it.name), ImVec2(60, 60));
		if (on) ImGui::PopStyleColor();
		ImGui::SetItemTooltip("%s", tr(it.name));
		if (pressed) {
			if (on) scatterSet_.erase(pos);
			else scatterSet_.push_back(it.path);
		}
		ImGui::PopID();
		if (++col % 4) ImGui::SameLine();
	}
	if (col % 4) ImGui::NewLine();
}

void App::scatter(const V3& at)
{
	MapDoc& d     = ed_.doc;
	const float r = brushRadius_;
	auto distance = [&](float x, float z) {
		const float dx = std::fabs(x - at.x), dz = std::fabs(z - at.z);
		return brushShape_ == BrushShape::Square ? std::max(dx, dz) : std::hypot(dx, dz);
	};
	auto rnd      = [&]() {
        scatterSeed_ = scatterSeed_ * 1664525u + 1013904223u;
        return float((scatterSeed_ >> 8) & 0xFFFF) / 65535.0f;
	};
	auto inSet = [&](const std::string& a) { return scatterSet_.empty() || std::find(scatterSet_.begin(), scatterSet_.end(), a) != scatterSet_.end(); };
	if (scatterErase_) {
		std::vector<ItemRef> gone;
		for (std::size_t i = 0; i < d.props.size(); i++) {
			const nectar::DocProp& p = d.props[i];
			for (int side = 0; side < (d.symmetric ? 2 : 1); side++) {
				const float px = side ? float(-p.x) : float(p.x), pz = side ? float(-p.z) : float(p.z);
				if (distance(px, pz) <= r && inSet(p.asset)) {
					gone.push_back({ ItemType::Prop, int(i), 0 });
					break;
				}
			}
		}
		if (!gone.empty()) {
			ed_.clearSelection();
			remapSessionItemsAfterRemoval(gone);
			removeItems(d, gone);
			ed_.touch();
		}
		return;
	}
	if (scatterSet_.empty()) return;
	// El juego tiene un tope de vértices por mapa: se para un poco antes.
	constexpr std::size_t kMaxProps = 1500;
	if (d.props.size() >= kMaxProps || (compiled_.ok && compiled_.vertices > nectar::kMapMaxVertices * 9 / 10)) {
		if (!scatterFullWarned_) notify(1, tr("Ya hay mucha decoración: el mapa no cabría en el juego."));
		scatterFullWarned_ = true;
		return;
	}
	scatterFullWarned_  = false;
	const float spacing = 100.0f / std::sqrt(scatterDensity_);
	const nectar::DocTerrain& g = d.terrain;
	auto wet = [&](float x, float z) {
		if (!g.grid || g.water.empty()) return false;
		const int gx = std::max(0, std::min(g.nx, int(std::lround((x - d.minX) / d.cell))));
		const int gz = std::max(0, std::min(g.nz, int(std::lround((z - d.minZ) / d.cell))));
		return g.water[std::size_t(g.vertexIndex(gx, gz))] >= 128;
	};
	bool changed = false;
	for (int attempt = 0; attempt < 10; attempt++) {
		float x, z;
		if (brushShape_ == BrushShape::Square) {
			x = at.x + (rnd() * 2.0f - 1.0f) * r;
			z = at.z + (rnd() * 2.0f - 1.0f) * r;
		} else {
			const float a = rnd() * 6.2831853f, dist = std::sqrt(rnd()) * r;
			x = at.x + std::cos(a) * dist;
			z = at.z + std::sin(a) * dist;
		}
		if (brushFalloff_ == BrushFalloff::Smooth && rnd() > std::max(0.0f, 1.0f - distance(x, z) / r)) continue;
		if (!docSolid(x, z) || wet(x, z)) continue;
		// Ni en cuestas fuertes ni junto a un borde.
		const float h = docHeight(x, z);
		bool steep    = false;
		for (int k = 0; k < 4 && !steep; k++) {
			const float ox = (k == 0 ? 20.0f : k == 1 ? -20.0f : 0.0f), oz = (k == 2 ? 20.0f : k == 3 ? -20.0f : 0.0f);
			steep = !docSolid(x + ox, z + oz) || std::fabs(docHeight(x + ox, z + oz) - h) > 12.0f;
		}
		if (steep) continue;
		// Separación con lo que ya hay (y con su copia del otro lado).
		bool crowded = false;
		for (const nectar::DocProp& p : d.props) {
			for (int side = 0; side < (d.symmetric ? 2 : 1) && !crowded; side++) {
				const float px = side ? float(-p.x) : float(p.x), pz = side ? float(-p.z) : float(p.z);
				crowded = (px - x) * (px - x) + (pz - z) * (pz - z) < spacing * spacing;
			}
			if (crowded) break;
		}
		if (crowded) continue;
		nectar::DocProp p;
		p.asset   = scatterSet_[std::min(scatterSet_.size() - 1, std::size_t(rnd() * float(scatterSet_.size())))];
		p.x       = x;
		p.z       = z;
		p.y       = h;
		p.ry      = rnd() * 360.0;
		p.scale   = std::max(0.2, 1.0 + (rnd() * 2.0 - 1.0) * scatterSizeVar_);
		p.collide = scatterCollide_;
		d.props.push_back(p);
		changed = true;
		if (d.props.size() >= kMaxProps) break;
	}
	if (changed) ed_.touch();
}

void App::drawWallOptions()
{
	ImGui::SeparatorText(tr("Muros"));
	hint(tr("Haz clic donde empieza el muro y arrastra hasta donde acaba."));
	ImGui::Text("%s", tr("Altura"));
	if (choiceButton(tr("Bajo"), wallHeight_ == 60, ImVec2(80, 0))) wallHeight_ = 60;
	ImGui::SameLine();
	if (choiceButton(tr("Medio"), wallHeight_ == 150, ImVec2(80, 0))) wallHeight_ = 150;
	ImGui::SameLine();
	if (choiceButton(tr("Alto"), wallHeight_ == 300, ImVec2(80, 0))) wallHeight_ = 300;
	ImGui::SliderFloat(tr("Grosor"), &wallThickness_, 10, 200, "%.0f");
	hint(tr("Los muros bajos se pueden ver por encima, pero los Pikmin no los cruzan."));
}

void App::drawRampOptions()
{
	ImGui::SeparatorText(tr("Rampas de terreno"));
	if (drawingRamp_)
		ImGui::TextColored(kGood, "%s", tr("PASO 2 DE 2 · Haz clic en el extremo alto"));
	else
		ImGui::TextColored(kWarn, "%s", tr("PASO 1 DE 2 · Haz clic en el extremo bajo"));
	hint(tr("Une dos zonas que ya están a alturas diferentes. No coloca un objeto: modifica el suelo dentro del rectángulo."));
	ImGui::SliderFloat(tr("Anchura"), &rampWidth_, 100.0f, 1000.0f, "%.0f");
	if (choiceButton(tr("Perfil suavizado"), rampSmooth_, ImVec2(132, 0))) rampSmooth_ = true;
	ImGui::SameLine();
	if (choiceButton(tr("Perfil lineal"), !rampSmooth_, ImVec2(120, 0))) rampSmooth_ = false;
	if (drawingRamp_ && rampPreviewValid_) {
		const float distance = std::hypot(rampEnd_.x - rampStart_.x, rampEnd_.z - rampStart_.z);
		const float height = std::fabs(rampEnd_.y - rampStart_.y);
		const float slope = distance > 1.0f ? height / distance : 999.0f;
		if (height < 5.0f)
			ImGui::TextColored(kWarn, "%s", tr("Sin desnivel: apunta a una zona más alta."));
		else
			ImGui::TextColored(slope <= 0.577f ? kGood : kWarn, slope <= 0.577f ? tr("Pendiente transitable: %.0f%%") : tr("Aviso: pendiente demasiado fuerte (%.0f%%)"), slope * 100.0f);
	}
	if (drawingRamp_ && ImGui::Button(tr("Cancelar rampa"), ImVec2(-1, 30))) drawingRamp_ = false;
	hint(tr("La rampa modifica el terreno y se puede deshacer con Ctrl+Z."));
}

void App::growMap(int dx, int dz)
{
	if (!ensureTerrain()) return;
	nectar::resizeTerrain(ed_.doc, dx / 2, dx - dx / 2, dz / 2, dz - dz / 2);
	ed_.touch();
}

const char* App::toolHelp() const
{
	switch (tool_) {
	case Tool::Move: return tr("W: mover · E: girar · R: escalar · Q: giro por pasos · Supr: borrar");
	case Tool::Place: return tr("Haz clic en el mapa para poner lo elegido a la izquierda.");
	case Tool::Scatter: return scatterErase_ ? tr("Pinta para quitar decoración.") : tr("Pinta para esparcir decoración (Mayús: quitarla).");
	case Tool::Floor: return floorErase_ ? tr("Pinta con el ratón para hacer huecos.") : tr("Pinta con el ratón para añadir suelo (Mayús: hacer huecos).");
	case Tool::Height: return tr("Pinta con el ratón para subir o bajar el terreno (Mayús: al revés).");
	case Tool::Plateau: return plateauUp_ ? tr("Pinta una zona para elevarla, plana (Mayús: hundir).") : tr("Pinta una zona para hundirla, plana (Mayús: elevar).");
	case Tool::Paint: return tr("Pinta con el ratón para cambiar el aspecto del suelo.");
	case Tool::Water: return waterErase_ ? tr("Pinta para quitar agua.") : tr("Pinta sobre el suelo para hacer un lago (Mayús: quitar agua).");
	case Tool::Walls: return tr("Haz clic y arrastra para dibujar un muro.");
	case Tool::Trace: return traceNodes_.empty() ? tr("Haz clic en el mapa para poner el primer punto.") : tr("Clic: otro punto. Retroceso: quitar el último. Intro: aplicar.");
	case Tool::Ramp: return drawingRamp_ ? tr("2/2: mueve el ratón a la zona alta y haz clic para crear.") : tr("1/2: haz clic donde debe empezar la rampa.");
	}
	return "";
}

void App::drawSidePanel(const Rect& r)
{
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::Begin("##side", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ed_.pruneSelection();
	panelHeading(tr("Propiedades"), tr("De lo elegido, del mapa o de la herramienta"));
	const float headingH = ImGui::GetCursorPosY();
	const float bodyH    = r.h - headingH - 22.0f;
	const float topH     = std::max(120.0f, std::min(bodyH - 120.0f, bodyH * cfg_.sideSplit));
	ImGui::BeginChild("top", ImVec2(0, topH));
	{
		// Se cambia de pestaña sola: al elegir algo, a «Selección»; al
		// soltarlo, a «Mapa»; al cambiar de herramienta (o con «Más opciones»),
		// a «Herramienta».
		static bool hadSelection = false;
		static Tool lastTool     = Tool::Move;
		const bool hasSelection  = !ed_.selection.empty();
		int want                 = -1;
		if (hasSelection && !hadSelection) want = 0;
		if (!hasSelection && hadSelection) want = 1;
		if (tool_ != lastTool && tool_ != Tool::Move) want = 2;
		if (focusToolTab_) want = 2;
		focusToolTab_ = false;
		hadSelection  = hasSelection;
		lastTool      = tool_;
		if (ImGui::BeginTabBar("##props")) {
			if (hasSelection && ImGui::BeginTabItem(tr("Selección"), nullptr, want == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
				drawSelected();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(tr("Mapa"), nullptr, want == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
				drawMapPanel();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(tr("Herramienta"), nullptr, want == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
				drawToolOptions();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::EndChild();
	// Divisor entre propiedades y escena (se arrastra; doble clic: como al principio).
	{
		ImGui::InvisibleButton("##side_split", ImVec2(-1, 10));
		const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if (ImGui::IsItemActive()) cfg_.sideSplit = std::max(0.2f, std::min(0.85f, (topH + ImGui::GetIO().MouseDelta.y) / bodyH));
		if (ImGui::IsItemDeactivated()) cfg_.save();
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			cfg_.sideSplit = 0.58f;
			cfg_.save();
		}
		if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		const float cy = (a.y + b.y) * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, cy), ImVec2(b.x, cy), hot ? IM_COL32(171, 196, 76, 255) : IM_COL32(70, 84, 60, 255), hot ? 3.0f : 1.5f);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2((a.x + b.x) * 0.5f - 22, cy - 3), ImVec2((a.x + b.x) * 0.5f + 22, cy + 3), hot ? IM_COL32(171, 196, 76, 255) : IM_COL32(94, 117, 49, 220), 3.0f);
	}
	const ImVec2 cmin = ImGui::GetCursorScreenPos();
	checklistRect_    = { cmin.x, cmin.y, r.w - 16, bodyH - topH - 16 };
	ImGui::BeginChild("check");
	if (ImGui::BeginTabBar("##side_bottom_tabs")) {
		if (ImGui::BeginTabItem(tr("Escena"))) {
			drawHierarchy();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(tr("Informe"))) {
			drawTechnicalReport();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::EndChild();
	ImGui::End();
}

void App::drawMinimap(const ImVec2& o, float w, float h, bool& hoveredOut)
{
	const MapDoc& d             = ed_.doc;
	const nectar::DocTerrain& g = d.terrain;
	const float spanX = float(d.maxX - d.minX), spanZ = float(d.maxZ - d.minZ);
	if (spanX <= 0 || spanZ <= 0) return;
	// Sobre el visor: el clic y el arrastre se miran a mano (un botón
	// invisible taparía el visor).
	const bool hovered = ImGui::IsMouseHoveringRect(o, ImVec2(o.x + w, o.y + h));
	hoveredOut |= hovered;
	static bool dragging = false;
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragging = true;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
	const float sx = w / spanX, sz = h / spanZ;
	// Mundo -> minimapa (x a la derecha, z hacia abajo, como la vista cenital).
	auto toMap   = [&](float x, float z) { return ImVec2(o.x + (x - float(d.minX)) * sx, o.y + (z - float(d.minZ)) * sz); };
	auto toWorld = [&](const ImVec2& p) { return V3(float(d.minX) + (p.x - o.x) / sx, 0, float(d.minZ) + (p.y - o.y) / sz); };
	if (dragging) {
		const V3 p  = toWorld(ImGui::GetIO().MousePos);
		cam_.target = V3(std::max(float(d.minX), std::min(float(d.maxX), p.x)), 0, std::max(float(d.minZ), std::min(float(d.maxZ), p.z)));
		cam_.target.y = docHeight(cam_.target.x, cam_.target.z);
	}
	if (hovered) ImGui::SetTooltip("%s", tr("Haz clic (o arrastra) para mover la vista ahí."));

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(ImVec2(o.x - 4, o.y - 4), ImVec2(o.x + w + 4, o.y + h + 4), IM_COL32(16, 22, 16, 215), 10.0f);
	dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), IM_COL32(21, 34, 20, 255), 8.0f);
	// Suelo: celdas sombreadas por altura; agua en azul; huecos vacíos.
	const int cols = std::max(1, std::min(96, g.grid ? g.nx : 48));
	const int rows = std::max(1, std::min(96, int(float(cols) * spanZ / spanX + 0.5f)));
	float hmin = 0, hmax = 1;
	if (courseMode()) {
		hmin = 1e30f, hmax = -1e30f;
		for (float v : courseField_.h) {
			if (std::isnan(v)) continue;
			hmin = std::min(hmin, v);
			hmax = std::max(hmax, v);
		}
		if (hmin > hmax) hmin = 0, hmax = 1;
		hmax = std::max(hmin + 1.0f, hmax);
	} else if (g.grid && !g.heights.empty()) {
		hmin = float(*std::min_element(g.heights.begin(), g.heights.end()));
		hmax = std::max(hmin + 1.0f, float(*std::max_element(g.heights.begin(), g.heights.end())));
	}
	for (int iz = 0; iz < rows; iz++) {
		for (int ix = 0; ix < cols; ix++) {
			const float x = float(d.minX) + (ix + 0.5f) * spanX / cols, z = float(d.minZ) + (iz + 0.5f) * spanZ / rows;
			if (!docSolid(x, z)) continue;
			bool wet = false;
			if (g.grid && !g.water.empty()) {
				const int gx = std::max(0, std::min(g.nx, int(std::lround((x - d.minX) / d.cell))));
				const int gz = std::max(0, std::min(g.nz, int(std::lround((z - d.minZ) / d.cell))));
				wet          = g.water[std::size_t(g.vertexIndex(gx, gz))] >= 128;
			}
			const float k = 0.62f + 0.38f * std::max(0.0f, std::min(1.0f, (docHeight(x, z) - hmin) / (hmax - hmin)));
			const ImU32 c = wet ? IM_COL32(58, 118, 196, 255) : IM_COL32(int(96 * k), int(150 * k), int(62 * k), 255);
			const ImVec2 a = toMap(float(d.minX) + ix * spanX / cols, float(d.minZ) + iz * spanZ / rows);
			const ImVec2 b = toMap(float(d.minX) + (ix + 1) * spanX / cols, float(d.minZ) + (iz + 1) * spanZ / rows);
			dl->AddRectFilled(a, ImVec2(b.x + 0.5f, b.y + 0.5f), c);
		}
	}
	// Muros (y su copia si el mapa es espejo).
	for (const nectar::DocWall& wl : d.walls) {
		const int segs = wl.isArc ? std::max(1, wl.segments) : 1;
		for (int side = 0; side < (d.symmetric ? 2 : 1); side++) {
			const float m = side ? -1.0f : 1.0f;
			for (int k = 0; k < segs; k++) {
				float ax = float(wl.ax), az = float(wl.az), bx = float(wl.bx), bz = float(wl.bz);
				if (wl.isArc) {
					const float a0 = float(wl.from + k * (wl.to - wl.from) / segs) * 0.0174533f;
					const float a1 = float(wl.from + (k + 1) * (wl.to - wl.from) / segs) * 0.0174533f;
					ax = float(wl.cx + wl.radius * std::cos(a0)), az = float(wl.cz + wl.radius * std::sin(a0));
					bx = float(wl.cx + wl.radius * std::cos(a1)), bz = float(wl.cz + wl.radius * std::sin(a1));
				}
				dl->AddLine(toMap(m * ax, m * az), toMap(m * bx, m * bz), IM_COL32(150, 120, 90, 255), 2.5f);
			}
		}
	}
	// Cosas del mapa: puntos con el color de su jugador.
	for (const ItemRef& it : allItems(d)) {
		if (itemHidden(it)) continue;
		const ItemCaps caps = itemCaps(it.type);
		// La decoración no: solo lo que importa para jugar (si no, tapa el dibujo).
		if (it.type == ItemType::Wall || it.type == ItemType::Pond || it.type == ItemType::Prop || it.type == ItemType::Gen) continue;
		const Xform x = getXform(d, it);
		ImU32 c       = IM_COL32(235, 235, 235, 255);
		if (caps.perPlayer) {
			const Rgba pc = playerColour(it.index);
			c             = ImGui::ColorConvertFloat4ToU32(ImVec4(pc.r, pc.g, pc.b, 1));
		} else if (it.type == ItemType::Piece || it.type == ItemType::BigPiece || it.type == ItemType::ShipPart) {
			c = IM_COL32(240, 200, 60, 255);
		} else if (it.type == ItemType::Captain || it.type == ItemType::Ship || it.type == ItemType::StoryOnion) {
			c = IM_COL32(120, 220, 255, 255);
		} else if (it.type == ItemType::Enemy || it.type == ItemType::Guard) {
			c = IM_COL32(230, 90, 200, 255);
		} else if (it.type == ItemType::Prop) {
			c = IM_COL32(140, 150, 140, 200);
		}
		const float r = caps.perPlayer ? 4.0f : 3.0f;
		dl->AddCircleFilled(toMap(float(x.x), float(x.z)), r, c);
		if (d.symmetric && caps.twinned) dl->AddCircleFilled(toMap(float(-x.x), float(-x.z)), r, c);
	}
	// Dónde mira la cámara.
	const ImVec2 t = toMap(cam_.target.x, cam_.target.z);
	if (cam_.topDown) {
		dl->AddCircle(t, 7.0f, IM_COL32(255, 255, 255, 230), 16, 2.0f);
	} else {
		const V3 e      = cam_.eye();
		ImVec2 dir(cam_.target.x - e.x, cam_.target.z - e.z);
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len > 0) dir = ImVec2(dir.x / len, dir.y / len);
		const ImVec2 back(t.x - dir.x * 14.0f, t.y - dir.y * 14.0f);
		dl->AddTriangleFilled(ImVec2(t.x + dir.x * 6, t.y + dir.y * 6), ImVec2(back.x - dir.y * 8, back.y + dir.x * 8), ImVec2(back.x + dir.y * 8, back.y - dir.x * 8),
		                      IM_COL32(255, 255, 255, 110));
		dl->AddCircleFilled(t, 3.5f, IM_COL32(255, 255, 255, 255));
	}
	dl->AddRect(o, ImVec2(o.x + w, o.y + h), IM_COL32(112, 139, 59, 255), 8.0f, 0, 2.0f);
}

// Qué ocurre en una reacción (enemigos, pastillas, bombas o néctar) y cuántos.
// true si ha cambiado algo.
static bool editReaction(nectar::DocReaction& r)
{
	bool changed = false;
	const char* actions[] = { tr("No ocurre nada"), tr("Aparecen enemigos"), tr("Aparecen pellets"), tr("Aparecen rocas bomba"), tr("Aparece néctar") };
	int action = r.action == "enemy" ? 1 : r.action == "pellet" ? 2 : r.action == "bomb" ? 3 : r.action == "nectar" ? 4 : 0;
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo(tr("Entonces ocurre"), &action, actions, 5)) {
		if (action == 0) r = nectar::DocReaction {};
		else r.action = action == 1 ? "enemy" : action == 2 ? "pellet" : action == 3 ? "bomb" : "nectar";
		if (action == 1 && !enemyInfo(r.value)) r.value = "chappy";
		if (action == 2 && !validPelletId(r.value)) r.value = "pr01";
		if (action >= 3) r.value.clear();
		changed = true;
	}
	if (action == 1) {
		const nectar::EnemyInfo* selected = enemyInfo(r.value);
		if (ImGui::BeginCombo(tr("Enemigo que aparece"), selected ? tr(selected->label) : r.value.c_str())) {
			for (const nectar::EnemyInfo& enemy : nectar::enemyTypes()) {
				if (ImGui::Selectable(tr(enemy.label), r.value == enemy.id)) {
					r.value = enemy.id;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
	} else if (action == 2) {
		int colour = pelletColour(r.value), value = pelletValue(r.value);
		const char* colours[] = { tr("Roja"), tr("Azul"), tr("Amarilla") };
		for (int c = 0; c < 3; c++) {
			if (c) ImGui::SameLine();
			if (choiceButton(colours[c], colour == c, ImVec2(78, 0))) {
				r.value = pelletId(c, value);
				changed = true;
			}
		}
		const int values[] = { 1, 5, 10, 20 };
		for (int k = 0; k < 4; k++) {
			if (k) ImGui::SameLine();
			char label[8];
			std::snprintf(label, sizeof(label), "%d", values[k]);
			if (choiceButton(label, value == values[k], ImVec2(55, 0))) {
				r.value = pelletId(colour, values[k]);
				changed = true;
			}
		}
	}
	if (action != 0) {
		if (ImGui::SliderInt(tr("Cantidad que aparece"), &r.count, 1, 8)) changed = true;
		if (r.count > 1) {
			float radius = float(r.radius);
			if (ImGui::SliderFloat(tr("Separación al aparecer"), &radius, 0.0f, 400.0f, "%.0f")) {
				r.radius = radius;
				changed = true;
			}
		}
	}
	return changed;
}

void App::drawSelected()
{
	MapDoc& d           = ed_.doc;
	const ItemRef r     = ed_.selection.back();
	const ItemCaps caps = itemCaps(r.type);
	const bool multi    = ed_.selection.size() > 1;
	bool ch             = false;
	if (multi) ImGui::TextColored(kDim, tr("%zu cosas elegidas"), ed_.selection.size());
	ImGui::SeparatorText(itemName(d, r).c_str());
	ImGui::SeparatorText(tr("Transformación"));
	Xform transform = getXform(d, r);
	auto syncMirror = [&](const ItemRef& selected) {
		ItemRef other;
		if (mirrorEdits_ && d.symmetric && counterpart(selected, other) && !ed_.selected(other)) mirrorToCounterpart(d, selected);
	};
	if (!multi) {
		bool transformChanged = false;
		float positionXZ[2] = { float(transform.x), float(transform.z) };
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat2(tr("Posición X / Z"), positionXZ, 1.0f, -10000.0f, 10000.0f, "%.1f")) {
			transform.x = positionXZ[0];
			transform.z = positionXZ[1];
			transformChanged = true;
		}
		if (caps.height) {
			float y = float(transform.y);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::DragFloat(r.type == ItemType::Object ? tr("Altura sobre el suelo") : tr("Posición Y"), &y, 1.0f, -2000.0f, 4000.0f, "%.1f")) {
				transform.y = y;
				transformChanged = true;
			}
		}
		if (caps.rotateAll) {
			float rotation[3] = { float(transform.rx), float(transform.ry), float(transform.rz) };
			ImGui::SetNextItemWidth(-1);
			if (ImGui::DragFloat3(tr("Rotación X / Y / Z"), rotation, 0.5f, -360.0f, 360.0f, "%.1f°")) {
				transform.rx = rotation[0];
				transform.ry = rotation[1];
				transform.rz = rotation[2];
				transformChanged = true;
			}
		} else if (caps.rotateY) {
			float rotationY = float(transform.ry);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::DragFloat(tr("Rotación Y"), &rotationY, 0.5f, -360.0f, 360.0f, "%.1f°")) {
				transform.ry = rotationY;
				transformChanged = true;
			}
		}
		if (caps.scale) {
			float scale = float(transform.scale);
			ImGui::SetNextItemWidth(-1);
			if (ImGui::DragFloat(tr("Escala uniforme"), &scale, 0.01f, 0.01f, 20.0f, "%.2f")) {
				transform.scale = scale;
				transformChanged = true;
			}
		}
		if (transformChanged) {
			setXform(d, r, transform);
			ItemRef other;
			if (mirrorEdits_ && d.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(d, r);
			ch = true;
		}
	} else {
		V3 pivot;
		selectionPivot(pivot);
		float center[2] = { pivot.x, pivot.z };
		ImGui::SetNextItemWidth(-1);
		if (ImGui::DragFloat2(tr("Centro X / Z"), center, 1.0f, -10000.0f, 10000.0f, "%.1f")) {
			const double dx = center[0] - pivot.x, dz = center[1] - pivot.z;
			for (const ItemRef& selected : ed_.selection) {
				Xform x = getXform(d, selected);
				const double oldGround = docHeight(float(x.x), float(x.z));
				x.x += dx;
				x.z += dz;
				if (selected.type == ItemType::Prop || selected.type == ItemType::Gen) x.y += docHeight(float(x.x), float(x.z)) - oldGround;
				setXform(d, selected, x);
				ItemRef other;
				if (mirrorEdits_ && d.symmetric && counterpart(selected, other) && !ed_.selected(other)) mirrorToCounterpart(d, selected);
			}
			ch = true;
		}
		const bool allHeight = std::all_of(ed_.selection.begin(), ed_.selection.end(), [](const ItemRef& item) { return itemCaps(item.type).height; });
		const bool allRotate = std::all_of(ed_.selection.begin(), ed_.selection.end(), [](const ItemRef& item) { return itemCaps(item.type).rotateY; });
		const bool allScale = std::all_of(ed_.selection.begin(), ed_.selection.end(), [](const ItemRef& item) { return itemCaps(item.type).scale; });
		auto differs = [&](auto value) {
			for (const ItemRef& selected : ed_.selection) if (std::fabs(value(getXform(d, selected)) - value(transform)) > 0.001) return true;
			return false;
		};
		if (allHeight) {
			if (differs([](const Xform& x) { return x.y; })) ImGui::TextColored(kDim, "%s", tr("— valores distintos"));
			float y = float(transform.y);
			if (ImGui::DragFloat(tr("Posición Y"), &y, 1.0f, -2000.0f, 4000.0f, "%.1f")) {
				for (const ItemRef& selected : ed_.selection) { Xform x = getXform(d, selected); x.y = y; setXform(d, selected, x); syncMirror(selected); }
				ch = true;
			}
		}
		if (allRotate) {
			if (differs([](const Xform& x) { return x.ry; })) ImGui::TextColored(kDim, "%s", tr("— valores distintos"));
			float ry = float(transform.ry);
			if (ImGui::DragFloat(tr("Rotación Y"), &ry, 0.5f, -360.0f, 360.0f, "%.1f°")) {
				for (const ItemRef& selected : ed_.selection) { Xform x = getXform(d, selected); x.ry = ry; setXform(d, selected, x); syncMirror(selected); }
				ch = true;
			}
		}
		if (allScale) {
			if (differs([](const Xform& x) { return x.scale; })) ImGui::TextColored(kDim, "%s", tr("— valores distintos"));
			float scale = float(transform.scale);
			if (ImGui::DragFloat(tr("Escala uniforme"), &scale, 0.01f, 0.01f, 20.0f, "%.2f")) {
				for (const ItemRef& selected : ed_.selection) { Xform x = getXform(d, selected); x.scale = scale; setXform(d, selected, x); syncMirror(selected); }
				ch = true;
			}
		}
	}
	hint(tr("Arrastra un valor o haz doble clic para escribir una cifra exacta."));
	const float transformButtonW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(multi);
	if (ImGui::Button(tr("Copiar transformación"), ImVec2(transformButtonW, 30))) {
		transformClipboard_ = getXform(d, r);
		transformClipboardCaps_ = caps;
		hasTransformClipboard_ = true;
		notify(0, tr("Transformación copiada."));
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!hasTransformClipboard_);
	if (ImGui::Button(tr("Pegar transformación"), ImVec2(-1, 30))) {
		Xform pasted = getXform(d, r);
		pasted.x = transformClipboard_.x;
		pasted.z = transformClipboard_.z;
		if (caps.height && transformClipboardCaps_.height) pasted.y = transformClipboard_.y;
		if (caps.rotateY && transformClipboardCaps_.rotateY) pasted.ry = transformClipboard_.ry;
		if (caps.rotateAll && transformClipboardCaps_.rotateAll) {
			pasted.rx = transformClipboard_.rx;
			pasted.rz = transformClipboard_.rz;
		}
		if (caps.scale && transformClipboardCaps_.scale) pasted.scale = transformClipboard_.scale;
		setXform(d, r, pasted);
		ItemRef other;
		if (mirrorEdits_ && d.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(d, r);
		ch = true;
	}
	ImGui::EndDisabled();
	if (ImGui::Button(tr("Restablecer transformación"), ImVec2(-1, 30))) {
		Xform reset = getXform(d, r);
		reset.x = reset.y = reset.z = 0.0;
		reset.rx = reset.ry = reset.rz = 0.0;
		reset.scale = 1.0;
		setXform(d, r, reset);
		ItemRef other;
		if (mirrorEdits_ && d.symmetric && counterpart(r, other) && !ed_.selected(other)) mirrorToCounterpart(d, r);
		ch = true;
	}
	ImGui::SetItemTooltip("%s", tr("Devuelve la posición y la rotación a cero, y la escala a uno. Puedes deshacerlo con Ctrl+Z."));
	ImGui::EndDisabled();

	if (selectionCanRotate()) {
		if (ImGui::Button(tr("Girar a la izquierda"), ImVec2(-1, 32))) rotateSelection(-cfg_.snapAngle);
		if (ImGui::Button(tr("Girar a la derecha"), ImVec2(-1, 32))) rotateSelection(cfg_.snapAngle);
	}
	const bool anyHeight = std::any_of(ed_.selection.begin(), ed_.selection.end(), [](const ItemRef& item) { return itemCaps(item.type).height; });
	if (anyHeight) {
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if (ImGui::Button(tr("Subir (Re Pág)"), ImVec2(half, 32))) liftSelection(ImGui::GetIO().KeyShift ? 100 : 20);
		ImGui::SameLine();
		if (ImGui::Button(tr("Hundir (Av Pág)"), ImVec2(half, 32))) liftSelection(ImGui::GetIO().KeyShift ? -100 : -20);
		hint(tr("Con Mayúsculas sube o baja más de golpe."));
	}
	if (selectionCanGround()) {
		if (ImGui::Button(tr("Colocar sobre el terreno"), ImVec2(-1, 32))) placeSelectionOnGround();
		ImGui::SetItemTooltip("%s", tr("Baja al suelo la decoración y los objetos seleccionados que estén flotando o enterrados."));
	}
	const auto i = std::size_t(r.index);
	if (!multi) {
	ImGui::SeparatorText(tr("Jugabilidad y propiedades"));
	switch (r.type) {
	case ItemType::Pellet: {
		std::string& id = d.pellets[i].id;
		int colour = pelletColour(id), value = pelletValue(id);
		const char* colours[] = { tr("Roja"), tr("Azul"), tr("Amarilla") };
		for (int c = 0; c < 3; c++) {
			if (c) ImGui::SameLine();
			if (choiceButton(colours[c], colour == c, ImVec2(90, 0))) {
				id = pelletId(c, value);
				ch = true;
			}
		}
		const int values[] = { 1, 5, 10, 20 };
		for (int k = 0; k < 4; k++) {
			if (k) ImGui::SameLine();
			char lbl[8];
			std::snprintf(lbl, sizeof(lbl), "%d", values[k]);
			if (choiceButton(lbl, value == values[k], ImVec2(64, 0))) {
				id = pelletId(colour, values[k]);
				ch = true;
			}
		}
		hint(tr("El número es cuántos Pikmin salen al llevarla a la cebolla de su color."));
		break;
	}
	case ItemType::Piece: {
		if (const nectar::ShipPartInfo* info = partInfo(d.pieces[i].part)) {
			ImGui::TextColored(kDim, tr("La levantan %d Pikmin (más rápido con %d)."), info->carryMin, info->carryMax);
			if (ImGui::SmallButton(tr("Que la elija el juego"))) {
				d.pieces[i].part.clear();
				ch = true;
			}
		} else {
			hint(tr("Pieza elegida por el juego según su tipo. Para una concreta, ponla desde «Poner cosas»."));
		}
		int& k = d.pieces[i].kind;
		if (choiceButton(tr("Normal"), k <= 2, ImVec2(-1, 0))) {
			k  = 0;
			ch = true;
		}
		if (choiceButton(tr("Vigilada (junto a un Bulborb)"), k == 3, ImVec2(-1, 0))) {
			k  = 3;
			ch = true;
		}
		if (choiceButton(tr("En el agua"), k == 4, ImVec2(-1, 0))) {
			k  = 4;
			ch = true;
		}
		hint(tr("Cada pieza llevada al cohete da puntos. La de en medio vale más."));
		break;
	}
	case ItemType::BigPiece: hint(tr("Aparece durante la partida (a los pocos minutos) y vale mucho.")); break;
	case ItemType::Enemy: {
		if (const nectar::EnemyInfo* info = enemyInfo(d.enemies[i].type)) hint(tr(info->hint));
		hint(tr("Se comporta como en el juego."));
		if (d.story.present) {
			// Misiones de varios días y campañas.
			nectar::DocEnemy& e        = d.enemies[i];
			static const char* kIds[]  = { "auto", "always", "never", "days" };
			const char* labels[]       = { tr("Como en el juego (los jefes no)"), tr("Siempre"), tr("Nunca"), tr("Al cabo de unos días") };
			int rule                   = 0;
			for (int k = 0; k < 4; k++) {
				if (e.respawn == kIds[k]) rule = k;
			}
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo(tr("Si se le derrota, vuelve"), &rule, labels, 4)) {
				e.respawn = kIds[rule];
				ch        = true;
			}
			if (rule == 3) ch |= ImGui::SliderInt(tr("Días hasta que vuelve"), &e.respawnDays, 1, 30);
			ch |= ImGui::SliderInt(tr("Sale desde el día"), &e.fromDay, 0, 30, e.fromDay ? "%d" : tr("el primero"));
			ch |= ImGui::SliderInt(tr("Sale hasta el día"), &e.untilDay, 0, 30, e.untilDay ? "%d" : tr("siempre"));
			if (e.untilDay && e.untilDay < e.fromDay) e.untilDay = e.fromDay;
		}
		break;
	}
	case ItemType::Gen: {
		nectar::DocGen& g = d.gens[i];
		hint(tr("Lo crea el juego igual que en la historia, y recuerda de un día a otro lo que pase con él."));
		nectar::GenSettings s;
		if (!nectar::readGenSettings(g, s)) break;
		bool gc = false;
		if (s.hasRadius) gc |= ImGui::SliderFloat(tr("Radio donde salen"), &s.radius, 0.0f, 500.0f, "%.0f");
		if (s.hasMinCount) gc |= ImGui::SliderInt(tr("Cuántos salen como mínimo"), &s.minCount, 0, 30);
		if (s.hasCount) gc |= ImGui::SliderInt(s.hasMinCount ? tr("Cuántos salen como máximo") : tr("Cuántos salen"), &s.count, 0, 30);
		if (s.hasFace) gc |= ImGui::SliderInt(tr("Giro"), &s.face, 0, 359, "%d°");
		if (s.hasRebirth) gc |= ImGui::SliderInt(tr("Vuelve a salir a los días"), &s.rebirthDays, 0, 30, s.rebirthDays ? "%d" : tr("no vuelve"));
		if (gc) {
			nectar::writeGenSettings(g, s);
			ch = true;
		}
		if (d.symmetric && (d.modes & nectar::MAPMODE_VS)) {
			ch |= ImGui::Checkbox(tr("Repetir en el lado del Jugador 2"), &g.mirror);
			hint(tr("En VS, el juego pone otro igual en el sitio contrario (girado media vuelta)."));
		}
		break;
	}
	case ItemType::Object: {
		nectar::DocObject& o            = d.objects[i];
		const nectar::ObjectInfo* info = objectInfo(o.kind);
		if (info) hint(tr(info->hint));
		if (o.kind.rfind("bridge", 0) == 0 || o.kind.rfind("slope", 0) == 0) {
			ImGui::Text("%s", tr("Trabajo para construirlo:"));
			const struct {
				const char* label;
				int work;
			} works[] = { { tr("Poco"), 1 }, { tr("Normal"), 2 }, { tr("Mucho"), 4 } };
			for (int k = 0; k < 3; k++) {
				if (k) ImGui::SameLine();
				if (choiceButton(works[k].label, o.work == works[k].work, ImVec2(90, 0))) {
					o.work = works[k].work;
					ch     = true;
				}
			}
			hint(tr("Se construye hacia donde mira: gíralo con el aro (E)."));
		}
		if (o.kind == "push_rock") {
			float push = float(o.push);
			if (ImGui::SliderFloat(tr("Hasta dónde se empuja"), &push, 50, 1500, "%.0f")) {
				o.push = push;
				ch     = true;
			}
			hint(tr("La línea amarilla enseña hacia dónde va. Gíralo con el aro (E)."));
		}
		break;
	}
	case ItemType::Guard: hint(tr("Un Bulborb dormido: si los Pikmin pasan cerca, se despierta.")); break;
	case ItemType::Onion: hint(tr("Aquí se llevan las pastillas de su color para conseguir Pikmin.")); break;
	case ItemType::Base: hint(tr("Donde empieza el capitán de este jugador.")); break;
	case ItemType::Rocket: hint(tr("Aquí se llevan las piezas. Si el rival lo destruye, pierdes.")); break;
	case ItemType::Gate: hint(tr("Una puerta de roca que se rompe con rocas bomba: un atajo.")); break;
	case ItemType::BombPile: hint(tr("Rocas bomba para que los Pikmin amarillos rompan la puerta.")); break;
	case ItemType::Captain: hint(tr("Donde empieza este capitán en solo y coop. Gíralo con el aro (E).")); break;
	case ItemType::Ship: hint(tr("Aquí se llevan las piezas de la nave en solo y coop.")); break;
	case ItemType::StoryOnion: {
		nectar::DocStoryOnion& o = d.story.onions[i];
		const char* colours[]    = { tr("Azul"), tr("Roja"), tr("Amarilla") };
		for (int c = 0; c < 3; c++) {
			if (c) ImGui::SameLine();
			if (choiceButton(colours[c], o.color == c, ImVec2(80, 0))) {
				o.color = c;
				ch      = true;
			}
		}
		ch |= ImGui::Checkbox(tr("Enterrada (hay que descubrirla)"), &o.buried);
		if (o.buried) hint(tr("Sale dormida: se despierta (y da su primera semilla) cuando un capitán se acerca."));
		else if (d.modes & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP))
			hint(tr("En una campaña, las cebollas que aún no se tienen salen siempre enterradas."));
		hint(tr("Las pastillas de su color dan Pikmin aquí. Solo una cebolla de cada color."));
		break;
	}
	case ItemType::ShipPart: {
		nectar::DocShipPart& p = d.story.parts[i];
		if (ImGui::BeginCombo(tr("Pieza"), p.id.c_str())) {
			for (const nectar::ShipPartInfo& info : nectar::shipParts()) {
				char text[96];
				std::snprintf(text, sizeof(text), tr("%s (la levantan %d Pikmin)"), info.id, info.carryMin);
				if (ImGui::Selectable(text, p.id == info.id)) {
					p.id = info.id;
					ch   = true;
				}
			}
			ImGui::EndCombo();
		}
		hint(tr("Hay que llevarla a la nave. Cada pieza solo puede estar una vez."));
		break;
	}
	case ItemType::Prop: {
		nectar::DocProp& p = d.props[i];
		ch |= ImGui::Checkbox(tr("Los Pikmin no lo atraviesan"), &p.collide);
		if (p.collide) {
			ch |= ImGui::Checkbox(tr("Se puede pisar (es suelo)"), &p.ground);
			hint(tr("Para mallas que son parte del nivel (troncos, puentes, rampas, salas): los Pikmin andan por encima y no cortan los caminos."));
		}
		break;
	}
	case ItemType::Wall: {
		nectar::DocWall& w = d.walls[i];
		float h = float(w.height), t = float(w.thickness);
		if (ImGui::SliderFloat(tr("Altura"), &h, 20, 600, "%.0f")) {
			w.height = h;
			ch       = true;
		}
		if (ImGui::SliderFloat(tr("Grosor"), &t, 10, 200, "%.0f")) {
			w.thickness = t;
			ch          = true;
		}
		if (!w.isArc) hint(tr("Arrastra los círculos de los extremos para alargarlo o girarlo."));
		if (ImGui::Checkbox(tr("Muro curvo"), &w.isArc)) {
			ch = true;
			if (w.isArc) {
				w.cx     = (w.ax + w.bx) * 0.5;
				w.cz     = (w.az + w.bz) * 0.5;
				w.radius = std::max(60.0, std::hypot(w.bx - w.ax, w.bz - w.az) * 0.5);
				w.from   = 0;
				w.to     = 90;
			} else {
				w.ax = w.cx - w.radius;
				w.az = w.cz;
				w.bx = w.cx + w.radius;
				w.bz = w.cz;
			}
		}
		if (w.isArc) {
			float rad = float(w.radius), open = float(w.to - w.from);
			if (ImGui::SliderFloat(tr("Radio"), &rad, 40, 2000, "%.0f")) {
				w.radius = rad;
				ch       = true;
			}
			if (ImGui::SliderFloat(tr("Cuánto cierra"), &open, 10, 360, "%.0f°")) {
				w.to = w.from + open;
				ch   = true;
			}
			w.segments = std::max(3, int(std::fabs(w.to - w.from) / 15.0));
		}
		break;
	}
	case ItemType::Pond: {
		nectar::DocPond& p = d.ponds[i];
		float rx = float(p.rx), rz = float(p.rz), island = float(p.island);
		if (ImGui::SliderFloat(tr("Ancho"), &rx, 40, 1500, "%.0f")) {
			p.rx = rx;
			ch   = true;
		}
		if (ImGui::SliderFloat(tr("Largo"), &rz, 40, 1500, "%.0f")) {
			p.rz = rz;
			ch   = true;
		}
		if (ImGui::SliderFloat(tr("Isla en el centro"), &island, 0, float(std::min(p.rx, p.rz)) - 20, "%.0f")) {
			p.island = island;
			ch       = true;
		}
		hint(tr("Solo los Pikmin azules pueden entrar en el agua."));
		break;
	}
	}

	nectar::DocReaction* reaction = nullptr;
	const char* reactionTrigger = nullptr;
	if (r.type == ItemType::Enemy) {
		reaction = &d.enemies[i].reaction;
		reactionTrigger = tr("Al derrotar al enemigo");
	} else if (r.type == ItemType::Guard) {
		reaction = &d.guards[i].reaction;
		reactionTrigger = tr("Al derrotar al guardián");
	} else if (r.type == ItemType::Gate) {
		reaction = &d.gates[r.index].reaction;
		reactionTrigger = tr("Al destruir la compuerta");
	} else if (r.type == ItemType::Object && d.objects[i].kind != "nectar") {
		reaction = &d.objects[i].reaction;
		const std::string& kind = d.objects[i].kind;
		reactionTrigger = kind.rfind("bridge", 0) == 0 || kind.rfind("slope", 0) == 0 ? tr("Al terminar de construirlo")
		                : kind == "push_rock" ? tr("Al terminar de empujarlo")
		                : kind == "key" ? tr("Al recoger la llave")
		                : kind == "key_door" ? tr("Al abrir la puerta")
		                : kind.rfind("gate_", 0) == 0 ? tr("Al destruir la compuerta") : tr("Al destruirlo");
	}
	if (reaction) {
		bool reactionChanged = false;
		ImGui::PushID("reaction");
		ImGui::SeparatorText(tr("Reacción al interactuar"));
		ImGui::TextColored(kDim, "%s", reactionTrigger);
		ch |= reactionChanged = editReaction(*reaction);
		if (!reaction->action.empty()) {
			hint(tr("La reacción ocurre una sola vez y aparece alrededor del elemento."));
			if (d.symmetric && (r.type == ItemType::Enemy || r.type == ItemType::Guard || r.type == ItemType::Object))
				hint(tr("En un mapa espejo, cada copia activa su propia reacción."));
		}
		if (reactionChanged && r.type == ItemType::Gate && mirrorEdits_ && d.symmetric) d.gates[1 - r.index].reaction = *reaction;
		ImGui::PopID();
	}
	}
	if (!multi && ImGui::CollapsingHeader(tr("Información técnica"))) {
		ImGui::TextColored(kDim, "%s", tr("Referencia interna"));
		ImGui::SameLine();
		ImGui::Text("%d:%d:%d", int(r.type), r.index, r.sub);
		const std::string asset = markerAsset(r);
		if (!asset.empty()) {
			ImGui::TextColored(kDim, "%s", tr("Ruta del asset"));
			ImGui::TextWrapped("%s", asset.c_str());
			if (ImGui::SmallButton(tr("Copiar ruta"))) {
				ImGui::SetClipboardText(asset.c_str());
				notify(0, tr("Ruta copiada al portapapeles."));
			}
		}
	}

	ImGui::Spacing();
	ImGui::SeparatorText(tr("Acciones"));
	if (!multi && caps.perPlayer) {
		ImGui::TextColored(r.index == 0 ? ImVec4(1, 0.5f, 0.45f, 1) : ImVec4(0.45f, 0.65f, 1, 1), tr("Es del %s."), r.index == 0 ? tr("Jugador 1") : tr("Jugador 2"));
		if (d.symmetric && mirrorEdits_) hint(tr("Al moverlo, el del otro jugador se coloca solo en el lado contrario."));
	}
	if (!multi && caps.twinned && d.symmetric) hint(tr("Como el mapa es espejo, el otro jugador tiene uno igual en su lado (se ve más claro)."));
	const bool anyRemovable = std::any_of(ed_.selection.begin(), ed_.selection.end(), [](const ItemRef& item) { return itemCaps(item.type).removable; });
	if (anyRemovable) {
		if (ImGui::Button(tr("Duplicar (Ctrl+D)"), ImVec2(150, 32))) duplicateSelection();
		ImGui::SameLine();
		if (ImGui::Button(tr("Borrar (Supr)"), ImVec2(-1, 32))) {
			deleteSelection();
			return;
		}
	}
	if (ImGui::Button(tr("Ver de cerca (F)"), ImVec2(-1, 0))) focusSelection();
	if (ch) ed_.touch();
}

void App::drawMapPanel()
{
	MapDoc& d = ed_.doc;
	bool ch   = false;
	char buf[256];
	if (ImGui::CollapsingHeader(tr("General"), ImGuiTreeNodeFlags_DefaultOpen)) {
	copyStr(buf, sizeof(buf), d.name);
	ImGui::SetNextItemWidth(-90);
	if (ImGui::InputText(tr("Nombre"), buf, sizeof(buf))) {
		d.name = buf;
		ch     = true;
	}
	copyStr(buf, sizeof(buf), d.author);
	ImGui::SetNextItemWidth(-90);
	if (ImGui::InputText(tr("Autor"), buf, sizeof(buf))) {
		d.author = buf;
		ch       = true;
	}
	ImGui::Spacing();
	// El espejo es cosa del VS (dos jugadores, un lado cada uno).
	if (d.modes & nectar::MAPMODE_VS) {
		ch |= ImGui::Checkbox(tr("Mapa espejo"), &d.symmetric);
		hint(d.symmetric ? tr("Lo que pongas en un lado aparece igual en el otro, para que sea justo para los dos jugadores.")
		                 : tr("Cada lado es distinto: tú decides dónde va todo."));
		if (d.symmetric) ImGui::Checkbox(tr("Mover a la vez lo de los dos jugadores"), &mirrorEdits_);
		ImGui::Checkbox(tr("Ver el lado del otro jugador"), &showTwins_);
	}
	}

	if (ImGui::CollapsingHeader(tr("Modos de juego"), ImGuiTreeNodeFlags_DefaultOpen)) {
		bool vs = (d.modes & nectar::MAPMODE_VS) != 0, solo = (d.modes & nectar::MAPMODE_SOLO) != 0, coop = (d.modes & nectar::MAPMODE_COOP) != 0;
		bool changed = ImGui::Checkbox("VS", &vs);
		ImGui::SameLine();
		changed |= ImGui::Checkbox(tr("Solo"), &solo);
		ImGui::SameLine();
		changed |= ImGui::Checkbox(tr("Coop"), &coop);
		if (changed) {
			const unsigned m = (vs ? nectar::MAPMODE_VS : 0u) | (solo ? nectar::MAPMODE_SOLO : 0u) | (coop ? nectar::MAPMODE_COOP : 0u);
			if (!m) {
				notify(1, tr("El mapa tiene que servir para algún modo."));
			} else {
				if (d.modes & ~m) notify(0, tr("Lo de ese modo no se borra: vuelve si lo marcas otra vez."));
				d.modes = m;
				if (solo || coop) nm::ensureStory(d, coop);
				ed_.pruneSelection();
				ch = true;
			}
		}
		hint(tr("VS: dos jugadores, uno contra otro. Solo y coop: una misión con capitanes, nave, cebollas y un objetivo."));
	}
	if (d.story.present && (d.modes & (nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP)) && ImGui::CollapsingHeader(tr("Misión (solo y coop)"), ImGuiTreeNodeFlags_DefaultOpen)) {
		nectar::DocRules& rules = d.story.rules;
		static const char* const kGoalIds[] = { "parts", "pellets", "pikmin", "boss", "survive", "waves" };
		const char* goals[] = { tr("Recuperar piezas de la nave"), tr("Llevar pastillas a las cebollas"), tr("Conseguir Pikmin"), tr("Derrotar a los jefes"),
			                    tr("Aguantar los días"), tr("Aguantar oleadas") };
		int goal = 0;
		for (int k = 0; k < 6; k++) {
			if (rules.objective == kGoalIds[k]) goal = k;
		}
		ImGui::SetNextItemWidth(-90);
		if (ImGui::Combo(tr("Objetivo"), &goal, goals, 6)) {
			rules.objective = kGoalIds[goal];
			if (goal == 5 && rules.target < 1) rules.target = 3;
			ch              = true;
		}
		if (goal == 5) {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(tr("Oleadas"), &rules.target, 1, 50);
			hint(tr("Marca como «oleada» los disparadores que sacan enemigos (Disparadores, abajo)."));
		}
		{
			int limit = rules.timeLimit;
			ImGui::SetNextItemWidth(-90);
			if (ImGui::SliderInt(tr("Límite de tiempo"), &limit, 0, 1200, limit ? "%d s" : tr("Sin límite"))) {
				rules.timeLimit = limit;
				ch              = true;
			}
			if (rules.timeLimit) {
				if (choiceButton(tr("Al acabar, pierdes"), rules.timeLimitEnd != "endday", ImVec2(150, 0))) rules.timeLimitEnd = "lose", ch = true;
				ImGui::SameLine();
				if (choiceButton(tr("Al acabar, se acaba el día"), rules.timeLimitEnd == "endday", ImVec2(190, 0))) rules.timeLimitEnd = "endday", ch = true;
			}
		}
		if (goal <= 2) {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(goal == 2 ? tr("Cuántos") : tr("Cuántas"), &rules.target, goal == 2 ? 1 : 0, goal == 2 ? 100 : 30,
			                       goal != 2 && rules.target == 0 ? tr("Todas") : "%d");
		}
		ImGui::SetNextItemWidth(-90);
		ch |= ImGui::SliderInt(tr("Días"), &rules.days, 1, 30);
		ImGui::SetNextItemWidth(-90);
		ImGui::SliderInt(tr("Ver el día"), &viewDay_, 0, 30, viewDay_ ? "%d" : tr("todos"));
		ImGui::SetItemTooltip("%s", tr("Enseña solo los enemigos que salen ese día (según «Sale desde/hasta el día»). No cambia el mapa."));
		ImGui::Text("%s", tr("Pikmin al empezar (en el grupo):"));
		const char* colours[] = { tr("Azules"), tr("Rojos"), tr("Amarillos") };
		for (int c = 0; c < 3; c++) {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(colours[c], &rules.startPikmin[c], 0, 100);
		}
		hint(tr("Al anochecer, los Pikmin que no estén en el grupo ni junto a una cebolla o la nave se pierden. Con varios días, la partida se guarda cada noche."));

		if (ImGui::CollapsingHeader(tr("Medallas"))) {
			const char* names[] = { tr("Bronce"), tr("Plata"), tr("Oro") };
			for (int k = 0; k < 3; k++) {
				ImGui::SetNextItemWidth(-90);
				ch |= ImGui::SliderInt(names[k], &rules.medals[k], 0, 100, rules.medals[k] ? tr("%d Pikmin") : tr("No"));
			}
			hint(tr("Pikmin que hay que tener al cumplir la misión para cada medalla (bronce <= plata <= oro)."));
		}

		if (ImGui::CollapsingHeader(tr("Disparadores"))) {
			hint(tr("Cuando se cumplen sus condiciones, hacen sus acciones: mensajes, cuadros de texto, que aparezcan cosas, romper construcciones, cuentas atrás, ganar o perder."));
			std::vector<nectar::DocTrigger>& triggers = d.story.triggers;
			static const char* const kCondIds[] = { "parts", "pellets", "bosses", "enemies", "day", "hour", "time", "after", "area", "object", "pikmin" };
			const char* conds[] = { tr("Piezas entregadas"), tr("Pastillas entregadas"), tr("Jefes derrotados"), tr("Enemigos derrotados hoy"),
				                    tr("Desde el día"), tr("El reloj llega a las"), tr("Segundos desde que empieza el día"), tr("Segundos desde otro disparador"),
				                    tr("Un capitán entra en una zona"), tr("Una construcción terminada o rota"), tr("Pikmin que tienes") };
			static const char* const kActIds[] = { "message", "textbox", "spawn", "spawn_at", "break", "win", "lose", "endday", "countdown", "camera" };
			const char* acts[] = { tr("Mensaje"), tr("Cuadro de texto (pausa)"), tr("Aparecen cosas en un punto"), tr("Aparecen cosas junto a una construcción"),
				                   tr("Romper o terminar una construcción"), tr("Ganar"), tr("Perder"), tr("Acabar el día"), tr("Cuenta atrás"),
				                   tr("Mover la cámara a un punto") };
			auto indexOf = [](const char* const* ids, int n, const std::string& v) {
				for (int k = 0; k < n; k++) {
					if (v == ids[k]) return k;
				}
				return 0;
			};
			// Elegir una construcción del mapa por su nombre.
			auto objectCombo = [&](int& index) {
				bool changed = false;
				const std::string cur =
				    index >= 0 && index < int(d.objects.size()) ? std::to_string(index + 1) + ". " + itemName(d, { ItemType::Object, index, 0 }) : tr("(ninguna)");
				ImGui::SetNextItemWidth(-90);
				if (ImGui::BeginCombo(tr("Construcción"), cur.c_str())) {
					for (int k = 0; k < int(d.objects.size()); k++) {
						const std::string name = std::to_string(k + 1) + ". " + itemName(d, { ItemType::Object, k, 0 });
						if (ImGui::Selectable(name.c_str(), k == index)) {
							index   = k;
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				if (d.objects.empty()) hint(tr("Pon antes alguna construcción en el mapa."));
				return changed;
			};
			// Un punto del mapa (con «donde está lo elegido»).
			auto pointEdit = [&](double& x, double& z) {
				bool changed = false;
				float pos[2] = { float(x), float(z) };
				ImGui::SetNextItemWidth(-90);
				if (ImGui::DragFloat2(tr("Dónde"), pos, 2.0f, -10000.0f, 10000.0f, "%.0f")) {
					x       = pos[0];
					z       = pos[1];
					changed = true;
				}
				ImGui::BeginDisabled(ed_.selection.empty());
				if (ImGui::SmallButton(tr("Donde está lo elegido"))) {
					const Xform xf = getXform(d, ed_.selection.back());
					x              = xf.x;
					z              = xf.z;
					changed        = true;
				}
				ImGui::EndDisabled();
				return changed;
			};
			auto textEdit = [&](const char* label, std::string& text) {
				char buf[256];
				copyStr(buf, sizeof(buf), text);
				ImGui::SetNextItemWidth(-90);
				if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
				text = buf;
				return true;
			};
			int remove = -1;
			for (size_t t = 0; t < triggers.size(); t++) {
				nectar::DocTrigger& trig = triggers[t];
				ImGui::PushID(int(t));
				char head[48];
				std::snprintf(head, sizeof(head), tr("Disparador %zu"), t + 1);
				ImGui::SeparatorText(head);

				// ── Cuándo ──
				ImGui::TextColored(kDim, "%s", tr("Cuando..."));
				if (trig.conditions.size() > 1) {
					if (choiceButton(tr("Se cumplen todas"), !trig.any, ImVec2(140, 0))) trig.any = false, ch = true;
					ImGui::SameLine();
					if (choiceButton(tr("Se cumple alguna"), trig.any, ImVec2(140, 0))) trig.any = true, ch = true;
				}
				int removeCond = -1;
				for (size_t c = 0; c < trig.conditions.size(); c++) {
					nectar::DocCondition& cond = trig.conditions[c];
					ImGui::PushID(int(c) + 1000);
					int type = indexOf(kCondIds, 11, cond.type);
					ImGui::SetNextItemWidth(-30);
					if (ImGui::Combo("##cond", &type, conds, 11)) {
						cond      = nectar::DocCondition();
						cond.type = kCondIds[type];
						if (cond.type == "hour") cond.count = 12;
						if (cond.type == "time" || cond.type == "after") cond.count = 60;
						if (cond.type == "area") {
							cond.x = (d.minX + d.maxX) * 0.5;
							cond.z = (d.minZ + d.maxZ) * 0.5;
						}
						ch = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) removeCond = int(c);
					const std::string& ty = cond.type;
					if (ty == "hour") {
						ImGui::SetNextItemWidth(-90);
						ch |= ImGui::SliderInt(tr("Hora"), &cond.count, 5, 22, "%d h");
					} else if (ty == "time" || ty == "after") {
						ImGui::SetNextItemWidth(-90);
						ch |= ImGui::SliderInt(tr("Segundos"), &cond.count, 0, 1200);
						if (ty == "after") {
							int other = cond.index + 1;
							ImGui::SetNextItemWidth(-90);
							if (ImGui::SliderInt(tr("Disparador"), &other, 1, std::max(1, int(triggers.size())))) {
								cond.index = other - 1;
								ch         = true;
							}
						}
					} else if (ty == "area") {
						ch |= pointEdit(cond.x, cond.z);
						float r = float(cond.radius);
						ImGui::SetNextItemWidth(-90);
						if (ImGui::SliderFloat(tr("Radio"), &r, 20.0f, 1500.0f, "%.0f")) {
							cond.radius = r;
							ch          = true;
						}
					} else if (ty == "object") {
						ch |= objectCombo(cond.index);
					} else {
						if (ty == "pikmin") {
							const char* colors[] = { tr("En total"), tr("Azules"), tr("Rojos"), tr("Amarillos") };
							int col              = cond.color + 1;
							ImGui::SetNextItemWidth(-90);
							if (ImGui::Combo(tr("Color"), &col, colors, 4)) {
								cond.color = col - 1;
								ch         = true;
							}
						}
						ImGui::SetNextItemWidth(-90);
						ch |= ImGui::SliderInt(ty == "day" ? tr("Día") : tr("Cuántos"), &cond.count, 1, ty == "pikmin" ? 100 : 30);
					}
					ImGui::PopID();
				}
				if (removeCond >= 0) {
					trig.conditions.erase(trig.conditions.begin() + removeCond);
					ch = true;
				}
				if (trig.conditions.size() < 8 && ImGui::SmallButton(tr("+ condición"))) {
					trig.conditions.push_back(nectar::DocCondition());
					ch = true;
				}

				// ── Qué pasa ──
				ImGui::TextColored(kDim, "%s", tr("Entonces..."));
				int removeAct = -1;
				for (size_t a = 0; a < trig.actions.size(); a++) {
					nectar::DocAction& act = trig.actions[a];
					ImGui::PushID(int(a) + 2000);
					int type = indexOf(kActIds, 10, act.type);
					ImGui::SetNextItemWidth(-30);
					if (ImGui::Combo("##act", &type, acts, 10)) {
						act      = nectar::DocAction();
						act.type = kActIds[type];
						if (act.type == "camera") {
							act.seconds = 3;
							act.x       = (d.minX + d.maxX) * 0.5;
							act.z       = (d.minZ + d.maxZ) * 0.5;
						}
						if (act.type == "spawn" || act.type == "spawn_at") {
							act.reaction.action = "enemy";
							act.reaction.value  = "chappy";
							act.x               = (d.minX + d.maxX) * 0.5;
							act.z               = (d.minZ + d.maxZ) * 0.5;
						}
						ch = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("x")) removeAct = int(a);
					const std::string& ty = act.type;
					if (ty == "message" || ty == "textbox" || ty == "countdown") ch |= textEdit(tr("Texto"), act.text);
					if (ty == "spawn" || ty == "spawn_at") {
						ch |= editReaction(act.reaction);
						if (act.reaction.action.empty()) act.reaction.action = "enemy"; // «aparecer» sin qué no vale
						if (ty == "spawn") ch |= pointEdit(act.x, act.z);
						else ch |= objectCombo(act.index);
					}
					if (ty == "break") ch |= objectCombo(act.index);
					if (ty == "camera") {
						ch |= pointEdit(act.x, act.z);
						ImGui::SetNextItemWidth(-90);
						ch |= ImGui::SliderInt(tr("Segundos"), &act.seconds, 1, 30);
						hint(tr("La cámara mira lo que haya más cerca de ese punto (construcción, enemigo, cebolla, nave...) y luego vuelve. Mientras, nadie se mueve."));
					}
					if (ty == "countdown") {
						ImGui::SetNextItemWidth(-90);
						ch |= ImGui::SliderInt(tr("Segundos"), &act.seconds, 1, 1200);
						static const char* const kThen[] = { "", "lose", "win", "endday" };
						const char* thens[]              = { tr("Nada"), tr("Perder"), tr("Ganar"), tr("Acabar el día") };
						int then                         = indexOf(kThen, 4, act.then);
						ImGui::SetNextItemWidth(-90);
						if (ImGui::Combo(tr("Al llegar a cero"), &then, thens, 4)) {
							act.then = kThen[then];
							ch       = true;
						}
					}
					ImGui::PopID();
				}
				if (removeAct >= 0) {
					trig.actions.erase(trig.actions.begin() + removeAct);
					ch = true;
				}
				if (trig.actions.size() < 8 && ImGui::SmallButton(tr("+ acción"))) {
					trig.actions.push_back(nectar::DocAction());
					ch = true;
				}

				// ── Repetir ──
				ch |= ImGui::Checkbox(tr("Se repite"), &trig.repeat);
				if (trig.repeat) {
					ImGui::SetNextItemWidth(-90);
					ch |= ImGui::SliderInt(tr("Espera (s)"), &trig.cooldown, 1, 600);
					ImGui::SetNextItemWidth(-90);
					ch |= ImGui::SliderInt(tr("Veces"), &trig.times, 0, 50, trig.times ? "%d" : tr("sin fin"));
				}
				ch |= ImGui::Checkbox(tr("Es una oleada"), &trig.wave);
				if (trig.wave) hint(tr("Cuenta para el objetivo «Aguantar oleadas»: una oleada termina cuando no queda nadie de los que sacó."));
				if (ImGui::Button(tr("Quitar disparador"))) remove = int(t);
				ImGui::PopID();
			}
			if (remove >= 0) {
				triggers.erase(triggers.begin() + remove);
				ch = true;
			}
			if (triggers.size() < 32 && ImGui::Button(tr("Añadir disparador"), ImVec2(-1, 0))) {
				nectar::DocTrigger t;
				t.conditions.push_back(nectar::DocCondition());
				t.actions.push_back(nectar::DocAction());
				t.actions.back().text = tr("¡Algo ha pasado!");
				triggers.push_back(t);
				ch = true;
			}
		}
	}
	hint(tr("Elige algo del mapa con «Mover y girar» para ver sus opciones aquí."));

	if (ImGui::CollapsingHeader(tr("Ambiente"))) {
		if (d.story.present) {
			// Misiones: música de una zona del juego y horas del día.
			const char* musics[] = { tr("La de siempre"), "Impact Site", "Forest of Hope", "Forest Navel", "Distant Spring", "Final Trial" };
			int music            = 0;
			for (size_t k = 0; k < nectar::musicNames().size(); k++) {
				if (d.music == nectar::musicNames()[k]) music = int(k) + 1;
			}
			ImGui::SetNextItemWidth(-90);
			if (ImGui::Combo(tr("Música"), &music, musics, 6)) {
				d.music = music ? nectar::musicNames()[size_t(music - 1)] : "";
				ch      = true;
			}
			float hours[2] = { float(d.startHour), float(d.endHour) };
			ImGui::SetNextItemWidth(-90);
			if (ImGui::SliderFloat(tr("Amanece a las"), &hours[0], 0.0f, 20.0f, hours[0] > 0.0f ? "%.1f h" : tr("como en el juego"))) {
				d.startHour = hours[0] < 5.0f ? 0.0 : double(hours[0]);
				ch          = true;
			}
			ImGui::SetNextItemWidth(-90);
			if (ImGui::SliderFloat(tr("Anochece a las"), &hours[1], 0.0f, 22.0f, hours[1] > 0.0f ? "%.1f h" : tr("como en el juego"))) {
				d.endHour = hours[1] < 6.0f ? 0.0 : double(hours[1]);
				ch        = true;
			}
			hint(tr("Días más cortos o más largos en las misiones. Menos de 5 h (o 6 h) = como en el juego."));
		}
		hint(tr("La luz, la niebla y los colores del cielo salen de un escenario del juego."));
		if (ImGui::BeginCombo(tr("Como en"), sceneName(d.baseStage).c_str())) {
			for (const nectar::CatalogEntry& e : catalog_.entries) {
				if (e.kind != nectar::AssetKind::Scene || !e.error.empty()) continue;
				const std::string name = sceneName(e.path);
				if (name == e.path) continue; // solo los escenarios de verdad
				if (ImGui::Selectable((name + "##" + e.path).c_str(), e.path == d.baseStage)) {
					d.baseStage = e.path;
					ch          = true;
				}
			}
			ImGui::EndCombo();
		}
		float sun = float(std::atan2(d.sun[2], d.sun[0]) / kDeg);
		if (ImGui::SliderFloat(tr("De dónde viene el sol"), &sun, -180, 180, "%.0f°")) {
			const double a = sun * kDeg, len = std::hypot(d.sun[0], d.sun[2]);
			d.sun[0]       = std::cos(a) * len;
			d.sun[2]       = std::sin(a) * len;
			ch             = true;
		}
		float bright = float(d.brightness), fog = float(d.fogScale);
		if (ImGui::SliderFloat(tr("Brillo"), &bright, 0.4f, 1.6f, "%.2f")) {
			d.brightness = bright;
			ch           = true;
		}
		float tint[3] = { d.tint[0] / 255.0f, d.tint[1] / 255.0f, d.tint[2] / 255.0f };
		if (ImGui::ColorEdit3(tr("Color de la luz"), tint)) {
			for (int i = 0; i < 3; i++) d.tint[i] = int(std::lround(tint[i] * 255.0f));
			ch = true;
		}
		if (ImGui::SliderFloat(tr("Niebla (distancia)"), &fog, 0.2f, 4.0f, "x%.2f")) {
			d.fogScale = fog;
			ch         = true;
		}
		hint(tr("Retoques sobre la luz del escenario elegido: más oscuro o más claro, luz cálida o fría, niebla más cerca (cuevas, bosques espesos) o más lejos."));
	}
	if (ImGui::CollapsingHeader(tr("Opciones avanzadas"))) {
		hint(tr("Los Pikmin siguen una red de caminos que se hace sola. Aquí puedes ajustarla."));
		float step = float(d.routeStep);
		if (ImGui::SliderFloat(tr("Separación de caminos"), &step, 80, 300, "%.0f")) {
			d.routeStep = step;
			ch          = true;
		}
		hint(tr("Líneas que los Pikmin solo cruzan por puntos concretos (como la muralla de cada base)."));
		for (std::size_t k = 0; k < d.barrierX.size(); k++) {
			ImGui::PushID(int(k));
			float v = float(d.barrierX[k]);
			ImGui::SetNextItemWidth(160);
			if (ImGui::DragFloat(tr("Línea en x"), &v, 1, -6000, 6000, "%.0f")) {
				d.barrierX[k] = v;
				ch            = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(tr("Quitar"))) {
				d.barrierX.erase(d.barrierX.begin() + long(k));
				ch = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		for (std::size_t k = 0; k < d.passes.size(); k++) {
			ImGui::PushID(1000 + int(k));
			float v[2] = { float(d.passes[k].first), float(d.passes[k].second) };
			ImGui::SetNextItemWidth(160);
			if (ImGui::DragFloat2(tr("Paso"), v, 1)) {
				d.passes[k] = { v[0], v[1] };
				ch          = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(tr("Quitar"))) {
				d.passes.erase(d.passes.begin() + long(k));
				ch = true;
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		if (ImGui::SmallButton(tr("Añadir línea"))) {
			d.barrierX.push_back(0);
			ch = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(tr("Añadir paso"))) {
			d.passes.push_back({ 0, 0 });
			ch = true;
		}
	}
	if (ch) ed_.touch();
}

void App::drawHierarchy()
{
	const MapDoc& d = ed_.doc;
	const std::vector<ItemRef> items = allItems(d);
	ImGui::SetNextItemWidth(hierarchySearch_[0] ? -30.0f : -1.0f);
	ImGui::InputTextWithHint("##hierarchy_search", tr("Buscar..."), hierarchySearch_, sizeof(hierarchySearch_));
	if (hierarchySearch_[0]) {
		ImGui::SameLine();
		if (ImGui::SmallButton("×##clear_hierarchy_search")) hierarchySearch_[0] = '\0';
	}
	const std::string query = searchKey(hierarchySearch_);
	struct Group {
		const char* name;
		int id;
	};
	const Group groups[] = { { tr("Misión (solo y coop)"), 8 }, { tr("Jugador 1"), 0 }, { tr("Jugador 2"), 1 }, { tr("Objetivos"), 2 }, { tr("Enemigos"), 3 },
	                         { tr("Construcciones"), 4 }, { tr("Muros y agua"), 5 }, { tr("Decoración"), 6 }, { tr("Elementos del escenario"), 7 } };
	auto groupOf = [](const ItemRef& r) {
		if (isStoryItem(r.type)) return 8;
		if (itemCaps(r.type).perPlayer) return r.index == 0 ? 0 : 1;
		switch (r.type) {
		case ItemType::Pellet:
		case ItemType::Piece:
		case ItemType::BigPiece: return 2;
		case ItemType::Enemy:
		case ItemType::Guard: return 3;
		case ItemType::Object: return 4;
		case ItemType::Wall:
		case ItemType::Pond: return 5;
		case ItemType::Prop: return 6;
		case ItemType::Gen: return 7;
		default: return 7;
		}
	};
	auto sameKind = [&](const ItemRef& a, const ItemRef& b) {
		if (a.type != b.type) return false;
		switch (a.type) {
		case ItemType::Onion: return a.sub == b.sub;
		case ItemType::Pellet: return d.pellets[std::size_t(a.index)].id == d.pellets[std::size_t(b.index)].id;
		case ItemType::Piece: {
			const nectar::DocPiece& x = d.pieces[std::size_t(a.index)], &y = d.pieces[std::size_t(b.index)];
			return x.kind == y.kind && x.part == y.part;
		}
		case ItemType::Guard: return d.guards[std::size_t(a.index)].enemy == d.guards[std::size_t(b.index)].enemy;
		case ItemType::Enemy: return d.enemies[std::size_t(a.index)].type == d.enemies[std::size_t(b.index)].type;
		case ItemType::Object: return d.objects[std::size_t(a.index)].kind == d.objects[std::size_t(b.index)].kind;
		case ItemType::Prop: return d.props[std::size_t(a.index)].asset == d.props[std::size_t(b.index)].asset;
		case ItemType::Gen: {
			const nectar::DocGen& x = d.gens[std::size_t(a.index)], &y = d.gens[std::size_t(b.index)];
			return x.file == y.file && x.label == y.label && x.model == y.model;
		}
		case ItemType::Wall: return d.walls[std::size_t(a.index)].isArc == d.walls[std::size_t(b.index)].isArc;
		default: return true;
		}
	};
	hint(tr("Clic para elegir; doble clic para ir hasta ello. Ctrl+clic elige varios."));
	hint(tr("V oculta y B bloquea mientras editas (no cambia el mapa)."));
	int totalMatches = 0;
	for (const Group& group : groups) {
		int count = 0;
		for (const ItemRef& r : items) {
			if (groupOf(r) == group.id && (query.empty() || searchKey(itemName(d, r)).find(query) != std::string::npos)) count++;
		}
		// Sin nada de la misión o de los jugadores del VS (mapas de un solo
		// tipo), ni se enseña el grupo.
		if (!count && (!query.empty() || group.id == 8 || group.id <= 1)) continue;
		totalMatches += count;
		char heading[128];
		std::snprintf(heading, sizeof(heading), "%s (%d)##hierarchy_group_%d", group.name, count, group.id);
		const ImGuiTreeNodeFlags flags = (group.id <= 2 || group.id == 8 || !query.empty()) ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
		if (!ImGui::TreeNodeEx(heading, flags)) continue;
		int row = 0;
		for (const ItemRef& r : items) {
			if (groupOf(r) != group.id) continue;
			const std::string label = itemName(d, r);
			if (!query.empty() && searchKey(label).find(query) == std::string::npos) continue;
			ImGui::PushID(group.id * 100000 + row++);
			const bool selected = ed_.selected(r);
			const bool hidden = itemHidden(r), locked = itemLocked(r);
			const bool canHide = r.type != ItemType::Wall && r.type != ItemType::Pond;
			ImGui::BeginDisabled(hidden || locked);
			const float labelW = std::max(40.0f, ImGui::GetContentRegionAvail().x - 58.0f);
			if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(labelW, 0))) {
				ed_.select(r, ImGui::GetIO().KeyCtrl);
				tool_ = Tool::Move;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ed_.selected(r)) focusSelection();
			}
			ImGui::EndDisabled();
			if (ImGui::BeginPopupContextItem("##hierarchy_item_menu")) {
				if (ImGui::MenuItem(tr("Seleccionar todos los del mismo tipo"))) {
					ed_.clearSelection();
					for (const ItemRef& candidate : items) {
						if (sameKind(candidate, r) && !itemHidden(candidate) && !itemLocked(candidate)) ed_.selection.push_back(candidate);
					}
					tool_ = Tool::Move;
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!canHide);
			if (choiceButton("V##visibility", !hidden, ImVec2(22, 0))) toggleHidden(r);
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s", hidden ? tr("Mostrar en el visor") : tr("Ocultar temporalmente"));
			ImGui::SameLine();
			if (choiceButton("B##lock", locked, ImVec2(22, 0))) toggleLocked(r);
			ImGui::SetItemTooltip("%s", locked ? tr("Desbloquear selección") : tr("Bloquear selección"));
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	if (!query.empty() && totalMatches == 0) hint(tr("No hay elementos que coincidan con la búsqueda."));
}

void App::drawChecklist()
{
	ImGui::SeparatorText(tr("¿Está listo para jugar?"));
	const std::vector<Issue> issues = this->issues();
	bool blocking = false;
	for (const Issue& i : issues) blocking = blocking || i.blocking;
	if (preview_.busy() && compiled_.generation != ed_.liveGeneration) ImGui::TextColored(kDim, "%s", tr("Comprobando..."));
	else if (!blocking) ImGui::TextColored(kGood, "%s", tr("¡Sí! Pulsa «Probar en el juego»."));
	else ImGui::TextColored(kBad, "%s", tr("Todavía no. Arregla esto:"));
	int n = 0;
	for (const Issue& i : issues) {
		ImGui::PushID(n++);
		ImGui::PushStyleColor(ImGuiCol_Text, i.blocking ? kBad : kWarn);
		ImGui::Bullet();
		ImGui::TextWrapped("%s", i.text.c_str());
		ImGui::PopStyleColor();
		if (i.hasItem && ImGui::SmallButton(tr("Llévame allí"))) {
			ed_.selection = { i.item };
			tool_         = Tool::Move;
			focusSelection();
		}
		if (i.fix != IssueFix::None) {
			if (i.hasItem) ImGui::SameLine();
			const char* fixLabel = i.fix == IssueFix::ClampInside ? tr("Mover dentro") : i.fix == IssueFix::NearestGround ? tr("Buscar suelo cercano")
			                     : i.fix == IssueFix::DeleteItem ? tr("Borrar elemento") : tr("Normalizar escala");
			if (ImGui::SmallButton(fixLabel)) applyIssueFix(i);
		}
		if (i.fix == IssueFix::NearestGround) {
			ImGui::SameLine();
			if (ImGui::SmallButton(tr("Crear suelo debajo"))) createGroundUnder(i.item);
		}
		ImGui::PopID();
	}
	// Presupuesto: lo que lleva el mapa frente a lo que aguanta el juego.
	const nectar::MapDoc& doc = ed_.doc;
	const int twin            = doc.symmetric ? 2 : 1;
	int tekis = int(doc.guards.size()) * twin, bosses = 0;
	for (const nectar::DocEnemy& e : doc.enemies) {
		bool boss = false;
		for (const nectar::EnemyInfo& info : nectar::enemyTypes()) {
			if (e.type == info.id) boss = info.boss >= 0;
		}
		(boss ? bosses : tekis) += twin;
	}
	for (const nectar::DocGen& g : doc.gens) {
		nectar::GenSettings s;
		if (!nectar::readGenSettings(g, s)) continue;
		const int n = s.maxCount() * (g.mirror && doc.symmetric ? 2 : 1);
		if (s.object == "teki") tekis += n;
		if (s.object == "boss") bosses += n;
	}
	ImGui::Spacing();
	ImGui::TextColored(kDim, "%s", tr("Presupuesto del juego:"));
	auto bar = [&](const char* label, double used, double max) {
		const float use = max > 0 ? float(used / max) : 0.0f;
		char text[96];
		snprintf(text, sizeof(text), "%s: %.0f / %.0f", label, used, max);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, use < 0.75f ? kGood : use <= 1.0f ? kWarn : kBad);
		ImGui::ProgressBar(std::min(use, 1.0f), ImVec2(-1, 0), text);
		ImGui::PopStyleColor();
	};
	if (compiled_.ok) bar(tr("Vértices"), double(compiled_.vertices), double(nectar::kMapMaxVertices));
	bar(tr("Enemigos"), tekis, nectar::kMapMaxTekis);
	bar(tr("Jefes"), bosses, nectar::kMapMaxBosses);
	bar(tr("Construcciones"), double(doc.objects.size()) * twin, nectar::kMapMaxObjects);
	bar(tr("Pastillas"), double(doc.pellets.size()) * twin, nectar::kMapMaxPellets);
	if (doc.modes & nectar::MAPMODE_VS) bar(tr("Piezas"), double(doc.pieces.size()) * twin, nectar::kMapMaxPieces);
	bar(tr("Guardianes"), double(doc.guards.size()) * twin, nectar::kMapMaxGuards);
}

void App::drawTechnicalReport()
{
	ImGui::SeparatorText(tr("Resultado compilado"));
	if (preview_.busy() && compiled_.generation != ed_.liveGeneration) {
		hint(tr("Compilando los últimos cambios..."));
		return;
	}
	if (compiled_.generation != ed_.liveGeneration) {
		hint(tr("Todavía no hay un informe actualizado."));
		return;
	}
	const ImVec4 state = compiled_.playable ? kGood : compiled_.ok ? kWarn : kBad;
	ImGui::TextColored(state, "%s", compiled_.playable ? tr("Jugable") : compiled_.ok ? tr("Compila con avisos") : tr("No compila"));
	if (ImGui::BeginTable("##technical_report", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
		auto row = [](const char* label, const std::string& value) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		};
		row(tr("Vértices"), std::to_string(compiled_.vertices));
		row(tr("Triángulos de colisión"), std::to_string(compiled_.collisionTris));
		row(tr("Puntos de ruta"), std::to_string(compiled_.routePoints));
		const std::map<int, int> components = routeComponents(compiled_.routes);
		std::set<int> componentIds;
		for (const auto& entry : components) componentIds.insert(entry.second);
		row(tr("Grupos de rutas"), std::to_string(componentIds.size()));
		row(tr("Decoración compilada"), std::to_string(compiled_.props));
		row(tr("Tamaño compilado"), std::to_string((compiled_.bytes + 1023) / 1024) + " KiB");
		row(tr("Piezas"), std::to_string(ed_.doc.pieces.size()));
		row(tr("Pastillas"), std::to_string(ed_.doc.pellets.size()));
		row(tr("Enemigos"), std::to_string(ed_.doc.enemies.size() + ed_.doc.guards.size()));
		row(tr("Construcciones"), std::to_string(ed_.doc.objects.size()));
		std::set<std::string> uniqueAssets;
		for (const nectar::DocProp& p : ed_.doc.props) uniqueAssets.insert(p.asset);
		for (const nectar::DocGen& g : ed_.doc.gens) uniqueAssets.insert(g.file.empty() ? g.model : g.file);
		row(tr("Assets únicos"), std::to_string(uniqueAssets.size()));
		ImGui::EndTable();
	}
	if (!compiled_.error.empty()) {
		ImGui::Spacing();
		ImGui::SeparatorText(tr("Detalle técnico"));
		ImGui::TextWrapped("%s", compiled_.error.c_str());
		if (ImGui::Button(tr("Copiar detalle"), ImVec2(-1, 30))) {
			ImGui::SetClipboardText(compiled_.error.c_str());
			notify(0, tr("Detalle copiado al portapapeles."));
		}
	}
}

void App::drawModals()
{
	drawMaterialBrowser();
	drawHelp();
	if (openSaveAs_) {
		ImGui::OpenPopup(tr("Guardar mapa"));
		openSaveAs_ = false;
	}
	if (openOpen_) {
		ImGui::OpenPopup(tr("Abrir mapa"));
		openOpen_ = false;
	}
	if (openNew_) {
		ImGui::OpenPopup(tr("Mapa nuevo"));
		openNew_ = false;
	}
	if (confirmQuit_) {
		ImGui::OpenPopup(tr("Salir"));
		confirmQuit_ = false;
	}

	if (ImGui::BeginPopupModal(tr("Guardar mapa"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("%s", tr("¿Cómo se llama tu mapa?"));
		ImGui::SetNextItemWidth(360);
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("##name", saveName_, sizeof(saveName_), ImGuiInputTextFlags_EnterReturnsTrue);
		hint(tr("Aparecerá con ese nombre en el juego: modo VS > Mapa."));
		if (ImGui::Button(tr("Guardar"), ImVec2(150, 34)) || enter) {
			ed_.doc.name = saveName_[0] ? saveName_ : tr("Mi mapa");
			// Carpeta a partir del nombre, sin pisar otro mapa.
			std::string id = slug(ed_.doc.name), path;
			std::error_code ec;
			for (int n = 1;; n++) {
				const std::string folder = n == 1 ? id : id + "_" + std::to_string(n);
				path                     = (fs::path(cfg_.userMapsDir()) / folder / "map.json").string();
				if (!fs::exists(path, ec)) break;
			}
			ImGui::CloseCurrentPopup();
			if (saveMap(path) && testAfterSave_) testInGame();
		}
		ImGui::SameLine();
		if (ImGui::Button(tr("Cancelar"), ImVec2(150, 34))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(tr("Abrir mapa"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("%s", tr("Tus mapas"));
		ImGui::BeginChild("list", ImVec2(560, 340), ImGuiChildFlags_Borders);
		for (const auto& m : openList_) {
			ImGui::PushID(m.second.c_str());
			const unsigned t = thumb((fs::path(m.second).parent_path() / "thumb.png").string());
			if (t) {
				ImGui::Image(ImTextureID(std::intptr_t(t)), ImVec2(64, 64));
				ImGui::SameLine();
			}
			if (ImGui::Selectable(m.first.c_str(), false, 0, ImVec2(0, 64))) {
				if (ed_.dirty) autosave();
				openMap(m.second);
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopID();
		}
		if (openList_.empty()) hint(tr("Todavía no has guardado ninguno."));
		ImGui::EndChild();
		if (ImGui::Button(tr("Abrir otro archivo..."))) {
			const fs::path p = pikmin::launcher::platform::askForFile(tr("Abrir un mapa"), tr("Mapa"), "*.json");
			if (!p.empty()) {
				openMap(p.string());
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(tr("Cancelar"))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(tr("Mapa nuevo"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ed_.dirty) ImGui::TextColored(kWarn, "%s", tr("Ojo: el mapa actual tiene cambios sin guardar."));
		ImGui::Text("%s", tr("Elige la forma para empezar:"));
		struct S {
			const char* id;
			nectar::TerrainShape shape;
			const char* label;
		};
		const S shapes[] = { { "r", nectar::TerrainShape::Rectangle, tr("Rectángulo") }, { "o", nectar::TerrainShape::Oval, tr("Óvalo") },
			                 { "c", nectar::TerrainShape::Cross, tr("Cruz") },          { "i", nectar::TerrainShape::TwoIslands, tr("Dos islas") },
			                 { "a", nectar::TerrainShape::Ring, tr("Anillo") } };
		for (int i = 0; i < 5; i++) {
			if (i) ImGui::SameLine();
			if (shapeButton(shapes[i].id, shapes[i].shape, shapes[i].label, 104)) {
				newMap(shapes[i].shape);
				ImGui::CloseCurrentPopup();
			}
		}
		if (templatePickers(96)) ImGui::CloseCurrentPopup();
		if (coursePickers()) ImGui::CloseCurrentPopup();
		if (ImGui::Button(tr("Copiar la arena del juego"))) {
			openBuiltIn();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(tr("Cancelar"))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal(tr("Salir"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("%s", tr("Tienes cambios sin guardar."));
		if (ImGui::Button(tr("Guardar y salir"))) {
			ImGui::CloseCurrentPopup();
			if (!ed_.path.empty() && saveMap(ed_.path)) quit_ = true;
			else saveOrAsk(false);
		}
		ImGui::SameLine();
		if (ImGui::Button(tr("Salir sin guardar"))) quit_ = true;
		ImGui::SameLine();
		if (ImGui::Button(tr("Cancelar"))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void App::drawHelp()
{
	if (openHelp_) {
		ImGui::OpenPopup("###help");
		openHelp_ = false;
	}
	ImGui::SetNextWindowSize(ImVec2(620, 0));
	if (!ImGui::BeginPopupModal((std::string(tr("Ayuda")) + "###help").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	languagePicker(cfg_);
	ImGui::SeparatorText(tr("Cómo se hace un mapa"));
	ImGui::TextWrapped("%s", tr("1. Da forma al suelo con «Dibujar suelo» y súbelo o bájalo con «Montañas y hoyos»."));
	ImGui::TextWrapped("%s", tr("2. Con «Poner cosas» añade piezas de la nave, pastillas, Bulborbs y decoración."));
	ImGui::TextWrapped("%s", tr("3. Coloca las salidas, cebollas y cohetes de cada jugador con «Mover y girar»."));
	ImGui::TextWrapped("%s", tr("4. Mira a la derecha si está listo y pulsa «Probar en el juego»."));
	ImGui::TextWrapped("%s", tr("5. Usa Vista para revisar colisión, rutas y pendientes antes de jugar."));
	ImGui::TextWrapped("%s", tr("6. Las comprobaciones pueden mover elementos, crear suelo o corregir errores seguros."));
	ImGui::TextWrapped("%s", tr("7. Rampas une dos alturas: marca primero el extremo bajo y después el alto."));
	ImGui::SeparatorText(tr("La vista"));
	ImGui::TextWrapped("%s%s", tr("Botón derecho + arrastrar: girar la vista. Rueda: acercar y alejar. Botón central: desplazar. "),
	                   tr("Botón derecho + W A S D: volar."));
	ImGui::SeparatorText(tr("Teclas"));
	ImGui::TextWrapped("%s%s", tr("1-9: herramientas · Ctrl+Z: deshacer · Ctrl+Y: rehacer · Ctrl+S: guardar · F5: probar · "),
	                   tr("W: mover · E: girar · R: escalar · Q: giro por pasos · Supr: borrar · Ctrl+D: duplicar · F: ver de cerca · T: vista desde arriba"));
	ImGui::TextWrapped("%s", tr("Inicio: ver todo el mapa · C: catálogo grande · Mayús+A: menú rápido con lo último puesto · [ y ]: tamaño del pincel · Mayús+F5: probar desde donde mira la cámara · F1: esta ayuda"));
	ImGui::Spacing();
	if (ImGui::Button(tr("Ver el tutorial otra vez"))) {
		tutorialStep_ = 0;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button(tr("Cambiar carpetas del juego"))) {
		copyStr(assetsBuf_, sizeof(assetsBuf_), cfg_.assetsDir);
		copyStr(gameBuf_, sizeof(gameBuf_), cfg_.gameExe);
		screen_ = Screen::Setup;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	std::error_code ec;
	const std::string autosavePath = (fs::path(Config::directory()) / "autosave.json").string();
	if (fs::exists(autosavePath, ec) && ImGui::Button(tr("Recuperar copia automática"))) {
		openMap(autosavePath);
		ed_.path  = "";
		ed_.dirty = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::Spacing();
	if (ImGui::Button(tr("Cerrar"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void App::drawTutorial()
{
	if (tutorialStep_ < 0) return;
	struct Step {
		const Rect* target;
		const char* title;
		const char* text;
	};
	const Step steps[] = {
		{ &viewRect_, tr("Este es tu mapa"), tr("Mantén el botón derecho del ratón y arrastra para girar la vista. Usa la rueda para acercarte o alejarte.") },
		{ &toolboxRect_, tr("Las herramientas"), tr("Aquí eliges qué hacer. Cada herramienta explica debajo cómo se usa.") },
		{ &floorButtonRect_, tr("Dibujar suelo"), tr("Con esta herramienta cambias la forma del mapa: añade suelo o haz huecos pintando con el ratón.") },
		{ &placeButtonRect_, tr("Poner cosas"), tr("Elige piezas de la nave, pastillas, enemigos o decoración y haz clic en el mapa para ponerlos.") },
		{ &checklistRect_, tr("¿Está listo?"), tr("Aquí ves si el mapa ya se puede jugar. Si algo falla, pulsa «Llévame allí» para ir directo.") },
		{ &testButtonRect_, tr("¡A jugar!"), tr("Cuando quieras, pulsa aquí: el mapa se guarda y se abre en el juego al momento.") },
	};
	const int count = int(sizeof(steps) / sizeof(steps[0]));
	if (tutorialStep_ >= count) {
		tutorialStep_     = -1;
		cfg_.tutorialDone = true;
		cfg_.save();
		return;
	}
	const Step& s = steps[tutorialStep_];
	ImDrawList* fg = ImGui::GetForegroundDrawList();
	const float t  = float(std::fmod(now(), 1.0));
	fg->AddRect(ImVec2(s.target->x - 4, s.target->y - 4), ImVec2(s.target->x + s.target->w + 4, s.target->y + s.target->h + 4),
	            IM_COL32(255, 200, 40, int(170 + 85 * std::sin(t * 6.2831853f))), 8, 0, 4);
	// El globo, junto a lo señalado y dentro de la pantalla.
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImVec2 pos(s.target->x + s.target->w + 16, s.target->y + 10);
	if (pos.x + 380 > vp->WorkPos.x + vp->WorkSize.x) pos.x = s.target->x - 396;
	if (s.target == &viewRect_) pos = ImVec2(s.target->x + s.target->w * 0.5f - 190, s.target->y + 80);
	pos.y = std::min(pos.y, vp->WorkPos.y + vp->WorkSize.y - 200);
	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(ImVec2(380, 0));
	ImGui::SetNextWindowFocus();
	ImGui::Begin("##tutorial", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), tr("%s  (%d de %d)"), s.title, tutorialStep_ + 1, count);
	ImGui::TextWrapped("%s", s.text);
	ImGui::Spacing();
	if (tutorialStep_ > 0 && ImGui::Button(tr("Atrás"))) tutorialStep_--;
	if (tutorialStep_ > 0) ImGui::SameLine();
	if (ImGui::Button(tutorialStep_ + 1 < count ? tr("Siguiente") : tr("¡Empezar!"), ImVec2(120, 0))) tutorialStep_++;
	ImGui::SameLine();
	if (ImGui::Button(tr("Saltar"))) tutorialStep_ = count;
	ImGui::End();
}

void App::handleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantTextInput) return;
	const bool ctrl = io.KeyCtrl;
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveOrAsk(false);
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (io.KeyShift) ed_.redo();
		else ed_.undo();
	}
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ed_.redo();
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelection();
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
		ed_.label = tr("Pegar");
		paste();
	}
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
		ed_.label = tr("Duplicar");
		duplicateSelection();
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) testInGame(io.KeyShift);
	if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) openHelp_ = true;
	if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) viewWholeMap();
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		ed_.label = tr("Borrar");
		deleteSelection();
	}
	if (radialOpen_) return; // el menú rápido se lleva las teclas
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		ed_.clearSelection();
		drawingWall_ = false;
		drawingRamp_ = false;
	}
	// Letras sueltas: no mientras se vuela con el botón derecho (WASD).
	if (!ctrl && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
			tool_ = Tool::Move;
			transformMode_ = TransformMode::Move;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			tool_ = Tool::Move;
			transformMode_ = TransformMode::Rotate;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			tool_ = Tool::Move;
			transformMode_ = TransformMode::Scale;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
			ed_.label = tr("Girar");
			rotateSelection(io.KeyShift ? cfg_.snapAngle : -cfg_.snapAngle);
		}
		// Mayús+A: menú rápido con lo último puesto, donde está el ratón.
		if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
			radialOpen_ = true;
			radialX_    = io.MousePos.x;
			radialY_    = io.MousePos.y;
		}
		// C: catálogo grande de «Poner cosas».
		if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			if (tool_ != Tool::Place) {
				tool_        = Tool::Place;
				catalogOpen_ = true;
			} else {
				catalogOpen_ = !catalogOpen_;
			}
		}
		// [ y ]: tamaño del pincel (también Ctrl + rueda).
		const bool brushTool = tool_ == Tool::Floor || tool_ == Tool::Height || tool_ == Tool::Plateau || tool_ == Tool::Paint || tool_ == Tool::Water || tool_ == Tool::Scatter;
		if (brushTool && ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) brushRadius_ = std::max(25.0f, brushRadius_ / 1.15f);
		if (brushTool && ImGui::IsKeyPressed(ImGuiKey_RightBracket)) brushRadius_ = std::min(1000.0f, brushRadius_ * 1.15f);
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) liftSelection(io.KeyShift ? 100 : 20);
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) liftSelection(io.KeyShift ? -100 : -20);
		if (ImGui::IsKeyPressed(ImGuiKey_T, false)) cam_.topDown = !cam_.topDown;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) focusSelection();
		const Tool keys[] = { Tool::Move, Tool::Place, Tool::Floor, Tool::Height, Tool::Plateau, Tool::Paint, Tool::Water, Tool::Walls, Tool::Ramp, Tool::Scatter };
		for (int i = 0; i < 10; i++) {
			const ImGuiKey key = i == 9 ? ImGuiKey_0 : ImGuiKey(ImGuiKey_1 + i);
			if (!ImGui::IsKeyPressed(key, false)) continue;
			const bool terrain = keys[i] != Tool::Move && keys[i] != Tool::Place && keys[i] != Tool::Walls && keys[i] != Tool::Scatter;
			if (courseMode() && (keys[i] == Tool::Walls || keys[i] == Tool::Scatter))
				notify(1, tr("En los escenarios del juego todavía no se pueden poner muros ni decoración."));
			else if (!terrain || ensureTerrain()) {
				tool_ = keys[i];
				if (tool_ != Tool::Ramp) drawingRamp_ = false;
			}
		}
	}
}

// ── Vista 3D ────────────────────────────────────────────────────────────────

bool App::project(const V3& p, const ImVec2& origin, const ImVec2& size, ImVec2& out) const
{
	const M4 vp    = cam_.projection(viewAspect_) * cam_.view();
	const float cx = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
	const float cy = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
	const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
	if (cw <= 0) return false;
	out = ImVec2(origin.x + (cx / cw * 0.5f + 0.5f) * size.x, origin.y + (1 - (cy / cw * 0.5f + 0.5f)) * size.y);
	return true;
}

bool App::selectionPivot(V3& pivot) const
{
	if (ed_.selection.empty()) return false;
	pivot = V3();
	int count = 0;
	for (const ItemRef& r : ed_.selection) {
		if (!itemExists(ed_.doc, r)) continue;
		const Xform x = getXform(ed_.doc, r);
		pivot.x += float(x.x);
		pivot.z += float(x.z);
		count++;
	}
	if (!count) return false;
	pivot.x /= float(count);
	pivot.z /= float(count);
	pivot.y = docHeight(pivot.x, pivot.z) + 55.0f;
	return true;
}

bool App::selectionCanRotate() const
{
	if (ed_.selection.size() > 1) return true;
	for (const ItemRef& r : ed_.selection) {
		if (itemExists(ed_.doc, r) && itemCaps(r.type).rotateY) return true;
	}
	return false;
}

bool App::selectionCanScale() const
{
	if (ed_.selection.empty()) return false;
	for (const ItemRef& r : ed_.selection) {
		if (!itemExists(ed_.doc, r) || !itemCaps(r.type).scale) return false;
	}
	return true;
}

V3 App::moveAxis(int axis) const
{
	if (!cfg_.localGizmo || ed_.selection.empty()) return axis == 1 ? V3(1, 0, 0) : V3(0, 0, 1);
	const ItemRef& primary = ed_.selection.back();
	if (!itemExists(ed_.doc, primary)) return axis == 1 ? V3(1, 0, 0) : V3(0, 0, 1);
	const float a = float(getXform(ed_.doc, primary).ry * 3.14159265358979323846 / 180.0);
	return axis == 1 ? V3(std::cos(a), 0, -std::sin(a)) : V3(std::sin(a), 0, std::cos(a));
}

int App::moveGizmoAxis(const ImVec2& origin, const ImVec2& size, const ImVec2& mouse) const
{
	V3 pivot;
	if (tool_ != Tool::Move || !selectionPivot(pivot)) return 0;
	ImVec2 c, x, z;
	if (!project(pivot, origin, size, c)) return 0;
	if (transformMode_ == TransformMode::Rotate) {
		if (!selectionCanRotate()) return 0;
		const float radius = 58.0f;
		return std::fabs(std::hypot(mouse.x - c.x, mouse.y - c.y) - radius) <= 10.0f ? 3 : 0;
	}
	if (transformMode_ == TransformMode::Scale) {
		if (!selectionCanScale()) return 0;
		const ImVec2 end(c.x + 52.0f, c.y - 52.0f), start(c.x + 13.0f, c.y - 13.0f);
		return pointSegmentDistance(mouse, start, end) <= 10.0f ? 4 : 0;
	}
	const float length = std::max(90.0f, (cam_.topDown ? cam_.topZoom : cam_.distance) * 0.16f);
	const bool hasX = project(pivot + moveAxis(1) * length, origin, size, x);
	const bool hasZ = project(pivot + moveAxis(2) * length, origin, size, z);
	auto distance = [&](const ImVec2& end, bool visible) {
		if (!visible) return 1e9f;
		const float vx = end.x - c.x, vy = end.y - c.y;
		const float len = std::hypot(vx, vy);
		if (len < 28.0f) return 1e9f;
		const ImVec2 start(c.x + vx / len * 16.0f, c.y + vy / len * 16.0f);
		return pointSegmentDistance(mouse, start, end);
	};
	const float dx = distance(x, hasX), dz = distance(z, hasZ);
	if (std::min(dx, dz) > 9.0f) return 0;
	return dx <= dz ? 1 : 2;
}

void App::drawMoveGizmo(const ImVec2& origin, const ImVec2& size)
{
	V3 pivot;
	if (tool_ != Tool::Move || boxSelecting_ || !selectionPivot(pivot)) return;
	const float length = std::max(90.0f, (cam_.topDown ? cam_.topZoom : cam_.distance) * 0.16f);
	ImVec2 c, ends[2];
	if (!project(pivot, origin, size, c)) return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const int hover = moveGizmoAxis(origin, size, ImGui::GetIO().MousePos);
	if (transformMode_ == TransformMode::Rotate) {
		if (!selectionCanRotate()) return;
		const bool hot = dragAxis_ == 3 || hover == 3;
		const ImU32 gold = hot ? IM_COL32(255, 226, 91, 255) : IM_COL32(220, 188, 69, 255);
		dl->AddCircle(c, 58.0f, IM_COL32(16, 22, 14, 225), 64, hot ? 8.0f : 7.0f);
		dl->AddCircle(c, 58.0f, gold, 64, hot ? 5.0f : 3.5f);
		const float a = -0.62f;
		const ImVec2 tip(c.x + std::cos(a) * 58.0f, c.y + std::sin(a) * 58.0f);
		const ImVec2 tangent(-std::sin(a), std::cos(a));
		const ImVec2 radial(std::cos(a), std::sin(a));
		dl->AddTriangleFilled(ImVec2(tip.x + tangent.x * 8.0f, tip.y + tangent.y * 8.0f), ImVec2(tip.x - tangent.x * 8.0f + radial.x * 7.0f, tip.y - tangent.y * 8.0f + radial.y * 7.0f),
		                      ImVec2(tip.x - tangent.x * 8.0f - radial.x * 7.0f, tip.y - tangent.y * 8.0f - radial.y * 7.0f), gold);
		dl->AddCircleFilled(c, 6.0f, IM_COL32(224, 216, 130, 255));
		dl->AddCircle(c, 7.0f, IM_COL32(20, 30, 16, 240), 16, 2.0f);
		return;
	}
	if (transformMode_ == TransformMode::Scale) {
		if (!selectionCanScale()) return;
		const bool hot = dragAxis_ == 4 || hover == 4;
		const ImU32 violet = hot ? IM_COL32(232, 139, 255, 255) : IM_COL32(187, 104, 211, 255);
		const ImVec2 end(c.x + 52.0f, c.y - 52.0f);
		dl->AddLine(c, end, IM_COL32(16, 22, 14, 225), hot ? 8.0f : 7.0f);
		dl->AddLine(c, end, violet, hot ? 5.0f : 3.5f);
		dl->AddRectFilled(ImVec2(end.x - 7.0f, end.y - 7.0f), ImVec2(end.x + 7.0f, end.y + 7.0f), violet, 2.0f);
		dl->AddRect(ImVec2(end.x - 8.0f, end.y - 8.0f), ImVec2(end.x + 8.0f, end.y + 8.0f), IM_COL32(16, 22, 14, 240), 2.0f, 0, 2.0f);
		dl->AddCircleFilled(c, 6.0f, IM_COL32(224, 216, 130, 255));
		dl->AddCircle(c, 7.0f, IM_COL32(20, 30, 16, 240), 16, 2.0f);
		return;
	}
	const bool visible[2] = { project(pivot + moveAxis(1) * length, origin, size, ends[0]), project(pivot + moveAxis(2) * length, origin, size, ends[1]) };
	const ImU32 colours[2] = { IM_COL32(231, 80, 67, 255), IM_COL32(74, 154, 236, 255) };
	const char* labels[2] = { "X", "Z" };
	for (int axis = 0; axis < 2; axis++) {
		if (!visible[axis]) continue;
		const ImVec2 d(ends[axis].x - c.x, ends[axis].y - c.y);
		const float len = std::max(0.001f, std::hypot(d.x, d.y));
		const ImVec2 u(d.x / len, d.y / len), n(-u.y, u.x);
		const bool hot = dragAxis_ == axis + 1 || hover == axis + 1;
		const float thickness = hot ? 6.0f : 4.0f;
		dl->AddLine(c, ends[axis], IM_COL32(16, 22, 14, 225), thickness + 3.0f);
		dl->AddLine(c, ends[axis], colours[axis], thickness);
		const ImVec2 base(ends[axis].x - u.x * 13.0f, ends[axis].y - u.y * 13.0f);
		dl->AddTriangleFilled(ends[axis], ImVec2(base.x + n.x * 7.0f, base.y + n.y * 7.0f), ImVec2(base.x - n.x * 7.0f, base.y - n.y * 7.0f), colours[axis]);
		dl->AddText(ImVec2(ends[axis].x + u.x * 6.0f - 4.0f, ends[axis].y + u.y * 6.0f - 8.0f), colours[axis], labels[axis]);
	}
	dl->AddCircleFilled(c, 6.0f, IM_COL32(224, 216, 130, 255));
	dl->AddCircle(c, 7.0f, IM_COL32(20, 30, 16, 240), 16, 2.0f);
}

// ── Interfaz: barra de estado, avisos, brújula, recientes y catálogo ───────

const char* App::toolName(Tool t) const
{
	switch (t) {
	case Tool::Move: return tr("Mover y girar");
	case Tool::Place: return tr("Poner cosas");
	case Tool::Floor: return tr("Dibujar suelo");
	case Tool::Height: return tr("Montañas y hoyos");
	case Tool::Plateau: return tr("Elevar o hundir zona");
	case Tool::Paint: return tr("Pintar el suelo");
	case Tool::Water: return tr("Agua");
	case Tool::Walls: return tr("Muros");
	case Tool::Ramp: return tr("Rampas");
	case Tool::Scatter: return tr("Decorar");
	case Tool::Trace: return tr("Trazos");
	}
	return "";
}

const std::vector<App::Issue>& App::issues()
{
	// checkMap() mira el mapa y su compilación: se rehace cuando cambia alguno
	// (y cada poco, por si el catálogo termina de cargar modelos).
	static double at = -10.0;
	if (issuesGen_ != ed_.liveGeneration || issuesCompiledGen_ != compiled_.generation || now() - at > 1.0) {
		issuesGen_         = ed_.liveGeneration;
		issuesCompiledGen_ = compiled_.generation;
		at                 = now();
		issuesCache_       = checkMap();
	}
	return issuesCache_;
}

void App::viewWholeMap()
{
	const MapDoc& d = ed_.doc;
	cam_.target     = V3(float((d.minX + d.maxX) * 0.5), 0, float((d.minZ + d.maxZ) * 0.5));
	cam_.distance   = float(std::max(d.maxX - d.minX, d.maxZ - d.minZ));
	cam_.topZoom    = float(std::max((d.maxX - d.minX) / viewAspect_, d.maxZ - d.minZ)) * 0.55f;
}

void App::rememberPlace(const PlaceChoice& c)
{
	const std::string key = std::to_string(int(c.type)) + "|" + c.asset + "|" + c.label;
	auto& list            = cfg_.placeRecents;
	list.erase(std::remove(list.begin(), list.end(), key), list.end());
	list.insert(list.begin(), key);
	if (list.size() > 10) list.resize(10);
	cfg_.save();
}

std::vector<App::PlaceChoice> App::placeRecents() const
{
	std::vector<PlaceChoice> out;
	for (const std::string& key : cfg_.placeRecents) {
		const std::size_t a = key.find('|'), b = a == std::string::npos ? a : key.find('|', a + 1);
		if (b == std::string::npos) continue;
		PlaceChoice c;
		c.type  = ItemType(std::atoi(key.substr(0, a).c_str()));
		c.asset = key.substr(a + 1, b - a - 1);
		c.label = key.substr(b + 1);
		// Lo de un modo que el mapa no tiene, no.
		if (isVsItem(c.type) && !(ed_.doc.modes & nectar::MAPMODE_VS)) continue;
		if (isStoryItem(c.type) && !ed_.doc.story.present) continue;
		out.push_back(c);
	}
	return out;
}

void App::drawReadyPill()
{
	const std::vector<Issue>& list = issues();
	int blocking = 0;
	for (const Issue& i : list) blocking += i.blocking ? 1 : 0;
	const bool checking = preview_.busy() && compiled_.generation != ed_.liveGeneration;
	char text[96];
	ImVec4 col;
	if (checking) {
		std::snprintf(text, sizeof(text), "%s", tr("Comprobando..."));
		col = kDim;
	} else if (blocking) {
		std::snprintf(text, sizeof(text), blocking == 1 ? tr("1 cosa por arreglar") : tr("%d cosas por arreglar"), blocking);
		col = kWarn;
	} else {
		std::snprintf(text, sizeof(text), "%s", tr("Listo para jugar"));
		col = kGood;
	}
	const ImVec2 at = ImGui::GetCursorScreenPos();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.16f, 0.13f, 1));
	const bool pressed = ImGui::Button((std::string("    ") + text + "##ready").c_str(), ImVec2(ImGui::CalcTextSize(text).x + 44, 34));
	ImGui::PopStyleColor();
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 17, at.y + 17), 6.0f, ImGui::GetColorU32(col));
	ImGui::SetItemTooltip("%s", tr("¿Está listo para jugar? Pulsa para ver la lista."));
	if (pressed) ImGui::OpenPopup("##ready_list");
	ImGui::SetNextWindowSize(ImVec2(440, 0));
	if (ImGui::BeginPopup("##ready_list")) {
		drawChecklist();
		ImGui::EndPopup();
	}
}

void App::drawHistoryMenu()
{
	ImGui::BeginDisabled(!ed_.canUndo());
	if (ImGui::Button("...##history", ImVec2(30, 34))) ImGui::OpenPopup("##history");
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s", tr("Historial: vuelve a cualquier paso anterior"));
	if (ImGui::BeginPopup("##history")) {
		ImGui::TextColored(kDim, "%s", tr("Haz clic en un paso para deshacerlo (y todo lo que vino después)."));
		ImGui::Separator();
		const int n = ed_.undoCount();
		for (int i = 0; i < n && i < 40; i++) {
			ImGui::PushID(i);
			const std::string& label = ed_.undoLabel(i);
			char row[160];
			std::snprintf(row, sizeof(row), "%d.  %s", n - i, label.empty() ? tr("Cambio") : label.c_str());
			if (ImGui::Selectable(row)) {
				for (int k = 0; k <= i; k++) ed_.undo();
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
}

void App::drawStatusBar(const Rect& r)
{
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), IM_COL32(24, 28, 25, 255));
	dl->AddLine(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y), IM_COL32(60, 72, 58, 255));
	const float ty      = r.y + (r.h - ImGui::GetFontSize()) * 0.5f;
	const ImU32 ink     = IM_COL32(214, 216, 200, 255), dim = IM_COL32(150, 158, 140, 255);

	// Derecha: coordenadas, selección, guardado y presupuesto (de derecha a izquierda).
	float x = r.x + r.w - 12;
	auto rightText = [&](const std::string& s, ImU32 c) {
		const float w = ImGui::CalcTextSize(s.c_str()).x;
		x -= w;
		dl->AddText(ImVec2(x, ty), c, s.c_str());
		x -= 22;
		dl->AddText(ImVec2(x + 8, ty), IM_COL32(80, 92, 76, 255), "|");
	};
	if (compiled_.ok) {
		const float use = float(double(compiled_.vertices) / double(nectar::kMapMaxVertices));
		const float bw = 90, bh = 8;
		x -= bw;
		const ImVec2 a(x, r.y + (r.h - bh) * 0.5f);
		dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + bh), IM_COL32(50, 58, 48, 255), 4.0f);
		dl->AddRectFilled(a, ImVec2(a.x + bw * std::min(1.0f, use), a.y + bh), use < 0.75f ? IM_COL32(146, 214, 94, 255) : use <= 1.0f ? IM_COL32(245, 194, 71, 255) : IM_COL32(245, 102, 76, 255), 4.0f);
		if (ImGui::IsMouseHoveringRect(ImVec2(a.x, r.y), ImVec2(a.x + bw, r.y + r.h)))
			ImGui::SetTooltip(tr("Tamaño del mapa para el juego: %zu de %zu vértices"), compiled_.vertices, nectar::kMapMaxVertices);
		x -= 10;
		rightText(tr("Tamaño"), dim);
	}
	{
		std::string saved;
		if (ed_.path.empty()) saved = tr("Sin guardar");
		else if (ed_.dirty) saved = tr("Cambios sin guardar");
		else if (lastSaved_ >= 0) {
			const int mins = int((now() - lastSaved_) / 60.0);
			char buf[64];
			if (mins < 1) std::snprintf(buf, sizeof(buf), "%s", tr("Guardado ahora mismo"));
			else std::snprintf(buf, sizeof(buf), tr("Guardado hace %d min"), mins);
			saved = buf;
		} else saved = tr("Guardado");
		rightText(saved, ed_.dirty || ed_.path.empty() ? IM_COL32(245, 194, 71, 255) : dim);
	}
	if (!ed_.selection.empty()) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), ed_.selection.size() == 1 ? tr("1 cosa elegida") : tr("%zu cosas elegidas"), ed_.selection.size());
		rightText(buf, ink);
	}
	if (hoverValid_) {
		char buf[96];
		std::snprintf(buf, sizeof(buf), "X %.0f   Z %.0f   %s %.0f", hoverPos_.x, hoverPos_.z, tr("Altura"), hoverPos_.y);
		rightText(buf, dim);
	}

	// Izquierda: qué hace el ratón con esta herramienta y con la vista.
	std::string left = std::string(toolName(tool_)) + ":  " + toolHelp() + "     ";
	left += cam_.topDown ? tr("Botón derecho o central: desplazar · Rueda: acercar") : tr("Botón derecho: girar · Central: desplazar · Rueda: acercar · Derecho + WASD: volar");
	dl->PushClipRect(ImVec2(r.x, r.y), ImVec2(std::max(r.x, x - 8), r.y + r.h), true);
	drawIcon(dl, ImVec2(r.x + 18, r.y + r.h * 0.5f), 7.0f, Icon::Move, IM_COL32(183, 207, 75, 255));
	dl->AddText(ImVec2(r.x + 34, ty), ink, left.c_str());
	dl->PopClipRect();
	ImGui::End();
}

void App::drawNavGizmo(const ImVec2& origin, const ImVec2& size, bool& hovered)
{
	// Brújula de vistas (como el gizmo de navegación de Blender): los ejes
	// tal como se ven ahora; un clic en uno mira desde ese lado y el del
	// centro cambia entre la vista desde arriba y la 3D.
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImGuiIO& io    = ImGui::GetIO();
	const float R  = 38.0f;
	const ImVec2 c(origin.x + size.x - R - 24, origin.y + R + 24);
	V3 right, up, fwd;
	if (cam_.topDown) {
		right = V3(1, 0, 0);
		up    = V3(0, 0, -1);
		fwd   = V3(0, -1, 0);
	} else {
		fwd   = (cam_.target - cam_.eye()).normalized();
		right = fwd.cross(V3(0, 1, 0)).normalized();
		up    = right.cross(fwd);
	}
	const bool over = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= R + 14;
	hovered |= over;
	dl->AddCircleFilled(c, R + 13, over ? IM_COL32(32, 40, 32, 215) : IM_COL32(24, 30, 24, 150));
	struct Axis {
		V3 dir;
		ImU32 col;
		const char* label;
		int view; // 0 desde arriba, 1 +X, 2 -X, 3 +Z, 4 -Z
		ImVec2 p;
		float depth;
	};
	Axis axes[] = { { V3(1, 0, 0), IM_COL32(231, 80, 67, 255), "X", 1, {}, 0 },  { V3(-1, 0, 0), IM_COL32(140, 66, 60, 235), "", 2, {}, 0 },
		            { V3(0, 0, 1), IM_COL32(74, 154, 236, 255), "Z", 3, {}, 0 }, { V3(0, 0, -1), IM_COL32(58, 96, 146, 235), "", 4, {}, 0 },
		            { V3(0, 1, 0), IM_COL32(126, 204, 92, 255), "Y", 0, {}, 0 } };
	for (Axis& a : axes) {
		a.p     = ImVec2(c.x + a.dir.dot(right) * R, c.y - a.dir.dot(up) * R);
		a.depth = a.dir.dot(fwd);
	}
	std::sort(std::begin(axes), std::end(axes), [](const Axis& a, const Axis& b) { return a.depth > b.depth; });
	int hot = -1;
	float best = 11.0f;
	for (int i = 0; i < 5; i++) {
		const float dd = std::hypot(io.MousePos.x - axes[i].p.x, io.MousePos.y - axes[i].p.y);
		if (dd < best) {
			best = dd;
			hot  = i;
		}
	}
	const bool centreHot = hot < 0 && std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) < 10.0f;
	for (int i = 0; i < 5; i++) {
		const Axis& a = axes[i];
		if (a.label[0]) dl->AddLine(c, a.p, a.col, 2.5f);
		dl->AddCircleFilled(a.p, a.label[0] ? 9.5f : 7.0f, a.col);
		if (i == hot) dl->AddCircle(a.p, 11.5f, IM_COL32(255, 255, 255, 230), 0, 2.0f);
		if (a.label[0]) dl->AddText(ImVec2(a.p.x - ImGui::CalcTextSize(a.label).x * 0.5f, a.p.y - ImGui::GetFontSize() * 0.5f), IM_COL32(20, 24, 20, 255), a.label);
	}
	dl->AddCircleFilled(c, 7.0f, centreHot ? IM_COL32(255, 255, 255, 240) : IM_COL32(210, 214, 196, 200));
	if (hot >= 0) {
		const char* tips[] = { tr("Ver desde arriba"), tr("Mirar desde el lado X"), tr("Mirar desde el lado −X"), tr("Mirar desde el lado Z"), tr("Mirar desde el lado −Z") };
		ImGui::SetTooltip("%s", tips[axes[hot].view]);
	} else if (centreHot) {
		ImGui::SetTooltip("%s", cam_.topDown ? tr("Volver a la vista 3D (T)") : tr("Ver desde arriba (T)"));
	}
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		if (hot >= 0) {
			const int v = axes[hot].view;
			if (v == 0) cam_.topDown = true;
			else {
				const float yaws[] = { 0.0f, 1.5708f, -1.5708f, 0.0f, 3.14159f };
				cam_.topDown = false;
				cam_.yaw     = yaws[v];
				cam_.pitch   = 0.5f;
			}
		} else if (centreHot) cam_.topDown = !cam_.topDown;
	}
	// Debajo: ver todo el mapa.
	const ImVec2 ba(c.x - 44, c.y + R + 22), bb(c.x + 44, c.y + R + 46);
	const bool overAll = ImGui::IsMouseHoveringRect(ba, bb);
	hovered |= overAll;
	dl->AddRectFilled(ba, bb, overAll ? IM_COL32(70, 88, 52, 235) : IM_COL32(32, 40, 32, 215), 6.0f);
	const char* all = tr("Ver todo");
	dl->AddText(ImVec2((ba.x + bb.x - ImGui::CalcTextSize(all).x) * 0.5f, ba.y + 4), IM_COL32(230, 232, 214, 255), all);
	if (overAll) ImGui::SetTooltip("%s", tr("Ver todo el mapa (Inicio)"));
	if (overAll && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) viewWholeMap();
}

void App::drawIssueMarkers(const ImVec2& origin, const ImVec2& size, bool& hovered)
{
	// Cada aviso de la lista, como un globo en su sitio del mapa.
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImGuiIO& io    = ImGui::GetIO();
	for (const Issue& i : issues()) {
		if (!i.hasItem || !itemExists(ed_.doc, i.item)) continue;
		const Xform x = getXform(ed_.doc, i.item);
		const float gy = docHeight(float(x.x), float(x.z));
		ImVec2 p, g;
		if (!project(V3(float(x.x), gy + 90.0f, float(x.z)), origin, size, p) || !project(V3(float(x.x), gy, float(x.z)), origin, size, g)) continue;
		if (p.x < origin.x || p.y < origin.y || p.x > origin.x + size.x || p.y > origin.y + size.y) continue;
		const ImU32 col = i.blocking ? IM_COL32(222, 70, 55, 245) : IM_COL32(236, 172, 44, 245);
		dl->AddLine(p, g, col, 2.0f);
		dl->AddCircleFilled(p, 11.0f, col);
		dl->AddCircle(p, 11.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
		dl->AddText(ImVec2(p.x - ImGui::CalcTextSize("!").x * 0.5f, p.y - ImGui::GetFontSize() * 0.5f), IM_COL32(255, 255, 255, 255), "!");
		if (std::hypot(io.MousePos.x - p.x, io.MousePos.y - p.y) <= 13.0f) {
			hovered = true;
			ImGui::SetTooltip("%s\n\n%s", i.text.c_str(), tr("Clic: elegirlo"));
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				ed_.selection = { i.item };
				tool_         = Tool::Move;
			}
		}
	}
}

namespace {
/// Nombre para mostrar de lo que se pone (enemigos y construcciones por su nombre).
std::string placeLabel(ItemType type, const std::string& asset, const std::string& label)
{
	if (type == ItemType::Enemy) {
		for (const nectar::EnemyInfo& e : nectar::enemyTypes()) {
			if (label == e.id) return tr(e.label);
		}
	}
	if (type == ItemType::Object) {
		for (const nectar::ObjectInfo& o : nectar::objectKinds()) {
			if (label == o.id) return tr(o.label);
		}
	}
	if (type == ItemType::Prop && !asset.empty() && label.empty()) return fs::path(asset).stem().string();
	return label;
}
} // namespace

void App::drawRecentsBar(const ImVec2& origin, const ImVec2& size, bool& hovered)
{
	// «Poner cosas»: lo último puesto, a mano (como la barra de Mario Maker),
	// y el botón del catálogo grande.
	if (tool_ != Tool::Place) return;
	const std::vector<PlaceChoice> list = placeRecents();
	ImDrawList* dl   = ImGui::GetWindowDrawList();
	ImGuiIO& io      = ImGui::GetIO();
	const float tile = 50, gap = 6, catW = 116;
	const float emptyW = list.empty() ? ImGui::CalcTextSize(tr("Lo que pongas saldrá aquí")).x + 20 : 0.0f;
	const float w      = float(list.size()) * (tile + gap) + emptyW + catW + 20;
	const ImVec2 a(origin.x + size.x * 0.5f - w * 0.5f, origin.y + size.y - tile - 26);
	const ImVec2 b(a.x + w, a.y + tile + 14);
	const bool over = ImGui::IsMouseHoveringRect(a, b);
	hovered |= over;
	dl->AddRectFilled(a, b, IM_COL32(22, 28, 22, 225), 10.0f);
	dl->AddRect(a, b, IM_COL32(94, 117, 49, 200), 10.0f);
	float x = a.x + 10;
	for (std::size_t i = 0; i < list.size(); i++) {
		const PlaceChoice& c = list[i];
		const ImVec2 ta(x, a.y + 7), tb(x + tile, a.y + 7 + tile);
		const bool on  = place_.type == c.type && place_.label == c.label && place_.asset == c.asset;
		const bool hot = ImGui::IsMouseHoveringRect(ta, tb);
		dl->AddRectFilled(ta, tb, on ? IM_COL32(110, 140, 52, 255) : hot ? IM_COL32(58, 70, 50, 255) : IM_COL32(40, 48, 38, 255), 6.0f);
		const unsigned t = assetThumb(placeAsset(c));
		if (t) dl->AddImage(ImTextureID(std::intptr_t(t)), ImVec2(ta.x + 3, ta.y + 3), ImVec2(tb.x - 3, tb.y - 3));
		else {
			const std::string s = placeLabel(c.type, c.asset, c.label).substr(0, 5);
			dl->AddText(ImVec2(ta.x + 5, ta.y + tile * 0.5f - 8), IM_COL32(230, 230, 214, 255), s.c_str());
		}
		if (hot) {
			ImGui::SetTooltip("%s", placeLabel(c.type, c.asset, c.label).c_str());
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				place_            = c;
				drawerDragArmed_  = true;
				drawerDragChoice_ = c;
			}
		}
		x += tile + gap;
	}
	if (drawerDragArmed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drawerDragArmed_ = false;
	if (drawerDragArmed_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f)) {
		const PlaceChoice& c = drawerDragChoice_;
		beginPlaceDrag(c, assetThumb(placeAsset(c)), placeLabel(c.type, c.asset, c.label), ImGuiDragDropFlags_SourceExtern);
	}
	if (list.empty()) {
		dl->AddText(ImVec2(x, a.y + 7 + tile * 0.5f - ImGui::GetFontSize() * 0.5f), IM_COL32(150, 158, 140, 255), tr("Lo que pongas saldrá aquí"));
		x += emptyW;
	}
	const ImVec2 ca(x + 4, a.y + 7), cb(x + 4 + catW, a.y + 7 + tile);
	const bool catHot = ImGui::IsMouseHoveringRect(ca, cb);
	dl->AddRectFilled(ca, cb, catalogOpen_ ? IM_COL32(110, 140, 52, 255) : catHot ? IM_COL32(70, 88, 52, 255) : IM_COL32(48, 58, 40, 255), 6.0f);
	const char* cat = catalogOpen_ ? tr("Cerrar catálogo") : tr("Catálogo (C)");
	dl->AddText(ImVec2((ca.x + cb.x - ImGui::CalcTextSize(cat).x) * 0.5f, ca.y + tile * 0.5f - ImGui::GetFontSize() * 0.5f), IM_COL32(236, 236, 220, 255), cat);
	if (catHot) {
		ImGui::SetTooltip("%s", tr("Todo lo que se puede poner, a lo ancho bajo el mapa"));
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) catalogOpen_ = !catalogOpen_;
	}
	(void)io;
}

void App::drawRadialMenu()
{
	// Mayús+A: lo último puesto en un círculo bajo el ratón (como los menús
	// circulares de Blender). Un clic elige; fuera o Esc, se cierra.
	if (!radialOpen_) return;
	const std::vector<PlaceChoice> list = placeRecents();
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImGuiIO& io    = ImGui::GetIO();
	const ImVec2 c(radialX_, radialY_);
	const float R = 110.0f;
	dl->AddCircleFilled(c, R + 42, IM_COL32(16, 20, 16, 150));
	dl->AddCircle(c, 22.0f, IM_COL32(200, 210, 180, 160), 0, 2.0f);
	if (list.empty()) {
		const char* msg = tr("Pon algo con «Poner cosas» y saldrá aquí");
		dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(msg).x * 0.5f, c.y + 30), IM_COL32(230, 230, 214, 255), msg);
	}
	const int n = std::min<int>(8, int(list.size()));
	int hot = -1;
	const float md = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y);
	if (n > 0 && md > 26.0f) {
		float ang = std::atan2(io.MousePos.y - c.y, io.MousePos.x - c.x) + 1.5708f;
		if (ang < 0) ang += 6.28318f;
		hot = int(std::floor(ang / (6.28318f / float(n)) + 0.5f)) % n;
	}
	for (int i = 0; i < n; i++) {
		const float a = -1.5708f + 6.28318f * float(i) / float(n);
		const ImVec2 p(c.x + std::cos(a) * R, c.y + std::sin(a) * R);
		dl->AddCircleFilled(p, 36.0f, i == hot ? IM_COL32(110, 140, 52, 245) : IM_COL32(40, 48, 38, 240));
		const unsigned t = assetThumb(placeAsset(list[std::size_t(i)]));
		if (t) dl->AddImage(ImTextureID(std::intptr_t(t)), ImVec2(p.x - 24, p.y - 26), ImVec2(p.x + 24, p.y + 22));
		const std::string s = placeLabel(list[std::size_t(i)].type, list[std::size_t(i)].asset, list[std::size_t(i)].label);
		const float tw      = ImGui::CalcTextSize(s.c_str()).x;
		dl->AddText(ImVec2(p.x - tw * 0.5f, p.y + 38), IM_COL32(240, 240, 226, 255), s.c_str());
	}
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			place_ = list[std::size_t(hot)];
			tool_  = Tool::Place;
		}
		radialOpen_ = false;
	}
}

void App::drawCatalogDrawer(const Rect& r)
{
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::Begin("##catalog", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextColored(ImVec4(0.90f, 0.87f, 0.62f, 1), "%s", tr("Catálogo: elige algo y haz clic en el mapa"));
	ImGui::SameLine(ImGui::GetWindowWidth() - 130);
	if (ImGui::Button(tr("Cerrar (C)"), ImVec2(118, 0))) catalogOpen_ = false;
	ImGui::Separator();
	ImGui::BeginChild("##catalog_body");
	drawPlaceOptions();
	ImGui::EndChild();
	ImGui::End();
}

void App::drawGrid()
{
	// La cuadrícula de encaje alrededor del ratón (al mover, poner o dibujar muros).
	if (!cfg_.snap || !hoverValid_ || !(tool_ == Tool::Move || tool_ == Tool::Place || tool_ == Tool::Walls)) return;
	const float snapStep = std::max(1.0f, cfg_.snapStep);
	const float step     = snapStep * std::max(1.0f, std::ceil(50.0f / snapStep));
	const int cells      = 8;
	const float cx = std::round(hoverPos_.x / step) * step, cz = std::round(hoverPos_.z / step) * step, half = step * float(cells);
	std::vector<V3> lines;
	lines.reserve(std::size_t(cells * 2 + 1) * 2 * std::size_t(cells * 4) * 2);
	for (int k = -cells; k <= cells; k++) {
		for (int axis = 0; axis < 2; axis++) {
			for (float t = -half; t < half - 0.01f; t += step * 0.5f) {
				const float x0 = axis ? cx + float(k) * step : cx + t, z0 = axis ? cz + t : cz + float(k) * step;
				const float x1 = axis ? x0 : x0 + step * 0.5f, z1 = axis ? z0 + step * 0.5f : z0;
				lines.push_back(V3(x0, docHeight(x0, z0) + 3, z0));
				lines.push_back(V3(x1, docHeight(x1, z1) + 3, z1));
			}
		}
	}
	renderer_.drawLines(lines, Rgba { 0.86f, 0.95f, 0.74f, 0.26f });
}

void App::drawViewport(const Rect& r)
{
	ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
	ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("##view", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar();
	viewRect_ = r;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail  = ImGui::GetContentRegionAvail();
	const int vw = std::max(1, int(avail.x)), vh = std::max(1, int(avail.y));
	viewAspect_ = float(vw) / float(vh);
	target_.resize(vw, vh);

	ImGuiIO& io        = ImGui::GetIO();
	const float nx     = (io.MousePos.x - origin.x) / avail.x * 2 - 1;
	const float ny     = 1 - (io.MousePos.y - origin.y) / avail.y * 2;
	const Ray mouseRay = cameraRay(cam_, viewAspect_, nx, ny);

	renderScene(vw, vh);
	ImGui::Image(ImTextureID(std::intptr_t(target_.color)), avail, ImVec2(0, 1), ImVec2(1, 0));
	// Mientras se arrastra algo del catálogo, el visor sigue «bajo el ratón»
	// para enseñar la vista previa de lo que se va a soltar.
	const bool imageHovered = ImGui::IsItemHovered() || (ImGui::GetDragDropPayload() != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
	hoverValid_             = imageHovered && terrainRay(mouseRay, hoverPos_);
	// Debe ir inmediatamente después de Image: Dear ImGui usa el último item
	// como destino. Los controles flotantes se dibujan después para no romperlo.
	if (ImGui::BeginDragDropTarget()) {
		V3 hit;
		if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("NM_ASSET")) {
			if (terrainRay(mouseRay, hit)) {
				place_ = { ItemType::Prop, static_cast<const char*>(p->Data), "" };
				cfg_.recents.erase(std::remove(cfg_.recents.begin(), cfg_.recents.end(), place_.asset), cfg_.recents.end());
				cfg_.recents.insert(cfg_.recents.begin(), place_.asset);
				if (cfg_.recents.size() > 16) cfg_.recents.resize(16);
				cfg_.save();
				placeAt(hit);
			}
		}
		if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("NM_PLACE")) {
			const PlacePayload* pl = static_cast<const PlacePayload*>(p->Data);
			place_ = { ItemType(pl->type), pl->asset, pl->label };
			tool_  = Tool::Place;
			if (terrainRay(mouseRay, hit)) {
				placeAt(hit);
				rememberPlace(place_);
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImDrawList* dl   = ImGui::GetWindowDrawList();
	// Marco fino y neutro; el acento queda reservado para selección y acciones.
	dl->AddRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(75, 86, 77, 230), 0.0f, 0, 1.0f);

	bool viewportControlHovered = false;
	drawIssueMarkers(origin, avail, viewportControlHovered);
	drawNavGizmo(origin, avail, viewportControlHovered);
	drawRecentsBar(origin, avail, viewportControlHovered);
	if (showMinimap_) {
		const MapDoc& d   = ed_.doc;
		const float spanX = float(d.maxX - d.minX), spanZ = float(d.maxZ - d.minZ);
		if (spanX > 0 && spanZ > 0) {
			float mw = std::max(150.0f, std::min(260.0f, avail.x * 0.22f));
			float mh = mw * spanZ / spanX;
			if (mh > avail.y * 0.38f) {
				mh = avail.y * 0.38f;
				mw = mh * spanX / spanZ;
			}
			drawMinimap(ImVec2(origin.x + avail.x - mw - 16, origin.y + avail.y - mh - 16), mw, mh, viewportControlHovered);
		}
	}
	if (radialOpen_) viewportControlHovered = true;
	viewportHovered_ = imageHovered && !viewportControlHovered;

	if (showLabels_) drawLabels(origin, avail);
	viewportMouse(mouseRay, origin, avail);
	if (tool_ == Tool::Ramp && rampPreviewValid_) {
		auto rampTag = [&](const V3& world, const char* label, ImU32 colour) {
			ImVec2 p;
			if (!project(world + V3(0, 28, 0), origin, avail, p)) return;
			const ImVec2 textSize = ImGui::CalcTextSize(label);
			const ImVec2 lo(p.x - textSize.x * 0.5f - 7, p.y - textSize.y - 7), hi(p.x + textSize.x * 0.5f + 7, p.y + 5);
			dl->AddRectFilled(lo, hi, IM_COL32(25, 35, 20, 230), 5.0f);
			dl->AddRect(lo, hi, colour, 5.0f, 0, 2.0f);
			dl->AddText(ImVec2(p.x - textSize.x * 0.5f, p.y - textSize.y - 2), IM_COL32(255, 250, 220, 255), label);
		};
		if (drawingRamp_) {
			rampTag(rampStart_, tr("INICIO"), IM_COL32(255, 205, 55, 255));
			rampTag(rampEnd_, tr("FINAL · CLIC PARA CREAR"), IM_COL32(100, 255, 100, 255));
		} else
			rampTag(rampEnd_, tr("CLIC: MARCAR INICIO"), IM_COL32(255, 205, 55, 255));
	}
	drawMoveGizmo(origin, avail);
	if (boxSelecting_) {
		const ImVec2 a(std::min(pressX_, boxEndX_), std::min(pressY_, boxEndY_));
		const ImVec2 b(std::max(pressX_, boxEndX_), std::max(pressY_, boxEndY_));
		dl->AddRectFilled(a, b, IM_COL32(171, 196, 76, 38));
		dl->AddRect(a, b, IM_COL32(215, 222, 98, 245), 3.0f, 0, 2.0f);
	}

	// (La ayuda de la herramienta y del ratón va en la barra de estado.)

	// Avisos (encima de la barra de recientes si está).
	float ty = origin.y + avail.y - (tool_ == Tool::Place ? 92.0f : 18.0f);
	for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
		if (it->until < now()) continue;
		const ImVec2 sz = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, avail.x * 0.6f);
		const float x0  = origin.x + avail.x * 0.5f - sz.x * 0.5f - 14;
		const ImU32 bg  = it->level == 2 ? IM_COL32(150, 45, 40, 235) : it->level == 1 ? IM_COL32(150, 110, 30, 235) : IM_COL32(35, 110, 55, 235);
		dl->AddRectFilled(ImVec2(x0, ty - sz.y - 14), ImVec2(x0 + sz.x + 28, ty), bg, 8);
		dl->AddText(nullptr, 0, ImVec2(x0 + 14, ty - sz.y - 7), IM_COL32(255, 255, 255, 255), it->text.c_str(), nullptr, avail.x * 0.6f);
		ty -= sz.y + 22;
	}
	toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.until < now(); }), toasts_.end());
	ImGui::End();
}

void App::viewportMouse(const Ray& ray, const ImVec2& origin, const ImVec2& size)
{
	ImGuiIO& io = ImGui::GetIO();
	const bool overButtons = false; // los controles flotantes ya quitan el ratón al visor (viewportHovered_)
	const bool hovered     = viewportHovered_ && !overButtons && tutorialStep_ < 0;
	const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left), leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool brushTool = tool_ == Tool::Floor || tool_ == Tool::Height || tool_ == Tool::Plateau || tool_ == Tool::Paint || tool_ == Tool::Water
	                    || tool_ == Tool::Scatter;
	rampPreviewValid_ = tool_ == Tool::Ramp && hovered && terrainRay(ray, rampEnd_);
	if (rampPreviewValid_ && leftClick) {
		if (!drawingRamp_) {
			rampStart_ = rampEnd_;
			drawingRamp_ = true;
			notify(0, tr("Inicio marcado. Ahora haz clic en una zona más alta."));
		} else {
			const float distance = std::hypot(rampEnd_.x - rampStart_.x, rampEnd_.z - rampStart_.z);
			const float height = std::fabs(rampEnd_.y - rampStart_.y);
			if (distance < std::max(25.0, ed_.doc.cell * 0.25))
				notify(1, tr("El final está demasiado cerca. Alarga la rampa y vuelve a hacer clic."));
			else if (height < 5.0f)
				notify(1, tr("Los dos extremos están a la misma altura. Elige una zona más alta."));
			else if (height / distance > 0.577f)
				notify(1, tr("La rampa es demasiado empinada. Aleja el extremo hasta que el rectángulo sea verde."));
			else if (applyRamp(rampStart_, rampEnd_)) {
				drawingRamp_ = false;
				notify(0, tr("Rampa creada. Puedes deshacerla con Ctrl+Z."));
			} else
				notify(1, tr("La rampa no alcanzó ningún punto del terreno. Prueba con más anchura o longitud."));
		}
	}

	// Pinceles de terreno.
	brushValid_ = brushTool && hovered && terrainRay(ray, brushPos_);
	const bool pickingMaterial = brushValid_ && tool_ == Tool::Paint && io.KeyAlt && leftClick;
	if (pickingMaterial && ed_.doc.terrain.grid && !ed_.doc.terrain.layers.empty()) {
		const nectar::DocTerrain& terrain = ed_.doc.terrain;
		const int ix = std::max(0, std::min(terrain.nx, int(std::lround((brushPos_.x - ed_.doc.minX) / ed_.doc.cell))));
		const int iz = std::max(0, std::min(terrain.nz, int(std::lround((brushPos_.z - ed_.doc.minZ) / ed_.doc.cell))));
		const std::size_t vertex = std::size_t(terrain.vertexIndex(ix, iz));
		paintMaterial_ = 0;
		for (std::size_t k = 1; k < terrain.layers.size(); k++) if (terrain.layers[k][vertex] > terrain.layers[std::size_t(paintMaterial_)][vertex]) paintMaterial_ = int(k);
		notify(0, tr("Material tomado del terreno."));
	}
	if (brushValid_ && leftDown && !pickingMaterial) {
		// Mayús hace lo contrario mientras se pinta.
		const bool erase = floorErase_, raise = heightOp_ == HeightOp::Raise, wErase = waterErase_, up = plateauUp_, sErase = scatterErase_;
		if (io.KeyShift) {
			if (tool_ == Tool::Scatter) scatterErase_ = !scatterErase_;
			if (tool_ == Tool::Floor) floorErase_ = !floorErase_;
			if (tool_ == Tool::Water) waterErase_ = !waterErase_;
			if (tool_ == Tool::Plateau) plateauUp_ = !plateauUp_;
		}
		if (!brushWasDown_) {
			// Allanar a la altura donde se empieza; elevar o hundir desde ella.
			flattenTarget_ = docHeight(brushPos_.x, brushPos_.z);
			if (tool_ == Tool::Plateau) flattenTarget_ += plateauUp_ ? plateauAmount_ : -plateauAmount_;
		}
		if (io.KeyShift && tool_ == Tool::Height && (heightOp_ == HeightOp::Raise || heightOp_ == HeightOp::Lower))
			heightOp_ = raise ? HeightOp::Lower : HeightOp::Raise;
		applyBrush(brushPos_, io.DeltaTime);
		floorErase_   = erase;
		waterErase_   = wErase;
		scatterErase_ = sErase;
		plateauUp_  = up;
		if (heightOp_ == HeightOp::Raise || heightOp_ == HeightOp::Lower) heightOp_ = raise ? HeightOp::Raise : HeightOp::Lower;
	}
	brushWasDown_ = brushValid_ && leftDown && !pickingMaterial;
	if (hovered && brushTool && io.KeyCtrl && io.MouseWheel != 0) {
		brushRadius_  = std::max(25.0f, std::min(1000.0f, brushRadius_ * std::pow(1.15f, io.MouseWheel)));
		io.MouseWheel = 0; // Ctrl + rueda: tamaño del pincel, no zoom
	}

	// Poner cosas y agua: vista previa bajo el ratón y clic para poner.
	placeValid_ = tool_ == Tool::Place && hovered && terrainRay(ray, placePos_);
	if (placeValid_ && leftClick) {
		placeAt(placePos_);
		rememberPlace(place_);
	}

	// Trazos: clic para cada punto; Retroceso quita el último; Intro aplica.
	if (tool_ == Tool::Trace) {
		V3 p;
		if (hovered && leftClick && terrainRay(ray, p)) traceNodes_.push_back(p);
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace) && !traceNodes_.empty()) traceNodes_.pop_back();
		if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter)) applyTrace();
	}

	// Muros: arrastrar de un extremo al otro.
	if (tool_ == Tool::Walls) {
		V3 hit;
		const bool onGround = hovered && terrainRay(ray, hit);
		if (onGround && leftClick) {
			drawingWall_ = true;
			wallStart_   = V3(float(snap(hit.x)), hit.y, float(snap(hit.z)));
		}
		if (drawingWall_) {
			if (onGround) placePos_ = V3(float(snap(hit.x)), hit.y, float(snap(hit.z)));
			if (!leftDown) {
				drawingWall_ = false;
				if ((placePos_ - wallStart_).length() >= 30) {
					nectar::DocWall w;
					w.ax        = wallStart_.x;
					w.az        = wallStart_.z;
					w.bx        = placePos_.x;
					w.bz        = placePos_.z;
					w.height    = wallHeight_;
					w.thickness = wallThickness_;
					ed_.doc.walls.push_back(w);
					ed_.selection = { { ItemType::Wall, int(ed_.doc.walls.size()) - 1, 0 } };
					ed_.touch();
				}
			}
		}
	} else {
		drawingWall_ = false;
	}

	// Mover: clic para elegir y arrastrar por el suelo.
	if (tool_ == Tool::Move) {
		if (hovered && leftClick) {
			bool emptyClick = false;
			dragHandle_ = -1;
			dragAxis_   = moveGizmoAxis(origin, size, io.MousePos);
			if (dragAxis_ == 3) {
				V3 onPlane;
				if (!selectionPivot(transformPivot_) || !rayPlaneY(ray, transformPivot_.y, onPlane)) dragAxis_ = 0;
				else rotateStartAngle_ = std::atan2(onPlane.z - transformPivot_.z, onPlane.x - transformPivot_.x);
			}
			if (dragAxis_ == 4) {
				ImVec2 pivotScreen;
				if (!selectionPivot(transformPivot_) || !project(transformPivot_, origin, size, pivotScreen)) dragAxis_ = 0;
				else scaleStartDistance_ = std::max(1.0f, std::hypot(io.MousePos.x - pivotScreen.x, io.MousePos.y - pivotScreen.y));
			}
			// ¿Un extremo del muro elegido?
			if (dragAxis_ == 0 && ed_.selection.size() == 1 && ed_.selection[0].type == ItemType::Wall && !ed_.doc.walls[std::size_t(ed_.selection[0].index)].isArc) {
				const nectar::DocWall& w = ed_.doc.walls[std::size_t(ed_.selection[0].index)];
				for (int e = 0; e < 2; e++) {
					const float ex = float(e ? w.bx : w.ax), ez = float(e ? w.bz : w.az);
					ImVec2 s;
					if (project(V3(ex, docHeight(ex, ez) + float(w.height), ez), origin, size, s) && std::hypot(s.x - io.MousePos.x, s.y - io.MousePos.y) < 14)
						dragHandle_ = e;
				}
			}
			ItemRef hit;
			float t = 0;
			if (dragAxis_ == 0 && dragHandle_ < 0) {
				int cycle = 0;
				if (io.KeyAlt) {
					if (std::hypot(io.MousePos.x - lastAltPickX_, io.MousePos.y - lastAltPickY_) <= 6.0f) altPickCycle_++;
					else altPickCycle_ = 0;
					lastAltPickX_ = io.MousePos.x;
					lastAltPickY_ = io.MousePos.y;
					cycle = altPickCycle_;
				} else {
					altPickCycle_ = 0;
				}
				if (pick(ray, hit, t, cycle)) {
					if (io.KeyCtrl) ed_.select(hit, true);
					else if (!ed_.selected(hit)) ed_.select(hit, false);
				} else {
					emptyClick = true;
					if (!io.KeyCtrl) ed_.clearSelection();
				}
			}
			pressX_    = io.MousePos.x;
			pressY_    = io.MousePos.y;
			boxEndX_   = pressX_;
			boxEndY_   = pressY_;
			boxSelecting_ = emptyClick;
			boxSelectionBase_ = ed_.selection;
			dragArmed_ = !boxSelecting_ && !ed_.selection.empty()
			          && (dragAxis_ == 3 || dragAxis_ == 4 || (transformMode_ == TransformMode::Move && terrainRay(ray, dragGrab_)));
			dragStart_.clear();
			for (const ItemRef& r : ed_.selection) dragStart_.push_back({ r, getXform(ed_.doc, r) });
		}
		if (boxSelecting_) {
			boxEndX_ = io.MousePos.x;
			boxEndY_ = io.MousePos.y;
			if (!leftDown) {
				const float x0 = std::min(pressX_, boxEndX_), x1 = std::max(pressX_, boxEndX_);
				const float y0 = std::min(pressY_, boxEndY_), y1 = std::max(pressY_, boxEndY_);
				if (x1 - x0 > 4.0f || y1 - y0 > 4.0f) {
					ed_.selection = boxSelectionBase_;
					for (const ItemRef& candidate : allItems(ed_.doc)) {
						if (itemHidden(candidate) || itemLocked(candidate)) continue;
						if (cfg_.ignoreDecorSelection && (candidate.type == ItemType::Prop || candidate.type == ItemType::Gen)) continue;
						const Xform x = getXform(ed_.doc, candidate);
						ImVec2 screen;
						bool inside = project(V3(float(x.x), docHeight(float(x.x), float(x.z)) + 20.0f, float(x.z)), origin, size, screen)
						           && screen.x >= x0 && screen.x <= x1 && screen.y >= y0 && screen.y <= y1;
						if (!inside && showTwins_ && ed_.doc.symmetric && itemCaps(candidate.type).twinned) {
							inside = project(V3(float(-x.x), docHeight(float(-x.x), float(-x.z)) + 20.0f, float(-x.z)), origin, size, screen)
							      && screen.x >= x0 && screen.x <= x1 && screen.y >= y0 && screen.y <= y1;
						}
						if (inside && !ed_.selected(candidate)) ed_.selection.push_back(candidate);
					}
				}
				boxSelecting_ = false;
				boxSelectionBase_.clear();
			}
		}
		if (dragArmed_ && leftDown && !dragging_ && std::hypot(io.MousePos.x - pressX_, io.MousePos.y - pressY_) > 4) dragging_ = true;
		if (dragging_ && dragAxis_ == 4) {
			ImVec2 pivotScreen;
			if (project(transformPivot_, origin, size, pivotScreen)) {
				float factor = std::hypot(io.MousePos.x - pivotScreen.x, io.MousePos.y - pivotScreen.y) / scaleStartDistance_;
				factor = std::max(0.1f, std::min(10.0f, factor));
				if (cfg_.snap) factor = std::max(0.1f, std::round(factor * 10.0f) / 10.0f);
				MapDoc& d = ed_.doc;
				for (const auto& st : dragStart_) {
					if (!itemExists(d, st.first) || !itemCaps(st.first.type).scale) continue;
					Xform x = st.second;
					x.scale = std::max(0.05, std::min(20.0, st.second.scale * factor));
					if (dragStart_.size() > 1) {
						x.x = transformPivot_.x + (st.second.x - transformPivot_.x) * factor;
						x.z = transformPivot_.z + (st.second.z - transformPivot_.z) * factor;
						x.y = st.second.y - docHeight(float(st.second.x), float(st.second.z)) + docHeight(float(x.x), float(x.z));
					}
					setXform(d, st.first, x);
					ItemRef other;
					if (mirrorEdits_ && d.symmetric && counterpart(st.first, other) && !ed_.selected(other)) mirrorToCounterpart(d, st.first);
				}
				ed_.touch();
			}
		} else if (dragging_ && dragAxis_ == 3) {
			V3 onPlane;
			if (rayPlaneY(ray, transformPivot_.y, onPlane)) {
				float delta = -(std::atan2(onPlane.z - transformPivot_.z, onPlane.x - transformPivot_.x) - rotateStartAngle_) * 180.0f / float(3.14159265358979323846);
				while (delta > 180.0f) delta -= 360.0f;
				while (delta < -180.0f) delta += 360.0f;
				if (cfg_.snap && cfg_.snapAngle > 0.0f) delta = std::round(delta / cfg_.snapAngle) * cfg_.snapAngle;
				const float a = delta * float(3.14159265358979323846 / 180.0), cs = std::cos(a), sn = std::sin(a);
				MapDoc& d = ed_.doc;
				for (const auto& st : dragStart_) {
					if (!itemExists(d, st.first)) continue;
					Xform x = st.second;
					if (dragStart_.size() > 1) {
						const double ox = st.second.x - transformPivot_.x, oz = st.second.z - transformPivot_.z;
						x.x = transformPivot_.x + cs * ox + sn * oz;
						x.z = transformPivot_.z - sn * ox + cs * oz;
						if (st.first.type == ItemType::Prop || st.first.type == ItemType::Gen)
							x.y = st.second.y - docHeight(float(st.second.x), float(st.second.z)) + docHeight(float(x.x), float(x.z));
					}
					if (itemCaps(st.first.type).rotateY) x.ry = st.second.ry + delta;
					setXform(d, st.first, x);
					ItemRef other;
					if (mirrorEdits_ && d.symmetric && counterpart(st.first, other) && !ed_.selected(other)) mirrorToCounterpart(d, st.first);
				}
				ed_.touch();
			}
		} else if (dragging_) {
			V3 at;
			if (terrainRay(ray, at)) {
				MapDoc& d = ed_.doc;
				if (dragHandle_ >= 0 && !ed_.selection.empty()) {
					nectar::DocWall& w = d.walls[std::size_t(ed_.selection[0].index)];
					(dragHandle_ ? w.bx : w.ax) = snap(at.x);
					(dragHandle_ ? w.bz : w.az) = snap(at.z);
				} else {
					double dx = at.x - dragGrab_.x, dz = at.z - dragGrab_.z;
					bool relativeSnap = false;
					if (dragAxis_ == 1 || dragAxis_ == 2) {
						const V3 axis = moveAxis(dragAxis_);
						double along = dx * axis.x + dz * axis.z;
						if (cfg_.localGizmo && cfg_.snap && cfg_.snapStep > 0.0f) {
							along = std::round(along / cfg_.snapStep) * cfg_.snapStep;
							relativeSnap = true;
						}
						dx = along * axis.x;
						dz = along * axis.z;
					}
					for (const auto& st : dragStart_) {
						if (!itemExists(d, st.first)) continue;
						Xform x = st.second;
						x.x     = relativeSnap ? st.second.x + dx : snap(st.second.x + dx);
						x.z     = relativeSnap ? st.second.z + dz : snap(st.second.z + dz);
						// La decoración sigue el suelo sin perder su altura sobre él.
						if (st.first.type == ItemType::Prop || st.first.type == ItemType::Gen)
							x.y = st.second.y - docHeight(float(st.second.x), float(st.second.z)) + docHeight(float(x.x), float(x.z));
						setXform(d, st.first, x);
						ItemRef other;
						if (mirrorEdits_ && d.symmetric && counterpart(st.first, other) && !ed_.selected(other)) mirrorToCounterpart(d, st.first);
					}
				}
				ed_.touch();
			}
		}
		if (!leftDown) {
			dragging_   = false;
			dragArmed_  = false;
			dragHandle_ = -1;
			dragAxis_   = 0;
		}
	} else {
		boxSelecting_ = false;
		boxSelectionBase_.clear();
		dragAxis_ = 0;
	}

	// Cámara.
	if (hovered && io.MouseWheel != 0) {
		if (cam_.topDown) cam_.topZoom = std::max(100.0f, cam_.topZoom * std::pow(0.88f, io.MouseWheel));
		else cam_.distance = std::max(80.0f, cam_.distance * std::pow(0.88f, io.MouseWheel));
	}
	if (viewportHovered_) {
		const float scale = cam_.topDown ? cam_.topZoom * 2 / size.y : cam_.distance * 0.0016f;
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !cam_.topDown) {
			cam_.yaw -= io.MouseDelta.x * 0.006f;
			cam_.pitch = std::max(0.08f, std::min(1.55f, cam_.pitch + io.MouseDelta.y * 0.006f));
		}
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (cam_.topDown && ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
			const V3 right   = cam_.topDown ? V3(1, 0, 0) : V3(std::cos(cam_.yaw), 0, -std::sin(cam_.yaw));
			const V3 forward = cam_.topDown ? V3(0, 0, -1) : V3(-std::sin(cam_.yaw), 0, -std::cos(cam_.yaw));
			cam_.target      = cam_.target - right * (io.MouseDelta.x * scale) + forward * (io.MouseDelta.y * scale);
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			const float speed = (io.KeyShift ? 3.0f : 1.0f) * std::max(400.0f, cam_.distance) * io.DeltaTime;
			const V3 right    = V3(std::cos(cam_.yaw), 0, -std::sin(cam_.yaw));
			const V3 forward  = V3(-std::sin(cam_.yaw), 0, -std::cos(cam_.yaw));
			if (ImGui::IsKeyDown(ImGuiKey_W)) cam_.target = cam_.target + forward * speed;
			if (ImGui::IsKeyDown(ImGuiKey_S)) cam_.target = cam_.target - forward * speed;
			if (ImGui::IsKeyDown(ImGuiKey_D)) cam_.target = cam_.target + right * speed;
			if (ImGui::IsKeyDown(ImGuiKey_A)) cam_.target = cam_.target - right * speed;
		}
	}
}

void App::renderScene(int w, int h)
{
	const M4 view = cam_.view(), proj = cam_.projection(float(w) / float(h));
	// Fondo pizarra ligeramente frío: separa bien siluetas y gizmos sin
	// competir con los colores naturales del mapa.
	renderer_.begin(target_, view, proj, Rgba { 0.16f, 0.20f, 0.23f, 1 });
	const MapDoc& d = ed_.doc;
	if (baseGpu_) renderer_.drawModel(*baseGpu_, M4());

	for (const ItemRef& r : allItems(d)) {
		if (itemHidden(r)) continue;
		drawItem(r, false);
		if (showTwins_ && d.symmetric && itemCaps(r.type).twinned) drawItem(r, true);
	}
	if (showWaterOverlay_) drawWater();
	if (showCollision_ && !collisionTris_.empty()) renderer_.drawTriangles(collisionTris_, Rgba { 0.95f, 0.25f, 0.16f, 0.24f }, false);
	if (showRoutes_) drawRoutes();
	if (tool_ == Tool::Height || showSlopesOverlay_) drawSlopes();
	// Hacia dónde va cada bloque que se empuja (y el del otro lado).
	{
		std::vector<V3> lines;
		for (const nectar::DocObject& o : d.objects) {
			if (o.kind != "push_rock") continue;
			for (int twin = 0; twin < (d.symmetric ? 2 : 1); twin++) {
				const float sgn = twin ? -1.0f : 1.0f, face = float((o.face + (twin ? 180.0 : 0.0)) * kDeg);
				const V3 a(float(o.x) * sgn, 0, float(o.z) * sgn);
				const V3 b(a.x + std::sin(face) * float(o.push), 0, a.z + std::cos(face) * float(o.push));
				lines.push_back(V3(a.x, docHeight(a.x, a.z) + 20, a.z));
				lines.push_back(V3(b.x, docHeight(b.x, b.z) + 20, b.z));
			}
		}
		if (!lines.empty()) renderer_.drawLines(lines, Rgba { 1, 0.85f, 0.2f, 1 });
	}
	if (tool_ == Tool::Move) drawSelection();
	drawGrid();

	// Pincel.
	if (brushValid_) {
		std::vector<V3> ring;
		if (brushShape_ == BrushShape::Circle) {
			for (int k = 0; k < 64; k++) {
				for (int e = 0; e < 2; e++) {
					const float a = (k + e) * 6.2831853f / 64;
					const float x = brushPos_.x + std::cos(a) * brushRadius_, z = brushPos_.z + std::sin(a) * brushRadius_;
					ring.push_back(V3(x, docHeight(x, z) + 4, z));
				}
			}
		} else {
			const V3 corners[] = { { brushPos_.x - brushRadius_, 0, brushPos_.z - brushRadius_ }, { brushPos_.x + brushRadius_, 0, brushPos_.z - brushRadius_ },
			                       { brushPos_.x + brushRadius_, 0, brushPos_.z + brushRadius_ }, { brushPos_.x - brushRadius_, 0, brushPos_.z + brushRadius_ } };
			for (int k = 0; k < 4; k++) {
				for (int e = 0; e < 2; e++) {
					V3 p = corners[(k + e) % 4];
					p.y = docHeight(p.x, p.z) + 4;
					ring.push_back(p);
				}
			}
		}
		renderer_.drawLines(ring, Rgba { 1, 0.85f, 0.2f, 1 });
		if (d.symmetric) {
			for (V3& v : ring) {
				v.x = -v.x;
				v.z = -v.z;
				v.y = docHeight(v.x, v.z) + 4;
			}
			renderer_.drawLines(ring, Rgba { 1, 0.85f, 0.2f, 0.45f });
		}
	}
	// Lo que se va a poner, bajo el ratón.
	if (placeValid_) {
		const std::string asset      = placeAsset(place_);
		std::shared_ptr<GpuModel> gm = asset.empty() ? nullptr : model(asset);
		const float gx = float(snap(placePos_.x)), gz = float(snap(placePos_.z));
		const M4 m     = M4::translate(V3(gx, placePos_.y, gz));
		// En rojo si no cabe: sin suelo o (salvo la decoración) en pendiente.
		bool fits = docSolid(gx, gz);
		if (fits && place_.type != ItemType::Prop && place_.type != ItemType::Gen) {
			const float h0 = docHeight(gx, gz);
			for (int k = 0; k < 4 && fits; k++) {
				const float ox = k == 0 ? 30.0f : k == 1 ? -30.0f : 0.0f, oz = k == 2 ? 30.0f : k == 3 ? -30.0f : 0.0f;
				fits = std::fabs(docHeight(gx + ox, gz + oz) - h0) < 22.0f;
			}
		}
		const Rgba tint = fits ? Rgba { 1, 1, 1, 0.55f } : Rgba { 1, 0.3f, 0.25f, 0.65f };
		if (gm) renderer_.drawModel(*gm, m, tint);
		else renderer_.drawCylinder(m * M4::scale(30, 60, 30), fits ? Rgba { 1, 0.85f, 0.2f, 0.55f } : Rgba { 1, 0.3f, 0.25f, 0.65f });
	}
	// El muro que se está dibujando.
	if (drawingWall_ && (placePos_ - wallStart_).length() > 1) {
		const Segment s { wallStart_.x, wallStart_.z, placePos_.x, placePos_.z };
		const float len = (placePos_ - wallStart_).length();
		renderer_.drawBox(segmentMatrix(s, (wallStart_.y + placePos_.y) * 0.5f) * M4::translate(V3(0, wallHeight_ * 0.5f, 0)) * M4::scale(len, wallHeight_, wallThickness_),
		                  Rgba { 0.85f, 0.75f, 0.55f, 0.7f });
	}
	if (drawingRamp_ && rampPreviewValid_) {
		const float dx = rampEnd_.x - rampStart_.x, dz = rampEnd_.z - rampStart_.z;
		const float length = std::hypot(dx, dz);
		if (length > 1.0f) {
			const float px = -dz / length * rampWidth_ * 0.5f, pz = dx / length * rampWidth_ * 0.5f;
			const V3 a(rampStart_.x + px, rampStart_.y + 8, rampStart_.z + pz), b(rampStart_.x - px, rampStart_.y + 8, rampStart_.z - pz);
			const V3 c(rampEnd_.x - px, rampEnd_.y + 8, rampEnd_.z - pz), e(rampEnd_.x + px, rampEnd_.y + 8, rampEnd_.z + pz);
			std::vector<V3> outline { a, b, b, c, c, e, e, a, rampStart_ + V3(0, 10, 0), rampEnd_ + V3(0, 10, 0) };
			const float height = std::fabs(rampEnd_.y - rampStart_.y);
			const bool level = height < 5.0f, safe = !level && height / length <= 0.577f;
			const Rgba colour = safe ? Rgba { 0.35f, 1.0f, 0.35f, 1.0f } : level ? Rgba { 1.0f, 0.78f, 0.18f, 1.0f } : Rgba { 1.0f, 0.25f, 0.15f, 1.0f };
			std::vector<V3> fill { a, b, c, a, c, e };
			renderer_.drawTriangles(fill, Rgba { colour.r, colour.g, colour.b, 0.22f }, false);
			renderer_.drawLines(outline, colour);
			renderer_.drawCylinder(M4::translate(rampStart_ + V3(0, 6, 0)) * M4::scale(22, 12, 22), Rgba { 1.0f, 0.82f, 0.18f, 1.0f });
			renderer_.drawCylinder(M4::translate(rampEnd_ + V3(0, 6, 0)) * M4::scale(22, 12, 22), colour);
		}
	}
	renderer_.end();
}

void App::drawWater()
{
	// El agua del juego son capas transparentes con efectos que el editor no
	// dibuja (se ve solo el fondo): aquí, una capa azul con su misma forma.
	if (!waterTris_.empty()) renderer_.drawTriangles(waterTris_, Rgba { 0.25f, 0.55f, 0.95f, 0.5f }, false);
}

void App::drawRoutes()
{
	if (compiled_.generation != ed_.liveGeneration || compiled_.routes.points.empty()) return;
	std::map<int, V3> points;
	for (const nectar::RoutePoint& p : compiled_.routes.points) points[p.index] = V3(p.pos.x, p.pos.y + 12.0f, p.pos.z);
	const std::map<int, int> components = routeComponents(compiled_.routes);
	const Rgba colours[] = { { 0.25f, 1.0f, 0.45f, 1.0f }, { 1.0f, 0.45f, 0.20f, 1.0f }, { 0.30f, 0.65f, 1.0f, 1.0f }, { 0.85f, 0.35f, 1.0f, 1.0f } };
	std::vector<V3> linksByComponent[4];
	for (const auto& link : compiled_.routes.links) {
		const auto a = points.find(link.first), b = points.find(link.second);
		if (a == points.end() || b == points.end()) continue;
		const int component = components.count(link.first) ? components.at(link.first) : 0;
		linksByComponent[component % 4].push_back(a->second);
		linksByComponent[component % 4].push_back(b->second);
	}
	for (int k = 0; k < 4; k++) if (!linksByComponent[k].empty()) renderer_.drawLines(linksByComponent[k], colours[k]);
	for (const nectar::RoutePoint& p : compiled_.routes.points) {
		const V3 at(p.pos.x, p.pos.y + 12.0f, p.pos.z);
		const float radius = std::max(7.0f, p.width * 0.08f);
		const int component = components.count(p.index) ? components.at(p.index) : 0;
		renderer_.drawCylinder(M4::translate(at) * M4::scale(radius, 18.0f, radius), colours[component % 4]);
	}
	if (!ed_.selection.empty()) {
		const Xform target = getXform(ed_.doc, ed_.selection.back());
		const int start = nearestRoutePoint(compiled_.routes, ed_.doc.bases[0].x, ed_.doc.bases[0].z);
		const int goal = nearestRoutePoint(compiled_.routes, target.x, target.z);
		const std::vector<int> path = routePath(compiled_.routes, start, goal);
		std::vector<V3> highlighted;
		for (std::size_t k = 1; k < path.size(); k++) {
			if (!points.count(path[k - 1]) || !points.count(path[k])) continue;
			highlighted.push_back(points[path[k - 1]] + V3(0, 10, 0));
			highlighted.push_back(points[path[k]] + V3(0, 10, 0));
		}
		if (!highlighted.empty()) renderer_.drawLines(highlighted, Rgba { 1.0f, 1.0f, 1.0f, 1.0f });
		if (path.empty() && goal >= 0 && points.count(goal))
			renderer_.drawCylinder(M4::translate(points[goal]) * M4::scale(24.0f, 45.0f, 24.0f), Rgba { 1.0f, 0.12f, 0.08f, 1.0f });
	}
}

void App::drawSlopes()
{
	// Celdas donde se resbala: más de 30° (tan 30° = 0,577).
	const MapDoc& d             = ed_.doc;
	const nectar::DocTerrain& g = d.terrain;
	if (!g.grid) return;
	const float cell = float(d.cell);
	std::vector<V3> tris;
	for (int iz = 0; iz < g.nz; iz++) {
		for (int ix = 0; ix < g.nx; ix++) {
			const std::uint8_t f[4] = { g.floor[std::size_t(g.vertexIndex(ix, iz))], g.floor[std::size_t(g.vertexIndex(ix + 1, iz))],
				                        g.floor[std::size_t(g.vertexIndex(ix, iz + 1))], g.floor[std::size_t(g.vertexIndex(ix + 1, iz + 1))] };
			if (std::max({ f[0], f[1], f[2], f[3] }) < 128) continue; // sin suelo
			const float h00 = g.heights[std::size_t(g.vertexIndex(ix, iz))], h10 = g.heights[std::size_t(g.vertexIndex(ix + 1, iz))];
			const float h01 = g.heights[std::size_t(g.vertexIndex(ix, iz + 1))], h11 = g.heights[std::size_t(g.vertexIndex(ix + 1, iz + 1))];
			const float steep = std::max({ std::fabs(h10 - h00), std::fabs(h01 - h00), std::fabs(h11 - h10), std::fabs(h11 - h01) }) / cell;
			if (steep <= 0.577f) continue;
			const float x0 = float(d.minX) + ix * cell, z0 = float(d.minZ) + iz * cell;
			const V3 a(x0, h00 + 3, z0), b(x0 + cell, h10 + 3, z0), c(x0, h01 + 3, z0 + cell), e(x0 + cell, h11 + 3, z0 + cell);
			tris.insert(tris.end(), { a, b, c, b, e, c });
		}
	}
	renderer_.drawTriangles(tris, Rgba { 1, 0.2f, 0.15f, 0.45f }, false);
}

void App::drawItem(const ItemRef& r, bool ghost)
{
	// Muros y charcas ya van en el mapa compilado.
	if (r.type == ItemType::Wall || r.type == ItemType::Pond) return;
	const M4 m              = itemMatrix(r, ghost);
	Rgba tint               = markerTint(r);
	tint.a                  = ghost ? 0.4f : 1.0f;
	const std::string asset = markerAsset(r);
	std::shared_ptr<GpuModel> gm = asset.empty() ? nullptr : model(asset);
	if (gm) renderer_.drawModel(*gm, m, tint);
	else if (r.type == ItemType::Rocket || r.type == ItemType::Ship) {
		Rgba c = r.type == ItemType::Ship ? Rgba { 0.85f, 0.85f, 0.9f, 1 } : playerColour(r.index);
		c.a    = tint.a;
		renderer_.drawCylinder(m * M4::scale(45, 150, 45), c);
		renderer_.drawCylinder(m * M4::translate(V3(0, 150, 0)) * M4::scale(25, 40, 25), Rgba { 0.9f, 0.9f, 0.95f, tint.a });
	} else renderer_.drawCylinder(m * M4::scale(30, 60, 30), Rgba { 1, 0.3f, 0.8f, tint.a });

	// Dueño: un disco de su color bajo lo que es de un jugador.
	if (itemCaps(r.type).perPlayer) {
		Rgba c          = playerColour(r.index);
		c.a             = 0.85f;
		const float rad = r.type == ItemType::Rocket ? 160.0f : r.type == ItemType::Gate ? 140.0f : 50.0f;
		renderer_.drawCylinder(M4::translate(V3(m.at(0, 3), m.at(1, 3) + 0.5f, m.at(2, 3))) * M4::scale(rad, 2, rad), c);
	}
}

void App::drawCutOptions()
{
	if (!iconHeader(tr("Recortar una pieza de este escenario"), Icon::More)) return;
	hint(tr("Elige una caja del nivel (un tronco, una raíz, una lata...) y guárdala en tu biblioteca: luego la puedes poner en cualquier mapa. El mapa solo guarda dónde está el recorte; las texturas salen de tu copia del juego."));
	ImGui::Checkbox(tr("Ver la caja"), &cutBox_);
	if (ImGui::Button(tr("Llevar la caja a donde miro"))) {
		const V3 half((cutHi_.x - cutLo_.x) * 0.5f, 0, (cutHi_.z - cutLo_.z) * 0.5f);
		const float h = cutHi_.y - cutLo_.y, y = docHeight(cam_.target.x, cam_.target.z);
		cutLo_        = V3(cam_.target.x - half.x, y - 50, cam_.target.z - half.z);
		cutHi_        = V3(cam_.target.x + half.x, y - 50 + h, cam_.target.z + half.z);
		cutBox_       = true;
	}
	float c[2] = { (cutLo_.x + cutHi_.x) * 0.5f, (cutLo_.z + cutHi_.z) * 0.5f };
	float s[2] = { cutHi_.x - cutLo_.x, cutHi_.z - cutLo_.z };
	float y[2] = { cutLo_.y, cutHi_.y };
	bool ch    = ImGui::DragFloat2(tr("Centro (x, z)"), c, 5.0f);
	ch |= ImGui::DragFloat2(tr("Tamaño (x, z)"), s, 5.0f, 20.0f, 3000.0f, "%.0f");
	ch |= ImGui::DragFloat2(tr("Altura (desde, hasta)"), y, 5.0f);
	hint(tr("Sube «desde» para dejar fuera el suelo y quedarte solo con lo que sobresale."));
	if (ch) {
		cutLo_  = V3(c[0] - s[0] * 0.5f, std::min(y[0], y[1] - 10), c[1] - s[1] * 0.5f);
		cutHi_  = V3(c[0] + s[0] * 0.5f, y[1], c[1] + s[1] * 0.5f);
		cutBox_ = true;
	}
	ImGui::InputText(tr("Nombre"), cutName_, sizeof(cutName_));
	ImGui::BeginDisabled(!cutName_[0]);
	if (ImGui::Button(tr("Guardar en mi biblioteca"))) {
		const std::string asset = nectar::cutAsset(ed_.doc.course, { cutLo_.x, cutLo_.y, cutLo_.z }, { cutHi_.x, cutHi_.y, cutHi_.z });
		cutModels_.erase(asset);
		if (std::shared_ptr<nectar::Model> m = cutModel(asset)) {
			cfg_.pieces.push_back({ cutName_, asset });
			cfg_.save();
			char buf[160];
			std::snprintf(buf, sizeof(buf), tr("«%s» guardada: %zu triángulos. Está en Poner cosas > Mis piezas de cualquier mapa."), cutName_, m->triangleCount());
			notify(0, buf);
			cutName_[0] = 0;
		} else {
			notify(2, tr("No hay nada del escenario dentro de la caja (o es demasiado grande)."));
		}
	}
	ImGui::EndDisabled();
}

void App::drawTraceOptions()
{
	ImGui::SeparatorText(tr("Trazos"));
	const char* modes[] = { tr("Camino"), tr("Río"), tr("Calzada"), tr("Zona de suelo"), tr("Hueco"), tr("Unir caminos"), tr("Sin caminos") };
	const char* tips[]  = { tr("Un sendero del suelo elegido que sigue la línea y allana un poco el terreno."),
		                    tr("Un cauce hundido con agua (solo lo cruzan los azules o un puente)."),
		                    tr("Un paso elevado sobre el terreno, con laderas a los lados."),
		                    tr("Contorno cerrado: todo lo de dentro pasa a tener suelo (del material elegido)."),
		                    tr("Contorno cerrado: todo lo de dentro se queda sin suelo (con paredes alrededor)."),
		                    tr("Dos puntos: los Pikmin podrán llevar cosas de uno a otro aunque la red de caminos no los una (subir a una malla, cruzar un puente)."),
		                    tr("Contorno cerrado: ahí no habrá caminos (para que los Pikmin no se metan por sitios raros).") };
	for (int i = 0; i < 7; i++) {
		if (i % 2) ImGui::SameLine();
		if (choiceButton(modes[i], int(traceMode_) == i, ImVec2(130, 30))) traceMode_ = TraceMode(i);
	}
	hint(tips[int(traceMode_)]);
	const bool line = traceMode_ == TraceMode::Path || traceMode_ == TraceMode::River || traceMode_ == TraceMode::Causeway;
	if (line) ImGui::SliderFloat(tr("Anchura"), &traceWidth_, 30.0f, 600.0f, "%.0f");
	if (traceMode_ == TraceMode::River) ImGui::SliderFloat(tr("Hondura"), &traceDepth_, 10.0f, 200.0f, "%.0f");
	if (traceMode_ == TraceMode::Causeway) ImGui::SliderFloat(tr("Altura"), &traceDepth_, 10.0f, 300.0f, "%.0f");
	const bool routeMode = traceMode_ == TraceMode::RouteLink || traceMode_ == TraceMode::RouteBlock;
	if (traceMode_ != TraceMode::Hole && !routeMode) drawSwatches(true);
	if (routeMode) {
		showRoutes_ = true;
		nectar::MapDoc& d = ed_.doc;
		for (std::size_t i = 0; i < d.routeLinks.size(); i++) {
			ImGui::PushID(int(i));
			if (ImGui::SmallButton(tr("Quitar"))) {
				d.routeLinks.erase(d.routeLinks.begin() + long(i));
				ed_.touch();
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::Text(tr("Unión %d"), int(i + 1));
			ImGui::PopID();
		}
		for (std::size_t i = 0; i < d.routeBlocks.size(); i++) {
			ImGui::PushID(int(1000 + i));
			if (ImGui::SmallButton(tr("Quitar"))) {
				d.routeBlocks.erase(d.routeBlocks.begin() + long(i));
				ed_.touch();
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::Text(tr("Zona sin caminos %d"), int(i + 1));
			ImGui::PopID();
		}
	}
	ImGui::Text(tr("Puntos: %d"), int(traceNodes_.size()));
	const std::size_t need = line || traceMode_ == TraceMode::RouteLink ? 2 : 3;
	ImGui::BeginDisabled(traceNodes_.size() < need);
	if (ImGui::Button(tr("Aplicar"), ImVec2(130, 30))) applyTrace();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(tr("Borrar puntos"), ImVec2(130, 30))) traceNodes_.clear();
	hint(tr("En los mapas espejo se hace también en el lado del otro jugador. Se puede deshacer con Ctrl+Z."));
}

void App::applyTrace()
{
	// Caminos a mano: se guardan en el mapa (el espejo lo pone el compilador).
	if (traceMode_ == TraceMode::RouteLink) {
		if (traceNodes_.size() < 2) return;
		const V3 &a = traceNodes_[traceNodes_.size() - 2], &b = traceNodes_.back();
		ed_.doc.routeLinks.push_back({ { a.x, a.y, a.z }, { b.x, b.y, b.z } });
		traceNodes_.clear();
		ed_.touch();
		return;
	}
	if (traceMode_ == TraceMode::RouteBlock) {
		if (traceNodes_.size() < 3) return;
		std::vector<std::pair<double, double>> poly;
		for (const V3& p : traceNodes_) poly.push_back({ p.x, p.z });
		ed_.doc.routeBlocks.push_back(poly);
		traceNodes_.clear();
		ed_.touch();
		return;
	}
	const bool line = traceMode_ == TraceMode::Path || traceMode_ == TraceMode::River || traceMode_ == TraceMode::Causeway;
	if (traceNodes_.size() < (line ? 2u : 3u) || !ensureTerrain()) return;
	MapDoc& d             = ed_.doc;
	nectar::DocTerrain& g = d.terrain;
	const float cell      = float(d.cell);
	paintMaterial_        = std::max(0, std::min(paintMaterial_, int(g.palette.size()) - 1));
	std::vector<std::vector<V3>> shapes { traceNodes_ };
	if (d.symmetric) {
		std::vector<V3> twin;
		for (const V3& p : traceNodes_) twin.push_back(V3(-p.x, p.y, -p.z));
		shapes.push_back(twin);
	}
	const float half = traceWidth_ * 0.5f, margin = std::max(cell * 1.5f, traceWidth_ * 0.4f);
	auto paint = [&](std::size_t v, float a) {
		for (std::size_t k = 0; k < g.layers.size(); k++) {
			std::uint8_t& x = g.layers[k][v];
			const float t   = int(k) == paintMaterial_ ? 255.0f : 0.0f;
			x               = std::uint8_t(std::lround(x + (t - x) * a));
		}
	};
	for (const std::vector<V3>& n : shapes) {
		for (int iz = 0; iz <= g.nz; iz++) {
			for (int ix = 0; ix <= g.nx; ix++) {
				const float x = float(d.minX) + ix * cell, z = float(d.minZ) + iz * cell;
				const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
				if (line) {
					float best = 1e9f, along = 0;
					for (std::size_t i = 0; i + 1 < n.size(); i++) {
						const float dx = n[i + 1].x - n[i].x, dz = n[i + 1].z - n[i].z, l2 = dx * dx + dz * dz;
						const float t  = l2 > 0 ? std::max(0.0f, std::min(1.0f, ((x - n[i].x) * dx + (z - n[i].z) * dz) / l2)) : 0.0f;
						const float dd = std::hypot(x - (n[i].x + dx * t), z - (n[i].z + dz * t));
						if (dd < best) {
							best  = dd;
							along = n[i].y + (n[i + 1].y - n[i].y) * t;
						}
					}
					if (best > half + margin) continue;
					float w = best <= half ? 1.0f : 1.0f - (best - half) / margin;
					w       = w * w * (3 - 2 * w);
					float h = g.heights[v];
					if (traceMode_ == TraceMode::Path) {
						h += (along - h) * w * 0.7f;
						if (best <= half) g.floor[v] = 255;
						paint(v, w);
					} else if (traceMode_ == TraceMode::River) {
						h += (along - traceDepth_ - h) * w;
						g.floor[v] = std::max(g.floor[v], std::uint8_t(std::lround(255.0f * w)));
						if (best <= half * 0.85f) g.water[v] = 255;
						paint(v, w);
					} else {
						h = std::max(h, h + (along + traceDepth_ - h) * w);
						if (best <= half) g.floor[v] = 255;
						paint(v, w);
					}
					g.heights[v] = std::int16_t(std::max(-3000.0f, std::min(3000.0f, std::round(h))));
				} else {
					bool inside = false;
					for (std::size_t i = 0, j = n.size() - 1; i < n.size(); j = i++) {
						if (((n[i].z > z) != (n[j].z > z)) && x < (n[j].x - n[i].x) * (z - n[i].z) / (n[j].z - n[i].z) + n[i].x) inside = !inside;
					}
					if (!inside) continue;
					if (traceMode_ == TraceMode::Area) {
						g.floor[v] = 255;
						paint(v, 1.0f);
					} else {
						g.floor[v] = 0;
					}
				}
			}
		}
	}
	traceNodes_.clear();
	ed_.touch();
}

void App::paintCliffs()
{
	if (!ensureTerrain()) return;
	nectar::DocTerrain& g = ed_.doc.terrain;
	paintMaterial_        = std::max(0, std::min(paintMaterial_, int(g.palette.size()) - 1));
	const float cell      = float(ed_.doc.cell);
	const std::vector<std::int16_t> h = g.heights;
	int painted                       = 0;
	for (int iz = 0; iz <= g.nz; iz++) {
		for (int ix = 0; ix <= g.nx; ix++) {
			auto at        = [&](int x, int z) { return float(h[std::size_t(g.vertexIndex(std::max(0, std::min(g.nx, x)), std::max(0, std::min(g.nz, z))))]); };
			const float gx = (at(ix + 1, iz) - at(ix - 1, iz)) / (2 * cell), gz = (at(ix, iz + 1) - at(ix, iz - 1)) / (2 * cell);
			const float s  = std::hypot(gx, gz);
			if (s < cliffSlope_ * 0.8f) continue;
			float a = std::min(1.0f, (s - cliffSlope_ * 0.8f) / (cliffSlope_ * 0.2f));
			a       = a * a * (3 - 2 * a);
			const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
			for (std::size_t k = 0; k < g.layers.size(); k++) {
				std::uint8_t& x = g.layers[k][v];
				const float t   = int(k) == paintMaterial_ ? 255.0f : 0.0f;
				x               = std::uint8_t(std::lround(x + (t - x) * a));
			}
			painted++;
		}
	}
	if (painted) ed_.touch();
	notify(0, painted ? tr("Acantilados pintados.") : tr("No hay pendientes tan fuertes: baja «Desde la pendiente»."));
}

void App::drawSelection()
{
	if (tool_ == Tool::Trace && (traceMode_ == TraceMode::RouteLink || traceMode_ == TraceMode::RouteBlock)) {
		std::vector<V3> links, blocks;
		for (const nectar::MapDoc::RouteLink& l : ed_.doc.routeLinks) {
			links.push_back(V3(float(l.a[0]), float(l.a[1]) + 10, float(l.a[2])));
			links.push_back(V3(float(l.b[0]), float(l.b[1]) + 10, float(l.b[2])));
		}
		for (const auto& poly : ed_.doc.routeBlocks) {
			for (std::size_t i = 0; i < poly.size(); i++) {
				const auto &a = poly[i], &b = poly[(i + 1) % poly.size()];
				blocks.push_back(V3(float(a.first), docHeight(float(a.first), float(a.second)) + 10, float(a.second)));
				blocks.push_back(V3(float(b.first), docHeight(float(b.first), float(b.second)) + 10, float(b.second)));
			}
		}
		if (!links.empty()) renderer_.drawLines(links, Rgba { 1.0f, 0.6f, 0.1f, 1 });
		if (!blocks.empty()) renderer_.drawLines(blocks, Rgba { 1.0f, 0.2f, 0.2f, 1 });
	}
	if (tool_ == Tool::Trace && !traceNodes_.empty()) {
		std::vector<V3> path;
		const bool closed = traceMode_ == TraceMode::Area || traceMode_ == TraceMode::Hole || traceMode_ == TraceMode::RouteBlock;
		for (std::size_t i = 0; i + 1 < traceNodes_.size() + (closed ? 1 : 0); i++) {
			const V3 &a = traceNodes_[i], &b = traceNodes_[(i + 1) % traceNodes_.size()];
			path.push_back(V3(a.x, a.y + 8, a.z));
			path.push_back(V3(b.x, b.y + 8, b.z));
		}
		for (const V3& p : traceNodes_) boxLines(path, M4(), V3(p.x - 12, p.y, p.z - 12), V3(p.x + 12, p.y + 24, p.z + 12));
		renderer_.drawLines(path, Rgba { 0.2f, 0.8f, 1.0f, 1 });
	}
	if (cutBox_ && courseMode()) {
		std::vector<V3> box;
		boxLines(box, M4(), cutLo_, cutHi_);
		renderer_.drawLines(box, Rgba { 0.3f, 1.0f, 0.4f, 1 });
	}
	std::vector<V3> lines;
	const MapDoc& d = ed_.doc;
	for (const ItemRef& r : ed_.selection) {
		if (!itemExists(d, r)) continue;
		if (r.type == ItemType::Wall) {
			const nectar::DocWall& w = d.walls[std::size_t(r.index)];
			for (const Segment& s : wallSegments(w)) {
				const float len = float(std::hypot(s.bx - s.ax, s.bz - s.az));
				const float y   = docHeight(float((s.ax + s.bx) * 0.5), float((s.az + s.bz) * 0.5));
				boxLines(lines, segmentMatrix(s, y), V3(-len / 2, 0, float(-w.thickness / 2)), V3(len / 2, float(w.height), float(w.thickness / 2)));
			}
			// Los extremos, para arrastrarlos.
			if (!w.isArc && ed_.selection.size() == 1) {
				for (int e = 0; e < 2; e++) {
					const float ex = float(e ? w.bx : w.ax), ez = float(e ? w.bz : w.az);
					renderer_.drawCylinder(M4::translate(V3(ex, docHeight(ex, ez), ez)) * M4::scale(14, float(w.height) + 20, 14), Rgba { 1, 0.85f, 0.1f, 1 });
				}
			}
			continue;
		}
		if (r.type == ItemType::Pond) {
			const nectar::DocPond& p = d.ponds[std::size_t(r.index)];
			for (int k = 0; k < 48; k++) {
				const float a0 = k * 6.2831853f / 48, a1 = (k + 1) * 6.2831853f / 48;
				const float y  = docHeight(float(p.cx), float(p.cz)) + 3;
				lines.push_back(V3(float(p.cx + p.rx * std::cos(a0)), y, float(p.cz + p.rz * std::sin(a0))));
				lines.push_back(V3(float(p.cx + p.rx * std::cos(a1)), y, float(p.cz + p.rz * std::sin(a1))));
			}
			continue;
		}
		const std::string asset      = markerAsset(r);
		std::shared_ptr<GpuModel> gm = asset.empty() ? nullptr : model(asset);
		const M4 m                   = itemMatrix(r, false);
		V3 lo, hi;
		fallbackBox(r.type, lo, hi);
		if (gm) boxLines(lines, m, gm->boundsMin, gm->boundsMax);
		else boxLines(lines, m, lo, hi);
	}
	renderer_.drawLines(lines, Rgba { 1, 0.85f, 0.1f, 1 });
}

void App::drawLabels(const ImVec2& origin, const ImVec2& size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	for (const ItemRef& r : allItems(ed_.doc)) {
		if (itemHidden(r)) continue;
		const bool storyLabel = r.type == ItemType::Captain || r.type == ItemType::Ship || r.type == ItemType::StoryOnion;
		if (!itemCaps(r.type).perPlayer && r.type != ItemType::BigPiece && !storyLabel) continue;
		const Xform x = getXform(ed_.doc, r);
		ImVec2 s;
		if (!project(V3(float(x.x), docHeight(float(x.x), float(x.z)) + 90, float(x.z)), origin, size, s)) continue;
		// Cortas: el color ya dice de qué jugador es.
		static const char* const onions[] = { "Cebolla azul", "Cebolla roja", "Cebolla amarilla" };
		const std::string text = storyLabel ? itemName(ed_.doc, r)
		                       : r.type == ItemType::Base ? tr("Salida") : r.type == ItemType::Rocket ? tr("Cohete") : r.type == ItemType::Gate ? tr("Puerta de roca")
		                       : r.type == ItemType::BombPile ? tr("Rocas bomba") : r.type == ItemType::Onion ? tr(onions[r.sub]) : tr("Pieza grande");
		const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
		const Rgba c           = r.type == ItemType::BigPiece ? Rgba { 0.9f, 0.75f, 0.2f, 1 } : storyLabel ? Rgba { 0.25f, 0.6f, 0.75f, 1 } : playerColour(r.index);
		dl->AddRectFilled(ImVec2(s.x - ts.x / 2 - 5, s.y - ts.y - 6), ImVec2(s.x + ts.x / 2 + 5, s.y), IM_COL32(int(c.r * 200), int(c.g * 200), int(c.b * 200), 200), 4);
		dl->AddText(ImVec2(s.x - ts.x / 2, s.y - ts.y - 3), IM_COL32(255, 255, 255, 255), text.c_str());
	}
	dl->PopClipRect();
}

bool App::groundHit(const Ray& ray, V3& hit) const { return terrainRay(ray, hit); }

bool App::pick(const Ray& ray, ItemRef& out, float& bestT, int cycle)
{
	bestT           = -1;
	const MapDoc& d = ed_.doc;
	std::vector<std::pair<float, ItemRef>> hits;
	auto consider   = [&](const ItemRef& r, float t) {
		if (t < 0) return;
		for (auto& hit : hits) {
			if (hit.second == r) {
				hit.first = std::min(hit.first, t);
				return;
			}
		}
		hits.push_back({ t, r });
	};
	for (const ItemRef& r : allItems(d)) {
		if (itemHidden(r) || itemLocked(r)) continue;
		if (cfg_.ignoreDecorSelection && (r.type == ItemType::Prop || r.type == ItemType::Gen)) continue;
		if (r.type == ItemType::Wall) {
			const nectar::DocWall& w = d.walls[std::size_t(r.index)];
			for (const Segment& s : wallSegments(w)) {
				const float len = float(std::hypot(s.bx - s.ax, s.bz - s.az));
				const float y   = docHeight(float((s.ax + s.bx) * 0.5), float((s.az + s.bz) * 0.5));
				consider(r, rayBox(ray, segmentMatrix(s, y), V3(-len / 2, -20, float(-w.thickness / 2)), V3(len / 2, float(w.height), float(w.thickness / 2))));
			}
			continue;
		}
		if (r.type == ItemType::Pond) {
			const nectar::DocPond& p = d.ponds[std::size_t(r.index)];
			V3 h;
			if (rayPlaneY(ray, docHeight(float(p.cx), float(p.cz)), h)) {
				const double ex = (h.x - p.cx) / p.rx, ez = (h.z - p.cz) / p.rz;
				// Las charcas, solo por su orilla: el centro deja elegir lo que hay dentro.
				const double e = ex * ex + ez * ez;
				if (e < 1.0 && e > 0.55) consider(r, (h - ray.origin).length() + 1.0f);
			}
			continue;
		}
		const std::string asset      = markerAsset(r);
		std::shared_ptr<GpuModel> gm = asset.empty() ? nullptr : model(asset);
		V3 lo, hi;
		fallbackBox(r.type, lo, hi);
		if (gm) {
			lo = gm->boundsMin;
			hi = gm->boundsMax;
		}
		// Que lo pequeño también se pueda coger.
		for (float* v : { &lo.x, &lo.z }) *v = std::min(*v, -25.0f);
		for (float* v : { &hi.x, &hi.z }) *v = std::max(*v, 25.0f);
		hi.y = std::max(hi.y, 40.0f);
		consider(r, rayBox(ray, itemMatrix(r, false), lo, hi));
	}
	if (hits.empty()) return false;
	std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	const auto& chosen = hits[std::size_t(std::max(0, cycle)) % hits.size()];
	bestT = chosen.first;
	out   = chosen.second;
	return true;
}

// ── Terreno ─────────────────────────────────────────────────────────────────

bool App::ensureTerrain()
{
	if (courseMode()) {
		notify(1, tr("En los escenarios del juego el suelo no se puede cambiar: mueve, quita o añade cosas."));
		return false;
	}
	if (ed_.doc.terrain.grid) return true;
	nectar::makeTerrainGrid(ed_.doc);
	ed_.touch();
	return true;
}

bool App::hasGroundProps() const
{
	for (const nectar::DocProp& p : ed_.doc.props) {
		if (p.ground) return true;
	}
	return false;
}

float App::docHeight(float x, float z) const
{
	const MapDoc& d             = ed_.doc;
	const nectar::DocTerrain& g = d.terrain;
	if (!d.course.empty()) {
		float y = 0.0f;
		return courseField_.at(x, z, y) ? y : 0.0f;
	}
	if (meshField_.nx > 0) {
		float y = 0.0f;
		if (meshField_.at(x, z, y)) return y;
	}
	if (!g.grid || g.nx < 1 || g.nz < 1) return 0.0f;
	const float cell = float(d.cell);
	const float fx   = std::max(0.0f, std::min(float(g.nx), (x - float(d.minX)) / cell));
	const float fz   = std::max(0.0f, std::min(float(g.nz), (z - float(d.minZ)) / cell));
	const int ix = std::min(g.nx - 1, int(fx)), iz = std::min(g.nz - 1, int(fz));
	const float tx = fx - ix, tz = fz - iz;
	auto h         = [&](int a, int b) { return float(g.heights[std::size_t(g.vertexIndex(a, b))]); };
	if (tx + tz <= 1.0f) return h(ix, iz) + (h(ix + 1, iz) - h(ix, iz)) * tx + (h(ix, iz + 1) - h(ix, iz)) * tz;
	return h(ix + 1, iz + 1) + (h(ix, iz + 1) - h(ix + 1, iz + 1)) * (1.0f - tx) + (h(ix + 1, iz) - h(ix + 1, iz + 1)) * (1.0f - tz);
}

bool App::docSolid(float x, float z) const
{
	const MapDoc& d             = ed_.doc;
	const nectar::DocTerrain& g = d.terrain;
	if (x <= d.minX || x >= d.maxX || z <= d.minZ || z >= d.maxZ) return false;
	if (!d.course.empty()) {
		float y;
		return courseField_.at(x, z, y);
	}
	if (meshField_.nx > 0) {
		float y;
		if (meshField_.at(x, z, y)) return true;
	}
	if (!g.grid) return !g.none; // sin suelo propio, solo donde hay malla
	// El suelo, interpolado como los triángulos del terreno.
	const float cell = float(d.cell);
	const float fx = (x - float(d.minX)) / cell, fz = (z - float(d.minZ)) / cell;
	const int ix = std::min(g.nx - 1, int(fx)), iz = std::min(g.nz - 1, int(fz));
	const float tx = fx - ix, tz = fz - iz;
	auto f         = [&](int a, int b) {
        const bool ring = a == 0 || b == 0 || a == g.nx || b == g.nz; // el borde del mapa nunca tiene suelo
        return ring ? 0.0f : float(g.floor[std::size_t(g.vertexIndex(a, b))]);
	};
	float v;
	if (tx + tz <= 1.0f) v = f(ix, iz) + (f(ix + 1, iz) - f(ix, iz)) * tx + (f(ix, iz + 1) - f(ix, iz)) * tz;
	else v = f(ix + 1, iz + 1) + (f(ix, iz + 1) - f(ix + 1, iz + 1)) * (1.0f - tx) + (f(ix + 1, iz) - f(ix + 1, iz + 1)) * (1.0f - tz);
	return v >= 128.0f;
}

bool App::terrainRay(const Ray& ray, V3& hit) const
{
	// Corte con el plano a la altura del terreno, afinando unas cuantas veces.
	float h = 0;
	for (int i = 0; i < 8; i++) {
		if (!rayPlaneY(ray, h, hit)) return false;
		const float nh = docHeight(hit.x, hit.z);
		if (std::fabs(nh - h) < 0.5f) break;
		h = h + (nh - h) * 0.7f;
	}
	const MapDoc& d = ed_.doc;
	return hit.x >= d.minX && hit.x <= d.maxX && hit.z >= d.minZ && hit.z <= d.maxZ;
}

bool App::applyRamp(const V3& from, const V3& to)
{
	MapDoc& d = ed_.doc;
	nectar::DocTerrain& terrain = d.terrain;
	if (!terrain.grid) return false;
	bool changed = false;
	auto applyOne = [&](const V3& a, const V3& b) {
		const double dx = b.x - a.x, dz = b.z - a.z, length2 = dx * dx + dz * dz;
		if (length2 < 25.0 * 25.0) return;
		const double length = std::sqrt(length2), halfWidth = rampWidth_ * 0.5;
		const double minX = std::min(a.x, b.x) - halfWidth, maxX = std::max(a.x, b.x) + halfWidth;
		const double minZ = std::min(a.z, b.z) - halfWidth, maxZ = std::max(a.z, b.z) + halfWidth;
		const int ix0 = std::max(0, int(std::floor((minX - d.minX) / d.cell))), ix1 = std::min(terrain.nx, int(std::ceil((maxX - d.minX) / d.cell)));
		const int iz0 = std::max(0, int(std::floor((minZ - d.minZ) / d.cell))), iz1 = std::min(terrain.nz, int(std::ceil((maxZ - d.minZ) / d.cell)));
		for (int iz = iz0; iz <= iz1; iz++) {
			for (int ix = ix0; ix <= ix1; ix++) {
				const double x = d.minX + ix * d.cell, z = d.minZ + iz * d.cell;
				const double along = ((x - a.x) * dx + (z - a.z) * dz) / length2;
				if (along < 0.0 || along > 1.0) continue;
				const double lateral = std::fabs((x - a.x) * dz - (z - a.z) * dx) / length;
				if (lateral > halfWidth) continue;
				double profile = along;
				if (rampSmooth_) profile = along * along * (3.0 - 2.0 * along);
				const double target = a.y + (b.y - a.y) * profile;
				const double edge = halfWidth > 1.0 ? std::min(1.0, (halfWidth - lateral) / std::max(d.cell, halfWidth * 0.2)) : 1.0;
				const std::size_t vertex = std::size_t(terrain.vertexIndex(ix, iz));
				const double blended = terrain.heights[vertex] + (target - terrain.heights[vertex]) * edge;
				const std::int16_t newHeight = std::int16_t(std::max(-3000.0, std::min(3000.0, std::round(blended))));
				const std::uint8_t newFloor = std::max(terrain.floor[vertex], std::uint8_t(std::lround(255.0 * edge)));
				changed |= terrain.heights[vertex] != newHeight || terrain.floor[vertex] != newFloor;
				terrain.heights[vertex] = newHeight;
				terrain.floor[vertex] = newFloor;
			}
		}
	};
	applyOne(from, to);
	if (d.symmetric) applyOne(V3(-from.x, from.y, -from.z), V3(-to.x, to.y, -to.z));
	if (changed) ed_.touch();
	return changed;
}

void App::applyBrush(const V3& at, float dt)
{
	if (tool_ == Tool::Scatter) {
		scatter(at);
		return;
	}
	MapDoc& d             = ed_.doc;
	nectar::DocTerrain& g = d.terrain;
	if (!g.grid) return;
	const float cell = float(d.cell), r = brushRadius_;
	paintMaterial_   = std::max(0, std::min(paintMaterial_, int(g.palette.size()) - 1));
	std::vector<V3> centres { at };
	if (d.symmetric) centres.push_back(V3(-at.x, at.y, -at.z)); // el lado del otro jugador igual
	bool changed = false;
	for (const V3& c : centres) {
		auto brushDistance = [&](float x, float z) {
			const float dx = std::fabs(x - c.x), dz = std::fabs(z - c.z);
			return brushShape_ == BrushShape::Square ? std::max(dx, dz) : std::hypot(dx, dz);
		};
		auto brushWeight = [&](float x, float z) {
			const float distance = brushDistance(x, z);
			if (distance > r) return 0.0f;
			if (brushFalloff_ == BrushFalloff::Hard) return 1.0f;
			const float t = std::max(0.0f, 1.0f - distance / r);
			return t * t * (3.0f - 2.0f * t);
		};
		const int x0 = std::max(0, int(std::floor((c.x - r - d.minX) / cell))), x1 = std::min(g.nx, int(std::ceil((c.x + r - d.minX) / cell)));
		const int z0 = std::max(0, int(std::floor((c.z - r - d.minZ) / cell))), z1 = std::min(g.nz, int(std::ceil((c.z + r - d.minZ) / cell)));
		if (tool_ == Tool::Plateau) {
			// Borde duro: todo lo de dentro, a la misma altura.
			const std::int16_t v = std::int16_t(std::max(-3000.0f, std::min(3000.0f, std::round(flattenTarget_))));
			for (int iz = z0; iz <= z1; iz++) {
				for (int ix = x0; ix <= x1; ix++) {
					const float vx = float(d.minX) + ix * cell, vz = float(d.minZ) + iz * cell;
					if (brushDistance(vx, vz) > r) continue;
					std::int16_t& h = g.heights[std::size_t(g.vertexIndex(ix, iz))];
					if (h != v) {
						h       = v;
						changed = true;
					}
				}
			}
		} else if (tool_ == Tool::Water || tool_ == Tool::Floor || tool_ == Tool::Paint) {
			// Por punto: núcleo firme y borde suave, para que las formas salgan redondas.
			for (int iz = z0; iz <= z1; iz++) {
				for (int ix = x0; ix <= x1; ix++) {
					const float vx = float(d.minX) + ix * cell, vz = float(d.minZ) + iz * cell;
					const float a  = brushWeight(vx, vz);
					if (a <= 0.0f) continue;
					const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
					const std::uint8_t full = std::uint8_t(std::lround(255.0f * a)), keep = std::uint8_t(std::lround(255.0f * (1.0f - a)));
					auto raise = [&](std::uint8_t& x) {
						if (x < full) {
							x       = full;
							changed = true;
						}
					};
					auto lower = [&](std::uint8_t& x) {
						if (x > keep) {
							x       = keep;
							changed = true;
						}
					};
					if (tool_ == Tool::Water) {
						if (waterErase_) lower(g.water[v]);
						else raise(g.water[v]);
						continue;
					}
					if (tool_ == Tool::Floor) {
						if (floorErase_) {
							lower(g.floor[v]);
							continue;
						}
						raise(g.floor[v]);
					}
					// Pintar (y el suelo nuevo, del material elegido).
					for (std::size_t k = 0; k < g.layers.size(); k++) {
						if (int(k) == paintMaterial_) raise(g.layers[k][v]);
						else lower(g.layers[k][v]);
					}
				}
			}
		} else if (tool_ == Tool::Height) {
			// Sellos: uno por clic, no mientras se arrastra.
			const bool stamp = heightOp_ == HeightOp::Hill || heightOp_ == HeightOp::Crater || heightOp_ == HeightOp::Mesa;
			if (stamp && brushWasDown_) continue;
			const float mirror = &c == &centres[0] ? 1.0f : -1.0f; // el ruido, igual en el lado del otro jugador
			const std::vector<std::int16_t> before = g.heights;
			for (int iz = z0; iz <= z1; iz++) {
				for (int ix = x0; ix <= x1; ix++) {
					const float vx = float(d.minX) + ix * cell, vz = float(d.minZ) + iz * cell;
					const float w = brushWeight(vx, vz);
					if (w <= 0.0f) continue;
					std::int16_t& h = g.heights[std::size_t(g.vertexIndex(ix, iz))];
					float nh        = h;
					switch (heightOp_) {
					case HeightOp::Raise: nh += brushStrength_ * dt * w; break;
					case HeightOp::Lower: nh -= brushStrength_ * dt * w; break;
					case HeightOp::Flatten: nh += (flattenTarget_ - nh) * std::min(1.0f, w * dt * 6.0f); break;
					case HeightOp::Noise: nh += terrainNoise2(mirror * vx, mirror * vz, 140.0f) * brushStrength_ * dt * w * 1.5f; break;
					case HeightOp::Hill: nh += brushStrength_ * w; break;
					case HeightOp::Crater: {
						const float t = std::min(1.0f, brushDistance(vx, vz) / r);
						const float bowl = 1.0f - std::max(0.0f, std::min(1.0f, (t - 0.1f) / 0.6f));
						nh += brushStrength_ * (0.35f * std::exp(-std::pow((t - 0.82f) / 0.11f, 2.0f)) - bowl * bowl * (3 - 2 * bowl));
						break;
					}
					case HeightOp::Mesa: {
						const float t = std::min(1.0f, brushDistance(vx, vz) / r);
						const float k = 1.0f - std::max(0.0f, std::min(1.0f, (t - 0.65f) / 0.35f));
						nh += (std::max(nh, flattenTarget_ + brushStrength_) - nh) * k * k * (3 - 2 * k);
						break;
					}
					case HeightOp::Smooth: {
						float sum = 0;
						int n     = 0;
						const int nb[4][2] = { { ix - 1, iz }, { ix + 1, iz }, { ix, iz - 1 }, { ix, iz + 1 } };
						for (const auto& q : nb) {
							if (q[0] < 0 || q[1] < 0 || q[0] > g.nx || q[1] > g.nz) continue;
							sum += before[std::size_t(g.vertexIndex(q[0], q[1]))];
							n++;
						}
						if (n) nh += (sum / n - nh) * std::min(1.0f, w * dt * 8.0f);
						break;
					}
					}
					const std::int16_t v = std::int16_t(std::max(-3000.0f, std::min(3000.0f, std::round(nh))));
					if (v != h) {
						h       = v;
						changed = true;
					}
				}
			}
		}
	}
	if (changed) ed_.touch();
}

std::shared_ptr<nectar::Model> App::courseModel(const std::string& source)
{
	auto it = courseModels_.find(source);
	if (it != courseModels_.end()) return it->second;
	auto m = std::make_shared<nectar::Model>();
	std::string err;
	if (!nectar::loadMod(cfg_.dataDir() + "/" + source, *m, err)) m.reset();
	courseModels_[source] = m;
	return m;
}

unsigned App::swatch(const std::string& source, int index)
{
	const std::string key = source + "#" + std::to_string(index);
	auto it               = swatches_.find(key);
	if (it != swatches_.end()) return it->second;
	unsigned tex = 0;
	if (std::shared_ptr<nectar::Model> m = courseModel(source)) {
		if (index >= 0 && std::size_t(index) < m->materials.size()) {
			const int attr = m->materials[std::size_t(index)].texAttr;
			if (attr >= 0 && std::size_t(attr) < m->texAttrs.size()) {
				const int t = m->texAttrs[std::size_t(attr)].texture;
				if (t >= 0 && std::size_t(t) < m->textures.size()) tex = uploadTexture(nectar::shrinkToFit(m->textures[std::size_t(t)].decode(), 96, 96), false);
			}
		}
	}
	swatches_[key] = tex;
	return tex;
}

void App::drawMaterialBrowser()
{
	if (openMatBrowser_) {
		ImGui::OpenPopup(tr("Elige un suelo"));
		openMatBrowser_ = false;
	}
	ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal(tr("Elige un suelo"), nullptr)) return;
	hint(matBrowserTarget_ == -2 ? tr("Elige el aspecto de las paredes del borde: sale de los escenarios del juego.")
	                             : tr("Elige un suelo de los escenarios del juego. Los primeros de cada lista son los que más se usan como suelo."));
	ImGui::BeginChild("courses", ImVec2(240, -40), ImGuiChildFlags_Borders);
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			ImGui::Spacing();
			ImGui::TextColored(kDim, "%s", tr("Escenarios de prueba"));
		}
		for (const nectar::CatalogEntry& e : catalog_.entries) {
			if (e.kind != nectar::AssetKind::Model || e.category != "terrain" || !e.error.empty()) continue;
			const char* name = courseName(e.path);
			if ((name != nullptr) != (pass == 0)) continue;
			const std::string label = name ? name : fs::path(e.path).stem().string();
			if (ImGui::Selectable((label + "##" + e.path).c_str(), matBrowserCourse_ == e.path)) matBrowserCourse_ = e.path;
		}
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("mats", ImVec2(0, -40), ImGuiChildFlags_Borders);
	if (!matBrowserCourse_.empty()) {
		std::shared_ptr<nectar::Model> m = courseModel(matBrowserCourse_);
		if (m && !courseStats_.count(matBrowserCourse_)) {
			// Cuántos triángulos usan cada material, y cuántos miran hacia arriba.
			std::map<int, std::array<int, 3>> st;
			for (const nectar::BakedTri& t : nectar::bakeModel(*m)) {
				std::array<int, 3>& e = st[t.material];
				e[0]                  = t.material;
				e[1]++;
				const V3 a(t.p[1].x - t.p[0].x, t.p[1].y - t.p[0].y, t.p[1].z - t.p[0].z), b(t.p[2].x - t.p[0].x, t.p[2].y - t.p[0].y, t.p[2].z - t.p[0].z);
				if (std::fabs(a.cross(b).normalized().y) > 0.8f) e[2]++;
			}
			std::vector<std::array<int, 3>> list;
			for (auto& kv : st) list.push_back(kv.second);
			std::sort(list.begin(), list.end(), [&](const std::array<int, 3>& x, const std::array<int, 3>& y) {
				return matBrowserTarget_ == -2 ? x[1] - x[2] > y[1] - y[2] : x[2] > y[2];
			});
			courseStats_[matBrowserCourse_] = list;
		}
		int col        = 0;
		const int cols = std::max(1, int(ImGui::GetContentRegionAvail().x / 104));
		for (const std::array<int, 3>& e : courseStats_[matBrowserCourse_]) {
			const unsigned t = swatch(matBrowserCourse_, e[0]);
			if (!t) continue;
			ImGui::PushID(e[0]);
			if (ImGui::ImageButton("##m", ImTextureID(std::intptr_t(t)), ImVec2(88, 88))) {
				nectar::DocMaterial nm;
				nm.source             = matBrowserCourse_;
				nm.index              = e[0];
				nectar::DocTerrain& g = ed_.doc.terrain;
				if (matBrowserTarget_ == -2) {
					nm.attribute = 1; // roca
					g.border     = nm;
				} else if (matBrowserTarget_ >= 0 && std::size_t(matBrowserTarget_) < g.palette.size()) {
					nm.attribute                              = g.palette[std::size_t(matBrowserTarget_)].attribute;
					g.palette[std::size_t(matBrowserTarget_)] = nm;
				} else {
					g.palette.push_back(nm);
					g.layers.emplace_back(std::size_t(g.vertexCount()), std::uint8_t(0));
					paintMaterial_ = int(g.palette.size()) - 1;
				}
				ed_.touch();
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopID();
			if (++col < cols) ImGui::SameLine();
			else col = 0;
		}
	} else {
		hint(tr("Elige un escenario a la izquierda."));
	}
	ImGui::EndChild();
	if (ImGui::Button(tr("Cancelar"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}


// ── Campañas ────────────────────────────────────────────────────────────────
// Un paquete (user/campaigns/<id>/): campaign.json con las zonas sobre una
// imagen de mapa, sus caminos y reglas, y el nivel de cada zona en
// zones/<zona>/map.json (se edita con el editor de siempre).

namespace {

// Nombre de carpeta a partir de un nombre: minúsculas, números, _ y -.
std::string slugify(const std::string& name)
{
	std::string out;
	for (unsigned char c : name) {
		if (std::isalnum(c)) out += char(std::tolower(c));
		else if ((c == ' ' || c == '-' || c == '_') && !out.empty() && out.back() != '_') out += '_';
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out.empty() ? std::string("campana") : out.substr(0, 40);
}

const char* const kRuleTypes[] = { "parts", "part", "pikmin", "onion", "zone", "visited", "day", "medal" };

} // namespace

std::string App::campaignsDir() const { return cfg_.gameDir() + "/user/campaigns"; }

bool App::pickPackImage(const char* title, const std::string& as, std::string& field)
{
	const fs::path p = pikmin::launcher::platform::askForFile(title, tr("Imagen PNG"), "*.png");
	if (p.empty()) return false;
	std::error_code ec;
	if (!fs::copy_file(p, campDir_ + "/" + as, fs::copy_options::overwrite_existing, ec)) {
		notify(2, tr("No se pudo copiar la imagen."));
		return false;
	}
	field = as;
	return true;
}

void App::exportPackOf(const std::string& dir)
{
	const fs::path where = pikmin::launcher::platform::askForInstallDirectory(tr("Elige dónde guardar el paquete"));
	if (where.empty()) return;
	const std::string file = (where / (fs::path(dir).filename().string() + ".nectarpack")).string();
	std::string err;
	if (!nectar::exportPack(dir, file, err)) {
		notify(2, tr("No se pudo exportar: ") + err);
		return;
	}
	notify(0, tr("Exportado: ") + file + tr(". En el juego: Custom Maps > Importar (o déjalo en user/import/)."));
}

// Miniatura de la campaña (thumb.png): la imagen del mapa, pequeña, con sus
// zonas y caminos encima.
void App::writeCampaignThumb()
{
	nectar::Image img;
	if (camp_.worldmap == "game") {
		nectar::loadBti(cfg_.dataDir() + "/screen/eng_tex/w_map.bti", img);
	} else {
		int w = 0, h = 0, comp = 0;
		if (unsigned char* px = stbi_load((campDir_ + "/" + camp_.worldmap).c_str(), &w, &h, &comp, 4)) {
			img.resize(w, h);
			std::memcpy(img.rgba.data(), px, std::size_t(w) * h * 4);
			stbi_image_free(px);
		}
	}
	if (img.empty()) return;
	img = nectar::shrinkToFit(img, 320, 240);
	auto dot = [&](int cx, int cy, int r, const std::uint8_t c[4]) {
		for (int y = cy - r; y <= cy + r; y++) {
			for (int x = cx - r; x <= cx + r; x++) {
				if (x < 0 || y < 0 || x >= img.width || y >= img.height || (x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
				std::memcpy(img.at(x, y), c, 4);
			}
		}
	};
	const std::uint8_t path[4] = { 255, 240, 200, 255 }, zone[4] = { 255, 255, 255, 255 }, start[4] = { 120, 220, 90, 255 }, edge[4] = { 20, 20, 20, 255 };
	for (const auto& p : camp_.paths) {
		const int a = camp_.find(p.first), b = camp_.find(p.second);
		if (a < 0 || b < 0) continue;
		const nectar::CampaignZone &za = camp_.zones[size_t(a)], &zb = camp_.zones[size_t(b)];
		for (int k = 0; k <= 24; k++) {
			const double t = k / 24.0;
			dot(int((za.x + (zb.x - za.x) * t) * img.width), int((za.y + (zb.y - za.y) * t) * img.height), 1, path);
		}
	}
	for (size_t i = 0; i < camp_.zones.size(); i++) {
		const int x = int(camp_.zones[i].x * img.width), y = int(camp_.zones[i].y * img.height);
		dot(x, y, 6, edge);
		dot(x, y, 4, int(i) == camp_.startZone() ? start : zone);
	}
	nectar::writePng(campDir_ + "/thumb.png", img);
}

void App::drawCampaignPickers()
{
	ImGui::SeparatorText(tr("Campañas"));
	hint(tr("Varios niveles (zonas) sobre un mapa, con reglas para ir abriéndolos, como la historia del juego."));
	static double sListAt = -10.0;
	if (now() - sListAt > 3.0) {
		sListAt = now();
		campaignList_.clear();
		std::error_code ec;
		for (fs::directory_iterator it(campaignsDir(), ec), end; !ec && it != end; it.increment(ec)) {
			std::string text, err;
			nectar::CampaignDoc d;
			if (!readText((it->path() / "campaign.json").string(), text)) continue;
			const std::string name = nectar::parseCampaignDoc(text, d, err) ? d.name : it->path().filename().string();
			campaignList_.push_back({ name, it->path().string() });
		}
		std::sort(campaignList_.begin(), campaignList_.end());
	}
	if (ImGui::Button(tr("Nueva campaña"), ImVec2(240, 40))) {
		copyStr(campNewName_, sizeof(campNewName_), tr("Mi campaña"));
		openNewCampaign_ = true;
	}
	for (const auto& c : campaignList_) {
		ImGui::SameLine();
		if (ImGui::GetCursorPosX() + 240 > ImGui::GetContentRegionMax().x) ImGui::NewLine();
		if (ImGui::Button((c.first + "##" + c.second).c_str(), ImVec2(240, 40))) openCampaign(c.second);
	}
	if (openNewCampaign_) {
		ImGui::OpenPopup("##newcampaign");
		openNewCampaign_ = false;
	}
	if (ImGui::BeginPopupModal("##newcampaign", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("%s", tr("¿Cómo se llama la campaña?"));
		ImGui::SetNextItemWidth(320);
		ImGui::InputText("##campname", campNewName_, sizeof(campNewName_));
		if (ImGui::Button(tr("Crear"), ImVec2(120, 0)) && campNewName_[0]) {
			newCampaign(campNewName_);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(tr("Cancelar"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void App::newCampaign(const std::string& name)
{
	std::error_code ec;
	std::string id = slugify(name), dir = campaignsDir() + "/" + id;
	for (int n = 2; fs::exists(dir, ec); n++) dir = campaignsDir() + "/" + id + "_" + std::to_string(n);
	camp_      = nectar::CampaignDoc();
	camp_.name = name;
	camp_.zones.clear();
	campDir_ = dir;
	addCampaignZone();
	camp_.start.zone = camp_.zones[0].id;
	saveCampaign();
	openCampaign(dir);
}

void App::openCampaign(const std::string& dir)
{
	std::string text, err;
	nectar::CampaignDoc d;
	if (!readText(dir + "/campaign.json", text) || !nectar::parseCampaignDoc(text, d, err)) {
		notify(2, tr("No se puede abrir la campaña: ") + err);
		return;
	}
	camp_           = d;
	campDir_        = dir;
	campDirty_      = false;
	campZone_       = camp_.zones.empty() ? -1 : 0;
	campLinkFrom_   = -1;
	campCheckDirty_ = true;
	editingZoneOf_.clear();
	screen_ = Screen::Campaign;
}

bool App::saveCampaign()
{
	if (campDir_.empty()) return false;
	if (!writeText(campDir_ + "/campaign.json", nectar::writeCampaignDoc(camp_))) {
		notify(2, tr("No se pudo guardar la campaña."));
		return false;
	}
	campDirty_ = false;
	writeCampaignThumb();
	notify(0, tr("Campaña guardada. Está en el juego: Custom Maps."));
	return true;
}

void App::addCampaignZone()
{
	// Un id libre (y su carpeta).
	int n = int(camp_.zones.size()) + 1;
	std::string id;
	std::error_code ec;
	do {
		id = "zona_" + std::to_string(n++);
	} while (camp_.find(id) >= 0 || fs::exists(campDir_ + "/zones/" + id, ec));
	nectar::CampaignZone z;
	z.id   = id;
	z.name = tr("Zona ") + std::to_string(camp_.zones.size() + 1);
	z.x    = 0.3 + 0.1 * double(camp_.zones.size() % 5);
	z.y    = 0.5;
	// Un nivel nuevo, ya preparado para solo y coop (para explorar).
	MapDoc m = nectar::defaultMapDoc();
	m.name   = z.name;
	m.modes  = nectar::MAPMODE_SOLO | nectar::MAPMODE_COOP;
	nm::ensureStory(m, true);
	m.story.rules.objective = "survive";
	writeText(campDir_ + "/" + z.folder() + "/map.json", nectar::writeMapDoc(m));
	camp_.zones.push_back(z);
	campZone_       = int(camp_.zones.size()) - 1;
	campDirty_      = true;
	campCheckDirty_ = true;
}

void App::removeCampaignZone(int zone)
{
	if (zone < 0 || zone >= int(camp_.zones.size())) return;
	const std::string id = camp_.zones[size_t(zone)].id;
	camp_.zones.erase(camp_.zones.begin() + zone);
	camp_.paths.erase(std::remove_if(camp_.paths.begin(), camp_.paths.end(), [&](const auto& p) { return p.first == id || p.second == id; }),
	                  camp_.paths.end());
	auto clean = [&](std::vector<nectar::UnlockRule>& rules) {
		rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const nectar::UnlockRule& r) { return r.id == id && r.type != "part"; }), rules.end());
	};
	for (nectar::CampaignZone& z : camp_.zones) {
		clean(z.unlock.all);
		clean(z.unlock.any);
	}
	if (camp_.start.zone == id) camp_.start.zone.clear();
	if (camp_.ending.zone == id) camp_.ending.zone.clear();
	campZone_       = std::min(zone, int(camp_.zones.size()) - 1);
	campDirty_      = true;
	campCheckDirty_ = true;
	notify(1, tr("Zona quitada de la campaña (su carpeta sigue en el disco)."));
}

void App::editCampaignZone(int zone)
{
	if (zone < 0 || zone >= int(camp_.zones.size())) return;
	if (campDirty_) saveCampaign();
	const std::string dir = campDir_;
	openMap(campDir_ + "/" + camp_.zones[size_t(zone)].folder() + "/map.json");
	editingZoneOf_ = dir;
	screen_        = Screen::Editor;
}

void App::testCampaignZone(int zone)
{
	if (zone < 0 || zone >= int(camp_.zones.size())) return;
	if (campDirty_ && !saveCampaign()) return;
	checkCampaign();
	if (!campCheck_.empty()) {
		notify(2, tr("Antes de probar: ") + campCheck_);
		return;
	}
	std::string exe = cfg_.gameExe.empty() ? guessGameExe() : cfg_.gameExe;
	std::error_code ec;
	if (exe.empty() || !fs::is_regular_file(exe, ec)) {
		notify(2, tr("No encuentro el juego. Indícalo en Ayuda > Cambiar carpetas."));
		return;
	}
	std::string err;
	std::vector<std::string> args = { "--campaign", fs::absolute(campDir_, ec).string(), "--zone", camp_.zones[size_t(zone)].id };
	if (campTestState_) {
		char state[160];
		std::snprintf(state, sizeof(state), "day=%d;pikmin=%d %d %d;onions=%d %d %d;parts=%d", campTestDay_, campTestPikmin_[0], campTestPikmin_[1],
		              campTestPikmin_[2], int(campTestOnions_[0]), int(campTestOnions_[1]), int(campTestOnions_[2]), campTestParts_);
		args.push_back("--campaign-state");
		args.push_back(state);
	}
	if (!launchGame(exe, cfg_.gameDir(), args, err)) {
		notify(2, err);
		return;
	}
	notify(0, tr("Abriendo el juego en esa zona (partida de prueba: no se guarda)."));
}

void App::checkCampaign()
{
	campCheckDirty_ = false;
	std::vector<std::unique_ptr<nectar::MapData>> maps;
	std::vector<const nectar::MapData*> ptrs;
	for (const nectar::CampaignZone& z : camp_.zones) {
		std::string text, err;
		auto m = std::make_unique<nectar::MapData>();
		if (readText(campDir_ + "/" + z.folder() + "/map.json", text) && nectar::parseMap(text, *m, err)) {
			ptrs.push_back(m.get());
			maps.push_back(std::move(m));
		} else {
			campCheck_ = z.name + ": " + (err.empty() ? tr("no se encuentra su nivel") : err);
			return;
		}
	}
	bool coop = false;
	campCheck_.clear();
	nectar::validateCampaign(camp_, ptrs, coop, campCheck_);
}

unsigned App::campaignMapTexture(int& w, int& h)
{
	const std::string key = camp_.worldmap == "game" ? "game" : campDir_ + "/" + camp_.worldmap;
	if (key != campTexKey_) {
		campTexKey_ = key;
		campTex_    = 0;
		campTexW_ = 4, campTexH_ = 3;
		if (key == "game") {
			nectar::Image img;
			if (nectar::loadBti(cfg_.dataDir() + "/screen/eng_tex/w_map.bti", img) && !img.empty()) {
				campTex_  = uploadTexture(img, false);
				campTexW_ = img.width;
				campTexH_ = img.height;
			}
		} else {
			campTex_ = loadPngTexture(key, &campTexW_, &campTexH_);
		}
	}
	w = campTexW_;
	h = campTexH_;
	return campTex_;
}

void App::drawCampaign()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::Begin("##campaign", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::Button(tr("< Inicio"), ImVec2(110, 34))) {
		if (campDirty_) saveCampaign();
		campDir_.clear();
		screen_ = Screen::Welcome;
		ImGui::End();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button(campDirty_ ? tr("Guardar *") : tr("Guardar"), ImVec2(110, 34))) saveCampaign();
	ImGui::SameLine();
	if (ImGui::Button(tr("Exportar paquete..."), ImVec2(170, 34)) && (!campDirty_ || saveCampaign())) exportPackOf(campDir_);
	ImGui::SetItemTooltip("%s", tr("Un solo archivo .nectarpack con la campaña y sus niveles, para compartirla"));
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.90f, 0.87f, 0.62f, 1), "%s", camp_.name.c_str());
	ImGui::SameLine();
	if (campCheckDirty_) checkCampaign();
	if (campCheck_.empty()) ImGui::TextColored(kGood, "%s", tr("Lista para jugar"));
	else ImGui::TextColored(kBad, "%s %s", tr("Falta:"), campCheck_.c_str());

	const float panelW = 380.0f;
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("##campcanvas", ImVec2(avail.x - panelW - 8, avail.y), ImGuiChildFlags_Borders);
	drawCampaignCanvas(ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail());
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("##camppanel", ImVec2(panelW, avail.y), ImGuiChildFlags_Borders);
	drawCampaignPanel();
	ImGui::EndChild();
	ImGui::End();
	// Toasts y demás.
	for (auto it = toasts_.begin(); it != toasts_.end();) it = it->until < now() ? toasts_.erase(it) : it + 1;
	if (!toasts_.empty()) {
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 20), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::Begin("##camptoasts", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
		for (const Toast& t : toasts_) ImGui::TextColored(t.level == 2 ? kBad : t.level == 1 ? kWarn : kGood, "%s", t.text.c_str());
		ImGui::End();
	}
}

void App::drawCampaignCanvas(const ImVec2& origin, const ImVec2& size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	int iw = 4, ih = 3;
	const unsigned tex = campaignMapTexture(iw, ih);
	// La imagen, entera y centrada.
	float w = size.x, h = size.x * float(ih) / float(std::max(1, iw));
	if (h > size.y) {
		h = size.y;
		w = size.y * float(iw) / float(std::max(1, ih));
	}
	const ImVec2 p0(origin.x + (size.x - w) * 0.5f, origin.y + (size.y - h) * 0.5f), p1(p0.x + w, p0.y + h);
	if (tex) dl->AddImage(ImTextureID(std::intptr_t(tex)), p0, p1);
	else dl->AddRectFilled(p0, p1, IM_COL32(40, 60, 40, 255));
	auto toScreen = [&](const nectar::CampaignZone& z) { return ImVec2(p0.x + float(z.x) * w, p0.y + float(z.y) * h); };

	// Caminos.
	for (const auto& p : camp_.paths) {
		const int a = camp_.find(p.first), b = camp_.find(p.second);
		if (a < 0 || b < 0) continue;
		dl->AddLine(toScreen(camp_.zones[size_t(a)]), toScreen(camp_.zones[size_t(b)]), IM_COL32(255, 240, 200, 220), 3.0f);
	}
	// Zonas.
	const int start = camp_.startZone();
	for (size_t i = 0; i < camp_.zones.size(); i++) {
		const nectar::CampaignZone& z = camp_.zones[i];
		const ImVec2 c                = toScreen(z);
		const ImU32 fill              = int(i) == start ? IM_COL32(120, 220, 90, 255) : z.unlock.empty() ? IM_COL32(240, 240, 240, 255) : IM_COL32(200, 160, 90, 255);
		dl->AddCircleFilled(c, 11.0f, fill);
		dl->AddCircle(c, 11.0f, IM_COL32(20, 20, 20, 255), 0, 2.0f);
		if (int(i) == campZone_) dl->AddCircle(c, 17.0f, IM_COL32(255, 215, 40, 255), 0, 3.0f);
		if (int(i) == campLinkFrom_) dl->AddCircle(c, 22.0f, IM_COL32(120, 200, 255, 255), 0, 2.0f);
		const ImVec2 ts = ImGui::CalcTextSize(z.name.c_str());
		dl->AddRectFilled(ImVec2(c.x - ts.x / 2 - 4, c.y + 14), ImVec2(c.x + ts.x / 2 + 4, c.y + 16 + ts.y), IM_COL32(0, 0, 0, 160), 3);
		dl->AddText(ImVec2(c.x - ts.x / 2, c.y + 15), IM_COL32(255, 255, 255, 255), z.name.c_str());
	}
	// Puntos de interés: rombos azules.
	for (size_t k = 0; k < camp_.pois.size(); k++) {
		const nectar::CampaignPoi& poi = camp_.pois[k];
		const ImVec2 c(p0.x + float(poi.x) * w, p0.y + float(poi.y) * h);
		const ImVec2 q[4] = { ImVec2(c.x, c.y - 9), ImVec2(c.x + 9, c.y), ImVec2(c.x, c.y + 9), ImVec2(c.x - 9, c.y) };
		dl->AddConvexPolyFilled(q, 4, IM_COL32(120, 200, 255, 255));
		if (int(k) == campPoi_) dl->AddCircle(c, 14.0f, IM_COL32(255, 215, 40, 255), 0, 2.0f);
		dl->AddText(ImVec2(c.x + 11, c.y - 7), IM_COL32(200, 235, 255, 255), poi.name.c_str());
	}
	// Ratón: clic elige (o une, en «unir»), arrastrar mueve.
	ImGui::SetCursorScreenPos(origin);
	ImGui::InvisibleButton("##campcanvasbtn", size);
	const ImVec2 m = ImGui::GetIO().MousePos;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
		int hit = -1;
		for (size_t i = 0; i < camp_.zones.size(); i++) {
			const ImVec2 c = toScreen(camp_.zones[i]);
			if ((m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y) <= 16.0f * 16.0f) hit = int(i);
		}
		// Los puntos de interés, después de las zonas (índice zonas + k).
		for (size_t k = 0; hit < 0 && k < camp_.pois.size(); k++) {
			const ImVec2 c(p0.x + float(camp_.pois[k].x) * w, p0.y + float(camp_.pois[k].y) * h);
			if ((m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y) <= 12.0f * 12.0f) hit = int(camp_.zones.size() + k);
		}
		if (hit >= int(camp_.zones.size())) {
			campPoi_     = hit - int(camp_.zones.size());
			campDrag_    = hit;
			campLinkFrom_ = -1;
		} else if (hit >= 0 && campLinkFrom_ >= 0 && hit != campLinkFrom_) {
			// Unir (o separar, si ya estaban unidas).
			const std::string a = camp_.zones[size_t(campLinkFrom_)].id, b = camp_.zones[size_t(hit)].id;
			auto it = std::find_if(camp_.paths.begin(), camp_.paths.end(), [&](const auto& p) {
				return (p.first == a && p.second == b) || (p.first == b && p.second == a);
			});
			if (it != camp_.paths.end()) camp_.paths.erase(it);
			else camp_.paths.push_back({ a, b });
			campLinkFrom_   = -1;
			campDirty_      = true;
			campCheckDirty_ = true;
		} else if (hit >= 0) {
			campZone_ = hit;
			campDrag_ = hit;
		}
		if (hit < 0) campLinkFrom_ = -1;
	}
	if (campDrag_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemActive()) {
		double& x  = campDrag_ < int(camp_.zones.size()) ? camp_.zones[size_t(campDrag_)].x : camp_.pois[size_t(campDrag_) - camp_.zones.size()].x;
		double& y  = campDrag_ < int(camp_.zones.size()) ? camp_.zones[size_t(campDrag_)].y : camp_.pois[size_t(campDrag_) - camp_.zones.size()].y;
		x          = std::clamp(double((m.x - p0.x) / w), 0.0, 1.0);
		y          = std::clamp(double((m.y - p0.y) / h), 0.0, 1.0);
		campDirty_ = true;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) campDrag_ = -1;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", campLinkFrom_ >= 0 ? tr("Haz clic en la zona que quieres unir") : tr("Clic: elegir zona. Arrastrar: moverla."));
}

bool App::drawUnlockList(std::vector<nectar::UnlockRule>& rules, const char* id)
{
	bool ch = false;
	const char* types[] = { tr("Piezas en total"), tr("Una pieza concreta"), tr("Pikmin"), tr("Tener una cebolla"), tr("Zona completada"),
		                    tr("Zona visitada"), tr("Desde el día"), tr("Medalla en una zona") };
	const char* colors[] = { tr("Azul"), tr("Roja"), tr("Amarilla") };
	ImGui::PushID(id);
	int remove = -1;
	for (size_t i = 0; i < rules.size(); i++) {
		nectar::UnlockRule& r = rules[i];
		ImGui::PushID(int(i));
		int type = 0;
		for (int k = 0; k < 8; k++) {
			if (r.type == kRuleTypes[k]) type = k;
		}
		ImGui::SetNextItemWidth(-30);
		if (ImGui::Combo("##type", &type, types, 8)) {
			r      = nectar::UnlockRule();
			r.type = kRuleTypes[type];
			if (r.type == "onion" || r.type == "pikmin") r.color = r.type == "onion" ? 0 : -1;
			if (r.type == "part") r.id = "uf01";
			if ((r.type == "zone" || r.type == "visited" || r.type == "medal") && !camp_.zones.empty()) r.id = camp_.zones[0].id;
			ch = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("x")) remove = int(i);
		const std::string& t = r.type;
		if (t == "parts" || t == "pikmin" || t == "day") {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(tr("Cuántos"), &r.count, 1, t == "pikmin" ? 100 : 30);
		}
		if (t == "pikmin" || t == "onion") {
			int col = t == "pikmin" ? r.color + 1 : r.color;
			const char* pcolors[] = { tr("En total"), tr("Azules"), tr("Rojos"), tr("Amarillos") };
			ImGui::SetNextItemWidth(-90);
			if (t == "pikmin" ? ImGui::Combo(tr("Color"), &col, pcolors, 4) : ImGui::Combo(tr("Color"), &col, colors, 3)) {
				r.color = t == "pikmin" ? col - 1 : col;
				ch      = true;
			}
		}
		if (t == "part") {
			ImGui::SetNextItemWidth(-90);
			if (ImGui::BeginCombo(tr("Pieza"), r.id.c_str())) {
				for (const nectar::ShipPartInfo& sp : nectar::shipParts()) {
					if (ImGui::Selectable(sp.id, r.id == sp.id)) {
						r.id = sp.id;
						ch   = true;
					}
				}
				ImGui::EndCombo();
			}
		}
		if (t == "zone" || t == "visited" || t == "medal") {
			const int z = camp_.find(r.id);
			ImGui::SetNextItemWidth(-90);
			if (ImGui::BeginCombo(tr("Zona"), z >= 0 ? camp_.zones[size_t(z)].name.c_str() : "?")) {
				for (const nectar::CampaignZone& cz : camp_.zones) {
					if (ImGui::Selectable((cz.name + "##" + cz.id).c_str(), r.id == cz.id)) {
						r.id = cz.id;
						ch   = true;
					}
				}
				ImGui::EndCombo();
			}
			if (t == "medal") {
				const char* medals[] = { tr("Bronce"), tr("Plata"), tr("Oro") };
				int md               = std::clamp(r.count, 0, 2);
				ImGui::SetNextItemWidth(-90);
				if (ImGui::Combo(tr("Medalla"), &md, medals, 3)) {
					r.count = md;
					ch      = true;
				}
			}
		}
		ImGui::PopID();
	}
	if (remove >= 0) {
		rules.erase(rules.begin() + remove);
		ch = true;
	}
	if (ImGui::SmallButton(tr("+ regla"))) {
		rules.push_back(nectar::UnlockRule());
		ch = true;
	}
	ImGui::PopID();
	return ch;
}

void App::drawCampaignZone(int zi)
{
	nectar::CampaignZone& z = camp_.zones[size_t(zi)];
	bool ch                 = false;
	char buf[128];
	copyStr(buf, sizeof(buf), z.name);
	ImGui::SetNextItemWidth(-90);
	if (ImGui::InputText(tr("Nombre"), buf, sizeof(buf))) {
		z.name = buf;
		ch     = true;
	}
	copyStr(buf, sizeof(buf), z.nameEn);
	ImGui::SetNextItemWidth(-90);
	if (ImGui::InputText(tr("En inglés"), buf, sizeof(buf))) {
		z.nameEn = buf;
		ch       = true;
	}
	ImGui::TextColored(kDim, tr("Carpeta: zones/%s"), z.id.c_str());
	// Foto: una del juego o la miniatura del nivel.
	static const char* const kSnaps[] = { "", "game:pr_snap", "game:fo_snap", "game:ca_snap", "game:ya_snap", "game:ga_snap" };
	const char* snaps[] = { tr("La miniatura del nivel"), "Impact Site", "Forest of Hope", "Forest Navel", "Distant Spring", "Final Trial" };
	int snap            = 0;
	for (int k = 0; k < 6; k++) {
		if (z.photo == kSnaps[k]) snap = k;
	}
	ImGui::SetNextItemWidth(-90);
	if (ImGui::Combo(tr("Foto"), &snap, snaps, 6)) {
		z.photo = kSnaps[snap];
		ch      = true;
	}
	const char* musics[] = { tr("La del nivel"), "Impact Site", "Forest of Hope", "Forest Navel", "Distant Spring", "Final Trial" };
	int music            = 0;
	for (size_t k = 0; k < nectar::musicNames().size(); k++) {
		if (z.music == nectar::musicNames()[k]) music = int(k) + 1;
	}
	ImGui::SetNextItemWidth(-90);
	if (ImGui::Combo(tr("Música"), &music, musics, 6)) {
		z.music = music ? nectar::musicNames()[size_t(music - 1)] : "";
		ch      = true;
	}
	ImGui::SeparatorText(tr("Se abre cuando..."));
	if (zi == camp_.startZone()) {
		hint(tr("Es la zona de salida: está abierta desde el principio."));
	} else {
		ImGui::TextColored(kDim, "%s", tr("Se cumplen todas estas:"));
		ch |= drawUnlockList(z.unlock.all, "all");
		ImGui::TextColored(kDim, "%s", tr("Y además, alguna de estas (opcional):"));
		ch |= drawUnlockList(z.unlock.any, "any");
		if (z.unlock.empty()) hint(tr("Sin reglas: abierta desde el principio."));
	}
	ImGui::Separator();
	if (wideButton(tr("Editar el nivel de esta zona"))) editCampaignZone(zi);
	if (wideButton(tr("Probar desde esta zona"))) testCampaignZone(zi);
	ImGui::Checkbox(tr("Probar con un estado elegido"), &campTestState_);
	if (campTestState_) {
		ImGui::SetNextItemWidth(-90);
		ImGui::SliderInt(tr("Día"), &campTestDay_, 1, 60);
		const char* onions[] = { tr("Cebolla azul"), tr("Cebolla roja"), tr("Cebolla amarilla") };
		const char* pikmin[] = { tr("Azules"), tr("Rojos"), tr("Amarillos") };
		for (int c = 0; c < 3; c++) {
			ImGui::Checkbox(onions[c], &campTestOnions_[c]);
			ImGui::SameLine(170);
			ImGui::SetNextItemWidth(-90);
			ImGui::SliderInt(pikmin[c], &campTestPikmin_[c], 0, 100);
		}
		ImGui::SetNextItemWidth(-90);
		ImGui::SliderInt(tr("Piezas ya recogidas"), &campTestParts_, 0, 30);
		hint(tr("Las piezas son las primeras de las zonas, en orden. Sirve para probar reglas sin jugar todo."));
	}
	if (wideButton(campLinkFrom_ == zi ? tr("Unir: haz clic en otra zona...") : tr("Unir con otra zona (camino)"))) campLinkFrom_ = campLinkFrom_ == zi ? -1 : zi;
	hint(tr("Unir dos zonas que ya lo están quita el camino."));
	if (ImGui::Button(tr("Quitar de la campaña"), ImVec2(-1, 0))) removeCampaignZone(zi);
	if (ch) {
		campDirty_      = true;
		campCheckDirty_ = true;
	}
}

void App::drawCampaignPanel()
{
	bool ch = false;
	char buf[512];
	if (ImGui::CollapsingHeader(tr("La campaña"), ImGuiTreeNodeFlags_DefaultOpen)) {
		copyStr(buf, sizeof(buf), camp_.name);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Nombre"), buf, 128)) camp_.name = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.author);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Autor"), buf, 128)) camp_.author = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.nameEn);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("En inglés"), buf, 128)) camp_.nameEn = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.intro);
		ImGui::Text("%s", tr("Introducción (al empezar una partida):"));
		if (ImGui::InputTextMultiline("##intro", buf, sizeof(buf), ImVec2(-1, 70))) camp_.intro = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.introEn);
		ImGui::Text("%s", tr("En inglés:"));
		if (ImGui::InputTextMultiline("##introen", buf, sizeof(buf), ImVec2(-1, 50))) camp_.introEn = buf, ch = true;
		ImGui::Text(tr("Imagen de la introducción: %s"), camp_.introImage.empty() ? tr("ninguna") : camp_.introImage.c_str());
		if (ImGui::Button(tr("Elegir...##introimg"))) ch |= pickPackImage(tr("Imagen de la introducción"), "intro.png", camp_.introImage);
		ImGui::SameLine();
		if (ImGui::Button(tr("Quitar##introimg"))) camp_.introImage.clear(), ch = true;
		ImGui::Text("%s", tr("Imagen del mapa:"));
		if (choiceButton(tr("La del juego"), camp_.worldmap == "game", ImVec2(150, 0))) camp_.worldmap = "game", ch = true;
		ImGui::SameLine();
		if (ImGui::Button(tr("Elegir una imagen PNG..."), ImVec2(-1, 0))) {
			const fs::path p = pikmin::launcher::platform::askForFile(tr("Elige la imagen del mapa"), tr("Imagen PNG"), "*.png");
			std::error_code ec;
			if (!p.empty() && fs::copy_file(p, campDir_ + "/worldmap.png", fs::copy_options::overwrite_existing, ec)) {
				camp_.worldmap = "worldmap.png";
				campTexKey_.clear(); // volver a cargarla
				ch = true;
			} else if (!p.empty()) {
				notify(2, tr("No se pudo copiar la imagen."));
			}
		}
	}
	if (ImGui::CollapsingHeader(tr("Al empezar"))) {
		const int start = camp_.startZone();
		ImGui::SetNextItemWidth(-90);
		if (ImGui::BeginCombo(tr("Zona"), start >= 0 ? camp_.zones[size_t(start)].name.c_str() : "?")) {
			for (const nectar::CampaignZone& z : camp_.zones) {
				if (ImGui::Selectable((z.name + "##s" + z.id).c_str(), camp_.start.zone == z.id)) camp_.start.zone = z.id, ch = true;
			}
			ImGui::EndCombo();
		}
		const char* onions[] = { tr("Cebolla azul"), tr("Cebolla roja"), tr("Cebolla amarilla") };
		const char* pikmin[] = { tr("Azules"), tr("Rojos"), tr("Amarillos") };
		for (int c = 0; c < 3; c++) ch |= ImGui::Checkbox(onions[c], &camp_.start.onions[c]);
		ImGui::Text("%s", tr("Pikmin en el grupo el primer día:"));
		for (int c = 0; c < 3; c++) {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(pikmin[c], &camp_.start.pikmin[c], 0, 100);
		}
	}
	if (ImGui::CollapsingHeader(tr("El final"))) {
		const char* types[] = { tr("Recoger piezas"), tr("Completar una zona") };
		int type            = camp_.ending.type == "zone" ? 1 : 0;
		ImGui::SetNextItemWidth(-90);
		if (ImGui::Combo(tr("Se gana al"), &type, types, 2)) camp_.ending.type = type ? "zone" : "parts", ch = true;
		if (type == 0) {
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(tr("Piezas"), &camp_.ending.count, 0, 30, camp_.ending.count ? "%d" : tr("Todas"));
		} else {
			const int z = camp_.find(camp_.ending.zone);
			ImGui::SetNextItemWidth(-90);
			if (ImGui::BeginCombo(tr("Zona"), z >= 0 ? camp_.zones[size_t(z)].name.c_str() : "?")) {
				for (const nectar::CampaignZone& cz : camp_.zones) {
					if (ImGui::Selectable((cz.name + "##e" + cz.id).c_str(), camp_.ending.zone == cz.id)) camp_.ending.zone = cz.id, ch = true;
				}
				ImGui::EndCombo();
			}
		}
		ImGui::SetNextItemWidth(-90);
		ch |= ImGui::SliderInt(tr("Límite de días"), &camp_.ending.dayLimit, 0, 60, camp_.ending.dayLimit ? "%d" : tr("Sin límite"));
		copyStr(buf, sizeof(buf), camp_.ending.goodText);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Final bueno"), buf, 200)) camp_.ending.goodText = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.ending.badText);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Final malo"), buf, 200)) camp_.ending.badText = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.ending.goodTextEn);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Bueno (inglés)"), buf, 200)) camp_.ending.goodTextEn = buf, ch = true;
		copyStr(buf, sizeof(buf), camp_.ending.badTextEn);
		ImGui::SetNextItemWidth(-90);
		if (ImGui::InputText(tr("Malo (inglés)"), buf, 200)) camp_.ending.badTextEn = buf, ch = true;
		ImGui::Text(tr("Imagen del final: %s"), camp_.ending.image.empty() ? tr("ninguna") : camp_.ending.image.c_str());
		if (ImGui::Button(tr("Elegir...##endimg"))) ch |= pickPackImage(tr("Imagen del final"), "ending.png", camp_.ending.image);
		ImGui::SameLine();
		if (ImGui::Button(tr("Quitar##endimg"))) camp_.ending.image.clear(), ch = true;
	}
	// Diario: textos que se leen al anochecer.
	if (ImGui::CollapsingHeader(tr("Diario"))) {
		hint(tr("Al acabar el día se enseña la primera entrada sin leer de ese día (o de cualquiera) y de esa zona (o de cualquiera)."));
		int remove = -1;
		for (size_t k = 0; k < camp_.diary.size(); k++) {
			nectar::CampaignDiary& e = camp_.diary[k];
			ImGui::PushID(int(k) + 5000);
			ImGui::Separator();
			ImGui::SetNextItemWidth(-90);
			ch |= ImGui::SliderInt(tr("Día"), &e.day, 0, 60, e.day ? "%d" : tr("cualquiera"));
			const int z = camp_.find(e.zone);
			ImGui::SetNextItemWidth(-90);
			if (ImGui::BeginCombo(tr("Zona"), z >= 0 ? camp_.zones[size_t(z)].name.c_str() : tr("cualquiera"))) {
				if (ImGui::Selectable(tr("cualquiera"), e.zone.empty())) e.zone.clear(), ch = true;
				for (const nectar::CampaignZone& cz : camp_.zones) {
					if (ImGui::Selectable((cz.name + "##d" + cz.id).c_str(), e.zone == cz.id)) e.zone = cz.id, ch = true;
				}
				ImGui::EndCombo();
			}
			copyStr(buf, sizeof(buf), e.text);
			if (ImGui::InputTextMultiline("##dtext", buf, sizeof(buf), ImVec2(-1, 50))) e.text = buf, ch = true;
			copyStr(buf, sizeof(buf), e.textEn);
			ImGui::SetNextItemWidth(-90);
			if (ImGui::InputText(tr("En inglés"), buf, sizeof(buf))) e.textEn = buf, ch = true;
			if (ImGui::SmallButton(tr("Quitar entrada"))) remove = int(k);
			ImGui::PopID();
		}
		if (remove >= 0) camp_.diary.erase(camp_.diary.begin() + remove), ch = true;
		if (camp_.diary.size() < 200 && ImGui::Button(tr("Añadir entrada"), ImVec2(-1, 0))) {
			nectar::CampaignDiary e;
			e.day  = int(camp_.diary.size()) + 1;
			e.text = tr("Hoy...");
			camp_.diary.push_back(e);
			ch = true;
		}
	}
	// Puntos de interés del mapa.
	if (ImGui::CollapsingHeader(tr("Puntos de interés"))) {
		hint(tr("Marcas en el mapa de zonas que solo enseñan un texto (pistas, historia). Se mueven arrastrándolas."));
		int remove = -1;
		for (size_t k = 0; k < camp_.pois.size(); k++) {
			nectar::CampaignPoi& poi = camp_.pois[k];
			ImGui::PushID(int(k) + 7000);
			ImGui::Separator();
			if (ImGui::Selectable(poi.name.empty() ? "?" : poi.name.c_str(), int(k) == campPoi_)) campPoi_ = int(k);
			if (int(k) == campPoi_) {
				copyStr(buf, sizeof(buf), poi.name);
				ImGui::SetNextItemWidth(-90);
				if (ImGui::InputText(tr("Nombre"), buf, 128)) poi.name = buf, ch = true;
				copyStr(buf, sizeof(buf), poi.nameEn);
				ImGui::SetNextItemWidth(-90);
				if (ImGui::InputText(tr("En inglés"), buf, 128)) poi.nameEn = buf, ch = true;
				copyStr(buf, sizeof(buf), poi.text);
				if (ImGui::InputTextMultiline("##ptext", buf, sizeof(buf), ImVec2(-1, 50))) poi.text = buf, ch = true;
				copyStr(buf, sizeof(buf), poi.textEn);
				ImGui::SetNextItemWidth(-90);
				if (ImGui::InputText(tr("Texto en inglés"), buf, sizeof(buf))) poi.textEn = buf, ch = true;
				if (ImGui::SmallButton(tr("Quitar punto"))) remove = int(k);
			}
			ImGui::PopID();
		}
		if (remove >= 0) {
			camp_.pois.erase(camp_.pois.begin() + remove);
			campPoi_ = -1;
			ch       = true;
		}
		if (camp_.pois.size() < 32 && ImGui::Button(tr("Añadir punto de interés"), ImVec2(-1, 0))) {
			nectar::CampaignPoi poi;
			poi.name = tr("Punto");
			poi.x    = 0.5 + 0.05 * double(camp_.pois.size() % 6);
			camp_.pois.push_back(poi);
			campPoi_ = int(camp_.pois.size()) - 1;
			ch       = true;
		}
	}
	ImGui::SeparatorText(tr("Zonas"));
	for (size_t i = 0; i < camp_.zones.size(); i++) {
		if (ImGui::Selectable((camp_.zones[i].name + "##z" + camp_.zones[i].id).c_str(), int(i) == campZone_)) campZone_ = int(i);
	}
	if (camp_.zones.size() < 30 && wideButton(tr("Añadir zona"))) addCampaignZone();
	if (campZone_ >= 0 && campZone_ < int(camp_.zones.size())) {
		ImGui::SeparatorText(camp_.zones[size_t(campZone_)].name.c_str());
		drawCampaignZone(campZone_);
	}
	if (ch) {
		campDirty_      = true;
		campCheckDirty_ = true;
	}
}

} // namespace nm
