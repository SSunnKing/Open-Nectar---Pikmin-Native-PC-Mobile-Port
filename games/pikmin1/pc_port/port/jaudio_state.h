#ifndef PIKMIN_PORT_JAUDIO_STATE_H
#define PIKMIN_PORT_JAUDIO_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The full JAudio mixer is not hosted yet, but movie ADPCM still needs the
 * game's saved BGM/SE levels. Values use JAudio's original Q15 stream table.
 */
uint16_t PikiJAudioBGMStreamLevel(void);
uint16_t PikiJAudioSEStreamLevel(void);
int PikiJAudioStereoOutput(void);

#ifdef __cplusplus
}
#endif

#endif
