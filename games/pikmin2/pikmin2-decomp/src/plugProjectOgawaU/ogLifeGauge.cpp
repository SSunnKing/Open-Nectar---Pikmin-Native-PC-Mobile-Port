#include "pc_p2_captains.h"
#include "p2_host_restimg.h"
#include <math.h>
#include <string.h>
#include "og/Screen/callbackNodes.h"
#include "og/Screen/NaviLifeGauge.h"
#include "og/Screen/ogScreen.h"
#include "og/newScreen/ogUtil.h"
#include "LifeGaugeMgr.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "System.h"
#include "og/Sound.h"
#include "trig.h"

namespace og {
namespace Screen {

/**
 * @note Address: 0x80306038
 * @note Size: 0x158
 */
CallBack_LifeGauge::CallBack_LifeGauge()
{
	mData                = nullptr;
	mNaviLifeRatio       = 1.0f;
	mWidthOrRadiusMaybe  = 17.0f;
	mOffsetX             = 0.0f;
	mOffsetY             = 0.0f;
	mLowLifeSoundTimer   = 0.0f;
	mLowLifeSoundRateMod = 1.0f;
	mScreenOwner         = nullptr;
	mPin1                = nullptr;
	mPin2                = nullptr;
	mNa_i                = nullptr;
	mLi_i                = nullptr;

	mLifeGauge           = new LifeGauge;
	mCurrentAngleSin     = 0.0f;
	mCurrentAngleCos     = 0.0f;
	mNaviIconOffset.x    = 0.0f;
	mNaviIconOffset.y    = 0.0f;
	mLifeIconOffset.x    = 0.0f;
	mLifeIconOffset.y    = 0.0f;
	mCurrentAngle        = 0.0f;
	mIsActiveNavi        = 1;
	mIsActiveNaviOld     = mIsActiveNavi;
	mPaneOlimarIcon      = nullptr;
	mPaneLouieIcon       = nullptr;
	mPanePresidentIcon   = nullptr;
	mPaneOlimarBorder    = nullptr;
	mPaneLouieBorder     = nullptr;
	mPanePresidentBorder = nullptr;

	mAngleMgr = new AngleMgr;
	mScaleMgr = new ScaleMgr;

	mCanNaviChange = false;
	mMoveTimer     = 0.0f;

	mPcOwn.mValid = false;
	mPcCharacter  = -1;
	// Un HUD nuevo: los retratos de otro HUD apuntan a texturas ya liberadas.
	sPcOriginal[0].mValid = false;
	sPcOriginal[1].mValid = false;
	sPcPresidentTex       = nullptr;
}

/**
 * @note Address: 0x80306200
 * @note Size: 0x2C4
 */
void CallBack_LifeGauge::init(P2DScreen::Mgr* mgr, DataNavi* data, LifeGaugeType lifeGaugeType)
{
	if (!mData) {
		mData          = data;
		mNaviLifeRatio = data->mNaviLifeRatio;
		mLifeGauge->init(128);
		mLifeGauge->mCurrentSegmentNum = 128.0f * mNaviLifeRatio;
		mScreenOwner                   = mgr;

		mPin1 = TagSearch(mgr, 'pin1');
		mPin2 = static_cast<J2DPicture*>(TagSearch(mgr, 'pin2'));
		mNa_i = TagSearch(mgr, 'na_i');
		mLi_i = TagSearch(mgr, 'li_i');

		mNaviIconOffset.x = mNa_i->mTranslateX;
		mNaviIconOffset.y = mNa_i->mTranslateY;

		mLifeIconOffset.x = mLi_i->mTranslateX;
		mLifeIconOffset.y = mLi_i->mTranslateY;

		mPin1->hide();
		mPin2->hide();
		mPaneOlimarIcon      = static_cast<J2DPicture*>(mgr->search(MC8("navi_i")));
		mPaneLouieIcon       = static_cast<J2DPicture*>(mgr->search(MC8("navi2_i")));
		mPanePresidentIcon   = static_cast<J2DPicture*>(mgr->search(MC8("navi3_i")));
		mPaneOlimarBorder    = static_cast<J2DPicture*>(mgr->search('navi'));
		mPaneLouieBorder     = static_cast<J2DPicture*>(mgr->search(MC8("navi2")));
		mPanePresidentBorder = static_cast<J2DPicture*>(mgr->search(MC8("navi3")));
#ifdef PIKI_PC_PORT
		// Selector de capitanes: los HUD de 2P y VS no tienen 'navi_i'; su retrato
		// es la otra imagen de 'na_i' junto al fondo 'navi' ('cap' o sin nombre).
		if (!mPaneOlimarIcon && mNa_i) {
			for (J2DPane* child = mNa_i->getFirstChildPane(); child; child = child->getNextChildPane()) {
				if (child->getTypeID() == PANETYPE_Picture && child != mPaneOlimarBorder && child->mTag != 'pin1') {
					mPaneOlimarIcon = static_cast<J2DPicture*>(child);
					break;
				}
			}
		}
#endif

		if (mPaneOlimarIcon && mPaneOlimarBorder && mPaneOlimarIcon->getTexture(0) && mPaneOlimarBorder->getTexture(0)) {
			mPcOwn.mIcon      = mPaneOlimarIcon->getTexture(0)->mTexInfo;
			mPcOwn.mBorder    = mPaneOlimarBorder->getTexture(0)->mTexInfo;
			mPcOwn.mIconWhite = mPaneOlimarIcon->getWhite();
			mPcOwn.mIconBlack = mPaneOlimarIcon->getBlack();
			mPcOwn.mWhite     = mPaneOlimarBorder->getWhite();
			mPcOwn.mBlack     = mPaneOlimarBorder->getBlack();
			mPaneOlimarBorder->getCornerColor(mPcOwn.mCorner);
			mPcOwn.mValid = true;
		}

		if (mPaneLouieBorder && mPanePresidentBorder) {
			mPaneLouieBorder->hide();
			mPanePresidentBorder->hide();
			setType(lifeGaugeType);
		}

		if (data->mActiveNaviID) {
			mCurrentAngle    = 0.0f;
			mIsActiveNavi    = true;
			mIsActiveNaviOld = mIsActiveNavi;
			mAngleMgr->init(0.0f, msVal._08, msVal._0C);
		} else {
			mCurrentAngle    = PI;
			mIsActiveNavi    = false;
			mIsActiveNaviOld = mIsActiveNavi;
			mAngleMgr->init(PI, msVal._08, msVal._0C);
		}

		mCanNaviChange = false;
	}
}

/**
 * @note Address: 0x803064C4
 * @note Size: 0x434
 */
void CallBack_LifeGauge::setType(LifeGaugeType lifeGaugeType)
{
	J2DPicture::TCornerColor color;
	mLifeGaugeType = lifeGaugeType;

	switch (lifeGaugeType) {
	case LIFEGAUGE_LOUIE: {
		JUTTexture* texture1 = mPaneLouieIcon->getTexture(0);
		mPaneOlimarIcon->changeTexture(texture1->mTexInfo, 0);

		mPaneOlimarBorder->setWhite(mPaneLouieBorder->getWhite());
		mPaneOlimarBorder->setBlack(mPaneLouieBorder->getBlack());

		mPaneLouieBorder->getCornerColor(color);
		mPaneOlimarBorder->setCornerColor(color);
		break;

			}	case LIFEGAUGE_PRESIDENT: {
		JUTTexture* texture2 = mPanePresidentIcon->getTexture(0);
		mPaneOlimarIcon->changeTexture(texture2->mTexInfo, 0);

		mPaneOlimarBorder->setWhite(mPanePresidentBorder->getWhite());
		mPaneOlimarBorder->setBlack(mPanePresidentBorder->getBlack());

		mPanePresidentBorder->getCornerColor(color);
		mPaneOlimarBorder->setCornerColor(color);
		break;
			}	}
}

// Selector de capitanes: disco de bordes suaves (IA8 32x32, en memoria) para el
// fondo del retrato de los capitanes Pikmin; el HUD no trae ninguno liso.
static const ResTIMG* pcDiscTexture()
{
	static const int kSize = 32;
	// Memoria estática, no del montón del juego: el montón de la sección se
	// vacía entre subniveles y el puntero quedaba colgando.
	alignas(32) static u8 sImage[0x20 + kSize * kSize * 2];
	static bool sBuilt = false;
	if (!sBuilt) {
		sBuilt       = true;
		ResTIMG* img = reinterpret_cast<ResTIMG*>(sImage);
		memset(img, 0, 0x20);
		img->mTextureFormat   = GX_TF_IA8;
		img->mSizeX           = kSize;
		img->mSizeY           = kSize;
		img->mWrapS           = GX_CLAMP;
		img->mWrapT           = GX_CLAMP;
		img->mMinFilterType   = GX_LINEAR;
		img->mMagFilterType   = GX_LINEAR;
		img->mTotalImageCount = 1;
		img->mImageDataOffset = 0x20;
		img->_19              = P2_RESTIMG_HOST_ENDIAN_MARK;

		// IA8 en bloques de 4x4 píxeles; cada píxel es alfa y luego intensidad.
		u8* data = sImage + 0x20;
		for (int y = 0; y < kSize; y++) {
			for (int x = 0; x < kSize; x++) {
				f32 dx    = (x + 0.5f) - kSize / 2.0f;
				f32 dy    = (y + 0.5f) - kSize / 2.0f;
				f32 edge  = kSize / 2.0f - sqrtf(dx * dx + dy * dy);
				f32 alpha = edge >= 1.5f ? 1.0f : (edge <= 0.0f ? 0.0f : edge / 1.5f);
				int block = (y / 4) * (kSize / 4) + (x / 4);
				int texel = block * 16 + (y % 4) * 4 + (x % 4);
				data[texel * 2]     = (u8)(alpha * 255.0f);
				data[texel * 2 + 1] = 255;
			}
		}
	}
	return reinterpret_cast<const ResTIMG*>(sImage);
}

CallBack_LifeGauge::PcPortrait CallBack_LifeGauge::sPcOriginal[2];
const ResTIMG* CallBack_LifeGauge::sPcPresidentTex = nullptr;

// Selector de capitanes: retrato del personaje. Olimar, Louie y el Presidente
// salen de los panes del .blo (los suyos en la historia; en 2P y VS, el del
// capitán que lo lleva de serie). Los Pikmin usan el icono de hoja del HUD
// (PikiIconTextureName, cargado por CallBack_CatchPiki) sobre un fondo liso de
// su color.
void CallBack_LifeGauge::pcSetCharacter(int character, int naviIndex)
{
	character = pc_p2_character_base(character); // los propios llevan el retrato de Olimar
	if (character == mPcCharacter || !mPcOwn.mValid) {
		return;
	}
	const bool storyPanes = mPaneLouieBorder && mPanePresidentBorder;
	if (!storyPanes && naviIndex >= 0 && naviIndex < 2) {
		sPcOriginal[naviIndex] = mPcOwn;
	}

	const bool louieLike = character == PC_P2_CHAR_LOUIE || character == PC_P2_CHAR_PRESIDENT;
	if (louieLike && storyPanes) {
		applyPcPortrait(mPcOwn); // fondo de Olimar; setType pone el de Louie o el Presidente
		setType(character == PC_P2_CHAR_LOUIE ? LIFEGAUGE_LOUIE : LIFEGAUGE_PRESIDENT);
		mPcCharacter = character;
		return;
	}
	if (!pc_p2_character_is_pikmin(character)) {
		// Olimar, o Louie/Presidente en 2P y VS: el retrato del capitán que lo
		// lleva de serie; el Presidente, con su cara sobre el fondo de Louie.
		const PcPortrait& src = storyPanes ? mPcOwn : sPcOriginal[louieLike ? 1 : 0];
		if (!src.mValid) {
			return; // el HUD del otro jugador aún no está: se reintenta
		}
		applyPcPortrait(src);
		if (character == PC_P2_CHAR_PRESIDENT && sPcPresidentTex) {
			mPaneOlimarIcon->changeTexture(sPcPresidentTex, 0);
		}
		mLifeGaugeType = louieLike ? LIFEGAUGE_LOUIE : LIFEGAUGE_OLIMAR;
		mPcCharacter   = character;
		return;
	}

	static const int kLeafIcon[PC_P2_CHAR_COUNT] = { 0, 0, 0, 4, 7, 1, 10, 13, 16 }; // rp, yp, bp, blp, wp _l64, cha_l
	// Se busca en los archivos montados ahora: PikiIconTextureResTIMG guarda
	// punteros del HUD de la pantalla anterior, que en un cambio de subnivel ya
	// está liberado cuando ObjCave hace su primer update.
	ResTIMG* pikminTex = static_cast<ResTIMG*>(JKRArchive::getGlbResource('TIMG', PikiIconTextureName[kLeafIcon[character]], nullptr));
	if (!pikminTex) {
		return; // el HUD aún no ha cargado los iconos: se reintenta
	}
	mLifeGaugeType = LIFEGAUGE_OLIMAR;

	// Detrás, el borde hace de fondo: un disco liso del color del Pikmin,
	// aclarado. Delante, el icono del Pikmin, escalado desde su centro hasta
	// el tamaño del borde.
	u8 rgb[3];
	pc_p2_character_color(character, rgb);
	JUtility::TColor flat((rgb[0] + 2 * 255) / 3, (rgb[1] + 2 * 255) / 3, (rgb[2] + 2 * 255) / 3, mPcOwn.mWhite.a);
	mPaneOlimarBorder->changeTexture(pcDiscTexture(), 0);
	mPaneOlimarBorder->setWhite(flat);
	mPaneOlimarBorder->setBlack(JUtility::TColor(flat.r, flat.g, flat.b, mPcOwn.mBlack.a));
	mPaneOlimarBorder->setCornerColor(mPcOwn.mCorner);

	mPaneOlimarIcon->changeTexture(pikminTex, 0);
	mPaneOlimarIcon->setWhite(JUtility::TColor(255, 255, 255, mPcOwn.mIconWhite.a));
	mPaneOlimarIcon->setBlack(JUtility::TColor(0, 0, 0, mPcOwn.mIconBlack.a));
	const f32 iconWidth = mPaneOlimarIcon->getWidth();
	mPaneOlimarIcon->rotate(iconWidth / 2, mPaneOlimarIcon->getHeight() / 2, J2DROTATE_Z, 0.0f);
	mPaneOlimarIcon->updateScale(iconWidth > 0.0f ? mPaneOlimarBorder->getWidth() / iconWidth : 1.0f);
	mPcCharacter = character;
}

// Selector de capitanes: pone un retrato guardado en los panes de este HUD.
void CallBack_LifeGauge::applyPcPortrait(const PcPortrait& portrait)
{
	mPaneOlimarBorder->changeTexture(portrait.mBorder, 0);
	mPaneOlimarBorder->setWhite(portrait.mWhite);
	mPaneOlimarBorder->setBlack(portrait.mBlack);
	mPaneOlimarBorder->setCornerColor(portrait.mCorner);
	mPaneOlimarIcon->changeTexture(portrait.mIcon, 0);
	mPaneOlimarIcon->setWhite(portrait.mIconWhite);
	mPaneOlimarIcon->setBlack(portrait.mIconBlack);
	mPaneOlimarIcon->updateScale(1.0f);
}

/**
 * @note Address: 0x803068F8
 * @note Size: 0x78
 */
void CallBack_LifeGauge::setOffset(f32 x, f32 y)
{
	if (!mPane) {
		JUT_PANICLINE(243, "ERR! : NULL Pane !!!\n");
	} else {
		mOffsetX = (mPane->getWidth() / 2) + x;
		mOffsetY = (mPane->getHeight() / 2) + y;
	}
}

/**
 * @note Address: 0x80306970
 * @note Size: 0x318
 */
void CallBack_LifeGauge::moveIcon()
{
	mIsActiveNaviOld = mIsActiveNavi;
	if (mData) {
		mIsActiveNavi = mData->mActiveNaviID;
	}

	u8 cNavi = mIsActiveNavi;
	if (cNavi != mIsActiveNaviOld) {
		if (cNavi) {
			mAngleMgr->init(mCurrentAngle, msVal._08, msVal._0C);
			mAngleMgr->chase(0.0f, msVal._04);
			mCanNaviChange = true;
			mMoveTimer     = msVal._00;
		} else {
			mAngleMgr->init(mCurrentAngle, msVal._08, msVal._0C);
			mAngleMgr->chase(PI, msVal._04);
		}
	} else if (cNavi && mCanNaviChange) {
		mMoveTimer -= sys->mDeltaTime;
		if (mMoveTimer < 0.0f) {
			mCanNaviChange = false;
			ogSound->setNaviChange(mLifeGaugeType);
			mScaleMgr->up(0.4f, 30.0f, 0.6f, 0.0f);
		}
	}

	mCurrentAngle    = mAngleMgr->calc();
	mCurrentAngleSin = sinf(mCurrentAngle) * 50.0f;
	mCurrentAngleCos = cosf(mCurrentAngle) * 30.0f;

	f32 scale = (cosf(mCurrentAngle) + 1.0f) / 2.0f;
	scale     = ((1.0f - scale) * 0.7f + scale);
	scale     = scale * mScaleMgr->calc();

	mNa_i->move(mNaviIconOffset.x - mCurrentAngleSin, mNaviIconOffset.y - 30.0f + mCurrentAngleCos);
	mLi_i->move(scale * (mLifeIconOffset.x - mNaviIconOffset.x) + (mNaviIconOffset.x - mCurrentAngleSin),
	            (mLifeIconOffset.y - 30.0f) + mCurrentAngleCos);
	mNa_i->updateScale(scale);
	mLi_i->updateScale(scale);
}

/**
 * @note Address: 0x80306C88
 * @note Size: 0x26C
 */
void CallBack_LifeGauge::update()
{
	if (!og::newScreen::checkMovieActive()) {
		moveIcon();
		mNaviLifeRatio = mData->mNaviLifeRatio;
		mLifeGauge->update(mNaviLifeRatio);
		if (mNaviLifeRatio < 0.5f) {
			mPin1->show();
			mPin2->show();
			mLowLifeSoundTimer += mLowLifeSoundRateMod * sys->mDeltaTime;
			if (mLowLifeSoundTimer >= 1.0f) {
				mLowLifeSoundTimer = 0.0f;
				if (mNaviLifeRatio > 0.0f && mIsActiveNavi) {
					ogSound->setLifeDanger();
				}
			}
			// inline perhaps?
			f32 timer = mLowLifeSoundTimer;
			f32 scale = mLowLifeSoundTimer / 2.f + 1.0f;
			mPin1->rotate((mPin1->getWidth()) / 2.f, (mPin1->getHeight()) / 2.f, J2DROTATE_Z, 0.0f);
			mPin1->updateScale(scale);
			mPin1->setAlpha(-(timer * 255.0f - 255.0f));

			f32 timerScale = -(mLowLifeSoundRateMod * 0.3f - mLowLifeSoundTimer);
			if (timerScale < 0.0f)
				timerScale += 1.0f;

			scale = timerScale / 2.f + 1.0f;
			mPin2->rotate((mPin2->getWidth()) / 2.f, (mPin2->getHeight()) / 2.f, J2DROTATE_Z, 0.0f);
			mPin2->updateScale(scale);
			mPin2->setAlpha(-(timerScale * 255.0f - 255.0f));
			if (mNaviLifeRatio <= 0.0f) {
				mNa_i->hide();
				mLi_i->hide();
			} else {
				mNa_i->show();
				mLi_i->show();
			}
		} else {
			mPin1->hide();
			mPin2->hide();
		}
	}
}

/**
 * @note Address: 0x80306EF4
 * @note Size: 0xB4
 */
void CallBack_LifeGauge::draw(Graphics& gfx, J2DGrafContext& context)
{
	if (mNaviLifeRatio > 0.0f) {
		LifeGauge::initLifeGaugeDraw();
		Mtx concatMtx;
		PSMTXConcat(context.mPosMtx, mPane->mGlobalMtx, concatMtx);
		GXLoadPosMtxImm(concatMtx, 0);

		mLifeGauge->draw(mWidthOrRadiusMaybe, mOffsetX, mOffsetY);

		if (mNaviLifeRatio < 0.5f) {
			mPin2->draw(mOffsetX, mOffsetY, false, false, false);
		}
	}
}

/**
 * @note Address: 0x80306FA8
 * @note Size: 0x118
 */
void NaviLifeGauge::setCallBack(DataNavi* data, CallBack_LifeGauge::LifeGaugeType lifeGaugeType)
{
	if (!mDataNavi) {
		mDataNavi = data;
		setAlphaScreen(this);
		mCallBackLifeGauge = new CallBack_LifeGauge;
		mCallBackLifeGauge->init(this, data, lifeGaugeType);
		addCallBack('life', mCallBackLifeGauge);
		mCallBackDrawAfter = setCallBack_DrawAfter(this, 'mete');

		CallBack_LifeGauge* cback = mCallBackLifeGauge;
		J2DPane* pane             = cback->mPane;
		if (!pane) {
			JUT_PANICLINE(243, "ERR! : NULL Pane !!!\n");
		} else {
			f32 temp        = 0.0f;
			cback->mOffsetX = (pane->getWidth()) / 2 + temp;
			J2DPane* pane2  = cback->mPane;
			cback->mOffsetY = (pane2->getHeight()) / 2 + temp;
		}
	}
}

/**
 * @note Address: 0x803070C0
 * @note Size: 0x2C
 */
void NaviLifeGauge::setType(CallBack_LifeGauge::LifeGaugeType lifeGaugeType)
{
	if (mCallBackLifeGauge) {
		mCallBackLifeGauge->setType(lifeGaugeType);
	}
}

void NaviLifeGauge::pcSetCharacter(int character, int naviIndex)
{
	if (mCallBackLifeGauge) {
		mCallBackLifeGauge->pcSetCharacter(character, naviIndex);
	}
}

/**
 * @note Address: 0x803070EC
 * @note Size: 0x5C
 */
void NaviLifeGauge::update()
{
	P2DScreen::Mgr::update();
	if (mDataNavi->mNaviLifeRatio > 0.0f) {
		mCallBackDrawAfter->mIsVisible = true;
	} else {
		mCallBackDrawAfter->mIsVisible = false;
	}
}

CallBack_LifeGauge::StaticValues CallBack_LifeGauge::msVal;

} // namespace Screen
} // namespace og
