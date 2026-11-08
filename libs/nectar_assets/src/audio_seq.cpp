#include "nectar_assets/audio_seq.h"

#include "be_writer.h"

#include <algorithm>

namespace nectar {

namespace {

std::uint32_t be32(const std::uint8_t* p)
{
	return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}

constexpr std::size_t kHeaderSize = 0x20; // BarcHeader
constexpr std::size_t kEntrySize  = 0x20; // BarcEntry
constexpr std::size_t kMaxChildren = 16;  // seqp_::children
constexpr std::uint32_t kMaxOffset = 0xFFFFFF;

void u24(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v)
{
	b[at]     = std::uint8_t(v >> 16);
	b[at + 1] = std::uint8_t(v >> 8);
	b[at + 2] = std::uint8_t(v);
}

/// Escribe la pista en w y devuelve false si no cabe. Las hijas van detrás del
/// bucle de espera del padre; los OpenTrack se rellenan cuando se sabe dónde.
bool emitTrack(const JamIdleTrack& t, BeWriter& w, std::string& error)
{
	if (t.children.size() > kMaxChildren) {
		error = "a JAM track has more than 16 children";
		return false;
	}
	if (t.connect) {
		w.u8(0xD0); // ConnectName
		w.u16(t.connectHigh);
		w.u16(t.connectLow);
		w.u8(0xE5); // ConnectOpen
	}
	std::vector<std::size_t> openAt;
	for (std::size_t i = 0; i < t.children.size(); i++) {
		w.u8(0xC1); // OpenTrack
		w.u8(std::uint8_t(i));
		openAt.push_back(w.b.size());
		w.u8(0);
		w.u8(0);
		w.u8(0);
	}
	const std::size_t loop = w.b.size();
	w.u8(0x88); // espera de 2 bytes
	w.u16(0xFFFF);
	w.u8(0xC7); // Jmp
	const std::size_t jmpAt = w.b.size();
	w.u8(0);
	w.u8(0);
	w.u8(0);
	if (loop > kMaxOffset) {
		error = "JAM sequence is larger than 24-bit offsets reach";
		return false;
	}
	u24(w.b, jmpAt, std::uint32_t(loop));
	for (std::size_t i = 0; i < t.children.size(); i++) {
		const std::size_t at = w.b.size();
		if (at > kMaxOffset) {
			error = "JAM sequence is larger than 24-bit offsets reach";
			return false;
		}
		u24(w.b, openAt[i], std::uint32_t(at));
		if (!emitTrack(t.children[i], w, error)) return false;
	}
	return true;
}

} // namespace

bool readSeqArcHeader(const std::uint8_t* data, std::size_t size, std::vector<SeqArcEntry>& out, std::string& error)
{
	out.clear();
	if (!data || size < kHeaderSize || std::string(reinterpret_cast<const char*>(data), 4) != "BARC") {
		error = "not a BARC header";
		return false;
	}
	const std::uint32_t count = be32(data + 0x0C);
	if (size < kHeaderSize + std::size_t(count) * kEntrySize) {
		error = "BARC header truncated: " + std::to_string(count) + " entries need " + std::to_string(kHeaderSize + count * kEntrySize) + " bytes";
		return false;
	}
	for (std::uint32_t i = 0; i < count; i++) {
		const std::uint8_t* e = data + kHeaderSize + i * kEntrySize;
		SeqArcEntry s;
		const char* name = reinterpret_cast<const char*>(e);
		s.name   = std::string(name, std::find(name, name + 14, '\0'));
		s.used   = (std::uint32_t(e[0x0E]) << 8 | e[0x0F]) == 0xFFFF;
		s.offset = be32(e + 0x18);
		s.size   = be32(e + 0x1C);
		out.push_back(s);
	}
	return true;
}

bool writeJamIdleSequence(const JamIdleTrack& root, std::vector<std::uint8_t>& out, std::string& error)
{
	BeWriter w;
	if (!emitTrack(root, w, error)) return false;
	out = std::move(w.b);
	return true;
}

bool writeSeqArc(const SeqArcInput& in, std::vector<std::uint8_t>& out, std::string& error)
{
	std::vector<std::uint8_t> idle;
	if (!writeJamIdleSequence({}, idle, error)) return false;

	std::uint64_t total = 0;
	std::vector<const SeqArcEntry*> live;
	for (const SeqArcEntry& e : in.layout) {
		if (!e.used || e.size == 0) continue;
		live.push_back(&e);
		total = std::max<std::uint64_t>(total, std::uint64_t(e.offset) + e.size);
	}
	std::sort(live.begin(), live.end(), [](const SeqArcEntry* a, const SeqArcEntry* b) { return a->offset < b->offset; });
	for (std::size_t i = 1; i < live.size(); i++) {
		if (std::uint64_t(live[i - 1]->offset) + live[i - 1]->size > live[i]->offset) {
			error = "sequence entries " + live[i - 1]->name + " and " + live[i]->name + " overlap";
			return false;
		}
	}
	for (const auto& kv : in.sequences) {
		if (kv.first >= in.layout.size() || !in.layout[kv.first].used || in.layout[kv.first].size == 0) {
			error = "sequence " + std::to_string(kv.first) + " has no used entry in the BARC table";
			return false;
		}
		if (kv.second.size() > in.layout[kv.first].size) {
			error = "sequence " + std::to_string(kv.first) + " (" + in.layout[kv.first].name + ") is " + std::to_string(kv.second.size()) +
			        " bytes, its entry holds " + std::to_string(in.layout[kv.first].size);
			return false;
		}
	}
	for (const SeqArcEntry* e : live) {
		if (e->size < idle.size() && !in.sequences.count(std::uint32_t(e - in.layout.data()))) {
			error = "entry " + e->name + " is too small for an idle sequence";
			return false;
		}
	}

	out.assign(std::size_t(total), 0);
	for (std::size_t i = 0; i < in.layout.size(); i++) {
		const SeqArcEntry& e = in.layout[i];
		if (!e.used || e.size == 0) continue;
		const auto it = in.sequences.find(std::uint32_t(i));
		const std::vector<std::uint8_t>& seq = it != in.sequences.end() ? it->second : idle;
		std::copy(seq.begin(), seq.end(), out.begin() + e.offset);
	}
	return true;
}

} // namespace nectar
