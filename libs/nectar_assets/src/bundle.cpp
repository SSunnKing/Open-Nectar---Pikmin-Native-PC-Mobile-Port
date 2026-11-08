#include "nectar_assets/bundle.h"

#include "be_writer.h"

#include <set>

namespace nectar {

bool writeBundle(const std::vector<BundleEntry>& entries, std::vector<std::uint8_t>& out, std::string& error)
{
	std::set<std::string> seen;
	for (const BundleEntry& e : entries) {
		if (e.type < BUNDLE_BINARY || e.type > BUNDLE_DCK) {
			error = "bundle entry '" + e.name + "' has type " + std::to_string(e.type) + " (the game knows 0-3)";
			return false;
		}
		if (e.name.empty()) {
			error = "bundle entry with an empty name";
			return false;
		}
		if (!seen.insert(e.name).second) {
			error = "bundle entry '" + e.name + "' appears twice";
			return false;
		}
		if (e.payload.size() > 0x7FFFFFFF) {
			error = "bundle entry '" + e.name + "' is too big for an int32 size";
			return false;
		}
	}
	BeWriter w;
	w.u32(std::uint32_t(entries.size()));
	for (const BundleEntry& e : entries) {
		w.s32(e.type);
		w.s32(std::int32_t(e.payload.size()));
		w.str(e.name);
		w.bytes(e.payload); // pegada al nombre, sin relleno
	}
	out = std::move(w.b);
	return true;
}

bool readBundle(const std::uint8_t* d, std::size_t size, std::vector<BundleEntry>& out, std::string& error)
{
	std::size_t at = 0;
	auto u32 = [&](std::uint32_t& v) {
		if (size - at < 4 || at > size) return false;
		v = (std::uint32_t(d[at]) << 24) | (std::uint32_t(d[at + 1]) << 16) | (std::uint32_t(d[at + 2]) << 8) | d[at + 3];
		at += 4;
		return true;
	};
	out.clear();
	std::uint32_t count;
	if (!u32(count)) {
		error = "bundle: no entry count";
		return false;
	}
	for (std::uint32_t i = 0; i < count; i++) {
		std::uint32_t type, len, nameLen;
		if (!u32(type) || !u32(len) || !u32(nameLen) || nameLen > size - at) {
			error = "bundle: entry " + std::to_string(i) + " header is cut off";
			return false;
		}
		if (type > BUNDLE_DCK) {
			error = "bundle: entry " + std::to_string(i) + " has unknown type " + std::to_string(type);
			return false;
		}
		BundleEntry e;
		e.type = int(type);
		e.name.assign(reinterpret_cast<const char*>(d + at), nameLen);
		at += nameLen;
		if (len > size - at) {
			error = "bundle: entry '" + e.name + "' runs past the end of the file";
			return false;
		}
		e.payload.assign(d + at, d + at + len);
		at += len;
		out.push_back(std::move(e));
	}
	if (at != size) {
		error = "bundle: " + std::to_string(size - at) + " bytes after the last entry";
		return false;
	}
	return true;
}

} // namespace nectar
