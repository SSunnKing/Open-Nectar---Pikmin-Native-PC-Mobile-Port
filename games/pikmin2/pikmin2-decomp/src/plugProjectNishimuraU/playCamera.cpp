#include "Game/CameraMgr.h"
#ifdef PIKI_PC_PORT
namespace Game {
struct Navi;
void pcNaviRotateCursor(Navi* navi, float angle);
void pcNaviCursorToFront(Navi* navi);
} // namespace Game
#endif
#include "Game/Navi.h"
#include "Game/Stickers.h"
#include "Game/MapMgr.h"
#include "PSSystem/PSSystemIF.h"
#include "nans.h"
#include "Game/MoviePlayer.h"

#ifdef PIKI_PC_PORT
extern "C" int pc_settings_get_mouse_wheel_action(void);
int pc_window_take_wheel_steps(void);
// Mod "Mouse Wheel" (zoom): multiplica la distancia de la camara sobre los tres
// niveles originales de R. Volver a "color" la deja como estaba.
static f32 sPcZoomMul = 1.0f;
// Mods "Free Camera" y "First Person": arrastre de camara (raton con B, stick
// derecho o, en primera persona, el raton siempre) y cabeceo en primera persona.
extern "C" float pc_window_take_camera_drag(void);
extern "C" float pc_window_take_camera_pitch(void);
extern "C" int pc_settings_get_free_camera(void);
extern "C" int pc_first_person_active(void);
static f32 sPcPitch = -0.15f; // cabeceo neutro, un poco hacia abajo, como en Pikmin 1
static bool pcFirstPersonFor(Game::Navi* navi)
{
	return navi && navi->mController1 && pc_first_person_active() && (!Game::moviePlayer || Game::moviePlayer->mDemoState == Game::DEMOSTATE_Inactive);
}
static void pcUpdateWheelZoom()
{
	if (pc_settings_get_mouse_wheel_action() != 1) {
		sPcZoomMul = 1.0f;
		return;
	}
	const int steps = pc_window_take_wheel_steps();
	if (steps != 0) {
		// Alejar la rueda del jugador aleja la camara.
		sPcZoomMul += 0.08f * static_cast<f32>(steps);
		if (sPcZoomMul < 0.45f) sPcZoomMul = 0.45f;
		if (sPcZoomMul > 2.50f) sPcZoomMul = 2.50f;
	}
}
#endif

namespace Game {

/**
 * @note Address: 0x8023F3F0
 * @note Size: 0x138
 */
PlayCamera::PlayCamera(Navi* target)
{
	mTargetObj            = target;
	mChangePlayerState    = CAMCHANGE_None;
	mCameraZoomLevel      = CAMZOOM_Mid;
	mCameraSelAngle       = CAMANGLE_Behind;
	mCanInput             = true;
	mIsCollisionCamActive = false;

	mGoalTargetDistance = 100.0f;
	mCurrTargetDistance = 100.0f;
	mCameraAngleTarget  = 0.0f;
	mCameraAngleCurrent = 0.0f;
	mGoalVerticalAngle  = 0.0f;
	mCurrVerticalAngle  = 0.0f;

	mGoalFOV        = 30.0f;
	mViewAngle      = 30.0f;
	mNearZPlane     = 1.0f;
	mProjectionNear = 1.0f;
	mFarZPlane      = 12800.0f;
	mProjectionFar  = 12800.0f;
	mYOffset        = 0.0f;

	mDetachedWeight  = 0.0f;
	mDetachedParm    = 0.0f;
	mSmoothMoveSpeed = 0.0f;
	mFollowTime      = 0.0f;
	mHoldRTimer      = 0.0f;

	for (int i = 0; i < 3; i++) {
		mVibrateEnabled[i]   = false;
		mVibrateStrength[i]  = 0.0f;
		mVibrateScale[i]     = 0.0f;
		mVibrateDuration[i]  = 0.0f;
		mVibrateTimer[i]     = 0.0f;
		mVibrateAngle[i]     = 0.0f;
		mVibrateRollAngle[i] = 0.0f;
		mVibrateSpeed[i]     = 0.0f;
	}

	mCameraParms    = nullptr;
	mVibrationParms = nullptr;
	mName           = "PlayCamera";
}

/**
 * @note Address: 0x8023F528
 * @note Size: 0x8
 */
void PlayCamera::setCameraParms(CameraParms* parms)
{
	mCameraParms = parms;
}

/**
 * @note Address: 0x8023F530
 * @note Size: 0x8
 */
void PlayCamera::setVibrationParms(VibrationParms* parms)
{
	mVibrationParms = parms;
}

/**
 * @note Address: 0x8023F538
 * @note Size: 0x16C
 */
void PlayCamera::init()
{
	P2ASSERTLINE(120, mTargetObj);
	P2ASSERTLINE(121, mCameraParms);
	P2ASSERTLINE(122, mVibrationParms);
	mCanInput          = true;
	mChangePlayerState = CAMCHANGE_None;
	mCameraZoomLevel   = CAMZOOM_Mid; // (default to medium zoom)
	mCameraSelAngle    = CAMANGLE_Behind;
	setTargetParms();
	changeTargetAtPosition();
	mCurrTargetDistance = mGoalTargetDistance;

	f32 angle = mTargetObj->getFaceDir() + PI;
	clampAngle(angle);
	mCameraAngleTarget  = angle;
	mCameraAngleCurrent = angle;
	mCurrVerticalAngle  = mGoalVerticalAngle;
	mViewAngle          = mGoalFOV;
	mProjectionNear     = mNearZPlane;
	mProjectionFar      = mFarZPlane;
	mLookAtPosition     = mGoalPosition;
	updateMatrix();
}

/**
 * @note Address: 0x8023F6A4
 * @note Size: 0x34
 */
void PlayCamera::setCameraAngle(f32 angle)
{
	mCameraAngleTarget  = angle;
	mCameraAngleCurrent = angle;
	updateMatrix();
}

/**
 * @note Address: 0x8023F6D8
 * @note Size: 0x5C
 */
void PlayCamera::getCameraData(CameraData& data)
{
	data.mTargetDistance = mCurrTargetDistance;
	data.mCameraAngle    = mCameraAngleCurrent;
	data.mVerticalAngle  = mCurrVerticalAngle;
	data.mFieldofView    = mViewAngle;
	data.mNearZ          = mProjectionNear;
	data.mFarZ           = mProjectionFar;
	data.mLookAtPosition = mLookAtPosition;
	data.mZoomLevel      = mCameraZoomLevel;
	data.mSelAngle       = mCameraSelAngle;
}

/**
 * @note Address: 0x8023F734
 * @note Size: 0x5C
 */
void PlayCamera::setCameraData(CameraData& data)
{
	mCurrTargetDistance = data.mTargetDistance;
	mCameraAngleCurrent = data.mCameraAngle;
	mCurrVerticalAngle  = data.mVerticalAngle;
	mViewAngle          = data.mFieldofView;
	mProjectionNear     = data.mNearZ;
	mProjectionFar      = data.mFarZ;
	mLookAtPosition     = data.mLookAtPosition;
	mCameraZoomLevel    = data.mZoomLevel;
	mCameraSelAngle     = data.mSelAngle;
}

/**
 * @note Address: 0x8023F790
 * @note Size: 0x80
 */
void PlayCamera::changePlayerMode(bool doCenterCameraBehind)
{
	mChangePlayerState = CAMCHANGE_IsChanging;
	setTargetParms();
	changeTargetAtPosition();
	if (doCenterCameraBehind) {
		setTargetThetaToWhistle();
	} else {
		mCameraAngleTarget = mCameraAngleCurrent;
	}
	updateMatrix();
	setProjection();
}

/**
 * @note Address: 0x8023F810
 * @note Size: 0x4C
 * Returns true if the conditions to use the holding R camera are met
 */
bool PlayCamera::isSpecialCamera()
{
	if (mTargetObj->mController1 && mCanInput && (mChangePlayerState == CAMCHANGE_None) && mHoldRTimer >= 1.0f) {
		return true;
	}
	return false;
}

/**
 * @note Address: 0x8023F85C
 * @note Size: 0xE8
 */
void PlayCamera::doUpdate()
{
	u32 flags = updateCameraMode();
	if (flags & CAMFLAGS_StartZoomCam) {
		startZoomCamera();
	}
	if (flags & CAMFLAGS_EndZoomCam) {
		finishDemoCamera();
	}
	if (flags & (CAMFLAGS_ChangeZoomLevel | CAMFLAGS_ChangeSelAngle)) {
		startGameCamera(flags);
	}
	if (flags & CAMFLAGS_CenterBehind) {
		setFollowTime();
	}
	if (flags & CAMFLAGS_SmoothFollow) {
		setSmoothThetaSpeed();
	}
	changeTargetTheta();
	changeTargetAtPosition();
	setCollisionCameraTargetPhi(flags);
	updateParms(flags);
	for (int i = 0; i < 3; i++) {
		if (mVibrateEnabled[i]) {
			updateVibration(i);
		}
	}
	isModCameraFinished();
	return;
}

/**
 * @note Address: 0x8023F944
 * @note Size: 0xE0
 */
void PlayCamera::updateMatrix()
{
	Mtx mtx1, mtx2, mtx3, mtx4, mtx5;
#ifdef PIKI_PC_PORT
	// Mod "First Person": el ojo en la cabeza del capitan, con la misma
	// orientacion horizontal que la camara normal (asi el stick, el cursor y
	// el enjambre siguen relativos a la vista) y el cabeceo del raton.
	if (pcFirstPersonFor(mTargetObj)) {
		Vector3f eye = mTargetObj->getPosition();
		eye.y += 25.0f;
		mCameraAngleCurrent = mCameraAngleTarget; // sin la inercia de tercera persona
		PSMTXRotRad(mtx2, 'Y', -mCameraAngleCurrent);
		PSMTXRotRad(mtx3, 'X', -sPcPitch);
		PSMTXTrans(mtx5, -eye.x, -eye.y, -eye.z);
		PSMTXConcat(mtx3, mtx2, mLookMatrix.mMatrix.mtxView);
		PSMTXConcat(mLookMatrix.mMatrix.mtxView, mtx5, mLookMatrix.mMatrix.mtxView);
		return;
	}
#endif
	PSMTXRotRad(mtx1, 'Z', mVibrateRollAngle[0]);
	PSMTXRotRad(mtx2, 'Y', -mCameraAngleCurrent);
	PSMTXRotRad(mtx3, 'X', mCurrVerticalAngle);
	PSMTXTrans(mtx4, 0.0f, 0.0f, mVibrateRollAngle[2] - mCurrTargetDistance);
	PSMTXTrans(mtx5, -mLookAtPosition.x, -mVibrateRollAngle[1] - mLookAtPosition.y, -mLookAtPosition.z);

	PSMTXConcat(mtx3, mtx2, mLookMatrix.mMatrix.mtxView);
	PSMTXConcat(mtx1, mLookMatrix.mMatrix.mtxView, mLookMatrix.mMatrix.mtxView);
	PSMTXConcat(mtx4, mLookMatrix.mMatrix.mtxView, mLookMatrix.mMatrix.mtxView);
	PSMTXConcat(mLookMatrix.mMatrix.mtxView, mtx5, mLookMatrix.mMatrix.mtxView);
}

/**
 * @note Address: 0x8023FA24
 * @note Size: 0x7C
 */
void PlayCamera::noUpdate()
{
	PSMTX44Copy(mProjectionMtx, mBackupMtx);
	PSMTXCopy(getViewMatrix(0)->mMatrix.mtxView, mCurViewMatrix.mMatrix.mtxView);
	updateScreenConstants();
	updatePlanes();
}

/**
 * @note Address: 0x8023FAA0
 * @note Size: 0x44
 */
bool PlayCamera::isVibration()
{
	for (int i = 0; i < 3; i++) {
		if (mVibrateEnabled[i])
			return true;
	}
	return false;
}

/**
 * @note Address: 0x8023FAE4
 * @note Size: 0x208
 */
void PlayCamera::startVibration(int type, f32 scale)
{
	if (type == VIBTYPE_NaviDamage) {
		mVibrateEnabled[0]  = true;
		mVibrateTimer[0]    = 0.0f;
		mVibrateScale[0]    = scale;
		mVibrateStrength[0] = mVibrationParms->mAzimuthShortVib;
		mVibrateSpeed[0]    = mVibrationParms->mAzimuthShortSpeed;
		mVibrateDuration[0] = mVibrationParms->mAzimuthShortTime;
		otherVibFinished(0);
		return;
	}

	if (type == VIBTYPE_Boom) {
		mVibrateEnabled[2]  = true;
		mVibrateTimer[2]    = 0.0f;
		mVibrateScale[2]    = scale;
		mVibrateStrength[2] = mVibrationParms->mZoomShortVib;
		mVibrateSpeed[2]    = mVibrationParms->mZoomShortSpeed;
		mVibrateDuration[2] = mVibrationParms->mZoomShortTime;
		otherVibFinished(2);
		return;
	}

	mVibrateEnabled[1] = true;
	mVibrateTimer[1]   = 0.0f;
	mVibrateScale[1]   = scale;
	otherVibFinished(1);
	if (type == VIBTYPE_Crash) {
		mVibrateStrength[1] = mVibrationParms->mElevationHardVib2;
		mVibrateSpeed[1]    = mVibrationParms->mElevationHardSpeed;
		mVibrateDuration[1] = mVibrationParms->mElevationHardTime;
		return;
	}

	// strength
	if (type <= VIBTYPE_LIGHT) {
		mVibrateStrength[1] = mVibrationParms->mElevationLightVib;
	} else if (type <= VIBTYPE_MID) {
		mVibrateStrength[1] = mVibrationParms->mElevationMiddleVib;
	} else { // VIBTYPE_HARD
		mVibrateStrength[1] = mVibrationParms->mElevationHardVib;
	}

	// speed
	int speedType = (type / 3) % 3;
	if (speedType == 0) { // slow
		mVibrateSpeed[1] = mVibrationParms->mElevationSlowSpeed;
	} else if (speedType == 1) { // middle
		mVibrateSpeed[1] = mVibrationParms->mElevationMiddleSpeed;
	} else { // fast
		mVibrateSpeed[1] = mVibrationParms->mElevationFastSpeed;
	}

	// duration
	if (type % 3 == 0) {
		mVibrateDuration[1] = mVibrationParms->mElevationShortTime;
	} else if (type % 3 == 1) {
		mVibrateDuration[1] = mVibrationParms->mElevationMiddleTime;
	} else {
		mVibrateDuration[1] = mVibrationParms->mElevationLongTime;
	}
}

/**
 * @note Address: 0x8023FCEC
 * @note Size: 0xD0
 */
void PlayCamera::startDemoCamera(int type)
{
	switch (type) {
	case CAMDEMO_Test:
		mGoalTargetDistance = mCameraParms->mZoomDist;
		mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mZoomAngle());
		mGoalFOV            = mCameraParms->mZoomFOV;
		mNearZPlane         = 1.0f;
		mFarZPlane          = 12800.0f;
		mYOffset            = 10.0f;
		mDetachedWeight     = 1000.0f;
		mDetachedParm       = 27.5f;
		break;
	default:
		mGoalTargetDistance = mCameraParms->mNearLowDist;
		mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mNearLowAngle());
		mGoalFOV            = mCameraParms->mNearLowFOV;
		mNearZPlane         = mCameraParms->mNearLowNear;
		mFarZPlane          = mCameraParms->mNearLowFar;
		mYOffset            = mCameraParms->mNearLowOffset;
		mDetachedWeight     = mCameraParms->mNearLowWeight;
		mDetachedParm       = mCameraParms->mNearLowDetached;
		break;
	}
}

/**
 * @note Address: 0x8023FDBC
 * @note Size: 0x20
 */
void PlayCamera::finishDemoCamera()
{
	setTargetParms();
}

/**
 * @note Address: 0x8023FDDC
 * @note Size: 0xEC
 */
u32 PlayCamera::updateCameraMode()
{
	Controller* pad = mTargetObj->mController1;
	u32 flags       = 0;
	if (pad && mCanInput) {
		if (mChangePlayerState == CAMCHANGE_None) {
			if (pad->getButton() & Controller::PRESS_R) {
				if (mHoldRTimer < 1.0f) {
					mHoldRTimer += sys->mDeltaTime;
					if (mHoldRTimer >= 1.0f) {
						flags |= (CAMFLAGS_StartZoomCam | CAMFLAGS_InZoomCam);
					}
				} else {
					flags |= CAMFLAGS_InZoomCam;
				}
			} else {
				if (mHoldRTimer >= 1.0f) {
					flags |= CAMFLAGS_EndZoomCam;
				}
				mHoldRTimer = 0.0f;
			}

			if (!(flags & CAMFLAGS_InZoomCam)) {
				if (pad->getButtonDown() & Controller::PRESS_R) {
					flags |= CAMFLAGS_ChangeZoomLevel;
				}
				if (pad->getButtonDown() & Controller::PRESS_Z) {
					flags |= CAMFLAGS_ChangeSelAngle;
				}
			}

			if (pad->getButtonDown() & Controller::PRESS_L) {
				flags |= CAMFLAGS_CenterBehind;
#ifdef PIKI_PC_PORT
				// Cámara libre: L la pone detrás del capitán, y el cursor (que
				// gira con ella) vuelve delante de él.
				if (pc_settings_get_free_camera() && mTargetObj) {
					pcNaviCursorToFront(mTargetObj);
				}
#endif
			} else {
				if (pad->mButton.mAnalogL > 0.1f) {
					flags |= CAMFLAGS_SmoothFollow;
				}
			}
		}
	}
	return flags;
}

/**
 * @note Address: 0x8023FEC8
 * @note Size: 0x80
 */
void PlayCamera::startZoomCamera()
{
	mGoalTargetDistance = mCameraParms->mZoomDist;
	mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mZoomAngle());
	mGoalFOV            = mCameraParms->mZoomFOV;
	mNearZPlane         = 1.0f;
	mFarZPlane          = 12800.0f;
	mYOffset            = 10.0f;
	mDetachedWeight     = 1000.0f;
	mDetachedParm       = 27.5f;
	PSSystem::spSysIF->playSystemSe(PSSE_SY_CAMERAVIEW_ROTATE, 0);
}

/**
 * @note Address: 0x8023FF48
 * @note Size: 0x84
 */
void PlayCamera::startGameCamera(int flag)
{
	if (flag & CAMFLAGS_ChangeZoomLevel) {
		mCameraZoomLevel++;
		if (mCameraZoomLevel > CAMZOOM_Far) { // only 3 zoom levels and they cycle
			mCameraZoomLevel = CAMZOOM_Near;
		}
	}
	if (flag & CAMFLAGS_ChangeSelAngle) {
		mCameraSelAngle       = mCameraSelAngle ^ 1;
		mIsCollisionCamActive = false;
	}
	PSSystem::spSysIF->playSystemSe(PSSE_SY_CAMERAVIEW_CHANGE, 0);
	setTargetParms();
}

/**
 * @note Address: 0x8023FFCC
 * @note Size: 0x2F8
 */
void PlayCamera::setTargetParms()
{
	mHoldRTimer = 0.0f;
	switch (mCameraSelAngle) {
	case CAMANGLE_Behind: {
		switch (mCameraZoomLevel) {
		case CAMZOOM_Near: // low zoom low angle
			mGoalTargetDistance = mCameraParms->mNearLowDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mNearLowAngle());
			mGoalFOV            = mCameraParms->mNearLowFOV;
			mNearZPlane         = mCameraParms->mNearLowNear;
			mFarZPlane          = mCameraParms->mNearLowFar;
			mYOffset            = mCameraParms->mNearLowOffset;
			mDetachedWeight     = mCameraParms->mNearLowWeight;
			mDetachedParm       = mCameraParms->mNearLowDetached;
			break;
		case CAMZOOM_Mid: // medium zoom low angle
			mGoalTargetDistance = mCameraParms->mMidLowDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mMidLowAngle());
			mGoalFOV            = mCameraParms->mMidLowFOV;
			mNearZPlane         = mCameraParms->mMidLowNear;
			mFarZPlane          = mCameraParms->mMidLowFar;
			mYOffset            = mCameraParms->mMidLowOffset;
			mDetachedWeight     = mCameraParms->mMidLowWeight;
			mDetachedParm       = mCameraParms->mMidLowDetached;
			break;
		case CAMZOOM_Far: // far zoom low angle
			mGoalTargetDistance = mCameraParms->mFarLowDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mFarLowAngle());
			mGoalFOV            = mCameraParms->mFarLowFOV;
			mNearZPlane         = mCameraParms->mFarLowNear;
			mFarZPlane          = mCameraParms->mFarLowFar;
			mYOffset            = mCameraParms->mFarLowOffset;
			mDetachedWeight     = mCameraParms->mFarLowWeight;
			mDetachedParm       = mCameraParms->mFarLowDetached;
			break;
		}
		break;
	}
	case CAMANGLE_Overhead: {
		switch (mCameraZoomLevel) {
		case CAMZOOM_Near: // low zoom high angle
			mGoalTargetDistance = mCameraParms->mNearHighDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mNearHighAngle());
			mGoalFOV            = mCameraParms->mNearHighFOV;
			mNearZPlane         = mCameraParms->mNearHighNear;
			mFarZPlane          = mCameraParms->mNearHighFar;
			mYOffset            = mCameraParms->mNearHighOffset;
			mDetachedWeight     = mCameraParms->mNearHighWeight;
			mDetachedParm       = mCameraParms->mNearHighDetached;
			break;
		case CAMZOOM_Mid: // medium zoom high angle
			mGoalTargetDistance = mCameraParms->mMidHighDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mMidHighAngle());
			mGoalFOV            = mCameraParms->mMidHighFOV;
			mNearZPlane         = mCameraParms->mMidHighNear;
			mFarZPlane          = mCameraParms->mMidHighFar;
			mYOffset            = mCameraParms->mMidHighOffset;
			mDetachedWeight     = mCameraParms->mMidHighWeight;
			mDetachedParm       = mCameraParms->mMidHighDetached;
			break;
		case CAMZOOM_Far: // far zoom high angle
			mGoalTargetDistance = mCameraParms->mFarHighDist;
			mGoalVerticalAngle  = MTXDegToRad(mCameraParms->mFarHighAngle());
			mGoalFOV            = mCameraParms->mFarHighFOV;
			mNearZPlane         = mCameraParms->mFarHighNear;
			mFarZPlane          = mCameraParms->mFarHighFar;
			mYOffset            = mCameraParms->mFarHighOffset;
			mDetachedWeight     = mCameraParms->mFarHighWeight;
			mDetachedParm       = mCameraParms->mFarHighDetached;
			break;
		}
		break;
	}
	}
}

/**
 * @note Address: 0x802402C4
 * @note Size: 0x70
 */
void PlayCamera::setTargetThetaToWhistle()
{
	Vector3f pos         = mTargetObj->getPosition();
	NaviWhistle* whistle = mTargetObj->mWhistle;
	mCameraAngleTarget   = JMAAtan2Radian(pos.x - whistle->mPosition.x, pos.z - whistle->mPosition.z);
}

/**
 * @note Address: 0x80240334
 * @note Size: 0x10
 */
void PlayCamera::setFollowTime()
{
	mFollowTime = mCameraParms->mRotFollowTime;
}

/**
 * @note Address: 0x80240344
 * @note Size: 0x68
 */
void PlayCamera::setSmoothThetaSpeed()
{
	Controller* pad = mTargetObj->mController1;
	if (pad) {
		f32 maxSpeed = mCameraParms->mMaxRotSpeed.mValue * sys->mDeltaTime;
		// FPS Mode: velocidad por tick -> la aceleracion por tick escala con s^2.
		mSmoothMoveSpeed += pad->mMStick.mXPos * mCameraParms->mRotAccel.mValue * PC_ORIG_DT_SCALE() * PC_ORIG_DT_SCALE();
		mSmoothMoveSpeed = boundAboveBelow(mSmoothMoveSpeed, maxSpeed);
	}
}

/**
 * @note Address: 0x802403AC
 * @note Size: 0xE0
 */
void PlayCamera::changeTargetTheta()
{
#ifdef PIKI_PC_PORT
	// Solo la camara del capitan con mando consume el arrastre: la del otro
	// capitan tambien se actualiza y se lo quedaria a medias.
	if (mTargetObj && mTargetObj->mController1) {
		const f32 drag  = pc_window_take_camera_drag();
		const f32 pitch = pc_window_take_camera_pitch();
		const bool fp   = pcFirstPersonFor(mTargetObj);
		if (drag != 0.0f && (pc_settings_get_free_camera() || fp)) {
			// Aproximadamente media vuelta por una pasada de un ancho de pantalla.
			f32 angle = mCameraAngleTarget + drag * 3.2f;
			clampAngle(angle);
			mCameraAngleTarget = angle;
			mSmoothMoveSpeed   = 0.0f;
			// El cursor gira con la cámara alrededor del capitán para seguir en
			// el mismo sitio de la pantalla: su desplazamiento va en coordenadas
			// del mundo y, con la cámara girada, apuntaba a otro lado (#74).
			if (!fp) {
				pcNaviRotateCursor(mTargetObj, drag * 3.2f);
			}
		}
		if (fp) {
			sPcPitch += pitch * 3.2f;
			if (sPcPitch < -1.2f) sPcPitch = -1.2f;
			if (sPcPitch > 1.0f) sPcPitch = 1.0f;
		}
	}
#endif
	if (mFollowTime > 0.0f) {
		mFollowTime -= sys->mDeltaTime;
		setTargetThetaToWhistle();
	} else {
		f32 angle = mCameraAngleTarget - mSmoothMoveSpeed;
		clampAngle(angle);
		mCameraAngleTarget = angle;
	}
	mSmoothMoveSpeed *= PC_DAMP(mCameraParms->mRotDampRate.mValue); // FPS Mode
}

/**
 * @note Address: 0x8024048C
 * @note Size: 0x384
 */
void PlayCamera::changeTargetAtPosition()
{
	f32 M            = mDetachedWeight;
	Vector3f naviPos = mTargetObj->getPosition();
	Iterator<Creature> iterator((Stickers*)mTargetObj->mCPlateMgr);

	Vector3f newPos = naviPos * M;

	CI_LOOP(iterator)
	{
		Creature* obj = *iterator;
		if (obj) {
			Vector3f pos = obj->getPosition();
			newPos += pos;
			M += 1.0f;
		}
	}

	mGoalPosition = newPos / M;

	f32 distFromGoal = mGoalPosition.distance(naviPos);
	if (mDetachedParm < distFromGoal) {
		f32 ratio     = mDetachedParm / distFromGoal;
		f32 inv       = 1.0f - ratio;
		mGoalPosition = (naviPos * inv) + (mGoalPosition * ratio);
	}

	mGoalPosition.y += mYOffset;
}

/**
 * @note Address: 0x80240810
 * @note Size: 0x164
 */
void PlayCamera::updateParms(int flag)
{
	f32 rate            = PC_LERP(mCameraParms->mSettingChangeSpeed.mValue); // FPS Mode: suavizado por frame
	f32 invrate         = 1.0f - rate;
#ifdef PIKI_PC_PORT
	pcUpdateWheelZoom();
	mCurrTargetDistance = (mCurrTargetDistance * invrate) + (mGoalTargetDistance * sPcZoomMul * rate);
#else
	mCurrTargetDistance = (mCurrTargetDistance * invrate) + (mGoalTargetDistance * rate);
#endif
	mCurrVerticalAngle  = (mCurrVerticalAngle * invrate) + (mGoalVerticalAngle * rate);
	mViewAngle          = (mViewAngle * invrate) + (mGoalFOV * rate);
	mProjectionNear     = (mProjectionNear * invrate) + (mNearZPlane * rate);
#ifdef PIKI_PC_PORT
	if (pcFirstPersonFor(mTargetObj)) {
		mProjectionNear = 3.0f; // con el ojo en la cabeza, el plano normal recorta lo cercano
	}
#endif
	mProjectionFar      = (mProjectionFar * invrate) + (mFarZPlane * rate);
	if (flag & CAMFLAGS_InZoomCam) {
		rate    = PC_LERP(0.175f);
		invrate = 1.0f - rate;
	}
	mLookAtPosition = (mLookAtPosition * invrate) + (mGoalPosition * rate);

	CameraParms* parms = mCameraParms;

	f32 targetAngle = mCameraAngleTarget;
	f32 delta;
	if (targetAngle >= mCameraAngleCurrent) {
		delta = targetAngle - mCameraAngleCurrent;
		if (TAU - delta < delta) {
			targetAngle -= TAU;
		}
	} else {
		delta = mCameraAngleCurrent - targetAngle;
		if (TAU - delta < delta) {
			targetAngle += TAU;
		}
	}

	mCameraAngleCurrent += PC_LERP(parms->mRotSpeed.mValue) * (targetAngle - mCameraAngleCurrent); // FPS Mode

	f32 angle = mCameraAngleCurrent;
	clampAngle(angle);
	mCameraAngleCurrent = angle;
}

/**
 * @note Address: 0x80240974
 * @note Size: 0x12C
 */
void PlayCamera::updateVibration(int id)
{
	f32* vibrateTimer  = &mVibrateTimer[id];
	f32 newSpeed       = mVibrateSpeed[id] * sys->mDeltaTime;
	f32 packetStrength = 1.0f;
	mVibrateAngle[id] += newSpeed;
	mVibrateTimer[id] += sys->mDeltaTime;
	if (mVibrateAngle[id] > TAU) {
		mVibrateAngle[id] -= TAU;
	}

	if (mVibrateTimer[id] > mVibrateDuration[id]) {
		packetStrength -= (mVibrateTimer[id] - mVibrateDuration[id]) / 0.5f;
		if (packetStrength < 0.0f) {
			mVibrateEnabled[id] = false;
			mVibrateAngle[id]   = 0.0f;
			mVibrateTimer[id]   = 0.0f;
			packetStrength      = 0.0f;
		}
	}

	f32 angle             = mVibrateAngle[id];
	mVibrateRollAngle[id] = (packetStrength * mVibrateScale[id]) * mVibrateStrength[id] * sinf(angle);
}

/**
 * @note Address: 0x80240AA0
 * @note Size: 0x74
 */
void PlayCamera::otherVibFinished(int id)
{
	for (int i = 0; i < 3; i++) {
		if (mVibrateEnabled[i] && i != id) {
			mVibrateTimer[i] += 12800.0f;
		}
	}
}

/**
 * @note Address: 0x80240B14
 * @note Size: 0x174
 */
bool PlayCamera::isModCameraFinished()
{
	if (mChangePlayerState == CAMCHANGE_IsChanging) {
		f32 anglein  = mCameraAngleTarget;
		f32 angleout = mCameraAngleCurrent;
		if (anglein >= angleout) {
			if (TAU - (anglein - angleout) < (anglein - angleout)) {
				anglein -= TAU;
			}
		} else if (TAU - (angleout - anglein) < (angleout - anglein)) {
			anglein += TAU;
		}

		f32 anglediff = anglein - angleout;
#ifdef PIKI_PC_PORT
		if (absVal(anglediff) < 0.1f && absVal(mGoalTargetDistance * sPcZoomMul - mCurrTargetDistance) < 10.0f
#else
		if (absVal(anglediff) < 0.1f && absVal(mGoalTargetDistance - mCurrTargetDistance) < 10.0f
#endif
		    && absVal(mGoalVerticalAngle - mCurrVerticalAngle) < 0.1f && absVal(mGoalFOV - mViewAngle) < 1.0f) {
			if (mGoalPosition.distance(mLookAtPosition) < 50.0f) {
				mChangePlayerState = CAMCHANGE_None;
				return true;
			}
		}
	}
	return false;
}

/**
 * @note Address: 0x80240C88
 * @note Size: 0x158
 */
void PlayCamera::setCollisionCameraTargetPhi(int flag)
{
	if (flag & CAMFLAGS_InZoomCam) {
		mGoalVerticalAngle = getCollisionCameraTargetPhi(mCameraParms->mZoomAngle(), mCameraParms->mZoomDist());
		return;
	}

	// collision camera only active in behind cam
	if (mCameraSelAngle != CAMANGLE_Behind) {
		return;
	}

	if (mIsCollisionCamActive) {
		f32 phi;
		switch (mCameraZoomLevel) {
		case CAMZOOM_Near:
			phi = getCollisionCameraTargetPhi(mCameraParms->mNearLowAngle(), mCameraParms->mCollRadius());
			break;
		case CAMZOOM_Mid:
			phi = getCollisionCameraTargetPhi(mCameraParms->mMidLowAngle(), mCameraParms->mCollRadius());
			break;
		case CAMZOOM_Far:
			phi = getCollisionCameraTargetPhi(mCameraParms->mFarLowAngle(), mCameraParms->mCollRadius());
			break;
		default:
			phi = getCollisionCameraTargetPhi(mCameraParms->mZoomAngle(), mCameraParms->mCollRadius());
			break;
		}

		mGoalVerticalAngle = approach(mGoalVerticalAngle, phi, mCameraParms->mCollInterpSpeed());
		return;
	}

	// if we're in behind cam and our vertical angle has settled, activate collision cam
	if (absVal(mCurrVerticalAngle - mGoalVerticalAngle) < 0.1f) {
		mIsCollisionCamActive = true;
	}
}

/**
 * @note Address: 0x80240DE0
 * @note Size: 0x384
 */
f32 PlayCamera::getCollisionCameraTargetPhi(f32 angle, f32 dist)
{
	dist /= 15.0f;
	angle *= PI / 180.0f;

	f32 sinPhi, cosPhi;
	// this feels very fake-matchy, but it's the closest I've been able to find
	// the casts are load-bearing, and I hate the mix of the cosf and cosfc inlines :(
	// please someone find something better later -HP
	f32 cosTheta = (f32)cosfc(mCameraAngleTarget);
	f32 sinTheta = sinf(mCameraAngleTarget);
	sinPhi       = (f32)sinf(angle);
	cosPhi       = (f32)cosf(angle);

	for (int i = 1; i <= 15; i++) {
		f32 rad       = dist * (f32)i;
		f32 scaledSin = rad * cosPhi;
		f32 scaledCos = rad * sinPhi;
		f32 val       = (f32)i * mCameraParms->mCollCorrHeight();
		Vector3f pos(sinTheta * scaledSin + mGoalPosition.x, mGoalPosition.y + scaledCos, cosTheta * scaledSin + mGoalPosition.z);
		CurrTriInfo info;
		info.mPosition        = pos;
		info.mUpdateOnNewMaxY = 0;
		mapMgr->getCurrTri(info);

		if (gameSystem && gameSystem->mIsInCave) {
			if (info.mTriangle) {
				val = val + info.mMaxY;
			} else {
				val = pos.y + mCameraParms->mNoCollHeight();
			}
		} else {
			val = val + info.mMinY;
		}

		if (val > pos.y) {
			f32 anglediff = JMAAtan2Radian(val - mGoalPosition.y, rad);
			if (anglediff > angle) {
				angle = anglediff;
			}
		}
	}

	return angle;
}

} // namespace Game
