/**
 * @file jaudio_hw_stubs.cpp
 * @brief Hardware symbols the original audio engine references but that this
 *        port has no silicon for.
 *
 * Only compiled when PIKMIN_NATIVE_JAUDIO is on. Activating src/jaudio pulls in
 * exactly nine symbols that live in directories the port never builds:
 * src/dsp/ (the DSP register block), src/os/ (interrupt masking) and
 * src/hvqm4dec/ (the video decoder). Nothing else was missing, and no symbol
 * collided, so this file is the whole hardware boundary.
 *
 * The DSP register and interrupt stubs are inert by design, not by omission:
 * pc_dsp_host renders the voice parameter blocks on the CPU and deliberately
 * produces no mailbox traffic, so a real register file would have no reader.
 */

#include "types.h"

#if PIKI_USE_JAUDIO

#include "Dolphin/OS/OSInterrupt.h"
#include "hvqm4.h"

/**
 * DSP hardware register file. audiothread.c and dspboot.c poll and write these
 * during boot and sync. Backing them with plain memory lets that code run
 * unchanged; the values it reads back are the ones it last wrote, which is
 * enough because the host renderer never waits on the DSP to answer.
 */
vu16 __DSPRegs[32];

extern "C" {

/**
 * @brief Accepts an interrupt unmask request and reports nothing was masked.
 *
 * DSPInit2 and DspBoot unmask the DSP interrupt during boot. There is no
 * interrupt controller here and no handler to arm, so the request is a no-op.
 */
OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask)
{
	(void)mask;
	return 0;
}

} // extern "C"

///////////////////////////////////////////////////////////////////////////////
// HVQM4 video decoder.
//
// Referenced only by src/jaudio/hvqm_play.c, which drives video alongside the
// audio stream. The port excludes src/hvqm4dec/ entirely and plays cinematics
// through the .stx audio path instead, so these keep the engine linking
// without pretending to decode anything.
//
// HVQM4BuffSize reports zero so a caller that sizes an allocation from it asks
// for nothing rather than for an arbitrary block this file invented.

void HVQM4InitDecoder() { }

void HVQM4InitSeqObj(SeqObj* obj, VideoInfo* header)
{
	(void)obj;
	(void)header;
}

u32 HVQM4BuffSize(SeqObj* obj)
{
	(void)obj;
	return 0;
}

void HVQM4SetBuffer(SeqObj* obj, void* buf)
{
	(void)obj;
	(void)buf;
}

void HVQM4DecodeIpic(SeqObj* obj, void* code, void* outbuf)
{
	(void)obj;
	(void)code;
	(void)outbuf;
}

void HVQM4DecodePpic(SeqObj* obj, void* code, void* outbuf, void* ref1)
{
	(void)obj;
	(void)code;
	(void)outbuf;
	(void)ref1;
}

void HVQM4DecodeBpic(SeqObj* obj, void* code, void* outbuf, void* ref2,
                     void* ref1)
{
	(void)obj;
	(void)code;
	(void)outbuf;
	(void)ref2;
	(void)ref1;
}

#endif // PIKI_USE_JAUDIO
