/**
 * @file dvd_stubs.cpp
 * @brief Stub implementations for Dolphin DVD filesystem functions.
 *
 * In Stage 2, these will be replaced with real filesystem access
 * reading from extracted game assets on disk.
 */
#include "Dolphin/dvd.h"

#include <cstdio>
#include <cstring>

#include <unordered_map>
#include <vector>
#include <string>

static std::unordered_map<DVDFileInfo*, FILE*> sOpenFiles;
static std::vector<std::string> sFastOpenPaths;

extern "C" {

void DVDInit() {
    printf("[PC Port] DVDInit() - DVD subsystem initialized\n");
}

BOOL DVDOpen(const char* filename, DVDFileInfo* fileInfo) {
    std::string path = filename;
    if (path.length() > 0 && (path[0] == '/' || path[0] == '\\')) {
        path = path.substr(1);
    }
    path = "assets/" + path;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        printf("[PC Port] DVDOpen(\"%s\") -> FAILED to open %s\n", filename, path.c_str());
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    fileInfo->length = ftell(f);
    fseek(f, 0, SEEK_SET);
    fileInfo->startAddr = 0;
    
    sOpenFiles[fileInfo] = f;
    printf("[PC Port] DVDOpen(\"%s\") -> OK, size = %u\n", filename, fileInfo->length);
    return TRUE;
}

BOOL DVDFastOpen(s32 entryNum, DVDFileInfo* fileInfo) {
    if (entryNum >= 0 && entryNum < (s32)sFastOpenPaths.size()) {
        return DVDOpen(sFastOpenPaths[entryNum].c_str(), fileInfo);
    }
    return FALSE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    (void)prio;
    auto it = sOpenFiles.find(fileInfo);
    if (it == sOpenFiles.end()) return -1;
    
    FILE* f = it->second;
    fseek(f, offset, SEEK_SET);
    s32 read_bytes = fread(addr, 1, length, f);

    // GameCube DVD reads are rounded to cache-line boundaries. Bytes beyond the
    // logical end of a file are returned as padding, not as a short read.
    if (read_bytes < length) {
        memset(static_cast<u8*>(addr) + read_bytes, 0, length - read_bytes);
    }
    return length;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio) {
    s32 bytes = DVDReadPrio(fileInfo, addr, length, offset, prio);
    if (callback) {
        callback(bytes, fileInfo);
    }
    return TRUE;
}

BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset,
                         DVDCBCallback callback, s32 prio) {
    (void)block; (void)addr; (void)length; (void)offset; (void)callback; (void)prio;
    return FALSE;
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    auto it = sOpenFiles.find(fileInfo);
    if (it != sOpenFiles.end()) {
        fclose(it->second);
        sOpenFiles.erase(it);
        return TRUE;
    }
    return FALSE;
}

BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback) {
    (void)fileInfo; (void)length; (void)offset;
    if (callback) callback(0, fileInfo);
    return TRUE;
}

void DVDResume() { }
void DVDReset()  { }

BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) { 
    if (callback) callback(0, block);
    return TRUE; 
}
s32  DVDCancel(DVDCommandBlock* block) { (void)block; return 0; }
s32  DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) { (void)block; (void)id; return 0; }
BOOL DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) {
    (void)block; (void)id; 
    if (callback) callback(0, block);
    return TRUE;
}

s32  DVDGetCommandBlockStatus(const DVDCommandBlock* block) { (void)block; return DVD_STATE_END; }
s32  DVDGetDriveStatus()       { return DVD_STATE_END; }
BOOL DVDSetAutoInvalidation(BOOL doAutoInval) { (void)doAutoInval; return TRUE; }
void* DVDGetFSTLocation()      { return nullptr; }

BOOL DVDOpenDir(const char* dirName, DVDDir* dir) { (void)dirName; (void)dir; return FALSE; }
BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirEntry) { (void)dir; (void)dirEntry; return FALSE; }
BOOL DVDCloseDir(DVDDir* dir)  { (void)dir; return TRUE; }
BOOL DVDGetCurrentDir(char* path, u32 maxLength) { (void)path; (void)maxLength; return FALSE; }
BOOL DVDChangeDir(const char* dirName) { (void)dirName; return FALSE; }
s32  DVDConvertPathToEntrynum(const char* path) {
    std::string s_path = path;
    sFastOpenPaths.push_back(s_path);
    return sFastOpenPaths.size() - 1;
}

void __DVDLowSetWAType(u32 type, u32 location) { (void)type; (void)location; }
s32  DVDGetTransferredSize(DVDFileInfo* fileInfo) { (void)fileInfo; return 0; }

// Los campos son char[4]/char[2] sin terminador: un literal "GPIE" no cabe
// en C++ estricto (Clang lo rechaza; GCC lo dejaba pasar con -fpermissive).
static DVDDiskID sDummyDiskID = { { 'G', 'P', 'I', 'E' }, { '0', '1' }, 0, 1, 0, 0, { 0 } };
DVDDiskID* DVDGetCurrentDiskID() { return &sDummyDiskID; }
BOOL DVDCompareDiskID(DVDDiskID* id1, DVDDiskID* id2) {
    return memcmp(id1, id2, sizeof(DVDDiskID)) == 0;
}
DVDLowCallback DVDLowClearCallback() { return nullptr; }

BOOL DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback callback) { (void)block; (void)callback; return TRUE; }
BOOL DVDCancelStream(DVDCommandBlock* block) { (void)block; return TRUE; }
BOOL DVDCheckDisk() { return TRUE; }

void DVDPause() { }
s32  DVDSeekPrio(DVDFileInfo* fileInfo, s32 offset, s32 prio) { (void)fileInfo; (void)offset; (void)prio; return 0; }
BOOL DVDSeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, DVDCallback callback, s32 prio) {
    (void)fileInfo; (void)offset; (void)callback; (void)prio; return FALSE;
}
s32  DVDGetFileInfoStatus(DVDFileInfo* fileInfo) { (void)fileInfo; return DVD_FILEINFO_READY; }
BOOL DVDFastOpenDir(s32 entryNum, DVDDir* dir) { (void)entryNum; (void)dir; return FALSE; }
BOOL DVDCancelAllAsync(DVDCBCallback callback) { (void)callback; return TRUE; }
s32  DVDCancelAll() { return 0; }
void DVDDumpWaitingQueue() { }

/* Private DVD functions */
void __DVDFSInit()             { }
void __DVDClearWaitingQueue()  { }
void __DVDInitWA(void)         { }
void __fstLoad(void)           { }
BOOL DVDLowRead(void* addr, u32 length, u32 offset, DVDLowCallback callback) {
    (void)addr; (void)length; (void)offset; (void)callback; return FALSE;
}
void __DVDStoreErrorCode(u32 error) { (void)error; }
BOOL DVDLowStopMotor(DVDLowCallback callback) { (void)callback; return FALSE; }
BOOL DVDLowRequestError(DVDLowCallback callback) { (void)callback; return FALSE; }
BOOL DVDLowSeek(u32 offset, DVDLowCallback callback) { (void)offset; (void)callback; return FALSE; }
BOOL DVDLowAudioBufferConfig(BOOL enable, u32 size, DVDLowCallback callback) {
    (void)enable; (void)size; (void)callback; return FALSE;
}
BOOL DVDLowReadDiskID(DVDDiskID* diskID, DVDLowCallback callback) { (void)diskID; (void)callback; return FALSE; }
BOOL DVDLowWaitCoverClose(DVDLowCallback callback) { (void)callback; return FALSE; }
BOOL __DVDCheckWaitingQueue() { return FALSE; }
BOOL DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback) { (void)subcmd; (void)callback; return FALSE; }
BOOL DVDLowAudioStream(u32 subcmd, u32 length, u32 offset, DVDLowCallback callback) {
    (void)subcmd; (void)length; (void)offset; (void)callback; return FALSE;
}
BOOL DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback) { (void)info; (void)callback; return FALSE; }
BOOL __DVDPushWaitingQueue(int idx, DVDQueue* newTail) { (void)idx; (void)newTail; return FALSE; }
void DVDLowReset() { }
BOOL DVDLowBreak() { return FALSE; }
BOOL __DVDDequeueWaitingQueue(DVDQueue* queue) { (void)queue; return FALSE; }
BOOL DVDReadDiskID(DVDCommandBlock* block, DVDDiskID* diskID, DVDCBCallback callback) {
    (void)block; (void)diskID; (void)callback; return FALSE;
}
void DVDSeekAbsAsyncPrio(DVDCommandBlock* block, void* addr, DVDCBCallback callback, s32 prio) {
    (void)block; (void)addr; (void)callback; (void)prio;
}
BOOL DVDPrepareStreamAbsAsync(DVDCommandBlock* block, u32 length, u32 offset, DVDCBCallback callback) {
    (void)block; (void)length; (void)offset; (void)callback; return FALSE;
}
BOOL DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
    (void)block; (void)addr; (void)length; (void)offset; (void)callback; return FALSE;
}
void __DVDPrepareResetAsync(DVDCBCallback callback) { (void)callback; }

} // extern "C"
