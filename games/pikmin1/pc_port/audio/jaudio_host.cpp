/*
 * Pikmin JAudio host boundary.
 *
 * The game-side sequencer, banks, oscillators and logical channel manager are
 * the original code.  This file replaces only the GameCube AI/DSP boundary:
 * it renders the 64 native VPBs in software and submits the resulting stereo
 * PCM to the port's SDL sink.
 */

#include "port/jaudio_host.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <chrono>
#include <thread>

#include <Dolphin/ai.h>
#include <Dolphin/ar.h>
#include <Dolphin/os.h>

#include "jaudio/aictrl.h"
#include "jaudio/audiocommon.h"
#include "jaudio/audiostruct.h"
#include "jaudio/audiothread.h"
#include "jaudio/driverinterface.h"
#include "jaudio/dspdriver.h"
#include "jaudio/dspinterface.h"
#include "jaudio/dspproc.h"
#include "jaudio/dummyrom.h"
#include "jaudio/dvdthread.h"
#include "jaudio/ja_calc.h"
#include "jaudio/memory.h"
#include "jaudio/playercall.h"
#include "jaudio/rate.h"
#include "jaudio/streamctrl.h"
#include "port/audio_sink.h"
#include "port/jaudio_state.h"


namespace {

/*
 * These are optional at the translation-unit boundary.  Normal builds provide
 * strong definitions from dspinterface.c and the H4M player respectively.
 */
#if defined(__GNUC__)
extern "C" const u32* PikiJAudioGetResampleTableWords(size_t*) __attribute__((weak));
extern "C" const u32* PikiJAudioGetAfcTableWords(size_t*) __attribute__((weak));
extern "C" int PikiMovieAudioActive(void) __attribute__((weak));
#endif

constexpr size_t kVoiceCount      = 64;
constexpr size_t kSubframeSamples = 80;
constexpr size_t kSubframes       = 7;
constexpr size_t kFrameSamples    = kSubframeSamples * kSubframes;
constexpr int kHostSampleRate     = 32000;
/* The producer is a cooperative OSThread: while the game overruns its 33 ms
   frame budget (Forest of Hope peaks ~42 ms at ~950 draws), the producer gets
   no baton and the sink drains. 5 frames (87 ms) of queue audibly glitched on
   every hitch; 12 frames (210 ms) rides them out, and this game does not need
   tight audio latency. */
constexpr size_t kPrebufferFrames = kFrameSamples * 5;
constexpr size_t kMaxQueuedFrames = kFrameSamples * 12;
constexpr size_t kRawSampleCount  = 0x500 + 4;
constexpr u64 kAudioFramePeriodNs = static_cast<u64>(kFrameSamples) * 1000000000ULL / kHostSampleRate;
constexpr u64 kSinkRetryPeriodNs  = 500000000ULL;

static_assert(kFrameSamples == 560);
static_assert(sizeof(DSPchannel_) == 0x180, "DSPchannel_ must retain the Pikmin VPB layout");

template <typename T>
T clamp16(T value)
{
	return std::clamp<T>(value, static_cast<T>(-0x8000), static_cast<T>(0x7fff));
}

u16 clampMenuVolume(u8 value) { return std::min<u16>(value, 10); }

u64 monotonicNs()
{
	// Was clock_gettime(CLOCK_MONOTONIC). That is POSIX: MinGW resolves it to
	// clock_gettime64 in winpthreads, which this port does not link. steady_clock
	// gives the same guarantee -- monotonic, never adjusted -- on every platform,
	// and the rest of the port already keeps time this way.
	return static_cast<u64>(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(
	        std::chrono::steady_clock::now().time_since_epoch())
	        .count());
}

struct DirectPCM {
	const s16* samples = nullptr;
	u32 capacity       = 0;
};

std::array<DirectPCM, kVoiceCount> sDirectPCM {};

using MixBuffer = std::array<s32, kSubframeSamples>;

enum class Bus : u8 {
	FrontLeft,
	FrontRight,
	BackLeft,
	BackRight,
	FrontLeftReverb,
	FrontRightReverb,
	BackLeftReverb,
	BackRightReverb,
	Unknown0Reverb,
	Unknown1Reverb,
	Unknown0,
	Unknown1,
	Unknown2,
	Count,
};

constexpr size_t busIndex(Bus bus) { return static_cast<size_t>(bus); }

using BusBuffers = std::array<MixBuffer, busIndex(Bus::Count)>;

struct BusDiagnostics {
	u64 sampleCount     = 0;
	long double squareSum = 0.0L;
	u64 outsideInt16    = 0;
	s64 peak            = 0;
};

constexpr std::array<Bus, 7> kDiagnosticBuses = {
	Bus::FrontLeft,
	Bus::FrontRight,
	Bus::BackLeft,
	Bus::BackRight,
	Bus::Unknown0,
	Bus::Unknown1,
	Bus::Unknown2,
};
constexpr std::array<const char*, kDiagnosticBuses.size()> kDiagnosticBusNames = {
	"FrontL", "FrontR", "BackL", "BackR", "Unknown0", "Unknown1", "Unknown2",
};

/*
 * Only the audio producer touches these accumulators.  Keeping the enable bit
 * separate lets the normal renderer skip every per-sample diagnostic cost.
 */
bool sBusDiagnosticsEnabled = false;
std::array<BusDiagnostics, kDiagnosticBuses.size()> sBusDiagnostics {};

void resetBusDiagnostics()
{
	sBusDiagnostics.fill(BusDiagnostics {});
}

void observeBusDiagnostics(const BusBuffers& buses)
{
	for (size_t bus = 0; bus < kDiagnosticBuses.size(); ++bus) {
		BusDiagnostics& stats = sBusDiagnostics[bus];
		for (s32 value : buses[busIndex(kDiagnosticBuses[bus])]) {
			const s64 wideValue = value;
			const s64 magnitude = wideValue < 0 ? -wideValue : wideValue;
			stats.peak          = std::max(stats.peak, magnitude);
			stats.squareSum += static_cast<long double>(wideValue) * wideValue;
			stats.outsideInt16 += wideValue < std::numeric_limits<s16>::min()
			                   || wideValue > std::numeric_limits<s16>::max();
			++stats.sampleCount;
		}
	}
}

class HostRenderer {
public:
	void render(s16* interleaved);
	void setMasterLevel(f32 level);

private:
	MixBuffer* bufferForId(u16 id);
	void initialiseTables();
	void beginSubframe();
	void finishSubframe(s16* interleaved);
	void renderVoice(u8 voice);
	void loadInput(u8 voice, DSPchannel_& vpb, MixBuffer& output);
	u16 neededRawSamples(const DSPchannel_& vpb) const;
	void resample(DSPchannel_& vpb, const s16* source, MixBuffer& output);
	void loadPcmFromAram(DSPchannel_& vpb, s16* output, u16 count, u8 bytesPerSample);
	void loadAfcFromAram(DSPchannel_& vpb, s16* output, u16 count);
	void decodeAfcBlocks(DSPchannel_& vpb, s16* output, size_t blocks);
	void loadDirectPcm(u8 voice, DSPchannel_& vpb, s16* output, u16 count);
	void applyLowPass(DSPchannel_& vpb, MixBuffer& samples);
	void applyVariableFir(DSPchannel_& vpb, MixBuffer& samples);
	void applyBiquad(DSPchannel_& vpb, MixBuffer& samples);
	void mixVoice(DSPchannel_& vpb, const MixBuffer& samples);
	void readEffects();
	void writeEffects();
	void addWithVolume(MixBuffer& dst, const s16* src, size_t count, s16 volume);
	void addWithVolume(MixBuffer& dst, const MixBuffer& src, s32 volumeQ16, s32 stepQ16);
	bool aramRange(u32 address, size_t length, const u8** output) const;

	BusBuffers mBuses {};
	std::array<s16, 256> mResampleCoefficients {};
	std::array<s16, 256> mPatterns {};
	std::array<s16, 32> mAfcCoefficients {};
	std::array<s16, 128> mDolbySine {};
	std::array<u16, 4> mEffectCursor {};
	std::array<std::array<s16, 8>, 4> mEffectHistory {};
	u16 mMasterLevel = 0x2000;
	bool mTablesReady = false;
};

HostRenderer sRenderer;

void HostRenderer::setMasterLevel(f32 level)
{
	if (!std::isfinite(level)) {
		return;
	}
	const f32 scaled = std::clamp(level * 4096.0f, 0.0f, 65535.0f);
	mMasterLevel     = static_cast<u16>(scaled);
}

void HostRenderer::initialiseTables()
{
	if (mTablesReady) {
		return;
	}

	/*
	 * A conservative linear polyphase fallback.  The matched table below
	 * replaces it in normal builds.
	 */
	for (size_t phase = 0; phase < 64; ++phase) {
		const s32 right                     = static_cast<s32>(phase * 32767 / 64);
		const s32 left                      = 32767 - right;
		mResampleCoefficients[phase * 4 + 0] = 0;
		mResampleCoefficients[phase * 4 + 1] = static_cast<s16>(left);
		mResampleCoefficients[phase * 4 + 2] = static_cast<s16>(right);
		mResampleCoefficients[phase * 4 + 3] = 0;
	}

	for (size_t i = 0; i < 64; ++i) {
		const double phase = static_cast<double>(i) * 2.0 * 3.14159265358979323846 / 64.0;
		mPatterns[i]       = static_cast<s16>(std::sin(phase) * 32767.0);
		mPatterns[64 + i]  = static_cast<s16>(-mPatterns[i]);
		mPatterns[128 + i] = 0;
		mPatterns[192 + i] = static_cast<s16>((static_cast<s32>(i) - 32) * 1024);
	}

	static constexpr s16 kFallbackAfc[32] = {
		0x0000, 0x0000, 0x0800, 0x0000, 0x0000, 0x0800, 0x0400, 0x0400,
		0x1000, static_cast<s16>(0xf800), 0x0e00, static_cast<s16>(0xfa00),
		0x0c00, static_cast<s16>(0xfc00), 0x1200, static_cast<s16>(0xf600),
		0x1068, static_cast<s16>(0xf738), 0x12c0, static_cast<s16>(0xf704),
		0x1400, static_cast<s16>(0xf400), 0x0800, static_cast<s16>(0xf800),
		0x0400, static_cast<s16>(0xfc00), static_cast<s16>(0xfc00), 0x0400,
		static_cast<s16>(0xfc00), 0x0000, static_cast<s16>(0xf800), 0x0000,
	};
	std::copy(std::begin(kFallbackAfc), std::end(kFallbackAfc), mAfcCoefficients.begin());

#if defined(__GNUC__)
	if (PikiJAudioGetResampleTableWords != nullptr)
#endif
	{
		size_t count     = 0;
		const u32* words = PikiJAudioGetResampleTableWords(&count);
		if (words != nullptr && count >= 256) {
			for (size_t i = 0; i < 128; ++i) {
				mResampleCoefficients[i * 2 + 0] = static_cast<s16>(words[i] >> 16);
				mResampleCoefficients[i * 2 + 1] = static_cast<s16>(words[i] & 0xffff);
			}
			for (size_t i = 0; i < 128; ++i) {
				mPatterns[i * 2 + 0] = static_cast<s16>(words[128 + i] >> 16);
				mPatterns[i * 2 + 1] = static_cast<s16>(words[128 + i] & 0xffff);
			}
		}
	}

#if defined(__GNUC__)
	if (PikiJAudioGetAfcTableWords != nullptr)
#endif
	{
		size_t count     = 0;
		const u32* words = PikiJAudioGetAfcTableWords(&count);
		if (words != nullptr && count >= 16) {
			for (size_t i = 0; i < 16; ++i) {
				mAfcCoefficients[i * 2 + 0] = static_cast<s16>(words[i] >> 16);
				mAfcCoefficients[i * 2 + 1] = static_cast<s16>(words[i] & 0xffff);
			}
		}
	}

	for (size_t i = 0; i < mDolbySine.size(); ++i) {
		const double angle = static_cast<double>(i) * (3.14159265358979323846 / 2.0) / 127.0;
		mDolbySine[i]      = static_cast<s16>(std::sin(angle) * 32767.0);
	}
	mTablesReady = true;
}

MixBuffer* HostRenderer::bufferForId(u16 id)
{
	switch (id) {
	case 0x0d00:
		return &mBuses[busIndex(Bus::FrontLeft)];
	case 0x0d60:
		return &mBuses[busIndex(Bus::FrontRight)];
	case 0x0f40:
		return &mBuses[busIndex(Bus::BackLeft)];
	case 0x0ca0:
		return &mBuses[busIndex(Bus::BackRight)];
	case 0x0e80:
		return &mBuses[busIndex(Bus::FrontLeftReverb)];
	case 0x0ee0:
		return &mBuses[busIndex(Bus::FrontRightReverb)];
	case 0x0c00:
		return &mBuses[busIndex(Bus::BackLeftReverb)];
	case 0x0c50:
		return &mBuses[busIndex(Bus::BackRightReverb)];
	case 0x0dc0:
		return &mBuses[busIndex(Bus::Unknown0Reverb)];
	case 0x0e20:
		return &mBuses[busIndex(Bus::Unknown1Reverb)];
	case 0x09a0:
		return &mBuses[busIndex(Bus::Unknown0)];
	case 0x0fa0:
		return &mBuses[busIndex(Bus::Unknown1)];
	case 0x0b00:
		return &mBuses[busIndex(Bus::Unknown2)];
	default:
		return nullptr;
	}
}

void HostRenderer::addWithVolume(MixBuffer& dst, const s16* src, size_t count, s16 volume)
{
	count = std::min(count, dst.size());
	for (size_t i = 0; i < count; ++i) {
		dst[i] += static_cast<s32>((static_cast<s64>(src[i]) * volume) >> 15);
	}
}

void HostRenderer::addWithVolume(MixBuffer& dst, const MixBuffer& src, s32 volumeQ16, s32 stepQ16)
{
	s64 volume = static_cast<s64>(volumeQ16);
	for (size_t i = 0; i < dst.size(); ++i) {
		dst[i] += static_cast<s32>((static_cast<s64>(src[i]) * volume) >> 32);
		volume += stepQ16;
	}
}

void HostRenderer::readEffects()
{
	static constexpr Bus kEffectSources[4] = {
		Bus::Unknown0Reverb,
		Bus::Unknown1Reverb,
		Bus::FrontLeftReverb,
		Bus::FrontRightReverb,
	};

	for (u8 effectId = 0; effectId < 4; ++effectId) {
		FXBuffer* effect = GetFxHandle(effectId);
		if (effect == nullptr || effect->enabled == 0 || effect->circularBufferBase == nullptr
		    || effect->circularBufferSize == 0) {
			continue;
		}

		const size_t frame = mEffectCursor[effectId] % effect->circularBufferSize;
		const s16* ring    = effect->circularBufferBase + frame * kSubframeSamples;
		std::array<s16, kSubframeSamples + 8> work {};
		std::copy(mEffectHistory[effectId].begin(), mEffectHistory[effectId].end(), work.begin());
		std::copy(ring, ring + kSubframeSamples, work.begin() + 8);
		std::copy(work.end() - 8, work.end(), mEffectHistory[effectId].begin());

		auto filter = [&]() {
			for (size_t i = 0; i < kSubframeSamples; ++i) {
				s64 sum = 0;
				for (size_t tap = 0; tap < 8; ++tap) {
					sum += static_cast<s64>(work[i + tap]) * effect->filterCoeffs[tap];
				}
				work[i] = static_cast<s16>(clamp16(sum >> 15));
			}
		};

		if ((effect->enabled & 1) != 0) {
			filter();
		}
		for (const FXDestination& destination : effect->dest) {
			MixBuffer* output = bufferForId(destination.bufferId);
			if (output != nullptr && destination.bufferId != 0 && destination.volume != 0) {
				addWithVolume(*output, work.data(), kSubframeSamples, destination.volume);
			}
		}
		if ((effect->enabled & 1) == 0 && (effect->enabled & 2) != 0) {
			filter();
		}

		MixBuffer& feedback = mBuses[busIndex(kEffectSources[effectId])];
		for (size_t i = 0; i < kSubframeSamples; ++i) {
			feedback[i] += work[i];
		}
	}
}

void HostRenderer::writeEffects()
{
	static constexpr Bus kEffectSources[4] = {
		Bus::Unknown0Reverb,
		Bus::Unknown1Reverb,
		Bus::FrontLeftReverb,
		Bus::FrontRightReverb,
	};

	for (u8 effectId = 0; effectId < 4; ++effectId) {
		FXBuffer* effect = GetFxHandle(effectId);
		if (effect == nullptr || effect->enabled == 0 || effect->circularBufferBase == nullptr
		    || effect->circularBufferSize == 0) {
			continue;
		}

		const size_t frame = mEffectCursor[effectId] % effect->circularBufferSize;
		s16* ring          = effect->circularBufferBase + frame * kSubframeSamples;
		const MixBuffer& source = mBuses[busIndex(kEffectSources[effectId])];
		for (size_t i = 0; i < kSubframeSamples; ++i) {
			ring[i] = static_cast<s16>(clamp16(source[i]));
		}
		mEffectCursor[effectId] = static_cast<u16>((frame + 1) % effect->circularBufferSize);
	}
}

void HostRenderer::beginSubframe()
{
	/*
	 * Zelda's light DSP leaves the rear dry pair alive between subframes,
	 * attenuating it before voices consume BackRight as the type-10
	 * modulator.  Its rear wet pair is forwarded once into the front wet
	 * pair and then cleared.  Snapshot both pairs before clearing the other
	 * buses so the host follows that lifecycle without an unbounded feedback
	 * path.
	 */
	const MixBuffer previousBackLeft
	    = mBuses[busIndex(Bus::BackLeft)];
	const MixBuffer previousBackRight
	    = mBuses[busIndex(Bus::BackRight)];
	std::array<s16, kSubframeSamples> previousBackLeftWet {};
	std::array<s16, kSubframeSamples> previousBackRightWet {};
	for (size_t i = 0; i < kSubframeSamples; ++i) {
		previousBackLeftWet[i]
		    = static_cast<s16>(clamp16(mBuses[busIndex(Bus::BackLeftReverb)][i]));
		previousBackRightWet[i]
		    = static_cast<s16>(clamp16(mBuses[busIndex(Bus::BackRightReverb)][i]));
	}

	for (MixBuffer& buffer : mBuses) {
		buffer.fill(0);
	}

	MixBuffer& backLeft  = mBuses[busIndex(Bus::BackLeft)];
	MixBuffer& backRight = mBuses[busIndex(Bus::BackRight)];
	for (size_t i = 0; i < kSubframeSamples; ++i) {
		backLeft[i] = static_cast<s32>(
		    clamp16(static_cast<s64>(previousBackLeft[i]) * 0x6784 >> 15));
		backRight[i] = static_cast<s32>(
		    clamp16(static_cast<s64>(previousBackRight[i]) * 0x6784 >> 15));
	}

	readEffects();

	MixBuffer& frontLeftWet  = mBuses[busIndex(Bus::FrontLeftReverb)];
	MixBuffer& frontRightWet = mBuses[busIndex(Bus::FrontRightReverb)];
	addWithVolume(frontLeftWet, previousBackLeftWet.data(), kSubframeSamples, 0x7fff);
	addWithVolume(frontRightWet, previousBackLeftWet.data(), kSubframeSamples,
	              static_cast<s16>(0xb820));
	addWithVolume(frontLeftWet, previousBackRightWet.data() + kSubframeSamples / 2,
	              kSubframeSamples / 2, static_cast<s16>(0xb820));
	addWithVolume(frontRightWet, previousBackRightWet.data() + kSubframeSamples / 2,
	              kSubframeSamples / 2, 0x7fff);

	/* The DSP evolves pattern banks 2 and 3 once per rendered subframe. */
	s16* pattern2 = mPatterns.data() + 2 * 64;
	s32 yn2       = pattern2[62];
	s32 yn1       = pattern2[63];
	for (size_t i = 0; i < 64; i += 2) {
		s64 value = static_cast<s64>(yn2) * yn1 - static_cast<s64>(pattern2[i]) * 65536;
		yn2        = yn1;
		yn1        = pattern2[i];
		pattern2[i] = static_cast<s16>(value >> 16);

		value = 2 * (static_cast<s64>(yn2) * yn1 + static_cast<s64>(pattern2[i + 1]) * 65536);
		yn2   = yn1;
		yn1   = pattern2[i + 1];
		pattern2[i + 1] = static_cast<s16>(value >> 16);
	}

	s16* pattern3 = mPatterns.data() + 3 * 64;
	yn2           = pattern3[62];
	yn1           = pattern3[63];
	const s16 accumulator = static_cast<s16>(yn1);
	s32 step = pattern3[0]
	         + static_cast<s32>((static_cast<s64>(yn1) * yn2 + static_cast<s64>(yn2) * 65536 + yn1)
	                            >> 16);
	step = (step & 0x1ff) | 0x2000;
	for (size_t i = 0; i < 64; ++i) {
		pattern3[i] = static_cast<s16>(accumulator + static_cast<s32>(i + 1) * step);
	}
}

void HostRenderer::finishSubframe(s16* interleaved)
{
	const MixBuffer& frontLeft  = mBuses[busIndex(Bus::FrontLeft)];
	const MixBuffer& frontRight = mBuses[busIndex(Bus::FrontRight)];
	const MixBuffer& backLeft   = mBuses[busIndex(Bus::BackLeft)];
	const MixBuffer& backRight  = mBuses[busIndex(Bus::BackRight)];
	const MixBuffer& unknown0   = mBuses[busIndex(Bus::Unknown0)];
	const MixBuffer& unknown1   = mBuses[busIndex(Bus::Unknown1)];
	const MixBuffer& unknown2   = mBuses[busIndex(Bus::Unknown2)];

	if (sBusDiagnosticsEnabled) {
		observeBusDiagnostics(mBuses);
	}

	/*
	 * Pikmin's light DSP protocol still has rear and intermediate wet buses.
	 * Fold those into stereo at the final boundary; dry front stays bit-for-bit
	 * on its native path and the wet buses retain enough headroom to be stable.
	 */
	for (size_t i = 0; i < kSubframeSamples; ++i) {
		s64 wet  = (static_cast<s64>(unknown0[i]) + unknown1[i] + unknown2[i]) / 2;
		s64 left = static_cast<s64>(frontLeft[i]) + (static_cast<s64>(backLeft[i]) * 0x6784 >> 15) + wet;
		s64 right
		    = static_cast<s64>(frontRight[i]) + (static_cast<s64>(backRight[i]) * 0x6784 >> 15) + wet;
		left  = left * mMasterLevel >> 12;
		right = right * mMasterLevel >> 12;
		interleaved[i * 2 + 0] = static_cast<s16>(clamp16(left));
		interleaved[i * 2 + 1] = static_cast<s16>(clamp16(right));
	}
	writeEffects();
}

bool HostRenderer::aramRange(u32 address, size_t length, const u8** output) const
{
	if (output == nullptr) {
		return false;
	}
	*output        = nullptr;
	u8* storage    = static_cast<u8*>(ARGetStorageAddress());
	const u32 size = ARGetSize();
	if (storage == nullptr || address > size || length > static_cast<size_t>(size - address)) {
		return false;
	}
	*output = storage + address;
	return true;
}

u16 HostRenderer::neededRawSamples(const DSPchannel_& vpb) const
{
	const u32 needed = (static_cast<u32>(vpb.currentPosFrac) + kSubframeSamples * vpb.resamplingRatio) >> 12;
	return static_cast<u16>(std::min<u32>(needed, kRawSampleCount - 4));
}

void HostRenderer::resample(DSPchannel_& vpb, const s16* source, MixBuffer& output)
{
	u32 position    = vpb.currentPosFrac;
	const u32 ratio = vpb.resamplingRatio;

	if ((ratio >> 12) >= 4) {
		for (s32& sample : output) {
			position += ratio;
			const size_t index = std::min<size_t>(position >> 12, kRawSampleCount - 1);
			sample             = source[index];
		}
	} else {
		for (s32& sample : output) {
			const size_t coefficientIndex = ((position & 0xfff) >> 6) * 4;
			const size_t inputIndex       = std::min<size_t>(position >> 12, kRawSampleCount - 4);
			s64 value                     = 0;
			for (size_t tap = 0; tap < 4; ++tap) {
				value += static_cast<s64>(2) * mResampleCoefficients[coefficientIndex + tap]
				       * source[inputIndex + tap];
			}
			sample = static_cast<s32>(clamp16(value >> 16));
			position += ratio;
		}
	}

	const size_t tail = std::min<size_t>(position >> 12, kRawSampleCount - 4);
	for (size_t i = 0; i < 4; ++i) {
		vpb.resampleBuffer[i] = source[tail + i];
	}
	vpb.constantSample = static_cast<s16>(clamp16(output.back()));
	vpb.currentPosFrac = static_cast<u16>(position & 0xfff);
}

void HostRenderer::loadPcmFromAram(DSPchannel_& vpb, s16* output, u16 count, u8 bytesPerSample)
{
	if (vpb.done != 0) {
		std::fill(output, output + count, 0);
		return;
	}

	if (vpb.resetVpb != 0) {
		vpb.remainingLength = vpb.loopStartPosition > vpb.currentPosition
		                        ? vpb.loopStartPosition - vpb.currentPosition
		                        : 0;
		const u64 address = static_cast<u64>(vpb.baseAddress)
		                  + static_cast<u64>(vpb.currentPosition) * bytesPerSample;
		vpb.currentAramAddr = address <= std::numeric_limits<u32>::max() ? static_cast<u32>(address) : 0;
	}

	vpb.endReached = DSP_FALSE;
	while (count != 0) {
		const u16 countBefore     = count;
		const u32 addressBefore   = vpb.currentAramAddr;
		const u32 remainingBefore = vpb.remainingLength;

		if (vpb.endReached != 0 || vpb.remainingLength == 0) {
			vpb.endReached = DSP_FALSE;
			if (vpb.isLooping == 0) {
				std::fill(output, output + count, 0);
				vpb.done = DSP_TRUE;
				return;
			}
			vpb.currentPosition = vpb.loopAddress;
			vpb.remainingLength = vpb.loopStartPosition > vpb.currentPosition
			                        ? vpb.loopStartPosition - vpb.currentPosition
			                        : 0;
			const u64 address = static_cast<u64>(vpb.baseAddress)
			                  + static_cast<u64>(vpb.currentPosition) * bytesPerSample;
			if (address > std::numeric_limits<u32>::max() || vpb.remainingLength == 0) {
				std::fill(output, output + count, 0);
				vpb.done = DSP_TRUE;
				return;
			}
			vpb.currentAramAddr = static_cast<u32>(address);
		}

		const u16 take = static_cast<u16>(std::min<u32>(vpb.remainingLength, count));
		const u8* source;
		if (take == 0
		    || !aramRange(vpb.currentAramAddr, static_cast<size_t>(take) * bytesPerSample, &source)) {
			std::fill(output, output + count, 0);
			vpb.done = DSP_TRUE;
			return;
		}

		if (bytesPerSample == 1) {
			for (u16 i = 0; i < take; ++i) {
				*output++ = static_cast<s16>(static_cast<s32>(static_cast<s8>(source[i])) * 256);
			}
		} else {
			for (u16 i = 0; i < take; ++i) {
				*output++ = static_cast<s16>((static_cast<u16>(source[i * 2]) << 8) | source[i * 2 + 1]);
			}
		}

		vpb.remainingLength -= take;
		vpb.currentAramAddr += static_cast<u32>(take) * bytesPerSample;
		count -= take;
		if (vpb.remainingLength == 0) {
			vpb.endReached = DSP_TRUE;
		}

		if (count == countBefore && vpb.currentAramAddr == addressBefore
		    && vpb.remainingLength == remainingBefore) {
			std::fill(output, output + count, 0);
			vpb.done = DSP_TRUE;
			return;
		}
	}
}

void HostRenderer::decodeAfcBlocks(DSPchannel_& vpb, s16* output, size_t blocks)
{
	const u8 blockBytes = vpb.samplesSourceType == 9 ? 9 : 5;
	const u8* source;
	const size_t byteCount = blocks * blockBytes;
	if (blocks == 0) {
		return;
	}
	if (!aramRange(vpb.currentAramAddr, byteCount, &source)) {
		std::fill(output, output + blocks * 16, 0);
		vpb.done = DSP_TRUE;
		return;
	}
	vpb.currentAramAddr += static_cast<u32>(byteCount);

	s32 yn2 = static_cast<s16>(vpb.afcRemainingSamples[14]);
	s32 yn1 = static_cast<s16>(vpb.afcRemainingSamples[15]);
	for (size_t block = 0; block < blocks; ++block) {
		const u8 header = *source++;
		const s32 scale = 1 << (header >> 4);
		const u8 coefficient = header & 0xf;
		const s32 coef0       = mAfcCoefficients[coefficient * 2 + 0];
		const s32 coef1       = mAfcCoefficients[coefficient * 2 + 1];

		for (size_t i = 0; i < 16; ++i) {
			s32 quantized;
			if (blockBytes == 9) {
				const u8 packed = source[i >> 1];
				const u8 nibble = (i & 1) == 0 ? packed >> 4 : packed & 0xf;
				quantized       = static_cast<s8>(nibble << 4) >> 4;
			} else {
				const u8 packed = source[i >> 2];
				const u8 shift  = static_cast<u8>(6 - (i & 3) * 2);
				quantized       = static_cast<s8>(((packed >> shift) & 3) << 6) >> 6;
				quantized *= 4;
			}

			s64 value = (static_cast<s64>(quantized) * scale << 11)
			          + static_cast<s64>(yn1) * coef0 + static_cast<s64>(yn2) * coef1;
			const s16 sample = static_cast<s16>(clamp16(value >> 11));
			*output++        = sample;
			yn2              = yn1;
			yn1              = sample;
		}
		source += blockBytes - 1;
	}
	vpb.afcRemainingSamples[14] = static_cast<u16>(static_cast<s16>(yn2));
	vpb.afcRemainingSamples[15] = static_cast<u16>(static_cast<s16>(yn1));
}

void HostRenderer::loadAfcFromAram(DSPchannel_& vpb, s16* output, u16 count)
{
	if (vpb.resetVpb != 0) {
		vpb.afcRemainingSamples[14]  = 0;
		vpb.afcRemainingSamples[15]  = 0;
		vpb.afcRemainingDecodedSamples = 0;
		vpb.remainingLength          = vpb.loopStartPosition;
		vpb.currentAramAddr          = vpb.baseAddress;
	}
	if (vpb.done != 0) {
		std::fill(output, output + count, 0);
		return;
	}

	while (count != 0) {
		const u16 countBefore       = count;
		const u32 addressBefore     = vpb.currentAramAddr;
		const u32 remainingBefore   = vpb.remainingLength;
		const u16 cachedBefore      = vpb.afcRemainingDecodedSamples;

		const u16 cached = std::min(vpb.afcRemainingDecodedSamples, count);
		const s16* cache = reinterpret_cast<const s16*>(vpb.afcRemainingSamples)
		                 + (16 - vpb.afcRemainingDecodedSamples);
		std::copy(cache, cache + cached, output);
		output += cached;
		count -= cached;
		vpb.afcRemainingDecodedSamples -= cached;
		if (count == 0) {
			return;
		}

		if (count <= vpb.remainingLength) {
			const u16 blocks  = static_cast<u16>((count + 15) >> 4);
			const u16 decoded = static_cast<u16>(blocks << 4);
			if (decoded < vpb.remainingLength) {
				vpb.afcRemainingDecodedSamples = decoded - count;
				vpb.remainingLength -= decoded;
			} else {
				vpb.afcRemainingDecodedSamples = static_cast<u16>(vpb.remainingLength - count);
				vpb.remainingLength = 0;
			}

			decodeAfcBlocks(vpb, output, blocks);
			if (vpb.done != 0) {
				std::fill(output, output + count, 0);
				return;
			}
			if (vpb.afcRemainingDecodedSamples != 0) {
				const s16* lastBlock = output + decoded - 16;
				for (size_t i = 0; i < 16; ++i) {
					vpb.afcRemainingSamples[i] = static_cast<u16>(lastBlock[i]);
				}

				if (vpb.remainingLength == 0 && vpb.loopStartPosition != 0) {
					const size_t offset = (vpb.loopStartPosition + 15) & 15;
					const s16* source   = reinterpret_cast<const s16*>(vpb.afcRemainingSamples) + offset;
					for (size_t i = 0; i < vpb.afcRemainingDecodedSamples; ++i) {
						const ptrdiff_t sourceIndex
						    = std::max<ptrdiff_t>(0, source - reinterpret_cast<const s16*>(vpb.afcRemainingSamples)
						                                      - static_cast<ptrdiff_t>(i));
						vpb.afcRemainingSamples[15 - i]
						    = static_cast<u16>(reinterpret_cast<const s16*>(vpb.afcRemainingSamples)[sourceIndex]);
					}
				}
			}
			return;
		}

		if (vpb.remainingLength != 0) {
			const u32 available = vpb.remainingLength;
			const u16 blocks    = static_cast<u16>((available + 15) >> 4);
			decodeAfcBlocks(vpb, output, blocks);
			if (vpb.done != 0) {
				std::fill(output, output + count, 0);
				return;
			}
			output += available;
			count -= static_cast<u16>(available);
			vpb.remainingLength = 0;
		}

		if (vpb.isLooping == 0) {
			vpb.done = DSP_TRUE;
			std::fill(output, output + count, 0);
			return;
		}

		const u32 loopByteOffset = (vpb.loopAddress >> 4) * vpb.samplesSourceType;
		if (loopByteOffset > std::numeric_limits<u32>::max() - vpb.baseAddress) {
			vpb.done = DSP_TRUE;
			std::fill(output, output + count, 0);
			return;
		}
		vpb.currentAramAddr         = vpb.baseAddress + loopByteOffset;
		vpb.afcRemainingSamples[14] = static_cast<u16>(vpb.loopYN2);
		vpb.afcRemainingSamples[15] = static_cast<u16>(vpb.loopYN1);
		decodeAfcBlocks(vpb, reinterpret_cast<s16*>(vpb.afcRemainingSamples), 1);
		if (vpb.done != 0) {
			std::fill(output, output + count, 0);
			return;
		}
		vpb.afcRemainingDecodedSamples = static_cast<u16>(16 - (vpb.loopAddress & 15));
		const u64 consumed = static_cast<u64>(vpb.afcRemainingDecodedSamples) + vpb.loopAddress;
		vpb.remainingLength = consumed <= vpb.loopStartPosition
		                        ? static_cast<u32>(vpb.loopStartPosition - consumed)
		                        : 0;

		if (count == countBefore && vpb.currentAramAddr == addressBefore
		    && vpb.remainingLength == remainingBefore
		    && vpb.afcRemainingDecodedSamples == cachedBefore) {
			std::fill(output, output + count, 0);
			vpb.done = DSP_TRUE;
			return;
		}
	}
}

void HostRenderer::loadDirectPcm(u8 voice, DSPchannel_& vpb, s16* output, u16 count)
{
	const DirectPCM direct = voice < sDirectPCM.size() ? sDirectPCM[voice] : DirectPCM {};
	const u32 ringLength   = std::min<u32>(vpb.loopStartPosition >> 16, direct.capacity);
	if (direct.samples == nullptr || ringLength == 0 || vpb.done != 0) {
		std::fill(output, output + count, 0);
		if (direct.samples == nullptr || ringLength == 0) {
			vpb.done = DSP_TRUE;
		}
		return;
	}

	u32 position = vpb.currentPosition >> 16;
	position %= ringLength;
	const u32 availableTotal = vpb.remainingLength;
	const u16 actual         = static_cast<u16>(std::min<u32>(count, availableTotal));
	s16 last                 = vpb.constantSample;
	for (u16 i = 0; i < actual; ++i) {
		last       = direct.samples[position];
		*output++  = last;
		position   = (position + 1) % ringLength;
	}
	if (actual < count) {
		std::fill(output, output + (count - actual), last);
		vpb.done = DSP_TRUE;
	}
	vpb.remainingLength -= actual;
	vpb.currentPosition = position << 16;
	vpb.samplesBeforeLoop = static_cast<u16>(ringLength - position);
	if (vpb.remainingLength == 0) {
		vpb.done = DSP_TRUE;
	}
}

void HostRenderer::loadInput(u8 voice, DSPchannel_& vpb, MixBuffer& output)
{
	/* +16: the AFC decoder rounds the sample count up to whole 16-sample
	   blocks, so it may write up to 15 samples past the requested count. */
	std::array<s16, kRawSampleCount + 16> raw {};
	std::copy(std::begin(vpb.resampleBuffer), std::end(vpb.resampleBuffer), raw.begin());

	if (vpb.useConstantSample != 0) {
		output.fill(vpb.constantSample);
		return;
	}

	switch (vpb.samplesSourceType) {
	case 0:
	case 3:
	{
		const u32 shift = vpb.samplesSourceType == 0 ? 1 : 2;
		const u32 mask  = (1u << shift) - 1;
		const u32 ratio = static_cast<u32>(vpb.resamplingRatio) << (shift - 1);
		u32 position    = static_cast<u32>(vpb.currentPosFrac) << shift;
		for (s32& sample : output) {
			sample = ((position >> 16) & mask) != 0 ? static_cast<s16>(0xc000) : 0x4000;
			position += ratio;
		}
		vpb.currentPosFrac = static_cast<u16>((position >> shift) & 0xffff);
		return;
	}
	case 1:
	{
		u32 position = vpb.currentPosFrac;
		for (s32& sample : output) {
			sample = static_cast<s16>(position & 0xffff);
			position += vpb.resamplingRatio >> 1;
		}
		vpb.currentPosFrac = static_cast<u16>(position);
		return;
	}
	case 4:
	case 7:
	case 10:
	case 11:
	case 12:
	{
		u16 patternIndex = 0;
		bool variable    = false;
		switch (vpb.samplesSourceType) {
		case 4:
			patternIndex = 1;
			break;
		case 10:
			variable = true;
			break;
		case 11:
			patternIndex = 2;
			break;
		case 12:
			patternIndex = 3;
			break;
		default:
			break;
		}
		const s16* pattern = mPatterns.data() + patternIndex * 64;
		u32 position      = static_cast<u32>(vpb.currentPosFrac) << 6;
		const u32 step    = static_cast<u32>(vpb.resamplingRatio) << 5;
		const MixBuffer& modulator = mBuses[busIndex(Bus::BackRight)];
		for (size_t i = 0; i < output.size(); ++i) {
			output[i] = pattern[(position >> 16) & 63];
			position  = (position + step) % (64u << 16);
			if (variable) {
				const s64 adjusted
				    = (static_cast<s64>(position) << 10) + static_cast<s64>(modulator[i]) * vpb.resamplingRatio;
				position = static_cast<u32>(std::max<s64>(0, adjusted >> 10)) % (64u << 16);
			}
		}
		vpb.currentPosFrac = static_cast<u16>(position >> 6);
		return;
	}
	case 5:
	case 9:
		loadAfcFromAram(vpb, raw.data() + 4, neededRawSamples(vpb));
		resample(vpb, raw.data(), output);
		return;
	case 8:
		loadPcmFromAram(vpb, raw.data() + 4, neededRawSamples(vpb), 1);
		resample(vpb, raw.data(), output);
		return;
	case 16:
		loadPcmFromAram(vpb, raw.data() + 4, neededRawSamples(vpb), 2);
		resample(vpb, raw.data(), output);
		return;
	case 0x21:
		loadDirectPcm(voice, vpb, raw.data() + 4, neededRawSamples(vpb));
		resample(vpb, raw.data(), output);
		return;
	default:
		output.fill(0);
		return;
	}
}

void HostRenderer::applyLowPass(DSPchannel_& vpb, MixBuffer& samples)
{
	s32 yn1 = vpb.resetVpb != 0 ? 0 : vpb.lowPassHistory[0];
	s32 xn1 = vpb.resetVpb != 0 ? 0 : vpb.lowPassHistory[1];
	const s32 coefficient = vpb.lowPassCoeff;
	for (s32& sample : samples) {
		const s32 xn0 = sample;
		const s64 value = yn1 + ((static_cast<s64>(xn0 - xn1) * coefficient) >> 7);
		const s16 yn0 = static_cast<s16>(clamp16(value));
		sample        = yn0;
		yn1           = yn0;
		xn1           = xn0;
	}
	vpb.lowPassHistory[0] = static_cast<s16>(yn1);
	vpb.lowPassHistory[1] = static_cast<s16>(xn1);
}

void HostRenderer::applyVariableFir(DSPchannel_& vpb, MixBuffer& samples)
{
	const size_t taps = std::min<size_t>(vpb.filterMode & 0x1f, 20);
	if (taps == 0) {
		return;
	}

	std::array<s16, 20 + kSubframeSamples> input {};
	for (size_t i = 0; i < 20; ++i) {
		input[i] = static_cast<s16>(vpb.variableFirHistory[i]);
	}
	for (size_t i = 0; i < samples.size(); ++i) {
		input[20 + i] = static_cast<s16>(clamp16(samples[i]));
	}

	for (size_t i = 0; i < samples.size(); ++i) {
		s64 sum = 0;
		for (size_t tap = 0; tap < taps; ++tap) {
			sum += static_cast<s64>(input[20 + i - tap]) * vpb.variableFirCoeffs[tap];
		}
		samples[i] = static_cast<s32>(clamp16(sum >> 15));
	}
	for (size_t i = 0; i < 20; ++i) {
		vpb.variableFirHistory[i] = static_cast<u16>(input[kSubframeSamples + i]);
	}
}

void HostRenderer::applyBiquad(DSPchannel_& vpb, MixBuffer& samples)
{
	s32 xn1 = vpb.biquadHistory[0];
	s32 xn2 = vpb.biquadHistory[1];
	s32 yn1 = vpb.biquadHistory[2];
	s32 yn2 = vpb.biquadHistory[3];
	for (s32& sample : samples) {
		const s32 xn0 = sample;
		s64 value = static_cast<s64>(vpb.biquadFilterCoeffs[0]) * xn1
		          + static_cast<s64>(vpb.biquadFilterCoeffs[1]) * xn2
		          + static_cast<s64>(vpb.biquadFilterCoeffs[2]) * yn1
		          + static_cast<s64>(vpb.biquadFilterCoeffs[3]) * yn2;
		const s16 yn0 = static_cast<s16>(clamp16(value >> 15));
		sample        = yn0;
		xn2           = xn1;
		xn1           = xn0;
		yn2           = yn1;
		yn1           = yn0;
	}
	vpb.biquadHistory[0] = static_cast<s16>(xn1);
	vpb.biquadHistory[1] = static_cast<s16>(xn2);
	vpb.biquadHistory[2] = static_cast<s16>(yn1);
	vpb.biquadHistory[3] = static_cast<s16>(yn2);
}

void HostRenderer::mixVoice(DSPchannel_& vpb, const MixBuffer& samples)
{
	if (vpb.useDolbyVolume != 0) {
		if (vpb.endRequested != 0) {
			vpb.dolbyVolumeTarget = static_cast<s16>(vpb.dolbyVolumeCurrent / 2);
			if (vpb.dolbyVolumeTarget == 0) {
				vpb.done = DSP_TRUE;
			}
		}

		const u8 x = static_cast<u8>((vpb.dolbyVoicePosition >> 8) & 0x7f);
		const u8 y = static_cast<u8>(vpb.dolbyVoicePosition & 0x7f);
		const s32 right = mDolbySine[x];
		const s32 left  = mDolbySine[x ^ 0x7f];
		const s32 back  = mDolbySine[y];
		const s32 front = mDolbySine[y ^ 0x7f];
		const s32 quadrants[4] = {
			left * front >> 16,
			left * back >> 16,
			right * front >> 16,
			right * back >> 16,
		};
		const Bus dry[4] = { Bus::FrontLeft, Bus::BackLeft, Bus::FrontRight, Bus::BackRight };
		const Bus wet[4]
		    = { Bus::FrontLeftReverb, Bus::BackLeftReverb, Bus::FrontRightReverb, Bus::BackRightReverb };
		const s32 delta = vpb.dolbyVolumeTarget - vpb.dolbyVolumeCurrent;
		for (size_t i = 0; i < 4; ++i) {
			const s32 start = quadrants[i] * vpb.dolbyVolumeCurrent >> 16;
			const s32 end   = quadrants[i] * vpb.dolbyVolumeTarget >> 16;
			const s32 dryStartQ16 = static_cast<s32>(static_cast<s64>(start) * 65536);
			const s32 dryStepQ16
			    = static_cast<s32>(static_cast<s64>(end - start) * 65536 / static_cast<s64>(kSubframeSamples));
			addWithVolume(mBuses[busIndex(dry[i])], samples, dryStartQ16, dryStepQ16);
			const s32 wetStart = start * vpb.dolbyReverbFactor >> 15;
			const s32 wetEnd   = end * vpb.dolbyReverbFactor >> 15;
			const s32 wetStartQ16 = static_cast<s32>(static_cast<s64>(wetStart) * 65536);
			const s32 wetStepQ16
			    = static_cast<s32>(static_cast<s64>(wetEnd - wetStart) * 65536 / static_cast<s64>(kSubframeSamples));
			addWithVolume(mBuses[busIndex(wet[i])], samples, wetStartQ16, wetStepQ16);
		}
		vpb.dolbyVolumeCurrent = vpb.dolbyVolumeTarget;
		return;
	}

	if (vpb.endRequested != 0) {
		bool silent = true;
		for (DSPMixerChannel& channel : vpb.mixChannels) {
			channel.targetVolume = static_cast<u16>(static_cast<s16>(channel.currentVolume) / 2);
			silent &= static_cast<s16>(channel.targetVolume) == 0;
		}
		if (silent) {
			vpb.done = DSP_TRUE;
		}
	}

	for (DSPMixerChannel& channel : vpb.mixChannels) {
		if (channel.id == 0) {
			continue;
		}
		MixBuffer* destination = bufferForId(channel.id);
		if (destination == nullptr) {
			continue;
		}
		const s32 current = static_cast<s16>(channel.currentVolume);
		const s32 target  = static_cast<s16>(channel.targetVolume);
		const s32 step
		    = static_cast<s32>(static_cast<s64>(target - current) * 65536 / static_cast<s64>(kSubframeSamples));
		if (current == 0 && step == 0) {
			continue;
		}
		addWithVolume(*destination, samples, static_cast<s32>(static_cast<s64>(current) * 65536), step);
		channel.currentVolume = static_cast<u16>(static_cast<s16>(target));
		if (sBusDiagnosticsEnabled) {
			s64 peakAfter = 0;
			for (s32 value : *destination) {
				peakAfter = std::max<s64>(peakAfter, value < 0 ? -static_cast<s64>(value) : value);
			}
			s64 peakIn = 0;
			for (s32 value : samples) {
				peakIn = std::max<s64>(peakIn, value < 0 ? -static_cast<s64>(value) : value);
			}
			if (peakAfter > 60000) {
				std::fprintf(stderr,
				             "[jaudio-diag-mix] id=%04x cur=%d tgt=%d step=%d in=%lld busAfter=%lld src=%u\n",
				             channel.id, current, target, step, static_cast<long long>(peakIn),
				             static_cast<long long>(peakAfter), vpb.samplesSourceType);
			}
		}
	}
}

void HostRenderer::renderVoice(u8 voice)
{
	DSPchannel_* vpb = GetDspHandle(voice);
	if (vpb == nullptr || vpb->enabled == 0 || vpb->done != 0) {
		return;
	}

	if (sBusDiagnosticsEnabled && vpb->resetVpb != 0) {
		std::fprintf(stderr, "[jaudio-diag-voice] v=%u src=%u ratio=%04x dolby=%u mix:", voice,
		             vpb->samplesSourceType, vpb->resamplingRatio, vpb->useDolbyVolume);
		for (const DSPMixerChannel& channel : vpb->mixChannels) {
			std::fprintf(stderr, " {id=%04x cur=%04x tgt=%04x lvl=%04x}", channel.id,
			             channel.currentVolume, channel.targetVolume, channel.level);
		}
		std::fputc('\n', stderr);
	}

	MixBuffer input {};
	loadInput(voice, *vpb, input);
	if (vpb->lowPassCoeff != 0) {
		applyLowPass(*vpb, input);
	}
	if ((vpb->filterMode & 0x1f) != 0) {
		applyVariableFir(*vpb, input);
	}
	if ((vpb->filterMode & 0x20) != 0
	    && (vpb->biquadFilterCoeffs[0] != 0x7fff || vpb->biquadFilterCoeffs[1] != 0
	        || vpb->biquadFilterCoeffs[2] != 0 || vpb->biquadFilterCoeffs[3] != 0)) {
		applyBiquad(*vpb, input);
	}
	mixVoice(*vpb, input);
	if (vpb->useConstantSample == 0) {
		vpb->resetVpb = DSP_FALSE;
	}
}

void HostRenderer::render(s16* interleaved)
{
	initialiseTables();
	beginSubframe();
	if (sBusDiagnosticsEnabled) {
		for (size_t bus = 0; bus < mBuses.size(); ++bus) {
			s64 peak = 0;
			for (s32 value : mBuses[bus]) {
				peak = std::max<s64>(peak, value < 0 ? -static_cast<s64>(value) : value);
			}
			if (peak > 60000) {
				std::fprintf(stderr, "[jaudio-diag-stage] after-begin bus=%zu peak=%lld\n", bus,
				             static_cast<long long>(peak));
			}
		}
	}
	for (u8 voice = 0; voice < kVoiceCount; ++voice) {
		renderVoice(voice);
	}
	finishSubframe(interleaved);
}

ALHeap sAudioHeap {};
void* sExpandedHeap = nullptr;
bool sAudioHeapPresent = false;
std::atomic<bool> sAudioRunning { false };
volatile int sDspSyncCount;
MixCallback sMixCallback = nullptr;
u8 sMixMode              = MixMode_Mono;
DACCallback sDacCallback = nullptr;
std::atomic<u32> sDacSyncCounter { 0 };
u8 sBgmVolume            = 8;
u8 sSeVolume             = 8;
u32 sAiStreamSampleRate  = AI_SAMPLERATE_48KHZ;
u32 sAiStreamTrigger     = 0;
u32 sAiStreamPlayState   = AI_STREAM_STOP;
u32 sAiStreamResetFrame  = 0;
u8 sAiStreamVolumeLeft   = 0;
u8 sAiStreamVolumeRight  = 0;

/*
 * Opt-in diagnostics for device-only investigations.  PIKMIN_JAUDIO_DUMP is
 * intentionally read once by the producer:
 *
 *   PIKMIN_JAUDIO_DUMP=1       -> /tmp/pikmin-jaudio-s16le-32000-stereo.raw
 *   PIKMIN_JAUDIO_DUMP=log     -> metrics only, no file
 *   PIKMIN_JAUDIO_DUMP=/path   -> raw PCM at the requested path
 *
 * PIKMIN_JAUDIO_DUMP_DELAY_SEC delays only the 15-second PCM window (0 by
 * default, clamped to 600 seconds); metrics still begin at audio startup.
 *
 * The object lives entirely on the audio producer thread, so it introduces no
 * locking or shared diagnostic state.  PCM capture is capped at 15 seconds
 * (1.92 MiB) even if the game keeps running.
 */
constexpr size_t kDiagnosticSourceTypes = 64;
constexpr size_t kDiagnosticOtherSource = kDiagnosticSourceTypes - 1;
constexpr size_t kDiagnosticDumpFrames  = static_cast<size_t>(kHostSampleRate) * 15;
constexpr size_t kDiagnosticReportFrames = static_cast<size_t>(kHostSampleRate) * 2;

struct SourceDiagnostics {
	u64 observations = 0;
	u32 minBase       = std::numeric_limits<u32>::max();
	u32 maxBase       = 0;
	u32 minCurrent    = std::numeric_limits<u32>::max();
	u32 maxCurrent    = 0;
	u16 minActualType = std::numeric_limits<u16>::max();
	u16 maxActualType = 0;
	u16 minRatio      = std::numeric_limits<u16>::max();
	u16 maxRatio      = 0;
	u8 maxConcurrent  = 0;
};

class AudioDiagnostics {
public:
	void start()
	{
		stop();
		mDumpedFrames          = 0;
		mDumpDelayFrames       = 0;
		mIntervalFrames        = 0;
		mTotalFrames           = 0;
		mSampleCount           = 0;
		mSquareSum             = 0.0L;
		mClipped               = 0;
		mPeak                  = 0;
		mVoiceSnapshots        = 0;
		mActiveVoiceObservations = 0;
		mMaxConcurrentVoices     = 0;
		mSources.fill(SourceDiagnostics {});
		resetBusDiagnostics();

		const char* setting = std::getenv("PIKMIN_JAUDIO_DUMP");
		if (setting == nullptr || setting[0] == '\0' || std::strcmp(setting, "0") == 0
		    || std::strcmp(setting, "off") == 0 || std::strcmp(setting, "false") == 0) {
			return;
		}

		mEnabled = true;
		sBusDiagnosticsEnabled = true;
		const char* delaySetting = std::getenv("PIKMIN_JAUDIO_DUMP_DELAY_SEC");
		if (delaySetting != nullptr && delaySetting[0] != '\0' && delaySetting[0] != '-') {
			char* end = nullptr;
			const unsigned long seconds = std::strtoul(delaySetting, &end, 10);
			if (end != delaySetting && end[0] == '\0') {
				mDumpDelayFrames = static_cast<size_t>(std::min<unsigned long>(seconds, 600))
				                 * kHostSampleRate;
			}
		}

		const char* path = setting;
		if (std::strcmp(setting, "1") == 0) {
			path = "/tmp/pikmin-jaudio-s16le-32000-stereo.raw";
		} else if (std::strcmp(setting, "log") == 0) {
			path = nullptr;
		}

		if (path != nullptr) {
			mDump = std::fopen(path, "wb");
			if (mDump == nullptr) {
				std::fprintf(stderr, "[jaudio-diag] cannot open PCM dump: %s\n", path);
			} else {
				std::fprintf(stderr,
				             "[jaudio-diag] dumping 15s of S16LE stereo 32000Hz PCM after %.2fs "
				             "to %s\n",
				             static_cast<double>(mDumpDelayFrames) / kHostSampleRate, path);
			}
		}
		std::fprintf(stderr, "[jaudio-diag] metrics enabled (2s intervals)\n");
	}

	void stop()
	{
		if (!mEnabled) {
			sBusDiagnosticsEnabled = false;
			return;
		}
		if (mIntervalFrames != 0) {
			report();
		}
		if (mDump != nullptr) {
			std::fflush(mDump);
			std::fclose(mDump);
		}
		mDump    = nullptr;
		mEnabled = false;
		sBusDiagnosticsEnabled = false;
	}

	void observe(const std::array<s16, kFrameSamples * 2>& pcm)
	{
		if (!mEnabled) {
			return;
		}

		if (mDump != nullptr && mDumpedFrames < kDiagnosticDumpFrames
		    && mTotalFrames + kFrameSamples > mDumpDelayFrames) {
			const size_t firstFrame = mTotalFrames < mDumpDelayFrames
			                            ? static_cast<size_t>(mDumpDelayFrames - mTotalFrames)
			                            : 0;
			const size_t frames = std::min(kFrameSamples - firstFrame,
			                               kDiagnosticDumpFrames - mDumpedFrames);
			const size_t written
			    = std::fwrite(pcm.data() + firstFrame * 2, sizeof(s16) * 2, frames, mDump);
			mDumpedFrames += written;
			if (written != frames) {
				std::fprintf(stderr, "[jaudio-diag] PCM dump write failed after %zu frames\n",
				             mDumpedFrames);
				std::fclose(mDump);
				mDump = nullptr;
			} else if (mDumpedFrames == kDiagnosticDumpFrames) {
				std::fflush(mDump);
				std::fclose(mDump);
				mDump = nullptr;
				std::fprintf(stderr, "[jaudio-diag] PCM dump complete: %zu frames (15.00s)\n",
				             mDumpedFrames);
			}
		}

		for (s16 sample : pcm) {
			const s32 magnitude = sample == std::numeric_limits<s16>::min()
			                        ? 0x8000
			                        : std::abs(static_cast<s32>(sample));
			mPeak = std::max(mPeak, magnitude);
			mSquareSum += static_cast<long double>(sample) * sample;
			mClipped += sample == std::numeric_limits<s16>::min()
			         || sample == std::numeric_limits<s16>::max();
		}
		mSampleCount += pcm.size();
		mIntervalFrames += kFrameSamples;
		mTotalFrames += kFrameSamples;

		std::array<u8, kDiagnosticSourceTypes> concurrent {};
		for (u8 voice = 0; voice < kVoiceCount; ++voice) {
			const DSPchannel_* vpb = GetDspHandle(voice);
			if (vpb == nullptr || vpb->enabled == 0 || vpb->done != 0) {
				continue;
			}

			const size_t source = vpb->samplesSourceType < kDiagnosticOtherSource
			                        ? static_cast<size_t>(vpb->samplesSourceType)
			                        : kDiagnosticOtherSource;
			SourceDiagnostics& stats = mSources[source];
			++stats.observations;
			stats.minBase    = std::min(stats.minBase, vpb->baseAddress);
			stats.maxBase    = std::max(stats.maxBase, vpb->baseAddress);
			stats.minCurrent = std::min(stats.minCurrent, vpb->currentAramAddr);
			stats.maxCurrent = std::max(stats.maxCurrent, vpb->currentAramAddr);
			stats.minActualType = std::min(stats.minActualType, vpb->samplesSourceType);
			stats.maxActualType = std::max(stats.maxActualType, vpb->samplesSourceType);
			stats.minRatio      = std::min(stats.minRatio, vpb->resamplingRatio);
			stats.maxRatio      = std::max(stats.maxRatio, vpb->resamplingRatio);
			if (concurrent[source] != std::numeric_limits<u8>::max()) {
				++concurrent[source];
			}
			++mActiveVoiceObservations;
		}
		for (size_t source = 0; source < mSources.size(); ++source) {
			mSources[source].maxConcurrent
			    = std::max(mSources[source].maxConcurrent, concurrent[source]);
		}
		mMaxConcurrentVoices = std::max<u16>(
		    mMaxConcurrentVoices,
		    static_cast<u16>(std::min<u64>(
		        std::accumulate(concurrent.begin(), concurrent.end(), static_cast<u64>(0)),
		        std::numeric_limits<u16>::max())));
		++mVoiceSnapshots;

		if (mIntervalFrames >= kDiagnosticReportFrames) {
			report();
		}
	}

private:
	void report()
	{
		const long double rms
		    = mSampleCount == 0 ? 0.0L : std::sqrt(mSquareSum / mSampleCount);
		const long double clipPercent
		    = mSampleCount == 0 ? 0.0L : static_cast<long double>(mClipped) * 100.0L / mSampleCount;
		const long double averageVoices = mVoiceSnapshots == 0
		                                    ? 0.0L
		                                    : static_cast<long double>(mActiveVoiceObservations)
		                                          / mVoiceSnapshots;
		std::fprintf(stderr,
		             "[jaudio-diag] t=%.2Lfs peak=%d rms=%.1Lf clip=%llu/%llu (%.4Lf%%) "
		             "voices(avg=%.2Lf,max=%u)",
		             static_cast<long double>(mTotalFrames) / kHostSampleRate, mPeak, rms,
		             static_cast<unsigned long long>(mClipped),
		             static_cast<unsigned long long>(mSampleCount), clipPercent, averageVoices,
		             static_cast<unsigned>(mMaxConcurrentVoices));

		for (size_t source = 0; source < mSources.size(); ++source) {
			const SourceDiagnostics& stats = mSources[source];
			if (stats.observations == 0) {
				continue;
			}
			if (source == kDiagnosticOtherSource) {
				std::fprintf(stderr, " srcOther(types=%u-%u)",
				             static_cast<unsigned>(stats.minActualType),
				             static_cast<unsigned>(stats.maxActualType));
			} else {
				std::fprintf(stderr, " src%zu", source);
			}
			std::fprintf(stderr,
			             "{vf=%llu,max=%u,ratio=%04x-%04x,base=%08x-%08x,cur=%08x-%08x}",
			             static_cast<unsigned long long>(stats.observations),
			             static_cast<unsigned>(stats.maxConcurrent),
			             static_cast<unsigned>(stats.minRatio),
			             static_cast<unsigned>(stats.maxRatio),
			             static_cast<unsigned>(stats.minBase),
			             static_cast<unsigned>(stats.maxBase),
			             static_cast<unsigned>(stats.minCurrent),
			             static_cast<unsigned>(stats.maxCurrent));
		}
		std::fputc('\n', stderr);

		std::fprintf(stderr, "[jaudio-diag-bus]");
		for (size_t bus = 0; bus < sBusDiagnostics.size(); ++bus) {
			const BusDiagnostics& stats = sBusDiagnostics[bus];
			const long double rms = stats.sampleCount == 0
			                           ? 0.0L
			                           : std::sqrt(stats.squareSum / stats.sampleCount);
			std::fprintf(stderr, " %s{peak=%lld,rms=%.1Lf,out=%llu/%llu}",
			             kDiagnosticBusNames[bus],
			             static_cast<long long>(stats.peak), rms,
			             static_cast<unsigned long long>(stats.outsideInt16),
			             static_cast<unsigned long long>(stats.sampleCount));
		}
		std::fputc('\n', stderr);
		if (mDump != nullptr) {
			std::fflush(mDump);
		}

		mIntervalFrames          = 0;
		mSampleCount             = 0;
		mSquareSum               = 0.0L;
		mClipped                 = 0;
		mPeak                    = 0;
		mVoiceSnapshots          = 0;
		mActiveVoiceObservations = 0;
		mMaxConcurrentVoices     = 0;
		mSources.fill(SourceDiagnostics {});
		resetBusDiagnostics();
	}

	bool mEnabled = false;
	FILE* mDump   = nullptr;
	size_t mDumpedFrames  = 0;
	size_t mDumpDelayFrames = 0;
	size_t mIntervalFrames = 0;
	u64 mTotalFrames       = 0;
	u64 mSampleCount       = 0;
	long double mSquareSum = 0.0L;
	u64 mClipped           = 0;
	s32 mPeak              = 0;
	u64 mVoiceSnapshots    = 0;
	u64 mActiveVoiceObservations = 0;
	u16 mMaxConcurrentVoices     = 0;
	std::array<SourceDiagnostics, kDiagnosticSourceTypes> mSources {};
};

AudioDiagnostics sAudioDiagnostics;

void applyExternalMix(s16* pcm, size_t frames)
{
	if (sMixCallback == nullptr) {
		return;
	}
	s16* extra = sMixCallback(static_cast<s32>(frames));
	if (extra == nullptr) {
		return;
	}
	for (size_t i = 0; i < frames; ++i) {
		s32 left;
		s32 right;
		switch (sMixMode) {
		case MixMode_MonoWide:
			left  = pcm[i * 2 + 0] + extra[i];
			right = pcm[i * 2 + 1] - extra[i];
			break;
		case MixMode_Extra:
			left  = pcm[i * 2 + 0] + extra[kFrameSamples + i];
			right = pcm[i * 2 + 1] + extra[i];
			break;
		case MixMode_Interleave:
			left  = pcm[i * 2 + 0] + extra[i * 2 + 0];
			right = pcm[i * 2 + 1] + extra[i * 2 + 1];
			break;
		case MixMode_Mono:
		default:
			left  = pcm[i * 2 + 0] + extra[i];
			right = pcm[i * 2 + 1] + extra[i];
			break;
		}
		pcm[i * 2 + 0] = static_cast<s16>(clamp16(left));
		pcm[i * 2 + 1] = static_cast<s16>(clamp16(right));
	}
}

void renderJAudioFrame(std::array<s16, kFrameSamples * 2>& pcm)
{
	DspSyncCountClear(static_cast<int>(kSubframes));
	for (size_t subframe = 0; subframe < kSubframes; ++subframe) {
		DspPlayerCallback();
		UpdateDSPchannelAll();
		sRenderer.render(pcm.data() + subframe * kSubframeSamples * 2);
		PlayerCallback();
		sDspSyncCount = static_cast<int>(kSubframes - subframe - 1);
	}
	++JAC_VFRAME_COUNTER;
	// One tick per rendered audio frame, which is what Jac_GetCurrentSCounter
	// reports. On the console it came from the DSP; here the host renderer is
	// the thing that advances, so it keeps the count.
	//
	// Atomic because Jac_HVQM_Init spins on it from another thread, waiting for
	// one audio frame to pass. A spin on a plain variable is compiled under LTO
	// as a single load outside the loop -- a hang, not a wait. That has caught
	// this project once before.
	sDacSyncCounter.fetch_add(1, std::memory_order_relaxed);
	StreamMain();
	applyExternalMix(pcm.data(), kFrameSamples);
	if (sDacCallback != nullptr) {
		sDacCallback(pcm.data(), static_cast<s32>(kFrameSamples));
	}
}

// Run on the game thread, like the port's existing audio pump. DVD tasks
// are synchronous; no unsynchronised producer may mutate JAudio tracks.
bool sSinkReady = false;
u64 sNextSinkAttempt = 0;
u64 sNextSilentFrame = 0;
bool sPumping = false;

// Opening the playback device can block for a long time when the default
// device is missing or busy -- SDL's PulseAudio backend waits a full 30
// seconds before reporting failure. This pump runs on the game thread, so a
// blocking open froze the whole game in 30-second chunks, retrying every half
// second: the black screen and the very slow start people saw when audio was
// not ready at launch. Attempt the open on a worker and keep simulating and
// rendering silently until it lands.
//
// Only the pump touches the sink, and it leaves it alone while an attempt is
// in flight (sSinkReady stays false, which gates every other sink call), so
// the worker owns the sink for the duration of its attempt.
enum class SinkOpen { Idle, Running, Finished };
std::thread sSinkOpenThread;
std::atomic<SinkOpen> sSinkOpenState { SinkOpen::Idle };
std::atomic<bool> sSinkOpenResult { false };
bool sSinkFailureReported = false;

void joinSinkOpenThread()
{
    if (sSinkOpenThread.joinable()) sSinkOpenThread.join();
}

void beginSinkOpen()
{
    joinSinkOpenThread();
    sSinkOpenState.store(SinkOpen::Running);
    sSinkOpenThread = std::thread([] {
        const bool ok = PikiAudioSinkTryOpen(kHostSampleRate) != 0;
        sSinkOpenResult.store(ok);
        sSinkOpenState.store(SinkOpen::Finished);
    });
}

void serviceSinkOpen(u64 now)
{
    switch (sSinkOpenState.load()) {
    case SinkOpen::Idle:
        if (now >= sNextSinkAttempt) beginSinkOpen();
        break;
    case SinkOpen::Running:
        break;
    case SinkOpen::Finished:
        joinSinkOpenThread();
        sSinkReady = sSinkOpenResult.load();
        sSinkOpenState.store(SinkOpen::Idle);
        sNextSinkAttempt = now + kSinkRetryPeriodNs;
        // Say so once, so a silent game is distinguishable from a broken one.
        // Retries continue in the background; if a device appears later the
        // sound comes back on its own.
        if (!sSinkReady && !sSinkFailureReported) {
            sSinkFailureReported = true;
            std::fprintf(stderr, "[jaudio] no audio device yet; playing silently and retrying\n");
        } else if (sSinkReady) {
            sSinkFailureReported = false;
        }
        break;
    }
}

void pumpAudio()
{
    if (!sAudioRunning.load() || sPumping) return;
    sPumping = true;
    const u64 now = monotonicNs();
    if (!sSinkReady) serviceSinkOpen(now);
    std::array<s16, kFrameSamples * 2> pcm {};
    // Bound catch-up work and queued latency even after a long frame stall.
    for (size_t frame = 0; frame < 12; ++frame) {
        if (sSinkReady) {
            if (PikiAudioSinkQueuedFrames() >= kMaxQueuedFrames) break;
        } else if (now < sNextSilentFrame) break;
        renderJAudioFrame(pcm);
        sAudioDiagnostics.observe(pcm);
        if (sSinkReady) {
            if (!PikiAudioSinkQueue(pcm.data(), kFrameSamples)) {
                PikiAudioSinkClose();
                sSinkReady = false;
                sNextSinkAttempt = now + kSinkRetryPeriodNs;
            }
        }
        if (!sSinkReady) {
            sNextSilentFrame = now + kAudioFramePeriodNs;
            break;
        }
    }
    if (sSinkReady && PikiAudioSinkQueuedFrames() >= kPrebufferFrames)
        PikiAudioSinkResume();
    sPumping = false;
}

} // namespace

extern "C" {

u32 UNIVERSAL_DACCOUNTER = 0;
u32 JAC_VFRAME_COUNTER   = 0;

u32 AIGetStreamSampleCount(void)
{
	if (sAiStreamPlayState != AI_STREAM_START) {
		return 0;
	}
	const u32 frames = JAC_VFRAME_COUNTER - sAiStreamResetFrame;
	const u32 samplesPerFrame
	    = sAiStreamSampleRate == AI_SAMPLERATE_48KHZ ? static_cast<u32>(kFrameSamples * 3 / 2)
	                                                  : static_cast<u32>(kFrameSamples);
	return frames * samplesPerFrame;
}

void AIResetStreamSampleCount(void) { sAiStreamResetFrame = JAC_VFRAME_COUNTER; }

void AISetStreamTrigger(u32 trigger) { sAiStreamTrigger = trigger; }

u32 AIGetStreamTrigger(void) { return sAiStreamTrigger; }

void AISetStreamPlayState(u32 state)
{
	sAiStreamPlayState = state != 0 ? AI_STREAM_START : AI_STREAM_STOP;
	if (sAiStreamPlayState == AI_STREAM_START) {
		sAiStreamResetFrame = JAC_VFRAME_COUNTER;
	}
}

u32 AIGetStreamPlayState(void) { return sAiStreamPlayState; }

void AISetStreamSampleRate(u32 rate) { sAiStreamSampleRate = rate; }

u32 AIGetStreamSampleRate(void) { return sAiStreamSampleRate; }

void AISetStreamVolLeft(u8 volume) { sAiStreamVolumeLeft = volume; }

u8 AIGetStreamVolLeft(void) { return sAiStreamVolumeLeft; }

void AISetStreamVolRight(u8 volume) { sAiStreamVolumeRight = volume; }

u8 AIGetStreamVolRight(void) { return sAiStreamVolumeRight; }

void PikiJAudioSetDirectPCM(u8 voice, const s16* samples, u32 capacitySamples)
{
	if (voice >= sDirectPCM.size()) {
		return;
	}
	sDirectPCM[voice].samples  = samples;
	sDirectPCM[voice].capacity = capacitySamples;
}

void PikiJAudioClearDirectPCM(u8 voice)
{
	if (voice < sDirectPCM.size()) {
		sDirectPCM[voice] = {};
	}
}

void PikiJAudioSetGameVolumeState(u8 bgm, u8 se)
{
	sBgmVolume = static_cast<u8>(clampMenuVolume(bgm));
	sSeVolume  = static_cast<u8>(clampMenuVolume(se));
}

u16 PikiJAudioBGMStreamLevel(void)
{
	static constexpr u16 levels[] = { 0, 600, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000 };
	return levels[clampMenuVolume(sBgmVolume)];
}

u16 PikiJAudioSEStreamLevel(void)
{
	static constexpr u16 levels[] = { 0, 600, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000 };
	return levels[clampMenuVolume(sSeVolume)];
}

int PikiJAudioStereoOutput(void) { return Jac_GetOutputMode() != 0; }

void Jac_HeapSetup(void* heap, s32 size)
{
    // The console supplies 512 KiB. Expanded 64-bit IBNK/WSYS graphs and
    // stream rings need a host allocation independent of the game's arena.
    constexpr s32 minimumHostHeap = 16 * 1024 * 1024;
    if (size < minimumHostHeap) {
        sExpandedHeap = std::malloc(minimumHostHeap);
        if (sExpandedHeap) { heap = sExpandedHeap; size = minimumHostHeap; }
    }
	if (heap != nullptr && size > 0) {
		Nas_HeapInit(&sAudioHeap, static_cast<u8*>(heap), size);
		sAudioHeapPresent = true;
	} else {
		sAudioHeapPresent = false;
	}
}

void* OSAlloc2(u32 size)
{
	const BOOL interrupts = OSDisableInterrupts();
	void* allocation;
	if (sAudioHeapPresent) {
		allocation = Nas_HeapAlloc(&sAudioHeap, static_cast<s32>(size));
	} else {
		allocation = OSAllocFromHeap(__OSCurrHeap, size);
	}
	OSRestoreInterrupts(interrupts);
	return allocation;
}

void Jac_Init() {}

u32 Jac_GetCurrentVCounter(void) { return JAC_VFRAME_COUNTER; }

void Jac_VframeWork()
{
	/*
	 * AI DMA used to enter here.  The host producer owns the render cadence,
	 * so an incidental legacy call only advances no additional audio.
	 */
}

void Jac_UpdateDAC() {}

void Jac_RegisterDacCallback(DACCallback callback) { sDacCallback = callback; }

MixCallback Jac_GetMixcallback(u8* mode)
{
	if (mode != nullptr) {
		*mode = sMixMode;
	}
	return sMixCallback;
}

void Jac_RegisterMixcallback(MixCallback callback, u8 mode)
{
	sMixCallback = callback;
	sMixMode     = mode < MixMode_Num ? mode : MixMode_Mono;
}

void Jac_SetOutputMode(int mode) { JAC_SYSTEM_OUTPUT_MODE = mode; }

int Jac_GetOutputMode() { return static_cast<int>(JAC_SYSTEM_OUTPUT_MODE); }

void Jac_SetMixerLevel(f32 channelLevel, f32 dspLevel)
{
	Channel_SetMixerLevel(channelLevel);
	DsetMixerLevel(dspLevel);
}

void DspSyncCountClear(int count) { sDspSyncCount = count; }

int DspSyncCountCheck() { return sDspSyncCount; }

void PikiJAudioTick() { pumpAudio(); }

void StopAudioThread()
{
    sAudioRunning.store(false);
    // The worker may still be sitting in a blocking open; it owns the sink
    // until it returns, so wait for it before touching the device.
    joinSinkOpenThread();
    sSinkOpenState.store(SinkOpen::Idle);
    PikiAudioSinkShutdown();
    sSinkReady = false;
    sNextSinkAttempt = 0;
    sAudioDiagnostics.stop();
    std::free(sExpandedHeap);
    sExpandedHeap = nullptr;
    sAudioHeapPresent = false;
}

void StartAudioThread(void* heap, s32 heapSize, u32 aramSize, u32 flags)
{
    StopAudioThread();
    Jac_HeapSetup(heap, heapSize);
    Jac_SetAudioARAMSize(aramSize);
    Jac_InitARAM((flags & AUDIO_THREAD_FLAG_NEOS) != 0);
    Jac_Init();
    Jac_InitSinTable();
    ResetPlayerCallback();
    DSP_InitBuffer();
    sRenderer = HostRenderer {};
    sDirectPCM.fill(DirectPCM {});
    sNextSinkAttempt = sNextSilentFrame = 0;
    sAudioDiagnostics.start();
    sAudioRunning.store((flags & AUDIO_THREAD_FLAG_AUDIO) != 0);
}

void HaltDSPSignal() {}
void HaltDSP() {}
void RunDSP() {}
void CheckHaltDSP() {}
void NeosSync() {}
void SetAudioThreadPriority() {}
void Jac_GetDacRate() {}

} // extern "C"

// Outside the extern "C" block above on purpose: jaudio/dspbuf.h declares this
// one without C linkage, and the H4M player is compiled against that header.
// Defined here rather than in dolphin_stubs/dspbuf.cpp because with the native
// engine the host renderer is what advances -- it is the thing that knows an
// audio frame has gone by.
u32 Jac_GetCurrentSCounter() { return sDacSyncCounter.load(std::memory_order_relaxed); }

/*
 * dspproc.h intentionally uses C++ linkage.  These symbols replace mailbox
 * traffic; the actual work is driven directly by HostRenderer.
 */
s32 DSPSendCommands(u32*, u32) { return 0; }
u32 DSPReleaseHalt() { return 0; }
void DSPWaitFinish() {}
void DsetupTable(u32, u32, u32, u32, u32) {}
void DsetMixerLevel(f32 level) { sRenderer.setMasterLevel(level); }
void DsyncFrame(u32, u32, u32) {}
void DwaitFrame() {}
void DiplSec(u32) {}
void DagbSec(u32) {}
