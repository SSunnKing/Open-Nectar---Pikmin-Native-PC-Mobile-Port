// Host-run test of the quick save file format and its file helpers
// (pc_quicksave_file.h, pc_quicksave_io.h).

#include "quicksave/pc_quicksave_file.h"
#include "quicksave/pc_quicksave_io.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace q  = pc_quicksave;

namespace {
int sFailures = 0;

void check(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

q::File sample(uint32_t ticks)
{
	q::File f;
	for (size_t i = 0; i < q::kBuildIdBytes; ++i) f.buildId[i] = (uint8_t)(0xA0 + i);
	f.region       = "GPIE01_01";
	f.simText      = "simcfg=1;fpsMode=0;vsMap=impact%3Barena;";
	f.simHash      = 0x0123456789ABCDEFull;
	f.knobsText    = "PIKMIN_NETPLAY_SEED=7;PIKMIN_UNLOCK_ALL=1;";
	f.modeText     = "qsmode=1;vs=0;coop=1;captain0=0;captain1=1;";
	f.seed         = 7;
	f.day          = 12;
	f.stage        = 3;
	f.slot         = 2;
	f.dayStartTick = 0xFEDCBA98u;
	f.tutorial     = 1;
	for (size_t i = 0; i < q::kClockWords; ++i) f.clock[i] = 0x3F800000u + (uint32_t)i * 0x01010101u;
	f.ticks     = ticks;
	f.stateHash = 0xF00DFACECAFEBEEFull;
	for (int i = 0; i < 300; ++i) f.view.push_back((uint8_t)(i * 7));
	for (int i = 0; i < 17; ++i) f.local.push_back((uint8_t)(255 - i));
	// A short card block keeps the every-truncation loops quick; the format
	// does not depend on its size.
	for (int i = 0; i < 0x800; ++i) f.card.push_back((uint8_t)((i * 31) ^ (i >> 8)));
	for (int i = 0; i < 36; ++i) f.start.push_back((uint8_t)(i * 5 + 1));
	for (int i = 0; i < 61; ++i) f.carry.push_back((uint8_t)(i * 9 + 3));
	f.recordVersion = 4;
	f.recordSize    = 104;
	for (uint32_t t = 0; t < ticks; ++t) {
		for (int b = 0; b < 104; ++b) f.records.push_back((uint8_t)(t * 13 + b));
	}
	return f;
}

bool same(const q::File& a, const q::File& b)
{
	return std::memcmp(a.buildId, b.buildId, q::kBuildIdBytes) == 0 && a.region == b.region && a.simText == b.simText
	    && a.simHash == b.simHash && a.knobsText == b.knobsText && a.modeText == b.modeText && a.seed == b.seed
	    && a.day == b.day && a.stage == b.stage && a.slot == b.slot && a.dayStartTick == b.dayStartTick
	    && a.tutorial == b.tutorial && std::memcmp(a.clock, b.clock, sizeof(a.clock)) == 0 && a.ticks == b.ticks
	    && a.stateHash == b.stateHash && a.view == b.view && a.local == b.local && a.card == b.card && a.start == b.start
	    && a.carry == b.carry
	    && a.recordVersion == b.recordVersion && a.recordSize == b.recordSize && a.records == b.records;
}

// Replaces the trailing CRC so a deliberately changed body still passes it.
void reseal(std::vector<uint8_t>& bytes)
{
	const size_t body  = bytes.size() - 4;
	const uint32_t crc = q::crc32(bytes.data(), body);
	for (int i = 0; i < 4; ++i) bytes[body + i] = (uint8_t)((crc >> (8 * i)) & 0xFF);
}

// Offset of the record count: the CRC, the records and the count precede the end.
size_t record_count_offset(const q::File& f, size_t total) { return total - 4 - f.records.size() - 4; }

void test_crc()
{
	const char* v = "123456789";
	check(q::crc32(reinterpret_cast<const uint8_t*>(v), 9) == 0xCBF43926u, "CRC-32 check value of \"123456789\"");
	check(q::crc32(nullptr, 0) == 0, "CRC-32 of nothing");
	// Incremental CRC equals the one-shot CRC.
	const uint32_t part = q::crc32(reinterpret_cast<const uint8_t*>(v), 4);
	check(q::crc32(reinterpret_cast<const uint8_t*>(v) + 4, 5, part) == 0xCBF43926u, "CRC-32 continues across calls");
}

void test_round_trip()
{
	for (uint32_t ticks : { 0u, 1u, 37u }) {
		const q::File f = sample(ticks);
		std::vector<uint8_t> bytes;
		check(q::encode(f, bytes), "encode a valid file");
		q::File g;
		check(q::decode(bytes.data(), bytes.size(), g) == q::kOk, "decode what encode wrote");
		check(same(f, g), "every field survives the round trip");
		std::vector<uint8_t> again;
		check(q::encode(g, again) && again == bytes, "re-encoding the decoded file gives the same bytes");
		check(bytes.size() > 4 && std::memcmp(bytes.data(), "PKQS", 4) == 0, "magic first");
		check(bytes[4] == 2 && bytes[5] == 0, "version 2 little-endian");
		check(q::file_version(bytes.data(), bytes.size()) == 2, "file_version reads it");
		check(bytes[6] == 0xA0 && bytes[6 + 31] == 0xA0 + 31, "build id follows the version");
		check(bytes[38] == 9 && bytes[39] == 0 && std::memcmp(bytes.data() + 40, "GPIE01_01", 9) == 0,
		      "region as u16 length and ASCII");
	}
	// Little-endian multi-byte fields at their documented offsets.
	const q::File f = sample(2);
	std::vector<uint8_t> bytes;
	q::encode(f, bytes);
	const size_t seedAt = 6 + 32 + 2 + f.region.size() + 4 + f.simText.size() + 8 + 4 + f.knobsText.size() + 4 + f.modeText.size();
	check(bytes[seedAt] == 7 && bytes[seedAt + 3] == 0, "seed u32 little-endian");
	const size_t tickAt = seedAt + 4 * 4;
	check(bytes[tickAt] == 0x98 && bytes[tickAt + 3] == 0xFE, "day start tick u32 little-endian");
	const size_t hashAt = tickAt + 4 + 1 + 4 * q::kClockWords + 4;
	check(bytes[hashAt - 4] == 2 && bytes[hashAt] == 0xEF && bytes[hashAt + 7] == 0xF0, "N, then the state hash little-endian");
}

void test_encode_refuses()
{
	q::File f = sample(3);
	std::vector<uint8_t> out;
	f.records.pop_back();
	check(!q::encode(f, out) && out.empty(), "records not ticks * size");
	f = sample(3);
	f.recordSize = 0;
	f.records.clear();
	check(!q::encode(f, out), "zero record size");
	f = sample(1);
	f.region.assign(q::kMaxRegionBytes + 1, 'x');
	check(!q::encode(f, out), "region over its limit");
	f = sample(1);
	f.simText.assign(q::kMaxTextBytes + 1, 'x');
	check(!q::encode(f, out), "text over its limit");
	f = sample(1);
	f.card.assign(q::kMaxBlockBytes + 1, 0);
	check(!q::encode(f, out), "block over its limit");
}

void test_corruption()
{
	const q::File f = sample(5);
	std::vector<uint8_t> bytes;
	q::encode(f, bytes);
	q::File g;
	int accepted = 0;
	for (size_t i = 0; i < bytes.size(); ++i) {
		std::vector<uint8_t> bad = bytes;
		bad[i] ^= 0x01;
		const q::Result r = q::decode(bad.data(), bad.size(), g);
		if (r == q::kOk) ++accepted;
		if (i < 4) check(r == q::kBadMagic, "a flipped magic byte is not a quick save");
		else if (i < 6) check(r == q::kBadVersion, "a flipped version byte is another version");
		else if (r != q::kBadCrc) check(false, "a flipped body or CRC byte fails the CRC");
	}
	check(accepted == 0, "no single flipped bit is accepted");
}

void test_truncation()
{
	const q::File f = sample(4);
	std::vector<uint8_t> bytes;
	q::encode(f, bytes);
	q::File g;
	int accepted = 0;
	for (size_t len = 0; len < bytes.size(); ++len) {
		if (q::decode(bytes.data(), len, g) == q::kOk) ++accepted;
	}
	check(accepted == 0, "no truncated length decodes");
	check(q::decode(bytes.data(), 0, g) == q::kTruncated, "empty file is truncated");
	check(q::decode(bytes.data(), 5, g) == q::kTruncated, "5 bytes is truncated");
	check(q::decode(nullptr, 100, g) == q::kTruncated, "null data");

	// The same truncations with a valid CRC over the shortened body: the
	// field reader itself must stop at the end of the data.
	int notTruncated = 0;
	for (size_t body = 6; body < bytes.size() - 4; ++body) {
		std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + (long)body);
		cut.resize(body + 4);
		reseal(cut);
		if (q::decode(cut.data(), cut.size(), g) != q::kTruncated) ++notTruncated;
	}
	check(notTruncated == 0, "every shortened body with a valid CRC reads as truncated");
}

void test_version_and_fields()
{
	const q::File f = sample(3);
	std::vector<uint8_t> bytes;
	q::encode(f, bytes);
	q::File g;
	for (uint16_t v : { (uint16_t)0, (uint16_t)1, (uint16_t)3, (uint16_t)0x0200 }) {
		std::vector<uint8_t> other = bytes;
		other[4] = (uint8_t)(v & 0xFF);
		other[5] = (uint8_t)(v >> 8);
		reseal(other);
		check(q::decode(other.data(), other.size(), g) == q::kBadVersion, "another version is refused before the CRC");
	}
	{
		std::vector<uint8_t> other = bytes;
		other[0]                   = 'X';
		check(q::decode(other.data(), other.size(), g) == q::kBadMagic, "bad magic");
	}
	{
		// Record count different from the tick count.
		std::vector<uint8_t> other = bytes;
		const size_t at            = record_count_offset(f, other.size());
		other[at]                  = (uint8_t)(other[at] + 1);
		reseal(other);
		check(q::decode(other.data(), other.size(), g) != q::kOk, "record count different from N");
	}
	{
		// Trailing bytes between the records and the CRC.
		std::vector<uint8_t> other = bytes;
		other.insert(other.end() - 4, (uint8_t)0x55);
		reseal(other);
		check(q::decode(other.data(), other.size(), g) == q::kBadField, "trailing bytes before the CRC");
	}
	{
		// A region length over the limit.
		std::vector<uint8_t> other = bytes;
		other[38]                  = (uint8_t)(q::kMaxRegionBytes + 1);
		reseal(other);
		check(q::decode(other.data(), other.size(), g) == q::kBadField, "region length over its limit");
	}
}

// A version 1 file (no precision block), laid out as 2a wrote it: refused as
// another version, and its version can still be read for the message.
void test_version_1_refused()
{
	q::File f = sample(2);
	f.carry.clear();
	std::vector<uint8_t> bytes;
	check(q::encode(f, bytes), "encode without a precision block");
	// Drop the empty block's length word (right before the record version and
	// size) and say version 1.
	const size_t at = record_count_offset(f, bytes.size()) - 4 - 4;
	check(bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 0 && bytes[at + 3] == 0, "empty block length found");
	bytes.erase(bytes.begin() + (long)at, bytes.begin() + (long)at + 4);
	bytes[4] = 1;
	reseal(bytes);
	q::File g;
	check(q::decode(bytes.data(), bytes.size(), g) == q::kBadVersion, "a version 1 file is refused");
	check(q::file_version(bytes.data(), bytes.size()) == 1, "and says it is version 1");
	check(q::file_version(bytes.data(), 5) == 0 && q::file_version(nullptr, 0) == 0, "no version from a short file");
	std::vector<uint8_t> notQs = bytes;
	notQs[0]                   = 'X';
	check(q::file_version(notQs.data(), notQs.size()) == 0, "no version from another kind of file");
}

void test_check()
{
	const q::File f = sample(1);
	q::Expect e;
	std::memcpy(e.buildId, f.buildId, q::kBuildIdBytes);
	e.region    = f.region;
	e.simHash   = f.simHash;
	e.knobsText = f.knobsText;
	check(q::check(f, e) == q::kMatch, "matching header");

	q::Expect b = e;
	b.buildId[31] ^= 1;
	check(q::check(f, b) == q::kBuildMismatch, "another build");
	q::Expect r = e;
	r.region    = "GPIP01_00";
	check(q::check(f, r) == q::kRegionMismatch, "another region");
	q::Expect s = e;
	s.simHash ^= 1;
	check(q::check(f, s) == q::kSimMismatch, "other sim settings");
	q::Expect k = e;
	k.knobsText = "PIKMIN_NETPLAY_SEED=8;PIKMIN_UNLOCK_ALL=1;";
	check(q::check(f, k) == q::kKnobsMismatch, "other env knobs");
	q::Expect none = e;
	none.knobsText.clear();
	check(q::check(f, none) == q::kKnobsMismatch, "a knob set at save time and unset now");
	// Order: the build is reported first when several differ.
	q::Expect all = k;
	all.buildId[0] ^= 1;
	all.region = "x";
	check(q::check(f, all) == q::kBuildMismatch, "build checked first");
	check(std::strlen(q::check_name(q::kSimMismatch)) > 0 && std::strlen(q::result_name(q::kBadCrc)) > 0, "names");
}

void test_io()
{
	std::error_code ec;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path dir = fs::temp_directory_path(ec) / ("pc_quicksave_test_" + std::to_string((long long)stamp));
	const std::string path = (dir / "quicksave" / "1.qsv").string();
	std::string err;
	const std::vector<uint8_t> a(1000, 0x11);
	const std::vector<uint8_t> b(70000, 0x22);
	check(q::write_file_atomic(path, a, &err), "atomic write creates the folder and the file");
	std::vector<uint8_t> back;
	check(q::read_file(path, back, &err) && back == a, "read back the first write");
	check(q::write_file_atomic(path, b, &err), "second write replaces the first");
	check(q::read_file(path, back, &err) && back == b, "read back the second write");
	check(!fs::exists(fs::path(path + ".tmp"), ec), "no temporary file left behind");
	check(!q::read_file((dir / "missing.qsv").string(), back, &err) && !err.empty(), "a missing file fails with a reason");
	err.clear();
	check(!q::read_file(path, back, &err, b.size() - 1) && back.empty() && !err.empty(), "a file over the size cap is refused unread");
	check(q::read_file(path, back, &err, b.size()) && back == b, "a file at the size cap is read");

	// The newest of the slot files that exist, as a load from the title picks.
	const std::string s0 = (dir / "quicksave" / "0.qsv").string();
	const std::string s2 = (dir / "quicksave" / "2.qsv").string();
	const std::string s3 = (dir / "quicksave" / "3.qsv").string();
	check(q::write_file_atomic(s0, a, &err) && q::write_file_atomic(s2, a, &err), "two more slot files");
	const auto now = fs::file_time_type::clock::now();
	fs::last_write_time(fs::path(s0), now - std::chrono::hours(2), ec);
	fs::last_write_time(fs::path(path), now - std::chrono::hours(1), ec);
	fs::last_write_time(fs::path(s2), now - std::chrono::hours(3), ec);
	const std::vector<std::string> order = q::existing_newest_first({ s0, path, s2, s3 });
	check(order.size() == 3 && order[0] == path && order[1] == s0 && order[2] == s2, "newest first, missing ones left out");
	fs::last_write_time(fs::path(s2), now - std::chrono::hours(1), ec);
	const std::vector<std::string> tie = q::existing_newest_first({ s2, path });
	check(tie.size() == 2 && tie[0] == s2 && tie[1] == path, "equal times keep the given order");
	check(q::existing_newest_first({ s3 }).empty(), "none exists");
	check(q::remove_file(s2) && q::existing_newest_first({ s2 }).empty(), "remove_file deletes an existing file");
	check(q::remove_file(s3), "remove_file on a missing file is not an error");
	fs::remove_all(dir, ec);

	uint8_t d1[32], d2[32];
	const bool ok1 = q::executable_sha256(d1);
	const bool ok2 = q::executable_sha256(d2);
	check(ok1 && ok2 && std::memcmp(d1, d2, 32) == 0, "executable hash available and stable");
	uint8_t zero[32] = {};
	check(std::memcmp(d1, zero, 32) != 0, "executable hash not all zero");
	check(!q::executable_path().empty(), "executable path known");
}
} // namespace

int main()
{
	test_crc();
	test_round_trip();
	test_encode_refuses();
	test_corruption();
	test_truncation();
	test_version_and_fields();
	test_version_1_refused();
	test_check();
	test_io();
	if (sFailures == 0) std::printf("pc_quicksave_file_test: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
