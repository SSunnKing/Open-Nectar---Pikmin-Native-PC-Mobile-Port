#include "pc_permadeath.h"

#include "Stream.h"
#include <cstdio>

namespace {
// 'NCT1' -- a magic rather than a bare flag, because the bytes at this offset
// in an original save are zeros that happen to be there, not a field anybody
// wrote. Only an exact match means the block is ours.
const int kMagic   = 0x4E435431;
const int kVersion = 2;

const f32 kTekiLifeScale  = 1.33f;
const f32 kNaviDamageScale = 1.5f;

bool sActiveRun  = false;
bool sActiveHard = false;
bool sPending    = false;
bool sPendingHard = false;
bool sSlots[3]     = { false, false, false };
bool sHardSlots[3] = { false, false, false };

struct SaveRules {
	bool permadeath;
	bool hard;
};

SaveRules peekRules(RandomAccessStream& in)
{
	const int resume  = in.getPosition();
	in.setPosition(PC_SAVE_BLOCK_OFFSET);
	const int magic   = in.readInt();
	const int version = in.readInt();
	const int flag    = in.readInt();
	int hardFlag      = 0;
	if (version >= 2)
		hardFlag = in.readInt();
	in.setPosition(resume);

	SaveRules rules = { false, false };
	if (magic != kMagic)
		return rules;
	// Version 1 is an older port that only stored permadeath. Anything newer
	// than we write is not guessed at.
	if (version == 1) {
		rules.permadeath = flag != 0;
		return rules;
	}
	if (version != kVersion)
		return rules;
	rules.permadeath = flag != 0;
	rules.hard       = hardFlag != 0;
	return rules;
}
}

void pc_permadeath_note_slot(int slot, bool on)
{
	if (slot >= 0 && slot < 3) sSlots[slot] = on;
}

bool pc_permadeath_slot(int slot)
{
	return (slot >= 0 && slot < 3) ? sSlots[slot] : false;
}

void pc_hardmode_note_slot(int slot, bool on)
{
	if (slot >= 0 && slot < 3) sHardSlots[slot] = on;
}

bool pc_hardmode_slot(int slot)
{
	return (slot >= 0 && slot < 3) ? sHardSlots[slot] : false;
}

void pc_permadeath_clear_slots(void)
{
	for (int i = 0; i < 3; i++) {
		sSlots[i]     = false;
		sHardSlots[i] = false;
	}
}

bool pc_permadeath_active(void) { return sActiveRun; }
bool pc_hardmode_active(void) { return sActiveHard; }

void pc_permadeath_set_pending(bool on) { sPending = on; }
bool pc_permadeath_pending(void) { return sPending; }
void pc_hardmode_set_pending(bool on) { sPendingHard = on; }
bool pc_hardmode_pending(void) { return sPendingHard; }

void pc_permadeath_begin_new_run(void)
{
	sActiveRun = sPending;
	std::printf("permadeath: new run starts %s\n", sActiveRun ? "PERMADEATH" : "normal");
}

void pc_hardmode_begin_new_run(void)
{
	sActiveHard = sPendingHard;
	std::printf("hardmode: new run starts %s\n", sActiveHard ? "HARD" : "normal");
}

void pc_permadeath_write_block(RandomAccessStream& out, bool permadeath, bool hard)
{
	// The caller has already padded to the checksum trailer, so seek back into
	// the padding, write, and leave the position where it was found.
	const int resume = out.getPosition();
	out.setPosition(PC_SAVE_BLOCK_OFFSET);
	out.writeInt(kMagic);
	out.writeInt(kVersion);
	out.writeInt(permadeath ? 1 : 0);
	out.writeInt(hard ? 1 : 0);
	out.setPosition(resume);
}

bool pc_permadeath_peek_block(RandomAccessStream& in)
{
	return peekRules(in).permadeath;
}

bool pc_hardmode_peek_block(RandomAccessStream& in)
{
	return peekRules(in).hard;
}

void pc_permadeath_read_block(RandomAccessStream& in)
{
	const SaveRules rules = peekRules(in);
	sActiveRun  = rules.permadeath;
	sActiveHard = rules.hard;
	std::printf("save rules: loaded %s%s\n", sActiveRun ? "PERMADEATH" : "normal",
	            sActiveHard ? " HARD" : "");
}

f32 pc_hardmode_teki_life(f32 base)
{
	return sActiveHard ? base * kTekiLifeScale : base;
}

f32 pc_hardmode_navi_damage(f32 base)
{
	return sActiveHard ? base * kNaviDamageScale : base;
}
