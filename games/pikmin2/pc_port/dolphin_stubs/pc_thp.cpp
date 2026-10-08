// THP movie player for the host (replaces the Dolphin SDK THPPlayer library).
//
// The console decodes each THP frame with the SDK's PowerPC/locked-cache JPEG
// decoder (src/Dolphin/thp/THPDec.c) into YUV tiles and converts them with a
// four-stage TEV. Here the frame is decoded on the CPU into RGBA:
//   - video: THP JPEG is baseline JPEG with the 0x00 stuffing byte after 0xFF
//     *omitted* inside the entropy-coded scan. Re-insert it and hand the
//     picture to stb_image (compiled once in gl/pc_texpack.cpp).
//   - audio: THP ADPCM (4-bit nibbles, 8 coefficient pairs per channel in
//     every frame header), decoded with the SDK algorithm and queued to a
//     private SDL audio device: the port has no mixed audio output yet.
// Timing follows the wall clock while playing (pause-aware); the audio of
// every frame that elapsed is queued, so skipped pictures do not skip sound.
//
// The public entry points keep the SDK contract used by
// sysGCU/pikmin2THPPlayer.cpp: ActivePlayer.mState == 3 means "finished".

#include "types.h"
#include "Dolphin/gx.h"
#include "Dolphin/GX/GXFifo.h"
#include "Dolphin/mtx.h"
#include "THP/THPPlayer.h"
#include "gl/pc_gfx.h"
#include "PikiMallocAllocator.h"

#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "stb_image.h"

namespace {

// operator new goes through the current JKR heap, which the game frees
// between movies: every host container here must live in malloc memory.
template <typename T>
using HostVector = std::vector<T, PikiMallocAllocator<T>>;

enum { THP_STOPPED = 0, THP_PLAYING = 1, THP_PAUSED = 2, THP_PLAYED = 3 };

inline u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
inline u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }
inline f32 bef32(const u8* p)
{
	u32 v = be32(p);
	f32 f;
	memcpy(&f, &v, 4);
	return f;
}

struct Player {
	FILE* file = nullptr;
	u32 numFrames = 0;
	f32 fps = 30.0f;
	u32 firstFrameOffset = 0;
	u32 firstFrameSize = 0;
	u32 videoW = 0, videoH = 0;
	bool hasAudio = false;
	u32 audioChannels = 0, audioRate = 0, audioTracks = 0;
	int audioTrack = 0;
	bool loop = false;

	// Reading cursor.
	u32 nextOffset = 0;  // file offset of the next frame to read
	u32 nextSize = 0;    // its total size
	u32 nextIndex = 0;   // its frame number
	HostVector<u8> frameBuf;
	HostVector<u8> jpegBuf;
	HostVector<s16> pcmBuf;

	// Picture shown by THPPlayerDrawCurrentFrame.
	HostVector<u8> rgba;
	int rgbaW = 0, rgbaH = 0;
	s32 shownFrame = -1;
	GXTexObj texObj;

	// Clock.
	Uint32 playStartTicks = 0;
	Uint32 pausedAtTicks = 0;
	double baseFrame = 0.0; // frame position when playStartTicks was taken

	// Audio.
	SDL_AudioDeviceID audioDev = 0;
	f32 volume = 1.0f;
	u32 queuedFrames = 0; // movie frames pushed to the device
};

Player sPlayer;

void close_file()
{
	if (sPlayer.file) {
		fclose(sPlayer.file);
		sPlayer.file = nullptr;
	}
}

// ── Audio mezclado en la salida del juego ──────────────────────────────────
// Android solo deja abrir un dispositivo de audio, y es del juego: entonces el
// vídeo deja su audio aquí y jaudio_sink lo suma (pc_thp_mix), como las pistas
// AST. Anillo fijo de PCM estéreo s16, sin memoria del heap del juego.
constexpr u32 kMixCapacity = 32000 * 2 * 8; // 8 s a 32 kHz
s16 sMixRing[kMixCapacity];
u32 sMixRead = 0, sMixCount = 0;
u32 sMixRate = 32000;
double sMixPhase = 0.0;
bool sMixMode = false, sMixPaused = true;
std::mutex sMixMutex;

void mix_clear()
{
	std::lock_guard<std::mutex> lock(sMixMutex);
	sMixRead = sMixCount = 0;
	sMixPhase = 0.0;
}

void mix_push(const s16* pcm, u32 samples)
{
	std::lock_guard<std::mutex> lock(sMixMutex);
	for (u32 i = 0; i < samples; i++) {
		if (sMixCount == kMixCapacity) { // lleno: se descarta lo más viejo
			sMixRead = (sMixRead + 1) % kMixCapacity;
			sMixCount--;
		}
		sMixRing[(sMixRead + sMixCount) % kMixCapacity] = pcm[i];
		sMixCount++;
	}
}

bool mix_empty()
{
	std::lock_guard<std::mutex> lock(sMixMutex);
	return sMixCount < 2;
}

void audio_close()
{
	if (sPlayer.audioDev) {
		SDL_CloseAudioDevice(sPlayer.audioDev);
		sPlayer.audioDev = 0;
	}
	sMixMode   = false;
	sMixPaused = true;
	mix_clear();
}

bool audio_open()
{
	if (sPlayer.audioDev || sMixMode || !sPlayer.hasAudio || sPlayer.audioRate == 0) return sPlayer.audioDev != 0 || sMixMode;
	if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		printf("[PC THP] SDL audio init failed: %s\n", SDL_GetError());
		return false;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq     = int(sPlayer.audioRate);
	want.format   = AUDIO_S16SYS;
	want.channels = 2;
	want.samples  = 1024;
	sPlayer.audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (!sPlayer.audioDev) {
		// Android: el dispositivo es del juego; se mezcla en su salida.
		printf("[PC THP] own audio device unavailable (%s): mixing into the game output\n", SDL_GetError());
		sMixMode = true;
		sMixRate = sPlayer.audioRate;
		mix_clear();
		return true;
	}
	return true;
}

// ── ADPCM ─────────────────────────────────────────────────────────────────
struct AdpcmState {
	const u8* data;
	u32 nibble;
	int predictor, scale;
	s16 yn1, yn2;
};

inline int adpcm_next_sample(AdpcmState& st)
{
	if (!(st.nibble & 0xF)) {
		st.predictor = (*st.data & 0x70) >> 4;
		st.scale     = *st.data & 0xF;
		st.data++;
		st.nibble += 2;
	}
	int s;
	if (st.nibble & 1) {
		s = int(s8(u8(*st.data << 4))) >> 4;
		st.data++;
	} else {
		s = int(s8(*st.data & 0xF0)) >> 4;
	}
	st.nibble++;
	return s;
}

void adpcm_decode_channel(const u8* src, u32 samples, const s16 coef[8][2], s16 yn1, s16 yn2, s16* dst, int step)
{
	AdpcmState st;
	st.data      = src;
	st.nibble    = 2;
	st.predictor = (*src & 0x70) >> 4;
	st.scale     = *src & 0xF;
	st.data++;
	for (u32 i = 0; i < samples; i++) {
		int sample = adpcm_next_sample(st);
		s64 yn     = s64(coef[st.predictor][1]) * yn2 + s64(coef[st.predictor][0]) * yn1;
		yn += s64(sample << st.scale) << 11;
		yn <<= 5;
		yn += 0x8000;
		yn        = std::clamp<s64>(yn, INT32_MIN, INT32_MAX);
		s16 out   = s16(yn >> 16);
		*dst      = out;
		dst += step;
		yn2 = yn1;
		yn1 = out;
	}
}

// Decodes one THP audio record into interleaved stereo s16.
u32 decode_audio(const u8* rec, u32 recSize, HostVector<s16>& out)
{
	if (recSize < 0x50) return 0;
	const u32 offsetNextChannel = be32(rec);
	const u32 sampleSize        = be32(rec + 4);
	s16 lCoef[8][2], rCoef[8][2];
	for (int i = 0; i < 8; i++) {
		lCoef[i][0] = s16(be16(rec + 8 + i * 4));
		lCoef[i][1] = s16(be16(rec + 10 + i * 4));
		rCoef[i][0] = s16(be16(rec + 40 + i * 4));
		rCoef[i][1] = s16(be16(rec + 42 + i * 4));
	}
	const s16 lYn1 = s16(be16(rec + 72)), lYn2 = s16(be16(rec + 74));
	const s16 rYn1 = s16(be16(rec + 76)), rYn2 = s16(be16(rec + 78));
	const u8* left  = rec + 80;
	const u8* right = left + offsetNextChannel;
	if (sampleSize == 0 || sampleSize > 0x10000) return 0;
	out.resize(size_t(sampleSize) * 2);
	if (offsetNextChannel == 0) {
		adpcm_decode_channel(left, sampleSize, lCoef, lYn1, lYn2, out.data(), 2);
		for (u32 i = 0; i < sampleSize; i++) out[i * 2 + 1] = out[i * 2];
	} else {
		adpcm_decode_channel(left, sampleSize, lCoef, lYn1, lYn2, out.data(), 2);
		adpcm_decode_channel(right, sampleSize, rCoef, rYn1, rYn2, out.data() + 1, 2);
	}
	return sampleSize;
}

// ── JPEG ──────────────────────────────────────────────────────────────────
// Rebuilds a standard JPEG: THP omits the 0x00 stuffing byte after 0xFF in
// the entropy-coded segment (its decoder treats every 0xFF there as data).
bool restuff_jpeg(const u8* src, u32 size, HostVector<u8>& out)
{
	out.clear();
	out.reserve(size + size / 32);
	u32 i = 0;
	// Copy headers verbatim until SOS (0xFFDA) + its length.
	while (i + 1 < size) {
		if (src[i] != 0xFF) return false;
		u8 marker = src[i + 1];
		if (marker == 0xD8) { // SOI
			out.push_back(0xFF); out.push_back(0xD8);
			i += 2;
			continue;
		}
		if (i + 3 >= size) return false;
		u32 len = be16(src + i + 2);
		if (i + 2 + len > size) return false;
		out.insert(out.end(), src + i, src + i + 2 + len);
		i += 2 + len;
		if (marker == 0xDA) break;
	}
	// Scan data: the THP bit reader consumes raw words, so every byte here
	// is entropy data (0xFF included, no restart markers); only the EOI that
	// closes the frame (before zero padding) is a marker. Stuff every 0xFF.
	u32 end = size;
	while (end > i && src[end - 1] == 0x00) end--;
	const bool hasEoi = end >= i + 2 && src[end - 2] == 0xFF && src[end - 1] == 0xD9;
	if (hasEoi) end -= 2;
	for (; i < end; i++) {
		out.push_back(src[i]);
		if (src[i] == 0xFF) out.push_back(0x00);
	}
	out.push_back(0xFF);
	out.push_back(0xD9);
	return true;
}

bool decode_video(const u8* jpeg, u32 size)
{
	if (!restuff_jpeg(jpeg, size, sPlayer.jpegBuf)) return false;
	int w = 0, h = 0, comp = 0;
	u8* pixels = stbi_load_from_memory(sPlayer.jpegBuf.data(), int(sPlayer.jpegBuf.size()), &w, &h, &comp, 4);
	if (!pixels) {
		static int logged = 0;
		if (logged++ < 4) printf("[PC THP] JPEG decode failed: %s\n", stbi_failure_reason());
		return false;
	}
	sPlayer.rgba.assign(pixels, pixels + size_t(w) * h * 4);
	sPlayer.rgbaW = w;
	sPlayer.rgbaH = h;
	stbi_image_free(pixels);
	return true;
}

// ── Frame stream ──────────────────────────────────────────────────────────
void rewind_stream()
{
	sPlayer.nextOffset = sPlayer.firstFrameOffset;
	sPlayer.nextSize   = sPlayer.firstFrameSize;
	sPlayer.nextIndex  = 0;
}

// Reads the next frame; decodes its audio (queued) and, if wanted, its video.
bool step_frame(bool wantVideo)
{
	Player& p = sPlayer;
	if (!p.file || p.nextIndex >= p.numFrames) return false;
	if (p.nextSize < 16 || p.nextSize > (64u << 20)) return false;
	p.frameBuf.resize(p.nextSize);
	if (fseek(p.file, long(p.nextOffset), SEEK_SET) != 0
	    || fread(p.frameBuf.data(), 1, p.nextSize, p.file) != p.nextSize) {
		printf("[PC THP] read error at frame %u\n", p.nextIndex);
		return false;
	}
	const u8* f          = p.frameBuf.data();
	const u32 nextTotal  = be32(f);
	const u32 imageSize  = be32(f + 8);
	const u32 audioSize  = p.hasAudio ? be32(f + 12) : 0;
	const u32 headerSize = 8 + 4 * (p.hasAudio ? 2 : 1);
	const u8* image      = f + headerSize;
	if (headerSize + imageSize + audioSize > p.nextSize) {
		printf("[PC THP] frame %u: bad sizes (%u/%u/%u)\n", p.nextIndex, headerSize, imageSize, audioSize);
		return false;
	}
	if (wantVideo && decode_video(image, imageSize)) p.shownFrame = s32(p.nextIndex);

	if (p.hasAudio && audioSize && (p.audioDev || sMixMode)) {
		const u8* audio = image + imageSize;
		// Every game movie carries a single track, so the record starts the block.
		const u8* rec = audio;
		u32 remaining = audioSize;
		u32 n = decode_audio(rec, remaining, p.pcmBuf);
		if (n) {
			// Apply volume on the CPU: SDL_QueueAudio has no gain.
			if (p.volume < 0.999f) {
				for (s16& s : p.pcmBuf) s = s16(s * p.volume);
			}
			if (p.audioDev) SDL_QueueAudio(p.audioDev, p.pcmBuf.data(), Uint32(p.pcmBuf.size() * sizeof(s16)));
			else mix_push(p.pcmBuf.data(), u32(p.pcmBuf.size()));
			p.queuedFrames++;
			// PIKMIN_THP_PCM=<file>: raw s16le stereo dump of the decoded audio.
			static FILE* pcmDump = getenv("PIKMIN_THP_PCM") ? fopen(getenv("PIKMIN_THP_PCM"), "wb") : nullptr;
			if (pcmDump) fwrite(p.pcmBuf.data(), sizeof(s16), p.pcmBuf.size(), pcmDump);
		}
	}

	p.nextOffset += p.nextSize;
	p.nextSize = nextTotal;
	p.nextIndex++;
	return true;
}

double clock_frame()
{
	Player& p = sPlayer;
	Uint32 now = ActivePlayer.mState == THP_PAUSED ? p.pausedAtTicks : SDL_GetTicks();
	double elapsed = double(now - p.playStartTicks) / 1000.0;
	return p.baseFrame + elapsed * p.fps;
}

} // namespace

extern "C" {

THPPlayer ActivePlayer;
u8 gTHPReaderDvdAccess;

BOOL THPPlayerInit(int)
{
	memset(&ActivePlayer, 0, sizeof(ActivePlayer));
	return TRUE;
}

void THPPlayerQuit()
{
	THPPlayerClose();
}

BOOL THPPlayerOpen(const char* fileName, BOOL onMemory)
{
	(void)onMemory;
	THPPlayerClose();
	Player& p = sPlayer;
	p = Player();

	char path[512];
	const char* rel = fileName ? fileName : "";
	if (*rel == '/' || *rel == '\\') rel++;
	snprintf(path, sizeof path, "assets/%s", rel);
	p.file = fopen(path, "rb");
	if (!p.file) {
		printf("[PC THP] cannot open %s\n", path);
		return FALSE;
	}
	u8 hdr[0x30];
	if (fread(hdr, 1, sizeof hdr, p.file) != sizeof hdr || memcmp(hdr, "THP\0", 4) != 0) {
		printf("[PC THP] %s: not a THP file\n", path);
		close_file();
		return FALSE;
	}
	const u32 version   = be32(hdr + 0x04);
	p.fps               = bef32(hdr + 0x10);
	p.numFrames         = be32(hdr + 0x14);
	p.firstFrameSize    = be32(hdr + 0x18);
	const u32 compOff   = be32(hdr + 0x20);
	p.firstFrameOffset  = be32(hdr + 0x28);

	// Component descriptors: u32 count, u8 types[16], then per component info.
	u8 comp[20];
	if (fseek(p.file, long(compOff), SEEK_SET) != 0 || fread(comp, 1, sizeof comp, p.file) != sizeof comp) {
		close_file();
		return FALSE;
	}
	const u32 numComp = be32(comp);
	for (u32 c = 0; c < numComp && c < 16; c++) {
		u8 info[16];
		if (comp[4 + c] == 0) { // video
			const size_t len = version >= 0x11000 ? 12 : 8;
			if (fread(info, 1, len, p.file) != len) break;
			p.videoW = be32(info);
			p.videoH = be32(info + 4);
		} else if (comp[4 + c] == 1) { // audio
			const size_t len = version >= 0x11000 ? 16 : 12;
			if (fread(info, 1, len, p.file) != len) break;
			p.hasAudio      = true;
			p.audioChannels = be32(info);
			p.audioRate     = be32(info + 4);
			p.audioTracks   = version >= 0x11000 ? be32(info + 12) : 1;
		}
	}
	if (p.videoW == 0 || p.videoH == 0 || p.numFrames == 0 || p.fps <= 0.0f) {
		printf("[PC THP] %s: unsupported layout\n", path);
		close_file();
		return FALSE;
	}
	rewind_stream();

	memset(&ActivePlayer, 0, sizeof(ActivePlayer));
	ActivePlayer.mIsOpen                   = TRUE;
	ActivePlayer.mState                    = THP_STOPPED;
	ActivePlayer.mVideoInfo.mXSize         = p.videoW;
	ActivePlayer.mVideoInfo.mYSize         = p.videoH;
	ActivePlayer.mAudioInfo.mSndChannels   = p.audioChannels;
	ActivePlayer.mAudioInfo.mSndFrequency  = p.audioRate;
	ActivePlayer.mAudioInfo.mSndNumTracks  = p.audioTracks;
	ActivePlayer.mAudioExist               = p.hasAudio;
	ActivePlayer.mHeader.mFrameRate        = p.fps;
	ActivePlayer.mHeader.mNumFrames        = p.numFrames;
	printf("[PC THP] %s: %ux%u, %u frames @ %.2f fps, audio %s (%u Hz, %u tracks)\n", path, p.videoW, p.videoH,
	       p.numFrames, p.fps, p.hasAudio ? "yes" : "no", p.audioRate, p.audioTracks);
	pc_gfx_set_post_suppressed(1); // bloom/SSAO dejaban el video en negro
	return TRUE;
}

BOOL THPPlayerClose()
{
	pc_gfx_set_post_suppressed(0);
	audio_close();
	close_file();
	sPlayer.rgba.clear();
	sPlayer.rgbaW = sPlayer.rgbaH = 0;
	sPlayer.shownFrame = -1;
	ActivePlayer.mIsOpen = FALSE;
	ActivePlayer.mState  = THP_STOPPED;
	return TRUE;
}

u32 THPPlayerCalcNeedMemory() { return 64; } // decoding happens in host memory
BOOL THPPlayerSetBuffer(u8*) { return TRUE; }

BOOL THPPlayerGetVideoInfo(void* dst)
{
	if (dst) memcpy(dst, &ActivePlayer.mVideoInfo, sizeof(THPVideoInfo));
	return ActivePlayer.mIsOpen;
}
BOOL THPPlayerGetAudioInfo(void* dst)
{
	if (dst) memcpy(dst, &ActivePlayer.mAudioInfo, sizeof(THPAudioInfo));
	return ActivePlayer.mIsOpen;
}

BOOL THPPlayerPrepare(int offset, u8 flag, int audioTrack)
{
	(void)offset;
	if (!ActivePlayer.mIsOpen) return FALSE;
	sPlayer.loop       = (flag & 1) != 0;
	sPlayer.audioTrack = audioTrack;
	rewind_stream();
	sPlayer.baseFrame  = 0.0;
	sPlayer.shownFrame = -1;
	// Decode the first picture so the first draw has something to show.
	step_frame(true);
	ActivePlayer.mState = THP_STOPPED;
	return TRUE;
}

BOOL THPPlayerPlay()
{
	if (!ActivePlayer.mIsOpen) return FALSE;
	if (ActivePlayer.mState == THP_PAUSED) {
		sPlayer.playStartTicks += SDL_GetTicks() - sPlayer.pausedAtTicks;
		if (sPlayer.audioDev) SDL_PauseAudioDevice(sPlayer.audioDev, 0);
		sMixPaused = false;
	} else if (ActivePlayer.mState != THP_PLAYING) {
		sPlayer.playStartTicks = SDL_GetTicks();
		sPlayer.baseFrame      = double(std::max(sPlayer.shownFrame, 0));
		if (audio_open()) {
			if (sPlayer.audioDev) {
				SDL_ClearQueuedAudio(sPlayer.audioDev);
				SDL_PauseAudioDevice(sPlayer.audioDev, 0);
			} else {
				mix_clear();
				sMixPaused = false;
			}
		}
	}
	ActivePlayer.mState = THP_PLAYING;
	return TRUE;
}

BOOL THPPlayerPause()
{
	if (ActivePlayer.mState != THP_PLAYING) return FALSE;
	sPlayer.pausedAtTicks = SDL_GetTicks();
	if (sPlayer.audioDev) SDL_PauseAudioDevice(sPlayer.audioDev, 1);
	sMixPaused          = true;
	ActivePlayer.mState = THP_PAUSED;
	return TRUE;
}

void THPPlayerStop()
{
	if (sPlayer.audioDev) {
		SDL_PauseAudioDevice(sPlayer.audioDev, 1);
		SDL_ClearQueuedAudio(sPlayer.audioDev);
	}
	sMixPaused = true;
	mix_clear();
	if (ActivePlayer.mIsOpen) ActivePlayer.mState = THP_STOPPED;
}

BOOL THPPlayerSetVolume(int vol, int duration)
{
	(void)duration;
	sPlayer.volume = std::clamp(vol, 0, 127) / 127.0f;
	return TRUE;
}

u32 THPPlayerGetTotalFrame() { return ActivePlayer.mHeader.mNumFrames; }
u8 THPPlayerGetState() { return ActivePlayer.mState; }

int THPPlayerDrawCurrentFrame(GXRenderModeObj* rmode, int x, int y, int polyWidth, int polyHeight)
{
	(void)rmode;
	Player& p = sPlayer;
	if (!ActivePlayer.mIsOpen) return -1;

	if (ActivePlayer.mState == THP_PLAYING) {
		// Advance to the frame the clock asks for; audio of every skipped
		// frame is still queued, only their pictures are dropped.
		const s32 target = s32(clock_frame());
		while (p.nextIndex <= u32(std::max(target, 0)) && p.nextIndex < p.numFrames) {
			const bool last = p.nextIndex == u32(target) || p.nextIndex + 1 >= p.numFrames;
			if (!step_frame(last)) break;
		}
		if (p.nextIndex >= p.numFrames && target >= s32(p.numFrames)) {
			if (p.loop) {
				rewind_stream();
				p.playStartTicks = SDL_GetTicks();
				p.baseFrame      = 0.0;
			} else if (p.audioDev ? SDL_GetQueuedAudioSize(p.audioDev) == 0 : (!sMixMode || mix_empty())) {
				ActivePlayer.mState = THP_PLAYED;
			}
		}
	}

	if (p.rgba.empty()) return p.shownFrame;

	// Textured quad through the immediate GX path; the caller already set the
	// 2D ortho projection (J2DOrthoGraph::setPort).
	GXSetNumChans(0);
	GXSetNumTexGens(1);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3X4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, 125);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
	GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	// Sin indirecto: la última partícula pudo dejarlo puesto.
	GXSetNumIndStages(0);
	GXSetTevDirect(GX_TEVSTAGE0);
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GXSetCullMode(GX_CULL_NONE);

	Mtx ident;
	PSMTXIdentity(ident);
	GXLoadPosMtxImm(ident, GX_PNMTX0);
	GXSetCurrentMtx(GX_PNMTX0);

	pc_gfx_init_tex_obj_rgba(&p.texObj, p.rgba.data(), u16(p.rgbaW), u16(p.rgbaH));
	GXLoadTexObj(&p.texObj, GX_TEXMAP0);

	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	const f32 x0 = f32(x), y0 = f32(y), x1 = f32(x + polyWidth), y1 = f32(y + polyHeight);
	GXBegin(GX_QUADS, GX_VTXFMT7, 4);
	GXPosition3f32(x0, y0, 0.0f); GXTexCoord2f32(0.0f, 0.0f);
	GXPosition3f32(x1, y0, 0.0f); GXTexCoord2f32(1.0f, 0.0f);
	GXPosition3f32(x1, y1, 0.0f); GXTexCoord2f32(1.0f, 1.0f);
	GXPosition3f32(x0, y1, 0.0f); GXTexCoord2f32(0.0f, 1.0f);
	GXEnd();

	// Restore the 2D defaults J2D expects (setup2D state).
	GXSetNumChans(1);
	GXSetNumTexGens(0);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	return p.shownFrame;
}

void THPPlayerDrawDone() {}
void THPPlayerPostDrawDone() {}

} // extern "C"

// Suma el audio del vídeo a `out` (estéreo s16, `frames` a `rate` Hz). La
// llama jaudio_sink al enviar cada bloque, como pc_ast_mix.
extern "C" void pc_thp_mix(int16_t* out, size_t frames, int rate)
{
	if (!sMixMode || sMixPaused || rate <= 0) return;
	std::lock_guard<std::mutex> lock(sMixMutex);
	const double step = double(sMixRate) / double(rate);
	for (size_t i = 0; i < frames && sMixCount >= 2; i++) {
		const s16 l = sMixRing[sMixRead];
		const s16 r = sMixRing[(sMixRead + 1) % kMixCapacity];
		const int ml = out[i * 2] + l, mr = out[i * 2 + 1] + r;
		out[i * 2]     = s16(ml > 32767 ? 32767 : ml < -32768 ? -32768 : ml);
		out[i * 2 + 1] = s16(mr > 32767 ? 32767 : mr < -32768 ? -32768 : mr);
		sMixPhase += step;
		while (sMixPhase >= 1.0 && sMixCount >= 2) {
			sMixPhase -= 1.0;
			sMixRead = (sMixRead + 2) % kMixCapacity;
			sMixCount -= 2;
		}
	}
}
