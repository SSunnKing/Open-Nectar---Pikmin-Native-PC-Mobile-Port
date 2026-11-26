#include "nectar_assets/charpack.h"

#include "stb_image.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace nectar {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

constexpr std::size_t kMaxEntry = 256u * 1024u * 1024u; // un pack nunca pasa de esto

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
	static std::uint32_t table[256];
	static bool ready = false;
	if (!ready) {
		for (std::uint32_t i = 0; i < 256; ++i) {
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		ready = true;
	}
	std::uint32_t c = 0xFFFFFFFFu;
	for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

void put16(std::vector<std::uint8_t>& o, std::uint32_t v)
{
	o.push_back(std::uint8_t(v));
	o.push_back(std::uint8_t(v >> 8));
}
void put32(std::vector<std::uint8_t>& o, std::uint32_t v)
{
	put16(o, v & 0xFFFF);
	put16(o, v >> 16);
}
std::uint32_t get16(const std::vector<std::uint8_t>& d, std::size_t at) { return at + 2 <= d.size() ? std::uint32_t(d[at] | d[at + 1] << 8) : 0; }
std::uint32_t get32(const std::vector<std::uint8_t>& d, std::size_t at) { return get16(d, at) | get16(d, at + 2) << 16; }

bool readFile(const fs::path& path, std::vector<std::uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), {});
	return true;
}

bool writeFile(const fs::path& path, const std::vector<std::uint8_t>& data, std::string& error)
{
	// Aparte y luego con su nombre: nadie lee uno a medias.
	const fs::path tmp = path.string() + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f || !f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()))) {
			error = "cannot write " + path.string();
			return false;
		}
	}
	std::error_code ec;
	fs::rename(tmp, path, ec);
	if (ec) {
		fs::remove(path, ec);
		fs::rename(tmp, path, ec);
	}
	if (ec) {
		error = ec.message();
		return false;
	}
	return true;
}

std::string infoJson(const CharacterInfo& info)
{
	json j;
	j["format"]   = info.format;
	j["id"]       = info.id;
	j["name"]     = info.name;
	j["author"]   = info.author;
	j["target"]   = info.target;
	j["skeleton"] = info.skeleton;
	return j.dump(1);
}

bool parseInfo(const std::string& text, CharacterInfo& info, std::string& error)
{
	try {
		const json j  = json::parse(text);
		info.format   = j.value("format", 0);
		info.id       = j.value("id", "");
		info.name     = j.value("name", "");
		info.author   = j.value("author", "");
		info.target   = j.value("target", kTargetCaptain);
		info.skeleton = j.value("skeleton", "olimar");
	} catch (const std::exception& e) {
		error = std::string("character.json: ") + e.what();
		return false;
	}
	if (info.format < 1 || info.format > kCharacterFormat) {
		error = "character.json: format " + std::to_string(info.format) + " is newer than this version";
		return false;
	}
	const bool pikmin = info.target == kTargetPikminRed || info.target == kTargetPikminYellow || info.target == kTargetPikminBlue
	                 || info.target == kTargetPikminPurple || info.target == kTargetPikminWhite || info.target == kTargetPikminBulbmin
	                 || info.target == kTargetPikminAll;
	if (info.target != kTargetCaptain && !pikmin) {
		error = "character.json: unknown target '" + info.target + "'";
		return false;
	}
	if (info.skeleton != (pikmin ? "pikmin" : "olimar")) {
		error = "character.json: skeleton '" + info.skeleton + "' does not match '" + info.target + "'";
		return false;
	}
	if (info.name.empty()) info.name = "Custom";
	if (info.name.size() > 40) info.name.resize(40);
	return true;
}

// Solo letras, números y guiones: es el nombre de una carpeta.
bool safeId(const std::string& id)
{
	if (id.empty() || id.size() > 64) return false;
	for (char c : id)
		if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
	return true;
}

} // namespace

std::string characterId(const std::string& name, const std::vector<std::uint8_t>& nhm)
{
	std::string id;
	for (std::size_t i = 0; i < name.size() && id.size() < 32; ++i) {
		unsigned char c = static_cast<unsigned char>(name[i]);
		// Vocales con tilde y la ñ (UTF-8 de 2 bytes que empiezan por 0xC3).
		if (c == 0xC3 && i + 1 < name.size()) {
			const unsigned char n = static_cast<unsigned char>(name[++i]);
			const char* map       = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
			c                     = static_cast<unsigned char>((n >= 0xA0 && n <= 0xBF) ? map[n - 0xA0] : (n >= 0x80 && n <= 0x9F) ? map[n - 0x80] : '-');
		}
		if (std::isalnum(c)) id += char(std::tolower(c));
		else if (!id.empty() && id.back() != '-') id += '-';
	}
	while (!id.empty() && id.back() == '-') id.pop_back();
	if (id.empty()) id = "character";
	char hash[16];
	std::snprintf(hash, sizeof(hash), "-%04x", unsigned(crc32(nhm.data(), nhm.size()) & 0xFFFF));
	return id + hash;
}

bool validNhm(const std::vector<std::uint8_t>& nhm, std::string& error)
{
	if (nhm.size() < 16 || std::memcmp(nhm.data(), "NHM1", 4) != 0) {
		error = "model.nhm is not a model pack";
		return false;
	}
	const std::uint32_t version = get32(nhm, 4), bones = get32(nhm, 8), parts = get32(nhm, 12);
	if (version < 1 || version > 3 || bones == 0 || bones > 64 || parts == 0 || parts > 8) {
		error = "model.nhm: unsupported version or layout";
		return false;
	}
	return true;
}

bool writeCharacterPack(const std::string& path, const CharacterFiles& files, std::string& error)
{
	if (!validNhm(files.nhm, error)) return false;
	const std::string meta = infoJson(files.info);
	struct Entry {
		const char* name;
		const std::uint8_t* data;
		std::size_t size;
	};
	std::vector<Entry> entries = { { "character.json", reinterpret_cast<const std::uint8_t*>(meta.data()), meta.size() },
		                           { "model.nhm", files.nhm.data(), files.nhm.size() } };
	if (!files.portrait.empty()) entries.push_back({ "portrait.png", files.portrait.data(), files.portrait.size() });
	std::vector<std::string> projectNames;
	projectNames.reserve(files.project.size());
	for (const auto& [path, data] : files.project) {
		if (!safeProjectPath(path)) continue;
		projectNames.push_back("project/" + path);
		entries.push_back({ projectNames.back().c_str(), data.data(), data.size() });
	}

	// Zip sin comprimir (el .nhm ya es binario; así se lee sin zlib).
	std::vector<std::uint8_t> out, central;
	for (const Entry& e : entries) {
		const std::uint32_t crc = crc32(e.data, e.size), offset = std::uint32_t(out.size());
		const std::uint32_t nameLen = std::uint32_t(std::strlen(e.name));
		put32(out, 0x04034b50);
		put16(out, 20), put16(out, 0), put16(out, 0), put16(out, 0), put16(out, 0x21);
		put32(out, crc), put32(out, std::uint32_t(e.size)), put32(out, std::uint32_t(e.size));
		put16(out, nameLen), put16(out, 0);
		out.insert(out.end(), e.name, e.name + nameLen);
		out.insert(out.end(), e.data, e.data + e.size);
		put32(central, 0x02014b50);
		put16(central, 20), put16(central, 20), put16(central, 0), put16(central, 0), put16(central, 0), put16(central, 0x21);
		put32(central, crc), put32(central, std::uint32_t(e.size)), put32(central, std::uint32_t(e.size));
		put16(central, nameLen), put16(central, 0), put16(central, 0), put16(central, 0), put16(central, 0);
		put32(central, 0), put32(central, offset);
		central.insert(central.end(), e.name, e.name + nameLen);
	}
	const std::uint32_t centralAt = std::uint32_t(out.size());
	out.insert(out.end(), central.begin(), central.end());
	put32(out, 0x06054b50);
	put16(out, 0), put16(out, 0), put16(out, std::uint32_t(entries.size())), put16(out, std::uint32_t(entries.size()));
	put32(out, std::uint32_t(central.size())), put32(out, centralAt), put16(out, 0);
	return writeFile(fs::u8path(path), out, error);
}

bool readCharacterPack(const std::string& path, CharacterFiles& files, std::string& error)
{
	std::vector<std::uint8_t> d;
	if (!readFile(fs::u8path(path), d)) {
		error = "cannot read " + path;
		return false;
	}
	// Fin del directorio central (puede llevar un comentario detrás).
	std::size_t eocd = std::string::npos;
	if (d.size() >= 22) {
		const std::size_t lowest = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
		for (std::size_t i = d.size() - 22 + 1; i-- > lowest;) {
			if (get32(d, i) == 0x06054b50) {
				eocd = i;
				break;
			}
		}
	}
	if (eocd == std::string::npos) {
		error = "not a character pack (.nectarchar)";
		return false;
	}
	const std::uint32_t count = get16(d, eocd + 10);
	std::size_t at            = get32(d, eocd + 16);
	std::string meta;
	files = CharacterFiles();
	for (std::uint32_t e = 0; e < count; ++e) {
		if (get32(d, at) != 0x02014b50) {
			error = "damaged character pack";
			return false;
		}
		const std::uint32_t method = get16(d, at + 10), crc = get32(d, at + 16), csize = get32(d, at + 20), usize = get32(d, at + 24);
		const std::uint32_t nameLen = get16(d, at + 28), extraLen = get16(d, at + 30), commentLen = get16(d, at + 32);
		const std::uint32_t local   = get32(d, at + 42);
		if (at + 46 + nameLen > d.size()) break;
		std::string name(reinterpret_cast<const char*>(&d[at + 46]), nameLen);
		at += 46 + nameLen + extraLen + commentLen;
		// El proyecto (para volver a editarlo) va bajo project/.
		std::string projectPath;
		const std::size_t proj = name.rfind("project/");
		if (proj != std::string::npos && (proj == 0 || name[proj - 1] == '/')) projectPath = name.substr(proj + 8);
		// Dentro de una carpeta si alguien lo volvió a comprimir a mano.
		const std::size_t slash = name.find_last_of('/');
		if (projectPath.empty() && slash != std::string::npos) name = name.substr(slash + 1);
		if (!projectPath.empty() && (!safeProjectPath(projectPath) || projectPath.back() == '/')) continue;
		if (projectPath.empty() && name != "character.json" && name != "model.nhm" && name != "portrait.png") continue;
		if (get32(d, local) != 0x04034b50 || usize > kMaxEntry) {
			error = "damaged character pack";
			return false;
		}
		const std::size_t start = local + 30 + get16(d, local + 26) + get16(d, local + 28);
		if (start + csize > d.size()) {
			error = "damaged character pack";
			return false;
		}
		std::vector<std::uint8_t> data;
		if (method == 0) {
			data.assign(d.begin() + std::ptrdiff_t(start), d.begin() + std::ptrdiff_t(start + csize));
		} else if (method == 8) {
			// Deflate: con el inflador de stb_image (el que usan los PNG).
			data.resize(usize);
			const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(data.data()), int(usize),
			                                                 reinterpret_cast<const char*>(&d[start]), int(csize));
			if (got != int(usize)) {
				error = "damaged character pack";
				return false;
			}
		} else {
			error = "unsupported compression in the character pack";
			return false;
		}
		if (crc32(data.data(), data.size()) != crc) {
			error = "damaged character pack (" + name + ")";
			return false;
		}
		if (!projectPath.empty()) files.project.push_back({ projectPath, std::move(data) });
		else if (name == "character.json") meta.assign(data.begin(), data.end());
		else if (name == "model.nhm") files.nhm = std::move(data);
		else files.portrait = std::move(data);
	}
	if (meta.empty() || files.nhm.empty()) {
		error = "the pack has no character.json or model.nhm";
		return false;
	}
	if (!parseInfo(meta, files.info, error) || !validNhm(files.nhm, error)) return false;
	if (!safeId(files.info.id)) files.info.id = characterId(files.info.name, files.nhm);
	return true;
}

std::string charactersDir(const std::string& gameDir) { return (fs::u8path(gameDir) / "Load" / "Characters").u8string(); }
std::string pikminModelsDir(const std::string& gameDir) { return (fs::u8path(gameDir) / "Load" / "Pikmin").u8string(); }

bool installCharacter(const std::string& gameDir, const CharacterFiles& files, std::string& installedDir, std::string& error)
{
	if (!validNhm(files.nhm, error)) return false;
	CharacterInfo info = files.info;
	if (!safeId(info.id)) info.id = characterId(info.name, files.nhm);
	const fs::path dir = fs::u8path(info.isPikmin() ? pikminModelsDir(gameDir) : charactersDir(gameDir)) / info.id;
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec) {
		error = ec.message();
		return false;
	}
	const std::string meta = infoJson(info);
	if (!writeFile(dir / "model.nhm", files.nhm, error)) return false;
	if (!files.portrait.empty() && !writeFile(dir / "portrait.png", files.portrait, error)) return false;
	if (files.portrait.empty()) fs::remove(dir / "portrait.png", ec);
	// El proyecto entero de nuevo (o fuera, si este no trae).
	fs::remove_all(dir / "project", ec);
	for (const auto& [path, data] : files.project) {
		if (!safeProjectPath(path)) continue;
		const fs::path target = dir / "project" / fs::u8path(path);
		fs::create_directories(target.parent_path(), ec);
		if (!writeFile(target, data, error)) return false;
	}
	// El json el último: sin él el juego no ve la carpeta.
	if (!writeFile(dir / "character.json", std::vector<std::uint8_t>(meta.begin(), meta.end()), error)) return false;
	installedDir = dir.u8string();
	return true;
}

bool safeProjectPath(const std::string& path)
{
	if (path.empty() || path.size() > 240 || path[0] == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
	std::size_t at = 0;
	while (at <= path.size()) {
		const std::size_t next = path.find('/', at);
		const std::string part = path.substr(at, next == std::string::npos ? std::string::npos : next - at);
		if (part == ".." || part == ".") return false;
		if (next == std::string::npos) break;
		at = next + 1;
	}
	return true;
}

bool readProjectFolder(const std::string& characterDir, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out)
{
	out.clear();
	const fs::path root = fs::u8path(characterDir) / "project";
	std::error_code ec;
	if (!fs::is_directory(root, ec)) return false;
	for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
		if (!it->is_regular_file(ec)) continue;
		std::string rel = fs::relative(it->path(), root, ec).generic_u8string();
		if (ec || !safeProjectPath(rel)) continue;
		std::vector<std::uint8_t> data;
		if (readFile(it->path(), data)) out.push_back({ rel, std::move(data) });
	}
	return !out.empty();
}

bool installCharacterPack(const std::string& gameDir, const std::string& packPath, CharacterInfo& info, std::string& error)
{
	CharacterFiles files;
	if (!readCharacterPack(packPath, files, error)) return false;
	std::string dir;
	if (!installCharacter(gameDir, files, dir, error)) return false;
	info     = files.info;
	info.dir = dir;
	return true;
}

bool readInstalledCharacter(const std::string& characterDir, CharacterFiles& files, std::string& error)
{
	files               = CharacterFiles();
	const fs::path dir  = fs::u8path(characterDir);
	std::vector<std::uint8_t> meta;
	if (!readFile(dir / "character.json", meta)) {
		error = "character.json is missing";
		return false;
	}
	if (!parseInfo(std::string(meta.begin(), meta.end()), files.info, error)) return false;
	files.info.id = dir.filename().u8string();
	if (!readFile(dir / "model.nhm", files.nhm) || !validNhm(files.nhm, error)) {
		if (error.empty()) error = "model.nhm is missing";
		return false;
	}
	readFile(dir / "portrait.png", files.portrait); // puede no tener
	readProjectFolder(characterDir, files.project);
	return true;
}

namespace {

std::vector<CharacterInfo> listIn(const std::string& root, bool pikmin)
{
	std::vector<CharacterInfo> out;
	std::error_code ec;
	for (const fs::directory_entry& e : fs::directory_iterator(fs::u8path(root), ec)) {
		if (!e.is_directory(ec)) continue;
		std::vector<std::uint8_t> meta;
		if (!readFile(e.path() / "character.json", meta) || !fs::is_regular_file(e.path() / "model.nhm", ec)) continue;
		CharacterInfo info;
		std::string error;
		if (!parseInfo(std::string(meta.begin(), meta.end()), info, error) || info.isPikmin() != pikmin) continue;
		info.id  = e.path().filename().u8string();
		info.dir = e.path().u8string();
		out.push_back(info);
	}
	std::sort(out.begin(), out.end(), [](const CharacterInfo& a, const CharacterInfo& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
	return out;
}

} // namespace

std::vector<CharacterInfo> listCharacters(const std::string& gameDir) { return listIn(charactersDir(gameDir), false); }
std::vector<CharacterInfo> listPikminModels(const std::string& gameDir) { return listIn(pikminModelsDir(gameDir), true); }

bool removeCharacter(const std::string& gameDir, const std::string& id, std::string& error)
{
	if (!safeId(id)) {
		error = "bad character id";
		return false;
	}
	std::error_code ec;
	for (const std::string& root : { charactersDir(gameDir), pikminModelsDir(gameDir) }) {
		fs::remove_all(fs::u8path(root) / id, ec);
		if (ec) break;
	}
	if (ec) {
		error = ec.message();
		return false;
	}
	return true;
}

} // namespace nectar
