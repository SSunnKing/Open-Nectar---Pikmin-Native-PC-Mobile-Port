#pragma once
// Quick save file (".qsv"): one recorded day, from its start to the tick it
// was saved on, so a load can replay the day and hand control back to the
// player at that tick (pc_quicksave.h).
//
// Format, little-endian throughout, every field written explicitly:
//   size   field
//   4      magic "PKQS"
//   2      version u16 (kVersion)
//   32     build id: SHA-256 of the executable that wrote the file
//   2+n    region: u16 length + ASCII (the compiled game release)
//   4+n    sim settings text: u32 length + bytes (pc_settings_sim_config_text)
//   8      sim settings hash u64 (FNV-1a 64 of that text)
//   4+n    env knobs text: u32 length + bytes (the test switches that
//          change the sim, "key=value;" for each one set)
//   4+n    mode text: u32 length + bytes (story or VS, co-op, captains, map)
//   4      seed u32 (the session seed the day was reseeded from)
//   4      day s32
//   4      stage id s32
//   4      slot s32 (1-3: the card file; 0: none)
//   4      day start tick u32 (the logical tick the day began on)
//   1      tutorial u8 (PlayerState tutorial flag at the day's start)
//   4*12   world clock at the day's start: 12 u32 words (float bits as is)
//   4      tick count N u32 (ticks of the day replayed by a load)
//   8      state hash at N u64 (pc_state_hash total)
//   4+n    view block: u32 length + bytes (cameras; opaque here)
//   4+n    local input block: u32 length + bytes (window state; opaque here)
//   4+n    day-start card block: u32 length + bytes (the game file format)
//   4+n    day-start state block: u32 length + bytes (sim state the card does
//          not hold and that carries over from day to day; opaque here)
//   4+n    day-start precision block: u32 length + bytes (what the card block
//          keeps less exactly than the game held it in memory, such as the
//          buried Pikmin's positions; pc_quicksave_carry.h; opaque here).
//          Version 2 on.
//   2      record version u16 (PKNI version, 4)
//   2      record size u16 (bytes per tick, 104)
//   4      record count u32 (must equal N)
//   N*size the day's input records, tick 1 to N
//   4      CRC-32 (IEEE, reflected 0xEDB88320) of every byte before it
//
// Pure: no game state, no files. The host test covers the round trip, the
// CRC, every truncated length, the version and the header checks.
//
// Versions: 1 had no precision block. A file of any other version than
// kVersion is refused (kBadVersion) before anything else is read: its
// recorded day may need state it does not hold.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pc_quicksave {

constexpr uint8_t kMagic[4]        = { 'P', 'K', 'Q', 'S' };
constexpr uint16_t kVersion        = 2;
constexpr size_t kBuildIdBytes     = 32;
constexpr size_t kClockWords       = 12;
constexpr size_t kMaxRegionBytes   = 64;
constexpr size_t kMaxTextBytes     = 64 * 1024;
constexpr size_t kMaxBlockBytes    = 1024 * 1024;
constexpr uint32_t kMaxTicks       = 16u * 1024u * 1024u; // ~6 days of play at 30 Hz

struct File {
	uint8_t buildId[kBuildIdBytes] = {};
	std::string region;
	std::string simText;
	uint64_t simHash = 0;
	std::string knobsText;
	std::string modeText;
	uint32_t seed          = 0;
	int32_t day            = 0;
	int32_t stage          = 0;
	int32_t slot           = 0;
	uint32_t dayStartTick  = 0;
	uint8_t tutorial       = 0;
	uint32_t clock[kClockWords] = {};
	uint32_t ticks         = 0;
	uint64_t stateHash     = 0;
	std::vector<uint8_t> view;
	std::vector<uint8_t> local;
	std::vector<uint8_t> card;
	std::vector<uint8_t> start;
	std::vector<uint8_t> carry;
	uint16_t recordVersion = 0;
	uint16_t recordSize    = 0;
	std::vector<uint8_t> records; // ticks * recordSize bytes
};

enum Result {
	kOk = 0,
	kTruncated,  // shorter than its own fields say
	kBadMagic,   // not a quick save
	kBadVersion, // a quick save of another format version
	kBadCrc,     // damaged
	kBadField,   // a field out of range or inconsistent (record count, sizes)
};

const char* result_name(Result r);

// CRC-32 (IEEE 802.3), as zlib's crc32(0, data, len).
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

// Appends the encoded file to out. False (out unchanged) when a field is out
// of range: a text or block over its limit, or records.size() != ticks *
// recordSize.
bool encode(const File& f, std::vector<uint8_t>& out);

// Decodes a whole file. On anything but kOk, f is unspecified.
Result decode(const uint8_t* data, size_t len, File& f);

// The format version a file says it has (its bytes 4-5), or 0 when it is
// too short or not a quick save. For messages about a refused file.
uint16_t file_version(const uint8_t* data, size_t len);

// What a load compares with the running game before it touches anything.
struct Expect {
	uint8_t buildId[kBuildIdBytes] = {};
	std::string region;
	uint64_t simHash = 0;
	std::string knobsText;
};

enum Check {
	kMatch = 0,
	kBuildMismatch,
	kRegionMismatch,
	kSimMismatch,
	kKnobsMismatch,
};

const char* check_name(Check c);
// The first mismatching field, in the order above.
Check check(const File& f, const Expect& e);

// Lowercase hex of bytes, for log lines.
std::string hex(const uint8_t* data, size_t len);

} // namespace pc_quicksave
