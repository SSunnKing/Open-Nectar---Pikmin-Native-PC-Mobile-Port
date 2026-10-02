#ifndef PC_MENU_REPEAT_H
#define PC_MENU_REPEAT_H

#include <cstdint>

/**
 * @brief Turns a held menu input into one-shot presses with a repeat.
 *
 * The keyboard path in the settings menu uses edge detection, so holding a key
 * moves one row. The controller path read raw state instead, so a held stick or
 * button moved a row every frame -- around sixty a second, enough to lap a list
 * before the player let go. This gives the controller the same behaviour: one
 * press on the way down, then a repeat once it has been held for a moment.
 *
 * @param pressed Whether the input is down right now.
 * @param slot    Which input this is. Each direction and button needs its own;
 *                out-of-range slots return false. Only one menu is on screen at
 *                a time, so every menu can share the slots.
 * @param nowMs   Milliseconds from any steady source. Passed in rather than
 *                read here so the behaviour can be tested without waiting.
 * @return true on the frame the input should act.
 */
bool pc_menu_edge(bool pressed, int slot, std::uint32_t nowMs);

/// Milliseconds an input must be held before it starts repeating.
constexpr std::uint32_t kPcMenuRepeatDelayMs = 450;

/// Milliseconds between repeats once it has started.
///
/// Chosen against the shortest menu rather than by feel: at 120 ms a one-second
/// hold acted six times, which is exactly enough to walk a six-row list back to
/// where it started -- the same "laps the entire menu" the player reported, only
/// slower. At 175 ms a held second moves four rows, so no menu wraps by
/// accident, and a long list still crosses at a reasonable pace.
constexpr std::uint32_t kPcMenuRepeatRateMs = 175;

/// Forgets every slot's state. For tests, and for reopening a menu cleanly.
void pc_menu_edge_reset(void);

#endif // PC_MENU_REPEAT_H
