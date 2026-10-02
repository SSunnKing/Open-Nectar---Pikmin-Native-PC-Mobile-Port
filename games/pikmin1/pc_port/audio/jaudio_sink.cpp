// SDL2 adapter for the NextOS JAudio host renderer. Native S16 stereo PCM.
#include "port/audio_sink.h"
#include <SDL.h>
#include <limits>
#include <cstdio>

namespace {
SDL_AudioDeviceID device = 0;
uint64_t submitted = 0;
bool ownsAudio = false;
}
namespace {
// Bringing the audio subsystem up connects SDL's client to the system daemon.
// Retries must not repeat that: only the device open is retried, so keep the
// subsystem alive between attempts and tear it down once, at shutdown.
bool ensureSubsystem()
{
    if (ownsAudio) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    ownsAudio = true;
    return true;
}
}
extern "C" {
void PikiAudioSinkClose()
{
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    submitted = 0;
}
void PikiAudioSinkShutdown()
{
    PikiAudioSinkClose();
    if (ownsAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    ownsAudio = false;
}
int PikiAudioSinkTryOpen(int rate)
{
    PikiAudioSinkClose();
    if (rate <= 0 || !ensureSubsystem()) return 0;
    SDL_AudioSpec spec {};
    spec.freq = rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 1024;
    device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (!device) { PikiAudioSinkClose(); return 0; }
    std::fprintf(stderr, "[jaudio] NextOS DSP, SDL2 S16 stereo %d Hz\n", rate);
    return 1;
}
int PikiAudioSinkOpen(int rate) { return PikiAudioSinkTryOpen(rate); }
int PikiAudioSinkQueue(const int16_t* pcm, size_t frames)
{
    if (!device || !pcm || !frames || frames > std::numeric_limits<Uint32>::max() / 4) return 0;
    if (SDL_QueueAudio(device, pcm, static_cast<Uint32>(frames * 4)) != 0) return 0;
    submitted += frames;
    return 1;
}
int PikiAudioSinkResume() { if (!device) return 0; SDL_PauseAudioDevice(device, 0); return 1; }
void PikiAudioSinkPause() { if (device) SDL_PauseAudioDevice(device, 1); }
int PikiAudioSinkFlush() { return device != 0; }
uint64_t PikiAudioSinkSubmittedFrames() { return submitted; }
uint64_t PikiAudioSinkQueuedFrames() { return device ? SDL_GetQueuedAudioSize(device) / 4 : 0; }
uint64_t PikiAudioSinkPlayedFrames() { const auto q = PikiAudioSinkQueuedFrames(); return q < submitted ? submitted - q : 0; }
int PikiAudioSinkDrained() { return PikiAudioSinkQueuedFrames() == 0; }
int PikiAudioSinkGetProgress(uint64_t* total, uint64_t* queued)
{
    if (!device) return 0;
    if (total) *total = submitted;
    if (queued) *queued = PikiAudioSinkQueuedFrames();
    return 1;
}
}
