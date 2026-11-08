// Write-then-read test for nectar::writeParmFile (parms/*.bin tagged lists).
//
// Oracle: the game's own Parameters::read (src/plugPikiColin/parameters.cpp),
// compiled into this test with the real Stream code and an in-memory RamStream,
// plus the exact byte vector for one entry. parameters.cpp links with stubs for
// System::halt, sysCon and Colour::write only (tests/game_loader_stubs.cpp).
#include "nectar_assets/parms.h"

#include "Colour.h"
#include "Common/String.h"
#include "Parameters.h"
#include "Stream.h"
#include "Vector.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

// A small Parameters block with the same kinds of fields the game's lists use.
struct TestParms : public Parameters {
	TestParms()
	    : Parameters("test")
	    , mFloat(this, 7.0f, 0.0f, 0.0f, "p00", "daySpeed")
	    , mInt(this, 1, 0, 0, "x99", "tekiAbility")
	    , mOther(this, 3.5f, 0.0f, 0.0f, "s01", "mornMid")
	    , mString(this, String("blah", 0), String("", 0), String("", 0), "x98", "gameTitle")
	{
	}
	Parm<f32> mFloat;
	Parm<int> mInt;
	Parm<f32> mOther;
	Parm<String> mString;
};

void readFrom(Parameters& p, const std::vector<std::uint8_t>& bytes, RamStream*& keep)
{
	keep = new RamStream(const_cast<std::uint8_t*>(bytes.data()), int(bytes.size()));
	p.read(*keep);
}
}

int main()
{
	std::string err;
	std::vector<std::uint8_t> out;

	// Spec test vector: ID p00 with a 4-byte payload writes 70 30 30 04.
	{
		nectar::ParmFile f;
		f.lists.push_back({nectar::parmFloat("p00", 1.0f)});
		check(nectar::writeParmFile(f, out, err), "write one entry");
		const std::uint8_t want[] = { 0x70, 0x30, 0x30, 0x04, 0x3F, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };
		check(out.size() == sizeof(want) && std::memcmp(out.data(), want, sizeof(want)) == 0, "exact bytes for p00=1.0f");
	}

	// A bare terminator is a valid file and leaves the code defaults.
	{
		nectar::ParmFile f;
		f.lists.push_back({});
		check(nectar::writeParmFile(f, out, err), "write empty list");
		check(out.size() == 4, "empty list is a 4-byte terminator");
		TestParms p;
		RamStream* s;
		readFrom(p, out, s);
		check(p.mFloat() == 7.0f && p.mInt() == 1 && p.mOther() == 3.5f, "defaults survive a bare terminator");
		check(s->getPending() == 0, "terminator consumed");
		delete s;
	}

	// Overrides, out of order, with an unknown ID in the middle that must be skipped.
	{
		nectar::ParmFile f;
		f.lists.push_back({ nectar::parmInt("x99", -12), nectar::parmFloat("zzz", 9.0f), nectar::parmString("x98", "PIKMIN"),
		                    nectar::parmFloat("p00", 27.5f) });
		check(nectar::writeParmFile(f, out, err), "write overrides");
		TestParms p;
		RamStream* s;
		readFrom(p, out, s);
		check(p.mFloat() == 27.5f, "float override read back");
		check(p.mInt() == -12, "int override read back");
		check(p.mOther() == 3.5f, "absent ID keeps its default");
		check(std::strcmp(p.mString().mString, "PIKMIN") == 0, "string override read back");
		check(s->getPending() == 0, "whole list consumed, unknown ID skipped by size");
		delete s;
	}

	// Two lists in one file (naviMgr.bin / pikiMgr.bin): read in order from one stream.
	{
		nectar::ParmFile f;
		f.lists.push_back({ nectar::parmFloat("p00", 1.5f) });
		f.lists.push_back({ nectar::parmFloat("p00", 2.5f), nectar::parmInt("x99", 4) });
		check(nectar::writeParmFile(f, out, err), "write two lists");
		RamStream s(out.data(), int(out.size()));
		TestParms a, b;
		a.read(s);
		b.read(s);
		check(a.mFloat() == 1.5f && a.mInt() == 1, "first list");
		check(b.mFloat() == 2.5f && b.mInt() == 4, "second list starts after the first terminator");
		check(s.getPending() == 0, "both lists consumed");
	}

	// Rejections.
	{
		nectar::ParmFile f;
		f.lists.push_back({ nectar::ParmEntry{ "ab", {} } });
		check(!nectar::writeParmFile(f, out, err) && !err.empty(), "2-character ID rejected");
		err.clear();
		f.lists[0][0] = nectar::ParmEntry{ "abc", std::vector<std::uint8_t>(256) };
		check(!nectar::writeParmFile(f, out, err) && !err.empty(), "256-byte payload rejected");
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("parms_writer_test: ok\n");
	return 0;
}
