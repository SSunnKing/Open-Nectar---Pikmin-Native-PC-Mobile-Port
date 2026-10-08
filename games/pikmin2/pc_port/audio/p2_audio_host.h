#ifndef P2_AUDIO_HOST_H
#define P2_AUDIO_HOST_H

#include "types.h"
#include <stddef.h>

void pc_p2_audio_init();
void pc_p2_audio_set_dsp_tables(const u16* resample, const u16* adpcm);
void pc_p2_audio_render(void* channels, s16* planarOutput, u32 frameSamples);
void pc_p2_audio_queue(const s16* interleaved, size_t frames);

#endif
