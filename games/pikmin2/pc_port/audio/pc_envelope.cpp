#include "audio/pc_envelope.h"

#include <algorithm>
#include <cmath>

namespace {

struct EnvelopeTableView {
    const PCEnvelopePoint* data;
    size_t size;
};

constexpr PCEnvelopePoint kForcedReleaseTable[] = {
    { 0, 5, 0 },
    { 0x0F, 0, 0 },
};

bool supported_curve(s16 curve) { return curve >= 0 && curve <= 2; }

float curve_apply(u8 curve, float value) {
    if (curve == 1) return std::copysign(value * value, value);
    if (curve == 2) return std::copysign(std::sqrt(std::abs(value)), value);
    return value;
}

float output_value(const PCEnvelopeState& state,
                   const PCInstrumentOscillator& osc) {
    return curve_apply(state.curveType, state.value) * osc.width + osc.vertex;
}

float fail(PCEnvelopeState& state, const PCInstrumentOscillator& osc,
           PCEnvelopeResult result) {
    state.state = 0;
    state.framesRemaining = 0.0;
    state.deltaPerFrame = 0.0f;
    state.result = result;
    return output_value(state, osc);
}

EnvelopeTableView active_table(const PCInstrumentOscillator& osc, u8 state) {
    if (state == 5) return { osc.release.data(), osc.release.size() };
    if (state == 7) {
        return { kForcedReleaseTable,
                 sizeof(kForcedReleaseTable) / sizeof(kForcedReleaseTable[0]) };
    }
    return { osc.attack.data(), osc.attack.size() };
}

} // namespace

void pc_envelope_init_attack(PCEnvelopeState* state,
                             const PCInstrumentOscillator* osc) {
    if (!state) return;
    *state = {};
    if (!osc) {
        state->result = PCEnvelopeResult::InvalidArgument;
    } else if (!std::isfinite(osc->rate) || osc->rate <= 0.0f) {
        state->result = PCEnvelopeResult::InvalidRate;
    } else if (osc->attack.empty()) {
        state->value = 1.0f;
    } else {
        state->state = 1;
    }
}

void pc_envelope_init_release(PCEnvelopeState* state,
                              const PCInstrumentOscillator* osc,
                              u16 releaseParam) {
    if (!state) return;
    if (!osc) {
        state->state = 0;
        state->result = PCEnvelopeResult::InvalidArgument;
        return;
    }
    state->state = 4;
    state->releaseParam = releaseParam;
    state->result = PCEnvelopeResult::Ok;
}

void pc_envelope_init_forced_release(PCEnvelopeState* state) {
    if (!state) return;
    state->state = 6;
    state->releaseParam = 0;
    state->result = PCEnvelopeResult::Ok;
}

bool pc_envelope_is_active(const PCEnvelopeState* state) {
    return state && state->state != 0;
}

float pc_envelope_get_value(const PCEnvelopeState* state, const PCInstrumentOscillator* osc) {
    if (!state || !osc) return 1.0f;
    return output_value(*state, *osc);
}

float pc_envelope_step(const PCInstrumentOscillator* osc, PCEnvelopeState* state,
                       float sampleRate, size_t maxInstantOperations) {
    if (!osc || !state) return 1.0f;
    if (!std::isfinite(osc->rate) || osc->rate <= 0.0f)
        return fail(*state, *osc, PCEnvelopeResult::InvalidRate);
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0f)
        return fail(*state, *osc, PCEnvelopeResult::InvalidSampleRate);
    if (state->state == 0 || state->state == 3) return output_value(*state, *osc);
    if (maxInstantOperations == 0)
        return fail(*state, *osc, PCEnvelopeResult::OperationLimit);

    bool startedTimedRelease = false;
    if (state->state == 4) {
        if (state->releaseParam == 0 && osc->release.empty()) state->releaseParam = 0x10;
        state->tableIndex = 0;
        state->framesRemaining = 0.0;
        state->targetValue = state->value;
        if (state->releaseParam != 0) {
            state->state = 8;
            state->curveType = static_cast<u8>((state->releaseParam >> 14) & 3);
            if (!supported_curve(state->curveType))
                return fail(*state, *osc, PCEnvelopeResult::UnsupportedCurve);
            const double duration = std::max(
                1.0, (state->releaseParam & 0x3FFF) * sampleRate / 600.0);
            state->framesRemaining = duration;
            state->targetValue = 0.0f;
            state->deltaPerFrame = -state->value / static_cast<float>(duration);
            startedTimedRelease = true;
        } else {
            state->state = 5;
        }
    } else if (state->state == 6) {
        state->state = 7;
        state->tableIndex = 0;
        state->framesRemaining = 0.0;
        state->targetValue = state->value;
    } else if (state->state == 1) {
        state->state = 2;
        state->tableIndex = 0;
        state->framesRemaining = 0.0;
        state->value = 0.0f;
        state->targetValue = 0.0f;
        state->releaseParam = 0;
    }

    if (startedTimedRelease) return output_value(*state, *osc);

    if (state->framesRemaining > 0.0) {
        state->framesRemaining = std::max(0.0, state->framesRemaining - 1.0);
        state->value = state->framesRemaining == 0.0
                     ? state->targetValue
                     : state->targetValue - state->deltaPerFrame
                                              * static_cast<float>(state->framesRemaining);
        if (state->framesRemaining > 0.0) return output_value(*state, *osc);
        if (state->state == 8) {
            state->state = 0;
            return output_value(*state, *osc);
        }
    }

    size_t operations = 0;
    while (true) {
        if (++operations > maxInstantOperations)
            return fail(*state, *osc, PCEnvelopeResult::OperationLimit);
        const EnvelopeTableView table = active_table(*osc, state->state);
        if (!table.data || table.size == 0 || state->tableIndex >= table.size)
            return fail(*state, *osc, PCEnvelopeResult::TruncatedTable);
        const PCEnvelopePoint& point = table.data[state->tableIndex];
        if (point.curve == 0x0D) {
            if (point.value < 0 || static_cast<size_t>(point.value) >= table.size)
                return fail(*state, *osc, PCEnvelopeResult::InvalidTableIndex);
            state->tableIndex = static_cast<size_t>(point.value);
            continue;
        }
        if (point.curve == 0x0F) {
            state->state = 0;
            return output_value(*state, *osc);
        }
        if (point.curve == 0x0E) {
            state->state = 3;
            return output_value(*state, *osc);
        }
        if (!supported_curve(point.curve))
            return fail(*state, *osc, PCEnvelopeResult::UnsupportedCurve);
        state->curveType = static_cast<u8>(point.curve);
        state->targetValue = static_cast<float>(point.value) / 32768.0f;
        ++state->tableIndex;
        if (point.time == 0) {
            state->value = state->targetValue;
            continue;
        }
        const double duration = static_cast<u16>(point.time) * sampleRate
                              / (600.0 * (state->state == 7 ? 1.0 : osc->rate));
        if (!std::isfinite(duration) || duration <= 0.0)
            return fail(*state, *osc, PCEnvelopeResult::InvalidRate);
        state->framesRemaining = duration;
        state->deltaPerFrame = (state->targetValue - state->value)
                             / static_cast<float>(duration);
        return output_value(*state, *osc);
    }
}
