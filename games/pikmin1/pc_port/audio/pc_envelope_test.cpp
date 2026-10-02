#include "audio/pc_envelope.h"

#include <cmath>
#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

void check_float(float expected, float actual, float epsilon = 0.000001f) {
    ++checks;
    if (std::abs(expected - actual) > epsilon) {
        std::printf("FAIL: expected %.9f, got %.9f\n", expected, actual);
        ++failures;
    }
}

PCInstrumentOscillator oscillator(std::initializer_list<PCEnvelopePoint> attack) {
    PCInstrumentOscillator osc;
    osc.attack = attack;
    return osc;
}

float begin(PCInstrumentOscillator& osc, PCEnvelopeState& state, float sampleRate) {
    pc_envelope_init_attack(&state, &osc);
    return pc_envelope_step(&osc, &state, sampleRate);
}

float advance(PCInstrumentOscillator& osc, PCEnvelopeState& state,
              float sampleRate, int frames) {
    float value = pc_envelope_get_value(&state, &osc);
    for (int i = 0; i < frames; ++i) value = pc_envelope_step(&osc, &state, sampleRate);
    return value;
}

void test_curves_exact() {
    for (s16 curve = 0; curve <= 2; ++curve) {
        auto osc = oscillator({ { curve, 600, 32767 }, { 0x0F, 0, 0 } });
        PCEnvelopeState state;
        check_float(0.0f, begin(osc, state, 600.0f));
        const float rawHalf = (32767.0f / 32768.0f) * 0.5f;
        const float expectedHalf = curve == 0 ? rawHalf
                                 : curve == 1 ? rawHalf * rawHalf
                                              : std::sqrt(rawHalf);
        check_float(expectedHalf, advance(osc, state, 600.0f, 300));
        const float rawEnd = 32767.0f / 32768.0f;
        const float expectedEnd = curve == 0 ? rawEnd
                                : curve == 1 ? rawEnd * rawEnd
                                             : std::sqrt(rawEnd);
        check_float(expectedEnd, advance(osc, state, 600.0f, 300));
        CHECK(!pc_envelope_is_active(&state));
        CHECK(state.result == PCEnvelopeResult::Ok);
    }
}

void test_timing_rate_and_sample_rate() {
    auto osc = oscillator({ { 0, 600, 32767 }, { 0x0F, 0, 0 } });
    osc.rate = 2.0f;
    PCEnvelopeState state;
    begin(osc, state, 48000.0f); // 600 / (600 * 2) s = 24000 frames.
    check_float((32767.0f / 32768.0f) * 0.5f,
                advance(osc, state, 48000.0f, 12000));
    CHECK(pc_envelope_is_active(&state));
    advance(osc, state, 48000.0f, 12000);
    CHECK(!pc_envelope_is_active(&state));
}

void test_width_vertex_and_current_value() {
    auto osc = oscillator({ { 0, 600, 16384 }, { 0x0E, 0, 0 } });
    osc.width = 2.0f;
    osc.vertex = -0.25f;
    PCEnvelopeState state;
    check_float(-0.25f, begin(osc, state, 600.0f));
    check_float(0.25f, advance(osc, state, 600.0f, 300));
    check_float(0.25f, pc_envelope_get_value(&state, &osc));
    check_float(0.75f, advance(osc, state, 600.0f, 300));
    CHECK(state.state == 3);
    check_float(0.75f, advance(osc, state, 600.0f, 20));
}

void test_end_and_jumps() {
    auto endOsc = oscillator({ { 0x0F, 0, 0 } });
    PCEnvelopeState state;
    check_float(0.0f, begin(endOsc, state, 600.0f));
    CHECK(!pc_envelope_is_active(&state));

    auto jumpOsc = oscillator({
        { 0x0D, 0, 2 }, { 0x0F, 0, 0 }, { 0, 0, 8192 }, { 0x0E, 0, 0 }
    });
    check_float(0.25f, begin(jumpOsc, state, 600.0f));
    CHECK(state.state == 3);

    auto negative = oscillator({ { 0x0D, 0, -1 } });
    begin(negative, state, 600.0f);
    CHECK(state.result == PCEnvelopeResult::InvalidTableIndex);
    CHECK(!pc_envelope_is_active(&state));

    auto outside = oscillator({ { 0x0D, 0, 1 } });
    begin(outside, state, 600.0f);
    CHECK(state.result == PCEnvelopeResult::InvalidTableIndex);

    auto cycle = oscillator({ { 0x0D, 0, 1 }, { 0x0D, 0, 0 } });
    pc_envelope_init_attack(&state, &cycle);
    pc_envelope_step(&cycle, &state, 600.0f, 5);
    CHECK(state.result == PCEnvelopeResult::OperationLimit);
    CHECK(!pc_envelope_is_active(&state));
}

void test_truncated_and_invalid_curve() {
    auto truncated = oscillator({ { 0, 2, 16384 } });
    PCEnvelopeState state;
    begin(truncated, state, 600.0f);
    advance(truncated, state, 600.0f, 2);
    CHECK(state.result == PCEnvelopeResult::TruncatedTable);
    CHECK(!pc_envelope_is_active(&state));

    auto invalid = oscillator({ { 3, 10, 16384 }, { 0x0F, 0, 0 } });
    begin(invalid, state, 600.0f);
    CHECK(state.result == PCEnvelopeResult::UnsupportedCurve);
    CHECK(!pc_envelope_is_active(&state));
}

void test_release_table_sizes() {
    auto run = [](size_t attackPadding, size_t releasePadding) {
        PCInstrumentOscillator osc;
        for (size_t i = 0; i < attackPadding; ++i) osc.attack.push_back({ 0, 0, 32767 });
        osc.attack.push_back({ 0x0E, 0, 0 });
        for (size_t i = 0; i < releasePadding; ++i) osc.release.push_back({ 0, 0, 16384 });
        osc.release.push_back({ 0, 4, 0 });
        osc.release.push_back({ 0x0F, 0, 0 });
        PCEnvelopeState state;
        begin(osc, state, 600.0f);
        pc_envelope_init_release(&state, &osc);
        const float releaseStart = releasePadding == 0 ? 32767.0f / 32768.0f : 0.5f;
        check_float(releaseStart, pc_envelope_step(&osc, &state, 600.0f));
        check_float(0.0f, advance(osc, state, 600.0f, 4));
        CHECK(state.result == PCEnvelopeResult::Ok);
        CHECK(!pc_envelope_is_active(&state));
    };
    run(4, 0); // release shorter than attack
    run(0, 4); // release longer than attack
}

void test_release_param_precedence() {
    auto osc = oscillator({ { 0, 0, 32767 }, { 0x0E, 0, 0 } });
    osc.release = { { 0, 1, 0 }, { 0x0F, 0, 0 } };
    PCEnvelopeState state;
    begin(osc, state, 600.0f);
    pc_envelope_init_release(&state, &osc, static_cast<u16>(0x4000 | 10));
    check_float((32767.0f / 32768.0f) * (32767.0f / 32768.0f),
                pc_envelope_step(&osc, &state, 600.0f));
    const float rawHalf = (32767.0f / 32768.0f) * 0.5f;
    check_float(rawHalf * rawHalf, advance(osc, state, 600.0f, 5));
    CHECK(state.state == 8);
    check_float(0.0f, advance(osc, state, 600.0f, 5));
    CHECK(!pc_envelope_is_active(&state));
}

void test_missing_release_defaults_to_0x10() {
    auto osc = oscillator({ { 0, 0, 32767 }, { 0x0E, 0, 0 } });
    PCEnvelopeState state;
    begin(osc, state, 600.0f);
    pc_envelope_init_release(&state, &osc);
    check_float(32767.0f / 32768.0f, pc_envelope_step(&osc, &state, 600.0f));
    check_float((32767.0f / 32768.0f) * 0.5f,
                advance(osc, state, 600.0f, 8));
    check_float(0.0f, advance(osc, state, 600.0f, 8));
    CHECK(!pc_envelope_is_active(&state));
}

void test_forced_release() {
    auto osc = oscillator({ { 0, 0, 32767 }, { 0x0E, 0, 0 } });
    osc.rate = 4.0f; // State 7 deliberately uses rate 1.
    PCEnvelopeState state;
    begin(osc, state, 600.0f);
    pc_envelope_init_forced_release(&state);
    check_float(32767.0f / 32768.0f, pc_envelope_step(&osc, &state, 600.0f));
    check_float(0.0f, advance(osc, state, 600.0f, 5));
    CHECK(!pc_envelope_is_active(&state));
    CHECK(state.result == PCEnvelopeResult::Ok);
}

void test_invalid_rates() {
    for (float rate : { 0.0f, -1.0f }) {
        auto osc = oscillator({ { 0, 10, 32767 }, { 0x0F, 0, 0 } });
        osc.rate = rate;
        PCEnvelopeState state;
        pc_envelope_init_attack(&state, &osc);
        CHECK(state.result == PCEnvelopeResult::InvalidRate);
        CHECK(!pc_envelope_is_active(&state));
    }
    auto osc = oscillator({ { 0, 10, 32767 }, { 0x0F, 0, 0 } });
    PCEnvelopeState state;
    pc_envelope_init_attack(&state, &osc);
    pc_envelope_step(&osc, &state, 0.0f);
    CHECK(state.result == PCEnvelopeResult::InvalidSampleRate);
}

} // namespace

int main() {
    test_curves_exact();
    test_timing_rate_and_sample_rate();
    test_width_vertex_and_current_value();
    test_end_and_jumps();
    test_truncated_and_invalid_curve();
    test_release_table_sizes();
    test_release_param_precedence();
    test_missing_release_defaults_to_0x10();
    test_forced_release();
    test_invalid_rates();
    std::printf("pc_envelope_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
