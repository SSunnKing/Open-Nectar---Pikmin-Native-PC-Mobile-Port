#ifndef PC_PERMADEATH_H
#define PC_PERMADEATH_H

#include "types.h"

class RandomAccessStream;

/*
 * Permadeath and Hard mode are properties of a save file, not of the port's
 * configuration: they are chosen when the file is created and then travel
 * with it. Loading a normal file cannot put you in a run that deletes itself
 * or silently turn Hard rules on; loading a marked file cannot quietly drop
 * them.
 *
 * The flags ride in a small block at a fixed offset near the end of the save
 * file, in space the original game pads with zeros and inside the region its
 * checksum already covers. An original save has no block there and reads back
 * as a normal, non-Hard run; a build without this port reads our files as it
 * always did.
 *
 * Version 1 wrote only permadeath. Version 2 appends Hard. An older file
 * still reads as whatever permadeath it had, with Hard off.
 */

/// Byte offset of the port block inside a save file's 0x8000 block. The game
/// writes up to about 0x6E20 and its checksum trailer starts at 0x7FF8.
#define PC_SAVE_BLOCK_OFFSET (0x7F00)

/// Hard-mode field cap and day length. F1 cannot raise these on a Hard file.
#define PC_HARDMODE_PIKI_LIMIT  (80)
#define PC_HARDMODE_DAY_MINUTES (8)

/* Per-slot flags for the file-select screen, filled when the card is listed.
   The screen needs to mark a file before it is loaded, and loading is exactly
   what it must not do to answer the question. */
void pc_permadeath_note_slot(int slot, bool on);
bool pc_permadeath_slot(int slot);
void pc_hardmode_note_slot(int slot, bool on);
bool pc_hardmode_slot(int slot);
void pc_permadeath_clear_slots(void);

/// True while the run in progress is a permadeath run.
bool pc_permadeath_active(void);

/// True while the run in progress uses Hard rules.
bool pc_hardmode_active(void);

/// Sets what a newly created file will be. Chosen on the new-game screens.
void pc_permadeath_set_pending(bool on);
bool pc_permadeath_pending(void);
void pc_hardmode_set_pending(bool on);
bool pc_hardmode_pending(void);

/// Applies the pending choices to the run that is starting.
void pc_permadeath_begin_new_run(void);
void pc_hardmode_begin_new_run(void);

/// Writes the port block. Call with the stream positioned anywhere: it seeks.
void pc_permadeath_write_block(RandomAccessStream& out, bool permadeath, bool hard);

/// Reads the port block and adopts both rules. A file with no block is a
/// normal, non-Hard run.
void pc_permadeath_read_block(RandomAccessStream& in);

/// Reads one flag without adopting either, for listing files.
bool pc_permadeath_peek_block(RandomAccessStream& in);
bool pc_hardmode_peek_block(RandomAccessStream& in);

/// Hard-mode combat scales. Identity when Hard is off.
f32 pc_hardmode_teki_life(f32 base);
f32 pc_hardmode_navi_damage(f32 base);

#endif // PC_PERMADEATH_H
