// Quick save file format. See pc_quicksave_file.h.

#include "quicksave/pc_quicksave_file.h"

#include <cstring>

namespace pc_quicksave {

namespace {

void put_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

void put_u16(std::vector<uint8_t>& out, uint16_t v)
{
	out.push_back((uint8_t)(v & 0xFF));
	out.push_back((uint8_t)((v >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; ++i) out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

void put_u64(std::vector<uint8_t>& out, uint64_t v)
{
	for (int i = 0; i < 8; ++i) out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

void put_bytes(std::vector<uint8_t>& out, const void* data, size_t len)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);
	out.insert(out.end(), p, p + len);
}

// Bounded little-endian reader: every read checks the remaining length and
// latches a failure, so a short file can never be read past its end.
struct Reader {
	const uint8_t* p;
	size_t left;
	bool ok = true;

	bool take(void* out, size_t n)
	{
		if (!ok || n > left) {
			ok = false;
			return false;
		}
		if (n > 0) std::memcpy(out, p, n);
		p += n;
		left -= n;
		return true;
	}
	uint8_t u8()
	{
		uint8_t b = 0;
		take(&b, 1);
		return b;
	}
	uint16_t u16()
	{
		uint8_t b[2] = {};
		if (!take(b, 2)) return 0;
		return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
	}
	uint32_t u32()
	{
		uint8_t b[4] = {};
		if (!take(b, 4)) return 0;
		return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	}
	uint64_t u64()
	{
		const uint64_t lo = u32();
		const uint64_t hi = u32();
		return lo | (hi << 32);
	}
	// A length-prefixed field; false (and a latched failure for a short
	// read) when it is over max or runs past the end.
	bool str(std::string& out, size_t lenBytes, size_t max, bool& tooLong)
	{
		const size_t n = lenBytes == 2 ? u16() : u32();
		if (!ok) return false;
		if (n > max) {
			tooLong = true;
			return false;
		}
		if (n > left) {
			ok = false;
			return false;
		}
		out.assign(reinterpret_cast<const char*>(p), n);
		p += n;
		left -= n;
		return true;
	}
	bool blob(std::vector<uint8_t>& out, size_t max, bool& tooLong)
	{
		const size_t n = u32();
		if (!ok) return false;
		if (n > max) {
			tooLong = true;
			return false;
		}
		if (n > left) {
			ok = false;
			return false;
		}
		out.assign(p, p + n);
		p += n;
		left -= n;
		return true;
	}
};

uint32_t sCrcTable[256];
bool sCrcReady = false;

void crc_init()
{
	for (uint32_t i = 0; i < 256; ++i) {
		uint32_t c = i;
		for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		sCrcTable[i] = c;
	}
	sCrcReady = true;
}

} // namespace

const char* result_name(Result r)
{
	switch (r) {
	case kOk: return "ok";
	case kTruncated: return "truncated";
	case kBadMagic: return "not a quick save";
	case kBadVersion: return "from another version of Quick Save";
	case kBadCrc: return "CRC mismatch (damaged file)";
	case kBadField: return "inconsistent field";
	}
	return "unknown";
}

const char* check_name(Check c)
{
	switch (c) {
	case kMatch: return "match";
	case kBuildMismatch: return "made by a different build of the game";
	case kRegionMismatch: return "made with a different game release (region)";
	case kSimMismatch: return "made with different sim-affecting settings";
	case kKnobsMismatch: return "made with different test switches (PIKMIN_NETPLAY_SEED and the like)";
	}
	return "unknown";
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc)
{
	if (!sCrcReady) crc_init();
	crc = ~crc;
	for (size_t i = 0; i < len; ++i) crc = sCrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

bool encode(const File& f, std::vector<uint8_t>& out)
{
	if (f.region.size() > kMaxRegionBytes || f.simText.size() > kMaxTextBytes || f.knobsText.size() > kMaxTextBytes
	    || f.modeText.size() > kMaxTextBytes || f.view.size() > kMaxBlockBytes || f.local.size() > kMaxBlockBytes
	    || f.card.size() > kMaxBlockBytes || f.start.size() > kMaxBlockBytes || f.carry.size() > kMaxBlockBytes
	    || f.ticks > kMaxTicks || f.recordSize == 0
	    || f.records.size() != (size_t)f.ticks * f.recordSize) {
		return false;
	}
	std::vector<uint8_t> b;
	b.reserve(256 + f.simText.size() + f.view.size() + f.local.size() + f.card.size() + f.records.size());
	put_bytes(b, kMagic, 4);
	put_u16(b, kVersion);
	put_bytes(b, f.buildId, kBuildIdBytes);
	put_u16(b, (uint16_t)f.region.size());
	put_bytes(b, f.region.data(), f.region.size());
	put_u32(b, (uint32_t)f.simText.size());
	put_bytes(b, f.simText.data(), f.simText.size());
	put_u64(b, f.simHash);
	put_u32(b, (uint32_t)f.knobsText.size());
	put_bytes(b, f.knobsText.data(), f.knobsText.size());
	put_u32(b, (uint32_t)f.modeText.size());
	put_bytes(b, f.modeText.data(), f.modeText.size());
	put_u32(b, f.seed);
	put_u32(b, (uint32_t)f.day);
	put_u32(b, (uint32_t)f.stage);
	put_u32(b, (uint32_t)f.slot);
	put_u32(b, f.dayStartTick);
	put_u8(b, f.tutorial);
	for (size_t i = 0; i < kClockWords; ++i) put_u32(b, f.clock[i]);
	put_u32(b, f.ticks);
	put_u64(b, f.stateHash);
	put_u32(b, (uint32_t)f.view.size());
	put_bytes(b, f.view.data(), f.view.size());
	put_u32(b, (uint32_t)f.local.size());
	put_bytes(b, f.local.data(), f.local.size());
	put_u32(b, (uint32_t)f.card.size());
	put_bytes(b, f.card.data(), f.card.size());
	put_u32(b, (uint32_t)f.start.size());
	put_bytes(b, f.start.data(), f.start.size());
	put_u32(b, (uint32_t)f.carry.size());
	put_bytes(b, f.carry.data(), f.carry.size());
	put_u16(b, f.recordVersion);
	put_u16(b, f.recordSize);
	put_u32(b, f.ticks);
	put_bytes(b, f.records.data(), f.records.size());
	put_u32(b, crc32(b.data(), b.size()));
	out.insert(out.end(), b.begin(), b.end());
	return true;
}

Result decode(const uint8_t* data, size_t len, File& f)
{
	if (data == nullptr || len < 6) return kTruncated;
	if (std::memcmp(data, kMagic, 4) != 0) return kBadMagic;
	const uint16_t version = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
	if (version != kVersion) return kBadVersion;
	if (len < 6 + 4) return kTruncated;
	const size_t body      = len - 4;
	const uint32_t stored  = (uint32_t)data[body] | ((uint32_t)data[body + 1] << 8) | ((uint32_t)data[body + 2] << 16)
	                      | ((uint32_t)data[body + 3] << 24);
	if (crc32(data, body) != stored) return kBadCrc;

	Reader r{ data + 6, body - 6 };
	bool tooLong = false;
	r.take(f.buildId, kBuildIdBytes);
	r.str(f.region, 2, kMaxRegionBytes, tooLong);
	r.str(f.simText, 4, kMaxTextBytes, tooLong);
	f.simHash = r.u64();
	r.str(f.knobsText, 4, kMaxTextBytes, tooLong);
	r.str(f.modeText, 4, kMaxTextBytes, tooLong);
	f.seed         = r.u32();
	f.day          = (int32_t)r.u32();
	f.stage        = (int32_t)r.u32();
	f.slot         = (int32_t)r.u32();
	f.dayStartTick = r.u32();
	f.tutorial     = r.u8();
	for (size_t i = 0; i < kClockWords; ++i) f.clock[i] = r.u32();
	f.ticks     = r.u32();
	f.stateHash = r.u64();
	r.blob(f.view, kMaxBlockBytes, tooLong);
	r.blob(f.local, kMaxBlockBytes, tooLong);
	r.blob(f.card, kMaxBlockBytes, tooLong);
	r.blob(f.start, kMaxBlockBytes, tooLong);
	r.blob(f.carry, kMaxBlockBytes, tooLong);
	f.recordVersion        = r.u16();
	f.recordSize           = r.u16();
	const uint32_t records = r.u32();
	if (tooLong) return kBadField;
	if (!r.ok) return kTruncated;
	if (f.ticks > kMaxTicks || records != f.ticks || f.recordSize == 0) return kBadField;
	const size_t recordBytes = (size_t)records * f.recordSize;
	if (recordBytes > r.left) return kTruncated;
	if (recordBytes < r.left) return kBadField; // trailing bytes before the CRC
	f.records.assign(r.p, r.p + recordBytes);
	return kOk;
}

uint16_t file_version(const uint8_t* data, size_t len)
{
	if (data == nullptr || len < 6 || std::memcmp(data, kMagic, 4) != 0) return 0;
	return (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
}

Check check(const File& f, const Expect& e)
{
	if (std::memcmp(f.buildId, e.buildId, kBuildIdBytes) != 0) return kBuildMismatch;
	if (f.region != e.region) return kRegionMismatch;
	if (f.simHash != e.simHash) return kSimMismatch;
	if (f.knobsText != e.knobsText) return kKnobsMismatch;
	return kMatch;
}

std::string hex(const uint8_t* data, size_t len)
{
	static const char* digits = "0123456789abcdef";
	std::string s;
	s.reserve(len * 2);
	for (size_t i = 0; i < len; ++i) {
		s += digits[data[i] >> 4];
		s += digits[data[i] & 0xF];
	}
	return s;
}

} // namespace pc_quicksave
