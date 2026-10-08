/**
 * @file pc_dsp_host.cpp
 * @brief Host-side software DSP. See pc_dsp_host.h for the rationale.
 */

#include "pc_dsp_host.h"

#include "pc_aram.h"
#ifndef BEGIN_SCOPE_EXTERN_C
#define BEGIN_SCOPE_EXTERN_C extern "C" {
#define END_SCOPE_EXTERN_C }
#endif
#include "jaudio/dspinterface.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Global operator new is routed through JKRHeap::sCurrentHeap by the game.
// The audio thread can run while MoviePlayer has made its disposable movie
// heap current, so persistent STL storage must explicitly use the host heap.
// Otherwise freeAll() invalidates the vectors and the next mix pass writes
// through their stale capacity into newly-created movie objects.
bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);

namespace {

struct HostAllocScope {
	bool mPrevious;
	HostAllocScope()
	    : mPrevious(pc_host_alloc_active())
	{
		pc_host_alloc_set(true);
	}
	~HostAllocScope() { pc_host_alloc_set(mPrevious); }
};

/// Fixed-point format of DSPchannel_::resamplingRatio. syncstream.c plays an
/// unresampled stream at 0x800, which fixes unity at 2048 and makes the ratio
/// Q11 -- the same scale the ADPCM predictor coefficients use.
u32 sPitchUnity = 0x800;

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
	// The Pikmin 2 table is supplied as native u16 coefficient pairs. Reading
	// it as packed u32 words reverses every pair on little-endian hosts.
	const u16* coefficients = reinterpret_cast<const u16*>(sAdpcmFilter);
	coef1 = static_cast<s16>(coefficients[index * 2]);
	coef2 = static_cast<s16>(coefficients[index * 2 + 1]);
}

/**
 * @brief Decodes one 4-bit ADPCM block into @p out.
 *
 * Nintendo's block layout: a header byte carrying the scale exponent in the
 * high nibble and the predictor index in the low nibble, then eight bytes of
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

	// AFC stores the scale in the high nibble and the coefficient pair in the
	// low nibble. The old code interpreted the header in the opposite order.
	const u32 scale     = static_cast<u32>(block[0] >> 4);
	const u32 predictor = static_cast<u32>(block[0] & 0x0F);

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
	s32 lastEndL         = 0;
	s32 lastEndR         = 0;
	/// Ventana de 4 muestras de entrada [tapNext-4, tapNext-1] para el filtro
	/// polifasico. Solo avanza, asi el ADPCM se decodifica en orden.
	s16 taps[4]  = { 0, 0, 0, 0 };
	u32 tapNext  = 0xFFFFFFFF; ///< 0xFFFFFFFF = ventana invalida.
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
	if (sReady) {
		return;
	}
	HostAllocScope hostAlloc;
	pc_aram_init();
	sVoices.assign(64, HostVoice());
	sBusLeft.reserve(560);
	sBusRight.reserve(560);
	sActiveVoices = 0;
	sReady        = true;
}

void pc_dsp_host_set_tables(const u32* resampleFilter, const u32* adpcmFilter)
{
	sResampleFilter = resampleFilter;
	sAdpcmFilter    = adpcmFilter;
}

void pc_dsp_host_set_pitch_unity(u32 unity)
{
	sPitchUnity = unity ? unity : 0x800;
}

void pc_dsp_host_shutdown(void)
{
	HostAllocScope hostAlloc;
	std::vector<HostVoice>().swap(sVoices);
	std::vector<s32>().swap(sBusLeft);
	std::vector<s32>().swap(sBusRight);
	sResampleFilter = nullptr;
	sAdpcmFilter    = nullptr;
	sActiveVoices   = 0;
	sReady          = false;
}

bool pc_dsp_host_ready(void) { return sReady; }

u32 pc_dsp_host_active_voices(void) { return sActiveVoices; }

// DsetMixerLevel puede llegar desde el hilo del juego mientras el de audio
// mezcla.
std::atomic<double> sMixerLevel { 1.2 };

void pc_dsp_host_set_mixer_level(float level) { sMixerLevel = std::max(0.0f, level); }

void busLogVoice(const DSPchannel_& channel);
void busLogFrame();

void pc_dsp_host_render_frame(DSPchannel_* channels, u32 channelCount, s16* out,
                              u32 frameSamples)
{
	if (out == nullptr || frameSamples == 0) {
		return;
	}
	if (!sReady) {
		pc_dsp_host_init();
	}
	HostAllocScope hostAlloc;
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

	if (std::getenv("PIKMIN_AUDIO_LOG")) {
		static unsigned n;
		if (++n % 57 == 0) {
			unsigned en = 0, dn = 0;
			for (u32 ch = 0; ch < channelCount; ++ch) {
				en += channels[ch].enabled != 0;
				dn += channels[ch].done != 0;
			}
			std::fprintf(stderr, "[PC DSP] enabled=%u done=%u\n", en, dn);
		}
	}
	static const bool busLog = [] {
		const char* v = std::getenv("PIKMIN_AUDIO_BUSLOG");
		return v && v[0] && v[0] != '0';
	}();
	for (u32 ch = 0; ch < channelCount; ++ch) {
		DSPchannel_& channel = channels[ch];
		if (!channel.enabled || channel.done) {
			continue;
		}

		HostVoice& voice = sVoices[ch];
		if (busLog) busLogVoice(channel);
		if (channel.resetVpb) {
			// Expand the source end stored in the initial VPB into the running
			// state, as the console DSP does when a voice starts.
			channel.remainingLength = channel.loopStartPosition;
			voice.blockIndex         = 0xFFFFFFFF;
			voice.history1           = 0;
			voice.history2           = 0;
			voice.tapNext            = 0xFFFFFFFF;
		}
		bool produced    = false;
		if (std::getenv("PIKMIN_AUDIO_LOG")) {
			static unsigned n;
			if (n++ < 12) {
				std::fprintf(stderr, "[PC DSP] voice ch=%u src=%u base=0x%x rem=%u pos=%u ratio=%u loop=%u endReq=%u const=%u mix:",
				             ch, (unsigned)channel.samplesSourceType, channel.baseAddress, channel.remainingLength, channel.currentPosition,
				             (unsigned)channel.resamplingRatio, (unsigned)channel.isLooping, (unsigned)channel.endRequested,
				             (unsigned)channel.useConstantSample);
				for (int m = 0; m < 6; ++m)
					std::fprintf(stderr, " [%04x cur=%d tgt=%d]", channel.mixChannels[m].id, (s16)channel.mixChannels[m].currentVolume,
					             (s16)channel.mixChannels[m].targetVolume);
				std::fprintf(stderr, " s0=%d s1=%d\n", fetchSample(channel, voice, channel.currentPosition), fetchSample(channel, voice, channel.currentPosition + 1));
			}
		}

		// Phase step. A zero ratio would freeze the voice forever, so treat it
		// as unity rather than stalling the channel.
		const u32 ratio = (channel.resamplingRatio != 0)
		                      ? channel.resamplingRatio
		                      : sPitchUnity;
		const u32 step  = (ratio << kPhaseShift) / sPitchUnity;

		u32 position = channel.currentPosition;
		u32 phase    = channel.currentPosFrac;

		for (u32 sub = 0; sub < frameSamples; sub += PC_DSP_SUBFRAME_SAMPLES) {
			const u32 subEnd =
			    std::min<u32>(sub + PC_DSP_SUBFRAME_SAMPLES, frameSamples);
			const u32 subLength = subEnd - sub;
			if (subLength == 0) {
				break;
			}

			// Volume ramps run per subframe. Pikmin 2's automatic mixer
			// (TChannel::setAutoMixer in JASDSPInterface.cpp) packs
			// (pan << 8) | dolby into mVolumeAndPan (@0x50) and fxMix into the
			// high byte of mFxMixAndDolby (@0x52). mCurrentMixerValue (@0x54)
			// ramps exponentially toward the mMixerLevel target (@0x56); the
			// value is Q14 with 0x4000 = unity, since JASDriverIF builds the
			// level as autoMixerLevel * 16383.5.
			s32 startL = 0, endL = 0, startR = 0, endR = 0;
			if (channel.useDolbyVolume) {
				auto sineGain = [](u8 position) -> s32 {
					constexpr double kHalfPi = 1.57079632679489661923;
					return static_cast<s32>(std::sin(static_cast<double>(position) * kHalfPi / 127.0) * 32767.0);
				};
				const u8 pan      = static_cast<u8>(channel.dolbyVoicePosition >> 8) & 0x7f;
				const u8 surround = static_cast<u8>(channel.dolbyVoicePosition) & 0x7f;
				// fxMix (0..127) feeds the rear buses; with no fx the voice
				// plays dry out front, matching the DSP's fx routing.
				const s32 fxSend  = static_cast<s32>(static_cast<u8>(static_cast<u16>(channel.dolbyReverbFactor) >> 8)) << 8;
				const s32 right   = sineGain(pan);
				const s32 left    = sineGain(pan ^ 0x7f);
				const s32 back    = (sineGain(surround) * fxSend) >> 15;
				const s32 front   = sineGain(surround ^ 0x7f);
				// Q15 * Q15 -> Q15 (con >> 16 el auto-mixer salía a la mitad).
				const s32 frontL  = (left * front) >> 15;
				const s32 backL   = (left * back) >> 15;
				const s32 frontR  = (right * front) >> 15;
				const s32 backR   = (right * back) >> 15;
				const s32 leftPan  = frontL + ((backL * kSurroundFold) >> 15);
				const s32 rightPan = frontR + ((backR * kSurroundFold) >> 15);

				// Exponential ramp of the mixer value toward its level, the
				// "current += (target - current) >> shift" update style the
				// console DSP runs per subframe. A finish request retargets
				// the ramp to zero so the voice fades out instead of stopping
				// dead, and the channel is done once the fade lands.
				s32 mixer  = static_cast<u16>(channel.dolbyVolumeCurrent);
				s32 target = channel.endRequested ? 0 : static_cast<u16>(channel.dolbyVolumeTarget);
				// Escala del microcódigo real (Dolphin, ZeldaAudioRenderer con
				// MAKE_DOLBY_LOUDER para Pikmin 2): cuadrante = (pan * mixer) >> 15
				// y la voz se suma con (vol * muestra) >> 16. Aquí se suma con
				// >> 15, así que la ganancia es mixer / 2. Antes iba mixer * 2:
				// cuatro veces más fuerte, saturaba y sonaba distorsionado.
				const s32 fromGain = mixer >> 1;
				mixer += (target - mixer) >> 5;
				if (((target - mixer) >> 5) == 0) {
					mixer = target; // Converge exactly instead of crawling.
				}
				channel.dolbyVolumeCurrent = static_cast<s16>(mixer);
				const s32 toGain = mixer >> 1;
				startL = (leftPan * fromGain) >> 15;
				endL   = (leftPan * toGain) >> 15;
				startR = (rightPan * fromGain) >> 15;
				endR   = (rightPan * toGain) >> 15;
				voice.lastEndL = endL;
				voice.lastEndR = endR;
				if (channel.endRequested && mixer == 0) {
					channel.endReached = DSP_TRUE;
					channel.done       = DSP_TRUE;
				}
			} else {
				for (int m = 0; m < 6; ++m) {
					const DSPMixerChannel& mix = channel.mixChannels[m];
					// El DSP suma cada bus con (vol * muestra) >> 16 (Dolphin,
					// AddBuffersWithVolumeRamp); aquí se suma con >> 15.
					const s32 from = static_cast<s16>(mix.currentVolume) >> 1;
					const s32 to   = static_cast<s16>(mix.targetVolume) >> 1;
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
					voice.lastEndL = endL;
					voice.lastEndR = endR;
				}
			}

			// A finish request on a voice without the auto-mixer stops it
			// here, and the DSP reports it done so JAS reclaims the channel.
			// Without this the voice stayed enabled and silent forever: every
			// stopped sound leaked one of the 64 channels until none were left
			// and the game went mute a few minutes in.
			if (channel.endRequested && !channel.useDolbyVolume) {
				channel.endReached = DSP_TRUE;
				channel.done       = DSP_TRUE;
			}
			for (u32 i = 0; i < subLength; ++i) {
				if (channel.done
				    // Auto-mixer voices keep playing through a finish
				    // request so the mixer fade below is audible; the
				    // fade sets done once it reaches silence.
				    || (channel.endRequested && !channel.useDolbyVolume)) {
					break;
				}

				s32 interpolated;
				if (channel.useConstantSample) {
					interpolated = channel.constantSample;
				} else {
					// Ventana s[p-1..p+2]. Antes se leia p y p+1 por separado:
					// al cruzar de bloque el ADPCM retrocedia y se redecodificaba
					// con la historia del bucle, corrompiendo la senal cada 16
					// muestras (sonido "pixelado").
					if (voice.tapNext == 0xFFFFFFFF || voice.tapNext > position + 3 || voice.tapNext + 4 < position + 3) {
						voice.tapNext = position >= 1 ? position - 1 : 0;
						for (int k = 0; k < 4; ++k)
							voice.taps[k] = 0;
					}
					while (voice.tapNext < position + 3) {
						u32 idx = voice.tapNext;
						s16 in  = 0;
						if (channel.remainingLength != 0 && idx >= channel.remainingLength) {
							if (channel.isLooping && channel.remainingLength > channel.loopAddress) {
								idx = channel.loopAddress + (idx - channel.remainingLength);
								in  = fetchSample(channel, voice, idx);
							}
						} else {
							in = fetchSample(channel, voice, idx);
						}
						voice.taps[0] = voice.taps[1];
						voice.taps[1] = voice.taps[2];
						voice.taps[2] = voice.taps[3];
						voice.taps[3] = in;
						voice.tapNext++;
					}
					const u32 frac = phase & (kPhaseOne - 1);
					if (sResampleFilter) {
						// DSPRES_FILTER: 64 fases x 4 coeficientes Q15 (fila 0 centrada
						// en s[p], fila 63 en s[p+1]); despues vienen otras tablas.
						const u16* table = reinterpret_cast<const u16*>(sResampleFilter);
						const u16* c     = &table[((frac >> 10) & 0x3F) * 4];
						const s32 acc    = voice.taps[0] * static_cast<s16>(c[0]) + voice.taps[1] * static_cast<s16>(c[1])
						              + voice.taps[2] * static_cast<s16>(c[2]) + voice.taps[3] * static_cast<s16>(c[3]);
						interpolated = clampToS16(acc >> 15);
					} else {
						const s32 s0 = voice.taps[1], s1 = voice.taps[2];
						interpolated = s0 + (((s1 - s0) * static_cast<s32>(frac)) >> kPhaseShift);
					}
				}

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
						// La ventana ya leyo lo posterior al final desde el
						// inicio del bucle: se desplaza con la posicion.
						if (voice.tapNext != 0xFFFFFFFF && channel.remainingLength > channel.loopAddress) {
							voice.tapNext -= channel.remainingLength - channel.loopAddress;
						}
						position = channel.loopAddress + (position - channel.remainingLength);
					} else {
						channel.endReached = DSP_TRUE;
						channel.done       = DSP_TRUE;
						break;
					}
				}
			}

			// Commit the ramp for the mixer mode that took part. The
			// auto-mixer branch already advanced and stored mCurrentMixerValue.
			if (!channel.useDolbyVolume) {
				for (int m = 0; m < 6; ++m) {
					channel.mixChannels[m].currentVolume = channel.mixChannels[m].targetVolume;
				}
			}
		}

		channel.currentPosition = position;
		channel.currentPosFrac  = static_cast<u16>(phase);
		channel.resetVpb        = DSP_FALSE;
		channel.ageCounter++;
		if (produced) {
			++sActiveVoices;
		}
	}

	if (busLog) busLogFrame();
	if (std::getenv("PIKMIN_AUDIO_LOG")) {
		static unsigned n2;
		if (sActiveVoices > 0) {
			s32 p = 0;
			for (u32 i = 0; i < frameSamples; ++i) {
				p = std::max(p, std::abs(sBusLeft[i]));
				p = std::max(p, std::abs(sBusRight[i]));
			}
			std::fprintf(stderr, "[PC DSP] mixed voices=%u buspeak=%d first: auto=%u volPan=%04x fx=%04x cur=%d lvl=%d endL=%d endR=%d\n",
			             sActiveVoices, (int)p,
			             (unsigned)channels[0].useDolbyVolume, channels[0].dolbyVoicePosition,
			             (unsigned)(u16)channels[0].dolbyReverbFactor, (int)channels[0].dolbyVolumeCurrent,
			             (int)channels[0].dolbyVolumeTarget, (int)sVoices[0].lastEndL, (int)sVoices[0].lastEndR);
		}
	}
	// Volumen maestro de la salida del DSP (efectos y secuencias): el nivel
	// que fija el juego (DsetMixerLevel, 1.2 en Pikmin 2) como en la consola,
	// por PIKMIN_SE_GAIN (1.0 por defecto) para ajustarlo a mano.
	static const double sTrim = [] {
		double gain = 1.0;
		if (const char* env = std::getenv("PIKMIN_SE_GAIN")) {
			gain = std::strtod(env, nullptr);
		}
		return std::clamp(gain, 0.0, 4.0);
	}();
	const s32 masterQ15 = static_cast<s32>(std::clamp(sMixerLevel.load() * sTrim, 0.0, 4.0) * 32768.0);
	auto master = [masterQ15](s32 v) { return static_cast<s32>((static_cast<s64>(v) * masterQ15) >> 15); };
	if (sPlanarOutput) {
		// Right plane first, then left. See pc_dsp_host.h for why.
		for (u32 i = 0; i < frameSamples; ++i) {
			out[i]                = clampToS16(master(sBusRight[i]));
			out[frameSamples + i] = clampToS16(master(sBusLeft[i]));
		}
	} else {
		for (u32 i = 0; i < frameSamples; ++i) {
			out[i * 2]     = clampToS16(master(sBusLeft[i]));
			out[i * 2 + 1] = clampToS16(master(sBusRight[i]));
		}
	}
}

// Diagnóstico (PIKMIN_AUDIO_BUSLOG=1): cada ~2 s, a qué buses mandan las voces
// y con cuánto volumen, y cómo están las voces del auto-mixer. Sirve para ver
// qué se pierde en el mezclado.
namespace {
struct BusLogStats {
	u32 frames = 0;
	u32 voices = 0, autoVoices = 0;
	u32 busVoices[16] = {};
	u64 busVolume[16] = {};
	u32 surroundHist[4] = {};
	u64 autoLevel = 0, fxSend = 0;
} sBusLog;
constexpr u16 kBusLogIds[12] = { 0x0000, 0x0D00, 0x0D60, 0x0DC0, 0x0E20, 0x0E80, 0x0EE0, 0x0CA0, 0x0F40, 0x0FA0, 0x0B00, 0x09A0 };
int busLogIndex(u16 id)
{
	for (int i = 0; i < 12; ++i)
		if (kBusLogIds[i] == id) return i;
	return 12; // otro
}
} // namespace

void busLogVoice(const DSPchannel_& channel)
{
	++sBusLog.voices;
	if (channel.useDolbyVolume) {
		++sBusLog.autoVoices;
		sBusLog.surroundHist[(static_cast<u8>(channel.dolbyVoicePosition) & 0x7f) >> 5]++;
		sBusLog.autoLevel += static_cast<u16>(channel.dolbyVolumeTarget);
		sBusLog.fxSend += static_cast<u8>(static_cast<u16>(channel.dolbyReverbFactor) >> 8);
		return;
	}
	for (int m = 0; m < 6; ++m) {
		const DSPMixerChannel& mix = channel.mixChannels[m];
		const s32 vol = std::abs(static_cast<s32>(static_cast<s16>(mix.targetVolume)));
		if (vol == 0) continue;
		const int i = busLogIndex(mix.id);
		sBusLog.busVoices[i]++;
		sBusLog.busVolume[i] += static_cast<u64>(vol);
	}
}

void busLogFrame()
{
	if (++sBusLog.frames < 400) return;
	std::fprintf(stderr, "[PC DSP bus] voices/frame=%.1f auto=%.1f", sBusLog.voices / 400.0, sBusLog.autoVoices / 400.0);
	if (sBusLog.autoVoices) {
		std::fprintf(stderr, " autoLvl=%llu fx=%llu surr=[%u %u %u %u]", sBusLog.autoLevel / sBusLog.autoVoices,
		             sBusLog.fxSend / sBusLog.autoVoices, sBusLog.surroundHist[0], sBusLog.surroundHist[1],
		             sBusLog.surroundHist[2], sBusLog.surroundHist[3]);
	}
	for (int i = 0; i < 13; ++i) {
		if (!sBusLog.busVoices[i]) continue;
		std::fprintf(stderr, " %04x:%u/%llu", i < 12 ? kBusLogIds[i] : 0xFFFF, sBusLog.busVoices[i],
		             sBusLog.busVolume[i] / sBusLog.busVoices[i]);
	}
	std::fprintf(stderr, "\n");
	sBusLog = BusLogStats();
}

void pc_dsp_host_render_frame_planar(DSPchannel_* channels, u32 channelCount,
                                     s16* out, u32 frameSamples)
{
	sPlanarOutput = true;
	pc_dsp_host_render_frame(channels, channelCount, out, frameSamples);
	sPlanarOutput = false;
}
