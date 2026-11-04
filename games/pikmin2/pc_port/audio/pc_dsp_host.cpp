/**
 * @file pc_dsp_host.cpp
 * @brief Host-side software DSP. See pc_dsp_host.h for the rationale.
 */

#include "pc_dsp_host.h"

#include "pc_aram.h"
#include "pc_audio_drops.h"
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
	kBusNone      = 0x0000,
	kBusDryLeft   = 0x0D00,
	kBusDryRight  = 0x0D60,
	kBusBackLeft  = 0x0F40,
	kBusBackRight = 0x0CA0,
	// Bus central: en estéreo y mono JASChannel::initialUpdateDSPChannel
	// redirige aquí el trasero izquierdo (connect 8 -> 11), para que lo que en
	// surround va en contrafase no se anule; la consola lo suma en fase a
	// los dos lados. Sin este caso esas voces se descartaban y en estéreo
	// faltaban sonidos (issue #135; en surround se oía todo).
	kBusCenter = 0x09A0,
	// Los otros dos buses intermedios del DSP: el renderer de Pikmin 1 los
	// suma igual a los dos lados; aquí se descartaban.
	kBusCenter2 = 0x0FA0,
	kBusCenter3 = 0x0B00,
	// Entradas de las cuatro líneas FX (JASDsp::setFXLine), en orden de línea.
	kBusFx0 = 0x0DC0,
	kBusFx1 = 0x0E20,
	kBusFx2 = 0x0E80,
	kBusFx3 = 0x0EE0,
};

constexpr int kFxLines              = 4;
constexpr u16 kFxInputBus[kFxLines] = { kBusFx0, kBusFx1, kBusFx2, kBusFx3 };

int fxLineForBus(u16 id)
{
	for (int i = 0; i < kFxLines; ++i) {
		if (kFxInputBus[i] == id) return i;
	}
	return -1;
}

/// Downmix weight for the surround pair when folded into stereo.
constexpr s32 kSurroundFold = 24576; // 0.75 in Q15.
/// Bus central repartido a izquierda y derecha (-3 dB).
constexpr s32 kCenterFold = 23170; // 0.7071 in Q15.

const u32* sResampleFilter = nullptr;
const u32* sAdpcmFilter    = nullptr;
bool sReady                = false;
bool sPlanarOutput         = false;
u32 sActiveVoices          = 0;

/// Scratch accumulator, one entry per output sample, reused between frames.
std::vector<s32> sBusLeft;
std::vector<s32> sBusRight;

/// Líneas FX: lo que mandan las voces en este frame, el buffer circular (el
/// retardo) y la historia del FIR. La configuración la pone el juego.
struct FxLine {
	bool enabled   = false;
	u32 subframes  = 0;
	u16 dest[2]    = { 0, 0 };
	s16 volume[2]  = { 0, 0 };
	s16 coeffs[8]  = { 0 };
	u32 index      = 0;
	s16 history[8] = { 0 };
	std::vector<s16> ring;
	std::vector<s32> input;
};
FxLine sFx[kFxLines];

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

s32 multiplyQ15(s32 sample, s32 gain)
{
	return static_cast<s32>((static_cast<s64>(sample) * gain) >> 15);
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
 * @brief Decodes one Nintendo AFC block (four- or two-bit) into @p out.
 *
 * Nintendo's block layout: a header byte carrying the scale exponent in the
 * high nibble and the predictor index in the low nibble. AFC4 stores two
 * samples in each of eight bytes; AFC2 stores four samples in four bytes.
 *
 * @param history1 Sample before the previous one, updated in place.
 * @param history2 Previous sample, updated in place.
 * @return false if the block is not resident in ARAM.
 */
bool decodeAdpcmBlock(u32 aramOffset, u16 sourceType, s16* out, s16& history1, s16& history2)
{
	const u32 blockBytes = sourceType == kSourceAdpcm4 ? 9 : 5;
	const u8* block      = pc_aram_read(aramOffset, blockBytes);
	if (block == nullptr) {
		pc_audio_drop(PCAD_AramMiss);
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
		s32 residual;
		if (sourceType == kSourceAdpcm4) {
			const u8 byte = block[1 + (i >> 1)];
			residual      = (i & 1) ? (byte & 0x0F) : (byte >> 4);
			if (residual > 7) residual -= 16; // Sign extend four bits.
		} else {
			const u8 byte = block[1 + (i >> 2)];
			const u8 shift = static_cast<u8>(6 - (i & 3) * 2);
			residual       = (byte >> shift) & 0x03;
			if (residual > 1) residual -= 4; // Sign extend two bits.
			// AFC2 keeps the same predictor scale as AFC4 and expands its
			// two-bit residual to the four-bit domain used by the DSP.
			residual *= 4;
		}

		// Predictor coefficients are Q11, so the history term shifts by 11.
		const s32 predicted = static_cast<s32>((static_cast<s64>(coef1) * history2
		                                        + static_cast<s64>(coef2) * history1) >> 11);
		const s32 sample    = clampToS16(residual * static_cast<s32>(1u << scale) + predicted);

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
		const u64 address = static_cast<u64>(channel.baseAddress) + static_cast<u64>(sampleIndex) * 2;
		if (address > UINT32_MAX) return 0;
		const u8* at = pc_aram_read(static_cast<u32>(address), 2);
		if (at == nullptr) {
			return 0;
		}
		// Sample banks keep the console's big-endian layout.
		return static_cast<s16>((static_cast<u16>(at[0]) << 8) | at[1]);
	}

	const u64 address = static_cast<u64>(channel.baseAddress) + sampleIndex;
	if (address > UINT32_MAX) return 0;
	const u8* at = pc_aram_read(static_cast<u32>(address), 1);
	if (at == nullptr) {
		return 0;
	}
	return static_cast<s16>(static_cast<s16>(static_cast<s8>(*at)) * 256);
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
	// Diagnóstico (PIKMIN_AUDIO_DROPS): vida de la voz actual.
	bool live        = false;
	u32 lifeSamples  = 0;
	s32 lifeMaxGain  = 0;
	s32 lifeMaxAsked = 0; ///< el mayor volumen que pidió el juego (Q15)
};

enum VoiceEnd { kEndDisabled, kEndSample, kEndRequest };

/// Cierra la vida de una voz para el diagnóstico: muy corta o inaudible, y por qué.
void finishVoiceLife(HostVoice& voice, VoiceEnd why)
{
	if (!voice.live) return;
	voice.live = false;
	if (voice.lifeSamples < 1280) { // 40 ms a 32 kHz
		pc_audio_drop(PCAD_VoiceShort);
		pc_audio_drop(why == kEndSample ? PCAD_ShortSampleEnd : why == kEndRequest ? PCAD_ShortEndRequest : PCAD_ShortDisabled);
	}
	if (voice.lifeMaxGain < 104) { // -50 dB en Q15
		pc_audio_drop(PCAD_VoiceInaudible);
		pc_audio_drop(voice.lifeMaxAsked < 104 ? PCAD_InaudibleGame : PCAD_InaudibleDsp);
	}
}

std::vector<HostVoice> sVoices;

/// Fetches sample @p index for @p channel, decoding ADPCM blocks on demand.
s16 fetchSample(DSPchannel_& channel, HostVoice& voice, u32 index)
{
	switch (channel.samplesSourceType) {
	case kSourcePcm8:
	case kSourcePcm16:
		return readPcmSample(channel, index);

	case kSourceAdpcm4:
	case kSourceAdpcm2: {
		const u32 block = index >> 4;
		if (block != voice.blockIndex) {
			// Decoding is sequential, so a jump backwards or a skip forwards
			// has to restart from the loop history the engine supplied.
			if (block != voice.blockIndex + 1) {
				voice.history1 = channel.loopYN2;
				voice.history2 = channel.loopYN1;
			}
			const u32 blockBytes = channel.samplesSourceType == kSourceAdpcm4 ? 9 : 5;
			const u64 address = static_cast<u64>(channel.baseAddress) + static_cast<u64>(block) * blockBytes;
			if (address > UINT32_MAX
			    || !decodeAdpcmBlock(static_cast<u32>(address), channel.samplesSourceType,
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

void pc_dsp_host_set_fx_line(u32 index, bool enabled, u32 subframes, const u16 dest[2], const s16 volume[2],
                             const s16 coeffs[8])
{
	if (index >= kFxLines) return;
	HostAllocScope hostAlloc;
	FxLine& fx   = sFx[index];
	fx.enabled   = enabled && subframes > 0;
	fx.subframes = subframes;
	for (int i = 0; i < 2; ++i) {
		fx.dest[i]   = dest[i];
		fx.volume[i] = volume[i];
	}
	for (int i = 0; i < 8; ++i) {
		fx.coeffs[i]  = coeffs[i];
		fx.history[i] = 0;
	}
	fx.index = 0;
	fx.ring.assign(fx.enabled ? subframes * PC_DSP_SUBFRAME_SAMPLES : 0, 0);
	fx.input.clear();
}

namespace {
/// Como el DSP (Dolphin, ZeldaAudioRenderer::ApplyReverb): cada subframe de 80
/// muestras entra en el buffer circular y sale el que entró hace `subframes`
/// subframes, filtrado por el FIR de 8 coeficientes, hacia sus dos buses.
/// PIKMIN_FX_GAIN (1.0 por defecto, 0 lo apaga) ajusta el nivel a mano.
void renderFxLines(u32 frameSamples)
{
	static const double sFxTrim = [] {
		const char* env = std::getenv("PIKMIN_FX_GAIN");
		return env ? std::clamp(std::strtod(env, nullptr), 0.0, 4.0) : 1.0;
	}();
	if (sFxTrim <= 0.0) return;
	const s32 trimQ15 = static_cast<s32>(sFxTrim * 32768.0);
	for (FxLine& fx : sFx) {
		if (!fx.enabled || fx.input.size() < frameSamples || fx.ring.empty()) continue;
		for (u32 sub = 0; sub + PC_DSP_SUBFRAME_SAMPLES <= frameSamples; sub += PC_DSP_SUBFRAME_SAMPLES) {
			s16* slot = &fx.ring[fx.index * PC_DSP_SUBFRAME_SAMPLES];
			s16 buffer[8 + PC_DSP_SUBFRAME_SAMPLES];
			for (int i = 0; i < 8; ++i) buffer[i] = fx.history[i];
			for (u32 i = 0; i < PC_DSP_SUBFRAME_SAMPLES; ++i) {
				buffer[8 + i] = slot[i];
				slot[i]       = clampToS16(fx.input[sub + i]);
			}
			fx.index = (fx.index + 1) % fx.subframes;
			for (int i = 0; i < 8; ++i) fx.history[i] = buffer[PC_DSP_SUBFRAME_SAMPLES + i];
			for (u32 i = 0; i < PC_DSP_SUBFRAME_SAMPLES; ++i) {
				s64 acc = 0;
				for (int j = 0; j < 8; ++j) acc += static_cast<s64>(buffer[i + j]) * fx.coeffs[j];
				const s32 filtered = static_cast<s32>(std::clamp<s64>(acc >> 15, -32768, 32767));
				const s32 out = multiplyQ15(filtered, trimQ15);
				for (int d = 0; d < 2; ++d) {
					if (!fx.dest[d] || !fx.volume[d]) continue;
					const s32 v = multiplyQ15(out, fx.volume[d]);
					switch (fx.dest[d]) {
					case kBusDryLeft: sBusLeft[sub + i] += v; break;
					case kBusDryRight: sBusRight[sub + i] += v; break;
					case kBusBackLeft: sBusLeft[sub + i] += (v * kSurroundFold) >> 15; break;
					case kBusBackRight: sBusRight[sub + i] += (v * kSurroundFold) >> 15; break;
					case kBusCenter:
					case kBusCenter2:
					case kBusCenter3: {
						const s32 c = (v * kCenterFold) >> 15;
						sBusLeft[sub + i] += c;
						sBusRight[sub + i] += c;
						break;
					}
					default: break;
					}
				}
			}
		}
	}
}
} // namespace

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
	for (FxLine& fx : sFx) {
		if (fx.enabled) fx.input.assign(frameSamples, 0);
	}
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
			finishVoiceLife(sVoices[ch], kEndDisabled);
			continue;
		}

		HostVoice& voice = sVoices[ch];
		if (busLog) busLogVoice(channel);
		if (channel.resetVpb && pc_audio_levels_enabled()) {
			finishVoiceLife(voice, kEndDisabled);
			voice.live         = true;
			voice.lifeSamples  = 0;
			voice.lifeMaxGain  = 0;
			voice.lifeMaxAsked = 0;
			pc_audio_drop(PCAD_VoiceStart);
		}
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
		static const bool sLevels = pc_audio_levels_enabled();
		double levelSum = 0.0;
		s32 levelPeak   = 0;
		u32 levelCount  = 0;
		s32 levelGain   = 0;
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
			s32 fxStart[kFxLines] = { 0 }, fxEnd[kFxLines] = { 0 };
			bool hasFx            = false;
			if (channel.useDolbyVolume) {
				auto sineGain = [](u8 position) -> s32 {
					constexpr double kHalfPi = 1.57079632679489661923;
					return static_cast<s32>(std::sin(static_cast<double>(position) * kHalfPi / 127.0) * 32767.0);
				};
				const u8 pan      = static_cast<u8>(channel.dolbyVoicePosition >> 8) & 0x7f;
				const u8 surround = static_cast<u8>(channel.dolbyVoicePosition) & 0x7f;
				// fxMix (0..127) feeds the rear buses; with no fx the voice
				// plays dry out front, matching the DSP's fx routing.
				// La parte trasera va siempre a los buses traseros (se pliegan en
				// estéreo); antes dependía del envío a FX, y un sonido detrás de
				// la cámara sin reverb se quedaba casi mudo. El envío a FX va
				// aparte, a las líneas de eco (Dolphin: reverb_volume_factor =
				// current * reverb_factor >> (shift - 1), o sea el doble).
				const s32 fxSend  = static_cast<s32>(static_cast<u8>(static_cast<u16>(channel.dolbyReverbFactor) >> 8)) << 9;
				const s32 right   = sineGain(pan);
				const s32 left    = sineGain(pan ^ 0x7f);
				const s32 back    = sineGain(surround);
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
				// Como el DSP (Dolphin, ZeldaAudioRenderer::AddVoice): el volumen
				// va en línea recta de current a target dentro del subframe y
				// queda en target. Al pedir el final, cada subframe el objetivo es
				// la mitad del actual, y la voz acaba al llegar a 0. Antes era una
				// rampa exponencial (>> 5 por subframe) que tardaba ~200 ms en
				// subir desde 0, y como initAutoMixer arranca las voces en 0, los
				// sonidos cortos (pasos, golpes, gritos) se acababan casi mudos.
				s32 mixer  = static_cast<u16>(channel.dolbyVolumeCurrent);
				s32 target = channel.endRequested ? (mixer >> 1) : static_cast<u16>(channel.dolbyVolumeTarget);
				// Escala del microcódigo real (Dolphin, ZeldaAudioRenderer con
				// MAKE_DOLBY_LOUDER para Pikmin 2): cuadrante = (pan * mixer) >> 15
				// y la voz se suma con (vol * muestra) >> 16. Aquí se suma con
				// >> 15, así que la ganancia es mixer / 2. Antes iba mixer * 2:
				// cuatro veces más fuerte, saturaba y sonaba distorsionado.
				const s32 fromGain = mixer >> 1;
				mixer              = target;
				channel.dolbyVolumeCurrent = static_cast<s16>(mixer);
				const s32 toGain = mixer >> 1;
				startL = (leftPan * fromGain) >> 15;
				endL   = (leftPan * toGain) >> 15;
				startR = (rightPan * fromGain) >> 15;
				endR   = (rightPan * toGain) >> 15;
				if (fxSend != 0) {
					// Delante a las líneas 2/3 (salen por izquierda/derecha con
					// más volumen), detrás a las 0/1.
					const s32 quadrant[kFxLines] = { backL, backR, frontL, frontR };
					for (int f = 0; f < kFxLines; ++f) {
						const s32 send = (quadrant[f] * fxSend) >> 15;
						fxStart[f]     = (send * fromGain) >> 15;
						fxEnd[f]       = (send * toGain) >> 15;
						hasFx |= fxStart[f] != 0 || fxEnd[f] != 0;
					}
				}
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
				case kBusBackLeft:
					startL += (from * kSurroundFold) >> 15;
					endL += (to * kSurroundFold) >> 15;
					break;
				case kBusBackRight:
					startR += (from * kSurroundFold) >> 15;
					endR += (to * kSurroundFold) >> 15;
					break;
				case kBusCenter:
				case kBusCenter2:
				case kBusCenter3:
					startL += (from * kCenterFold) >> 15;
					endL += (to * kCenterFold) >> 15;
					startR += (from * kCenterFold) >> 15;
					endR += (to * kCenterFold) >> 15;
					break;
				case kBusFx0:
				case kBusFx1:
				case kBusFx2:
				case kBusFx3: {
					// Envío a una línea FX: antes se sumaba en seco a 0,75.
					const int f = fxLineForBus(mix.id);
					fxStart[f] += from;
					fxEnd[f] += to;
					hasFx |= from != 0 || to != 0;
					break;
				}
				case kBusNone:
				default:
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

			// Pre-calculate subframe volume ramp deltas to eliminate redundant
			// per-sample division when volume is constant (steady-state audio).
			// Behaviour-identical: diff==0 gives start/fxStart; diff!=0 matches
			// original linear ramp formula start + (diff * t) / n.
			const s32 diffL = endL - startL;
			const s32 diffR = endR - startR;
			const s32 n = static_cast<s32>(subLength);
			const bool rampL = (diffL != 0);
			const bool rampR = (diffR != 0);

			s32 fxDiff[kFxLines] = { 0 };
			bool fxRamp[kFxLines] = { false };
			if (hasFx) {
				for (int f = 0; f < kFxLines; ++f) {
					if (sFx[f].enabled) {
						fxDiff[f] = fxEnd[f] - fxStart[f];
						fxRamp[f] = (fxDiff[f] != 0);
					}
				}
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
						const s64 acc    = static_cast<s64>(voice.taps[0]) * static_cast<s16>(c[0])
						              + static_cast<s64>(voice.taps[1]) * static_cast<s16>(c[1])
						              + static_cast<s64>(voice.taps[2]) * static_cast<s16>(c[2])
						              + static_cast<s64>(voice.taps[3]) * static_cast<s16>(c[3]);
						interpolated = static_cast<s32>(std::clamp<s64>(acc >> 15, -32768, 32767));
					} else {
						const s32 s0 = voice.taps[1], s1 = voice.taps[2];
						interpolated = s0 + static_cast<s32>((static_cast<s64>(s1 - s0) * frac) >> kPhaseShift);
					}
				}

				// Ramp both sides across the subframe.
				const s32 t = static_cast<s32>(i);
				const s32 volL = rampL ? startL + (diffL * t) / n : startL;
				const s32 volR = rampR ? startR + (diffR * t) / n : startR;

				const u32 slot = sub + i;
				const s32 mixedL = multiplyQ15(interpolated, volL);
				const s32 mixedR = multiplyQ15(interpolated, volR);
				sBusLeft[slot] += mixedL;
				sBusRight[slot] += mixedR;
				if (sLevels) {
					const s32 cl = mixedL, cr = mixedR;
					levelSum += 0.5 * (double(cl) * cl + double(cr) * cr);
					levelPeak = std::max(levelPeak, std::max(std::abs(cl), std::abs(cr)));
					levelGain = std::max(levelGain, std::max(std::abs(volL), std::abs(volR)));
					++levelCount;
				}
				if (hasFx) {
					for (int f = 0; f < kFxLines; ++f) {
						if (!sFx[f].enabled) continue;
						const s32 vol = fxRamp[f] ? fxStart[f] + (fxDiff[f] * t) / n : fxStart[f];
						sFx[f].input[slot] += multiplyQ15(interpolated, vol);
					}
				}
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
		if (voice.live) {
			voice.lifeSamples += levelCount;
			voice.lifeMaxGain = std::max(voice.lifeMaxGain, levelGain);
			// Lo que pidió el juego, con la misma escala que la ganancia: el
			// auto-mixer como target/2 por el paneo central; el resto, el mayor
			// volumen de bus seco.
			s32 asked = 0;
			if (channel.useDolbyVolume) {
				asked = (static_cast<s32>(static_cast<u16>(channel.dolbyVolumeTarget)) >> 1) * 23170 >> 15;
			} else {
				for (int m = 0; m < 6; ++m) {
					const u16 id = channel.mixChannels[m].id;
					if (id == kBusDryLeft || id == kBusDryRight || id == kBusCenter || id == kBusCenter2 || id == kBusCenter3) {
						asked = std::max(asked, std::abs(static_cast<s32>(static_cast<s16>(channel.mixChannels[m].targetVolume)) >> 1));
					}
				}
			}
			voice.lifeMaxAsked = std::max(voice.lifeMaxAsked, asked);
			if (channel.done) finishVoiceLife(voice, channel.endRequested ? kEndRequest : kEndSample);
		}
		if (produced) {
			++sActiveVoices;
			if (sLevels) {
				const int source = channel.useDolbyVolume ? PCAL_Voice3D : PCAL_Voice2D;
				// Ganancia con el volumen maestro, como llega a la salida.
				pc_audio_level_gain(source, static_cast<int>((static_cast<s64>(levelGain) * static_cast<s64>(sMixerLevel.load() * 32768.0)) >> 15));
				pc_audio_level_block(source, levelSum, levelPeak, levelCount, 0);
			}
		}
	}

	renderFxLines(frameSamples);

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
	if (pc_audio_levels_enabled()) {
		double sum = 0.0;
		s32 peak   = 0;
		unsigned clipped = 0;
		for (u32 i = 0; i < frameSamples; ++i) {
			for (s32 v : { master(sBusLeft[i]), master(sBusRight[i]) }) {
				sum += 0.5 * double(v) * v;
				peak = std::max(peak, std::abs(v));
				clipped += (v > 32767 || v < -32768) ? 1u : 0u;
			}
		}
		pc_audio_level_block(PCAL_DspOut, sum, std::min<s32>(peak, 32767), frameSamples, clipped);
	}
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
		std::fprintf(stderr, " autoLvl=%llu fx=%llu surr=[%u %u %u %u]",
		             static_cast<unsigned long long>(sBusLog.autoLevel / sBusLog.autoVoices),
		             static_cast<unsigned long long>(sBusLog.fxSend / sBusLog.autoVoices), sBusLog.surroundHist[0], sBusLog.surroundHist[1],
		             sBusLog.surroundHist[2], sBusLog.surroundHist[3]);
	}
	for (int i = 0; i < 13; ++i) {
		if (!sBusLog.busVoices[i]) continue;
		std::fprintf(stderr, " %04x:%u/%llu", i < 12 ? kBusLogIds[i] : 0xFFFF, sBusLog.busVoices[i],
		             static_cast<unsigned long long>(sBusLog.busVolume[i] / sBusLog.busVoices[i]));
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
