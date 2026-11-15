#ifndef PC_FILE_POINTER_H
#define PC_FILE_POINTER_H

// Hit tests for the file-select screens that have no GameCube glyph to click:
// the day chevrons beside the highlighted card, and the copy destination.
// Graph pixels for the chevrons (the same space ogScrFileSelectMgr draws them
// in). The copy rule does not care about pixels: the card row already has
// targets, and this only says what a hit on one of them means.
// No window, pad or disc, so the host test drives it directly.

#include "pc_menu_click.h"

// Click-target kind of the three save cards. ogFileSelect collects them.
constexpr int PC_FILE_CARD_TARGET_KIND = 0;
// The selected card keeps the pointer this far past its cell (drawable px).
constexpr float PC_FILE_CARD_HOLD_PX = 16.0f;

// Drawn "< L" / "> R" beside the highlighted card, plus the menu margin.
// leftOn/rightOn are false when that direction has no further day: the
// chevron is grey and a click there does nothing.
PcMenuClickRect pc_file_day_arrow_left(int cx, int cy);
PcMenuClickRect pc_file_day_arrow_right(int cx, int cy);
// -1 previous day, +1 next day, 0 miss or a grey chevron.
int pc_file_day_arrow_hit(float x, float y, int cx, int cy, bool leftOn, bool rightOn);

enum PcFileCopyAction {
	PC_FILE_COPY_NONE    = 0, // miss, or the card the file is copied from
	PC_FILE_COPY_MOVE    = 1, // pointer moved onto a different destination
	PC_FILE_COPY_CONFIRM = 2, // click on a destination: open the yes/no
};

struct PcFileCopyPointer {
	int action; // PcFileCopyAction
	int slot;   // destination card, or -1
};

// sourceSlot is the card being copied. hitTag is the card under the pointer,
// or -1. A click on another card confirms it. A click on the source does nothing.
PcFileCopyPointer pc_file_copy_pointer(int sourceSlot, int currentTarget, int hitTag, bool click);

#endif
