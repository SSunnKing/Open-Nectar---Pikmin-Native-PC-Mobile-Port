#include "P2JME/SimpleMessage.h"
#include "JSystem/JUtility/JUTFont.h"

namespace P2JME {

/**
 * @note Address: 0x8043DBEC
 * @note Size: 0x4
 */
SimpleMessage::SimpleMessage()
{
}

/**
 * @note Address: 0x8043DBF0
 * @note Size: 0x6C
 */
void SimpleMessage::init()
{
#ifdef PIKI_PC_PORT
	if (!gP2JMEMgr || !gP2JMEMgr->mMsgRef) {
		printf("[PC Port] Boot: SimpleMessage skipped (P2JME not ready)\n");
		mProcessor = nullptr;
		return;
	}
#endif
	mProcessor = new P2JME::TRenderingProcessor(gP2JMEMgr->mMsgRef);

	mProcessor->setFont(gP2JMEMgr->mFont);
	mProcessor->mRubyFont = gP2JMEMgr->mFont;
}

/**
 * @note Address: 0x8043DC5C
 * @note Size: 0x80
 */
void SimpleMessage::drawMessageID(Graphics& gfx, u32 lowerHalf, u32 upperHalf)
{
#ifdef PIKI_PC_PORT
	if (!mProcessor)
		return;
#endif
	mProcessor->preProcID(lowerHalf, upperHalf);

	JMessage::TRenderingProcessor* jmProc = static_cast<JMessage::TRenderingProcessor*>(mProcessor);
	jmProc->reset_(nullptr);
	jmProc->setBegin_messageID(lowerHalf, upperHalf, nullptr);
	jmProc->process(nullptr);
}

/**
 * @note Address: N/A
 * @note Size: 0x90
 * @note Stripped in every version except PAL.
 */
void SimpleMessage::drawMessageID(Graphics& gfx, char* messageID)
{
	u32 lowerHalf, upperHalf;
	convertCharToMessageID(messageID, &lowerHalf, &upperHalf);
	mProcessor->preProcID(lowerHalf, upperHalf);
	JMessage::TRenderingProcessor* jmProc = static_cast<JMessage::TRenderingProcessor*>(mProcessor);
	u32 renderLowerHalf, renderUpperHalf;
	renderUpperHalf = upperHalf;
	renderLowerHalf = lowerHalf;
	jmProc->reset_(nullptr);
	jmProc->setBegin_messageID(renderLowerHalf, renderUpperHalf, nullptr);
	jmProc->process(nullptr);
}

/**
 * @note Address: N/A
 * @note Size: 0x50
 * @note Stripped in every version except PAL.
 */
void SimpleMessage::locate(int x, int y)
{
	mProcessor->setLocate(x, y);
}

} // namespace P2JME
