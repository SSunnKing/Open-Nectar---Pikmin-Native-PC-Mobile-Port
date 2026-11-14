#include "DebugLog.h"
#include "P2D/TextBox.h"
#include "SoundMgr.h"
#include "gameflow.h"
#include "jaudio/verysimple.h"
#include "zen/EffectMgr2D.h"
#include "zen/ogFileSelect.h"
#include "zen/ogNitaku.h"
#if defined(PIKI_PC_PORT)
#include "pc_file_pointer.h"
#include "pc_gfx.h"
#include "pc_menu_click.h"
#endif

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("OgFileSelectSection")

/**
 * @todo: Documentation
 */
void zen::ogScrFileSelectMgr::setOperateMode_Copy()
{
	SetTitleMsg(SelectSourceFileForCopy);

	for (int i = 0; i < 3; i++) {
		if (i != mCurrSlotIdx) {
			mCopyTargetFileIndex = i;
			break;
		}
	}

	PRINT("cL(%d,%d)  cR(%d,%d)\n", mCopyCursorLPosX[mCopyTargetFileIndex], mCopyCursorLPosY[mCopyTargetFileIndex],
	      mCopyCursorRPosX[mCopyTargetFileIndex], mCopyCursorRPosY[mCopyTargetFileIndex]);

	mCopyLeftCursor.init(mCopyCursorsScreen, mOperationCursorsScreenPane, 'z00l', mCopyCursorLPosX[mCopyTargetFileIndex],
	                     mCopyCursorLPosY[mCopyTargetFileIndex]);
	mCopyRightCursor.init(mCopyCursorsScreen, mOperationCursorsScreenPane, 'z00r', mCopyCursorRPosX[mCopyTargetFileIndex],
	                      mCopyCursorRPosY[mCopyTargetFileIndex]);

	mOperationCursorsScreenPane->show();
	paneOnOffXY(false);
	mIsCopyTargetSelectionActive = true;
	mCopyLeftCursor.scale(1.0f, 0.25f);
	mCopyRightCursor.scale(1.0f, 0.25f);

	STACK_PAD_VAR(4);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00006C
 */
void zen::ogScrFileSelectMgr::MoveCpyCursor(int id, f32 a2)
{
	mCopyLeftCursor.move(mCopyCursorLPosX[id], mCopyCursorLPosY[id], a2);
	mCopyRightCursor.move(mCopyCursorRPosX[id], mCopyCursorRPosY[id], a2);
}

/**
 * @todo: Documentation
 */
void zen::ogScrFileSelectMgr::CopyEffectStart()
{
	Vector3f effPos;
	Vector3f attractorPos;

	P2DPane* pane1 = mIconOnyonPanes[mCurrSlotIdx];
	P2DPane* pane2 = mIconOnyonDestPanes[mCopyTargetFileIndex];

	effPos.set(0.0f, 0.0f, 0.0f);
	effPos.x = f32(pane1->getPosH()) + f32(pane1->getWidth()) / 2.0f;
	effPos.y = 480.0f - (f32(pane1->getPosV()) + f32(pane1->getHeight()) / 2.0f);

	pane2->getDispPos(&attractorPos);
	attractorPos.y = 480.0f - attractorPos.y;

	mFileCopyEffectOnyon = mFxMgr->create(EFF2D_Unk42, effPos, nullptr, nullptr);
	mFileCopyEffectOnyon->setNewtonField(attractorPos, 0.0046f, true);

	pane1 = mIconPikminPanes[mCurrSlotIdx];
	pane2 = mIconPikminDestPanes[mCopyTargetFileIndex];

	effPos.set(0.0f, 0.0f, 0.0f);
	effPos.x = f32(pane1->getPosH()) + f32(pane1->getWidth()) / 2.0f;
	effPos.y = 480.0f - (f32(pane1->getPosV()) + f32(pane1->getHeight()) / 2.0f);

	pane2->getDispPos(&attractorPos);
	attractorPos.y = 480.0f - attractorPos.y;

	mFileCopyEffectPikminGroup = mFxMgr->create(EFF2D_Unk41, effPos, nullptr, nullptr);
	mFileCopyEffectPikminGroup->setNewtonField(attractorPos, 0.0046f, true);
}

/**
 * @todo: Documentation
 */
void zen::ogScrFileSelectMgr::OperateCopy(Controller* input)
{
	if (mIsCopyCompleteMessageActive) {
		mOpCompletePane->show();
		mCopyAnimTimer -= gsys->getFrameTime();
		if (mCopyAnimTimer < 0.0f) {
			mIsCopyCompleteMessageActive = false;
			setOperateMode(Normal);
			mOpCompletePane->hide();
		}
		return;
	}

	if (mIsCopyingFileActive) {
		mOpInProgressPane->show();
		mCardAccessIcon->show();

		f32 rate = mCopyAnimTimer;
		int x, y;
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01)
		if (rate > 2.25f) {
			y = mCardAccessIcon->getPosV();
			x = f32((rate - 2.25f) / 0.75) * 640.0f;
		} else if (rate < 0.75f) {
			y = mCardAccessIcon->getPosV();
			x = f32((rate - 0.75f) / 0.75) * 640.0f;
			mCardAccessIcon->move(x, y);
		} else {
			x = mCardAccessIcon->getPosH();
			y = mCardAccessIcon->getPosV();
		}
#else
		if (rate > 2.7f) {
			y = mCardAccessIcon->getPosV();
			x = (rate - 2.7f) / 0.3f * 640.0f;
		} else if (rate < 0.3f) {
			y = mCardAccessIcon->getPosV();
			x = (rate - 0.3f) / 0.3f * 640.0f;
			mCardAccessIcon->move(x, y);
		} else {
			x = mCardAccessIcon->getPosH();
			y = mCardAccessIcon->getPosV();
		}
#endif

		mCardAccessIcon->move(x, y);
		mCopyAnimTimer -= gsys->getFrameTime();

		if (mCopyAnimTimer < 0.0f && gameflow.mMemoryCard.hasCardFinished()) {
			seSystem->playSysSe(ogEnumFix(SYSSE_CARDOK, JACSYS_CardOK));
			copyCardInfosSub();
			ChkNewData();
			mIsCopyingFileActive         = false;
			mIsCopyCompleteMessageActive = true;
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01)
			mCopyAnimTimer = 2.0f;
#else
			mCopyAnimTimer = 1.0f;
#endif
			mOpInProgressPane->hide();
			mCardAccessIcon->hide();
			mFileCopyEffectOnyon->finish();
			mFileCopyEffectPikminGroup->finish();
		}
		return;
	}

	if (mIsCopyTargetSelectionActive) {
#if defined(PIKI_PC_PORT)
		// The destination is one of the other two cards. Hover moves the cursor
		// onto it. A click opens the yes/no, as A does. The source card is not
		// a destination. Right click cancels, as B does. Claimed, so the click
		// is not also A through a mouse binding.
		bool pointerConfirm = false;
		bool pointerBack    = false;
		pc_menu_click_claim_pointer_menu();
		int drawableW = 0, drawableH = 0;
		pc_gfx_get_drawable_size(&drawableW, &drawableH);
		float px = 0.0f, py = 0.0f;
		int tag = -1;
		if (drawableW > 0 && drawableH > 0 && pc_menu_click_read_pointer_normalized(&px, &py)
		    && pc_menu_click_targets_hit_row(PC_FILE_CARD_TARGET_KIND, px * drawableW, py * drawableH, mCopyTargetFileIndex,
		                                    PC_FILE_CARD_HOLD_PX, &tag)) {
			const PcFileCopyPointer hover = pc_file_copy_pointer(mCurrSlotIdx, mCopyTargetFileIndex, tag, false);
			if (hover.action == PC_FILE_COPY_MOVE) {
				seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
				mCopyTargetFileIndex = hover.slot;
				MoveCpyCursor(mCopyTargetFileIndex, 0.25f);
			}
		}
		tag = -1;
		if (drawableW > 0 && drawableH > 0 && pc_menu_click_pending(&px, &py)
		    && pc_menu_click_targets_hit_row(PC_FILE_CARD_TARGET_KIND, px * drawableW, py * drawableH, mCopyTargetFileIndex,
		                                    PC_FILE_CARD_HOLD_PX, &tag)) {
			// Take it either way, so a click on the source does not stay pending.
			pc_menu_click_take(nullptr, nullptr);
			const PcFileCopyPointer hit = pc_file_copy_pointer(mCurrSlotIdx, mCopyTargetFileIndex, tag, true);
			if (hit.action == PC_FILE_COPY_CONFIRM) {
				mCopyTargetFileIndex = hit.slot;
				MoveCpyCursor(mCopyTargetFileIndex, 0.25f);
				pointerConfirm = true;
			}
		}
		pointerBack = pc_menu_click_take_back();
		if (input->keyClick(KBBTN_B) || pointerBack) {
#else
		if (input->keyClick(KBBTN_B)) {
#endif
			seSystem->playSysSe(ogEnumFix(SYSSE_CANCEL, JACSYS_Cancel));
			setOperateMode(Normal);
		} else if (input->keyClick(KBBTN_A)
#if defined(PIKI_PC_PORT)
		           || pointerConfirm
#endif
		) {
			seSystem->playSysSe(ogEnumFix(SYSSE_DECIDE1, JACSYS_Decide1));
			mIsCopyTargetSelectionActive = false;
			mYesNoDialogPane->show();
			mYesNoDialogImage->show();
			mYesNoDialogImage->setAlpha(mYesNoDialogPromptText->getAlphaChar());
			mCopyLeftCursor.scale(0.0f, 0.25f);
			mCopyRightCursor.scale(0.0f, 0.25f);
			OpenYesNoWindow();
			SetTitleMsg(ConfirmFileCopy);
		} else if (input->keyClick(KBBTN_MSTICK_LEFT)) {
			for (int i = 0; i < 3; i++) {
				if (i != mCurrSlotIdx) {
					seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
					mCopyTargetFileIndex = i;
					break;
				}
			}
			MoveCpyCursor(mCopyTargetFileIndex, 0.25f);
		} else if (input->keyClick(KBBTN_MSTICK_RIGHT)) {
			for (int i = 2; i >= 0; i--) {
				if (i != mCurrSlotIdx) {
					seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
					mCopyTargetFileIndex = i;
					break;
				}
			}
			MoveCpyCursor(mCopyTargetFileIndex, 0.25f);
		}

		return;
	}

	ogNitakuMgr::NitakuStatus status = mNitaku->update(input);
	if (status >= 4) {
		CloseYesNoWindow();
	}
	if (status == ogNitakuMgr::Status_4) {
		seSystem->playSysSe(ogEnumFix(SYSSE_CANCEL, JACSYS_Cancel));
		setOperateMode(Normal);
	} else if (status == ogNitakuMgr::ExitSuccess) {
		mIsCopyingFileActive = true;
		mCopyAnimTimer       = 3.0f;
		seSystem->playSysSe(ogEnumFix(SYSSE_CARDACCESS, JACSYS_CardAccess));
		gameflow.mMemoryCard.copyFile(mCardInfo[mCurrSlotIdx], mCardInfo[mCopyTargetFileIndex]);
		CopyEffectStart();
		mConfirmCopyText->hide();
	} else if (status == ogNitakuMgr::ExitFailure) {
		setOperateMode(Normal);
	}

	STACK_PAD_VAR(4);
}
