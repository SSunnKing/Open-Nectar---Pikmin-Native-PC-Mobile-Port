#ifndef PIKMIN_PORT_AUDIO_SINK_H
#define PIKMIN_PORT_AUDIO_SINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Small SDL3 playback seam shared by the host audio ports.
 *
 * Input is always signed 16-bit little-endian stereo. SDL negotiates the
 * physical device format and rate. Open leaves the device paused so a caller
 * can prebuffer audio and its matching video before starting the clock.
 */
int PikiAudioSinkOpen(int sampleRate);
/* Single device-open attempt; unlike Open, this never sleeps and retries. */
int PikiAudioSinkTryOpen(int sampleRate);
int PikiAudioSinkQueue(const int16_t* samples, size_t frameCount);
int PikiAudioSinkResume(void);
int PikiAudioSinkFlush(void);
/* Returns 1 when drained, 0 while draining, and -1 on a device/query error. */
int PikiAudioSinkDrained(void);
int PikiAudioSinkGetProgress(uint64_t* submittedFrames, uint64_t* queuedFrames);
void PikiAudioSinkPause(void);
void PikiAudioSinkClose(void);
/* Closes the device and releases the audio subsystem. Close() keeps the
   subsystem up so a failed open can be retried cheaply; this is the real
   teardown, for process shutdown. */
void PikiAudioSinkShutdown(void);
uint64_t PikiAudioSinkSubmittedFrames(void);
uint64_t PikiAudioSinkQueuedFrames(void);
uint64_t PikiAudioSinkPlayedFrames(void);

#ifdef __cplusplus
}
#endif

#endif
