#ifndef PC_AST_STREAM_H
#define PC_AST_STREAM_H

#include <stddef.h>
#include <stdint.h>

// Reproductor host de streams AST (ver pc_ast_stream.cpp).
#ifdef __cplusplus
extern "C" {
#endif

bool pc_ast_load(int32_t entryNum);
void pc_ast_play(void);
void pc_ast_stop(void);
void pc_ast_pause(bool pause);
void pc_ast_set_gain(float left, float right);
bool pc_ast_finished(void);
// Suma el stream sobre `frames` tramas estereo S16 a `outRate` Hz.
void pc_ast_mix(int16_t* pcm, size_t frames, int outRate);

#ifdef __cplusplus
}
#endif

#endif
