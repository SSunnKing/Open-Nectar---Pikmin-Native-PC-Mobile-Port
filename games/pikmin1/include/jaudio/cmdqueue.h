#ifndef _JAUDIO_CMDQUEUE_H
#define _JAUDIO_CMDQUEUE_H

#include "jaudio/audiomesg.h"
#include "jaudio/jammain_2.h"

#include "types.h"

BEGIN_SCOPE_EXTERN_C

typedef struct cmdqueue_ CmdQueue;

struct cmdqueue_ {
	Jac_MessageQueue msgQueue; // _00
	/*
	 * This was previously modelled as one OSMessage plus console-sized byte
	 * padding.  OSMessage is a pointer on the host, so a 16-entry queue would
	 * overwrite `track` and `next`.
	 */
	OSMessage messages[16];     // _20-_5F on GameCube
	seqp_* track;              // _60
	u8 mPortId;                // _64
	CmdQueue* next;            // _68;
};

extern CmdQueue* queue_list; // Pointer to the start of a linked list

void Jal_AddCmdQueue(CmdQueue* cmdQueue, seqp_* track, u8 portId); // TODO: Types uncertain
void Jal_SendCmdQueue_Noblock(CmdQueue* queue, u16 msg);
void Jal_SendCmdQueue_Force(CmdQueue* queue, u16 msg);
void Jal_SendCmdQueue(CmdQueue* queue, u16 msg);
void Jal_CmdQueue_Init(void);

END_SCOPE_EXTERN_C

#endif
