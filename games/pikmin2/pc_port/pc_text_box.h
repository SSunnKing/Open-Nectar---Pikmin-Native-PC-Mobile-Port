#pragma once
/**
 * @file pc_text_box.h
 * @brief What Enter, Space and left click do in a Pikmin 2 text box.
 *
 * On the disc, B fast-forwards the letters and A turns a finished page.
 * Pikmin 1 lets A do both: the press that arrives while the page is still
 * typing shows the whole page, and the next press turns it. Enter, Space
 * and left click are that A. A controller button is not: this helper only
 * sees the port key.
 *
 * One hold is one press. The press that shows the page is used up, so the
 * page does not also turn until the key comes up and goes down again.
 */

enum PcTextBoxPress {
	PC_TEXT_BOX_NONE = 0,
	PC_TEXT_BOX_SHOW_PAGE,
	PC_TEXT_BOX_NEXT_PAGE,
};

struct PcTextAccept {
	bool wasDown;
	bool taken;
};

// down is Enter, Space or left click held this sample. Returns true once
// per press.
inline bool pc_text_take_accept(PcTextAccept& state, bool down)
{
	if (!down) {
		state.wasDown = false;
		state.taken   = false;
		return false;
	}
	const bool edge = !state.wasDown;
	state.wasDown   = true;
	if (!edge || state.taken)
		return false;
	state.taken = true;
	return true;
}

inline PcTextBoxPress pc_text_box_port_press(bool pageShown, bool acceptEdge)
{
	if (!acceptEdge)
		return PC_TEXT_BOX_NONE;
	return pageShown ? PC_TEXT_BOX_NEXT_PAGE : PC_TEXT_BOX_SHOW_PAGE;
}

// A cutscene with no text box skips on the same press. A cutscene that is
// waiting on its text box does not: the text box spends the press. Speedrun
// keeps the disc rule.
inline bool pc_cutscene_port_skip(bool speedrun, bool textBoxOpen, bool acceptEdge)
{
	return acceptEdge && !speedrun && !textBoxOpen;
}

// Backspace, right click and mouse Back are the text box's B: show a page
// that is still typing. A shown page is not turned by B.
inline bool pc_text_cancel_wants(bool backspace, bool rightClick, bool mouseBack)
{
	return backspace || rightClick || mouseBack;
}

// One press of Enter, Space or left click. Further calls in the same hold
// return false, so a text box and a cutscene cannot both spend it.
bool pc_text_port_accept_pressed(void);

// One press of Backspace, right click or mouse Back.
bool pc_text_port_cancel_pressed(void);

// Observe releases even when neither a text box nor a cutscene is active.
void pc_text_update_input(bool latchedEnter);
