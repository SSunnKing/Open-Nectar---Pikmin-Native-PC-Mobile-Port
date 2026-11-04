#ifndef PC_BUTTON_PROMPT_H
#define PC_BUTTON_PROMPT_H

// On-screen button prompts, such as "Y Ship's Log" on the world map or "A Read Diary", as
// mouse targets. Nothing here knows a screen: every GameCube button picture the game draws
// goes through P2DPane::draw, which reports it here with the row it sits on (the glyph plus
// the label, dots and bubble beside it). A left click on that row is the same button press
// as the key, sent into the pad for one logical sample, so the screen's own code decides
// whether the press does anything. The pointer shows the hand over the row.
//
// The set is rebuilt every drawn frame: a prompt that is hidden, not drawn, or on a screen
// that is gone has no target. P2DPane.cpp holds the draw-side glue, pc_window.cpp the
// pad-side glue; this file has no window, renderer or disc, so tests drive it directly.
//
// PC_BUTTON_PROMPT_LOG=1 in the environment logs each change to the drawn prompts (screen, texture,
// button, target or the reason for none) and each prompt click, to stderr.

#include "pc_menu_click.h"

#include <cstdint>
#include <cstdio>

// GameCube pad bits, as in include/Dolphin/pad.h (P2DPane.cpp checks they match).
enum : std::uint16_t {
	PC_BUTTON_PROMPT_PAD_Z     = 0x0010,
	PC_BUTTON_PROMPT_PAD_R     = 0x0020,
	PC_BUTTON_PROMPT_PAD_L     = 0x0040,
	PC_BUTTON_PROMPT_PAD_A     = 0x0100,
	PC_BUTTON_PROMPT_PAD_B     = 0x0200,
	PC_BUTTON_PROMPT_PAD_X     = 0x0400,
	PC_BUTTON_PROMPT_PAD_Y     = 0x0800,
	PC_BUTTON_PROMPT_PAD_START = 0x1000,
};

enum {
	PC_BUTTON_PROMPT_MAX         = 16,
	PC_BUTTON_PROMPT_SIBLING_MAX = 64,
};

// The button a glyph stands for, from pc_keycaps_tag_for_texture's tag ('a', 'b', 'x', 'y',
// 'z', 'l', 'r', 'p' for Start). 0 for the sticks ('c', 's'): those glyphs label a
// direction, not a press, so they are drawn but never a target. 0 for any other tag.
std::uint16_t pc_button_prompt_button_for_tag(char tag);
const char* pc_button_prompt_button_name(std::uint16_t button);

// Screens whose button pictures are a diagram, not prompts (the controller page). Matched
// on the .blo file name, without folders.
bool pc_button_prompt_screen_opted_out(const char* bloName);

// A pane beside the glyph under the same parent, in the parent's layout space.
struct PcButtonPromptSibling {
	PcMenuClickRect rect;
	bool glyph; // another button picture: its own prompt, never part of this one
};

// The prompt row around a glyph, in the parent's layout space: the glyph, plus each sibling
// picture or text that shares its row (overlaps at least half the shorter height) and
// touches the row so far within a small gap, repeated until nothing more joins. A pane nearer
// another glyph on the row is that prompt's ("B Back  A End"). Panes much
// taller than the glyph, or wider than half the screen, are backgrounds and never join.
PcMenuClickRect pc_button_prompt_group(PcMenuClickRect glyph, const PcButtonPromptSibling* siblings, int count);

// Draw side, once per drawn glyph. windowRect: the row in drawable pixels (origin top left).
// registered=false keeps it out of the targets but still logs it with the reason.
void pc_button_prompt_add(std::uint16_t button, PcMenuClickRect windowRect, const char* screen, const char* texName,
                         bool registered, const char* reason);
// End of a drawn frame (buffer swap): what was added since becomes the target set. Two
// glyphs of the same button that overlap (a glyph and its shadow) become one target.
void pc_button_prompt_frame_end(void);
// Targets in the current set.
int pc_button_prompt_count(void);

// Pad side, once per logical input sample, after the click and pointer are posted
// (pc_menu_click) and before mouse bindings are read. live: the pointer is free for menus
// (not a running level that owns it, no settings overlay). leftHeld: the left mouse button
// is down now. A pending click on a target is taken and its button returned, to be ORed
// into the pad for this sample. The pointer over a target claims the pointer menu (so the
// click is not also A through a mouse binding) and asks for the hand; the claim holds while
// the button that clicked stays down, so a long press does not turn into A on release.
std::uint16_t pc_button_prompt_sample(bool live, int drawableW, int drawableH, bool leftHeld);

void pc_button_prompt_reset(void);
// Where the log goes; nullptr turns it off. Defaults to stderr when PC_BUTTON_PROMPT_LOG is set
// to anything but 0.
void pc_button_prompt_set_log(std::FILE* out);

#endif
