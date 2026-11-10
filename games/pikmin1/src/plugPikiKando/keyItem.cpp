#include "KeyItem.h"
#include "DebugLog.h"
#include "DoorItem.h"
#include "Font.h"
#include "Graphics.h"
#include "MapMgr.h"
#include "SoundMgr.h"
#include "EffectMgr.h"

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F0
 */
DEFINE_PRINT("keyItem")

/**
 * @todo: Documentation
 */
KeyItem::KeyItem(CreatureProp* props, Shape* model)
    : Creature(props)
{
	mModel = model;
	_68    = 4;
	mSearchBuffer.init(mSearch, 3);
	resetCreatureFlag(CF_DisableAutoFaceDir);
	setCreatureFlag(CF_Unk1);
	mObjType = OBJTYPE_Key;
	mState   = KeyState::Inactive;
#if defined(PIKI_PC_PORT)
	mGlow = nullptr;
#endif
}

/**
 * @todo: Documentation
 */
f32 KeyItem::getSize()
{
	return 25.0f;
}

/**
 * @todo: Documentation
 */
void KeyItem::init(immut Vector3f& pos)
{
	f32 scale = 1.0f;
	mSRT.s.set(scale, scale, scale);
	Creature::init(pos);
	mSearchBuffer.init(mSearch, 3);
	mState = KeyState::Inactive;
}

/**
 * @todo: Documentation
 */
void KeyItem::startAI(int)
{
	mState = KeyState::Active;
#if defined(PIKI_PC_PORT)
	mGlowPos = mSRT.t;
	mGlowPos.y += 2.0f;
	mGlow = effectMgr->create(EffectMgr::EFF_Piki_IdleYellow, mGlowPos, nullptr, nullptr);
	if (mGlow) {
		mGlow->setEmitPosPtr(&mGlowPos);
		mGlow->setScaleSize(mGlow->getScaleSize() * 2.5f);
	}
#endif
}

/**
 * @todo: Documentation
 */
bool KeyItem::isVisible()
{
	bool visible = false;

	if (mState != KeyState::Collected && mState != KeyState::Inactive) {
		visible = true;
	}

	return visible;
}

/**
 * @todo: Documentation
 */
void KeyItem::doKill()
{
	PRINT("key is killed ?\n");
	mState = KeyState::Collected;
#if defined(PIKI_PC_PORT)
	if (mGlow) {
		effectMgr->kill(mGlow, false);
		mGlow = nullptr;
	}
#endif
	itemMgr->kill(this);
}

/**
 * @todo: Documentation
 */
void KeyItem::update()
{
#if defined(PIKI_PC_PORT)
	// Fase de la animación de flotar (ver refresh).
	mFaceDirection = roundAng(mFaceDirection + gsys->getFrameTime() * 1.2f);
#else
	f32 rotationSpeed = mVelocity.length() / 150.0f * 0.01f + 0.025f;
	if (rotationSpeed > 2.0f) {
		rotationSpeed = 2.0f;
	}

	mFaceDirection += rotationSpeed * TAU;
	mFaceDirection = roundAng(mFaceDirection);
#endif
	mGrid.updateGrid(mSRT.t);
	updateAI();

	STACK_PAD_VAR(2);
}

/**
 * @todo: Documentation
 */
void KeyItem::collisionCallback(immut CollEvent&)
{
}

/**
 * @todo: Documentation
 */
f32 KeyItem::getiMass()
{
	return 10.0f;
}

/**
 * @todo: Documentation
 */
void KeyItem::refresh(Graphics& gfx)
{
	Matrix4f transform, viewTransform;

#if defined(PIKI_PC_PORT)
	// Flota arriba y abajo y se balancea un poco.
	const f32 phase = mFaceDirection;
	Vector3f rot(0.0f, mSRT.r.y + sinf(phase) * 0.35f, sinf(phase * 2.0f) * 0.12f);
	mWorldMtx.makeSRT(mSRT.s, rot, Vector3f(mSRT.t.x, mSRT.t.y + 12.0f + sinf(phase * 2.0f) * 5.0f, mSRT.t.z));
#else
	mWorldMtx.makeSRT(mSRT.s, mSRT.r, Vector3f(mSRT.t.x, mSRT.t.y + 20.0f, mSRT.t.z));
#endif
	gfx.calcViewMatrix(mWorldMtx, viewTransform);
	gfx.useMatrix(viewTransform, 0);

	gfx.mCamera->setBoundOffset(&mSRT.t);
	mapMgr->getLight(mSRT.t.x, mSRT.t.z);

	bool l = gfx.setLighting(true, nullptr);
	mModel->drawshape(gfx, *gfx.mCamera, nullptr);
	gfx.mCamera->setBoundOffset(nullptr);
}

#if defined(PIKI_PC_PORT)
// La puerta con cerradura de los mapas propios (el juego no la usa): a escala
// 2 medía 52 de ancho y tapaba poco más que su esfera de 60, así que en los
// pasos de los mapas quedaban huecos a los lados. Más grande y con su esfera
// a juego tapa un paso de ~160 sin muretes. El editor de mapas la dibuja igual
// (kKeyDoorScale en tools/mapmaker/src/app.cpp).
static const f32 kDoorScale = 6.0f;
static const f32 kDoorSize  = 80.0f;
#else
static const f32 kDoorScale = 2.0f;
static const f32 kDoorSize  = 60.0f;
#endif

/**
 * @todo: Documentation
 */
DoorItem::DoorItem(int objType, CreatureProp* props, Shape* shape)
    : ItemCreature(objType, props, shape)
{
	mStateId              = DoorState::Inactive;
	mFadeTimer            = 0.0f;
	mDestinationStagePath = nullptr;
	mLabelText            = nullptr;
}

/**
 * @todo: Documentation
 */
void DoorItem::disappear()
{
	if (mStateId != DoorState::Vanishing) {
		mStateId   = DoorState::Vanishing;
		mFadeTimer = 2.5f;
#if defined(PIKI_PC_PORT)
		seSystem->playSoundDirect(JACEVENT_Build, SEB_WALL_DOWN, mSRT.t);
#endif
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00000C
 */
void DoorItem::becomeGate()
{
	mObjType = OBJTYPE_Gate;
}

/**
 * @todo: Documentation
 */
f32 DoorItem::getSize()
{
	if (mObjType == OBJTYPE_Gate) {
		return kDoorSize;
	}

	return 30.0f;
}

/**
 * @todo: Documentation
 */
void DoorItem::init(immut Vector3f& pos)
{
	ItemCreature::init(pos);
	f32 scale = 1.0f;
	if (mObjType == OBJTYPE_Gate) {
		scale = kDoorScale;
	}
	mSRT.s.set(scale, scale, scale);
	mStateId = DoorState::Inactive;
}

/**
 * @todo: Documentation
 */
void DoorItem::startAI(int)
{
	mStateId = DoorState::Active;
}

/**
 * @todo: Documentation
 */
void DoorItem::update()
{
	if (mStateId == DoorState::Vanishing) {
#if defined(PIKI_PC_PORT)
		const f32 dt = gsys->getFrameTime() * 3.0f; // ~0,8 s en vez de 2,5
#else
		const f32 dt = gsys->getFrameTime();
#endif
		mFadeTimer -= dt;
		f32 yscale = (mFadeTimer / 2.5f) * kDoorScale;
		mSRT.t.y -= dt * 3.0f;

		mSRT.s.set(kDoorScale, yscale, kDoorScale);

		if (mFadeTimer < 0.0f) {
			mStateId = DoorState::Killed;
			kill(false);
		}
	} else {
		ItemCreature::update();
	}
}

/**
 * @todo: Documentation
 */
bool DoorItem::isVisible()
{
	bool res = false;
	if (mStateId != 1 && mStateId != DoorState::Inactive) {
		res = true;
	}
	return res;
}

/**
 * @todo: Documentation
 */
f32 DoorItem::getiMass()
{
	return 0.0f;
}

/**
 * @todo: Documentation
 */
bool DoorItem::isAtari()
{
	bool res = true;
	if (mStateId != DoorState::Active && mStateId != DoorState::Vanishing) {
		res = false;
	}
	return res;
}

/**
 * @todo: Documentation
 */
void DoorItem::refresh(Graphics& gfx)
{
	mWorldMtx.makeSRT(mSRT.s, mSRT.r, mSRT.t);
	Matrix4f mtx;
	gfx.calcViewMatrix(mWorldMtx, mtx);

	Vector3f pos(0.0f, 40.0f, 0.0f);
	pos.multMatrix(mtx);
	bool set = gfx.setLighting(false, nullptr);
	gfx.useMatrix(Matrix4f::ident, 0);

	char str[256];

	gfx.setColour(COLOUR_WHITE, true);
	gfx.useTexture(gsys->mConsFont->mTexture, GX_TEXMAP0);
	int blend = gfx.setCBlending(BLEND_Alpha);

	sprintf(str, "%s", mLabelText);
	gfx.perspPrintf(gsys->mConsFont, pos, -(gsys->mConsFont->stringWidth(str) / 2), 0, str);

	gfx.setCBlending(blend);
	gfx.setLighting(set, nullptr);
	gfx.useMatrix(mtx, 0);
	ItemCreature::refresh(gfx);
}
