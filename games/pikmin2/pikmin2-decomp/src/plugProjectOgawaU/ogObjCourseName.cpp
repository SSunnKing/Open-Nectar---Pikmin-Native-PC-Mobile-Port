#include <cstring>
#include <cstdlib>
#include "og/newScreen/CourseName.h"
#include "P2DScreen.h"
#include "og/Screen/anime.h"
#include "og/Screen/ogScreen.h"
#include "JSystem/J2D/J2DMaterial.h"
#include "System.h"
#ifdef PIKI_PC_PORT
#include "JSystem/J2D/J2DPicture.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "JSystem/ResTIMG.h"
#endif

#ifdef PIKI_PC_PORT
namespace {
// Titulo para la calima de AW, PP y WW: I8 de 480x80 como e_course_name_01
// (el "Challenge Cave" de referencia), con el texto en x 61..407 e y 13..73.
const int kPcHazeW       = 480;
const int kPcHazeH       = 80;
const int kPcHazeCourses = 3;
struct PcHazeTex {
	ResTIMG mHeader;
	u8 mPixels[kPcHazeW * kPcHazeH];
};
// Una por zona: cada textura conserva su contenido mientras se use.
PcHazeTex sPcHaze[kPcHazeCourses] ATTRIBUTE_ALIGN(32);

/// Intensidad (0..255) de un texel de las letras: I4, I8, IA4 o IA8.
int pcHazeTexel(const ResTIMG* img, int x, int y)
{
	const u8* data = (const u8*)pc_restimg_image_data(img);
	const int w    = img->mSizeX;
	switch (img->mTextureFormat) {
	case GX_TF_I4: {
		const u8 b = data[((y / 8) * ((w + 7) / 8) + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
		return ((x % 2) ? (b & 0xF) : (b >> 4)) * 17;
	}
	case GX_TF_I8:
		return data[((y / 4) * ((w + 7) / 8) + x / 8) * 32 + (y % 4) * 8 + (x % 8)];
	case GX_TF_IA4: {
		const u8 b = data[((y / 4) * ((w + 7) / 8) + x / 8) * 32 + (y % 4) * 8 + (x % 8)];
		return (b & 0xF) * (b >> 4) * 17 * 17 / 255;
	}
	case GX_TF_IA8: {
		const u8* p = data + ((y / 4) * ((w + 3) / 4) + x / 4) * 32 + ((y % 4) * 4 + (x % 4)) * 2;
		return p[0] * p[1] / 255;
	}
	default:
		return 0;
	}
}

/// Caja del panel en coordenadas de su padre, segun el .blo: mBounds es local
/// al origen del panel y mTranslate lo situa (antes de las animaciones).
JGeometry::TBox2f pcHazeBox(const J2DPane* pane)
{
	return JGeometry::TBox2f(pane->mTranslateX + pane->mBounds.i.x, pane->mTranslateY + pane->mBounds.i.y,
	                         pane->mTranslateX + pane->mBounds.f.x, pane->mTranslateY + pane->mBounds.f.y);
}

/// Las letras son las imagenes hijas de la raiz (PICT_0xx en PP y WW, PETxx
/// en AW); las demas son los adornos fuwa_xx y line_xx.
bool pcHazeIsLetter(const J2DPane* pane)
{
	if (pane->getTypeID() != PANETYPE_Picture) {
		return false;
	}
	char name[9];
	int len = 0;
	for (int i = 0; i < 8; i++) {
		const char c = (char)(pane->mTag >> (56 - 8 * i));
		if (c) {
			name[len++] = c;
		}
	}
	name[len] = '\0';
	return strncmp(name, "fuwa", 4) != 0 && strncmp(name, "line", 4) != 0;
}

/// Compone el titulo con las letras de la pantalla, escalado y centrado como
/// en la textura de referencia. nullptr si no hay letras.
const ResTIMG* pcBuildHazeTitle(J2DPane* root, int slot)
{
	J2DPicture* letters[32];
	int count = 0;
	f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (J2DPane* pane = root->getFirstChildPane(); pane && count < 32; pane = pane->getNextChildPane()) {
		if (!pcHazeIsLetter(pane)) {
			continue;
		}
		J2DPicture* pic = static_cast<J2DPicture*>(pane);
		if (!pic->getTexture(0) || !pic->getTexture(0)->mTexInfo || !pc_restimg_is_host_endian(pic->getTexture(0)->mTexInfo)) {
			continue;
		}
		letters[count++]            = pic;
		const JGeometry::TBox2f box = pcHazeBox(pane);
		x0                          = box.i.x < x0 ? box.i.x : x0;
		y0                          = box.i.y < y0 ? box.i.y : y0;
		x1                          = box.f.x > x1 ? box.f.x : x1;
		y1                          = box.f.y > y1 ? box.f.y : y1;
	}
	if (count == 0 || x1 <= x0 || y1 <= y0) {
		return nullptr;
	}

	PcHazeTex& tex = sPcHaze[slot];
	memset(&tex.mHeader, 0, sizeof(tex.mHeader));
	tex.mHeader.mTextureFormat   = GX_TF_I8;
	tex.mHeader.mSizeX           = kPcHazeW;
	tex.mHeader.mSizeY           = kPcHazeH;
	tex.mHeader.mWrapS           = GX_CLAMP;
	tex.mHeader.mWrapT           = GX_CLAMP;
	tex.mHeader.mMinFilterType   = GX_LINEAR;
	tex.mHeader.mMagFilterType   = GX_LINEAR;
	tex.mHeader.mTotalImageCount = 1;
	tex.mHeader.mImageDataOffset = 0x20;
	tex.mHeader._19              = P2_RESTIMG_HOST_ENDIAN_MARK;
	memset(tex.mPixels, 0, sizeof(tex.mPixels));

	// Caja del texto en la referencia: 346 de ancho como mucho (se deja
	// llegar a 400 para los nombres largos) y 60 de alto, centro en (234, 43).
	const f32 sx    = 400.0f / (x1 - x0);
	const f32 sy    = 60.0f / (y1 - y0);
	const f32 scale = sx < sy ? sx : sy;
	const f32 ox    = 234.0f - (x0 + x1) * 0.5f * scale;
	const f32 oy    = 43.0f - (y0 + y1) * 0.5f * scale;

	for (int i = 0; i < count; i++) {
		const JGeometry::TBox2f box = pcHazeBox(letters[i]);
		const ResTIMG* img          = letters[i]->getTexture(0)->mTexInfo;
		const f32 dx0 = ox + box.i.x * scale, dy0 = oy + box.i.y * scale;
		const f32 dw = box.getWidth() * scale, dh = box.getHeight() * scale;
		for (int y = (int)dy0; y < (int)(dy0 + dh) + 1; y++) {
			for (int x = (int)dx0; x < (int)(dx0 + dw) + 1; x++) {
				if (x < 0 || y < 0 || x >= kPcHazeW || y >= kPcHazeH) {
					continue;
				}
				const int u = (int)((x + 0.5f - dx0) / dw * img->mSizeX);
				const int v = (int)((y + 0.5f - dy0) / dh * img->mSizeY);
				if (u < 0 || v < 0 || u >= img->mSizeX || v >= img->mSizeY) {
					continue;
				}
				const int value = pcHazeTexel(img, u, v);
				u8& out         = tex.mPixels[((y / 4) * (kPcHazeW / 8) + x / 8) * 32 + (y % 4) * 8 + (x % 8)];
				if (value > out) {
					out = (u8)value;
				}
			}
		}
	}
	return &tex.mHeader;
}
} // namespace
#endif

/**
 * @note Address: N/A
 * @note Size: 0xE4
 */
static void _Print(char* format, ...)
{
	OSReport(format, __FILE__);
}

namespace og {
namespace newScreen {

/**
 * @note Address: 0x8031741C
 * @note Size: 0xA8
 */
ObjCourseName::ObjCourseName(char const* name)
    : mFadeLevel(0.0f)
    , mAlpha(0.0f)
{
	mName = name;

	mDisp   = nullptr;
	mScreen = nullptr;
	mAnims  = nullptr;

	mTimer = msVal.mFinishWaitTime;
	mState = 0;

	mColor.set(0, 0, 0, 255);
	mDoEnd           = 0;
	mBackgroundAlpha = 0.0f;
}

/**
 * @note Address: 0x803174C4
 * @note Size: 0xAC
 */
ObjCourseName::~ObjCourseName()
{
}

/**
 * @note Address: 0x80317570
 * @note Size: 0x334
 */
void ObjCourseName::doCreate(JKRArchive* arc)
{
	og::Screen::DispMemberCourseName* disp = static_cast<og::Screen::DispMemberCourseName*>(getDispMember());
	if (disp->isID(OWNER_OGA, MEMBER_COURSE_NAME)) {
		mDisp = disp;

	} else if (disp->isID(OWNER_OGA, MEMBER_DUMMY)) {
		mDisp = new og::Screen::DispMemberCourseName;

	} else {
		JUT_PANICLINE(101, "ERR! in ObjCourseName Create失敗！\n");
	}

	CourseName* owner = static_cast<CourseName*>(getOwner());
	const char* path  = owner->getCourseBloName();

	mScreen = new P2DScreen::Mgr_tuning;
	mScreen->set(path, 0x1040000, arc);

	mAnims         = new og::Screen::AnimGroup(5);
	char** list    = animFileTableCourse[owner->mCourseIndex];
	char* listItem = list[0];
	for (int i = 0; listItem; i++) {
		og::Screen::registAnimGroupScreen(mAnims, arc, mScreen, listItem, 1.0f);
		listItem = list[i + 1];
	}

	og::Screen::setAlphaScreen(mScreen);

	u64 tags[4] = { MC8("nuki_tex"), MC8("efect_00"), MC8("efect_01"), 0 };
	J2DBlend info(GX_BM_BLEND, GX_BL_INVDSTALPHA, GX_BL_DSTALPHA, GX_LO_CLEAR);
	mScreen->setBlendInfo(info, tags);

#ifdef PIKI_PC_PORT
	// Las copias con calima del titulo (PICT_001/002) solo llevan el titulo
	// correcto en Valley of Repose. En AW usan result_title_new.bti y en PP y
	// WW un "Challenge Cave" sobrante (e_course_name_01.bti), asi que se les da
	// el titulo de la zona compuesto con sus letras (issue #270).
	if (owner->mCourseIndex > 0 && owner->mCourseIndex <= kPcHazeCourses) {
		const ResTIMG* title = pcBuildHazeTitle(mScreen, owner->mCourseIndex - 1);
		const u64 hazeTags[] = { MC8("PICT_001"), MC8("PICT_002") };
		for (int i = 0; title && i < 2; i++) {
			J2DPane* haze = mScreen->search(hazeTags[i]);
			if (haze && haze->getTypeID() == PANETYPE_Picture) {
				static_cast<J2DPicture*>(haze)->changeTexture(title, 0);
			}
		}
	}
#endif
#ifdef PIKI_PC_PORT
	// TEMP depuracion: PIKMIN_CN_HIDE=PICT_001,PICT_002 oculta esos paneles.
	if (const char* hide = getenv("PIKMIN_CN_HIDE")) {
		char buf[128];
		strncpy(buf, hide, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
			u64 tag = 0;
			for (const char* c = tok; *c; c++) tag = (tag << 8) | (u8)*c;
			if (J2DPane* p = mScreen->search(tag)) {
				p->hide();
				printf("[CN] oculto %s\n", tok);
			}
		}
	}
#endif
	J2DPane* pane = mScreen->search(MC8("nuki_tex"));
	if (pane) {
		J2DPane* pane1 = mScreen->search(MC8("efect_00"));
		J2DPane* pane2 = mScreen->search(MC8("efect_01"));
		mScreen->prependChild(pane);
		if (pane1)
			pane1->hide();
		if (pane2)
			pane2->hide();
	}
}

/**
 * @note Address: N/A
 * @note Size: 0xF0
 */
bool ObjCourseName::commonUpdate()
{
	bool check = false;
	mAnims->update();
	mScreen->animation();
	mScreen->update();

	if (mState != 0) {
		mTimer -= sys->mDeltaTime;
		if (mTimer < 0.0f) {
			check = true;
		}
	} else {
		og::Screen::DispMemberCourseName* disp = static_cast<og::Screen::DispMemberCourseName*>(getDispMember());
		if (disp->isID(OWNER_OGA, MEMBER_COURSE_NAME)) {
			if (disp->mIsCounting) {
				mState = 1;
			}
			if (disp->mIsExiting) {
				mDoEnd = true;
			}
		}
	}
	return check;
}

/**
 * @note Address: 0x803178A4
 * @note Size: 0xF0
 */
bool ObjCourseName::doUpdate()
{
	return commonUpdate();
}

/**
 * @note Address: 0x80317994
 * @note Size: 0x170
 */
void ObjCourseName::doDraw(Graphics& gfx)
{
	drawBG(gfx);

	J2DPerspGraph* graf = &gfx.mPerspGraph;
	graf->setPort();

	u16 w = sys->getRenderModeObj()->fbWidth;
	u16 h = sys->getRenderModeObj()->efbHeight;
	JUtility::TColor color(255, 255, 255, 255);
	graf->setColor(color);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);

	JGeometry::TVec2f vec(0.0f, 0.0f);
	JGeometry::TBox2f box(vec.x, vec.y, vec.x + w, vec.y + h);
	graf->fillBox(box);
	GXSetColorUpdate(GX_TRUE);
	graf->setPort();

	f32 alpha = mAlpha * 255.0f;
	mScreen->setAlpha(alpha);
	mScreen->draw(gfx, *graf);
	graf->setPort();
}

/**
 * @note Address: 0x80317B04
 * @note Size: 0x14
 */
bool ObjCourseName::doStart(::Screen::StartSceneArg const*)
{
	mFadeLevel = 0.0f;
	mAlpha     = 0.0f;
	return true;
}

/**
 * @note Address: 0x80317B18
 * @note Size: 0x10
 */
bool ObjCourseName::doEnd(::Screen::EndSceneArg const*)
{
	mFadeLevel = 0.0f;
	return true;
}

/**
 * @note Address: 0x80317B28
 * @note Size: 0x4
 */
void ObjCourseName::doUpdateFadeinFinish()
{
}

/**
 * @note Address: 0x80317B2C
 * @note Size: 0xC
 */
void ObjCourseName::doUpdateFinish()
{
	mFadeLevel = 0.0f;
}

/**
 * @note Address: 0x80317B38
 * @note Size: 0x4
 */
void ObjCourseName::doUpdateFadeoutFinish()
{
}

/**
 * @note Address: 0x80317B3C
 * @note Size: 0x120
 */
bool ObjCourseName::doUpdateFadein()
{
	bool check = false;
	mFadeLevel += sys->mDeltaTime;
	if (mFadeLevel > msVal.mFadeinTime) {
		mFadeLevel = msVal.mFadeinTime;
		check      = true;
	}
	mAlpha = mFadeLevel / msVal.mFadeinTime;
	commonUpdate();
	return check;
}

/**
 * @note Address: 0x80317C5C
 * @note Size: 0x134
 */
bool ObjCourseName::doUpdateFadeout()
{
	bool check = false;
	mFadeLevel += sys->mDeltaTime;
	if (mFadeLevel > msVal.mFadeoutTime) {
		mFadeLevel = msVal.mFadeoutTime;
		if (!mDoEnd) {
			check = true;
		}
	}
	mAlpha = 1.0f - mFadeLevel / msVal.mFadeoutTime;
	commonUpdate();
	return check;
}

/**
 * @note Address: 0x80317D90
 * @note Size: 0x168
 */
void ObjCourseName::drawBG(Graphics& gfx)
{
	J2DPerspGraph* graf = &gfx.mPerspGraph;
	if (mDoEnd) {
		mBackgroundAlpha += sys->mDeltaTime;
		f32 temp = 1.0f - mBackgroundAlpha / msVal.mBgAlphaFadeTime;
		if (temp > 0.0f) {
			mColor.a = temp * 255.0f;
		} else {
			mColor.a = 0;
			mDoEnd   = 0;
		}
	}

	if (mColor.a != 0) {
		graf->setPort();
		u16 w = sys->getRenderModeObj()->fbWidth;
		u16 h = sys->getRenderModeObj()->efbHeight;
		graf->setColor(mColor);
		GXSetAlphaUpdate(GX_FALSE);

		JGeometry::TVec2f vec(0.0f, 0.0f);
		JGeometry::TBox2f box(vec.x, vec.y, vec.x + w, vec.y + h);
		graf->fillBox(box);
	}
}

ObjCourseName::StaticValues ObjCourseName::msVal;

} // namespace newScreen
} // namespace og
