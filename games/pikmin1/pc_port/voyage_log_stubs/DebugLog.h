#ifndef _DEBUGLOG_H
#define _DEBUGLOG_H

// Host stand-in so resultFlag.cpp can be linked into pc_voyage_log_test
// without the game's System console. Stream.h is what the real DebugLog.h
// pulls in, and saveCard/loadCard need a complete RandomAccessStream.
#include "Stream.h"

#define DEFINE_PRINT(name)
#define DEFINE_ERROR(line)
#define PRINT(...) ((void)0)
#define ERROR(...) ((void)0)

#endif
