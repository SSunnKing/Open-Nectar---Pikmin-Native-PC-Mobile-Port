#ifndef PIKMIN_PORT_JAUDIO_BANK_HOST_H
#define PIKMIN_PORT_JAUDIO_BANK_HOST_H

#include "types.h"

typedef struct Ibnk_ Ibnk_;
typedef struct Wsys_ Wsys_;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert and register every IBNK/WSYS embedded in a pikibank.bx image.
 *
 * The source image is the unmodified, big-endian GameCube file loaded by
 * DVDT.  Runtime objects are separately allocated with OSAlloc2, so none of
 * the 32-bit offsets in the image are ever treated as native pointers.
 */
BOOL JAudioHost_LoadBX(const void* data, u32 size);

/*
 * Registration seams implemented by bankread.c and waveread.c.  Keeping the
 * table ownership there preserves Bank_Get/WaveidToWavegroup and all of the
 * original JAudio callers while the host loader supplies native objects.
 */
BOOL Bank_Regist_Host(Ibnk_* ibnk, u32 bankIndex);
BOOL Wavegroup_Regist_Host(Wsys_* wsys, u32 wavegroupIndex);

#ifdef __cplusplus
}
#endif

#endif
