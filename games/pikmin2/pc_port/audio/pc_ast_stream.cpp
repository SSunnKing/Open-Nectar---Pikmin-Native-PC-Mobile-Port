// Reproductor host de streams AST (musica en stream de JAudio: titulo,
// seleccion, demos...). El JASAramStream original sube bloques a ARAM y los
// reproduce con canales DSP; en el port se decodifica el fichero completo a
// PCM estereo y se mezcla en la salida de jaudio_sink.
//
// Formato AST (big-endian):
//   0x00 'STRM' | 0x08 u16 formato (0 = ADPCM AFC, 1 = PCM16) | 0x0C u16 canales
//   0x0E u16 bucle | 0x10 u32 frecuencia | 0x14 u32 muestras
//   0x18 u32 inicio bucle | 0x1C u32 fin bucle | cabecera de 0x40 bytes
//   Bloques: 'BLCK' u32 tamano-por-canal, cabecera 0x20, luego un tramo por canal.
#include "pc_ast_stream.h"
#include "types.h"
#include "Dolphin/dvd.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

std::mutex sMutex;
// Todo con malloc/free: el new/delete global del port va al heap del juego.
s16* sPcm = nullptr; // estereo intercalado
u32 sRate      = 32000;
u32 sFrames    = 0;
u32 sLoopStart = 0;
u32 sLoopEnd   = 0;
bool sLoop     = false;
double sCursor = 0.0;
bool sPlaying  = false;
bool sPaused   = false;
float sGainL   = 1.0f;
float sGainR   = 1.0f;
std::atomic<bool> sFinished { false };

bool traceEnabled()
{
	static const bool on = std::getenv("PIKMIN_AUDIO_LOG") != nullptr;
	return on;
}

u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }
u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }

s16 clamp16(s32 v) { return s16(std::clamp(v, -32768, 32767)); }

// Coeficientes fijos del ADPCM AFC.
const s16 kAfcCoef[16][2] = {
	{ 0, 0 },          { 0x0800, 0 },     { 0, 0x0800 },     { 0x0400, 0x0400 }, { 0x1000, -0x0800 }, { 0x0E00, -0x0600 },
	{ 0x0C00, -0x0400 }, { 0x1200, -0x0A00 }, { 0x1068, -0x08C8 }, { 0x12C0, -0x08FC }, { 0x1400, -0x0C00 }, { 0x0800, -0x0800 },
	{ 0x0400, -0x0400 }, { -0x0400, 0x0400 }, { -0x0400, 0 },    { -0x0800, 0 },
};

// Decodifica `frames9` tramas AFC de 9 bytes (16 muestras cada una).
void decodeAfc(const u8* src, u32 frames9, s16& hist1, s16& hist2, s16* out, u32& n)
{
	for (u32 f = 0; f < frames9; f++, src += 9) {
		const s32 scale = 1 << (src[0] >> 4);
		const u32 idx   = src[0] & 0xF;
		for (int k = 0; k < 16; k++) {
			s32 nib = (k & 1) ? (src[1 + k / 2] & 0xF) : (src[1 + k / 2] >> 4);
			if (nib >= 8)
				nib -= 16;
			s32 s = (nib * scale) << 11;
			s += kAfcCoef[idx][0] * hist1 + kAfcCoef[idx][1] * hist2;
			const s16 v = clamp16(s >> 11);
			hist2       = hist1;
			hist1       = v;
			out[n++] = v;
		}
	}
}

bool decodeAst(const u8* data, u32 size)
{
	if (size < 0x40 || std::memcmp(data, "STRM", 4) != 0)
		return false;
	const u16 format   = be16(data + 0x08);
	const u16 channels = be16(data + 0x0C);
	const bool loop    = be16(data + 0x0E) != 0;
	const u32 rate     = be32(data + 0x10);
	const u32 samples  = be32(data + 0x14);
	const u32 lStart   = be32(data + 0x18);
	const u32 lEnd     = be32(data + 0x1C);
	if (channels == 0 || channels > 6 || format > 1 || rate == 0)
		return false;

	// Primera pasada: cuenta muestras por canal para reservar de una vez.
	u32 total = 0;
	for (u32 off = 0x40; off + 0x20 <= size && std::memcmp(data + off, "BLCK", 4) == 0;) {
		const u32 blk = be32(data + off + 4);
		off += 0x20;
		if (off + blk * channels > size)
			break;
		total += (format == 0) ? (blk / 9) * 16 : blk / 2;
		off += blk * channels;
	}
	if (total == 0)
		return false;

	s16* ch[2]  = { static_cast<s16*>(std::malloc(total * sizeof(s16))), nullptr };
	ch[1]       = channels > 1 ? static_cast<s16*>(std::malloc(total * sizeof(s16))) : ch[0];
	if (!ch[0] || !ch[1]) {
		if (ch[1] != ch[0])
			std::free(ch[1]);
		std::free(ch[0]);
		return false;
	}
	s16 h1[2]   = { 0, 0 }, h2[2] = { 0, 0 };
	u32 n[2]    = { 0, 0 };
	const u16 used = channels > 1 ? 2 : 1; // solo L/R
	for (u32 off = 0x40; off + 0x20 <= size && std::memcmp(data + off, "BLCK", 4) == 0;) {
		const u32 blk = be32(data + off + 4);
		off += 0x20;
		if (off + blk * channels > size)
			break;
		for (u16 c = 0; c < used; c++) {
			const u8* src = data + off + blk * c;
			if (format == 0) {
				decodeAfc(src, blk / 9, h1[c], h2[c], ch[c], n[c]);
			} else {
				for (u32 i = 0; i + 1 < blk; i += 2)
					ch[c][n[c]++] = s16(be16(src + i));
			}
		}
		off += blk * channels;
	}

	u32 frames = n[0];
	if (samples && samples < frames)
		frames = samples;
	s16* pcm = static_cast<s16*>(std::malloc(size_t(frames) * 2 * sizeof(s16)));
	for (u32 i = 0; pcm && i < frames; i++) {
		pcm[i * 2 + 0] = ch[0][i];
		pcm[i * 2 + 1] = ch[1][i];
	}
	if (ch[1] != ch[0])
		std::free(ch[1]);
	std::free(ch[0]);
	if (!pcm)
		return false;

	std::lock_guard<std::mutex> lock(sMutex);
	std::free(sPcm);
	sPcm       = pcm;
	sRate      = rate;
	sFrames    = frames;
	sLoop      = loop && lEnd > lStart && lStart < frames;
	sLoopStart = lStart;
	sLoopEnd   = std::min(lEnd, frames);
	sCursor    = 0.0;
	sPlaying   = false;
	sPaused    = false;
	sFinished  = false;
	if (traceEnabled())
		std::fprintf(stderr, "[PC AST] fmt=%u ch=%u rate=%u frames=%u loop=%d [%u..%u]\n", format, channels, rate, frames, (int)sLoop,
		             sLoopStart, sLoopEnd);
	return frames != 0;
}

} // namespace

extern "C" {

bool pc_ast_load(s32 entryNum)
{
	pc_ast_stop();
	DVDFileInfo info;
	if (!DVDFastOpen(entryNum, &info))
		return false;
	u8* buf       = info.length ? static_cast<u8*>(std::malloc(info.length)) : nullptr;
	const bool ok = buf && DVDReadPrio(&info, buf, (s32)info.length, 0, 2) == (s32)info.length;
	DVDClose(&info);
	const bool decoded = ok && decodeAst(buf, info.length);
	std::free(buf);
	if (!decoded) {
		std::fprintf(stderr, "[PC AST] no se pudo cargar el stream (entry %d)\n", (int)entryNum);
		return false;
	}
	return true;
}

void pc_ast_play()
{
	std::lock_guard<std::mutex> lock(sMutex);
	if (sFrames == 0)
		return;
	sPlaying  = true;
	sPaused   = false;
	sFinished = false;
}

void pc_ast_stop()
{
	std::lock_guard<std::mutex> lock(sMutex);
	sPlaying = false;
	sPaused  = false;
	sCursor  = 0.0;
	std::free(sPcm);
	sPcm = nullptr;
	sFrames = 0;
}

void pc_ast_pause(bool pause)
{
	std::lock_guard<std::mutex> lock(sMutex);
	sPaused = pause;
}

void pc_ast_set_gain(float left, float right)
{
	std::lock_guard<std::mutex> lock(sMutex);
	sGainL = std::clamp(left, 0.0f, 2.0f);
	sGainR = std::clamp(right, 0.0f, 2.0f);
}

bool pc_ast_finished() { return sFinished.load(); }

void pc_ast_mix(int16_t* pcm, size_t frames, int outRate)
{
	std::lock_guard<std::mutex> lock(sMutex);
	if (!sPlaying || sPaused || sFrames == 0 || outRate <= 0)
		return;
	const double step = double(sRate) / double(outRate);
	const u32 end     = sLoop ? sLoopEnd : sFrames;
	for (size_t i = 0; i < frames; i++) {
		u32 pos = u32(sCursor);
		if (pos >= end) {
			if (!sLoop) {
				sPlaying  = false;
				sFinished = true;
				return;
			}
			sCursor -= double(sLoopEnd - sLoopStart);
			pos = u32(sCursor);
		}
		const s32 l   = s32(sPcm[size_t(pos) * 2 + 0] * sGainL);
		const s32 r   = s32(sPcm[size_t(pos) * 2 + 1] * sGainR);
		pcm[i * 2 + 0] = clamp16(pcm[i * 2 + 0] + l);
		pcm[i * 2 + 1] = clamp16(pcm[i * 2 + 1] + r);
		sCursor += step;
	}
}

} // extern "C"
