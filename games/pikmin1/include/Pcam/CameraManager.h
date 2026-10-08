#ifndef _PCAM_CAMERAMANAGER_H
#define _PCAM_CAMERAMANAGER_H

#include "Node.h"
#include "types.h"

class Camera;
class Controller;
class Creature;
class PcamCamera;
class PcamMotionInfo;
class PeveEvent;
class Vector3f;

/**
 * @brief TODO
 */
enum PcamVibrationEventIDs {
	PCAMVIB_NULL          = -1,
	PCAMVIB_SideVibration = 0,
	PCAMVIB_Damage        = 1,
	PCAMVIB_Vibration1    = 2,
	PCAMVIB_Vibration2    = 3,
	PCAMVIB_LongVibration = 4,
	PCAMVIB_VibrationCount, // 5
};

/**
 * @brief TODO
 *
 * @note Size: 0x30.
 */
class PcamCameraManager : public Node {
public:
	PcamCameraManager(Camera*, Controller*);

	virtual void update(); // _10

	void startCamera(Creature*);
	void updateVibrationEvent();
#if defined(PIKI_PC_PORT)
	// mirror=false: solo esta cámara (daño propio de un Olimar).
	void startVibrationEvent(int, immut Vector3f&, bool mirror = true);
#else
	void startVibrationEvent(int, immut Vector3f&);
#endif
	void outputNaviPosition(Vector3f&);

	// unused/inlined:
	void startMotion(PcamMotionInfo&);
	void finishMotion();

	// _00     = VTBL
	// _00-_20 = Node
	PcamCamera* mCamera;          // _20
	Controller* mController;      // _24
	int mCurrEventIndex;          // _28, see PcamVibrationEventIDs enum
	PeveEvent** mVibrationEvents; // _2C, array of events indexed by mCurrEventIndex
};

extern PcamCameraManager* cameraMgr;
#if defined(PIKI_PC_PORT)
// Cooperativo: segundo manager (cámara de P2). Las vibraciones que recibe
// `cameraMgr` se reenvían aquí; cada cámara aplica su propio filtro de
// distancia a su Olimar.
extern PcamCameraManager* cameraMgrP2;
// Manager de P1 estable (cameraMgr se conmuta durante la pasada de P2).
extern PcamCameraManager* cameraMgrP1;
/// Manager de la cámara del Olimar con ese mNaviID (P2 si existe, si no P1).
inline PcamCameraManager* pcCameraMgrForNavi(int naviID)
{
	if (naviID == 1 && cameraMgrP2)
		return cameraMgrP2;
	return cameraMgrP1 ? cameraMgrP1 : cameraMgr;
}
#endif

#endif
