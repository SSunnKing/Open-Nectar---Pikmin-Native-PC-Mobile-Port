#include "pc_art.h"

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Texture.h"

// stb_image se compila una sola vez en pc_port/gl/pc_texpack.cpp.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace {

struct Art {
	Texture* texture = nullptr;
	int width = 0, height = 0;
};
std::map<std::string, Art> sArt;

bool readFile(const char* name, std::vector<unsigned char>& bytes)
{
	// Mismas rutas que la capa táctil: assets del APK en Android, árbol de
	// fuentes en escritorio (o PIKMIN_TOUCH_ART).
	std::vector<std::string> candidates;
	candidates.push_back(std::string("art/") + name + ".png");
	if (const char* dir = std::getenv("PIKMIN_TOUCH_ART")) candidates.insert(candidates.begin(), std::string(dir) + "/" + name + ".png");
	candidates.push_back(std::string("pc_port/touch/assets/art/") + name + ".png");
	candidates.push_back(std::string("../pc_port/touch/assets/art/") + name + ".png");
	for (const std::string& path : candidates) {
		SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
		if (!rw) continue;
		const Sint64 size = SDL_RWsize(rw);
		if (size <= 0) { SDL_RWclose(rw); continue; }
		bytes.resize((size_t)size);
		const size_t got = SDL_RWread(rw, bytes.data(), 1, (size_t)size);
		SDL_RWclose(rw);
		if (got == (size_t)size) return true;
	}
	return false;
}

// RGBA8 de GX: tiles 4x4 de 64 bytes, primero 32 bytes AR y luego 32 GB.
unsigned char* encodeRgba8Tiles(const unsigned char* rgba, int w, int h, int paddedW, int paddedH)
{
	const int tilesX = paddedW / 4, tilesY = paddedH / 4;
	unsigned char* out = new unsigned char[(size_t)tilesX * tilesY * 64];
	std::memset(out, 0, (size_t)tilesX * tilesY * 64);
	for (int ty = 0; ty < tilesY; ty++)
		for (int tx = 0; tx < tilesX; tx++) {
			unsigned char* tile = out + ((size_t)ty * tilesX + tx) * 64;
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++) {
					const int px = tx * 4 + x, py = ty * 4 + y;
					unsigned char r = 0, g = 0, b = 0, a = 0;
					if (px < w && py < h) {
						const unsigned char* p = rgba + ((size_t)py * w + px) * 4;
						r = p[0]; g = p[1]; b = p[2]; a = p[3];
					}
					const int i = y * 4 + x;
					tile[i * 2]      = a;
					tile[i * 2 + 1]  = r;
					tile[32 + i * 2] = g;
					tile[33 + i * 2] = b;
				}
		}
	return out;
}

const Art& load(const char* name)
{
	auto it = sArt.find(name);
	if (it != sArt.end()) return it->second;
	Art art;
	std::vector<unsigned char> bytes;
	if (readFile(name, bytes)) {
		int w = 0, h = 0, comp = 0;
		unsigned char* rgba = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
		if (rgba) {
			const int paddedW = (w + 3) & ~3, paddedH = (h + 3) & ~3;
			unsigned char* tiles = encodeRgba8Tiles(rgba, w, h, paddedW, paddedH);
			stbi_image_free(rgba);
			Texture* t = new Texture();
			t->createBuffer(paddedW, paddedH, TEX_FMT_RGBA8, tiles);
			t->mTexFlags = Texture::TEX_CLAMP_S | Texture::TEX_CLAMP_T;
			t->attach(); // inicializa el GXTexObj que usa GXLoadTexObj
			art.texture  = t;
			art.width    = w;
			art.height   = h;
		} else {
			std::printf("[Art] %s: %s\n", name, stbi_failure_reason());
		}
	} else {
		std::printf("[Art] missing: %s\n", name);
	}
	return sArt.emplace(name, art).first->second;
}

} // namespace

Texture* pc_art_texture(const char* name) { return load(name).texture; }

bool pc_art_size(const char* name, int* width, int* height)
{
	const Art& art = load(name);
	if (width) *width = art.width;
	if (height) *height = art.height;
	return art.texture != nullptr;
}
