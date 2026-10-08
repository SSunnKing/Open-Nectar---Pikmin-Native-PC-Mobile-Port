/**
 * @file pc_dsp_host_test.cpp
 * @brief Offline checks for the host software DSP.
 *
 * These verify the properties that can be established without listening:
 * unity pitch is transparent, pitch scales the read rate, volume ramps land on
 * their target, loops wrap to the engine's loop point and finished voices
 * report completion back through the voice parameter block.
 */

#include "pc_aram.h"
#include "pc_dsp_host.h"
#include "jaudio/dspinterface.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

constexpr u32 kFrame     = 560;
constexpr u32 kUnity     = 0x800;
constexpr u32 kAramBase  = 0x10000;
constexpr s16 kFullScale = 0x7FFF;

/// Writes @p samples as big-endian PCM16 into host ARAM at @p offset.
void writePcm16(u32 offset, const std::vector<s16>& samples)
{
	u8* at = pc_aram_write(offset, samples.size() * 2);
	if (at == nullptr) {
		return;
	}
	for (size_t i = 0; i < samples.size(); ++i) {
		at[i * 2]     = static_cast<u8>((static_cast<u16>(samples[i]) >> 8) & 0xFF);
		at[i * 2 + 1] = static_cast<u8>(static_cast<u16>(samples[i]) & 0xFF);
	}
}

/// A voice playing PCM16 from kAramBase at unity gain into the dry left bus.
DSPchannel_ makeChannel(u32 length)
{
	DSPchannel_ channel;
	std::memset(&channel, 0, sizeof(channel));
	channel.enabled            = DSP_TRUE;
	channel.resamplingRatio    = static_cast<u16>(kUnity);
	channel.samplesSourceType  = 16; // PCM16
	channel.baseAddress        = kAramBase;
	channel.remainingLength    = length;
	channel.mixChannels[0].id  = 0x0D00; // dry left
	channel.mixChannels[0].currentVolume = static_cast<u16>(kFullScale);
	channel.mixChannels[0].targetVolume  = static_cast<u16>(kFullScale);
	channel.mixChannels[1].id  = 0x0D60; // dry right
	channel.mixChannels[1].currentVolume = static_cast<u16>(kFullScale);
	channel.mixChannels[1].targetVolume  = static_cast<u16>(kFullScale);
	return channel;
}

} // namespace

int main()
{
	check(pc_aram_init(), "ARAM init");
	pc_dsp_host_init();
	check(pc_dsp_host_ready(), "renderer reports ready");

	// A ramp is easy to reason about under resampling. It has to outlast the
	// fastest pitch under test: 560 output samples at double rate read 1120.
	std::vector<s16> source(2048);
	for (size_t i = 0; i < source.size(); ++i) {
		source[i] = static_cast<s16>((i % 256) * 64);
	}
	writePcm16(kAramBase, source);

	std::vector<s16> out(kFrame * 2, 0);

	// 1. Unity pitch is transparent to within the interpolation error.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);

		int mismatches = 0;
		for (u32 i = 0; i < 64; ++i) {
			// Full-scale volume is 0x7FFF/0x8000, one part in 32768 short.
			if (std::abs(static_cast<int>(out[i * 2]) - source[i]) > 2) {
				++mismatches;
			}
		}
		check(mismatches == 0, "unity pitch reproduces the source");
		check(out[0] == out[1], "both dry buses receive the same signal");
		check(pc_dsp_host_active_voices() == 1, "one voice counted active");
		check(channel.currentPosition == kFrame,
		      "unity pitch advances one sample per output sample");
		check(channel.currentPosFrac == 0, "unity pitch leaves no phase debt");
	}

	// 2. Pitch scales the read rate.
	{
		DSPchannel_ channel  = makeChannel(static_cast<u32>(source.size()));
		channel.resamplingRatio = static_cast<u16>(kUnity * 2);
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.currentPosition == kFrame * 2,
		      "double pitch consumes two samples per output sample");
	}
	{
		DSPchannel_ channel  = makeChannel(static_cast<u32>(source.size()));
		channel.resamplingRatio = static_cast<u16>(kUnity / 2);
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.currentPosition == kFrame / 2,
		      "half pitch consumes one sample per two output samples");
	}

	// 3. A silent voice produces silence, and a disabled one is skipped.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		for (int m = 0; m < 6; ++m) {
			channel.mixChannels[m].currentVolume = 0;
			channel.mixChannels[m].targetVolume  = 0;
		}
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		bool silent = true;
		for (u32 i = 0; i < kFrame * 2; ++i) {
			if (out[i] != 0) {
				silent = false;
			}
		}
		check(silent, "zero volume renders silence");
	}
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		channel.enabled     = DSP_FALSE;
		std::memset(out.data(), 0x7F, out.size() * sizeof(s16));
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(out[0] == 0 && out[kFrame * 2 - 1] == 0,
		      "a disabled voice clears the output buffer");
		check(pc_dsp_host_active_voices() == 0, "disabled voice is not counted");
	}

	// 4. Volume ramps land exactly on their target.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		channel.mixChannels[0].currentVolume = 0;
		channel.mixChannels[0].targetVolume  = static_cast<u16>(kFullScale);
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.mixChannels[0].currentVolume == static_cast<u16>(kFullScale),
		      "ramp commits the target volume");
		check(std::abs(out[0]) < std::abs(out[158]),
		      "a rising ramp starts quieter than it ends");
	}

	// 5. A short looping voice wraps to the engine's loop point.
	{
		DSPchannel_ channel      = makeChannel(100);
		channel.isLooping        = DSP_TRUE;
		channel.loopStartPosition = 10;
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.done == DSP_FALSE, "a looping voice does not finish");
		check(channel.currentPosition < 100,
		      "a looping voice stays inside the sample");
		check(channel.currentPosition >= 10,
		      "a looping voice wraps to the loop point, not to zero");
	}

	// 6. A short one-shot reports completion back to the driver.
	{
		DSPchannel_ channel = makeChannel(100);
		channel.isLooping   = DSP_FALSE;
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.endReached == DSP_TRUE, "a finished voice sets endReached");
		check(channel.done == DSP_TRUE, "a finished voice sets done");
	}

	// 7. Paused voices hold their constant sample.
	{
		DSPchannel_ channel        = makeChannel(static_cast<u32>(source.size()));
		channel.useConstantSample  = 1;
		channel.constantSample     = 1000;
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(std::abs(static_cast<int>(out[0]) - 1000) <= 2,
		      "a paused voice emits its constant sample");
		check(out[0] == out[100], "a paused voice emits a steady level");
	}

	// 8. A zero ratio is treated as unity rather than freezing the voice.
	{
		DSPchannel_ channel     = makeChannel(static_cast<u32>(source.size()));
		channel.resamplingRatio = 0;
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(channel.currentPosition == kFrame, "zero ratio falls back to unity");
	}

	// 9. Voices sum, and the mix saturates instead of wrapping.
	{
		DSPchannel_ pair[2] = { makeChannel(static_cast<u32>(source.size())),
			                    makeChannel(static_cast<u32>(source.size())) };
		pc_dsp_host_render_frame(pair, 2, out.data(), kFrame);
		check(pc_dsp_host_active_voices() == 2, "both voices counted active");
		// Sample 4 of the ramp is 256; two voices give about 512.
		check(out[8] > 400 && out[8] < 620, "two voices sum");
	}
	{
		std::vector<s16> loud(64, 32767);
		writePcm16(kAramBase + 0x8000, loud);
		DSPchannel_ pair[2] = { makeChannel(64), makeChannel(64) };
		pair[0].baseAddress = kAramBase + 0x8000;
		pair[1].baseAddress = kAramBase + 0x8000;
		pair[0].isLooping   = DSP_TRUE;
		pair[1].isLooping   = DSP_TRUE;
		pc_dsp_host_render_frame(pair, 2, out.data(), kFrame);
		check(out[0] == 32767, "the mix saturates rather than wrapping");
	}

	// 10. Degenerate arguments are refused without crashing.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		pc_dsp_host_render_frame(nullptr, 0, out.data(), kFrame);
		check(out[0] == 0, "a null channel array renders silence");
		pc_dsp_host_render_frame(&channel, 1, nullptr, kFrame);
		pc_dsp_host_render_frame(&channel, 1, out.data(), 0);
	}

	// 11. A voice reading outside ARAM is silent rather than unsafe.
	{
		DSPchannel_ channel = makeChannel(64);
		channel.baseAddress = PC_ARAM_SIZE - 8;
		channel.isLooping   = DSP_TRUE;
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(out[kFrame * 2 - 1] == 0, "an out-of-range voice renders silence");
	}

	// 12. Planar output puts the left channel in the SECOND plane.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		// Route to the dry left bus only.
		channel.mixChannels[1].id = 0x0000;
		channel.mixChannels[1].currentVolume = 0;
		channel.mixChannels[1].targetVolume  = 0;

		std::vector<s16> planar(kFrame * 2, 0);
		pc_dsp_host_render_frame_planar(&channel, 1, planar.data(), kFrame);

		bool firstPlaneSilent = true;
		for (u32 i = 1; i < kFrame; ++i) {
			if (planar[i] != 0) {
				firstPlaneSilent = false;
			}
		}
		check(firstPlaneSilent, "a left-only voice leaves the first plane silent");

		bool secondPlaneSounds = false;
		for (u32 i = 0; i < kFrame; ++i) {
			if (planar[kFrame + i] != 0) {
				secondPlaneSounds = true;
			}
		}
		check(secondPlaneSounds, "a left-only voice fills the second plane");
	}

	// 13. Interleaved output is unaffected by a preceding planar render.
	{
		DSPchannel_ channel = makeChannel(static_cast<u32>(source.size()));
		pc_dsp_host_render_frame(&channel, 1, out.data(), kFrame);
		check(std::abs(static_cast<int>(out[2]) - source[1]) <= 2,
		      "interleaved layout is restored after a planar render");
	}

	pc_dsp_host_shutdown();
	pc_aram_shutdown();

	if (sFailures == 0) {
		std::printf("pc_dsp_host_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_dsp_host_test: %d failure(s)\n", sFailures);
	return 1;
}
