#include "renderer.h"

#include "nectar_assets/image.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace nm {

namespace {

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aCol;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNrm;
out vec2 vUv;
out vec4 vCol;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
	vNrm = mat3(uModel) * aNrm;
	vUv = aUv;
	vCol = aCol;
}
)";

const char* kFragment = R"(#version 330 core
in vec3 vNrm;
in vec2 vUv;
in vec4 vCol;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec4 uTint;
uniform float uCut;
uniform float uLit;
uniform int uEnvMap;
out vec4 outColor;
void main() {
	vec4 c = vCol * uTint;
	vec2 uv = vUv;
	if (uEnvMap != 0 && length(vNrm) > 0.0) { vec3 nn = normalize(vNrm); uv = vec2(0.5 + 0.5 * nn.x, 0.5 - 0.5 * nn.y); }
	if (uUseTex == 1) c *= texture(uTex, uv);
	if (uUseTex == 2) { vec4 t = texture(uTex, uv); c = vec4(min(vec3(1.0), c.rgb + t.rgb), c.a * t.a); }
	if (c.a < uCut) discard;
	vec3 l = normalize(vec3(-0.45, 0.8, 0.4));
	float n = length(vNrm) > 0.0 ? abs(dot(normalize(vNrm), l)) : 1.0;
	float shade = mix(1.0, 0.5 + 0.6 * n, uLit);
	outColor = vec4(c.rgb * shade, c.a);
}
)";

GLuint compile(GLenum type, const char* src, std::string& error)
{
	const GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		error = log;
		glDeleteShader(s);
		return 0;
	}
	return s;
}

GLuint uploadImage(const nectar::Image& img, bool repeat)
{
	if (img.empty()) return 0;
	GLuint t = 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	return t;
}

} // namespace

GpuModel::~GpuModel()
{
	for (Part& p : parts) {
		glDeleteBuffers(1, &p.vbo);
		glDeleteVertexArrays(1, &p.vao);
	}
	if (!textures.empty()) glDeleteTextures(GLsizei(textures.size()), textures.data());
}

void RenderTarget::resize(int w, int h)
{
	w = std::max(w, 1);
	h = std::max(h, 1);
	if (w == width && h == height && fbo) return;
	width  = w;
	height = h;
	if (!fbo) {
		glGenFramebuffers(1, &fbo);
		glGenTextures(1, &color);
		glGenRenderbuffers(1, &depth);
	}
	glBindTexture(GL_TEXTURE_2D, color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderTarget::~RenderTarget()
{
	if (fbo) {
		glDeleteFramebuffers(1, &fbo);
		glDeleteTextures(1, &color);
		glDeleteRenderbuffers(1, &depth);
	}
}

bool Renderer::init(std::string& error)
{
	const GLuint vs = compile(GL_VERTEX_SHADER, kVertex, error);
	if (!vs) return false;
	const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment, error);
	if (!fs) return false;
	program_ = glCreateProgram();
	glAttachShader(program_, vs);
	glAttachShader(program_, fs);
	glLinkProgram(program_);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program_, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
		error = log;
		return false;
	}
	uMvp_    = glGetUniformLocation(program_, "uMvp");
	uModel_  = glGetUniformLocation(program_, "uModel");
	uTint_   = glGetUniformLocation(program_, "uTint");
	uUseTex_ = glGetUniformLocation(program_, "uUseTex");
	uEnvMap_ = glGetUniformLocation(program_, "uEnvMap");
	uCut_    = glGetUniformLocation(program_, "uCut");
	uLit_    = glGetUniformLocation(program_, "uLit");
	uTex_    = glGetUniformLocation(program_, "uTex");

	// Cubo unidad.
	std::vector<Vertex> box;
	const float f[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	for (const auto& n : f) {
		const V3 N(n[0], n[1], n[2]);
		const V3 u = std::fabs(N.y) > 0.5f ? V3(1, 0, 0) : V3(0, 1, 0);
		const V3 v = N.cross(u);
		const V3 c = N * 0.5f;
		const V3 q[4] = { c - u * 0.5f - v * 0.5f, c + u * 0.5f - v * 0.5f, c + u * 0.5f + v * 0.5f, c - u * 0.5f + v * 0.5f };
		for (int k : { 0, 1, 2, 0, 2, 3 }) box.push_back({ { q[k].x, q[k].y, q[k].z }, { N.x, N.y, N.z }, { 0, 0 }, { 255, 255, 255, 255 } });
	}
	boxVao_   = makeMesh(box, boxVbo_);
	boxCount_ = int(box.size());

	// Cilindro de 24 lados.
	std::vector<Vertex> cyl;
	constexpr int kSides = 24;
	for (int i = 0; i < kSides; i++) {
		const float a0 = i * 6.2831853f / kSides, a1 = (i + 1) * 6.2831853f / kSides;
		const V3 p0(std::cos(a0), 0, std::sin(a0)), p1(std::cos(a1), 0, std::sin(a1));
		const Vertex s[6] = { { { p0.x, 0, p0.z }, { p0.x, 0, p0.z }, { 0, 0 }, { 255, 255, 255, 255 } },
			                  { { p1.x, 0, p1.z }, { p1.x, 0, p1.z }, { 0, 0 }, { 255, 255, 255, 255 } },
			                  { { p1.x, 1, p1.z }, { p1.x, 0, p1.z }, { 0, 0 }, { 255, 255, 255, 255 } },
			                  { { p0.x, 0, p0.z }, { p0.x, 0, p0.z }, { 0, 0 }, { 255, 255, 255, 255 } },
			                  { { p1.x, 1, p1.z }, { p1.x, 0, p1.z }, { 0, 0 }, { 255, 255, 255, 255 } },
			                  { { p0.x, 1, p0.z }, { p0.x, 0, p0.z }, { 0, 0 }, { 255, 255, 255, 255 } } };
		cyl.insert(cyl.end(), s, s + 6);
		const Vertex top[3] = { { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0 }, { 255, 255, 255, 255 } },
			                    { { p1.x, 1, p1.z }, { 0, 1, 0 }, { 0, 0 }, { 255, 255, 255, 255 } },
			                    { { p0.x, 1, p0.z }, { 0, 1, 0 }, { 0, 0 }, { 255, 255, 255, 255 } } };
		cyl.insert(cyl.end(), top, top + 3);
	}
	cylVao_   = makeMesh(cyl, cylVbo_);
	cylCount_ = int(cyl.size());
	dynVao_   = makeMesh({}, dynVbo_);
	return true;
}

GLuint Renderer::makeMesh(const std::vector<Vertex>& v, GLuint& vbo)
{
	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(Vertex)), v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, p)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, n)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, c)));
	glBindVertexArray(0);
	return vao;
}

std::shared_ptr<GpuModel> Renderer::upload(const nectar::Model& model)
{
	auto out = std::make_shared<GpuModel>();
	const std::vector<nectar::BakedTri> tris = nectar::bakeModel(model);
	std::map<int, std::vector<Vertex>> byMaterial;
	bool first = true;
	for (const nectar::BakedTri& t : tris) {
		std::vector<Vertex>& v = byMaterial[t.material];
		for (int k = 0; k < 3; k++) {
			v.push_back({ { t.p[k].x, t.p[k].y, t.p[k].z }, { t.n[k].x, t.n[k].y, t.n[k].z }, { t.uv[k].x, t.uv[k].y }, { t.col[k].r, t.col[k].g, t.col[k].b, t.col[k].a } });
			const V3 p(t.p[k].x, t.p[k].y, t.p[k].z);
			if (first) {
				out->boundsMin = out->boundsMax = p;
				first                           = false;
			}
			out->boundsMin = { std::min(out->boundsMin.x, p.x), std::min(out->boundsMin.y, p.y), std::min(out->boundsMin.z, p.z) };
			out->boundsMax = { std::max(out->boundsMax.x, p.x), std::max(out->boundsMax.y, p.y), std::max(out->boundsMax.z, p.z) };
		}
	}
	std::map<int, GLuint> texOf;
	for (auto& kv : byMaterial) {
		GpuModel::Part part;
		part.vao   = makeMesh(kv.second, part.vbo);
		part.count = int(kv.second.size());
		if (kv.first >= 0 && std::size_t(kv.first) < model.materials.size()) {
			const nectar::ModMaterial& m = model.materials[std::size_t(kv.first)];
			part.cutout                  = (m.flags & nectar::MOD_MAT_ALPHA_TEST) != 0;
			part.blend                   = (m.flags & nectar::MOD_MAT_ALPHA_BLEND) != 0;
			part.colour[0]               = m.shade.r / 255.0f;
			part.colour[1]               = m.shade.g / 255.0f;
			part.colour[2]               = m.shade.b / 255.0f;
			part.texAdd                  = m.texAdditive;
			part.envMap                  = m.envMap;
			if (m.texAttr >= 0 && std::size_t(m.texAttr) < model.texAttrs.size()) {
				const int t = model.texAttrs[std::size_t(m.texAttr)].texture;
				if (t >= 0 && std::size_t(t) < model.textures.size()) {
					auto it = texOf.find(t);
					if (it == texOf.end()) {
						const GLuint id = uploadImage(model.textures[std::size_t(t)].decode(), true);
						it              = texOf.emplace(t, id).first;
						if (id) out->textures.push_back(id);
					}
					part.tex = it->second;
				}
			}
		}
		out->parts.push_back(part);
	}
	// Opacos primero, luego recortes y al final las transparentes.
	std::stable_sort(out->parts.begin(), out->parts.end(), [](const GpuModel::Part& a, const GpuModel::Part& b) {
		return (a.blend ? 2 : a.cutout ? 1 : 0) < (b.blend ? 2 : b.cutout ? 1 : 0);
	});
	return out;
}

void Renderer::begin(RenderTarget& target, const M4& view, const M4& proj, const Rgba& clear)
{
	glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
	glViewport(0, 0, target.width, target.height);
	glClearColor(clear.r, clear.g, clear.b, clear.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	viewProj_ = proj * view;
	glUseProgram(program_);
	glUniform1i(uTex_, 0);
	glActiveTexture(GL_TEXTURE0);
}

void Renderer::end()
{
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_DEPTH_TEST);
}

void Renderer::setup(const M4& model, const Rgba& tint, int tex, float cut, bool lit)
{
	const M4 mvp = viewProj_ * model;
	glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
	glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
	glUniform4f(uTint_, tint.r, tint.g, tint.b, tint.a);
	glUniform1i(uUseTex_, tex);
	glUniform1i(uEnvMap_, 0);
	glUniform1f(uCut_, cut);
	glUniform1f(uLit_, lit ? 1.0f : 0.0f);
}

void Renderer::drawModel(const GpuModel& m, const M4& model, const Rgba& tint)
{
	for (const GpuModel::Part& p : m.parts) {
		setup(model, Rgba { tint.r * p.colour[0], tint.g * p.colour[1], tint.b * p.colour[2], tint.a }, p.tex ? (p.texAdd ? 2 : 1) : 0, (p.cutout || p.blend) ? 0.3f : 0.0f, true);
		glUniform1i(uEnvMap_, p.envMap ? 1 : 0);
		glBindTexture(GL_TEXTURE_2D, p.tex);
		glDepthMask(p.blend || tint.a < 1 ? GL_FALSE : GL_TRUE);
		glBindVertexArray(p.vao);
		glDrawArrays(GL_TRIANGLES, 0, p.count);
	}
	glDepthMask(GL_TRUE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::drawBox(const M4& model, const Rgba& c)
{
	setup(model, c, false, 0.0f, true);
	glDepthMask(c.a < 1 ? GL_FALSE : GL_TRUE);
	glBindVertexArray(boxVao_);
	glDrawArrays(GL_TRIANGLES, 0, boxCount_);
	glDepthMask(GL_TRUE);
}

void Renderer::drawCylinder(const M4& model, const Rgba& c)
{
	setup(model, c, false, 0.0f, true);
	glDepthMask(c.a < 1 ? GL_FALSE : GL_TRUE);
	glBindVertexArray(cylVao_);
	glDrawArrays(GL_TRIANGLES, 0, cylCount_);
	glDepthMask(GL_TRUE);
}

void Renderer::drawDynamic(const std::vector<Vertex>& v, GLenum mode)
{
	if (v.empty()) return;
	glBindVertexArray(dynVao_);
	glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(Vertex)), v.data(), GL_STREAM_DRAW);
	glDrawArrays(mode, 0, GLsizei(v.size()));
}

void Renderer::drawLines(const std::vector<V3>& points, const Rgba& c)
{
	std::vector<Vertex> v;
	v.reserve(points.size());
	for (const V3& p : points) v.push_back({ { p.x, p.y, p.z }, { 0, 0, 0 }, { 0, 0 }, { 255, 255, 255, 255 } });
	setup(M4(), c, false, 0.0f, false);
	drawDynamic(v, GL_LINES);
}

void Renderer::drawTriangles(const std::vector<V3>& points, const Rgba& c, bool lit)
{
	std::vector<Vertex> v;
	v.reserve(points.size());
	for (std::size_t i = 0; i + 2 < points.size(); i += 3) {
		const V3 n = (points[i + 1] - points[i]).cross(points[i + 2] - points[i]).normalized();
		for (int k = 0; k < 3; k++) {
			const V3& p = points[i + std::size_t(k)];
			v.push_back({ { p.x, p.y, p.z }, { n.x, n.y, n.z }, { 0, 0 }, { 255, 255, 255, 255 } });
		}
	}
	setup(M4(), c, false, 0.0f, lit);
	glDepthMask(c.a < 1 ? GL_FALSE : GL_TRUE);
	drawDynamic(v, GL_TRIANGLES);
	glDepthMask(GL_TRUE);
}

GLuint uploadTexture(const nectar::Image& img, bool repeat) { return uploadImage(img, repeat); }

GLuint loadPngTexture(const std::string& path, int* w, int* h)
{
	int x = 0, y = 0, comp = 0;
	unsigned char* data = stbi_load(path.c_str(), &x, &y, &comp, 4);
	if (!data) return 0;
	nectar::Image img;
	img.width  = x;
	img.height = y;
	img.rgba.assign(data, data + std::size_t(x) * y * 4);
	stbi_image_free(data);
	if (w) *w = x;
	if (h) *h = y;
	return uploadImage(img, false);
}

} // namespace nm
