#include <stdint.h>
#include "jaudio/cmdqueue.h"

#include "jaudio/audiomesg.h"
#include "jaudio/jammain_2.h"
#include "jaudio/playercall.h"
#include "jaudio/verysimple.h"
#include <stddef.h>

/**
 * @TODO: Documentation
 */
static void Add_Queue(CmdQueue* cmdQueue)
{
	CmdQueue* curr;
	CmdQueue* prev;

	curr           = queue_list;
	cmdQueue->next = NULL;
	if (!queue_list) {
		queue_list = cmdQueue;
		return;
	}

	while (prev = curr->next) {
		curr = prev;
	}
	curr->next = cmdQueue;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000048
 */
void Del_Queue(CmdQueue*)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
void Jal_AddCmdQueue(CmdQueue* cmdQueue, seqp_* track, u8 portId)
{
	BOOL restoreInterrupts;

	restoreInterrupts = OSDisableInterrupts();
	cmdQueue->track   = track;
	cmdQueue->mPortId = portId;
	Jac_InitMessageQueue(&cmdQueue->msgQueue, cmdQueue->messages, ARRAY_SIZE(cmdQueue->messages));
	Add_Queue(cmdQueue);
	OSRestoreInterrupts(restoreInterrupts);
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000044
 */
void Jal_RemoveCmdQueue(void)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000024
 */
void Jal_SendCmdQueue(CmdQueue* queue, u16 msg)
{
	Jac_SendMessageBlock(&queue->msgQueue, (OSMessage)msg);
}

/**
 * @TODO: Documentation
 */
void Jal_SendCmdQueue_Noblock(CmdQueue* queue, u16 msg)
{
	CmdQueue** REF_queue;
	u16* REF_msg;

	REF_queue = &queue;
	REF_msg   = &msg;
	Jac_SendMessage(&queue->msgQueue, (OSMessage)msg);

#if defined(VERSION_GPIJ01_01)
	int padding;
#endif
}

/**
 * @TODO: Documentation
 */
void Jal_SendCmdQueue_Force(CmdQueue* queue, u16 msg)
{
	OSMessage outMsg;
	if (!Jac_SendMessage(&queue->msgQueue, (OSMessage)msg)) {
		Jac_ReceiveMessage(&queue->msgQueue, &outMsg);
		Jac_SendMessage(&queue->msgQueue, (OSMessage)msg);
	}
}

/**
 * @TODO: Documentation
 */
static s32 Jal_FrameWork(void* callbackArg)
{
	CmdQueue* curr;
	OSMessage message;
	s32 result = 0;

	for (curr = queue_list; curr; curr = curr->next) {
#if defined(VERSION_GPIJ01_01) || defined(VERSION_G98P01_PIKIDEMO) || defined(VERSION_DPIJ01_PIKIDEMO)
		if (Jam_CheckPortIndirect(curr->track, curr->mPortId, 1) == 1)
#else
		if (Jam_CheckPortAppDirect(curr->track, curr->mPortId, 1) == 1)
#endif
		{
			if (Jac_ReceiveMessage_Fast(&curr->msgQueue, &message) == 1) {
				result = Jam_WritePortAppDirect(curr->track, curr->mPortId, (u16)(uintptr_t)message);
			}
		}
	}

	return result;
}

/**
 * @TODO: Documentation
 */
void Jal_CmdQueue_Init(void)
{
	Jac_RegisterPlayerCallback(Jal_FrameWork, NULL);
}
