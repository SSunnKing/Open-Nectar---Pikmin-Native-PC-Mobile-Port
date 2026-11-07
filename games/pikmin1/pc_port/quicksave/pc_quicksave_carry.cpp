// Quick save day-start precision block. See pc_quicksave_carry.h.

#include "quicksave/pc_quicksave_carry.h"

#include <cstring>

namespace pc_quicksave {

namespace {

void put_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void put_u32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; ++i) out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

void put_f32(std::vector<uint8_t>& out, float f)
{
	uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	put_u32(out, u);
}

struct Reader {
	const uint8_t* p;
	size_t left;
	bool ok = true;

	uint8_t u8()
	{
		if (!ok || left < 1) {
			ok = false;
			return 0;
		}
		const uint8_t v = *p++;
		--left;
		return v;
	}
	uint32_t u32()
	{
		if (!ok || left < 4) {
			ok = false;
			return 0;
		}
		const uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		p += 4;
		left -= 4;
		return v;
	}
	float f32()
	{
		const uint32_t u = u32();
		float f;
		std::memcpy(&f, &u, sizeof(f));
		return f;
	}
};

} // namespace

void encode_carry(const Carry& c, std::vector<uint8_t>& out)
{
	put_u32(out, kCarryVersion);
	put_u32(out, (uint32_t)c.livingPikis);
	put_u32(out, (uint32_t)c.areas.size());
	for (const CarryArea& a : c.areas) {
		put_u32(out, (uint32_t)a.id);
		put_u32(out, (uint32_t)a.sprouts.size());
		for (const CarrySprout& s : a.sprouts) {
			for (float v : s.pos) put_f32(out, v);
			for (float v : s.rot) put_f32(out, v);
			put_u8(out, s.colour);
			put_u8(out, s.stage);
		}
	}
}

bool decode_carry(const uint8_t* data, size_t len, Carry& c)
{
	if (data == nullptr) return false;
	Reader r{ data, len };
	if (r.u32() != kCarryVersion || !r.ok) return false;
	c.livingPikis       = (int32_t)r.u32();
	const uint32_t areas = r.u32();
	if (!r.ok || areas > kMaxCarryAreas) return false;
	c.areas.assign(areas, CarryArea());
	for (CarryArea& a : c.areas) {
		a.id             = (int32_t)r.u32();
		const uint32_t n = r.u32();
		if (!r.ok || n > kMaxCarrySprouts || (size_t)n * 26 > r.left) return false;
		a.sprouts.assign(n, CarrySprout());
		for (CarrySprout& s : a.sprouts) {
			for (float& v : s.pos) v = r.f32();
			for (float& v : s.rot) v = r.f32();
			s.colour = r.u8();
			s.stage  = r.u8();
		}
	}
	return r.ok && r.left == 0;
}

int16_t card_unit(float v)
{
	// As writeShort(short) receives it: the float-to-short conversion
	// truncates toward zero (every coordinate of a map is in range).
	return static_cast<int16_t>(v);
}

bool carry_matches_card(const CarrySprout& precise, const float cardPos[3], uint8_t colour, uint8_t stage)
{
	if (precise.colour != colour || precise.stage != stage) return false;
	for (int i = 0; i < 3; ++i) {
		if ((float)card_unit(precise.pos[i]) != cardPos[i]) return false;
	}
	return true;
}

} // namespace pc_quicksave
