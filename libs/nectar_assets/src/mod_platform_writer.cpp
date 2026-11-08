// writePlatformMod (mod_writer.h): writeMod plus a 0x100 collision chunk.
//
// The chunk is what BaseShape::read reads for BaseShapeChunk::CollisionPrism
// (src/sysCommon/shapeBase.cpp): the triangle count and room count, padding
// to 32, one RoomInfo (an int) per room, padding to 32, then 40 bytes per
// triangle (BaseCollTriInfo::read, Collision.h: code, three vertex indices,
// room, three neighbours as s16, the plane), then padding to 32. Paddings are
// relative to the file start, as Stream::skipPadding counts them; chunks start
// on a 32-byte boundary, so they are relative to the chunk start too.
#include "nectar_assets/mod_writer.h"

#include <cmath>
#include <cstring>

namespace nectar {

namespace {

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at)
{
	return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
	for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
}

void putF(std::vector<std::uint8_t>& b, float f)
{
	std::uint32_t v;
	std::memcpy(&v, &f, 4);
	put32(b, v);
}

void put16(std::vector<std::uint8_t>& b, int v)
{
	b.push_back(std::uint8_t(std::uint16_t(v) >> 8));
	b.push_back(std::uint8_t(v));
}

void pad32(std::vector<std::uint8_t>& b, std::size_t base)
{
	while ((b.size() + base) % 32) b.push_back(0);
}

} // namespace

bool writePlatformMod(const Model& model, std::vector<std::uint8_t>& out, std::string& error)
{
	out.clear();
	if (model.collision.empty()) {
		error = "platform .mod without collision triangles";
		return false;
	}
	Model visual = model;
	visual.collision.clear();
	visual.collRooms = 0;
	visual.hasGrid   = false;
	std::vector<std::uint8_t> base;
	if (!writeMod(visual, base, error)) return false;

	// Find the end chunk (writeMod puts the collinfo text after it).
	std::size_t pos = 0, endAt = std::size_t(-1);
	while (pos + 8 <= base.size()) {
		const std::uint32_t type = be32(base, pos), len = be32(base, pos + 4);
		if (type == 0xFFFF) {
			endAt = pos;
			break;
		}
		pos += 8 + len;
	}
	if (endAt == std::size_t(-1) || endAt % 32) {
		error = "platform .mod: writeMod output has no aligned end chunk";
		return false;
	}

	std::vector<std::uint8_t> chunk;
	put32(chunk, 0x100);
	put32(chunk, 0); // length, patched below
	put32(chunk, std::uint32_t(model.collision.size()));
	put32(chunk, 1); // one room
	pad32(chunk, endAt);
	put32(chunk, 0); // RoomInfo: room 0 -> joint 0
	pad32(chunk, endAt);
	for (const ModCollTri& t : model.collision) {
		for (int v : t.v) {
			if (v < 0 || std::size_t(v) >= model.positions.size()) {
				error = "platform .mod: collision vertex index out of range";
				return false;
			}
		}
		const Vec3 &a = model.positions[std::size_t(t.v[0])], &b = model.positions[std::size_t(t.v[1])], &c = model.positions[std::size_t(t.v[2])];
		const Vec3 u { c.x - a.x, c.y - a.y, c.z - a.z }, w { b.x - a.x, b.y - a.y, b.z - a.z };
		Vec3 n { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
		const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		if (!(len > 1e-6f)) {
			error = "platform .mod: degenerate collision triangle";
			return false;
		}
		n = { n.x / len, n.y / len, n.z / len };
		put32(chunk, t.code);
		for (int v : t.v) put32(chunk, std::uint32_t(v));
		put16(chunk, 0); // room
		for (int k = 0; k < 3; k++) put16(chunk, -1);
		putF(chunk, n.x);
		putF(chunk, n.y);
		putF(chunk, n.z);
		putF(chunk, n.x * a.x + n.y * a.y + n.z * a.z);
	}
	pad32(chunk, endAt);
	const std::uint32_t chunkLen = std::uint32_t(chunk.size() - 8);
	for (int k = 0; k < 4; k++) chunk[4 + std::size_t(k)] = std::uint8_t(chunkLen >> (24 - 8 * k));

	out.assign(base.begin(), base.begin() + std::ptrdiff_t(endAt));
	out.insert(out.end(), chunk.begin(), chunk.end());
	out.insert(out.end(), base.begin() + std::ptrdiff_t(endAt), base.end());
	return true;
}

} // namespace nectar
