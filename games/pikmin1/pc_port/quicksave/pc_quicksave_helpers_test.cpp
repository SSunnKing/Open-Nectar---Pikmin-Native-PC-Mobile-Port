// Host-run test of the quick save's pure helpers: the day-start precision
// block (pc_quicksave_carry.h).

#include "quicksave/pc_quicksave_carry.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace q = pc_quicksave;

namespace {
int sFailures = 0;

void check(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

q::Carry sample()
{
	q::Carry c;
	c.livingPikis = 87;
	for (int a = 0; a < 3; ++a) {
		q::CarryArea area;
		area.id = a == 2 ? 4 : a;
		for (int i = 0; i < (a == 1 ? 0 : 5 + a); ++i) {
			q::CarrySprout s;
			s.pos[0] = -312.4375f + 17.3f * i;
			s.pos[1] = 2.5f * a;
			s.pos[2] = 851.125f - 9.7f * i;
			s.rot[1] = 0.37f * i + 0.5f;
			s.colour = (uint8_t)(i % 3);
			s.stage  = (uint8_t)((i + a) % 3);
			area.sprouts.push_back(s);
		}
		c.areas.push_back(area);
	}
	return c;
}

bool same(const q::Carry& a, const q::Carry& b)
{
	if (a.livingPikis != b.livingPikis || a.areas.size() != b.areas.size()) return false;
	for (size_t i = 0; i < a.areas.size(); ++i) {
		const q::CarryArea& x = a.areas[i];
		const q::CarryArea& y = b.areas[i];
		if (x.id != y.id || x.sprouts.size() != y.sprouts.size()) return false;
		for (size_t k = 0; k < x.sprouts.size(); ++k) {
			const q::CarrySprout& s = x.sprouts[k];
			const q::CarrySprout& t = y.sprouts[k];
			if (std::memcmp(s.pos, t.pos, sizeof(s.pos)) != 0 || std::memcmp(s.rot, t.rot, sizeof(s.rot)) != 0
			    || s.colour != t.colour || s.stage != t.stage) {
				return false;
			}
		}
	}
	return true;
}

void test_carry_round_trip()
{
	const q::Carry c = sample();
	std::vector<uint8_t> bytes;
	q::encode_carry(c, bytes);
	check(bytes.size() == 12 + 3 * 8 + (5 + 7) * 26, "carry block size");
	check(bytes[0] == 1 && bytes[1] == 0 && bytes[4] == 87, "version, then the living count, little-endian");
	q::Carry d;
	check(q::decode_carry(bytes.data(), bytes.size(), d), "decode what encode wrote");
	check(same(c, d), "every field survives, float bits included");
	std::vector<uint8_t> again;
	q::encode_carry(d, again);
	check(again == bytes, "re-encoding gives the same bytes");

	q::Carry empty;
	bytes.clear();
	q::encode_carry(empty, bytes);
	check(bytes.size() == 12 && q::decode_carry(bytes.data(), bytes.size(), d) && d.areas.empty(), "no areas");
}

void test_carry_refuses()
{
	const q::Carry c = sample();
	std::vector<uint8_t> bytes;
	q::encode_carry(c, bytes);
	q::Carry d;
	int accepted = 0;
	for (size_t len = 0; len < bytes.size(); ++len) {
		if (q::decode_carry(bytes.data(), len, d)) ++accepted;
	}
	check(accepted == 0, "no shortened block decodes");
	std::vector<uint8_t> extra = bytes;
	extra.push_back(0);
	check(!q::decode_carry(extra.data(), extra.size(), d), "a byte left over");
	std::vector<uint8_t> v2 = bytes;
	v2[0]                   = 2;
	check(!q::decode_carry(v2.data(), v2.size(), d), "another version");
	std::vector<uint8_t> many = bytes;
	many[8]                   = 0xFF; // area count
	check(!q::decode_carry(many.data(), many.size(), d), "area count over its limit");
	std::vector<uint8_t> sprouts = bytes;
	sprouts[16]                  = 0xFF; // first area's sprout count
	sprouts[17]                  = 0xFF;
	check(!q::decode_carry(sprouts.data(), sprouts.size(), d), "sprout count over its limit or past the end");
	check(!q::decode_carry(nullptr, 0, d), "null");
}

void test_card_unit()
{
	check(q::card_unit(1.7f) == 1 && q::card_unit(-1.7f) == -1, "truncates toward zero");
	check(q::card_unit(0.4f) == 0 && q::card_unit(-0.4f) == 0, "fractions of a unit are dropped");
	check(q::card_unit(-312.4375f) == -312 && q::card_unit(851.125f) == 851, "map-sized values");
	check(q::card_unit(32767.9f) == 32767 && q::card_unit(-32768.0f) == -32768, "the short's range");

	q::CarrySprout s;
	s.pos[0] = -312.4375f;
	s.pos[1] = 5.75f;
	s.pos[2] = 851.125f;
	s.colour = 1;
	s.stage  = 2;
	const float card[3] = { -312.0f, 5.0f, 851.0f };
	check(q::carry_matches_card(s, card, 1, 2), "the card's whole-unit copy matches");
	const float other[3] = { -313.0f, 5.0f, 851.0f };
	check(!q::carry_matches_card(s, other, 1, 2), "another position");
	check(!q::carry_matches_card(s, card, 0, 2), "another colour");
	check(!q::carry_matches_card(s, card, 1, 1), "another growth stage");
}

} // namespace

int main()
{
	test_carry_round_trip();
	test_carry_refuses();
	test_card_unit();
	if (sFailures == 0) std::printf("pc_quicksave_helpers_test: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
