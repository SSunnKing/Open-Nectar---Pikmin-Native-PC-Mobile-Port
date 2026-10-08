/**
 * @file pc_hd_model_convert.cpp
 * @brief In-game converter from the public Pikmin 3 rips to NHM packs.
 *
 * Port of tools/build-hd-model-pack.py (and the Android HdModelConverter):
 * a small zip reader (stored / deflate via stb_image's inflate), a minimal
 * XML DOM for Collada, and the same part/texture decisions. Nothing from the
 * rips is redistributed: the user drops the zip in Load/Models and the pack
 * is built on their machine.
 */
#include "pc_hd_model_convert.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ───────────────────────────── helpers ─────────────────────────────

std::string lower(std::string s)
{
	for (char& c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

bool endsWithPath(const std::string& lowerName, const std::string& lowerSuffix)
{
	if (lowerName == lowerSuffix) return true;
	if (lowerName.size() <= lowerSuffix.size()) return false;
	return lowerName.compare(lowerName.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0
	    && lowerName[lowerName.size() - lowerSuffix.size() - 1] == '/';
}

struct Error {
	std::string message;
};

[[noreturn]] void fail(const std::string& message) { throw Error { message }; }

// ───────────────────────────── sources ─────────────────────────────

struct Source {
	virtual ~Source() = default;
	virtual bool has(const std::string& suffix) const = 0;
	virtual std::vector<char> read(const std::string& suffix) const = 0;
	virtual fs::file_time_type mtime() const = 0;
	virtual std::string label() const = 0;
};

uint16_t rd16(const char* p) { return (uint16_t)((uint8_t)p[0] | ((uint8_t)p[1] << 8)); }
uint32_t rd32(const char* p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

struct ZipSource : Source {
	struct Entry {
		std::string name;
		std::string lowerName;
		uint16_t method;
		uint32_t csize, usize, local;
	};
	fs::path path;
	std::vector<char> data;
	std::vector<Entry> entries;

	bool open(const fs::path& p)
	{
		path = p;
		std::ifstream in(p, std::ios::binary);
		if (!in) return false;
		data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		if (data.size() < 22) return false;
		// End of central directory: scan back over a possible comment.
		size_t eocd = std::string::npos;
		for (size_t i = data.size() - 22; i + 1 > 0; i--) {
			if (rd32(&data[i]) == 0x06054b50) { eocd = i; break; }
			if (data.size() - i > 70000) break;
		}
		if (eocd == std::string::npos) return false;
		uint16_t count = rd16(&data[eocd + 10]);
		uint32_t cdOff = rd32(&data[eocd + 16]);
		size_t pos = cdOff;
		for (uint16_t i = 0; i < count; i++) {
			if (pos + 46 > data.size() || rd32(&data[pos]) != 0x02014b50) return false;
			Entry e;
			e.method = rd16(&data[pos + 10]);
			e.csize  = rd32(&data[pos + 20]);
			e.usize  = rd32(&data[pos + 24]);
			uint16_t n = rd16(&data[pos + 28]), x = rd16(&data[pos + 30]), c = rd16(&data[pos + 32]);
			e.local = rd32(&data[pos + 42]);
			if (pos + 46 + n > data.size()) return false;
			std::string name(&data[pos + 46], n);
			std::replace(name.begin(), name.end(), '\\', '/');
			e.name      = name;
			e.lowerName = lower(name);
			entries.push_back(e);
			pos += 46 + n + x + c;
		}
		return true;
	}

	const Entry* find(const std::string& suffix) const
	{
		const std::string want = lower(suffix);
		for (const Entry& e : entries)
			if (endsWithPath(e.lowerName, want)) return &e;
		return nullptr;
	}

	bool has(const std::string& suffix) const override { return find(suffix) != nullptr; }

	std::vector<char> read(const std::string& suffix) const override
	{
		const Entry* e = find(suffix);
		if (!e) fail("The archive is missing " + suffix + ".");
		return readEntry(*e);
	}

	std::vector<char> readEntry(const Entry& entry) const
	{
		const Entry* e = &entry;
		const std::string suffix = e->name;
		size_t pos = e->local;
		if (pos + 30 > data.size() || rd32(&data[pos]) != 0x04034b50) fail("Corrupt zip entry " + suffix + ".");
		uint16_t n = rd16(&data[pos + 26]), x = rd16(&data[pos + 28]);
		size_t start = pos + 30 + n + x;
		if (start + e->csize > data.size()) fail("Truncated zip entry " + suffix + ".");
		if (e->method == 0) return std::vector<char>(&data[start], &data[start] + e->csize);
		if (e->method != 8) fail("Unsupported zip compression for " + suffix + ".");
		int outLen = 0;
		char* out = stbi_zlib_decode_noheader_malloc(&data[start], (int)e->csize, &outLen);
		if (!out) fail("Could not inflate " + suffix + ".");
		std::vector<char> result(out, out + outLen);
		std::free(out);
		return result;
	}

	fs::file_time_type mtime() const override
	{
		std::error_code ec;
		return fs::last_write_time(path, ec);
	}
	std::string label() const override { return path.filename().string(); }
};

struct DirSource : Source {
	fs::path root;
	std::vector<std::pair<std::string, fs::path>> files; // lower relative name, path

	bool open(const fs::path& p)
	{
		root = p;
		std::error_code ec;
		for (fs::recursive_directory_iterator it(p, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec)) continue;
			std::string rel = fs::relative(it->path(), p, ec).generic_string();
			files.emplace_back(lower(rel), it->path());
		}
		return !files.empty();
	}

	const fs::path* find(const std::string& suffix) const
	{
		const std::string want = lower(suffix);
		for (const auto& f : files)
			if (endsWithPath(f.first, want)) return &f.second;
		return nullptr;
	}

	bool has(const std::string& suffix) const override { return find(suffix) != nullptr; }

	std::vector<char> read(const std::string& suffix) const override
	{
		const fs::path* p = find(suffix);
		if (!p) fail("The folder is missing " + suffix + ".");
		std::ifstream in(*p, std::ios::binary);
		if (!in) fail("Could not read " + p->string() + ".");
		return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	fs::file_time_type mtime() const override
	{
		// file_time_type{} is not "the past" on every library (libstdc++'s
		// file clock epoch is in 2174), so start from the first .dae seen.
		fs::file_time_type newest = fs::file_time_type::min();
		std::error_code ec;
		for (const auto& f : files) {
			if (f.first.size() < 4 || f.first.compare(f.first.size() - 4, 4, ".dae") != 0) continue;
			auto t = fs::last_write_time(f.second, ec);
			if (!ec && t > newest) newest = t;
		}
		return newest;
	}
	std::string label() const override { return root.filename().string() + "/"; }
};

// ───────────────────────────── XML ─────────────────────────────

struct XmlNode {
	std::string name;
	std::vector<std::pair<std::string, std::string>> attrs;
	std::string text;
	std::vector<XmlNode> children;

	std::string attr(const char* key) const
	{
		for (const auto& a : attrs)
			if (a.first == key) return a.second;
		return {};
	}
	const XmlNode* child(const char* n) const
	{
		for (const XmlNode& c : children)
			if (c.name == n) return &c;
		return nullptr;
	}
	std::vector<const XmlNode*> all(const char* n) const
	{
		std::vector<const XmlNode*> out;
		for (const XmlNode& c : children)
			if (c.name == n) out.push_back(&c);
		return out;
	}
	void descendants(const char* n, std::vector<const XmlNode*>& out) const
	{
		for (const XmlNode& c : children) {
			if (c.name == n) out.push_back(&c);
			c.descendants(n, out);
		}
	}
};

const XmlNode* path(const XmlNode* n, std::initializer_list<const char*> names)
{
	for (const char* name : names) {
		if (!n) return nullptr;
		n = n->child(name);
	}
	return n;
}

struct XmlParser {
	const char* p;
	const char* end;

	void skipWs()
	{
		while (p < end && std::isspace((unsigned char)*p)) p++;
	}

	bool startsWith(const char* s) const { return (size_t)(end - p) >= std::strlen(s) && std::strncmp(p, s, std::strlen(s)) == 0; }

	void skipTo(const char* s)
	{
		const char* q = std::strstr(p, s);
		p = q ? q + std::strlen(s) : end;
	}

	static std::string stripPrefix(const std::string& n)
	{
		size_t c = n.find(':');
		return c == std::string::npos ? n : n.substr(c + 1);
	}

	void parseElement(XmlNode& node)
	{
		// p is at '<'
		p++;
		const char* s = p;
		while (p < end && !std::isspace((unsigned char)*p) && *p != '>' && *p != '/') p++;
		node.name = stripPrefix(std::string(s, p));
		for (;;) {
			skipWs();
			if (p >= end) fail("Unexpected end of XML.");
			if (*p == '/') { p += 2; return; } // "/>"
			if (*p == '>') { p++; break; }
			const char* ks = p;
			while (p < end && *p != '=' && !std::isspace((unsigned char)*p)) p++;
			std::string key = stripPrefix(std::string(ks, p));
			skipWs();
			if (p < end && *p == '=') p++;
			skipWs();
			char quote = p < end ? *p : '"';
			p++;
			const char* vs = p;
			while (p < end && *p != quote) p++;
			node.attrs.emplace_back(key, std::string(vs, p));
			p++;
		}
		// content
		for (;;) {
			const char* ts = p;
			while (p < end && *p != '<') p++;
			if (p > ts) node.text.append(ts, p);
			if (p >= end) fail("Unexpected end of XML.");
			if (startsWith("</")) { skipTo(">"); return; }
			if (startsWith("<!--")) { skipTo("-->"); continue; }
			if (startsWith("<![CDATA[")) { const char* cs = p + 9; skipTo("]]>"); node.text.append(cs, p - 3); continue; }
			if (startsWith("<?")) { skipTo("?>"); continue; }
			node.children.emplace_back();
			parseElement(node.children.back());
		}
	}

	XmlNode parse(const std::vector<char>& src)
	{
		std::string buf(src.begin(), src.end()); // NUL-terminated for strstr
		p   = buf.c_str();
		end = p + buf.size();
		if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
			p += 3; // UTF-8 BOM (the Olimar and bulborb rips carry one)
		XmlNode root;
		for (;;) {
			skipWs();
			if (p >= end) fail("Empty XML.");
			if (startsWith("<?")) { skipTo("?>"); continue; }
			if (startsWith("<!--")) { skipTo("-->"); continue; }
			if (startsWith("<!")) { skipTo(">"); continue; }
			if (*p == '<') { parseElement(root); return root; }
			fail("Malformed XML.");
		}
	}
};

std::vector<std::string> tokens(const std::string& text)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && std::isspace((unsigned char)text[i])) i++;
		size_t s = i;
		while (i < text.size() && !std::isspace((unsigned char)text[i])) i++;
		if (i > s) out.emplace_back(text, s, i - s);
	}
	return out;
}

std::vector<float> floats(const XmlNode* n)
{
	std::vector<float> out;
	if (!n) return out;
	const char* c = n->text.c_str();
	char* e = nullptr;
	for (;;) {
		while (*c && std::isspace((unsigned char)*c)) c++;
		if (!*c) break;
		out.push_back(std::strtof(c, &e));
		if (e == c) break;
		c = e;
	}
	return out;
}

std::vector<int> ints(const XmlNode* n)
{
	std::vector<int> out;
	if (!n) return out;
	const char* c = n->text.c_str();
	char* e = nullptr;
	for (;;) {
		while (*c && std::isspace((unsigned char)*c)) c++;
		if (!*c) break;
		out.push_back((int)std::strtol(c, &e, 10));
		if (e == c) break;
		c = e;
	}
	return out;
}

std::string ref(const XmlNode* n, const char* attr)
{
	std::string v = n ? n->attr(attr) : std::string();
	return (!v.empty() && v[0] == '#') ? v.substr(1) : v;
}

const XmlNode* input(const XmlNode* parent, const char* semantic)
{
	if (!parent) return nullptr;
	for (const XmlNode* in : parent->all("input"))
		if (in->attr("semantic") == semantic) return in;
	return nullptr;
}

// ───────────────────────────── Collada data ─────────────────────────────

struct DaeSource {
	std::vector<float> floats;
	std::vector<std::string> names;
	int stride = 1;
};

std::map<std::string, DaeSource> sourceData(const XmlNode* parent)
{
	std::map<std::string, DaeSource> out;
	if (!parent) return out;
	for (const XmlNode* source : parent->all("source")) {
		DaeSource s;
		const XmlNode* acc = path(source, { "technique_common", "accessor" });
		std::string stride = acc ? acc->attr("stride") : "";
		s.stride = stride.empty() ? 1 : std::atoi(stride.c_str());
		if (const XmlNode* arr = source->child("float_array")) s.floats = floats(arr);
		else if (const XmlNode* names = source->child("Name_array")) s.names = tokens(names->text);
		out[source->attr("id")] = std::move(s);
	}
	return out;
}

struct Influence {
	std::vector<std::string> joints;
	std::vector<float> weights;
};

struct Skins {
	std::map<std::string, std::vector<Influence>> controllers;
	std::vector<std::string> jointOrder;
	std::map<std::string, std::vector<float>> bindByJoint;
};

Skins readSkins(const XmlNode& root)
{
	Skins out;
	const XmlNode* lib = root.child("library_controllers");
	if (!lib) fail("No <library_controllers>.");
	for (const XmlNode* controller : lib->all("controller")) {
		const XmlNode* skin = controller->child("skin");
		if (!skin) continue;
		auto data = sourceData(skin);
		const XmlNode* joints = skin->child("joints");
		const auto& names = data[ref(input(joints, "JOINT"), "source")].names;
		const auto& binds = data[ref(input(joints, "INV_BIND_MATRIX"), "source")].floats;
		for (size_t i = 0; i < names.size(); i++) {
			if (out.bindByJoint.count(names[i])) continue;
			if (binds.size() < (i + 1) * 16) fail("Short INV_BIND_MATRIX array.");
			out.bindByJoint[names[i]] = std::vector<float>(binds.begin() + i * 16, binds.begin() + (i + 1) * 16);
			out.jointOrder.push_back(names[i]);
		}
		const XmlNode* vw = skin->child("vertex_weights");
		const XmlNode* weightInput = input(vw, "WEIGHT");
		const XmlNode* jointInput  = input(vw, "JOINT");
		if (!vw || !weightInput || !jointInput) fail("Skin without vertex weights.");
		const auto& weights = data[ref(weightInput, "source")].floats;
		int jointOffset  = std::atoi(jointInput->attr("offset").c_str());
		int weightOffset = std::atoi(weightInput->attr("offset").c_str());
		int stride = 0;
		for (const XmlNode* in : vw->all("input")) stride = std::max(stride, std::atoi(in->attr("offset").c_str()));
		stride += 1;
		std::vector<int> vcount = ints(vw->child("vcount"));
		std::vector<int> v      = ints(vw->child("v"));
		std::vector<Influence> influences;
		influences.reserve(vcount.size());
		size_t cursor = 0;
		for (int count : vcount) {
			std::vector<std::pair<float, std::string>> row;
			for (int k = 0; k < count; k++) {
				if (cursor + std::max(jointOffset, weightOffset) >= v.size()) fail("Short vertex weight array.");
				row.emplace_back(weights[v[cursor + weightOffset]], names[v[cursor + jointOffset]]);
				cursor += stride;
			}
			std::stable_sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			if (row.size() > 4) row.resize(4);
			float total = 0;
			for (const auto& r : row) total += r.first;
			if (total == 0) total = 1;
			Influence inf;
			for (const auto& r : row) {
				inf.joints.push_back(r.second);
				inf.weights.push_back(r.first / total);
			}
			influences.push_back(std::move(inf));
		}
		out.controllers[ref(skin, "source")] = std::move(influences);
	}
	return out;
}

// ───────────────────────────── expand ─────────────────────────────

using Wrap      = float (*)(float);
using Transform = void (*)(float*);

float wrapClamp(float t) { return t - std::floor(t); }
float wrapRepeat(float t) { return t; }
float wrapMirror(float t)
{
	float m = t - 2.0f * std::floor(t / 2.0f);
	return m > 1.0f ? 2.0f - m : m;
}

// Flat vertex: 3 pos, 3 nrm, 2 uv, 4 bone, 4 weight (bones stored as floats).
using Vertex = std::array<float, 16>;

std::string material(const XmlNode* geometry)
{
	const XmlNode* tri = path(geometry, { "mesh", "triangles" });
	return tri ? tri->attr("material") : std::string();
}

std::vector<Vertex> expand(const XmlNode* geometry, const Skins& skins, const std::vector<std::string>& jointNames,
                           Wrap wrap, Transform transform)
{
	const XmlNode* mesh = geometry->child("mesh");
	auto data = sourceData(mesh);
	const XmlNode* triangles = mesh ? mesh->child("triangles") : nullptr;
	if (!triangles) fail("Geometry without <triangles>.");
	const XmlNode* verticesNode = mesh->child("vertices");
	std::map<std::string, int> offsets;
	std::map<std::string, std::string> sources;
	int stride = 0;
	for (const XmlNode* in : triangles->all("input")) {
		int offset = std::atoi(in->attr("offset").c_str());
		stride = std::max(stride, offset);
		std::string semantic = in->attr("semantic");
		if (semantic == "VERTEX" && verticesNode) {
			for (const XmlNode* sub : verticesNode->all("input")) {
				offsets[sub->attr("semantic")] = offset;
				sources[sub->attr("semantic")] = ref(sub, "source");
			}
		} else {
			offsets[semantic] = offset;
			sources[semantic] = ref(in, "source");
		}
	}
	stride += 1;
	std::vector<int> indices = ints(triangles->child("p"));
	auto ctl = skins.controllers.find(geometry->attr("id"));
	if (ctl == skins.controllers.end()) fail("No skin for geometry " + geometry->attr("id") + ".");
	const std::vector<Influence>& influences = ctl->second;
	std::map<std::string, int> jointIndex;
	for (size_t i = 0; i < jointNames.size(); i++) jointIndex[jointNames[i]] = (int)i;

	auto value = [&](size_t base, const char* semantic, int size, float* out) {
		auto so = sources.find(semantic);
		auto oo = offsets.find(semantic);
		if (so == sources.end() || oo == offsets.end()) fail(std::string("Mesh has no ") + semantic + " input.");
		const DaeSource& s = data[so->second];
		size_t index = (size_t)indices[base + oo->second];
		if ((index + 1) * s.stride > s.floats.size() && index * s.stride + size > s.floats.size()) fail("Vertex index out of range.");
		for (int i = 0; i < size; i++) out[i] = s.floats[index * s.stride + i];
	};

	std::vector<Vertex> out;
	out.reserve(indices.size() / stride);
	for (size_t base = 0; base + stride <= indices.size(); base += stride) {
		float pos[3], nrm[3], uv[2];
		value(base, "POSITION", 3, pos);
		value(base, "NORMAL", 3, nrm);
		value(base, "TEXCOORD", 2, uv);
		if (transform) { transform(pos); transform(nrm); }
		size_t skinIndex = (size_t)indices[base + offsets["POSITION"]];
		if (skinIndex >= influences.size()) fail("Skin index out of range.");
		const Influence& skin = influences[skinIndex];
		Vertex v {};
		v[0] = pos[0]; v[1] = pos[1]; v[2] = pos[2];
		v[3] = nrm[0]; v[4] = nrm[1]; v[5] = nrm[2];
		// Collada UV origin is bottom-left; the images are top-left.
		v[6] = wrap(uv[0]);
		v[7] = wrap(1.0f - uv[1]);
		for (size_t k = 0; k < skin.joints.size() && k < 4; k++) {
			auto ji = jointIndex.find(skin.joints[k]);
			if (ji == jointIndex.end()) fail("Unknown joint " + skin.joints[k] + ".");
			v[8 + k]  = (float)ji->second;
			v[12 + k] = skin.weights[k];
		}
		out.push_back(v);
	}
	return out;
}

// ───────────────────────────── textures & NHM ─────────────────────────────

struct Texture {
	int width = 0, height = 0;
	std::vector<unsigned char> rgba;

	Texture translucent(int alpha) const
	{
		Texture t = *this;
		for (size_t i = 3; i < t.rgba.size(); i += 4) t.rgba[i] = (unsigned char)alpha;
		return t;
	}
};

Texture loadTexture(const Source& src, const std::string& suffix)
{
	std::vector<char> bytes = src.read(suffix);
	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
	if (!pixels) fail("Could not decode " + suffix + ".");
	Texture t;
	t.width  = w;
	t.height = h;
	t.rgba.assign(pixels, pixels + (size_t)w * h * 4);
	stbi_image_free(pixels);
	return t;
}

constexpr int kFlagRepeat = 1;
constexpr int kFlagNoCull = 2;
constexpr int kFlagNoTint = 4; // ignores the in-game tint (a captain's head/visor)
constexpr int kGlassAlpha  = 72;
constexpr int kCorneaAlpha = 64;

struct Part {
	std::vector<Vertex> vertices;
	Texture texture;
	int flags = 0;
};

struct Bone {
	std::string name;
	std::vector<float> inverseBind;
};

const std::vector<float> kIdentity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
const std::vector<std::string> kJoints = {
	"kosinull", "legcentre", "llegjnt", "rlegjnt", "sebonjnt", "headjnt",
	"happajnt1", "happajnt2", "happajnt3", "lhandjnt", "rhandjnt",
};

void put32(std::vector<char>& o, uint32_t v)
{
	o.push_back((char)(v & 0xff));
	o.push_back((char)((v >> 8) & 0xff));
	o.push_back((char)((v >> 16) & 0xff));
	o.push_back((char)((v >> 24) & 0xff));
}
void putF(std::vector<char>& o, float f)
{
	uint32_t v;
	std::memcpy(&v, &f, 4);
	put32(o, v);
}

void writePack(const fs::path& output, const std::vector<Bone>& bones, const std::vector<Part>& parts)
{
	bool anyFlags = false;
	for (const Part& p : parts) anyFlags |= p.flags != 0;
	const uint32_t version = anyFlags ? 2 : 1;
	std::vector<char> o;
	o.insert(o.end(), { 'N', 'H', 'M', '1' });
	put32(o, version);
	put32(o, (uint32_t)bones.size());
	put32(o, (uint32_t)parts.size());
	for (const Bone& b : bones) {
		o.push_back((char)b.name.size());
		o.insert(o.end(), b.name.begin(), b.name.end());
		for (float f : b.inverseBind) putF(o, f);
	}
	for (const Part& p : parts) {
		put32(o, (uint32_t)p.vertices.size());
		put32(o, (uint32_t)p.texture.width);
		put32(o, (uint32_t)p.texture.height);
		put32(o, (uint32_t)p.texture.rgba.size());
		if (version >= 2) put32(o, (uint32_t)p.flags);
		for (const Vertex& v : p.vertices) {
			for (int i = 0; i < 8; i++) putF(o, v[i]);
			for (int i = 8; i < 12; i++) o.push_back((char)(int)v[i]);
			for (int i = 12; i < 16; i++) putF(o, v[i]);
		}
		o.insert(o.end(), p.texture.rgba.begin(), p.texture.rgba.end());
	}
	std::error_code ec;
	fs::create_directories(output.parent_path(), ec);
	const fs::path tmp = output.string() + ".part";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out || !out.write(o.data(), (std::streamsize)o.size())) fail("Could not write " + output.string() + ".");
	}
	fs::rename(tmp, output, ec);
	if (ec) {
		fs::remove(output, ec);
		fs::rename(tmp, output, ec);
		if (ec) fail("Could not replace " + output.string() + ".");
	}
	size_t total = 0;
	for (const Part& p : parts) total += p.vertices.size();
	std::printf("[HD Models] wrote %s: %zu vertices, %zu parts (v%u)\n", output.string().c_str(), total, parts.size(), version);
}

std::vector<Bone> bonesFor(const std::vector<std::string>& joints, const Skins& skins)
{
	std::vector<Bone> out;
	for (const std::string& j : joints) {
		auto it = skins.bindByJoint.find(j);
		out.push_back({ j, it == skins.bindByJoint.end() ? kIdentity : it->second });
	}
	return out;
}

std::vector<Bone> identityBones(const std::vector<std::string>& joints)
{
	std::vector<Bone> out;
	for (const std::string& j : joints) out.push_back({ j, kIdentity });
	return out;
}

std::vector<const XmlNode*> geometries(const XmlNode& root)
{
	const XmlNode* lib = root.child("library_geometries");
	return lib ? lib->all("geometry") : std::vector<const XmlNode*>();
}

XmlNode dae(const Source& src, const std::string& suffix)
{
	XmlParser parser;
	return parser.parse(src.read(suffix));
}

// ───────────────────────────── builders ─────────────────────────────

// Capitán de Pikmin 3: playerE (Olimar) o playerD (Louie), misma
// estructura. Cabeza y visor van sin tinte (el tinte coop colorea el traje).
void buildCaptainP3(const Source& src, const fs::path& outDir, const char* prefix, const char* file)
{
	const std::string p = prefix;
	XmlNode root = dae(src, p + ".dae");
	Skins skins  = readSkins(root);
	// Only head_m samples playerE_head; suit, metal, light and both helmet
	// layers (naka inner, soto glass) sample playerE_body. The glass is the
	// last part so it is drawn after the face it covers and can blend.
	std::vector<Vertex> body, head, glass;
	for (const XmlNode* g : geometries(root)) {
		std::string m = material(g);
		std::vector<Vertex>& target = m == "head_m" ? head : (m == "naka_m" || m == "soto_m") ? glass : body;
		auto v = expand(g, skins, kJoints, wrapClamp, nullptr);
		target.insert(target.end(), v.begin(), v.end());
	}
	Texture bodyTex = loadTexture(src, p + "_body.png");
	std::vector<Part> parts;
	parts.push_back({ body, bodyTex, 0 });
	parts.push_back({ head, loadTexture(src, p + "_head.png"), kFlagNoTint });
	parts.push_back({ glass, bodyTex.translucent(kGlassAlpha), kFlagNoTint });
	writePack(outDir / file, bonesFor(kJoints, skins), parts);
}

void buildOlimar(const Source& src, const fs::path& outDir) { buildCaptainP3(src, outDir, "playerE", "olimar_hd.nhm"); }
void buildLouieHd(const Source& src, const fs::path& outDir) { buildCaptainP3(src, outDir, "playerD", "louie_hd.nhm"); }

// ── Louie (Pikmin 2 rip, "Captain Louie/orima3.dae") ──
// Same rig as Pikmin 1 (joint names on the scene nodes, skin refers to
// jointnodeN), no normals in the rip, <polylist> of triangles.

// Cristal casi transparente, como el de Olimar HD (kGlassAlpha).
constexpr int kLouieSotoAlpha = 56;
const unsigned char kLouieNakaRgba[4] = { 60, 120, 255, 28 };

void collectJointNames(const XmlNode& n, std::map<std::string, std::string>& out)
{
	if (n.name == "node" && n.attr("type") == "JOINT") {
		std::string name = n.attr("name").empty() ? n.attr("id") : n.attr("name");
		out[n.attr("id")] = name;
		if (!n.attr("sid").empty()) out[n.attr("sid")] = name;
	}
	for (const XmlNode& c : n.children) collectJointNames(c, out);
}

// Expands a <polylist> (all triangles) that has POSITION + TEXCOORD only,
// computing smooth normals per position index.
std::vector<Vertex> expandPolylist(const XmlNode* geometry, const Skins& skins, const std::vector<std::string>& jointNames,
                                   float uvScale, float uvOffset)
{
	const XmlNode* mesh = geometry->child("mesh");
	auto data = sourceData(mesh);
	const XmlNode* prim = mesh ? mesh->child("polylist") : nullptr;
	if (!prim) prim = mesh ? mesh->child("triangles") : nullptr;
	if (!prim) fail("Geometry without <polylist>.");
	const XmlNode* verticesNode = mesh->child("vertices");
	std::map<std::string, int> offsets;
	std::map<std::string, std::string> sources;
	int stride = 0;
	for (const XmlNode* in : prim->all("input")) {
		int offset = std::atoi(in->attr("offset").c_str());
		stride = std::max(stride, offset);
		std::string semantic = in->attr("semantic");
		if (semantic == "VERTEX" && verticesNode) {
			for (const XmlNode* sub : verticesNode->all("input")) {
				offsets[sub->attr("semantic")] = offset;
				sources[sub->attr("semantic")] = ref(sub, "source");
			}
		} else {
			offsets[semantic] = offset;
			sources[semantic] = ref(in, "source");
		}
	}
	stride += 1;
	if (const XmlNode* vcount = prim->child("vcount"))
		for (int c : ints(vcount)) if (c != 3) fail("polylist with non-triangles.");
	std::vector<int> indices = ints(prim->child("p"));
	auto ctl = skins.controllers.find(geometry->attr("id"));
	if (ctl == skins.controllers.end()) fail("No skin for geometry " + geometry->attr("id") + ".");
	const std::vector<Influence>& influences = ctl->second;
	std::map<std::string, int> jointIndex;
	for (size_t i = 0; i < jointNames.size(); i++) jointIndex[jointNames[i]] = (int)i;
	if (!sources.count("POSITION") || !sources.count("TEXCOORD")) fail("Mesh lacks POSITION/TEXCOORD.");
	const DaeSource& P = data[sources["POSITION"]];
	const DaeSource& T = data[sources["TEXCOORD"]];
	const int po = offsets["POSITION"], to = offsets["TEXCOORD"];
	auto pos = [&](int i, float* out) {
		if ((size_t)i * P.stride + 3 > P.floats.size()) fail("Vertex index out of range.");
		for (int k = 0; k < 3; k++) out[k] = P.floats[(size_t)i * P.stride + k];
	};
	std::map<int, std::array<float, 3>> acc;
	for (size_t base = 0; base + (size_t)stride * 3 <= indices.size(); base += (size_t)stride * 3) {
		int ids[3];
		float a[3], b[3], c[3];
		for (int k = 0; k < 3; k++) ids[k] = indices[base + (size_t)k * stride + po];
		pos(ids[0], a); pos(ids[1], b); pos(ids[2], c);
		float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		float w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float n[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
		for (int id : ids) {
			auto& e = acc[id];
			e[0] += n[0]; e[1] += n[1]; e[2] += n[2];
		}
	}
	std::vector<Vertex> out;
	out.reserve(indices.size() / stride);
	for (size_t base = 0; base + stride <= indices.size(); base += stride) {
		int pi = indices[base + po], ti = indices[base + to];
		float p[3];
		pos(pi, p);
		if ((size_t)ti * T.stride + 2 > T.floats.size()) fail("UV index out of range.");
		float uv[2] = { T.floats[(size_t)ti * T.stride], T.floats[(size_t)ti * T.stride + 1] };
		std::array<float, 3> n = acc.count(pi) ? acc[pi] : std::array<float, 3> { 0, 1, 0 };
		float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len <= 0) len = 1;
		if ((size_t)pi >= influences.size()) fail("Skin index out of range.");
		const Influence& skin = influences[pi];
		Vertex v {};
		v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
		v[3] = n[0] / len; v[4] = n[1] / len; v[5] = n[2] / len;
		v[6] = wrapClamp(uv[0] * uvScale + uvOffset);
		v[7] = wrapClamp((1.0f - uv[1]) * uvScale + uvOffset);
		for (size_t k = 0; k < skin.joints.size() && k < 4; k++) {
			auto ji = jointIndex.find(skin.joints[k]);
			if (ji == jointIndex.end()) fail("Unknown joint " + skin.joints[k] + ".");
			v[8 + k]  = (float)ji->second;
			v[12 + k] = skin.weights[k];
		}
		out.push_back(v);
	}
	return out;
}

// Per triangle: to `head` when most of its weight sits on the head/antenna
// joints (indices in kJoints), otherwise to `suit`.
void splitHead(const std::vector<Vertex>& in, std::vector<Vertex>& suit, std::vector<Vertex>& head)
{
	static const int kHeadJoints[] = { 5, 6, 7, 8 }; // headjnt, happajnt1-3
	for (size_t i = 0; i + 3 <= in.size(); i += 3) {
		float weight = 0;
		for (int k = 0; k < 3; k++)
			for (int b = 0; b < 4; b++)
				for (int hj : kHeadJoints)
					if ((int)in[i + k][8 + b] == hj) weight += in[i + k][12 + b];
		std::vector<Vertex>& dst = weight > 1.5f ? head : suit;
		dst.insert(dst.end(), in.begin() + i, in.begin() + i + 3);
	}
}

std::vector<Vertex> flipWinding(const std::vector<Vertex>& in)
{
	std::vector<Vertex> out;
	out.reserve(in.size());
	for (size_t i = 0; i + 3 <= in.size(); i += 3) {
		const Vertex* tri[3] = { &in[i], &in[i + 2], &in[i + 1] };
		for (const Vertex* t : tri) {
			Vertex v = *t;
			v[3] = -v[3]; v[4] = -v[4]; v[5] = -v[5];
			out.push_back(v);
		}
	}
	return out;
}

void buildLouie(const Source& src, const fs::path& outDir)
{
	XmlNode root = dae(src, "orima3.dae");
	std::map<std::string, std::string> nodeName;
	collectJointNames(root, nodeName);
	Skins raw = readSkins(root);
	// Rename jointnodeN -> real joint names everywhere.
	Skins skins;
	for (const auto& kv : raw.bindByJoint) {
		auto it = nodeName.find(kv.first);
		skins.bindByJoint[it == nodeName.end() ? kv.first : it->second] = kv.second;
	}
	for (const auto& kv : raw.controllers) {
		std::vector<Influence> rows = kv.second;
		for (Influence& inf : rows)
			for (std::string& j : inf.joints) {
				auto it = nodeName.find(j);
				if (it != nodeName.end()) j = it->second;
			}
		skins.controllers[kv.first] = std::move(rows);
	}
	// Geometry -> texture file, via scene node -> controller -> material -> effect -> image.
	std::map<std::string, std::string> imageFile;
	if (const XmlNode* lib = root.child("library_images"))
		for (const XmlNode* img : lib->all("image"))
			if (const XmlNode* init = img->child("init_from")) imageFile[img->attr("id")] = init->text;
	std::map<std::string, std::string> materialImage;
	if (const XmlNode* lib = root.child("library_materials"))
		for (const XmlNode* m : lib->all("material")) {
			std::string effectId = ref(m->child("instance_effect"), "url");
			const XmlNode* effect = nullptr;
			if (const XmlNode* el = root.child("library_effects"))
				for (const XmlNode* e : el->all("effect"))
					if (e->attr("id") == effectId) effect = e;
			std::vector<const XmlNode*> inits;
			if (effect) effect->descendants("init_from", inits);
			materialImage[m->attr("id")] = inits.empty() ? std::string() : inits[0]->text;
		}
	std::map<std::string, std::string> geometryMaterial;
	std::map<std::string, std::string> controllerGeometry;
	if (const XmlNode* lib = root.child("library_controllers"))
		for (const XmlNode* c : lib->all("controller"))
			if (const XmlNode* skin = c->child("skin")) controllerGeometry[c->attr("id")] = ref(skin, "source");
	std::vector<const XmlNode*> instances;
	root.descendants("instance_controller", instances);
	for (const XmlNode* inst : instances) {
		std::vector<const XmlNode*> mats;
		inst->descendants("instance_material", mats);
		auto g = controllerGeometry.find(ref(inst, "url"));
		if (g != controllerGeometry.end() && !mats.empty()) geometryMaterial[g->second] = ref(mats[0], "target");
	}
	std::vector<Vertex> body, naka, soto;
	for (const XmlNode* g : geometries(root)) {
		std::string image = materialImage[geometryMaterial[g->attr("id")]];
		std::string file  = imageFile.count(image) ? imageFile[image] : image;
		std::string lf    = lower(file);
		if (lf.find("luzy") != std::string::npos) {
			auto v = expandPolylist(g, skins, kJoints, 1.0f, 0.0f);
			body.insert(body.end(), v.begin(), v.end());
		} else if (lf.find("helkan") != std::string::npos) {
			auto v = expandPolylist(g, skins, kJoints, 0.5f, 0.5f); // soto_mat texture matrix
			soto.insert(soto.end(), v.begin(), v.end());
		} else {
			auto v = expandPolylist(g, skins, kJoints, 1.0f, 0.0f);
			naka.insert(naka.end(), v.begin(), v.end());
		}
	}
	// naka is wound inwards (inside of the visor); flip into an outward shell.
	naka = flipWinding(naka);
	// luzy_565 is an 8x8 palette (2x2 cells): nearest-upscale to 64x64 so
	// bilinear sampling never bleeds across cells.
	Texture palette = loadTexture(src, "luzy_565.png");
	Texture bodyTex;
	bodyTex.width = bodyTex.height = 64;
	bodyTex.rgba.resize(64 * 64 * 4);
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++) {
			int sx = x * palette.width / 64, sy = y * palette.height / 64;
			std::memcpy(&bodyTex.rgba[((size_t)y * 64 + x) * 4], &palette.rgba[((size_t)sy * palette.width + sx) * 4], 4);
		}
	Texture nakaTex;
	nakaTex.width = nakaTex.height = 4;
	for (int i = 0; i < 16; i++) nakaTex.rgba.insert(nakaTex.rgba.end(), kLouieNakaRgba, kLouieNakaRgba + 4);
	// The co-op tint must only colour the suit: head and visor go untinted.
	std::vector<Vertex> suit, head;
	splitHead(body, suit, head);
	std::vector<Part> parts;
	parts.push_back({ suit, bodyTex, 0 });
	parts.push_back({ head, bodyTex, kFlagNoTint });
	parts.push_back({ naka, nakaTex, kFlagNoTint });
	parts.push_back({ soto, loadTexture(src, "helkan_8ia.png").translucent(kLouieSotoAlpha), kFlagNoTint });
	writePack(outDir / "louie.nhm", bonesFor(kJoints, skins), parts);
}

// Pikmin 3's leaf/bud/flower have the stem along -Z; Pikmin 1 draws
// pikis/happas/*.mod in happajnt3's space with the stem along +X.
void happaToJoint(float* v)
{
	float x = v[0], y = v[1], z = v[2];
	v[0] = -z; v[1] = y; v[2] = x;
}

void buildPikmin(const Source& src, const fs::path& outDir)
{
	const char* colours[] = { "red", "yellow", "blue" };
	for (const char* colour : colours) {
		XmlNode root = dae(src, std::string("piki_p3_") + colour + ".dae");
		Skins skins  = readSkins(root);
		std::vector<Vertex> vertices;
		// "Piki_PikminCOLOR_all requires Image mapping Mirror X/Y" (rip notes).
		for (const XmlNode* g : geometries(root)) {
			auto v = expand(g, skins, kJoints, wrapMirror, nullptr);
			vertices.insert(vertices.end(), v.begin(), v.end());
		}
		std::vector<Part> parts;
		parts.push_back({ vertices, loadTexture(src, std::string("piki_") + colour + "_all.png"), 0 });
		writePack(outDir / (std::string("piki_") + colour + ".nhm"), bonesFor(kJoints, skins), parts);
	}
	const char* happas[][3] = {
		{ "leaf", "leaf.dae", "piki_leaf_tex.png" },
		{ "bud", "bud.dae", "piki_bud_tex.png" },
		{ "flower", "flower.dae", "piki_flower_tex.png" },
	};
	for (const auto& h : happas) {
		XmlNode root = dae(src, h[1]);
		Skins skins  = readSkins(root);
		std::vector<Vertex> vertices;
		for (const XmlNode* g : geometries(root)) {
			auto v = expand(g, skins, skins.jointOrder, wrapClamp, happaToJoint);
			vertices.insert(vertices.end(), v.begin(), v.end());
		}
		std::vector<Part> parts;
		parts.push_back({ vertices, loadTexture(src, h[2]), 0 });
		// Already in the target joint's space: identity inverse bind.
		writePack(outDir / (std::string("happa_") + h[0] + ".nhm"), identityBones({ h[0] }), parts);
	}
}

// Pikmin 3's bulborb rigs differ, so vertices are brought into Pikmin 1
// model space with a similarity transform fitted on matching joints and
// skinned with the engine's own inverse binds. swallow.mod draws at scale 3,
// hence the ~1/3 factor for the big one.
struct Fit {
	float scale, ox, oy, oz;
};
const Fit kFitDwarf = { 0.9463f, 0.004f, -0.07f, 1.13f };
const Fit kFitBig   = { 0.3298f, 0.004f, 14.193f, 3.691f };

std::vector<Vertex> expandAligned(const XmlNode* g, const Skins& skins, const std::vector<std::string>& joints, Wrap wrap,
                                  const Fit& fit)
{
	// Uniform scale + translation: normals unchanged, only positions move.
	std::vector<Vertex> v = expand(g, skins, joints, wrap, nullptr);
	for (Vertex& x : v) {
		x[0] = x[0] * fit.scale + fit.ox;
		x[1] = x[1] * fit.scale + fit.oy;
		x[2] = x[2] * fit.scale + fit.oz;
	}
	return v;
}

void buildDwarfBulborb(const Source& src, const fs::path& outDir)
{
	XmlNode root = dae(src, "kochappy.dae");
	Skins skins  = readSkins(root);
	// eye_3_m iris/sclera and eye_m highlight: thin two-sided discs.
	// eye_2_m glassy cornea: last, with alpha so the iris shows through.
	std::vector<Vertex> body, eyes, moyou, cornea;
	for (const XmlNode* g : geometries(root)) {
		std::string m = material(g);
		std::vector<Vertex>& target = m == "moyou_m" ? moyou : m == "eye_2_m" ? cornea
		    : (m == "eye_3_m" || m == "eye_m") ? eyes : body;
		auto v = expandAligned(g, skins, skins.jointOrder, wrapClamp, kFitDwarf);
		target.insert(target.end(), v.begin(), v.end());
	}
	Texture bodyTex = loadTexture(src, "kochappy_tex.png");
	std::vector<Part> parts;
	parts.push_back({ body, bodyTex, 0 });
	parts.push_back({ eyes, bodyTex, kFlagNoCull });
	parts.push_back({ moyou, loadTexture(src, "moyou_tex.png"), 0 });
	parts.push_back({ cornea, bodyTex.translucent(kCorneaAlpha), 0 });
	writePack(outDir / "bulborb_dwarf.nhm", identityBones(skins.jointOrder), parts);
}

void buildBulborb(const Source& src, const fs::path& outDir)
{
	XmlNode root = dae(src, "red bulborb/model.dae");
	Skins skins  = readSkins(root);

	std::map<std::string, std::string> images;
	if (const XmlNode* lib = root.child("library_images")) {
		for (const XmlNode* img : lib->all("image")) {
			const XmlNode* init = img->child("init_from");
			std::string p = init ? init->text : "";
			p.erase(0, p.find_first_not_of(" \t\r\n"));
			while (!p.empty() && (p[0] == '.' || p[0] == '/')) p.erase(0, 1);
			p.erase(p.find_last_not_of(" \t\r\n") + 1);
			images[img->attr("id")] = p;
		}
	}
	std::map<std::string, std::string> effectImage;
	if (const XmlNode* lib = root.child("library_effects")) {
		for (const XmlNode* effect : lib->all("effect")) {
			std::vector<const XmlNode*> init;
			effect->descendants("init_from", init);
			std::string key = init.empty() ? "" : init[0]->text;
			key.erase(0, key.find_first_not_of(" \t\r\n"));
			key.erase(key.find_last_not_of(" \t\r\n") + 1);
			auto it = images.find(key);
			effectImage[effect->attr("id")] = it == images.end() ? "" : it->second;
		}
	}
	std::map<std::string, std::string> materialImage;
	if (const XmlNode* lib = root.child("library_materials")) {
		for (const XmlNode* mat : lib->all("material")) {
			auto it = effectImage.find(ref(mat->child("instance_effect"), "url"));
			materialImage[mat->attr("id")] = it == effectImage.end() ? "" : it->second;
		}
	}
	// <triangles material> is a symbol that <bind_material> maps to the material id.
	std::map<std::string, std::string> symbolMaterial;
	std::vector<const XmlNode*> instMats;
	root.descendants("instance_material", instMats);
	for (const XmlNode* im : instMats) symbolMaterial[im->attr("symbol")] = ref(im, "target");
	// Scene nodes name the pieces in geometry order.
	std::vector<std::string> nodeNames;
	if (const XmlNode* scene = path(&root, { "library_visual_scenes", "visual_scene" })) {
		for (const XmlNode* n : scene->all("node")) {
			if (!n->child("instance_controller")) continue;
			std::string name = n->attr("name");
			nodeNames.push_back(name.empty() ? n->attr("id") : name);
		}
	}

	std::vector<Vertex> body, circle, cornea;
	auto geoms = geometries(root);
	for (size_t i = 0; i < geoms.size() && i < nodeNames.size(); i++) {
		const XmlNode* g = geoms[i];
		std::string symbol = material(g);
		auto sm = symbolMaterial.find(symbol);
		std::string matId = sm == symbolMaterial.end() ? symbol : sm->second;
		auto mi = materialImage.find(matId);
		std::string image = mi == materialImage.end() ? "face.0.png" : mi->second;
		std::vector<Vertex>* target;
		Wrap wrap;
		if (image.rfind("circle", 0) == 0) {
			// The spot layer tiles a small texture across the back: raw UVs, repeat.
			target = &circle; wrap = wrapRepeat;
		} else if (nodeNames[i].size() >= 7 && nodeNames[i].compare(nodeNames[i].size() - 7, 7, "eye_c_m") == 0) {
			target = &cornea; wrap = wrapClamp;
		} else {
			target = &body; wrap = wrapClamp;
		}
		auto v = expandAligned(g, skins, skins.jointOrder, wrap, kFitBig);
		target->insert(target->end(), v.begin(), v.end());
	}
	Texture faceTex = loadTexture(src, "face.0.png");
	std::vector<Part> parts;
	parts.push_back({ body, faceTex, 0 });
	parts.push_back({ circle, loadTexture(src, "circle.0.png"), kFlagRepeat });
	parts.push_back({ cornea, faceTex.translucent(kCorneaAlpha), 0 });
	writePack(outDir / "bulborb.nhm", identityBones(skins.jointOrder), parts);
}

// ───────────────────────────── driver ─────────────────────────────

struct Kind {
	const char* marker;                // .dae that identifies the rip
	const char* pack;                  // output folder under Load/Models
	std::vector<const char*> outputs;  // files the rip produces
	void (*build)(const Source&, const fs::path&);
};

const Kind kKinds[] = {
	{ "playerE.dae", "OlimarHD", { "olimar_hd.nhm" }, buildOlimar },
	// Louie: Pikmin 2 rip; luzy_565.png is unique to it (Olimar's rip shares orima3.dae).
	{ "luzy_565.png", "Louie", { "louie.nhm" }, buildLouie },
	{ "playerD.dae", "LouieHD", { "louie_hd.nhm" }, buildLouieHd },
	{ "piki_p3_red.dae", "PikminHD",
	  { "piki_red.nhm", "piki_yellow.nhm", "piki_blue.nhm", "happa_leaf.nhm", "happa_bud.nhm", "happa_flower.nhm" }, buildPikmin },
	{ "red bulborb/model.dae", "BulborbHD", { "bulborb.nhm" }, buildBulborb },
	{ "kochappy.dae", "BulborbHD", { "bulborb_dwarf.nhm" }, buildDwarfBulborb },
};

bool upToDate(const Kind& kind, const fs::path& modelsRoot, fs::file_time_type sourceTime)
{
	std::error_code ec;
	for (const char* file : kind.outputs) {
		const fs::path out = modelsRoot / kind.pack / file;
		if (!fs::is_regular_file(out, ec)) return false;
		if (fs::last_write_time(out, ec) < sourceTime) return false;
	}
	return true;
}

int convert(const Source& src, const fs::path& modelsRoot, bool force = false)
{
	int written = 0;
	for (const Kind& kind : kKinds) {
		if (!src.has(kind.marker)) continue;
		if (!force && upToDate(kind, modelsRoot, src.mtime())) continue;
		std::printf("[HD Models] converting %s -> %s\n", src.label().c_str(), kind.pack);
		try {
			kind.build(src, modelsRoot / kind.pack);
			written += (int)kind.outputs.size();
		} catch (const Error& e) {
			std::printf("[HD Models] %s: %s\n", src.label().c_str(), e.message.c_str());
		} catch (const std::exception& e) {
			std::printf("[HD Models] %s: %s\n", src.label().c_str(), e.what());
		}
	}
	return written;
}

// ───────────────────── Louie desde Pikmin 2 (pikis.szs) ─────────────────────
// Si Pikmin 2 está instalado, su archivo user/Kando/piki/pikis.szs trae a
// Louie en orima3.bmd (mismo esqueleto de 11 huesos que el navi de Pikmin 1)
// con sus texturas. Se lee directamente (Yaz0 -> RARC -> J3D) y se escribe el
// mismo louie.nhm que buildLouie saca del rip Collada.

uint32_t be32(const std::vector<unsigned char>& b, size_t o)
{
	if (o + 4 > b.size()) fail("Truncated Pikmin 2 data.");
	return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3];
}
uint16_t be16(const std::vector<unsigned char>& b, size_t o)
{
	if (o + 2 > b.size()) fail("Truncated Pikmin 2 data.");
	return (uint16_t)(b[o] << 8 | b[o + 1]);
}
float beF(const std::vector<unsigned char>& b, size_t o)
{
	uint32_t v = be32(b, o);
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

// Compresión LZ de Nintendo (Yaz0).
std::vector<unsigned char> yaz0(const std::vector<unsigned char>& src)
{
	if (src.size() < 16 || std::memcmp(src.data(), "Yaz0", 4) != 0) return src;
	const uint32_t size = be32(src, 4);
	std::vector<unsigned char> dst;
	dst.reserve(size);
	size_t i = 16;
	while (dst.size() < size) {
		if (i >= src.size()) fail("Truncated Yaz0 data.");
		const unsigned char code = src[i++];
		for (int bit = 0; bit < 8 && dst.size() < size; bit++) {
			if (code & (0x80 >> bit)) {
				if (i >= src.size()) fail("Truncated Yaz0 data.");
				dst.push_back(src[i++]);
				continue;
			}
			if (i + 2 > src.size()) fail("Truncated Yaz0 data.");
			const unsigned b1 = src[i], b2 = src[i + 1];
			i += 2;
			const size_t dist = ((b1 & 0xF) << 8 | b2) + 1;
			size_t n          = b1 >> 4;
			if (n == 0) {
				if (i >= src.size()) fail("Truncated Yaz0 data.");
				n = src[i++] + 0x12;
			} else {
				n += 2;
			}
			if (dist > dst.size()) fail("Bad Yaz0 back-reference.");
			for (size_t k = 0; k < n; k++) dst.push_back(dst[dst.size() - dist]);
		}
	}
	return dst;
}

// Un fichero de un archivo RARC, por nombre.
std::vector<unsigned char> rarcFile(const std::vector<unsigned char>& d, const std::string& wanted)
{
	if (d.size() < 0x40 || std::memcmp(d.data(), "RARC", 4) != 0) fail("Not a RARC archive.");
	const size_t dataOff = be32(d, 0xC) + 0x20;
	const size_t info    = 0x20;
	const uint32_t files = be32(d, info + 8);
	const size_t fileOff = be32(d, info + 0xC) + 0x20;
	const size_t strOff  = be32(d, info + 0x14) + 0x20;
	for (uint32_t i = 0; i < files; i++) {
		const size_t e   = fileOff + (size_t)i * 20;
		const unsigned type = be16(d, e + 4) >> 8;
		const size_t nameAt = strOff + be16(d, e + 6);
		if (!(type & 1) || nameAt >= d.size()) continue;
		const char* name = reinterpret_cast<const char*>(&d[nameAt]);
		if (wanted != std::string(name, strnlen(name, d.size() - nameAt))) continue;
		const size_t off = dataOff + be32(d, e + 8), size = be32(d, e + 12);
		if (off + size > d.size()) fail("Truncated RARC entry.");
		return std::vector<unsigned char>(d.begin() + off, d.begin() + off + size);
	}
	fail("Archive has no " + wanted + ".");
}

std::vector<std::string> j3dNames(const std::vector<unsigned char>& b, size_t o)
{
	std::vector<std::string> out;
	const uint16_t n = be16(b, o);
	for (uint16_t i = 0; i < n; i++) {
		const size_t at = o + be16(b, o + 4 + (size_t)i * 4 + 2);
		if (at >= b.size()) fail("Bad J3D name table.");
		const char* s = reinterpret_cast<const char*>(&b[at]);
		out.emplace_back(s, strnlen(s, b.size() - at));
	}
	return out;
}

// Texturas GX en bloques (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8).
Texture decodeGxTexture(const std::vector<unsigned char>& b, size_t at, int fmt, int w, int h)
{
	Texture t;
	t.width  = w;
	t.height = h;
	t.rgba.assign((size_t)w * h * 4, 255);
	int bw, bh;
	switch (fmt) {
	case 0: bw = 8; bh = 8; break;
	case 1: case 2: bw = 8; bh = 4; break;
	case 3: case 4: case 5: case 6: bw = 4; bh = 4; break;
	case 14: bw = 8; bh = 8; break;
	default: fail("Unsupported Pikmin 2 texture format " + std::to_string(fmt) + ".");
	}
	auto px = [&](int x, int y, int r, int g, int bl, int a) {
		if (x >= w || y >= h) return;
		unsigned char* p = &t.rgba[((size_t)y * w + x) * 4];
		p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)bl; p[3] = (unsigned char)a;
	};
	auto byteAt = [&](size_t o) -> unsigned {
		if (o >= b.size()) fail("Truncated Pikmin 2 texture.");
		return b[o];
	};
	size_t o = at;
	for (int by = 0; by < h; by += bh)
		for (int bx = 0; bx < w; bx += bw) {
			if (fmt == 14) { // CMPR: 4 subbloques DXT1 de 4x4
				for (int sub = 0; sub < 4; sub++, o += 8) {
					const unsigned c0 = byteAt(o) << 8 | byteAt(o + 1), c1 = byteAt(o + 2) << 8 | byteAt(o + 3);
					int pal[4][4];
					auto rgb = [](unsigned v, int* out) {
						out[0] = ((v >> 11) & 31) * 255 / 31; out[1] = ((v >> 5) & 63) * 255 / 63; out[2] = (v & 31) * 255 / 31; out[3] = 255;
					};
					rgb(c0, pal[0]);
					rgb(c1, pal[1]);
					for (int k = 0; k < 4; k++) {
						if (c0 > c1) {
							pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
							pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
						} else {
							pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
							pal[3][k] = 0;
						}
					}
					if (c0 <= c1) pal[3][3] = 0;
					const int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
					for (int y = 0; y < 4; y++) {
						const unsigned row = byteAt(o + 4 + y);
						for (int x = 0; x < 4; x++) {
							const int* c = pal[(row >> (6 - x * 2)) & 3];
							px(sx + x, sy + y, c[0], c[1], c[2], c[3]);
						}
					}
				}
				continue;
			}
			if (fmt == 6) { // RGBA8: 32 bytes AR + 32 bytes GB por bloque
				for (int k = 0; k < 16; k++) {
					const int x = bx + k % 4, y = by + k / 4;
					px(x, y, byteAt(o + k * 2 + 1), byteAt(o + 32 + k * 2), byteAt(o + 32 + k * 2 + 1), byteAt(o + k * 2));
				}
				o += 64;
				continue;
			}
			for (int y = by; y < by + bh; y++)
				for (int x = bx; x < bx + bw; x++) {
					switch (fmt) {
					case 0: {
						const unsigned v = byteAt(o + ((y - by) * bw + (x - bx)) / 2);
						const unsigned i = (((x - bx) & 1) ? (v & 0xF) : (v >> 4)) * 17;
						px(x, y, i, i, i, i);
						break;
					}
					case 1: { const unsigned i = byteAt(o++); px(x, y, i, i, i, i); break; }
					case 2: {
						const unsigned v = byteAt(o++);
						const unsigned i = (v & 0xF) * 17;
						px(x, y, i, i, i, (v >> 4) * 17);
						break;
					}
					case 3: {
						const unsigned a = byteAt(o), i = byteAt(o + 1);
						o += 2;
						px(x, y, i, i, i, a);
						break;
					}
					case 4: {
						const unsigned v = byteAt(o) << 8 | byteAt(o + 1);
						o += 2;
						px(x, y, ((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31, 255);
						break;
					}
					case 5: {
						const unsigned v = byteAt(o) << 8 | byteAt(o + 1);
						o += 2;
						if (v & 0x8000) {
							px(x, y, ((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31, 255);
						} else {
							px(x, y, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 / 7);
						}
						break;
					}
					}
				}
			if (fmt == 0) o += 32;
		}
	return t;
}

using Mtx = std::array<float, 16>; // fila mayor, vector columna (como Collada)

Mtx mtxMul(const Mtx& a, const Mtx& b)
{
	Mtx r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
	return r;
}

Mtx mtxInverseAffine(const Mtx& m)
{
	const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (std::fabs(det) < 1e-12f) fail("Singular joint matrix.");
	const float k = 1.0f / det;
	Mtx r {};
	r[0] = (e * i - f * h) * k; r[1] = (c * h - b * i) * k; r[2] = (b * f - c * e) * k;
	r[4] = (f * g - d * i) * k; r[5] = (a * i - c * g) * k; r[6] = (c * d - a * f) * k;
	r[8] = (d * h - e * g) * k; r[9] = (b * g - a * h) * k; r[10] = (a * e - b * d) * k;
	for (int row = 0; row < 3; row++)
		r[row * 4 + 3] = -(r[row * 4] * m[3] + r[row * 4 + 1] * m[7] + r[row * 4 + 2] * m[11]);
	r[15] = 1.0f;
	return r;
}

void mtxApply(const Mtx& m, const float* in, float* out, bool point)
{
	for (int r = 0; r < 3; r++)
		out[r] = m[r * 4] * in[0] + m[r * 4 + 1] * in[1] + m[r * 4 + 2] * in[2] + (point ? m[r * 4 + 3] : 0.0f);
}

// Un modelo J3D (bmd3) ya desplegado en triángulos en espacio de modelo, en
// la pose de reposo, con los huesos nombrados como en Pikmin 1.
struct BmdVertex {
	float pos[3], nrm[3], uv[2];
	unsigned char col[4];
	int joints[4];
	float weights[4];
};
struct BmdShape {
	int material = -1, texture = -1;
	bool hasUv = false, hasColor = false;
	std::vector<BmdVertex> tris;
};
struct BmdModel {
	std::vector<unsigned char> data;
	std::vector<std::string> jointNames;
	std::vector<Mtx> world;
	std::vector<std::string> texNames;
	std::vector<size_t> texHeaders;
	std::vector<std::array<int, 4>> tevColor0, matColor;
	std::vector<BmdShape> shapes;

	Texture texture(int index) const
	{
		const size_t h = texHeaders.at(index);
		return decodeGxTexture(data, h + be32(data, h + 0x1C), data[h], be16(data, h + 2), be16(data, h + 4));
	}
};

BmdModel parseBmd(const std::vector<unsigned char>& b)
{
	if (b.size() < 0x20 || std::memcmp(b.data(), "J3D2bmd3", 8) != 0) fail("Not a J3D model.");
	BmdModel model;
	model.data = b;
	std::map<std::string, size_t> sec;
	for (size_t o = 0x20; o + 8 <= b.size();) {
		const uint32_t size = be32(b, o + 4);
		sec[std::string(reinterpret_cast<const char*>(&b[o]), 4)] = o;
		if (size == 0) break;
		o += size;
	}
	for (const char* need : { "INF1", "VTX1", "EVP1", "DRW1", "JNT1", "SHP1", "MAT3", "TEX1" })
		if (!sec.count(need)) fail(std::string("Model has no ") + need + ".");

	// JNT1: nombres y transformaciones locales (escala, rotación ZYX, traslación).
	const size_t jnt = sec["JNT1"];
	const uint16_t jointCount = be16(b, jnt + 8);
	model.jointNames = j3dNames(b, jnt + be32(b, jnt + 0x14));
	std::vector<Mtx> local(jointCount);
	for (uint16_t j = 0; j < jointCount; j++) {
		const size_t e = jnt + be32(b, jnt + 0xC) + (size_t)be16(b, jnt + be32(b, jnt + 0x10) + j * 2) * 0x40;
		const float sx = beF(b, e + 4), sy = beF(b, e + 8), sz = beF(b, e + 12);
		float ang[3];
		for (int k = 0; k < 3; k++) ang[k] = (int16_t)be16(b, e + 0x10 + k * 2) * 3.14159265f / 32768.0f;
		const float cx = std::cos(ang[0]), snx = std::sin(ang[0]);
		const float cy = std::cos(ang[1]), sny = std::sin(ang[1]);
		const float cz = std::cos(ang[2]), snz = std::sin(ang[2]);
		const Mtx rx = { 1, 0, 0, 0, 0, cx, -snx, 0, 0, snx, cx, 0, 0, 0, 0, 1 };
		const Mtx ry = { cy, 0, sny, 0, 0, 1, 0, 0, -sny, 0, cy, 0, 0, 0, 0, 1 };
		const Mtx rz = { cz, -snz, 0, 0, snz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		const Mtx sc = { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1 };
		Mtx m = mtxMul(mtxMul(mtxMul(rz, ry), rx), sc);
		m[3] = beF(b, e + 0x18); m[7] = beF(b, e + 0x1C); m[11] = beF(b, e + 0x20);
		local[j] = m;
	}

	// INF1: jerarquía de huesos y qué material lleva cada shape.
	const Mtx identity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	model.world.assign(jointCount, identity);
	std::map<int, int> shapeMaterial;
	{
		const size_t inf = sec["INF1"];
		int lastJoint = -1, lastMaterial = -1;
		std::vector<int> openedJoint; // hueso vigente al abrir cada nivel
		for (size_t p = inf + be32(b, inf + 0x14);; p += 4) {
			const uint16_t type = be16(b, p), value = be16(b, p + 2);
			if (type == 0) break;
			if (type == 1) {
				openedJoint.push_back(lastJoint);
			} else if (type == 2) {
				if (openedJoint.empty()) fail("Bad INF1 hierarchy.");
				lastJoint = openedJoint.back();
				openedJoint.pop_back();
			} else if (type == 0x10) {
				if (value >= jointCount) fail("Bad INF1 joint.");
				int parent = -1;
				for (auto it = openedJoint.rbegin(); it != openedJoint.rend(); ++it)
					if (*it >= 0) { parent = *it; break; }
				model.world[value] = parent >= 0 ? mtxMul(model.world[parent], local[value]) : local[value];
				lastJoint          = value;
			} else if (type == 0x11) {
				lastMaterial = value;
			} else if (type == 0x12) {
				shapeMaterial[value] = lastMaterial;
			}
		}
	}

	// EVP1 + DRW1: cada matriz de dibujo es un hueso o una mezcla de huesos.
	struct Influences { int joints[4] = { 0, 0, 0, 0 }; float weights[4] = { 1, 0, 0, 0 }; };
	std::vector<Influences> envelopes;
	{
		const size_t evp = sec["EVP1"];
		const uint16_t count = be16(b, evp + 8);
		size_t cursor = 0;
		for (uint16_t i = 0; i < count; i++) {
			const unsigned n = b.at(evp + be32(b, evp + 0xC) + i);
			std::vector<std::pair<float, int>> row;
			for (unsigned k = 0; k < n; k++, cursor++)
				row.emplace_back(beF(b, evp + be32(b, evp + 0x14) + cursor * 4), be16(b, evp + be32(b, evp + 0x10) + cursor * 2));
			std::stable_sort(row.begin(), row.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
			if (row.size() > 4) row.resize(4);
			float total = 0;
			for (const auto& r : row) total += r.first;
			if (total <= 0) total = 1;
			Influences inf;
			for (size_t k = 0; k < 4; k++) {
				inf.joints[k]  = k < row.size() ? row[k].second : 0;
				inf.weights[k] = k < row.size() ? row[k].first / total : 0.0f;
			}
			envelopes.push_back(inf);
		}
	}
	const size_t drw = sec["DRW1"];
	const uint16_t drwCount = be16(b, drw + 8);
	std::vector<bool> drwWeighted(drwCount);
	std::vector<int> drwIndex(drwCount);
	for (uint16_t i = 0; i < drwCount; i++) {
		drwWeighted[i] = b.at(drw + be32(b, drw + 0xC) + i) != 0;
		drwIndex[i]    = be16(b, drw + be32(b, drw + 0x10) + i * 2);
		if (drwWeighted[i] ? drwIndex[i] >= (int)envelopes.size() : drwIndex[i] >= jointCount) fail("Bad DRW1 entry.");
	}

	// MAT3 + TEX1: textura y colores de cada material.
	const size_t mat = sec["MAT3"], tex = sec["TEX1"];
	model.texNames = j3dNames(b, tex + be32(b, tex + 0x10));
	for (size_t i = 0; i < model.texNames.size(); i++) model.texHeaders.push_back(tex + be32(b, tex + 0xC) + i * 32);
	const uint16_t materialCount = be16(b, mat + 8);
	std::vector<int> materialTexture(materialCount, -1);
	for (uint16_t m = 0; m < materialCount; m++) {
		const size_t e = mat + be32(b, mat + 0xC) + (size_t)be16(b, mat + be32(b, mat + 0x10) + m * 2) * 0x14C;
		const uint16_t slot = be16(b, e + 0x84);
		if (slot != 0xFFFF) materialTexture[m] = be16(b, mat + be32(b, mat + 0x48) + slot * 2);
		std::array<int, 4> tev { 255, 255, 255, 255 }, col { 255, 255, 255, 255 };
		const uint16_t tevIdx = be16(b, e + 0xDC);
		if (tevIdx != 0xFFFF && be32(b, mat + 0x50))
			for (int k = 0; k < 4; k++) tev[k] = std::clamp((int)(int16_t)be16(b, mat + be32(b, mat + 0x50) + tevIdx * 8 + k * 2), 0, 255);
		const uint16_t colIdx = be16(b, e + 0x08);
		if (colIdx != 0xFFFF && be32(b, mat + 0x20))
			for (int k = 0; k < 4; k++) col[k] = b.at(mat + be32(b, mat + 0x20) + colIdx * 4 + k);
		model.tevColor0.push_back(tev);
		model.matColor.push_back(col);
	}

	// VTX1: formatos y arrays (posición, normal, color 0, UV 0).
	const size_t vtx = sec["VTX1"];
	struct Array { size_t at = 0; int type = 0; int shift = 0; int comps = 0; };
	std::map<int, Array> arrays;
	for (size_t p = vtx + be32(b, vtx + 8);; p += 16) {
		const uint32_t attr = be32(b, p);
		if (attr == 0xFF) break;
		const uint32_t cnt = be32(b, p + 4);
		Array a;
		a.type  = (int)be32(b, p + 8);
		a.shift = b[p + 12];
		int slot = -1;
		if (attr == 9) { slot = 0; a.comps = cnt ? 3 : 2; }
		else if (attr == 10) { slot = 1; a.comps = 3; }
		else if (attr == 11) { slot = 3; a.comps = cnt ? 4 : 3; }
		else if (attr == 13) { slot = 5; a.comps = cnt ? 2 : 1; }
		if (slot < 0) continue;
		a.at = vtx + be32(b, vtx + 0xC + slot * 4);
		arrays[(int)attr] = a;
	}
	auto component = [&](const Array& a, size_t index, int k) -> float {
		static const int kSize[] = { 1, 1, 2, 2, 4 };
		if (a.type < 0 || a.type > 4) fail("Bad vertex format.");
		const size_t o = a.at + (index * a.comps + k) * kSize[a.type];
		const float scale = 1.0f / (float)(1 << a.shift);
		switch (a.type) {
		case 0: return b.at(o) * scale;
		case 1: return (int8_t)b.at(o) * scale;
		case 2: return be16(b, o) * scale;
		case 3: return (int16_t)be16(b, o) * scale;
		default: return beF(b, o);
		}
	};
	auto colour = [&](const Array& a, size_t index, unsigned char* out) {
		switch (a.type) {
		case 5: for (int k = 0; k < 4; k++) out[k] = b.at(a.at + index * 4 + k); break; // RGBA8
		case 2: for (int k = 0; k < 3; k++) out[k] = b.at(a.at + index * 4 + k); out[3] = 255; break; // RGBX8
		case 1: for (int k = 0; k < 3; k++) out[k] = b.at(a.at + index * 3 + k); out[3] = 255; break; // RGB8
		case 0: {
			const unsigned v = be16(b, a.at + index * 2);
			out[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
			out[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
			out[2] = (unsigned char)((v & 31) * 255 / 31);
			out[3] = 255;
			break;
		}
		default: out[0] = out[1] = out[2] = out[3] = 255; break;
		}
	};

	// SHP1: listas de display por paquete, llevadas a espacio de modelo.
	const size_t shp = sec["SHP1"];
	const uint16_t shapeCount = be16(b, shp + 8);
	for (uint16_t s = 0; s < shapeCount; s++) {
		const size_t e = shp + be32(b, shp + 0xC) + (size_t)s * 0x28;
		const uint16_t packets = be16(b, e + 2), attrOff = be16(b, e + 4), firstMtx = be16(b, e + 6), firstPacket = be16(b, e + 8);
		std::vector<std::pair<uint32_t, uint32_t>> attrs;
		for (size_t p = shp + be32(b, shp + 0x18) + attrOff;; p += 8) {
			const uint32_t a = be32(b, p);
			if (a == 0xFF) break;
			attrs.emplace_back(a, be32(b, p + 4));
		}
		BmdShape shape;
		shape.material = shapeMaterial.count(s) ? shapeMaterial[s] : -1;
		shape.texture  = shape.material >= 0 && shape.material < materialCount ? materialTexture[shape.material] : -1;
		for (const auto& at : attrs) {
			if (at.first == 13 && arrays.count(13)) shape.hasUv = true;
			if (at.first == 11 && arrays.count(11)) shape.hasColor = true;
		}
		int slots[10] = {};
		for (uint16_t pk = 0; pk < packets; pk++) {
			const size_t group = shp + be32(b, shp + 0x24) + (size_t)(firstMtx + pk) * 8;
			const uint16_t count = be16(b, group + 2);
			const uint32_t first = be32(b, group + 4);
			for (uint16_t k = 0; k < count && k < 10; k++) {
				const uint16_t d = be16(b, shp + be32(b, shp + 0x1C) + (size_t)(first + k) * 2);
				if (d != 0xFFFF) {
					if (d >= drwCount) fail("Bad SHP1 matrix.");
					slots[k] = d;
				}
			}
			const size_t draw = shp + be32(b, shp + 0x28) + (size_t)(firstPacket + pk) * 8;
			const size_t dlSize = be32(b, draw), dlAt = shp + be32(b, shp + 0x20) + be32(b, draw + 4);
			for (size_t p = dlAt; p < dlAt + dlSize;) {
				const unsigned op = b.at(p) & 0xF8;
				if (op == 0) { p++; continue; }
				if (op != 0x90 && op != 0x98 && op != 0xA0) fail("Unsupported primitive.");
				const uint16_t n = be16(b, p + 1);
				p += 3;
				std::vector<BmdVertex> strip;
				for (uint16_t v = 0; v < n; v++) {
					BmdVertex in {};
					in.col[0] = in.col[1] = in.col[2] = in.col[3] = 255;
					int mtx = 0;
					for (const auto& [attr, type] : attrs) {
						size_t index;
						if (type == 1 || type == 2) index = b.at(p++);
						else if (type == 3) { index = be16(b, p); p += 2; }
						else fail("Unsupported attribute type.");
						if (attr == 0) mtx = (int)index / 3;
						else if (attr == 9 && arrays.count(9)) for (int k = 0; k < 3; k++) in.pos[k] = component(arrays[9], index, k);
						else if (attr == 10 && arrays.count(10)) for (int k = 0; k < 3; k++) in.nrm[k] = component(arrays[10], index, k);
						else if (attr == 11 && arrays.count(11)) colour(arrays[11], index, in.col);
						else if (attr == 13 && arrays.count(13)) for (int k = 0; k < 2; k++) in.uv[k] = component(arrays[13], index, k);
					}
					if (mtx < 0 || mtx >= 10) fail("Bad matrix slot.");
					const int d = slots[mtx];
					if (drwWeighted[d]) {
						// Envolvente: la posición ya está en espacio de modelo.
						const Influences& inf = envelopes[drwIndex[d]];
						for (int k = 0; k < 4; k++) { in.joints[k] = inf.joints[k]; in.weights[k] = inf.weights[k]; }
					} else {
						const Mtx& m = model.world[drwIndex[d]];
						float p0[3], n0[3];
						mtxApply(m, in.pos, p0, true);
						mtxApply(m, in.nrm, n0, false);
						std::memcpy(in.pos, p0, sizeof p0);
						std::memcpy(in.nrm, n0, sizeof n0);
						in.joints[0]  = drwIndex[d];
						in.weights[0] = 1.0f;
					}
					float len = std::sqrt(in.nrm[0] * in.nrm[0] + in.nrm[1] * in.nrm[1] + in.nrm[2] * in.nrm[2]);
					if (len > 0) for (float& c : in.nrm) c /= len;
					strip.push_back(in);
				}
				auto emit = [&](int a, int c, int d) {
					int ids[3] = { a, c, d };
					// Orientado como sus normales, igual que el rip Collada.
					const float* A = strip[a].pos; const float* B = strip[c].pos; const float* C = strip[d].pos;
					const float u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
					const float w[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
					const float fn[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
					float dot = 0;
					for (int id : ids) dot += fn[0] * strip[id].nrm[0] + fn[1] * strip[id].nrm[1] + fn[2] * strip[id].nrm[2];
					if (dot < 0) std::swap(ids[1], ids[2]);
					for (int id : ids) shape.tris.push_back(strip[id]);
				};
				if (op == 0x90) for (int v = 0; v + 2 < n; v += 3) emit(v, v + 1, v + 2);
				else if (op == 0x98) for (int v = 0; v + 2 < n; v++) emit(v, v + 1, v + 2);
				else for (int v = 1; v + 1 < n; v++) emit(0, v, v + 1);
			}
		}
		model.shapes.push_back(std::move(shape));
	}
	return model;
}

// Vértices NHM de un shape: huesos renombrados al esqueleto de Pikmin 1.
std::vector<Vertex> nhmVertices(const BmdModel& model, const BmdShape& shape, float uvScale, float uvOffset, Wrap wrap,
                                const float* fixedUv = nullptr)
{
	std::map<std::string, int> jointIndex;
	for (size_t i = 0; i < kJoints.size(); i++) jointIndex[kJoints[i]] = (int)i;
	std::vector<Vertex> out;
	out.reserve(shape.tris.size());
	for (const BmdVertex& in : shape.tris) {
		Vertex v {};
		v[0] = in.pos[0]; v[1] = in.pos[1]; v[2] = in.pos[2];
		v[3] = in.nrm[0]; v[4] = in.nrm[1]; v[5] = in.nrm[2];
		if (fixedUv) {
			v[6] = fixedUv[0];
			v[7] = fixedUv[1];
		} else if (shape.hasUv) {
			v[6] = wrap(in.uv[0] * uvScale + uvOffset);
			v[7] = wrap(in.uv[1] * uvScale + uvOffset);
		} else {
			// Sin UV (cristal con mapa de entorno): de la normal.
			v[6] = wrapClamp(in.nrm[0] * 0.25f + 0.5f);
			v[7] = wrapClamp(-in.nrm[1] * 0.25f + 0.5f);
		}
		for (int k = 0; k < 4; k++) {
			if (in.weights[k] <= 0.0f) continue;
			const std::string& name = model.jointNames.at(in.joints[k]);
			auto ji = jointIndex.find(name);
			if (ji == jointIndex.end()) fail("Unknown joint " + name + ".");
			v[8 + k]  = (float)ji->second;
			v[12 + k] = in.weights[k];
		}
		out.push_back(v);
	}
	return out;
}

std::vector<Bone> bmdBones(const BmdModel& model)
{
	std::vector<Bone> bones;
	for (const std::string& name : kJoints) {
		std::vector<float> inverse(kIdentity);
		for (size_t j = 0; j < model.jointNames.size(); j++)
			if (model.jointNames[j] == name) {
				const Mtx inv = mtxInverseAffine(model.world[j]);
				inverse.assign(inv.begin(), inv.end());
			}
		bones.push_back({ name, inverse });
	}
	return bones;
}

// Capitán (Louie, presidente): cuerpo con paleta *_565, cristal interior
// (naka, sin textura) y casco exterior (soto, helkan_8ia), como buildLouie.
void buildCaptainBmd(const BmdModel& model, const fs::path& output)
{
	std::vector<Vertex> body, naka, soto;
	int bodyTex = -1, sotoTex = -1;
	for (const BmdShape& shape : model.shapes) {
		const std::string name = shape.texture >= 0 ? lower(model.texNames.at(shape.texture)) : "";
		if (name.find("565") != std::string::npos) {
			auto v = nhmVertices(model, shape, 1.0f, 0.0f, wrapClamp);
			body.insert(body.end(), v.begin(), v.end());
			bodyTex = shape.texture;
		} else if (name.find("helkan") != std::string::npos) {
			auto v = nhmVertices(model, shape, 0.5f, 0.5f, wrapClamp); // matriz de textura de soto
			soto.insert(soto.end(), v.begin(), v.end());
			sotoTex = shape.texture;
		} else {
			auto v = nhmVertices(model, shape, 1.0f, 0.0f, wrapClamp);
			naka.insert(naka.end(), v.begin(), v.end());
		}
	}
	if (body.empty() || bodyTex < 0) fail("No captain body in the model.");
	naka = flipWinding(naka);
	// La paleta 8x8 (celdas de 2x2) a 64x64 sin filtrar, para que el
	// bilineal no mezcle celdas.
	Texture palette = model.texture(bodyTex);
	Texture bodyTexture;
	bodyTexture.width = bodyTexture.height = 64;
	bodyTexture.rgba.resize(64 * 64 * 4);
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++) {
			int sx = x * palette.width / 64, sy = y * palette.height / 64;
			std::memcpy(&bodyTexture.rgba[((size_t)y * 64 + x) * 4], &palette.rgba[((size_t)sy * palette.width + sx) * 4], 4);
		}
	Texture nakaTex;
	nakaTex.width = nakaTex.height = 4;
	for (int i = 0; i < 16; i++) nakaTex.rgba.insert(nakaTex.rgba.end(), kLouieNakaRgba, kLouieNakaRgba + 4);
	std::vector<Vertex> suit, head;
	splitHead(body, suit, head);
	std::vector<Part> parts;
	parts.push_back({ suit, bodyTexture, 0 });
	parts.push_back({ head, bodyTexture, kFlagNoTint });
	if (!naka.empty()) parts.push_back({ naka, nakaTex, kFlagNoTint });
	if (!soto.empty() && sotoTex >= 0) parts.push_back({ soto, model.texture(sotoTex).translucent(kLouieSotoAlpha), kFlagNoTint });
	writePack(output, bmdBones(model), parts);
}

// Pikmin de Pikmin 2 (blanco, morado): el cuerpo no tiene textura, su color es
// el del TEV (por el color de vértice si lo hay) y va a una paleta; los ojos
// son la textura de intensidad por el color del TEV de su material.
void buildPikminBmd(const BmdModel& model, const fs::path& output)
{
	std::vector<Part> parts;
	for (const BmdShape& shape : model.shapes) {
		const std::array<int, 4> tev = shape.material >= 0 ? model.tevColor0.at(shape.material) : std::array<int, 4> { 255, 255, 255, 255 };
		if (shape.texture >= 0) {
			Texture eye = model.texture(shape.texture);
			for (size_t i = 0; i < eye.rgba.size(); i += 4) {
				for (int k = 0; k < 3; k++) eye.rgba[i + k] = (unsigned char)(eye.rgba[i + k] * tev[k] / 255);
				eye.rgba[i + 3] = 255;
			}
			parts.push_back({ nhmVertices(model, shape, 1.0f, 0.0f, wrapClamp), eye, kFlagNoTint });
			continue;
		}
		// Muy oscuro (el morado, 28,0,52): en Pikmin 2 la luz lo aclara. Se
		// escala hasta que su canal mayor llegue a 190, conservando el tono
		// (sumarle el color del material, blanquecino, lo desaturaba).
		std::array<int, 4> base = tev;
		const int top = std::max({ base[0], base[1], base[2] });
		if (base[0] + base[1] + base[2] < 200 && top > 0)
			for (int k = 0; k < 3; k++) base[k] = base[k] * 190 / top;
		// Paleta: un texel por color distinto, UV en su centro.
		std::vector<std::array<unsigned char, 3>> palette;
		std::vector<int> paletteOf(shape.tris.size());
		for (size_t i = 0; i < shape.tris.size(); i++) {
			std::array<unsigned char, 3> c;
			for (int k = 0; k < 3; k++)
				c[k] = (unsigned char)(base[k] * (shape.hasColor ? shape.tris[i].col[k] : 255) / 255);
			auto it = std::find(palette.begin(), palette.end(), c);
			if (it == palette.end()) {
				if (palette.size() >= 64) it = palette.begin(); // nunca pasa: pocos colores
				else it = palette.insert(palette.end(), c);
			}
			paletteOf[i] = (int)(it - palette.begin());
		}
		Texture tex;
		tex.width  = (int)std::max<size_t>(palette.size(), 1);
		tex.height = 4;
		tex.rgba.resize((size_t)tex.width * tex.height * 4);
		for (int y = 0; y < tex.height; y++)
			for (int x = 0; x < tex.width; x++) {
				unsigned char* p = &tex.rgba[((size_t)y * tex.width + x) * 4];
				if (!palette.empty()) { p[0] = palette[x][0]; p[1] = palette[x][1]; p[2] = palette[x][2]; }
				p[3] = 255;
			}
		std::vector<Vertex> verts = nhmVertices(model, shape, 1.0f, 0.0f, wrapClamp);
		for (size_t i = 0; i < verts.size(); i++) {
			verts[i][6] = (paletteOf[i] + 0.5f) / tex.width;
			verts[i][7] = 0.5f;
		}
		parts.push_back({ verts, tex, 0 });
	}
	if (parts.empty()) fail("Empty Pikmin model.");
	writePack(output, bmdBones(model), parts);
}

// Bulbmin: un solo shape con textura (S3TC) que se repite.
void buildBulbminBmd(const BmdModel& model, const fs::path& output)
{
	std::vector<Part> parts;
	for (const BmdShape& shape : model.shapes) {
		if (shape.texture < 0) continue;
		parts.push_back({ nhmVertices(model, shape, 1.0f, 0.0f, wrapRepeat), model.texture(shape.texture), kFlagRepeat });
	}
	if (parts.empty()) fail("Bulbmin model has no textured shape.");
	writePack(output, bmdBones(model), parts);
}

// PNG RGBA sin comprimir (deflate "stored"), para pc_art.
void writePng(const fs::path& output, const Texture& t)
{
	auto crc = [](const unsigned char* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
		for (size_t i = 0; i < n; i++) {
			c ^= d[i];
			for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
		}
		return c;
	};
	std::vector<unsigned char> raw;
	for (int y = 0; y < t.height; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), t.rgba.begin() + (size_t)y * t.width * 4, t.rgba.begin() + (size_t)(y + 1) * t.width * 4);
	}
	std::vector<unsigned char> z = { 0x78, 0x01 };
	uint32_t a = 1, b = 0;
	for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
	for (size_t at = 0; at < raw.size() || at == 0;) {
		const size_t n = std::min<size_t>(raw.size() - at, 65535);
		z.push_back(at + n >= raw.size() ? 1 : 0);
		z.push_back((unsigned char)(n & 0xFF)); z.push_back((unsigned char)(n >> 8));
		z.push_back((unsigned char)(~n & 0xFF)); z.push_back((unsigned char)((~n >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
		at += n;
		if (n == 0) break;
	}
	const uint32_t adler = b << 16 | a;
	for (int k = 3; k >= 0; k--) z.push_back((unsigned char)(adler >> (k * 8)));
	std::vector<unsigned char> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
		for (int k = 3; k >= 0; k--) png.push_back((unsigned char)(data.size() >> (k * 8)));
		std::vector<unsigned char> body(type, type + 4);
		body.insert(body.end(), data.begin(), data.end());
		png.insert(png.end(), body.begin(), body.end());
		const uint32_t c = ~crc(body.data(), body.size());
		for (int k = 3; k >= 0; k--) png.push_back((unsigned char)(c >> (k * 8)));
	};
	std::vector<unsigned char> ihdr;
	for (uint32_t v : { (uint32_t)t.width, (uint32_t)t.height })
		for (int k = 3; k >= 0; k--) ihdr.push_back((unsigned char)(v >> (k * 8)));
	ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
	chunk("IHDR", ihdr);
	chunk("IDAT", z);
	chunk("IEND", {});
	std::error_code ec;
	fs::create_directories(output.parent_path(), ec);
	std::ofstream out(output, std::ios::binary | std::ios::trunc);
	if (!out || !out.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size()))
		fail("Could not write " + output.string() + ".");
	std::printf("[HD Models] wrote %s (%dx%d)\n", output.string().c_str(), t.width, t.height);
}

// Una textura BTI suelta (cabecera de 32 bytes) de un archivo RARC.
Texture btiTexture(const std::vector<unsigned char>& bti)
{
	if (bti.size() < 32) fail("Truncated BTI.");
	return decodeGxTexture(bti, be32(bti, 0x1C), bti[0], be16(bti, 2), be16(bti, 4));
}

// Carpeta de Pikmin 2: la variable que pasa el launcher, la ruta que guarda al
// instalarlo o detectarlo, y las ubicaciones junto a Pikmin 1.
fs::path findPikmin2Szs()
{
	std::vector<fs::path> candidates;
	if (const char* env = std::getenv("NECTAR_PIKMIN2_DIR"); env && *env) candidates.emplace_back(env);
	{
		std::ifstream in(pc_pikmin2_dir_file());
		std::string line;
		if (in && std::getline(in, line)) {
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
			if (!line.empty()) candidates.emplace_back(fs::u8path(line));
		}
	}
	candidates.emplace_back("../pikmin2");
	candidates.emplace_back("pikmin2");
	std::error_code ec;
	for (const fs::path& dir : candidates) {
		for (const fs::path& sz : { dir / "assets" / "user" / "Kando" / "piki" / "pikis.szs",
		                            dir / "user" / "Kando" / "piki" / "pikis.szs" }) {
			if (fs::is_regular_file(sz, ec)) return sz;
		}
	}
	return {};
}

} // namespace

// Instala un pack de texturas desde un zip (escritorio): las entradas bajo
// ".../Load/Textures/<pack>/..." van a Load/Textures/<pack>/...; si el zip
// no lleva esa carpeta, todo cuelga de Load/Textures/<nombre del zip>/.
// Misma regla que el instalador Android (TexturePack.java).
int pc_texpack_install_zip(const char* zipPath, char* message, unsigned long messageSize)
{
	auto say = [&](const std::string& text) {
		if (message && messageSize) std::snprintf(message, messageSize, "%s", text.c_str());
	};
	ZipSource zip;
	if (!zipPath || !zip.open(zipPath)) { say("Not a readable .zip file."); return 0; }
	const fs::path root = fs::path("Load") / "Textures";
	const std::string stem = fs::path(zipPath).stem().string();
	std::error_code ec;
	fs::create_directories(root, ec);
	int written = 0;
	for (const ZipSource::Entry& e : zip.entries) {
		if (e.name.empty() || e.name.back() == '/') continue;
		std::string rel;
		const size_t idx = e.lowerName.find("load/textures/");
		if (idx != std::string::npos) rel = e.name.substr(idx + std::strlen("load/textures/"));
		else rel = stem + "/" + e.name;
		if (rel.empty() || rel.find("..") != std::string::npos) continue;
		const fs::path out = root / rel;
		try {
			std::vector<char> bytes = zip.readEntry(e);
			fs::create_directories(out.parent_path(), ec);
			std::ofstream f(out, std::ios::binary);
			if (!f) continue;
			f.write(bytes.data(), (std::streamsize)bytes.size());
			written++;
		} catch (const Error& err) {
			say(err.message);
			return written;
		}
	}
	if (written == 0) say("The zip has no files to install.");
	else say("Texture pack installed (" + std::to_string(written) + " files). Activate it in the list.");
	return written;
}

int pc_hd_models_convert_sources(void)
{
	const fs::path modelsRoot = fs::path("Load") / "Models";
	std::error_code ec;
	if (!fs::is_directory(modelsRoot, ec)) return 0;
	int written = 0;
	for (const fs::directory_entry& entry : fs::directory_iterator(modelsRoot, ec)) {
		if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".zip") {
			ZipSource zip;
			if (!zip.open(entry.path())) {
				std::printf("[HD Models] %s: not a readable zip\n", entry.path().filename().string().c_str());
				continue;
			}
			written += convert(zip, modelsRoot);
		} else if (entry.is_directory(ec)) {
			DirSource dir;
			if (dir.open(entry.path())) written += convert(dir, modelsRoot);
		}
	}
	return written;
}

int pc_hd_models_convert_file(const char* pathStr, int expected, char* message, unsigned long messageSize)
{
	auto say = [&](const char* text) {
		if (message && messageSize) std::snprintf(message, messageSize, "%s", text);
	};
	const fs::path modelsRoot = fs::path("Load") / "Models";
	const fs::path source(pathStr ? pathStr : "");
	std::error_code ec;
	std::unique_ptr<Source> src;
	if (fs::is_directory(source, ec)) {
		auto dir = std::make_unique<DirSource>();
		if (!dir->open(source)) { say("The folder is empty."); return 0; }
		src = std::move(dir);
	} else {
		auto zip = std::make_unique<ZipSource>();
		if (!zip->open(source)) { say("Not a readable .zip file."); return 0; }
		src = std::move(zip);
	}
	int found = -1;
	for (int k = 0; k < (int)(sizeof(kKinds) / sizeof(kKinds[0])); k++)
		if (src->has(kKinds[k].marker)) { found = k; break; }
	if (found < 0) {
		say("Not a known model zip (Olimar, Louie, Louie HD, Pikmin, Bulborb or Dwarf Bulborb).");
		return 0;
	}
	static const char* kNames[] = { "Olimar", "Louie", "Louie HD", "Pikmin", "Bulborb", "Dwarf Bulborb" };
	if (expected >= 0 && expected != found) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "This zip is the %s model, not %s.", kNames[found], kNames[expected]);
		say(buf);
		return 0;
	}
	const Kind& kind = kKinds[found];
	try {
		kind.build(*src, modelsRoot / kind.pack);
	} catch (const Error& e) {
		say(e.message.c_str());
		return 0;
	} catch (const std::exception& e) {
		say(e.what());
		return 0;
	}
	char buf[160];
	std::snprintf(buf, sizeof(buf), "%s HD installed (%d files). Restart to use it.", kNames[found], (int)kind.outputs.size());
	say(buf);
	return (int)kind.outputs.size();
}

std::string pc_pikmin2_dir_file(void)
{
#if defined(_WIN32)
	const char* base = std::getenv("LOCALAPPDATA");
	if (!base || !*base) return std::string();
	return (fs::path(base) / "Open Nectar" / "pikmin2_dir").string();
#else
	fs::path base;
	if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) base = xdg;
	else if (const char* home = std::getenv("HOME"); home && *home) base = fs::path(home) / ".config";
	else return std::string();
	return (base / "open-nectar" / "pikmin2_dir").string();
#endif
}

bool pc_pikmin2_detected(void)
{
	static int sDetected = -1;
	if (sDetected < 0) sDetected = findPikmin2Szs().empty() ? 0 : 1;
	return sDetected == 1;
}

int pc_hd_models_import_pikmin2(void)
{
	const fs::path szs = findPikmin2Szs();
	if (szs.empty()) return 0;
	struct Job {
		const char* model;
		fs::path output;
		void (*build)(const BmdModel&, const fs::path&);
	};
	const fs::path models = fs::path("Load") / "Models";
	const Job jobs[] = {
		{ "orima3.bmd", models / "Louie" / "louie.nhm", buildCaptainBmd },
		{ "syatyou.bmd", models / "Pikmin2" / "president.nhm", buildCaptainBmd },
		{ "piki_p2_white.bmd", models / "Pikmin2" / "piki_white.nhm", buildPikminBmd },
		{ "piki_p2_black.bmd", models / "Pikmin2" / "piki_purple.nhm", buildPikminBmd },
		{ "piki_kochappy.bmd", models / "Pikmin2" / "bulbmin.nhm", buildBulbminBmd },
	};
	std::error_code ec;
	const auto szsTime = fs::last_write_time(szs, ec);
	std::vector<unsigned char> archive;
	int written = 0;
	for (const Job& job : jobs) {
		// Ya hecho y no más viejo que Pikmin 2 (louie.nhm puede venir del rip,
		// que es el mismo modelo).
		if (fs::is_regular_file(job.output, ec) && fs::last_write_time(job.output, ec) >= szsTime) continue;
		try {
			if (archive.empty()) {
				std::printf("[HD Models] Pikmin 2 found: building its models from %s\n", szs.string().c_str());
				std::ifstream in(szs, std::ios::binary);
				std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				archive = yaz0(raw);
			}
			job.build(parseBmd(rarcFile(archive, job.model)), job.output);
			written++;
		} catch (const Error& e) {
			std::printf("[HD Models] Pikmin 2 %s: %s\n", job.model, e.message.c_str());
		} catch (const std::exception& e) {
			std::printf("[HD Models] Pikmin 2 %s: %s\n", job.model, e.what());
		}
	}
	// Retratos del HUD (los de Pikmin 2: el presidente y los Pikmin blanco y
	// morado con su hoja), como el de Louie. Van a Load/Art para pc_art.
	struct Portrait { const char* bti; const char* art; };
	static const Portrait kPortraits[] = {
		{ "president.bti", "coop_portrait_president" },
		{ "wp_l64.bti", "coop_portrait_piki_white" },
		{ "blp_l64.bti", "coop_portrait_piki_purple" },
	};
	const fs::path assets = szs.parent_path().parent_path().parent_path().parent_path();
	fs::path ground = assets / "new_screen" / "eng" / "res_ground.szs";
	if (!fs::is_regular_file(ground, ec)) {
		for (const char* lang : { "spa", "fre", "ger", "ita", "jpn" })
			if (fs::is_regular_file(assets / "new_screen" / lang / "res_ground.szs", ec)) {
				ground = assets / "new_screen" / lang / "res_ground.szs";
				break;
			}
	}
	std::vector<unsigned char> screens;
	for (const Portrait& portrait : kPortraits) {
		const fs::path output = fs::path("Load") / "Art" / (std::string(portrait.art) + ".png");
		if (fs::is_regular_file(output, ec) && fs::last_write_time(output, ec) >= szsTime) continue;
		try {
			if (screens.empty()) {
				std::ifstream in(ground, std::ios::binary);
				if (!in) fail("No res_ground.szs in " + assets.string() + ".");
				std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				screens = yaz0(raw);
			}
			writePng(output, btiTexture(rarcFile(screens, portrait.bti)));
			written++;
		} catch (const Error& e) {
			std::printf("[HD Models] Pikmin 2 %s: %s\n", portrait.bti, e.message.c_str());
		}
	}
	return written;
}
