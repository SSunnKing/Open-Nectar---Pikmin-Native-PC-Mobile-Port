#include "nectar_assets/catalog.h"

#include "nectar_assets/gen.h"
#include "nectar_assets/image.h"
#include "nectar_assets/ini.h"
#include "nectar_assets/raster.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

namespace nectar {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

// Sube si cambia lo que se guarda: la caché vieja se rehace entera.
constexpr int kIndexVersion = 3; // 2: matrices de GX arrastradas entre grupos (Mario); 3: color del TEV en las miniaturas

constexpr int kThumbSize    = 128;

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// FNV-1a: nombre estable de la miniatura a partir de la ruta.
std::string thumbName(const std::string& rel)
{
	std::uint64_t h = 1469598103934665603ull;
	for (unsigned char c : rel) {
		h ^= c;
		h *= 1099511628211ull;
	}
	char buf[24];
	snprintf(buf, sizeof(buf), "%016llx.png", static_cast<unsigned long long>(h));
	return buf;
}

const char* kindName(AssetKind k)
{
	switch (k) {
	case AssetKind::Model: return "model";
	case AssetKind::Texture: return "texture";
	case AssetKind::Generators: return "generators";
	default: return "scene";
	}
}

AssetKind kindFrom(const std::string& s)
{
	if (s == "texture") return AssetKind::Texture;
	if (s == "generators") return AssetKind::Generators;
	if (s == "scene") return AssetKind::Scene;
	return AssetKind::Model;
}

json toJson(const CatalogEntry& e)
{
	json j = { { "path", e.path }, { "kind", kindName(e.kind) }, { "category", e.category }, { "size", e.fileSize }, { "mtime", e.mtime } };
	if (!e.thumb.empty()) j["thumb"] = fs::path(e.thumb).filename().string();
	if (!e.error.empty()) j["error"] = e.error;
	switch (e.kind) {
	case AssetKind::Model:
		j["triangles"]     = e.triangles;
		j["vertices"]      = e.vertices;
		j["textures"]      = e.textures;
		j["joints"]        = e.joints;
		j["collisionTris"] = e.collisionTris;
		j["bounds"]        = { e.boundsMin.x, e.boundsMin.y, e.boundsMin.z, e.boundsMax.x, e.boundsMax.y, e.boundsMax.z };
		break;
	case AssetKind::Texture:
		j["width"]  = e.width;
		j["height"] = e.height;
		break;
	case AssetKind::Generators: j["generators"] = e.generators; break;
	case AssetKind::Scene: j["mapFile"] = e.mapFile; break;
	}
	return j;
}

CatalogEntry fromJson(const json& j, const std::string& thumbsDir)
{
	CatalogEntry e;
	e.path          = j.value("path", "");
	e.kind          = kindFrom(j.value("kind", "model"));
	e.category      = j.value("category", "other");
	e.fileSize      = j.value("size", std::uint64_t(0));
	e.mtime         = j.value("mtime", std::int64_t(0));
	e.error         = j.value("error", "");
	e.triangles     = j.value("triangles", 0);
	e.vertices      = j.value("vertices", 0);
	e.textures      = j.value("textures", 0);
	e.joints        = j.value("joints", 0);
	e.collisionTris = j.value("collisionTris", 0);
	e.width         = j.value("width", 0);
	e.height        = j.value("height", 0);
	e.generators    = j.value("generators", 0);
	e.mapFile       = j.value("mapFile", "");
	if (j.contains("bounds") && j["bounds"].size() == 6) {
		const json& b = j["bounds"];
		e.boundsMin   = { b[0].get<float>(), b[1].get<float>(), b[2].get<float>() };
		e.boundsMax   = { b[3].get<float>(), b[4].get<float>(), b[5].get<float>() };
	}
	if (j.contains("thumb")) e.thumb = (fs::path(thumbsDir) / j["thumb"].get<std::string>()).string();
	return e;
}

// Lee el asset y hace su miniatura.
void describe(const fs::path& file, const fs::path& thumbsDir, CatalogEntry& e)
{
	std::string err;
	const fs::path thumb = thumbsDir / thumbName(e.path);
	switch (e.kind) {
	case AssetKind::Model: {
		Model m;
		if (!loadMod(file.string(), m, err)) {
			e.error = err;
			return;
		}
		e.triangles     = int(m.triangleCount());
		e.vertices      = int(m.positions.size());
		e.textures      = int(m.textures.size());
		e.joints        = int(m.joints.size());
		e.collisionTris = int(m.collision.size());
		if (!m.positions.empty()) {
			e.boundsMin = e.boundsMax = m.positions[0];
			for (const Vec3& p : m.positions) {
				e.boundsMin = { std::min(e.boundsMin.x, p.x), std::min(e.boundsMin.y, p.y), std::min(e.boundsMin.z, p.z) };
				e.boundsMax = { std::max(e.boundsMax.x, p.x), std::max(e.boundsMax.y, p.y), std::max(e.boundsMax.z, p.z) };
			}
		}
		RenderOptions opt;
		opt.size = kThumbSize;
		if (e.category == "terrain") opt.pitch = 60.0f; // los mapas se ven mejor desde arriba
		const Image img = renderModel(m, opt);
		if (!img.empty() && writePng(thumb.string(), img)) e.thumb = thumb.string();
		break;
	}
	case AssetKind::Texture: {
		Image img;
		if (!loadBti(file.string(), img)) {
			e.error = "textura que no se puede decodificar (¿paletizada?)";
			return;
		}
		e.width  = img.width;
		e.height = img.height;
		if (writePng(thumb.string(), shrinkToFit(img, kThumbSize, kThumbSize))) e.thumb = thumb.string();
		break;
	}
	case AssetKind::Generators: {
		GenFile g;
		if (!loadGen(file.string(), g, err)) {
			e.error = err;
			return;
		}
		e.generators = int(g.generators.size());
		break;
	}
	case AssetKind::Scene: {
		Scene s;
		if (!loadScene(file.string(), s, err)) {
			e.error = err;
			return;
		}
		e.mapFile = s.mapFile;
		break;
	}
	}
}

bool kindOf(const fs::path& p, const std::string& rel, AssetKind& kind)
{
	const std::string ext = lower(p.extension().string());
	if (ext == ".mod") kind = AssetKind::Model;
	else if (ext == ".bti") kind = AssetKind::Texture;
	else if (ext == ".gen") kind = AssetKind::Generators;
	else if (ext == ".ini" && startsWith(rel, "stages/") && rel != "stages/stages.ini") kind = AssetKind::Scene;
	else return false;
	return true;
}

} // namespace

std::string defaultCacheDir()
{
#ifdef _WIN32
	if (const char* d = std::getenv("LOCALAPPDATA")) return (fs::path(d) / "nectar_mapmaker").string();
#else
	if (const char* d = std::getenv("XDG_CACHE_HOME")) {
		if (*d) return (fs::path(d) / "nectar_mapmaker").string();
	}
	if (const char* h = std::getenv("HOME")) return (fs::path(h) / ".cache" / "nectar_mapmaker").string();
#endif
	return "nectar_mapmaker_cache";
}

std::string assetCategory(const std::string& relPath, AssetKind kind)
{
	const std::string p = lower(relPath);
	if (kind == AssetKind::Generators || kind == AssetKind::Scene) return "stages";
	if (kind == AssetKind::Texture) {
		if (startsWith(p, "screen/") || startsWith(p, "2d_data/")) return "ui";
		if (startsWith(p, "effects/")) return "effects";
		return "textures";
	}
	if (startsWith(p, "courses/")) return "terrain";
	if (startsWith(p, "objects/ufoparts/")) return "ship_parts";
	if (startsWith(p, "objects/pellets/")) return "pellets";
	if (startsWith(p, "objects/plants/") || startsWith(p, "objects/weeds/")) return "plants";
	if (startsWith(p, "objects/")) return "objects";
	if (startsWith(p, "tekis/") || startsWith(p, "bosses/")) return "enemies";
	if (startsWith(p, "pikis/")) return "pikmin";
	if (startsWith(p, "effects/")) return "effects";
	return "other";
}

bool loadCatalog(const std::string& cacheDir, Catalog& out, std::string& error)
{
	out = Catalog();
	out.cacheDir = cacheDir;
	std::ifstream f(fs::path(cacheDir) / "index.json");
	if (!f) {
		error = "no hay índice en " + cacheDir;
		return false;
	}
	try {
		const json root = json::parse(f);
		if (root.value("version", 0) != kIndexVersion) {
			error = "índice de otra versión";
			return false;
		}
		out.dataDir                = root.value("dataDir", "");
		const std::string thumbsDir = (fs::path(cacheDir) / "thumbs").string();
		for (const json& j : root.at("entries")) out.entries.push_back(fromJson(j, thumbsDir));
	} catch (const std::exception& e) {
		error = std::string("índice roto: ") + e.what();
		return false;
	}
	return true;
}

namespace {

bool buildCatalogUnchecked(const std::string& dataDir, const std::string& cacheDir, Catalog& out, std::string& error,
                           const CatalogProgress& progress)
{
	std::error_code ec;
	const fs::path root(dataDir);
	if (!fs::is_directory(root, ec)) {
		error = "no existe la carpeta " + dataDir;
		return false;
	}
	const fs::path thumbs = fs::path(cacheDir) / "thumbs";
	fs::create_directories(thumbs, ec);
	if (ec) {
		error = "no se puede crear " + thumbs.string() + ": " + ec.message();
		return false;
	}

	// Lo que ya estaba: se reutiliza si el archivo no ha cambiado.
	std::map<std::string, CatalogEntry> previous;
	{
		Catalog old;
		std::string ignored;
		if (loadCatalog(cacheDir, old, ignored) && fs::path(old.dataDir) == root) {
			for (CatalogEntry& e : old.entries) previous[e.path] = e;
		}
	}

	std::vector<std::pair<fs::path, CatalogEntry>> todo;
	for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec)) continue;
		CatalogEntry e;
		e.path = fs::relative(it->path(), root, ec).generic_string();
		if (!kindOf(it->path(), e.path, e.kind)) continue;
		e.category = assetCategory(e.path, e.kind);
		e.fileSize = it->file_size(ec);
		e.mtime    = std::int64_t(it->last_write_time(ec).time_since_epoch().count());
		todo.push_back({ it->path(), e });
	}
	std::sort(todo.begin(), todo.end(), [](const auto& a, const auto& b) { return a.second.path < b.second.path; });

	out          = Catalog();
	out.dataDir  = root.string();
	out.cacheDir = cacheDir;
	for (std::size_t i = 0; i < todo.size(); i++) {
		CatalogEntry& e = todo[i].second;
		if (progress) progress(i, todo.size(), e.path);
		auto old = previous.find(e.path);
		if (old != previous.end() && old->second.fileSize == e.fileSize && old->second.mtime == e.mtime
		    && (old->second.thumb.empty() || fs::exists(old->second.thumb, ec))) {
			out.entries.push_back(old->second);
			out.reused++;
			continue;
		}
		describe(todo[i].first, thumbs, e);
		out.entries.push_back(e);
		out.rebuilt++;
	}
	if (progress) progress(todo.size(), todo.size(), "");

	json entries = json::array();
	for (const CatalogEntry& e : out.entries) entries.push_back(toJson(e));
	const json index = { { "version", kIndexVersion }, { "dataDir", out.dataDir }, { "entries", entries } };
	const fs::path tmp = fs::path(cacheDir) / "index.json.tmp";
	{
		std::ofstream f(tmp);
		if (!f) {
			error = "no se puede escribir " + tmp.string();
			return false;
		}
		// Rutas y mensajes pueden traer bytes que no son UTF-8 (nombres de
		// archivo en Linux, #326): se guardan con U+FFFD en vez de abortar.
		f << index.dump(1, ' ', false, json::error_handler_t::replace);
	}
	fs::rename(tmp, fs::path(cacheDir) / "index.json", ec);
	if (ec) {
		error = "no se puede guardar el índice: " + ec.message();
		return false;
	}
	return true;
}

} // namespace

bool buildCatalog(const std::string& dataDir, const std::string& cacheDir, Catalog& out, std::string& error, const CatalogProgress& progress)
{
	// Nada de lo que encuentre en el disco debe tumbar al editor (#326): un
	// fallo inesperado se devuelve como error, igual que los demás.
	try {
		return buildCatalogUnchecked(dataDir, cacheDir, out, error, progress);
	} catch (const std::exception& e) {
		error = std::string("no se pudo preparar el catálogo: ") + e.what();
	} catch (...) {
		error = "no se pudo preparar el catálogo";
	}
	return false;
}

} // namespace nectar
