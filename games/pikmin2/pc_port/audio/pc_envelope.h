#ifndef PC_ENVELOPE_H
#define PC_ENVELOPE_H

#include "audio/pc_instrument_bank.h"

#include <cstddef>

enum class PCEnvelopeResult : u8 {
    Ok = 0,
    InvalidArgument,
    InvalidRate,
    InvalidSampleRate,
    InvalidTableIndex,
    TruncatedTable,
    UnsupportedCurve,
    OperationLimit,
};

struct PCEnvelopeState {
    u8 state = 0;
    u8 curveType = 0;
    size_t tableIndex = 0;
    double framesRemaining = 0.0;
    float value = 0.0f;
    float deltaPerFrame = 0.0f;
    float targetValue = 0.0f;
    u16 releaseParam = 0;
    PCEnvelopeResult result = PCEnvelopeResult::Ok;
};

// One call advances one output sample frame. The first call selects the first
// segment and returns its initial value. State retains no table pointers.
float pc_envelope_step(const PCInstrumentOscillator* osc, PCEnvelopeState* state,
                       float sampleRate, size_t maxInstantOperations = 16);

void pc_envelope_init_attack(PCEnvelopeState* state,
                             const PCInstrumentOscillator* osc);

void pc_envelope_init_release(PCEnvelopeState* state,
                              const PCInstrumentOscillator* osc,
                              u16 releaseParam = 0);
void pc_envelope_init_forced_release(PCEnvelopeState* state);

bool pc_envelope_is_active(const PCEnvelopeState* state);

float pc_envelope_get_value(const PCEnvelopeState* state, const PCInstrumentOscillator* osc);

#endif
