#ifndef PC_AUDIO_H
#define PC_AUDIO_H

#include "types.h"
#include "Dolphin/ai.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialization & Teardown
bool pc_audio_init(void);
void pc_audio_shutdown(void);
bool pc_audio_play_stx(const char* path);
void pc_audio_stop_stream(void);

// AI Subsystem Emulation
AIDCallback pc_audio_register_dma_callback(AIDCallback callback);
void pc_audio_start_dma(u32 start_addr, u32 length);
void pc_audio_stop_dma(void);
u32  pc_audio_get_dma_bytes_left(void);

// Audio Tick (Simulates AI Hardware Interrupts)
void pc_audio_tick(void);

#ifdef __cplusplus
}
#endif

#endif // PC_AUDIO_H
