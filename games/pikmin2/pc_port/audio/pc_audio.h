#ifndef PC_AUDIO_H
#define PC_AUDIO_H

#include "types.h"
#include "audio/pc_instrument_bank.h"
#include <memory>
#include "Dolphin/ai.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialization & Teardown
bool pc_audio_init(void);
void pc_audio_shutdown(void);
bool pc_audio_play_stx(const char* path);
void pc_audio_stop_stream(void);
// Baja el stream a silencio en `fadeFrames` fotogramas de 60 Hz y lo apaga.
// 0 equivale a pc_audio_stop_stream().
void pc_audio_fade_stream(u32 fadeFrames);
bool pc_audio_load_wave_bank(const char* path);
bool pc_audio_play_sequence(u32 sequence);
void pc_audio_stop_sequence(void);
bool pc_audio_play_sequence_track(u8 track, u32 sequence);
bool pc_audio_sequence_track_active(u8 track);
void pc_audio_stop_sequence_track(u8 track);
void pc_audio_fade_sequence_track(u8 track, float volume, u32 fadeFrames);
bool pc_audio_write_sequence_port(u8 track, u8 port, u16 value);
void pc_audio_set_sequence_layers(u16 enabledMask, float volume, u32 fadeFrames);
void pc_audio_fade_sequence(float volume, u32 fadeFrames);

enum PCAudioBus {
    PC_AUDIO_BUS_STREAM = 0,
    PC_AUDIO_BUS_BGM = 1,
    PC_AUDIO_BUS_SE = 2,
    PC_AUDIO_BUS_DMA = 3,
    PC_AUDIO_BUS_COUNT = 4,
};

typedef struct PCAudioMetrics {
    u32 sampleRate;
    u32 deviceBufferFrames;
    u64 callbacks;
    u64 mixedFrames;
    u32 peakActiveVoices;
    u32 voiceSteals;
    u32 voiceRejects;
    u32 dmaUnderruns;
    u32 clips;
    u64 limitedFrames;
    u64 bgmTicks;
    u64 bossTicks;
    u64 seTicks;
    u64 eventTicks;
} PCAudioMetrics;

int pc_audio_play_wave(u32 waveSystem, u32 archive, u32 waveIndex,
                       float volume, float pan, bool looping);
int pc_audio_play_wave_ex(u32 waveSystem, u32 archive, u32 waveIndex,
                          float volume, float pan, bool looping,
                          PCAudioBus bus, u8 priority);
int pc_audio_play_note(u32 virtualBank, u32 program, u8 key, u8 velocity,
                       u32 waveScene, float volume, float pan,
                       PCAudioBus bus, u8 priority, float trackPitch,
                       u8 cutoff = 127, float fxMix = 0.0f,
                       float dolby = 0.0f,
                       std::shared_ptr<const std::vector<PCInstrumentOscillator>> envelope = {});
void pc_audio_stop_wave(int voice);
void pc_audio_update_wave(int voice, float volume, float pan, float pitch,
                          u8 cutoff = 127, float fxMix = 0.0f,
                          float dolby = 0.0f);
// Starts a linear release measured in output sample frames. A value of zero
// has the same immediate-stop semantics as pc_audio_stop_wave().
void pc_audio_release_wave(int voice, u32 releaseFrames, u16 releaseParam = 0);
void pc_audio_report_levels(void);
void pc_audio_set_bus_volume(PCAudioBus bus, float volume);
void pc_audio_stop_bus(PCAudioBus bus);
void pc_audio_set_stereo(bool stereo);
bool pc_audio_send_system_se(u16 id, bool stop);
bool pc_audio_send_orima_se(u16 id, bool stop, bool pikiSound);
bool pc_audio_write_se_port(u8 track, u8 port, u16 value);
void pc_audio_set_se_track_volume(u8 track, float volume);
void pc_audio_set_se_track_paused(u8 track, bool paused);
bool pc_audio_send_event_action(u8 event, u8 slot, u16 command, bool stop);
// Called when an event action reports that it has finished, so the caller can
// free the slot it was occupying.
void pc_audio_set_event_action_finished_hook(void (*hook)(u8 event, u8 slot));
void pc_audio_set_event_mix(u8 event, float volume, float pan);
void pc_audio_set_events_paused(bool paused);
u32 pc_audio_wave_count(void);
u32 pc_audio_get_active_voice_count(void);
u32 pc_audio_get_clip_count(void);
void pc_audio_get_metrics(PCAudioMetrics* metrics);
void pc_audio_reset_metrics(void);

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
