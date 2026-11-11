#include "nectar_assets/raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace nectar {

namespace {

struct Tex {
	Image img;
	bool cutout = false; // los píxeles con poco alfa no se pintan
};

std::uint8_t clamp8(float v) { return std::uint8_t(std::max(0.0f, std::min(255.0f, v))); }

} // namespace

Image renderModel(const Model& model, const RenderOptions& options)
{
	const std::vector<BakedTri> tris = bakeModel(model);
	if (tris.empty()) return Image();

	// Texturas de cada material (decodificadas una vez).
	std::map<int, Image> texCache;
	std::vector<Tex> mats(model.materials.size());
	for (std::size_t i = 0; i < model.materials.size(); i++) {
		const ModMaterial& m = model.materials[i];
		mats[i].cutout       = (m.flags & (MOD_MAT_ALPHA_TEST | MOD_MAT_ALPHA_BLEND)) != 0;
		if (m.texAttr < 0 || std::size_t(m.texAttr) >= model.texAttrs.size()) continue;
		const int t = model.texAttrs[std::size_t(m.texAttr)].texture;
		if (t < 0 || std::size_t(t) >= model.textures.size()) continue;
		auto it = texCache.find(t);
		if (it == texCache.end()) it = texCache.emplace(t, model.textures[std::size_t(t)].decode()).first;
		mats[i].img = it->second;
	}

	// Vista: giro en Y y luego inclinación en X.
	const float yaw = options.yaw * 3.14159265f / 180.0f, pitch = options.pitch * 3.14159265f / 180.0f;
	const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
	auto view = [&](const Vec3& p) {
		const float x = cy * p.x + sy * p.z, z = -sy * p.x + cy * p.z;
		return Vec3 { x, cp * p.y - sp * z, sp * p.y + cp * z }; // z hacia la cámara
	};

	float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
	for (const BakedTri& t : tris) {
		for (const Vec3& p : t.p) {
			const Vec3 v = view(p);
			minX = std::min(minX, v.x);
			maxX = std::max(maxX, v.x);
			minY = std::min(minY, v.y);
			maxY = std::max(maxY, v.y);
		}
	}
	const int ss   = std::max(1, options.supersample);
	const int size = options.size * ss;
	const float span = std::max(maxX - minX, maxY - minY);
	if (!(span > 0.0f)) return Image();
	const float scale = size * 0.92f / span;
	const float offX = size * 0.5f - (minX + maxX) * 0.5f * scale, offY = size * 0.5f + (minY + maxY) * 0.5f * scale;

	Image img;
	img.resize(size, size);
	std::vector<float> depth(std::size_t(size) * size, -std::numeric_limits<float>::max());
	const Vec3 light { -0.45f, 0.75f, 0.5f }; // en espacio de vista
	const float ll = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);

	for (const BakedTri& t : tris) {
		Vec3 s[3], n[3];
		for (int k = 0; k < 3; k++) {
			const Vec3 v = view(t.p[k]);
			s[k]         = { offX + v.x * scale, offY - v.y * scale, v.z };
			n[k]         = view(t.n[k]);
		}
		const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
		if (std::fabs(area) < 1e-6f) continue;
		const int x0 = std::max(0, int(std::floor(std::min({ s[0].x, s[1].x, s[2].x }))));
		const int x1 = std::min(size - 1, int(std::ceil(std::max({ s[0].x, s[1].x, s[2].x }))));
		const int y0 = std::max(0, int(std::floor(std::min({ s[0].y, s[1].y, s[2].y }))));
		const int y1 = std::min(size - 1, int(std::ceil(std::max({ s[0].y, s[1].y, s[2].y }))));
		const Tex* tex = t.material >= 0 && std::size_t(t.material) < mats.size() ? &mats[std::size_t(t.material)] : nullptr;
		const Rgba8 matCol = t.material >= 0 && std::size_t(t.material) < model.materials.size() ? model.materials[std::size_t(t.material)].shade : Rgba8();
		const bool textured = tex && !tex->img.empty();
		const bool texAdd   = t.material >= 0 && std::size_t(t.material) < model.materials.size() && model.materials[std::size_t(t.material)].texAdditive;
		const bool envMap   = t.material >= 0 && std::size_t(t.material) < model.materials.size() && model.materials[std::size_t(t.material)].envMap;

		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py)) / area;
				const float w1 = ((s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py)) / area;
				const float w2 = 1.0f - w0 - w1;
				if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
				const float z     = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
				float& zb         = depth[std::size_t(y) * size + x];
				if (z <= zb) continue;

				const Vec3 nn { w0 * n[0].x + w1 * n[1].x + w2 * n[2].x, w0 * n[0].y + w1 * n[1].y + w2 * n[2].y,
					            w0 * n[0].z + w1 * n[1].z + w2 * n[2].z };
				const float nl = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
				float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
				if (textured) {
					float u = w0 * t.uv[0].x + w1 * t.uv[1].x + w2 * t.uv[2].x;
					float v = w0 * t.uv[0].y + w1 * t.uv[1].y + w2 * t.uv[2].y;
					if (envMap && nl > 0.0f) { // mapa de entorno: la normal en espacio de vista
						u = 0.5f + 0.5f * nn.x / nl;
						v = 0.5f - 0.5f * nn.y / nl;
					}
					int tx = int(std::floor(u * tex->img.width)) % tex->img.width;
					int ty = int(std::floor(v * tex->img.height)) % tex->img.height;
					if (tx < 0) tx += tex->img.width;
					if (ty < 0) ty += tex->img.height;
					const std::uint8_t* c = tex->img.at(tx, ty);
					r = c[0] / 255.0f;
					g = c[1] / 255.0f;
					b = c[2] / 255.0f;
					a = c[3] / 255.0f;
				} else {
					r = matCol.r / 255.0f;
					g = matCol.g / 255.0f;
					b = matCol.b / 255.0f;
				}
				if (textured && texAdd) {
					r = std::min(1.0f, r + matCol.r / 255.0f);
					g = std::min(1.0f, g + matCol.g / 255.0f);
					b = std::min(1.0f, b + matCol.b / 255.0f);
				} else if (textured) {
					r *= matCol.r / 255.0f;
					g *= matCol.g / 255.0f;
					b *= matCol.b / 255.0f;
				}
				if (tex && tex->cutout && a < 0.5f) continue;
				const float vr = (w0 * t.col[0].r + w1 * t.col[1].r + w2 * t.col[2].r) / 255.0f;
				const float vg = (w0 * t.col[0].g + w1 * t.col[1].g + w2 * t.col[2].g) / 255.0f;
				const float vb = (w0 * t.col[0].b + w1 * t.col[1].b + w2 * t.col[2].b) / 255.0f;
				const float dot   = nl > 0.0f ? std::fabs((nn.x * light.x + nn.y * light.y + nn.z * light.z) / (nl * ll)) : 1.0f;
				const float shade = 0.45f + 0.55f * dot;

				zb               = z;
				std::uint8_t* o  = img.at(x, y);
				o[0]             = clamp8(r * vr * shade * 255.0f);
				o[1]             = clamp8(g * vg * shade * 255.0f);
				o[2]             = clamp8(b * vb * shade * 255.0f);
				o[3]             = 255;
			}
		}
	}
	return ss > 1 ? shrinkToFit(img, options.size, options.size) : img;
}

} // namespace nectar
