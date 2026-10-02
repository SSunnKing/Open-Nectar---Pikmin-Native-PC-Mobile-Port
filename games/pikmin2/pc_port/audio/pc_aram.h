#ifndef PC_ARAM_H
#define PC_ARAM_H

/**
 * @file pc_aram.h
 * @brief Host-backed ARAM for the original JAudio engine.
 *
 * The GameCube kept sample banks in 16 MiB of auxiliary RAM that the DSP read
 * directly, and the decompiled audio code addresses that memory with 32-bit
 * ARAM offsets it stores in Wave_ and DSPchannel_. Emulating the ARAM DMA
 * engine is unnecessary on PC: the port already extracts the sound archives to
 * disk, so this module keeps one flat host allocation of the same size and
 * lets DVDT_LoadtoARAM copy file ranges into it.
 *
 * The result is that every ARAM offset the decompilation produces stays a
 * valid index into host memory, so no call site in src/jaudio has to change.
 */

#include "types.h"

#include <stddef.h>

/// Size of the console's auxiliary RAM, and of the host backing store.
#define PC_ARAM_SIZE (16u * 1024u * 1024u)

/// Allocates the backing store, zero filled. Idempotent.
bool pc_aram_init(void);

/// Releases the backing store.
void pc_aram_shutdown(void);

/**
 * @brief Copies a byte range of a file into host ARAM.
 *
 * @param path   Host path of the archive to read.
 * @param dst    Destination ARAM offset.
 * @param src    Byte offset within the file.
 * @param length Bytes to copy, or 0 to read from @p src to end of file.
 * @return Bytes actually copied, or 0 on failure.
 *
 * A request that would run past the end of the backing store is truncated
 * rather than refused, matching how the console clamped oversized loads, and
 * the shortfall is reported through pc_aram_last_error.
 */
size_t pc_aram_load_file(const char* path, u32 dst, u32 src, u32 length);

/**
 * @brief Resolves an ARAM offset to host memory.
 * @return Pointer to @p offset, or null if @p offset and @p length fall
 *         outside the backing store.
 */
const u8* pc_aram_read(u32 offset, size_t length);

/// Writable view of the same range; null under the same conditions.
u8* pc_aram_write(u32 offset, size_t length);

/// Describes the most recent failure, or an empty string if there was none.
const char* pc_aram_last_error(void);

/// Total bytes loaded since init, for diagnostics.
size_t pc_aram_bytes_loaded(void);

#endif
