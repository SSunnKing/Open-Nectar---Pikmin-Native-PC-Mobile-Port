#ifndef PC_DSP_HOST_H
#define PC_DSP_HOST_H

/**
 * @file pc_dsp_host.h
 * @brief Host-side software DSP for the original JAudio engine.
 *
 * Replaces only the AI/DSP boundary. Everything above it -- the JAM
 * sequencer, IBNK banks, oscillators and the channel manager in src/jaudio --
 * runs unmodified from the decompilation. No GameCube DSP is emulated and no
 * mailbox traffic is produced, so DSPSendCommands and friends stay inert.
 *
 * The renderer walks the DSPchannel_ voice parameter blocks that
 * dspinterface.c already maintains and mixes them into stereo PCM, matching
 * the console's frame shape: JAC_SUBFRAMES (7) subframes of
 * PC_DSP_SUBFRAME_SAMPLES (80) each, so 560 samples per frame at
 * JAC_DAC_RATE (32028.5 Hz).
 *
 * Sample memory comes from pc_aram, which holds the archives the port already
 * extracts to disk. Every ARAM offset the decompilation produces therefore
 * stays a valid index and no call site in src/jaudio has to change.
 *
 * The channel array is passed in rather than fetched through GetDspHandle so
 * the renderer can be exercised offline without linking the audio engine.
 */

#include "types.h"

struct DSPchannel_;

/// Samples per subframe on the real hardware.
#define PC_DSP_SUBFRAME_SAMPLES 80

/// Prepares host renderer state. Safe to call more than once.
void pc_dsp_host_init(void);

/**
 * @brief Hands the renderer the engine's own DSP tables.
 *
 * Called from the port's DsetupTable, which already receives both. Taking the
 * pointers instead of keeping private copies means the coefficients can never
 * drift from the ones the decompiled code believes are in use.
 *
 * @param resampleFilter 4-tap polyphase resampling table (DSPRES_FILTER).
 * @param adpcmFilter    8 predictor pairs, Q11 (DSPADPCM_FILTER).
 */
void pc_dsp_host_set_tables(const u32* resampleFilter, const u32* adpcmFilter);

/// Discards renderer state.
void pc_dsp_host_shutdown(void);

/**
 * @brief Renders one console audio frame of interleaved stereo samples.
 *
 * @param channels     The voice parameter blocks, normally CH_BUF.
 * @param channelCount Number of blocks, normally CH_BUF_LENGTH (64).
 * @param out          Destination for @p frameSamples * 2 interleaved values.
 * @param frameSamples Samples per channel, normally JAC_FRAMESAMPLES (560).
 *
 * Advances each enabled channel's position, volume ramps and loop state, and
 * writes end-of-sample status back into the voice parameter block so the
 * decompiled driver observes the same completion signals as on hardware.
 */
void pc_dsp_host_render_frame(DSPchannel_* channels, u32 channelCount, s16* out,
                              u32 frameSamples);

/**
 * @brief Renders one frame in the layout the audio engine's own buffers use.
 *
 * cpubuf.c allocates JAC_FRAMESAMPLES << 2 bytes per DSP buffer and treats it
 * as two planes rather than interleaved stereo. The channel order is not the
 * obvious one: aictrl.c hands the planes to Jac_imixcopy(ta, tb, ...), which
 * writes ta to the even output index, and it passes the *second* plane as ta.
 * The even index is the left channel, so:
 *
 *   out[0 .. frameSamples-1]                 right
 *   out[frameSamples .. 2*frameSamples-1]    left
 *
 * The decompiler's local names in cpubuf.c suggest the opposite; the call in
 * Jac_DACCallback is what settles it. Getting this backwards swaps the stereo
 * image, so the ordering is pinned by pc_dsp_host_test.
 */
void pc_dsp_host_render_frame_planar(DSPchannel_* channels, u32 channelCount,
                                     s16* out, u32 frameSamples);

/// True once pc_dsp_host_init has run.
bool pc_dsp_host_ready(void);

/// Voices that produced samples during the most recent frame, for diagnostics.
u32 pc_dsp_host_active_voices(void);

#endif
