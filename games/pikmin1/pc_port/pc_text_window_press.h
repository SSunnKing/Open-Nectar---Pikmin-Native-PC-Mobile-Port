#pragma once
/**
 * @file pc_text_window_press.h
 * @brief What a fresh A or B press does in a text window (tutorial notes,
 * Olimar's log, the Voyage Log, result messages).
 *
 * On the GameCube, B shows the whole page and holding A types it faster. The
 * faster typing still adds at most one character per game tick, and at the
 * default 30 Hz the normal speed already does that, so holding A barely
 * changes anything. On PC, A (Space, Enter, left click) shows the whole page
 * as well. A press shows the page or turns it, never both: the press that
 * shows the page is used up, and the page turns on the next press.
 */

enum PcTextWindowPress {
	PC_TEXT_PRESS_NONE,
	PC_TEXT_PRESS_SHOW_PAGE,
	PC_TEXT_PRESS_NEXT_PAGE,
};

inline PcTextWindowPress pc_text_window_press(bool pageShown, bool clickA, bool clickB)
{
	if (!clickA && !clickB) {
		return PC_TEXT_PRESS_NONE;
	}
	return pageShown ? PC_TEXT_PRESS_NEXT_PAGE : PC_TEXT_PRESS_SHOW_PAGE;
}
