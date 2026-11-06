// Write-then-read tests for nectar::writeBti, writeTxe, writeBundle and the
// stand-in textures (issue #67).
//
// Oracles (the game's importBti, importTxe and loadBundle live in graphics.cpp
// and stdSystem.cpp, which do not link into a host test):
//  * header fields: the game's own BtiHeader::read (Texture.h) over the real
//    RamStream, so field offsets and byte order are the game's;
//  * pixels: nectar::decodeBti (the launcher's decoder) and
//    nectar::decodeModTexture, compared against the image that was encoded;
//  * rules: importBti's checks, restated by nectar::checkBti (data offset 0x20,
//    tile multiples, size == 0x20 + calcDataSize) and re-asserted here;
//  * bundles: loadBundle's entry loop (stdSystem.cpp:631-714) transcribed below
//    over the real RamStream::readInt/readString, plus nectar::readBundle.
// If importBti, importTxe or loadBundle change, update the copies. The strongest
// check remains boot_smoke on the stand-in tree (#68): importBti halts on violations.
#include "nectar_assets/bundle.h"
#include "nectar_assets/image.h"
#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/standin_textures.h"
#include "nectar_assets/texture.h"

#include "Common/String.h"
#include "Stream.h"
#include "Texture.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

using Bytes = std::vector<std::uint8_t>;

// Horizontal red ramp, vertical green ramp, opaque, on 5-bit-exact values so
// RGB5A3's opaque mode round-trips exactly.
nectar::Image gradient(int w, int h)
{
	nectar::Image img;
	img.resize(w, h);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			std::uint8_t* p = img.at(x, y);
			p[0]            = std::uint8_t(((x * 31 / (w - 1)) << 3) | ((x * 31 / (w - 1)) >> 2));
			p[1]            = std::uint8_t(((y * 31 / (h - 1)) << 3) | ((y * 31 / (h - 1)) >> 2));
			p[2]            = 0x84;
			p[3]            = 0xFF;
		}
	}
	return img;
}

// 5-bit quantisation of what decodeBti returns for an opaque RGB5A3 texel.
bool samePixels(const nectar::Image& a, const nectar::Image& b)
{
	if (a.width != b.width || a.height != b.height) return false;
	for (std::size_t i = 0; i < a.rgba.size(); i++) {
		if ((a.rgba[i] >> 3) != (b.rgba[i] >> 3)) return false;
	}
	return true;
}

// importBti's header read, with the game's BtiHeader over a RamStream.
struct BtiRead {
	BtiHeader h;
	int pending = 0; // stream position after the header
};

BtiRead readBtiHeader(const Bytes& file)
{
	BtiRead r;
	RamStream s(const_cast<std::uint8_t*>(file.data()), int(file.size()));
	r.h.read(s);
	r.pending = s.getPosition();
	return r;
}

// importBti's two ERROR conditions (graphics.cpp:1272-1297).
bool importBtiAccepts(const Bytes& file)
{
	BtiRead r = readBtiHeader(file);
	int fmt   = r.h.mImageFormat;
	if (fmt != 0 && fmt != 1 && fmt != 2 && fmt != 3 && fmt != 4 && fmt != 5 && fmt != 6 && fmt != 14) return false;
	// getTileSize on the converted TexImgFormat: I4 8x8, I8/IA4 8x4, others 4x4
	int tx = 4, ty = 4;
	if (fmt == 0) tx = ty = 8;
	if (fmt == 1 || fmt == 2) tx = 8, ty = 4;
	if (r.h.mWidth % tx || r.h.mHeight % ty) return false;
	return r.h.mImageDataOffset == sizeof(BtiHeader);
}

// loadBundle's loop (stdSystem.cpp:631-714), recording instead of acting. For
// types 1-3 the real loader hands the stream to a nested reader that consumes
// the payload itself, so "consumed" is `size` here, as the writer promises.
struct LoaderEntry {
	int type;
	std::string name;
	int payloadAt;
	int size;
};
struct LoaderRead {
	std::vector<LoaderEntry> entries;
	int pendingAfter = -1;
};

LoaderRead loadBundleLoop(const Bytes& file)
{
	LoaderRead r;
	RamStream fs(const_cast<std::uint8_t*>(file.data()), int(file.size()));
	int fileCount = fs.readInt();
	for (int i = 0; i < fileCount; i++) {
		int type = fs.readInt();
		int size = fs.readInt();
		String path(0);
		fs.readString(path);
		LoaderEntry e { type, path.mString, fs.getPosition(), size };
		r.entries.push_back(e);
		fs.setPosition(fs.getPosition() + size); // default case; nested readers consume `size` for 1-3
	}
	r.pendingAfter = fs.getPending();
	return r;
}
} // namespace

int main()
{
	std::string err;

	// ---------------------------------------------------------------- .bti RGB5A3
	{
		const nectar::Image src = gradient(16, 8);
		nectar::BtiInput in;
		in.format = nectar::BTI_RGB5A3;
		in.width  = 16;
		in.height = 8;
		in.data   = nectar::encodeRgb5a3(src);
		Bytes bti;
		check(nectar::writeBti(in, bti, err), "write RGB5A3 bti");
		check(bti.size() == 0x20 + 16 * 8 * 2, "bti size is 0x20 + calcDataSize");
		check(nectar::checkBti(bti.data(), bti.size(), err), "checkBti accepts it");
		check(importBtiAccepts(bti), "importBti rules accept it");

		BtiRead r = readBtiHeader(bti);
		check(r.h.mImageFormat == 5, "RGB5A3 is 5 in a bti");
		check(r.h.mWidth == 16 && r.h.mHeight == 8, "bti width and height");
		check(r.h.mImageDataOffset == 0x20, "data offset is 0x20");
		check(r.h.mNumImages == 1 && r.h.mIsMipmapEnabled == 0, "one image, no mipmaps");
		check(r.h.mNumPaletteEntries == 0 && r.h.mPaletteDataOffset == 0, "no palette");
		check(r.h.mWrapS == 0 && r.h.mWrapT == 0, "clamped by default");
		check(r.pending == 0x20, "BtiHeader::read consumes exactly 0x20 bytes");

		nectar::Image back;
		check(nectar::decodeBti(bti.data(), bti.size(), back), "decodeBti reads it");
		check(samePixels(src, back), "RGB5A3 gradient survives write then decode");
		// GX tile order: the first 4x4 tile holds pixels (0..3, 0..3), so byte 0x20 is pixel (0,0).
		check(bti[0x20] == 0x80 && bti[0x21] == 0x10, "pixel (0,0) is the big-endian u16 0x8010 (opaque bit, blue 0x84 >> 3)");

		in.repeatS = true;
		Bytes rep;
		check(nectar::writeBti(in, rep, err) && readBtiHeader(rep).h.mWrapS == 1 && readBtiHeader(rep).h.mWrapT == 0, "wrap flags");
	}

	// ---------------------------------------------------------------- every format: size and rules
	{
		struct Case {
			int fmt, w, h;
			std::size_t bytes;
		};
		const Case cases[] = {
			{ nectar::BTI_I4, 16, 8, 16 * 8 / 2 },    { nectar::BTI_I8, 16, 8, 16 * 8 },     { nectar::BTI_IA4, 16, 8, 16 * 8 },
			{ nectar::BTI_IA8, 8, 8, 8 * 8 * 2 },     { nectar::BTI_RGB565, 8, 8, 8 * 8 * 2 }, { nectar::BTI_RGB5A3, 8, 8, 8 * 8 * 2 },
			{ nectar::BTI_RGBA8, 8, 8, 8 * 8 * 4 },   { nectar::BTI_CMPR, 8, 8, 8 * 8 / 2 },
		};
		for (const Case& c : cases) {
			check(nectar::btiDataBytes(c.fmt, c.w, c.h) == c.bytes, "btiDataBytes matches calcDataSize");
			nectar::BtiInput in;
			in.format = c.fmt;
			in.width  = c.w;
			in.height = c.h;
			in.data.assign(c.bytes, 0x11);
			Bytes bti;
			check(nectar::writeBti(in, bti, err), "write bti");
			check(bti.size() == 0x20 + c.bytes && importBtiAccepts(bti) && nectar::checkBti(bti.data(), bti.size(), err), "rules hold");
			nectar::Image img;
			check(nectar::decodeBti(bti.data(), bti.size(), img) && img.width == c.w && img.height == c.h, "decodeBti reads every format");
		}
		// Tile rules: I4 needs 8x8, I8/IA4 8x4, others 4x4.
		check(nectar::btiDataBytes(nectar::BTI_I4, 4, 4) == 0, "I4 4x4 rejected");
		check(nectar::btiDataBytes(nectar::BTI_I4, 8, 4) == 0, "I4 8x4 rejected");
		check(nectar::btiDataBytes(nectar::BTI_IA4, 8, 2) == 0 && nectar::btiDataBytes(nectar::BTI_IA4, 4, 4) == 0, "IA4 needs 8x4 tiles");
		check(nectar::btiDataBytes(nectar::BTI_IA4, 8, 4) == 32, "IA4 8x4 is one 32-byte tile");
		check(nectar::btiDataBytes(nectar::BTI_RGB5A3, 6, 4) == 0, "RGB5A3 6 wide rejected");
		check(nectar::btiDataBytes(7, 8, 8) == 0 && nectar::btiDataBytes(9, 8, 8) == 0 && nectar::btiDataBytes(15, 8, 8) == 0,
		      "palette and unknown codes rejected");
		check(nectar::btiDataBytes(nectar::BTI_RGB5A3, 0, 8) == 0 && nectar::btiDataBytes(nectar::BTI_RGB5A3, 8, -4) == 0, "empty size rejected");

		nectar::BtiInput bad;
		bad.format = nectar::BTI_RGB5A3;
		bad.width  = 8;
		bad.height = 8;
		bad.data.assign(127, 0);
		Bytes out;
		check(!nectar::writeBti(bad, out, err), "short data rejected");
		bad.data.assign(129, 0);
		check(!nectar::writeBti(bad, out, err), "long data rejected");

		// checkBti catches each violation importBti would halt on.
		nectar::BtiInput ok;
		ok.width  = 8;
		ok.height = 8;
		ok.data.assign(128, 0);
		Bytes good;
		nectar::writeBti(ok, good, err);
		Bytes b = good;
		b[0x1F] = 0x40;
		check(!nectar::checkBti(b.data(), b.size(), err) && !importBtiAccepts(b), "data offset != 0x20 caught");
		b = good;
		b[3] = 6;
		check(!nectar::checkBti(b.data(), b.size(), err) && !importBtiAccepts(b), "non-tile width caught");
		b = good;
		b.push_back(0);
		check(!nectar::checkBti(b.data(), b.size(), err), "trailing byte caught");
		b = good;
		b[0x18] = 2;
		check(!nectar::checkBti(b.data(), b.size(), err), "numImages != 1 caught");
	}

	// ---------------------------------------------------------------- .txe
	{
		const nectar::Image src = gradient(8, 8);
		nectar::TxeInput in;
		in.format = nectar::MOD_TEX_RGB5A3; // 2 in a txe, 5 in a bti
		in.width  = 8;
		in.height = 8;
		in.flags  = 1; // TEX_CLAMP_S
		in.data   = nectar::encodeRgb5a3(src);
		Bytes txe;
		check(nectar::writeTxe(in, txe, err), "write txe");
		check(txe.size() == 0x20 + 8 * 8 * 2, "txe size is 0x20 + calcDataSize");
		check(nectar::checkTxe(txe.data(), txe.size(), err), "checkTxe accepts it");

		// importTxe's header reads (graphics.cpp:1299-1314), over the real RamStream.
		RamStream s(txe.data(), int(txe.size()));
		int w = s.readShort(), h = s.readShort();
		unsigned fmt = (unsigned short)s.readShort();
		s.readShort();
		s.readInt();
		for (int i = 0; i < 10; i++) s.readShort();
		check(w == 8 && h == 8, "txe width and height");
		check((fmt & 0xFF) == 2, "RGB5A3 is 2 in a txe");
		check((fmt >> 8) == 1, "flags land in mTexFlags (fmt >> 8)");
		check(s.getPosition() == 0x20, "header is 32 bytes");

		nectar::Image back;
		check(nectar::decodeModTexture(int(fmt & 0xFF), w, h, txe.data() + 0x20, txe.size() - 0x20, back), "decodeModTexture reads it");
		check(samePixels(src, back), "txe gradient survives write then decode");

		in.flags = 0x100;
		check(!nectar::writeTxe(in, txe, err), "flags beyond the high byte rejected");
		in.flags  = 0;
		in.format = nectar::MOD_TEX_I4;
		in.width  = 4;
		check(!nectar::writeTxe(in, txe, err), "txe I4 with a non-tile width rejected");
		check(nectar::txeDataBytes(nectar::MOD_TEX_CMPR, 8, 8) == 32 && nectar::txeDataBytes(nectar::MOD_TEX_Z8, 8, 8) == 64, "txe sizes (CMPR, Z8)");
	}

	// ---------------------------------------------------------------- bundles
	{
		const Bytes tex  = nectar::standInBti();
		const Bytes bin  = { 1, 2, 3, 4, 5 };
		const Bytes dca  = { 9, 9 };
		const Bytes dck  = { 7 };
		const std::vector<nectar::BundleEntry> entries = {
			{ nectar::BUNDLE_BINARY, "effects/pcr/a.pcr", bin },
			{ nectar::BUNDLE_BTI, "effects/tex/a.bti", tex },
			{ nectar::BUNDLE_DCA, "dca", dca },
			{ nectar::BUNDLE_DCK, "dck", dck },
			{ nectar::BUNDLE_BINARY, "empty", {} },
		};
		Bytes bun;
		check(nectar::writeBundle(entries, bun, err), "write bundle");

		// Layout by hand: count, then type, size, name len, name, payload; no padding.
		Bytes expect = { 0, 0, 0, 5 };
		auto be      = [&](std::uint32_t v) {
            for (int s = 24; s >= 0; s -= 8) expect.push_back(std::uint8_t(v >> s));
		};
		for (const nectar::BundleEntry& e : entries) {
			be(std::uint32_t(e.type));
			be(std::uint32_t(e.payload.size()));
			be(std::uint32_t(e.name.size()));
			expect.insert(expect.end(), e.name.begin(), e.name.end());
			expect.insert(expect.end(), e.payload.begin(), e.payload.end());
		}
		check(bun == expect, "bundle bytes match the documented layout (no NUL, no padding)");

		// The game's loop sees the same entries, and the last one ends at EOF.
		LoaderRead g = loadBundleLoop(bun);
		check(g.entries.size() == entries.size(), "loader entry count");
		for (std::size_t i = 0; i < g.entries.size() && i < entries.size(); i++) {
			check(g.entries[i].type == entries[i].type && g.entries[i].name == entries[i].name, "loader type and name");
			check(g.entries[i].size == int(entries[i].payload.size()), "loader size is the exact payload length");
			check(std::memcmp(bun.data() + g.entries[i].payloadAt, entries[i].payload.data(), entries[i].payload.size()) == 0, "payload follows the name");
		}
		check(g.pendingAfter == 0, "last entry ends exactly at the end of the file");

		std::vector<nectar::BundleEntry> back;
		check(nectar::readBundle(bun.data(), bun.size(), back, err), "readBundle");
		check(back.size() == entries.size(), "readBundle count");
		for (std::size_t i = 0; i < back.size() && i < entries.size(); i++) {
			check(back[i].type == entries[i].type && back[i].name == entries[i].name && back[i].payload == entries[i].payload, "readBundle round trip");
		}
		// A type-1 entry is a whole .bti: it still passes importBti's rules once read back.
		check(nectar::checkBti(back[1].payload.data(), back[1].payload.size(), err), "BTI entry is a whole valid .bti");

		// Empty bundle is valid: just a zero count.
		Bytes none;
		check(nectar::writeBundle({}, none, err) && none == Bytes({ 0, 0, 0, 0 }), "count-0 bundle");
		check(nectar::readBundle(none.data(), none.size(), back, err) && back.empty(), "count-0 bundle reads back");

		// Rejections.
		Bytes junk;
		check(!nectar::writeBundle({ { 4, "x", {} } }, junk, err), "unknown type rejected");
		check(!nectar::writeBundle({ { nectar::BUNDLE_BINARY, "", {} } }, junk, err), "empty name rejected");
		check(!nectar::writeBundle({ { nectar::BUNDLE_BINARY, "x", {} }, { nectar::BUNDLE_BTI, "x", {} } }, junk, err), "duplicate name rejected");
		Bytes cut(bun.begin(), bun.end() - 1);
		check(!nectar::readBundle(cut.data(), cut.size(), back, err), "truncated bundle rejected by the reader");
		Bytes extra = bun;
		extra.push_back(0);
		check(!nectar::readBundle(extra.data(), extra.size(), back, err), "trailing byte rejected by the reader");
	}

	// ---------------------------------------------------------------- stand-ins
	{
		Bytes ft = nectar::standInConsFontBti();
		BtiRead f = readBtiHeader(ft);
		check(f.h.mImageFormat == 2 && f.h.mWidth == 128 && f.h.mHeight == 64, "consFont is IA4 128x64");
		check(f.h.mWidth % 16 == 0 && f.h.mHeight % 8 == 0 && f.h.mHeight >= 8, "consFont fits the 16x8 glyph grid");
		check(importBtiAccepts(ft) && nectar::checkBti(ft.data(), ft.size(), err), "consFont passes importBti's rules");
		nectar::Image fi;
		check(nectar::decodeBti(ft.data(), ft.size(), fi), "consFont decodes");
		bool opaque = true;
		for (std::size_t i = 3; i < fi.rgba.size(); i += 4) opaque = opaque && fi.rgba[i] == 0xFF;
		check(opaque, "consFont alpha is 0xFF everywhere (nonzero glyph widths)");

		Bytes g = nectar::standInBti();
		BtiRead gr = readBtiHeader(g);
		check(gr.h.mImageFormat == 5 && gr.h.mWidth == 8 && gr.h.mHeight == 8, "generic stand-in is RGB5A3 8x8");
		check(importBtiAccepts(g) && nectar::checkBti(g.data(), g.size(), err), "generic stand-in passes importBti's rules");
		nectar::Image gi;
		check(nectar::decodeBti(g.data(), g.size(), gi) && gi.rgba[3] == 0xFF, "generic stand-in is opaque");

		Bytes sh = nectar::standInShadowTxe();
		check(nectar::checkTxe(sh.data(), sh.size(), err), "shadow.txe passes the txe rules");
		check(sh[4] == 0 && sh[5] == 2, "shadow.txe format short is RGB5A3 with flags 0");

		// The names: 54, unique, exact loadTexture keys.
		const std::vector<std::string>& names = nectar::effectTextureNames();
		check(names.size() == 54, "54 literal effect texture names");
		std::set<std::string> uniq(names.begin(), names.end());
		check(uniq.size() == names.size(), "effect texture names are unique");
		bool shape = true;
		for (const std::string& n : names) {
			shape = shape && n.rfind("effects/tex/", 0) == 0 && n.size() > 16 && n.compare(n.size() - 4, 4, ".bti") == 0;
		}
		check(shape, "names are effects/tex/*.bti");
		check(uniq.count("effects/tex/ps_glow.bti") && uniq.count("effects/tex/hokori4.bti"), "boot-critical textures are listed");

		Bytes bun;
		check(nectar::standInEffectTexBundle(bun, err), "effects/tex bundle");
		LoaderRead lr = loadBundleLoop(bun);
		check(lr.entries.size() == 54 && lr.pendingAfter == 0, "loader sees 54 entries ending at EOF");
		std::vector<nectar::BundleEntry> ents;
		check(nectar::readBundle(bun.data(), bun.size(), ents, err), "effects/tex bundle reads back");
		bool all = ents.size() == 54;
		for (std::size_t i = 0; i < ents.size() && i < names.size(); i++) {
			all = all && ents[i].type == nectar::BUNDLE_BTI && ents[i].name == names[i] && nectar::checkBti(ents[i].payload.data(), ents[i].payload.size(), err);
		}
		check(all, "each entry is a type-1 whole valid .bti under its exact lookup name");

		std::vector<nectar::StandInFile> files;
		check(nectar::standInTextureFiles(files, err), "standInTextureFiles");
		std::set<std::string> paths;
		for (const nectar::StandInFile& f : files) {
			paths.insert(f.path);
			const std::string& p = f.path;
			if (p.size() > 4 && p.compare(p.size() - 4, 4, ".bti") == 0) {
				check(nectar::checkBti(f.bytes.data(), f.bytes.size(), err), "loose .bti is valid");
			} else if (p.size() > 4 && p.compare(p.size() - 4, 4, ".txe") == 0) {
				check(nectar::checkTxe(f.bytes.data(), f.bytes.size(), err), "loose .txe is valid");
			} else {
				std::vector<nectar::BundleEntry> b;
				check(nectar::readBundle(f.bytes.data(), f.bytes.size(), b, err), "bundle reads back");
			}
		}
		check(paths.size() == files.size(), "stand-in paths are unique");
		check(paths.count("consFont.bti") && paths.count("effects/shadow.txe") && paths.count("intro/nintendo.bti")
		          && paths.count("effects/tex/effects.bun") && paths.count("screen/eng_tex/screen.bun"),
		      "required boot files present");
		check(paths.count("intro/map0.bti") && paths.count("intro/map1.bti"), "the area banners of the Impact Site (tutorial stage) and the Forest of Hope");
		check(!paths.count("effects/halowhit.txe") && !paths.count("effects/pcr/effects.bun"), "halowhit stays absent; pcr bundle belongs to #72");
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("texture_bundle_writer_test: ok\n");
	return 0;
}
