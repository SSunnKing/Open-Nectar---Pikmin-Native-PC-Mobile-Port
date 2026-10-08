/**
 * @file pc_dsp_host.cpp
 * @brief Host-side software DSP. See pc_dsp_host.h for the rationale.
 */

#include "pc_dsp_host.h"

#include "pc_aram.h"
#include "jaudio/dspinterface.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

/// Fixed-point format of DSPchannel_::resamplingRatio. syncstream.c plays an
/// unresampled stream at 0x800, which fixes unity at 2048 and makes the ratio
/// Q11 -- the same scale the ADPCM predictor coefficients use.
constexpr u32 kPitchUnity = 0x800;

/// Phase accumulates in Q16 so currentPosFrac keeps its hardware width.
constexpr u32 kPhaseShift = 16;
constexpr u32 kPhaseOne   = 1u << kPhaseShift;

/**
 * Values DSP_SetWaveInfo writes into samplesSourceType. The field is not an
 * enum: it is COMP_BLOCKBYTES[wave->compBlockIdx], the encoded size of one
 * block, and COMP_BLOCKSAMPLES gives how many samples that block holds.
 * Pairing the two tables identifies the format, and DSP_SetWaveInfo's own
 * "< 4 returns early" guard separates the stored formats below from the
 * synthetic oscillator sources, which carry no loop data.
 */
enum SourceType : u16 {
	kSourceAdpcm4 = 9,  ///< 16 samples per 9 bytes.
	kSourceAdpcm2 = 5,  ///< 16 samples per 5 bytes.
	kSourcePcm8   = 8,  ///< One sample per byte.
	kSourcePcm16  = 16, ///< One sample per halfword.
};

/// Bus identifiers DSP_SetBusConnect writes into DSPMixerChannel::id. They are
/// DSP DRAM addresses, spaced 0x60 apart for the six main buffers.
enum BusId : u16 {
	kBusNone       = 0x0000,
	kBusDryLeft    = 0x0D00,
	kBusDryRight   = 0x0D60,
	kBusSurrLeft   = 0x0DC0,
	kBusSurrRight  = 0x0E20,
	kBusAuxLeft    = 0x0E80,
	kBusAuxRight   = 0x0EE0,
};

/// Downmix weight for the surround pair when folded into stereo.
constexpr s32 kSurroundFold = 24576; // 0.75 in Q15.

const u32* sResampleFilter = nullptr;
const u32* sAdpcmFilter    = nullptr;
bool sReady                = false;
bool sPlanarOutput         = false;
u32 sActiveVoices          = 0;

/// Scratch accumulator, one entry per output sample, reused between frames.
std::vector<s32> sBusLeft;
std::vector<s32> sBusRight;

s16 clampToS16(s32 value)
{
	if (value > 32767) {
		return 32767;
	}
	if (value < -32768) {
		return -32768;
	}
	return static_cast<s16>(value);
}

/// Unpacks predictor pair @p index from the engine's Q11 coefficient table.
void adpcmCoefficients(u32 index, s32& coef1, s32& coef2)
{
	if (sAdpcmFilter == nullptr || index >= 16) {
		coef1 = 0;
		coef2 = 0;
		return;
	}
	const u32 packed = sAdpcmFilter[index];
	coef1 = static_cast<s16>(static_cast<u16>(packed >> 16));
	coef2 = static_cast<s16>(static_cast<u16>(packed & 0xFFFF));
}

/**
 * @brief Decodes one 4-bit ADPCM block into @p out.
 *
 * Nintendo's block layout: a header byte carrying the predictor index in the
 * high nibble and the scale exponent in the low nibble, then eight bytes of
 * two samples each, most significant nibble first.
 *
 * @param history1 Previous sample, updated in place.
 * @param history2 Sample before that, updated in place.
 * @return false if the block is not resident in ARAM.
 */
bool decodeAdpcm4Block(u32 aramOffset, s16* out, s16& history1, s16& history2)
{
	const u8* block = pc_aram_read(aramOffset, 9);
	if (block == nullptr) {
		return false;
	}

	const u32 predictor = static_cast<u32>(block[0] >> 4);
	const u32 scale     = static_cast<u32>(block[0] & 0x0F);

	s32 coef1 = 0;
	s32 coef2 = 0;
	adpcmCoefficients(predictor, coef1, coef2);

	for (int i = 0; i < 16; ++i) {
		const u8 byte = block[1 + (i >> 1)];
		s32 nibble    = (i & 1) ? (byte & 0x0F) : (byte >> 4);
		if (nibble > 7) {
			nibble -= 16; // Sign extend the 4-bit residual.
		}

		// Predictor coefficients are Q11, so the history term shifts by 11.
		const s32 predicted = (coef1 * history2 + coef2 * history1) >> 11;
		const s32 sample    = clampToS16((nibble << scale) + predicted);

		out[i]   = static_cast<s16>(sample);
		history1 = history2;
		history2 = static_cast<s16>(sample);
	}
	return true;
}

/// Reads one PCM sample at @p sampleIndex, or 0 if it is outside ARAM.
s16 readPcmSample(const DSPchannel_& channel, u32 sampleIndex)
{
	if (channel.samplesSourceType == kSourcePcm16) {
		const u8* at = pc_aram_read(channel.baseAddress + sampleIndex * 2, 2);
		if (at == nullptr) {
			return 0;
		}
		// Sample banks keep the console's big-endian layout.
		return static_cast<s16>((static_cast<u16>(at[0]) << 8) | at[1]);
	}

	const u8* at = pc_aram_read(channel.baseAddress + sampleIndex, 1);
	if (at == nullptr) {
		return 0;
	}
	return static_cast<s16>(static_cast<s8>(*at) << 8);
}

/// Per-channel decode state the voice parameter block has no room for.
struct HostVoice {
	s16 blockSamples[16] = { 0 };
	u32 blockIndex       = 0xFFFFFFFF; ///< Which ADPCM block is cached.
	s16 history1         = 0;
	s16 history2         = 0;
};

std::vector<HostVoice> sVoices;

/// Fetches sample @p index for @p channel, decoding ADPCM blocks on demand.
s16 fetchSample(DSPchannel_& channel, HostVoice& voice, u32 index)
{
	switch (channel.samplesSourceType) {
	case kSourcePcm8:
	case kSourcePcm16:
		return readPcmSample(channel, index);

	case kSourceAdpcm4: {
		const u32 block = index >> 4;
		if (block != voice.blockIndex) {
			// Decoding is sequential, so a jump backwards or a skip forwards
			// has to restart from the loop history the engine supplied.
			if (block != voice.blockIndex + 1) {
				voice.history1 = channel.loopYN2;
				voice.history2 = channel.loopYN1;
			}
			if (!decodeAdpcm4Block(channel.baseAddress + block * 9,
			                       voice.blockSamples, voice.history1,
			                       voice.history2)) {
				return 0;
			}
			voice.blockIndex = block;
		}
		return voice.blockSamples[index & 0x0F];
	}

	default:
		// Oscillator and other synthetic sources are driven by the engine
		// through useConstantSample rather than by sample memory.
		return channel.constantSample;
	}
}

} // namespace

void pc_dsp_host_init(void)
{
	pc_aram_init();
	sVoices.assign(64, HostVoice());
	sActiveVoices = 0;
	sReady        = true;
}

void pc_dsp_host_set_tables(const u32* resampleFilter, const u32* adpcmFilter)
{
	sResampleFilter = resampleFilter;
	sAdpcmFilter    = adpcmFilter;
}

void pc_dsp_host_shutdown(void)
{
	sVoices.clear();
	sBusLeft.clear();
	sBusRight.clear();
	sResampleFilter = nullptr;
	sAdpcmFilter    = nullptr;
	sActiveVoices   = 0;
	sReady          = false;
}

bool pc_dsp_host_ready(void) { return sReady; }

u32 pc_dsp_host_active_voices(void) { return sActiveVoices; }

void pc_dsp_host_render_frame(DSPchannel_* channels, u32 channelCount, s16* out,
                              u32 frameSamples)
{
	if (out == nullptr || frameSamples == 0) {
		return;
	}
	if (!sReady) {
		pc_dsp_host_init();
	}
	if (sVoices.size() < channelCount) {
		sVoices.resize(channelCount);
	}

	sBusLeft.assign(frameSamples, 0);
	sBusRight.assign(frameSamples, 0);
	sActiveVoices = 0;

	if (channels == nullptr || channelCount == 0) {
		std::memset(out, 0, frameSamples * 2 * sizeof(s16));
		return;
	}

	for (u32 ch = 0; ch < channelCount; ++ch) {
		DSPchannel_& channel = channels[ch];
		if (!channel.enabled || channel.done) {
			continue;
		}

		HostVoice& voice = sVoices[ch];
		bool produced    = false;

		// Phase step. A zero ratio would freeze the voice forever, so treat it
		// as unity rather than stalling the channel.
		const u32 ratio = (channel.resamplingRatio != 0)
		                      ? channel.resamplingRatio
		                      : kPitchUnity;
		const u32 step  = (ratio << kPhaseShift) / kPitchUnity;

		u32 position = channel.currentPosition;
		u32 phase    = channel.currentPosFrac;

		for (u32 sub = 0; sub < frameSamples; sub += PC_DSP_SUBFRAME_SAMPLES) {
			const u32 subEnd =
			    std::min<u32>(sub + PC_DSP_SUBFRAME_SAMPLES, frameSamples);
			const u32 subLength = subEnd - sub;
			if (subLength == 0) {
				break;
			}

			// Volume ramps run per subframe on hardware: each mixer walks from
			// its current level to its target across the 80 samples, then the
			// target becomes current.
			s32 startL = 0, endL = 0, startR = 0, endR = 0;
			for (int m = 0; m < 6; ++m) {
				const DSPMixerChannel& mix = channel.mixChannels[m];
				const s32 from = static_cast<s16>(mix.currentVolume);
				const s32 to   = static_cast<s16>(mix.targetVolume);
				switch (mix.id) {
				case kBusDryLeft:
					startL += from;
					endL += to;
					break;
				case kBusDryRight:
					startR += from;
					endR += to;
					break;
				case kBusSurrLeft:
				case kBusAuxLeft:
					startL += (from * kSurroundFold) >> 15;
					endL += (to * kSurroundFold) >> 15;
					break;
				case kBusSurrRight:
				case kBusAuxRight:
					startR += (from * kSurroundFold) >> 15;
					endR += (to * kSurroundFold) >> 15;
					break;
				case kBusNone:
				default:
					// Reverb and the remaining sends are accumulated by the
					// engine's FX buffers, which this renderer does not drive
					// yet; they contribute nothing to the dry mix.
					break;
				}
			}

			for (u32 i = 0; i < subLength; ++i) {
				if (channel.endRequested || channel.done) {
					break;
				}

				s16 sample;
				if (channel.useConstantSample) {
					sample = channel.constantSample;
				} else {
					sample = fetchSample(channel, voice, position);
				}

				// Linear interpolation between the two straddling samples. The
				// engine's DSPRES_FILTER polyphase table would be more exact;
				// it is deliberately not used until the phase convention is
				// confirmed against captured output.
				const s32 frac = static_cast<s32>(phase & (kPhaseOne - 1));
				s16 next;
				if (channel.useConstantSample) {
					next = sample;
				} else {
					next = fetchSample(channel, voice, position + 1);
				}
				const s32 interpolated =
				    sample + (((next - sample) * frac) >> kPhaseShift);

				// Ramp both sides across the subframe.
				const s32 t = static_cast<s32>(i);
				const s32 n = static_cast<s32>(subLength);
				const s32 volL = startL + ((endL - startL) * t) / n;
				const s32 volR = startR + ((endR - startR) * t) / n;

				const u32 slot = sub + i;
				sBusLeft[slot] += (interpolated * volL) >> 15;
				sBusRight[slot] += (interpolated * volR) >> 15;
				produced = true;

				phase += step;
				position += phase >> kPhaseShift;
				phase &= kPhaseOne - 1;

				// Loop or finish once the source runs out.
				if (channel.remainingLength != 0
				    && position >= channel.remainingLength) {
					if (channel.isLooping) {
						position = channel.loopStartPosition;
						voice.blockIndex = 0xFFFFFFFF;
					} else {
						channel.endReached = DSP_TRUE;
						channel.done       = DSP_TRUE;
						break;
					}
				}
			}

			// Commit the ramp for the mixers that took part.
			for (int m = 0; m < 6; ++m) {
				channel.mixChannels[m].currentVolume =
				    channel.mixChannels[m].targetVolume;
			}
		}

		channel.currentPosition = position;
		channel.currentPosFrac  = static_cast<u16>(phase);
		channel.ageCounter++;
		if (produced) {
			++sActiveVoices;
		}
	}

	if (sPlanarOutput) {
		// Right plane first, then left. See pc_dsp_host.h for why.
		for (u32 i = 0; i < frameSamples; ++i) {
			out[i]                = clampToS16(sBusRight[i]);
			out[frameSamples + i] = clampToS16(sBusLeft[i]);
		}
	} else {
		for (u32 i = 0; i < frameSamples; ++i) {
			out[i * 2]     = clampToS16(sBusLeft[i]);
			out[i * 2 + 1] = clampToS16(sBusRight[i]);
		}
	}
}

void pc_dsp_host_render_frame_planar(DSPchannel_* channels, u32 channelCount,
                                     s16* out, u32 frameSamples)
{
	sPlanarOutput = true;
	pc_dsp_host_render_frame(channels, channelCount, out, frameSamples);
	sPlanarOutput = false;
}
