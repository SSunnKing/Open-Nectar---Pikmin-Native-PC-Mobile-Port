// writeItemGenerator (gen.h): one item generator for a .gen file.
//
// The layout is Generator::write / Generator::read in the game
// (src/plugPikiKando/generator.cpp) with GenBase::write (version, doWrite,
// Parameters::write), GenObjectItem::doWrite (genItem.cpp), GenAreaPoint and
// GenTypeOne. On PC readID byte-swaps every 4-byte id, so ids are stored
// reversed ('item' is the text "meti"), and every number is big-endian.
#include "nectar_assets/gen.h"

#include <cstring>

namespace nectar {

namespace {

struct GenWriter {
	std::vector<std::uint8_t>& out;
	void u32(std::uint32_t v)
	{
		for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
	}
	void f32(float f)
	{
		std::uint32_t v;
		std::memcpy(&v, &f, 4);
		u32(v);
	}
	/// A 4-byte id, stored reversed (readID swaps it back on PC).
	void id(const char (&s)[5])
	{
		for (int i = 3; i >= 0; i--) out.push_back(std::uint8_t(s[i]));
	}
	/// One Parameters entry: the 3-character id in the top 24 bits, the size
	/// (4) in the low 8, then the value (Parm<int>::write).
	void parmInt(const char (&name)[4], int value)
	{
		u32((std::uint32_t(std::uint8_t(name[0])) << 24) | (std::uint32_t(std::uint8_t(name[1])) << 16) | (std::uint32_t(std::uint8_t(name[2])) << 8) | 4u);
		u32(std::uint32_t(value));
	}
	void parmEnd() { u32(0xFFFFFFFFu); }
	/// A fixed 32-byte text field, NUL-padded.
	void fixed32(const std::string& s)
	{
		for (std::size_t i = 0; i < 32; i++) out.push_back(i < s.size() ? std::uint8_t(s[i]) : 0);
	}
};

} // namespace

bool writeItemGenerator(const GenItemSpec& in, std::vector<std::uint8_t>& out, std::string& error)
{
	out.clear();
	if (in.memo.size() > 31) {
		error = "generator memo longer than 31 bytes: " + in.memo;
		return false;
	}
	// GenObjectItem::doRead reads the name into a 256-byte buffer, with its NUL.
	if (in.item.empty() || in.item.size() > 255) {
		error = "item generator needs an object type name of 1 to 255 bytes";
		return false;
	}
	GenWriter w { out };
	// Generator: name, version, _70, carry-over flags, memo, position, offset.
	w.id("    "); // mGeneratorName: the disc's editor leaves it blank
	w.id("v0.0"); // mGeneratorVersion
	w.u32(0);     // _70
	w.u32(in.carryOverFlags);
	w.fixed32(in.memo);
	w.f32(in.pos.x);
	w.f32(in.pos.y);
	w.f32(in.pos.z);
	for (int k = 0; k < 3; k++) w.f32(0.0f); // mGenOffset

	// GenObjectItem v0.1: type name (Stream::writeString: length with the NUL,
	// then the bytes), the 32-byte stage and print names (only doors read them;
	// the editor writes " "), then its four parameters.
	w.id("item");
	w.id("v0.1");
	w.u32(std::uint32_t(in.item.size() + 1));
	out.insert(out.end(), in.item.begin(), in.item.end());
	out.push_back(0);
	w.fixed32(" ");
	w.fixed32(" ");
	w.parmInt("p00", in.type);
	w.parmInt("p01", in.day);
	w.parmInt("p02", in.hour);
	w.parmInt("p03", in.stages);
	w.parmEnd();

	// GenAreaPoint: no offset, no parameters.
	w.id("pint");
	w.id("v0.0");
	for (int k = 0; k < 3; k++) w.f32(0.0f);
	w.parmEnd();

	// GenTypeOne: GenType's b00/b01, then the rotation in degrees.
	w.id("1one");
	w.id("v0.0");
	w.parmInt("b00", in.resurrectDays);
	w.parmInt("b01", in.carryOver);
	w.parmInt("p00", in.rotX);
	w.parmInt("p01", in.rotY);
	w.parmInt("p02", in.rotZ);
	w.parmEnd();
	return true;
}

} // namespace nectar
