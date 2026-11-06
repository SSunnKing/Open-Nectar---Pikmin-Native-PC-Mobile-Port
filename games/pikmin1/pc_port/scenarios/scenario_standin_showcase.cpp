// standin_showcase: flat prop layout + fixed camera for 640x480 stand-in art captures.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "GlobalGameOptions.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "UfoItem.h"
#include "MapMgr.h"
#include "Camera.h"
#include "scenarios/scenario_progress.h"
#include "scenarios/pc_scenario.h"

namespace {

// The captain spawns in front of the ship generator (standin_showcase_stage.h);
// the ship is then moved to the back right of the frame so he does not cover it.
constexpr float kShowcaseShipX = 74.0f;
constexpr float kShowcaseShipZ = 30.0f;
// Side-on to the camera, nose to the right of the frame.
constexpr float kShowcaseShipYaw = 1.5707964f;

void snapShowcaseShip()
{
	if (!itemMgr) return;
	UfoItem* ufo = itemMgr->getUfo();
	if (!ufo) return;
	Vector3f p(kShowcaseShipX, 0.0f, kShowcaseShipZ);
	if (mapMgr) p.y = mapMgr->getMinY(p.x, p.z, true);
	// UfoItem::update puts mSRT.t back on mSpotlightPosition every tick.
	ufo->mSpotlightPosition = p;
	ufo->mSRT.t             = p;
	ufo->mFaceDirection     = kShowcaseShipYaw;
	ufo->mSRT.r.y           = kShowcaseShipYaw;
}

void updateShowcaseCamera(Navi* navi)
{
	if (!navi) return;
	// Fixed offsets from the captain: the eye is in front of, above and right of him,
	// looking back along -z so the Pikmin and the captain face the camera and the
	// onion and the ship fill the upper half of the frame.
	Vector3f focus = navi->mSRT.t;
	focus.x += 20.0f;
	focus.y += 12.0f;
	focus.z -= 24.0f;
	Vector3f eye(navi->mSRT.t.x + 18.0f, navi->mSRT.t.y + 55.0f, navi->mSRT.t.z + 91.0f);
	PcScenarioDebugCamera cam {};
	cam.active  = true;
	cam.eyeX    = eye.x;
	cam.eyeY    = eye.y;
	cam.eyeZ    = eye.z;
	cam.focusX  = focus.x;
	cam.focusY  = focus.y;
	cam.focusZ  = focus.z;
	cam.fov     = 40.0f;
	pc_scenario_set_debug_camera(cam);
}

void spawnShowcasePiki(Navi* navi, int colour, PikiHappa happa, int slot)
{
	if (!scenario_progress::spawnPiki(navi, colour, 1, slot)) return;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* p = static_cast<Piki*>(*it);
		if (!p || p->mColor != colour) continue;
		Vector3f want = navi->mSRT.t;
		want.x += 12.0f * float(slot) + 20.0f;
		want.z += 5.0f;
		want.y = p->mSRT.t.y;
		p->mSRT.t = want;
		p->setFlower(happa); // also swaps the leaf, bud or flower model
		break;
	}
}

class StandinShowcase : public PcScenario {
public:
	const char* name() const override { return "standin_showcase"; }
	const char* stageFile() const override { return "stages/sandbox_showcase.ini"; }

	void providePads(unsigned /*tick*/, PcInputPad& /*pad0*/) override
	{
		if (Navi* navi = naviMgr ? naviMgr->getNavi(0) : nullptr) {
			// The whistle cursor and its marker would sit between the captain and the
			// camera; this capture is about the models.
			navi->mIsCursorVisible = FALSE;
			// Face the camera (+z) so the eyes are in the shot.
			navi->mFaceDirection = 0.35f;
			Iterator it(pikiMgr);
			CI_LOOP(it)
			{
				if (Piki* p = static_cast<Piki*>(*it)) p->mFaceDirection = 0.0f;
			}
			updateShowcaseCamera(navi);
		}
	}

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi(0);
		if (GoalItem* onion = itemMgr->getContainer(scenario_progress::kRed)) onion->pcSnapLanded();
		snapShowcaseShip();

		spawnShowcasePiki(navi, scenario_progress::kRed, Leaf, 0);
		spawnShowcasePiki(navi, scenario_progress::kYellow, Bud, 1);
		spawnShowcasePiki(navi, scenario_progress::kBlue, Flower, 2);

		updateShowcaseCamera(navi);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= kTicks) scenarioPass();
	}

private:
	static constexpr unsigned kTicks = 240;
};

} // namespace

PcScenario* pc_scenario_make_standin_showcase(void)
{
	static StandinShowcase s;
	return &s;
}

#endif
