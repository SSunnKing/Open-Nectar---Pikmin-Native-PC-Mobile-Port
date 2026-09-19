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

void buildOlimar(const Source& src, const fs::path& outDir)
{
	XmlNode root = dae(src, "playerE.dae");
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
	Texture bodyTex = loadTexture(src, "playerE_body.png");
	std::vector<Part> parts;
	parts.push_back({ body, bodyTex, 0 });
	parts.push_back({ head, loadTexture(src, "playerE_head.png"), 0 });
	parts.push_back({ glass, bodyTex.translucent(kGlassAlpha), 0 });
	writePack(outDir / "olimar_hd.nhm", bonesFor(kJoints, skins), parts);
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

} // namespace

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
		say("Not a Pikmin 3 model zip (Olimar, Pikmin, Bulborb or Dwarf Bulborb).");
		return 0;
	}
	static const char* kNames[] = { "Olimar", "Pikmin", "Bulborb", "Dwarf Bulborb" };
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
