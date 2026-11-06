#include "nectar_assets/standin_terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nectar {

namespace {

// ── Deterministic, tiling noise ─────────────────────────────────────────────
// Integer hashing and value noise on a lattice that wraps, so every texture
// tiles. No transcendental functions: the generator's output must not depend on
// the platform's libm.

std::uint32_t hash3(std::uint32_t x, std::uint32_t y, std::uint32_t seed)
{
	std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
	h               = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

float hashUnit(std::uint32_t x, std::uint32_t y, std::uint32_t seed) { return float(hash3(x, y, seed) & 0xFFFFu) / 65535.0f; }

/// Value noise in [0, 1] at (x, y) in lattice units; the lattice repeats every
/// `px` x `py` cells.
float tileNoise(float x, float y, int px, int py, std::uint32_t seed)
{
	const float fx = std::floor(x), fy = std::floor(y);
	float tx = x - fx, ty = y - fy;
	tx           = tx * tx * (3.0f - 2.0f * tx);
	ty           = ty * ty * (3.0f - 2.0f * ty);
	auto wrap    = [](int v, int p) { return std::uint32_t(((v % p) + p) % p); };
	const int ix = int(fx), iy = int(fy);
	const float a = hashUnit(wrap(ix, px), wrap(iy, py), seed), b = hashUnit(wrap(ix + 1, px), wrap(iy, py), seed);
	const float c = hashUnit(wrap(ix, px), wrap(iy + 1, py), seed), d = hashUnit(wrap(ix + 1, px), wrap(iy + 1, py), seed);
	const float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
	return top + (bottom - top) * ty;
}

/// Fractal noise over a `size`-pixel square that tiles: `cells` lattice cells
/// across for the first octave, doubling per octave.
float fbm(float px, float py, int size, int cells, int octaves, std::uint32_t seed)
{
	float sum = 0.0f, amp = 1.0f, norm = 0.0f;
	for (int o = 0; o < octaves; o++) {
		const int c   = cells << o;
		const float k = float(c) / float(size);
		sum += amp * tileNoise(px * k, py * k, c, c, seed + std::uint32_t(o) * 101u);
		norm += amp;
		amp *= 0.5f;
	}
	return sum / norm;
}

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

float smoothstep(float e0, float e1, float v)
{
	const float t = clamp01((v - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}

struct Rgb {
	float r, g, b;
};

Rgb hex(std::uint32_t h) { return { float((h >> 16) & 0xFF), float((h >> 8) & 0xFF), float(h & 0xFF) }; }
Rgb mix(Rgb a, Rgb b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
Rgb scale(Rgb a, float k) { return { a.r * k, a.g * k, a.b * k }; }

/// A float RGB canvas that wraps at its edges (so splats near a border tile).
struct Canvas {
	int w, h;
	std::vector<Rgb> px;
	std::vector<float> alpha;
	Canvas(int width, int height)
	    : w(width), h(height), px(std::size_t(width) * height, Rgb { 0, 0, 0 }), alpha(std::size_t(width) * height, 255.0f)
	{
	}
	Rgb& at(int x, int y) { return px[std::size_t(((y % h) + h) % h) * w + std::size_t(((x % w) + w) % w)]; }
	/// Blends `c` into the four pixels around (x, y) with bilinear weights times `k`.
	void splat(float x, float y, Rgb c, float k)
	{
		const float fx = std::floor(x), fy = std::floor(y);
		const float tx = x - fx, ty = y - fy;
		const int ix = int(fx), iy = int(fy);
		const float wts[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
		const int dx[4] = { 0, 1, 0, 1 }, dy[4] = { 0, 0, 1, 1 };
		for (int i = 0; i < 4; i++) {
			Rgb& p = at(ix + dx[i], iy + dy[i]);
			p      = mix(p, c, clamp01(wts[i] * k));
		}
	}
	void darken(int x, int y, float k)
	{
		Rgb& p = at(x, y);
		p      = scale(p, k);
	}
	Image image() const
	{
		Image img;
		img.resize(w, h);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				const Rgb& p     = px[std::size_t(y) * w + x];
				std::uint8_t* o = img.at(x, y);
				o[0]            = std::uint8_t(std::lround(std::min(255.0f, std::max(0.0f, p.r))));
				o[1]            = std::uint8_t(std::lround(std::min(255.0f, std::max(0.0f, p.g))));
				o[2]            = std::uint8_t(std::lround(std::min(255.0f, std::max(0.0f, p.b))));
				o[3]            = std::uint8_t(std::lround(std::min(255.0f, std::max(0.0f, alpha[std::size_t(y) * w + x]))));
			}
		}
		return img;
	}
};

/// A small deterministic generator for scattering (positions, sizes).
struct Lcg {
	std::uint32_t s;
	float next()
	{
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / float(1u << 24);
	}
	float range(float a, float b) { return a + (b - a) * next(); }
};

// Palette (ART_DIRECTION.md): moss and grass 6E9E3A / 4F7A2A, soil and paths
// 8A6A45 / 6B4F33, stone 8C8577 / 5E574C.
const Rgb kGrassLight = hex(0x6E9E3A), kGrassDark = hex(0x4F7A2A), kGrassWarm = hex(0x86A040);
const Rgb kBladeBase = hex(0x45702A), kBladeTip = hex(0x8DB850);
const Rgb kSoil = hex(0x8A6A45), kSoilDark = hex(0x6B4F33), kSoilBare = hex(0x7C5E3E), kPebble = hex(0xB2A58C);
const Rgb kStone = hex(0x8C8577), kStoneDark = hex(0x5E574C), kSand = hex(0xA39479), kStoneCool = hex(0x7D776C);

// Sandstone strata: layer boundaries in texture rows (row 0 is the bottom of a
// wall: compileMap's wall UVs grow upwards). Fixed thicknesses that sum to 256.
const int kStrata[] = { 22, 14, 30, 18, 26, 12, 34, 20, 16, 28, 36 };

} // namespace

Image standInGrassImage()
{
	constexpr int N = 256;
	Canvas c(N, N);
	std::vector<float> bare(std::size_t(N) * N);
	for (int y = 0; y < N; y++) {
		for (int x = 0; x < N; x++) {
			const float n1 = fbm(float(x), float(y), N, 4, 3, 11u);
			const float n2 = fbm(float(x), float(y), N, 16, 2, 23u);
			const float n3 = fbm(float(x), float(y), N, 8, 2, 37u);
			Rgb col        = mix(kGrassDark, kGrassLight, clamp01(0.15f + 0.75f * n1 + 0.35f * (n2 - 0.5f)));
			col            = mix(col, kGrassWarm, 0.22f * smoothstep(0.45f, 0.8f, n3));
			// Bare patches: soil showing through the grass.
			const float b                   = smoothstep(0.63f, 0.74f, fbm(float(x), float(y), N, 4, 3, 53u));
			bare[std::size_t(y) * N + x]    = b;
			col                             = mix(col, kSoilBare, 0.8f * b);
			const float grain               = (hashUnit(std::uint32_t(x), std::uint32_t(y), 71u) - 0.5f) * 10.0f;
			c.at(x, y)                      = { col.r + grain, col.g + grain, col.b + grain * 0.6f };
		}
	}
	// Soil specks and small pebbles, more of them on bare ground.
	Lcg rng { 0x5EEDu };
	for (int i = 0; i < 1400; i++) {
		const float x = rng.range(0.0f, float(N)), y = rng.range(0.0f, float(N));
		const float b = bare[std::size_t(int(y)) * N + std::size_t(int(x))];
		if (rng.next() > 0.35f + 0.65f * b) continue;
		const Rgb col = rng.next() < 0.5f ? kSoil : kSoilDark;
		c.splat(x, y, col, 0.9f);
		if (rng.next() < 0.3f) c.splat(x + 1.0f, y, col, 0.6f);
	}
	for (int i = 0; i < 70; i++) {
		const int x = int(rng.range(0.0f, float(N))), y = int(rng.range(0.0f, float(N)));
		c.darken(x + 1, y + 1, 0.7f);
		c.darken(x + 2, y + 1, 0.8f);
		c.at(x, y)     = kPebble;
		c.at(x + 1, y) = mix(kPebble, kStone, 0.4f);
	}
	// Clumps: a soft shadow down and to the right (the key light is above-left),
	// then blades fanning out from the centre, darker at the base, lighter at the
	// tip and lightest where they lean towards the light.
	for (int i = 0; i < 230; i++) {
		const float cx = rng.range(0.0f, float(N)), cy = rng.range(0.0f, float(N));
		const float r  = rng.range(4.0f, 9.5f);
		const float b  = bare[std::size_t(int(cy)) * N + std::size_t(int(cx))];
		if (b > 0.5f) continue;
		const int R = int(r * 1.3f) + 2;
		for (int dy = -R; dy <= R; dy++) {
			for (int dx = -R; dx <= R; dx++) {
				const float ex = (float(dx) - 1.6f) / (r * 1.15f), ey = (float(dy) - 2.0f) / (r * 0.95f);
				const float d = ex * ex + ey * ey;
				if (d < 1.0f) c.darken(int(cx) + dx, int(cy) + dy, 1.0f - 0.26f * (1.0f - d));
			}
		}
		const int blades = 9 + int(rng.next() * 6.0f);
		for (int k = 0; k < blades; k++) {
			// Direction on the unit circle without trig: a random point on the square, normalised.
			float vx = rng.range(-1.0f, 1.0f), vy = rng.range(-1.0f, 1.0f);
			const float len0 = std::sqrt(vx * vx + vy * vy);
			if (len0 < 0.15f) continue;
			vx /= len0;
			vy /= len0;
			const float len  = r * rng.range(0.55f, 1.05f);
			const float lean = clamp01(0.5f - 0.5f * (vx + vy) * 0.7071f); // towards the upper left
			const float bx = cx + rng.range(-1.2f, 1.2f), by = cy + rng.range(-1.2f, 1.2f);
			for (float t = 0.0f; t <= 1.0f; t += 0.5f / len) {
				Rgb col = mix(kBladeBase, kBladeTip, t * (0.6f + 0.4f * lean));
				c.splat(bx + vx * len * t, by + vy * len * t, col, 0.85f);
			}
		}
	}
	return c.image();
}

Image standInSoilPathImage()
{
	constexpr int N = 256;
	Canvas c(N, N);
	for (int y = 0; y < N; y++) {
		for (int x = 0; x < N; x++) {
			const float n1 = fbm(float(x), float(y), N, 4, 3, 101u);
			const float n2 = fbm(float(x), float(y), N, 32, 2, 113u);
			Rgb col        = mix(kSoilDark, kSoil, clamp01(0.2f + 0.8f * n1 + 0.3f * (n2 - 0.5f)));
			// The middle of the path is trodden flatter and lighter.
			const float across = std::fabs(float(y) - 127.5f) / 127.5f;
			col                = scale(col, 1.06f - 0.1f * across * across);
			const float grain  = (hashUnit(std::uint32_t(x), std::uint32_t(y), 127u) - 0.5f) * 12.0f;
			c.at(x, y)         = { col.r + grain, col.g + grain, col.b + grain * 0.7f };
			// Ragged edges: grass eats into both sides, unevenly along the path.
			const float edge   = 14.0f + 26.0f * fbm(float(x), 0.0f, N, 8, 2, 131u) + 8.0f * hashUnit(std::uint32_t(x), 7u, 137u);
			const float edge2  = 14.0f + 26.0f * fbm(float(x), 0.0f, N, 8, 2, 139u) + 8.0f * hashUnit(std::uint32_t(x), 9u, 137u);
			const bool cut     = float(y) < edge || float(N - 1 - y) < edge2;
			c.alpha[std::size_t(y) * N + x] = cut ? 0.0f : 255.0f;
		}
	}
	// Fine cracks in the packed soil.
	Lcg rng { 0xD1A7u };
	for (int i = 0; i < 26; i++) {
		float x = rng.range(0.0f, float(N)), y = rng.range(60.0f, 196.0f);
		float vx = rng.range(-1.0f, 1.0f), vy = rng.range(-1.0f, 1.0f);
		for (int s = 0; s < 18 + int(rng.next() * 20.0f); s++) {
			const float l = std::sqrt(vx * vx + vy * vy) + 1e-3f;
			x += vx / l;
			y += vy / l;
			c.splat(x, y, scale(kSoilDark, 0.8f), 0.55f);
			vx += rng.range(-0.6f, 0.6f);
			vy += rng.range(-0.6f, 0.6f);
		}
	}
	// Pebbles with a shadow below-right.
	for (int i = 0; i < 160; i++) {
		const float x = rng.range(0.0f, float(N)), y = rng.range(30.0f, 226.0f);
		const float r = rng.range(0.8f, 2.4f);
		const Rgb col = mix(kPebble, kStone, rng.next());
		for (int dy = -3; dy <= 3; dy++) {
			for (int dx = -3; dx <= 3; dx++) {
				const float d2 = float(dx * dx + dy * dy);
				if (d2 <= (r + 0.8f) * (r + 0.8f)) c.darken(int(x) + dx + 1, int(y) + dy + 1, 0.86f);
			}
		}
		for (int dy = -3; dy <= 3; dy++) {
			for (int dx = -3; dx <= 3; dx++) {
				const float d2 = float(dx * dx + dy * dy);
				if (d2 <= r * r) c.at(int(x) + dx, int(y) + dy) = scale(col, 1.0f - 0.08f * float(dx + dy) / 3.0f);
			}
		}
	}
	// A few grass blades poking in from the edges.
	for (int i = 0; i < 90; i++) {
		const float x = rng.range(0.0f, float(N));
		const bool top = rng.next() < 0.5f;
		const float len = rng.range(4.0f, 12.0f), lean = rng.range(-0.6f, 0.6f);
		for (float t = 0.0f; t <= len; t += 0.5f) {
			const float y = top ? 22.0f + t : float(N) - 23.0f - t;
			const int ix = int(x + lean * t), iy = int(y);
			c.at(ix, iy)                                                                    = mix(kBladeBase, kBladeTip, t / len);
			c.alpha[std::size_t(((iy % N) + N) % N) * N + std::size_t(((ix % N) + N) % N)] = 255.0f;
		}
	}
	return c.image();
}

Image standInSandstoneImage()
{
	constexpr int N = 256;
	Canvas c(N, N);
	int bounds[sizeof(kStrata) / sizeof(kStrata[0]) + 1] = { 0 };
	const int layers = int(sizeof(kStrata) / sizeof(kStrata[0]));
	for (int i = 0; i < layers; i++) bounds[i + 1] = bounds[i] + kStrata[i];
	const Rgb tones[4] = { kStone, kSand, kStoneCool, mix(kStone, kSand, 0.5f) };
	for (int y = 0; y < N; y++) {
		for (int x = 0; x < N; x++) {
			// The strata lines wander a little along x.
			const float wob = 3.0f * (fbm(float(x), float(y) * 0.1f, N, 8, 2, 211u) - 0.5f);
			const float yy  = float(y) + wob;
			int li          = 0;
			while (li < layers - 1 && yy >= float(bounds[li + 1])) li++;
			if (yy < 0.0f) li = layers - 1;
			const float lo = float(bounds[li]), hi = float(bounds[li + 1]);
			const float yl = yy < 0.0f ? yy + float(N) : yy;
			Rgb col        = tones[hash3(std::uint32_t(li), 3u, 223u) % 4u];
			// Streaks along the bedding.
			const float streak = fbm(float(x), float(y) * 6.0f, N, 4, 3, 227u + std::uint32_t(li));
			col                = scale(col, 0.88f + 0.22f * streak);
			// Under each strata line the layer above overhangs: shadow. On top of a
			// layer, where it meets the line, a lit lip.
			const float below = hi - yl, above = yl - lo;
			if (below < 4.0f) col = scale(col, 0.78f + 0.055f * below);
			if (above < 2.5f) col = scale(col, 1.12f - 0.03f * above);
			// The line itself.
			const float dLine = std::min(std::fabs(yl - lo), std::fabs(hi - yl));
			if (dLine < 1.2f) col = mix(col, kStoneDark, 0.75f * (1.0f - dLine / 1.2f));
			const float grain = (hashUnit(std::uint32_t(x), std::uint32_t(y), 233u) - 0.5f) * 14.0f;
			c.at(x, y)        = { col.r + grain, col.g + grain, col.b + grain * 0.9f };
		}
	}
	// Weathering pits and vertical cracks inside the layers.
	Lcg rng { 0x5A4Du };
	for (int i = 0; i < 380; i++) {
		const int x = int(rng.range(0.0f, float(N))), y = int(rng.range(0.0f, float(N)));
		c.darken(x, y, rng.range(0.6f, 0.85f));
	}
	for (int i = 0; i < 34; i++) {
		const int li = int(rng.next() * float(layers)) % layers;
		float x      = rng.range(0.0f, float(N));
		const int y0 = bounds[li + 1] - 1, len = std::max(3, int(float(kStrata[li]) * rng.range(0.4f, 0.95f)));
		for (int s = 0; s < len; s++) {
			x += rng.range(-0.7f, 0.7f);
			c.darken(int(x), y0 - s, 0.62f);
			c.darken(int(x) + 1, y0 - s, 0.9f);
		}
	}
	return c.image();
}

namespace {

/// RGB565 in 4x4 blocks (ModTexFormat 0), big-endian, with a 4x4 ordered dither.
std::vector<std::uint8_t> encodeRgb565(const Image& img)
{
	static const int kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
	std::vector<std::uint8_t> out;
	out.reserve(std::size_t(img.width) * img.height * 2);
	for (int by = 0; by < img.height; by += 4) {
		for (int bx = 0; bx < img.width; bx += 4) {
			for (int y = by; y < by + 4; y++) {
				for (int x = bx; x < bx + 4; x++) {
					const std::uint8_t* p = img.at(x, y);
					const float d         = (float(kBayer[y & 3][x & 3]) + 0.5f) / 16.0f - 0.5f;
					auto q = [&](int v, int bits) {
						const int levels = (1 << bits) - 1;
						const float f    = float(v) / 255.0f * float(levels) + d;
						return std::min(levels, std::max(0, int(std::lround(f))));
					};
					const std::uint16_t v = std::uint16_t((q(p[0], 5) << 11) | (q(p[1], 6) << 5) | q(p[2], 5));
					out.push_back(std::uint8_t(v >> 8));
					out.push_back(std::uint8_t(v));
				}
			}
		}
	}
	return out;
}

ModTexture rgb565Texture(const Image& img)
{
	ModTexture t;
	t.format = MOD_TEX_RGB565;
	t.width  = img.width;
	t.height = img.height;
	t.data   = encodeRgb565(img);
	return t;
}

} // namespace

void addStandInTerrainTextures(Model& m)
{
	const int base = int(m.textures.size());
	m.textures.push_back(rgb565Texture(standInSandstoneImage()));
	m.textures.push_back(rgb565Texture(standInGrassImage()));
	const int attrBase = int(m.texAttrs.size());
	for (int i = 0; i < 2; i++) {
		ModTexAttr a;
		a.texture = base + i;
		a.tiling  = 0; // repeat in S and T (compileMap forces it too)
		m.texAttrs.push_back(a);
	}
	ModMaterial& rock  = m.materials[std::size_t(kStandInRockMaterial)];
	rock.flags         = MOD_MAT_OPAQUE;
	rock.texAttr       = attrBase;
	rock.colour        = { 255, 255, 255, 255 };
	ModMaterial& grass = m.materials[std::size_t(kStandInGrassMaterial)];
	grass.flags        = MOD_MAT_OPAQUE;
	grass.texAttr      = attrBase + 1;
	grass.colour       = { 255, 255, 255, 255 };
}

// ── Dressing meshes ─────────────────────────────────────────────────────────

namespace {

Vec3 sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 mul(Vec3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
Vec3 lerp(Vec3 a, Vec3 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 norm(Vec3 a)
{
	const float l = std::sqrt(dot(a, a));
	return l > 1e-6f ? mul(a, 1.0f / l) : Vec3 { 0, 1, 0 };
}

/// Adds triangle (a, b, c) with its front towards `facing` (the game's rule: the
/// front is clockwise, n = (c - a) x (b - a)).
void addTri(MeshModInput& m, int a, int b, int c, const int (&nrm)[3], const int (&uv)[3], int material, Vec3 facing)
{
	const Vec3 &pa = m.positions[std::size_t(a)], &pb = m.positions[std::size_t(b)], &pc = m.positions[std::size_t(c)];
	const Vec3 n   = cross(sub(pc, pa), sub(pb, pa));
	MeshModTri t;
	const bool flip = dot(n, facing) < 0.0f;
	t.pos[0]        = a;
	t.pos[1]        = flip ? c : b;
	t.pos[2]        = flip ? b : c;
	t.nrm[0]        = nrm[0];
	t.nrm[1]        = flip ? nrm[2] : nrm[1];
	t.nrm[2]        = flip ? nrm[1] : nrm[2];
	t.uv[0]         = uv[0];
	t.uv[1]         = flip ? uv[2] : uv[1];
	t.uv[2]         = flip ? uv[1] : uv[2];
	t.material      = material;
	m.tris.push_back(t);
}

/// A grid of `cols` x `rows` points (row-major) as quads, each corner with its
/// own position, normal and UV index (all three indices equal per point).
void addGrid(MeshModInput& m, int first, int cols, int rows, int material, Vec3 facing)
{
	for (int r = 0; r + 1 < rows; r++) {
		for (int c = 0; c + 1 < cols; c++) {
			const int a = first + r * cols + c, b = a + 1, d = a + cols, e = d + 1;
			addTri(m, a, b, d, { a, b, d }, { a, b, d }, material, facing);
			addTri(m, b, e, d, { b, e, d }, { b, e, d }, material, facing);
		}
	}
}

int addPoint(MeshModInput& m, Vec3 p, Vec3 n, Vec2 uv)
{
	m.positions.push_back(p);
	m.normals.push_back(n);
	m.uvs.push_back(uv);
	return int(m.positions.size()) - 1;
}

// Sandstone UVs: world units per texture repeat. v follows world height, so the
// strata line up across spans.
constexpr float kRockRepeat  = 360.0f;
constexpr float kGrassRepeat = 512.0f; // as compileMap's floor (1 / 512)

} // namespace

MeshModInput standInPathMesh(const std::vector<Vec2>& points, float width, float lift, const std::function<float(float, float)>& ground)
{
	MeshModInput m;
	MeshModMaterial soil;
	soil.texture   = standInSoilPathImage();
	soil.alphaTest = true;
	m.materials.push_back(soil);
	if (points.size() < 2) return m;
	// Catmull-Rom through the points, about every 8 units.
	std::vector<Vec2> line;
	for (std::size_t i = 0; i + 1 < points.size(); i++) {
		const Vec2 p0 = points[i == 0 ? 0 : i - 1], p1 = points[i], p2 = points[i + 1], p3 = points[std::min(points.size() - 1, i + 2)];
		const float dx = p2.x - p1.x, dz = p2.y - p1.y;
		const int steps = std::max(2, int(std::sqrt(dx * dx + dz * dz) / 8.0f));
		for (int s = 0; s < steps; s++) {
			const float t = float(s) / float(steps), t2 = t * t, t3 = t2 * t;
			auto cr = [&](float a, float b, float c, float d) { return 0.5f * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 + (-a + 3.0f * b - 3.0f * c + d) * t3); };
			line.push_back({ cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y) });
		}
	}
	line.push_back(points.back());
	float run = 0.0f;
	const int first = int(m.positions.size());
	for (std::size_t i = 0; i < line.size(); i++) {
		const Vec2 a = line[i == 0 ? 0 : i - 1], b = line[std::min(line.size() - 1, i + 1)];
		float tx = b.x - a.x, tz = b.y - a.y;
		const float tl = std::sqrt(tx * tx + tz * tz);
		tx /= tl;
		tz /= tl;
		if (i > 0) {
			const float dx = line[i].x - line[i - 1].x, dz = line[i].y - line[i - 1].y;
			run += std::sqrt(dx * dx + dz * dz);
		}
		// The width breathes a little along the path.
		const float half = width * 0.5f * (0.85f + 0.3f * tileNoise(run / 90.0f, 0.5f, 1 << 16, 1, 307u));
		for (int side = 0; side < 2; side++) {
			const float k = side == 0 ? -half : half;
			const float x = line[i].x - tz * k, z = line[i].y + tx * k;
			addPoint(m, { x, ground(x, z) + lift, z }, { 0, 1, 0 }, { run / (2.0f * width), float(side) });
		}
	}
	addGrid(m, first, 2, int(line.size()), 0, { 0, 1, 0 });
	return m;
}

MeshModInput standInCliffMesh(const std::vector<StandInCliffSpan>& spans, const StandInCliffStyle& style)
{
	MeshModInput m;
	MeshModMaterial rock, grass;
	rock.texture  = standInSandstoneImage();
	grass.texture = standInGrassImage();
	m.materials.push_back(rock);
	m.materials.push_back(grass);

	// Strata heights in world units (the texture's layer boundaries at kRockRepeat).
	std::vector<float> strata;
	{
		int at = 0;
		for (int t : kStrata) {
			strata.push_back(float(at) / 256.0f * kRockRepeat);
			at += t;
		}
	}
	// Where height y sits inside its stratum: 0 at the line below, 1 at the line above.
	auto inLayer = [&](float y) {
		const float yy = y - std::floor(y / kRockRepeat) * kRockRepeat;
		for (std::size_t i = 0; i < strata.size(); i++) {
			const float lo = strata[i], hi = i + 1 < strata.size() ? strata[i + 1] : kRockRepeat;
			if (yy >= lo && yy < hi) return (yy - lo) / (hi - lo);
		}
		return 0.5f;
	};

	for (const StandInCliffSpan& s : spans) {
		const Vec3 out { s.out.x, 0.0f, s.out.y };
		const Vec3 dirAlong = sub(s.footB, s.footA);
		const float len     = std::sqrt(dirAlong.x * dirAlong.x + dirAlong.z * dirAlong.z);
		if (len < 1.0f) continue;
		// u: distance along the span's line in world space, with one sign per line, so
		// spans on the same line (the map edge comes in pieces) join without a seam.
		float ux = dirAlong.x / len, uz = dirAlong.z / len;
		if (ux < -1e-4f || (std::fabs(ux) <= 1e-4f && uz < 0.0f)) {
			ux = -ux;
			uz = -uz;
		}
		auto uAt = [&](Vec3 p) { return style.seed * 997.0f + 4096.0f + p.x * ux + p.z * uz; };
		// The face's own normal: across the span and up its slope, towards `out`.
		Vec3 nFace = norm(cross(sub(s.footB, s.footA), sub(lerp(s.topA, s.topB, 0.5f), lerp(s.footA, s.footB, 0.5f))));
		if (dot(nFace, out) < 0.0f) nFace = mul(nFace, -1.0f);
		const float hA = s.topA.y - s.footA.y, hB = s.topB.y - s.footB.y;
		const float h  = std::max(hA, hB);
		const int cols = std::max(2, int(len / 18.0f) + 1);
		const int rows = std::max(3, int(h / 6.0f) + 1);

		// Per column: the rim's reach and depth.
		std::vector<float> lipOut(static_cast<std::size_t>(cols)), lipDepth(static_cast<std::size_t>(cols));
		for (int c = 0; c < cols; c++) {
			const float u     = uAt(lerp(s.footA, s.footB, float(c) / float(cols - 1)));
			const float n     = tileNoise(u / 40.0f, 3.5f, 1 << 16, 1, 401u);
			const float n2    = tileNoise(u / 13.0f, 7.5f, 1 << 16, 1, 409u);
			lipOut[std::size_t(c)]   = style.lip * (0.35f + 0.65f * (0.7f * n + 0.3f * n2));
			lipDepth[std::size_t(c)] = std::min(h * 0.5f, style.lipDepth * (0.45f + 0.55f * (0.6f * n2 + 0.4f * n)));
		}

		// The face: from the foot to the rim's underside, pushed out of its plane by
		// rounded strata (proud in the middle of a layer, a groove at each line),
		// knobbly noise and a few deep vertical clefts.
		const int faceFirst = int(m.positions.size());
		std::vector<Vec3> facePts(std::size_t(cols) * rows);
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				const float t   = float(c) / float(cols - 1);
				const Vec3 foot = lerp(s.footA, s.footB, t), top = lerp(s.topA, s.topB, t);
				const float topY = top.y - lipDepth[std::size_t(c)];
				const float v    = float(r) / float(rows - 1);
				// Point on the span's plane at height y.
				const float y    = foot.y + (topY - foot.y) * v;
				const float k    = (top.y - foot.y) > 1e-3f ? (y - foot.y) / (top.y - foot.y) : 0.0f;
				const Vec3 base  = lerp(foot, top, k);
				const float u    = uAt(foot);
				const float layer  = inLayer(y);
				const float bulge  = 4.0f * layer * (1.0f - layer); // 0 at the lines, 1 mid-layer
				const float knobs  = tileNoise(u / 22.0f, y / 16.0f, 1 << 16, 1 << 16, 419u);
				const float cleft  = smoothstep(0.82f, 0.95f, tileNoise(u / 31.0f, y / 140.0f, 1 << 16, 1 << 16, 421u));
				float d            = style.minOut + (style.relief - style.minOut) * clamp01(0.45f * bulge + 0.55f * knobs - 0.6f * cleft);
				if (r == 0) d = style.minOut;
				facePts[std::size_t(r * cols + c)] = add(base, mul(nFace, d));
			}
		}
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				const Vec3 p = facePts[std::size_t(r * cols + c)];
				const Vec3 l = facePts[std::size_t(r * cols + std::max(0, c - 1))], rr = facePts[std::size_t(r * cols + std::min(cols - 1, c + 1))];
				const Vec3 dn = facePts[std::size_t(std::max(0, r - 1) * cols + c)], up = facePts[std::size_t(std::min(rows - 1, r + 1) * cols + c)];
				Vec3 n = norm(cross(sub(rr, l), sub(up, dn)));
				if (dot(n, nFace) < 0.0f) n = mul(n, -1.0f);
				const float u = uAt(lerp(s.footA, s.footB, float(c) / float(cols - 1)));
				addPoint(m, p, n, { u / kRockRepeat, p.y / kRockRepeat });
			}
		}
		addGrid(m, faceFirst, cols, rows, 0, nFace);

		// The rim, per column: the face's top, the underside's front, the front's
		// top and (with `back`) the top's back edge. All at or under the top edge.
		const int rimFirst = int(m.positions.size());
		for (int c = 0; c < cols; c++) {
			const float t   = float(c) / float(cols - 1);
			const Vec3 top  = lerp(s.topA, s.topB, t);
			const float u   = uAt(lerp(s.footA, s.footB, t));
			const Vec3 face = facePts[std::size_t((rows - 1) * cols + c)];
			const float lo  = lipOut[std::size_t(c)], ld = lipDepth[std::size_t(c)];
			const Vec3 under { top.x + out.x * lo, top.y - ld, top.z + out.z * lo };
			const Vec3 front { top.x + out.x * lo, top.y - 0.6f, top.z + out.z * lo };
			const Vec3 crest { top.x + out.x * (lo - 1.5f), top.y, top.z + out.z * (lo - 1.5f) };
			const Vec3 back { top.x - out.x * style.back, top.y, top.z - out.z * style.back };
			// Rock: face top -> underside front -> front top (two quads per column pair).
			addPoint(m, face, mul(Vec3 { 0, -1, 0 }, 1.0f), { u / kRockRepeat, face.y / kRockRepeat });
			addPoint(m, under, norm(add(out, Vec3 { 0, -0.6f, 0 })), { u / kRockRepeat, under.y / kRockRepeat });
			addPoint(m, front, norm(add(out, Vec3 { 0, 0.3f, 0 })), { u / kRockRepeat, front.y / kRockRepeat });
			// Grass on top: crest -> back.
			addPoint(m, crest, { 0, 1, 0 }, { crest.x / kGrassRepeat, crest.z / kGrassRepeat });
			addPoint(m, back, { 0, 1, 0 }, { back.x / kGrassRepeat, back.z / kGrassRepeat });
			addPoint(m, front, norm(add(out, Vec3 { 0, 1.5f, 0 })), { front.x / kGrassRepeat, front.z / kGrassRepeat });
		}
		constexpr int kRim = 6;
		for (int c = 0; c + 1 < cols; c++) {
			const int a = rimFirst + c * kRim, b = rimFirst + (c + 1) * kRim;
			const Vec3 downOut = norm(add(out, Vec3 { 0, -1, 0 }));
			// underside: face top (0) -> under (1)
			addTri(m, a + 0, b + 0, a + 1, { a + 0, b + 0, a + 1 }, { a + 0, b + 0, a + 1 }, 0, downOut);
			addTri(m, b + 0, b + 1, a + 1, { b + 0, b + 1, a + 1 }, { b + 0, b + 1, a + 1 }, 0, downOut);
			// front: under (1) -> front (2)
			addTri(m, a + 1, b + 1, a + 2, { a + 1, b + 1, a + 2 }, { a + 1, b + 1, a + 2 }, 0, out);
			addTri(m, b + 1, b + 2, a + 2, { b + 1, b + 2, a + 2 }, { b + 1, b + 2, a + 2 }, 0, out);
			// grass: front (5) -> crest (3) -> back (4)
			addTri(m, a + 5, b + 5, a + 3, { a + 5, b + 5, a + 3 }, { a + 5, b + 5, a + 3 }, 1, { 0, 1, 0 });
			addTri(m, b + 5, b + 3, a + 3, { b + 5, b + 3, a + 3 }, { b + 5, b + 3, a + 3 }, 1, { 0, 1, 0 });
			addTri(m, a + 3, b + 3, a + 4, { a + 3, b + 3, a + 4 }, { a + 3, b + 3, a + 4 }, 1, { 0, 1, 0 });
			addTri(m, b + 3, b + 4, a + 4, { b + 3, b + 4, a + 4 }, { b + 3, b + 4, a + 4 }, 1, { 0, 1, 0 });
		}
	}
	return m;
}

} // namespace nectar
