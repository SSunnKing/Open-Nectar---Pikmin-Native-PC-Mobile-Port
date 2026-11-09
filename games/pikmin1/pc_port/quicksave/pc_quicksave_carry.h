#pragma once
// Quick save: the day-start precision block (pc_quicksave_file.h, version 2).
//
// A day the game reached by continuing the previous one in memory starts from
// state the card file keeps less exactly than memory held it. A quick load
// re-enters a story day from its card block, so without this block such a day
// would start differently from the recorded one:
//   - the buried Pikmin (sprouts left in the ground at a day's end) of every
//     area: the card keeps their position in whole units (each coordinate
//     truncated to a short) and drops their rotation;
//   - the living Pikmin count of the last day's result (PlayerState), which
//     the card does not keep at all; the state hash counts it.
// The block keeps them as memory held them at the day's start, and a load puts
// them back over what the card block reloaded.
//
// Layout, little-endian:
//   4      version u32 (kCarryVersion)
//   4      living Pikmin count s32
//   4      area count u32, then per area:
//     4      area (stage) id s32
//     4      buried Pikmin count u32, then per buried Pikmin, in list order:
//       12     position: 3 floats as u32 bits
//       12     rotation: 3 floats as u32 bits
//       1      colour u8
//       1      growth stage u8 (leaf, bud, flower)
//
// Pure: no game state. The host test covers it (pc_quicksave_helpers_test).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc_quicksave {

constexpr uint32_t kCarryVersion   = 1;
constexpr uint32_t kMaxCarryAreas  = 64;
constexpr uint32_t kMaxCarrySprouts = 4096; // per area

struct CarrySprout {
	float pos[3] = {};
	float rot[3] = {};
	uint8_t colour = 0;
	uint8_t stage  = 0;
};

struct CarryArea {
	int32_t id = 0;
	std::vector<CarrySprout> sprouts;
};

struct Carry {
	int32_t livingPikis = 0;
	std::vector<CarryArea> areas;
};

void encode_carry(const Carry& c, std::vector<uint8_t>& out);
// False (c unspecified) on another version, a count over its limit, a short
// block or bytes left over.
bool decode_carry(const uint8_t* data, size_t len, Carry& c);

// The whole-unit value the card file keeps of a coordinate: the game writes
// the float through writeShort(short), which truncates toward zero.
int16_t card_unit(float v);

// Whether a buried Pikmin the card block reloaded (its whole-unit position,
// colour and growth stage) is this precise one.
bool carry_matches_card(const CarrySprout& precise, const float cardPos[3], uint8_t colour, uint8_t stage);

} // namespace pc_quicksave
