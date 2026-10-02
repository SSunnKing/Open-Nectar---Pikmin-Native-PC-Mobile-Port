#include "jaudio/hvqm_play.h"
#include <stdint.h>
#include "Dolphin/os.h"
#include "hvqm4.h"
#include "jaudio/dspbuf.h"
#include "jaudio/dummyprobe.h"
#include "jaudio/dvdthread.h"
#include "jaudio/interleave.h"
#include "jaudio/sample.h"
#include "jaudio/syncstream.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifdef PIKI_PC_PORT
// The port's synchronous loader, declared the same way virload.c declares it.
//
// The original queues a task onto jaudio's DVD thread and spins on a status
// word until it lands. That thread does not exist here, so the spin never ends:
// the game froze on the title screen the moment an attract movie started, with
// the file open and nothing after it in the log.
s32 DVDT_LoadtoDRAMHost(u32 owner, immut char* name, void* dst, u32 src, u32 length, u32* status,
                        Jac_DVDCallback callback);
#endif

#ifdef PIKI_PC_PORT
// The file is GameCube data and every multi-byte field in it is big-endian.
// The rest of the port converts in the Stream layer, but this player reads
// structures straight out of the buffer the DVD code filled, so nothing has
// touched them. Left alone, the very first thing the picture size said was
// 32770x57345 -- which is 0x8002 by 0xE001, the bytes of 640x480 reversed.
//
// Same endianness test the Stream layer uses; one program should not hold two
// opinions about the byte order of the machine it is running on.
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define HVQM_SWAP_NEEDED 1
#else
#define HVQM_SWAP_NEEDED 0
#endif

static inline u16 hvqmBE16(u16 value)
{
#if HVQM_SWAP_NEEDED
	return __builtin_bswap16(value);
#else
	return value;
#endif
}

static inline u32 hvqmBE32(u32 value)
{
#if HVQM_SWAP_NEEDED
	return __builtin_bswap32(value);
#else
	return value;
#endif
}
#endif

#ifdef PIKI_PC_PORT
// "No picture this time", written so that it clears the whole pointer.
//
// The original is "*(int*)data = 0", which on a 32-bit console cleared all of
// it. Here it clears the low half and leaves the high half of whatever
// Jac_GetPicture stored there a few lines earlier, producing an address like
// 0x7fff00000000: not null, so the caller's "if (pictureData)" passes, and the
// first read segfaults. That is what killed the game on the first frame of the
// first movie.
#define HVQM_CLEAR_PICTURE(ptr) (*(void**)(ptr) = NULL)
#else
#define HVQM_CLEAR_PICTURE(ptr) (*(int*)(ptr) = 0)
#endif

#ifdef PIKI_PC_PORT
static int hvqmAudioRecords;
static u32 hvqmAudioBytes;
// Why the decode loop gives up on a pass. It stops making progress while the
// picture is frozen, and there are only two ways out of the loop that do not
// advance anything: no free picture buffer to decode into, or no room in the
// audio stream to send to. Which one it is decides the fix.
static u32 hvqmStallNoPicBuffer;
static u32 hvqmStallStreamFull;
#endif

static volatile BOOL dvd_loadfinish;
static u32 dvdcount;
static int arcoffset;
static int AUDIO_FRAME;
static u32 PIC_FRAME;
static int drop_picture_flag;
static u32 PIC_BUFFERS;
static int dvdload_size;
static u32 dvdfile_size;

static struct RecHeader {
	u16 mRecordType; // _00, 0=audio, 1=video
	u16 mFrameFlags; // _02
	u32 mDataSize;   // _04
} rec_header;

static int v_header;
static int gop_baseframe;
static u32 gop_frame;
static int vh_state;
static SeqObj* hvqm_obj;
static u32 dvd_active;
static u8* virtualfile_buf;
static BOOL record_ok;
static void* ref1;
static void* ref2;

static void InitPic();
static BOOL CheckDraw(u32 id);
static int Decode1(u8* data, u32 frameId, u8 frameType);

struct PICControl {
	void* mPicBuffer; // _00, pointer to the picture data
	u32 mFrameNumber; // _04, frame this picture belongs to
	u32 mBufferState; // _08, 0=free, 1=ready, 2=displayed
	int mMagicNumber; // _0C
} pic_ctrl[24];

struct DVDControl {
	int _00;         // _00
	u8 mState;       // _04
	int mFileOffset; // _08
	int mLoadedSize; // _0C
} dvd_ctrl[3];

static char filename[64] ATTRIBUTE_ALIGN(32);
static u8* dvd_buf[3];
static int gop_subframe         = -1;
static BOOL playback_first_wait = TRUE;
static BOOL hvqm_first          = TRUE;

typedef struct HVQM_FileHeader {
	int _00;             // _00, unused
	int _04;             // _04, unused
	int _08;             // _08, unused
	int _0C;             // _0C, unused
	int _10;             // _10, unused
	u8 _14[4];           // _14, unused
	u32 mTotalFrames;    // _18
	int _1C;             // _1C, unused
	u8 _20[0x30 - 0x20]; // _20, unused
	u32 mFileSize;       // _30
	VideoInfo mInfo;     // _34
	u8 mAudioFormat;     // _3C
	u32 mSampleRate;     // _40
} HVQM_FileHeader;

static HVQM_FileHeader file_header;
static u32 gop_header[5]; // TODO: struct?
static OSThread jac_hvqmThread;
static u8 hvqmStack[0x1000] ATTRIBUTE_ALIGN(32);

/**
 * @TODO: Documentation
 */
static void __ReLoad()
{
#if defined(VERSION_GPIP01)
	STACK_PAD_VAR(2);
#endif
	int size = dvdfile_size;
	if (dvdfile_size == 0) {
		dvd_loadfinish = 1;
		return;
	}

	if (dvd_ctrl[(dvdcount % 3)].mState == 3) {
		dvd_ctrl[(dvdcount % 3)].mState = 1;
		if (dvdfile_size < 0x80000) {
			dvdload_size = size;
		} else {
			dvdload_size = 0x80000;
		}
		dvdfile_size -= dvdload_size;
		int inter = OSDisableInterrupts();
		dvd_active += 1;

		int num_bufs = 3;
#ifdef PIKI_PC_PORT
		// Synchronous here too. The callback still runs -- the host loader
		// calls it before returning -- so the state machine around this sees
		// exactly what it expects, just sooner.
		DVDT_LoadtoDRAMHost(dvdcount, filename, dvd_buf[dvdcount % num_bufs], dvdcount << 0x13, dvdload_size, NULL, __LoadFin);
#else
		DVDT_LoadtoDRAM(dvdcount, filename, (uintptr_t)dvd_buf[dvdcount % num_bufs], dvdcount << 0x13, dvdload_size, NULL, __LoadFin);
#endif
		OSRestoreInterrupts(inter);
	}
}

/**
 * @TODO: Documentation
 */
static void __LoadFin(u32 a)
{
	dvd_active--;

	dvd_ctrl[dvdcount % 3].mState      = 2;
	dvd_ctrl[dvdcount % 3].mFileOffset = dvdcount << 0x13;
	dvd_ctrl[dvdcount % 3].mLoadedSize = dvdload_size;

	dvdcount++;
	__ReLoad();
}

/**
 * @TODO: Documentation
 */
static int __VirtualLoad(u32 currentOffs, u32 bytesToRead, u8* data)
{
	u32 bufIdx = 0;
	for (bufIdx = 0; bufIdx < 3; bufIdx++) {
		if (dvd_ctrl[bufIdx].mState == 2) {

			int fileOffs = dvd_ctrl[bufIdx].mFileOffset;
			if (fileOffs <= currentOffs && fileOffs + dvd_ctrl[bufIdx].mLoadedSize > currentOffs) {
				if (fileOffs + dvd_ctrl[bufIdx].mLoadedSize > currentOffs + bytesToRead) {
					Jac_bcopy((void*)(dvd_buf[bufIdx] + (currentOffs - dvd_ctrl[bufIdx].mFileOffset)), data, bytesToRead);
					break;
				}

				if (dvd_ctrl[(bufIdx + 1) % 3].mState == 2) {
					int fromNextBuffer    = (currentOffs + bytesToRead) - (dvd_ctrl[bufIdx].mFileOffset + dvd_ctrl[bufIdx].mLoadedSize);
					int fromCurrentBuffer = bytesToRead - fromNextBuffer;
					Jac_bcopy((void*)(dvd_buf[bufIdx] + (currentOffs - dvd_ctrl[bufIdx].mFileOffset)), data, fromCurrentBuffer);
					Jac_bcopy((void*)(dvd_buf[(bufIdx + 1) % 3]), data + fromCurrentBuffer, fromNextBuffer);
					dvd_ctrl[bufIdx].mState = 3;
					break;
				}

				if (dvd_loadfinish) {
					return -1;
				}

			} else if (fileOffs + 0x80000 <= currentOffs) {
				dvd_ctrl[bufIdx].mState = 3;
			}
		}
	}

	if (bufIdx == 3) {
		for (int i = 0; i < 3; i++) { }
		return 0;
	}

	return bytesToRead;
}

/**
 * @TODO: Documentation
 */
static void InitAudio1(StreamHeader_* header, u8* data, u32 size)
{
	Jac_InitStreamData(data, size);
	StreamAudio_Start(0, 0, NULL, TRUE, FALSE, header);
}

/**
 * @TODO: Documentation
 */
void Jac_HVQM_Init(immut char* movieFilePath, u8* data, u32 bufferSize)
{
	// TODO: Use this in more places below instead of hardcoding 0x40000
	u32 audioBufferSize = 0x40000;
	playback_first_wait = TRUE;
	for (u32 i = 0; i < bufferSize; i++) {
		(void)&i;
		data[i] = 0;
	}
	STACK_PAD_VAR(1);

	dvdcount          = 0;
	dvd_active        = 0;
	vh_state          = 0;
	drop_picture_flag = 1;
	record_ok         = TRUE;
	PIC_FRAME         = 0;
	AUDIO_FRAME       = 0;

	if (bufferSize < 0x40000) {
		return;
	}

	u8* audioBuffer = data;
	if (bufferSize - 0x40000 < 0x60000) {
		return;
	}

	virtualfile_buf = data + 0x40000;

	u32 remainingMem = bufferSize - 0xa0000;
	data += 0xa0000;
	for (u32 dvdIndex = 0; dvdIndex < 3; dvdIndex++) {
		if (remainingMem < 0x80000) {
			return;
		}

		dvd_buf[dvdIndex] = data;
		data += 0x80000;
		remainingMem -= 0x80000;
	}

	for (u32 i = 0; i < 3; i++) {
		dvd_ctrl[i].mState = 3;
	}

	arcoffset      = 0;
	dvd_loadfinish = 0;
	dvdfile_size   = DVDT_CheckFile(movieFilePath);
	dvdfile_size -= 0x80000;
	volatile u32 status;
#ifdef PIKI_PC_PORT
	DVDT_LoadtoDRAMHost(dvdcount, movieFilePath, dvd_buf[dvdcount % 3], 0, 0x80000, (u32*)&status, NULL);
#else
	DVDT_LoadtoDRAM(dvdcount, movieFilePath, (uintptr_t)dvd_buf[dvdcount % 3], 0, 0x80000, (u32*)&status, 0);
	while (status == 0) { }
#endif

	dvd_ctrl[0].mFileOffset = 0;
	dvd_ctrl[0].mState      = 2;
	dvd_ctrl[0].mLoadedSize = 0x80000;
	dvdcount++;
	strcpy(filename, movieFilePath);
	__ReLoad();

	file_header = *(HVQM_FileHeader*)dvd_buf[0];
#ifdef PIKI_PC_PORT
	// Only the fields anything reads. The rest of the structure is marked
	// unused in the decompilation and swapping it would just be noise.
	file_header.mTotalFrames = hvqmBE32(file_header.mTotalFrames);
	file_header.mFileSize    = hvqmBE32(file_header.mFileSize);
	file_header.mSampleRate  = hvqmBE32(file_header.mSampleRate);
	file_header.mInfo.width  = hvqmBE16(file_header.mInfo.width);
	file_header.mInfo.height = hvqmBE16(file_header.mInfo.height);
#endif

	arcoffset += 0x44;
	gop_baseframe = 0;
	gop_frame     = 0;
	gop_subframe  = -1;

	STACK_PAD_VAR(2);
	StreamHeader_ header;
	u32 sampleRate;
	u32 audioFormat;
	u32 fileSize;
	u32 sampleCount;
	STACK_PAD_VAR(7);

	(void)&sampleRate;
	(void)&audioFormat;
	(void)&fileSize;
	(void)&sampleCount;

	audioFormat = file_header.mAudioFormat;
	sampleRate  = file_header.mSampleRate;
	fileSize    = file_header.mFileSize;
	switch (audioFormat) {
	case AUDIOFRMT_ADPCM:
	{
		// 16 samples in 18 bytes
		sampleCount = fileSize * 16 / 18;
		break;
	}
	case AUDIOFRMT_16BIT_PCM:
	{
		// 2 bytes per sample
		sampleCount = fileSize / 2;
		break;
	}
	case AUDIOFRMT_8BIT_PCM:
		// 1 byte per sample
		sampleCount = fileSize;
		break;
	case AUDIOFRMT_ADPCM4X:
		// 16 samples in 36 bytes
		sampleCount = fileSize * 16 / 36;
		break;
	}
	file_header.mFileSize = 0;

	header.fileSize    = fileSize;
	header.sampleCount = sampleCount;

	u16 sampleRate_2 = (u16)sampleRate;
	!sampleRate_2;
	header.sampleRate = sampleRate_2;

	header.audioFormat = audioFormat;
	header._0C         = 0x10;
	header.frameRate   = 30; // 30 FPS

	for (int i = 0; i < 4; i++) {
		header._10[i] = 0;
	}
	InitAudio1(&header, audioBuffer, audioBufferSize);

	hvqm_obj = (SeqObj*)data;
	data += 0x20;
	remainingMem -= 0x20;

	HVQM4InitDecoder();
	HVQM4InitSeqObj(hvqm_obj, &file_header.mInfo);

	u32 size = OSRoundUp32B(HVQM4BuffSize(hvqm_obj));
	if (remainingMem >= size) {
		void* buffer = data;
		data += size;
		remainingMem -= size;
		HVQM4SetBuffer(hvqm_obj, buffer);

		int i;
		for (i = 0; i < 0x18; i++) {
			if (remainingMem < 0x70800) {
				break;
			}
			pic_ctrl[i].mPicBuffer   = data;
			pic_ctrl[i].mMagicNumber = 0x12345678;
			data += 0x70800;
			remainingMem -= 0x70800;
		}
		PIC_BUFFERS = i;

		InitPic();
		int start = Jac_GetCurrentSCounter();
#if defined(PIKI_PC_PORT)
		// The console waited here for the DSP to tick over once. The host
		// renderer ticks the same counter, but if audio is not running -- a
		// device that failed to open, a build with the old mixer -- it never
		// will, and an unbounded spin in setup code is a frozen game with no
		// message. Bounded, and it says which of the two happened.
		{
			int spins = 0;
			while (start == (int)Jac_GetCurrentSCounter()) {
				if (++spins > 200000000) {
					OSReport("[PC Port] H4M: audio counter never advanced; carrying on without the wait\n");
					break;
				}
			}
			if (getenv("PIKMIN_H4M_DEBUG") != NULL) {
				OSReport("[PC Port] H4M: %d picture buffers, %u groups, %ux%u\n",
				         PIC_BUFFERS, file_header.mTotalFrames,
				         file_header.mInfo.width, file_header.mInfo.height);
			}
		}
#else
		while (start == Jac_GetCurrentSCounter()) { }
#endif
	}

	return;

	(void)&movieFilePath;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 00003C (Nonmatching)
 */
static void* hvqm_proc(void* data)
{
	TRAP_UNIMPLEMENTED;
	// We can infer this call to `OSInitFastCast` was here due to the fact that a deadstripped copy of it
	// appears next in the linker map.  Other context clues tell us that this is an `OSThreadStartFunction`
	// type of function.  Beyond that, it's anyone's guess what this function actually did.
	OSInitFastCast();
}

/**
 * @TODO: Documentation
 */
static void hvqm_forcestop()
{
	if (hvqm_first == 0 && OSIsThreadTerminated(&jac_hvqmThread) == FALSE) {
		OSCancelThread(&jac_hvqmThread);
	}
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 0000A4
 */
void Jac_HVQM_ThreadStart(void)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
BOOL Jac_HVQM_Update(void)
{
#if defined(VERSION_GPIP01)
	STACK_PAD_VAR(3);
#else
	STACK_PAD_VAR(2);
#endif
	u8* data = virtualfile_buf;
	if (gop_frame == file_header.mTotalFrames) {
		return TRUE;
	}

	u32 i = 0;
	int time_delta;
	while (TRUE) {
		if (gop_subframe == -1) {
			if (__VirtualLoad(arcoffset, sizeof(gop_header), (u8*)gop_header) == 0) {
				return FALSE;
			}
#ifdef PIKI_PC_PORT
			for (u32 word = 0; word < sizeof(gop_header) / sizeof(gop_header[0]); word++) {
				gop_header[word] = hvqmBE32(gop_header[word]);
			}
#endif
			arcoffset += 0x14;
			gop_subframe++;
		}

		if (record_ok != 0) {
			if (__VirtualLoad(arcoffset, sizeof(rec_header), (u8*)&rec_header) == 0) {
				return 0;
			}
#ifdef PIKI_PC_PORT
			rec_header.mRecordType = hvqmBE16(rec_header.mRecordType);
			rec_header.mFrameFlags = hvqmBE16(rec_header.mFrameFlags);
			rec_header.mDataSize   = hvqmBE32(rec_header.mDataSize);
			// Every record as it is parsed, for the first few. A one-byte audio
			// record appeared in the trace, which cannot occur in a file being
			// walked correctly: it means the read position drifted and headers
			// are now being read out of the middle of data. This says where.
			{
				// 24 was too few: the first group has 31 records and the
				// derail showed up after the trace had already stopped.
				// Condition-triggered, not capped. Every record in this file
				// starts on a 4-byte boundary, so the moment the read position
				// is not aligned the walk has drifted -- and the last few
				// records before it are what caused it. Chasing this with "log
				// the first N records" kept missing it, because how far the
				// decoder gets before drifting varies from run to run.
				static struct { int offset; u32 type; u32 size; } recent[4];
				static int recentAt = 0;
				static bool driftReported = false;
				recent[recentAt & 3].offset = arcoffset;
				recent[recentAt & 3].type   = rec_header.mRecordType;
				recent[recentAt & 3].size   = rec_header.mDataSize;
				recentAt++;
				if (!driftReported && (arcoffset & 3) != 0) {
					driftReported = true;
					OSReport("[PC Port] H4M: DRIFT -- header read at %d, not 4-byte aligned\n", arcoffset);
					for (int back = 4; back >= 1; back--) {
						const int slot = (recentAt - back) & 3;
						OSReport("[PC Port] H4M:   previous: offset=%d type=%u size=%u\n",
						         recent[slot].offset, recent[slot].type, recent[slot].size);
					}
					OSReport("[PC Port] H4M:   gopFrame=%u gopSubframe=%d gopHeader[2]=%u picFrame=%u\n",
					         gop_frame, gop_subframe, gop_header[2], PIC_FRAME);
				}
				static int recordsTraced = 0;
				if (recordsTraced < 0) {
					recordsTraced++;
					OSReport("[PC Port] H4M: record %2d type=%u flags=0x%04x size=%u at offset %d\n",
					         recordsTraced, rec_header.mRecordType, rec_header.mFrameFlags,
					         rec_header.mDataSize, arcoffset);
				}
			}
#endif
			arcoffset += 8;
		}

		switch (rec_header.mRecordType) {
		default:
		{
			return -1;
		}
		case 0:
		{
			if (Jac_CheckStreamFree(rec_header.mDataSize) == 0) {
#ifdef PIKI_PC_PORT
				// The record the loop is actually stuck on, printed once and
				// then rarely. Chasing this with a cap on the record trace kept
				// missing it: the derail happens further in each run, because
				// how far the decoder gets before stalling depends on timing.
				// This does not care how far in it is.
				if (getenv("PIKMIN_H4M_DEBUG") != NULL
				    && (hvqmStallStreamFull == 0 || (hvqmStallStreamFull % 4000000) == 0)) {
					OSReport("[PC Port] H4M: stalled sending audio: size=%u type=%u flags=%#06x "
					         "offset=%d free=%d\n",
					         rec_header.mDataSize, rec_header.mRecordType, rec_header.mFrameFlags,
					         arcoffset, Jac_GetStreamRemain());
				}
				hvqmStallStreamFull++;
#endif
				record_ok = 0;
			} else {
				if (__VirtualLoad(arcoffset, rec_header.mDataSize, data) == 0) {
					record_ok = 0;
					return FALSE;
				}
#if defined(VERSION_GPIP01)
				u32 remainBefore = Jac_GetStreamRemain();
#endif
				Jac_SendStreamData(data, rec_header.mDataSize);
#ifdef PIKI_PC_PORT
				hvqmAudioRecords++;
				hvqmAudioBytes += rec_header.mDataSize;
#endif
				record_ok = 1;
				arcoffset += rec_header.mDataSize;
#if defined(VERSION_GPIP01)
				u32 remainAfter = Jac_GetStreamRemain();
#endif
			}
			break;
		}
		case 1:
		{
			if (playback_first_wait && PIC_FRAME == PIC_BUFFERS) {
				if (StreamSyncCheckReady(0)) {
					StreamSyncPlayAudio(1.0f, 0, 0x3fff, 0x3fff);
#ifdef PIKI_PC_PORT
					if (getenv("PIKMIN_H4M_DEBUG") != NULL) {
						OSReport("[PC Port] H4M: audio started after %d records, %u bytes\n",
						         hvqmAudioRecords, hvqmAudioBytes);
					}
#endif
					playback_first_wait = 0;
				} else {
					record_ok = 0;
					return FALSE;
				}
			}

			switch (vh_state) {
			case 0:
			{
				if (__VirtualLoad(arcoffset, 4, (u8*)&v_header) == 0) {
					record_ok = 0;
					goto end;
				}
#ifdef PIKI_PC_PORT
				v_header = (int)hvqmBE32((u32)v_header);
#endif
				vh_state += 1;
				// fallthrough
			}
			case 1:
			{
				if (__VirtualLoad(arcoffset + 4, rec_header.mDataSize - 4, data) == 0) {
					record_ok = 0;
					goto end;
				}
				vh_state += 1;
				// fallthrough
			}
			case 2:
			{
				if (CheckDraw(v_header + gop_baseframe) == 0) {
#ifdef PIKI_PC_PORT
					hvqmStallNoPicBuffer++;
#endif
					record_ok = 0;
					goto end;
				}
				vh_state = 0;
				break;
			}
			}

			record_ok = 1;
			arcoffset += rec_header.mDataSize;
			int start_time = OSGetTime();
			u32 dec        = Decode1(data, v_header + gop_baseframe, rec_header.mFrameFlags & 0xff);
			time_delta     = OSGetTime() - start_time;
#ifdef PIKI_PC_PORT
			// Decode1 returns -1 when the picture buffer it wants is occupied,
			// and dec is unsigned, so "dec <= 1" is false and the frame is not
			// counted -- but arcoffset was advanced two lines above, so the
			// record is consumed anyway. One lost frame makes gop_subframe fall
			// one short of gop_header[2], the group's end is never recognised,
			// its 20-byte header is not skipped, and every read after that is
			// garbage. Measured: the walk derailed at 3883956, exactly 0x14
			// before the next real record, with gopSubframe=29 against 30.
			//
			// CheckDraw tested the same buffer moments earlier and said it was
			// free. It is the main thread that takes it in between, from
			// Jac_GetPicture -- a race that the console's cooperative threads
			// left almost no room for.
			//
			// So put the record back and retry it. The data is already loaded,
			// so vh_state stays at 2 and the retry costs only the CheckDraw.
			if (dec == (u32)-1) {
				arcoffset -= rec_header.mDataSize;
				record_ok = 0;
				vh_state  = 2;
				hvqmStallNoPicBuffer++;
				goto end;
			}
#endif
			if (dec <= 1) {
				PIC_FRAME++;
				gop_subframe++;
				if (gop_subframe == gop_header[2]) {
					gop_baseframe += gop_subframe;
					gop_subframe = -1;
					gop_frame++;
				}
			}
			break;
		}
		}

		if (gop_frame == file_header.mTotalFrames) {
			return TRUE;
		}

		for (int k = 0; k < 3; k++) {
			if (dvd_ctrl[k].mState == 3) {
				__ReLoad();
				break;
			}
		}

		if (i == 0 && time_delta >= 500000 && drop_picture_flag == 0) {
			break;
		}

		i++;
		if (i >= 2) {
			break;
		}
	}
end:
	return FALSE;
}

/**
 * @TODO: Documentation
 */
void Jac_HVQM_ForceStop(void)
{
	gop_frame = file_header.mTotalFrames;
	StreamSyncStopAudio(0);
	BOOL inter   = OSDisableInterrupts();
	dvdfile_size = 0;
	if (dvd_loadfinish != 1 && dvd_active != 0) {
		OSRestoreInterrupts(inter);
		while (dvd_loadfinish != 1) { }

	} else {
		OSRestoreInterrupts(inter);
	}
	hvqm_forcestop();
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000070
 */
void Jac_CountReadyPictures(void)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
int Jac_GetPicture(void* data, int* x, int* y)
{
	int offset = 0;
	int index  = -1;
#ifdef PIKI_PC_PORT
	// The frame this returns is the audio clock, and it is what decides which
	// picture is shown. A picture that never changes means this number never
	// changes, so print it next to the state that feeds it.
	{
		static const int debugOn = (getenv("PIKMIN_H4M_DEBUG") != NULL);
		static int reportGate = 0;
		if (debugOn && (++reportGate % 120) == 0) {
			OSReport("[PC Port] H4M: audioFrame=%d picFrame=%u firstWait=%d gopFrame=%u "
			         "audioRecords=%d audioBytes=%u stallPic=%u stallStream=%u\n",
			         StreamGetCurrentFrame(0, 2), PIC_FRAME, playback_first_wait, gop_frame,
			         hvqmAudioRecords, hvqmAudioBytes, hvqmStallNoPicBuffer, hvqmStallStreamFull);
		}
	}
#endif
	*x         = file_header.mInfo.width;
	*y         = file_header.mInfo.height;

	if (playback_first_wait) {
		HVQM_CLEAR_PICTURE(data);
		return 1;
	}

	int frame = StreamGetCurrentFrame(0, 2);
	if (frame == -1) {
		hvqm_forcestop();
		HVQM_CLEAR_PICTURE(data);
		return -1;
	}

	AUDIO_FRAME = frame;

	for (u32 i = 0; i < PIC_BUFFERS; i++) {
		if (pic_ctrl[i].mBufferState) {
			if (pic_ctrl[i].mFrameNumber <= frame) {
				if (offset < pic_ctrl[i].mFrameNumber) {
					offset = pic_ctrl[i].mFrameNumber;
					index  = i;
				}
				pic_ctrl[i].mBufferState = 0;
			} else {
				if (pic_ctrl[i].mBufferState == 2) {
					pic_ctrl[i].mBufferState = 0;
				}
			}
		}
	}

	if (index != -1) {
		pic_ctrl[index].mBufferState = 2;
	}

	for (u32 i = 0; i < PIC_BUFFERS; i++) {
		if (pic_ctrl[i].mBufferState && frame == pic_ctrl[i].mFrameNumber) {
			pic_ctrl[i].mBufferState = 2;
			*(void**)data            = pic_ctrl[i].mPicBuffer;
			drop_picture_flag        = 0;
			if (index != -1 && index != i) {
				pic_ctrl[index].mBufferState = 0;
			}
			if (frame < 3) {
				HVQM_CLEAR_PICTURE(data);
			}
			return frame + 1;
		}
	}

	drop_picture_flag = 1;

	if (index != -1) {
		*(void**)data = pic_ctrl[index].mPicBuffer;
		if (frame < 3) {
			HVQM_CLEAR_PICTURE(data);
		}
		return offset + 1;
	}

	if (gop_frame == file_header.mTotalFrames) {
		StreamSyncStopAudio(0);
	}
	HVQM_CLEAR_PICTURE(data);
	return 0;
}

/**
 * @TODO: Documentation
 */
static void InitPic()
{
	ref1 = NULL;
	ref2 = NULL;
	for (u32 i = 0; i < PIC_BUFFERS; i++) {
		pic_ctrl[i].mFrameNumber = 0;
		pic_ctrl[i].mBufferState = 0;
	}
}

/**
 * @TODO: Documentation
 */
static BOOL CheckDraw(u32 id)
{
	if (pic_ctrl[(id - pic_ctrl[0].mFrameNumber) % PIC_BUFFERS].mBufferState) {
		return FALSE;
	}
	return TRUE;
}

/**
 * @TODO: Documentation
 */
static int Decode1(u8* data, u32 frameId, u8 frameType)
{
	u32 id    = (frameId - pic_ctrl[0].mFrameNumber) % PIC_BUFFERS;
	void* ref = pic_ctrl[id].mPicBuffer;
	if (pic_ctrl[id].mBufferState) {
		return -1;
	}

	switch (frameType) {
	case 0x10: // IPIC chunk
	{
#if defined(VERSION_GPIP01)
		Probe_Start(12, "HVQM-I-PIC");
#endif
		HVQM4DecodeIpic(hvqm_obj, data, (u8*)ref);
#if defined(VERSION_GPIP01)
		Probe_Finish(12);
#endif
		ref2 = ref1;
		ref1 = ref;
		break;
	}
	case 0x20: // PPIC chunk
	{
#if defined(VERSION_GPIP01)
		Probe_Start(11, "HVQM-P-PIC");
#endif
		HVQM4DecodePpic(hvqm_obj, data, (u8*)ref, (u8*)ref1);
#if defined(VERSION_GPIP01)
		Probe_Finish(11);
#endif
		ref2 = ref1;
		ref1 = ref;
		break;
	}
	case 0x30: // BPIC chunk
	{
#if defined(VERSION_GPIP01)
		Probe_Start(13, "HVQM-B-PIC");
#endif
		HVQM4DecodeBpic(hvqm_obj, data, (u8*)ref, (u8*)ref2, (u8*)ref1);
#if defined(VERSION_GPIP01)
		Probe_Finish(13);
#endif
		break;
	}
	}

	pic_ctrl[id].mFrameNumber = frameId;
	pic_ctrl[id].mBufferState = 1;
	return 0;
}
