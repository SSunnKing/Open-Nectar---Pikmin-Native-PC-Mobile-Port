#ifndef PIKMIN_PORT_JAUDIO_HOST_H
#define PIKMIN_PORT_JAUDIO_HOST_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-only seams around the two addresses which cannot survive the original
 * GameCube u32 ABI.  Direct PCM is a streaming ring in normal host memory;
 * keep its real pointer out of DSPchannel_ and identify it by voice instead.
 */
void PikiJAudioTick(void);
void StopAudioThread(void);

void PikiJAudioSetDirectPCM(u8 voice, const s16* samples, u32 capacitySamples);
void PikiJAudioClearDirectPCM(u8 voice);

/*
 * The H4M player and JAudio share one physical SDL/ALSA stream.  The movie
 * implementation supplies a strong definition while it owns that stream.
 */
int PikiMovieAudioActive(void);

/*
 * dspinterface.c owns the original, game-matched coefficient tables.  Each
 * u32 contains two signed 16-bit DSP words, most-significant half first.
 */
const u32* PikiJAudioGetResampleTableWords(size_t* wordCount);
const u32* PikiJAudioGetAfcTableWords(size_t* wordCount);

/*
 * interface.c keeps the native menu-volume state.  Notify the host seam when
 * either value changes so H4M mixing observes the same settings as JAudio.
 */
void PikiJAudioSetGameVolumeState(u8 bgm, u8 se);

#ifdef __cplusplus
}
#endif

#endif
