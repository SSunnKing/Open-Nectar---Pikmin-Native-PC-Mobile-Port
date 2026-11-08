#include "nectar_assets/standin_objects.h"

// Authored animation schedules for the 45 object, ship-part, plant, boss and enemy bundles of
// the boot manifest (everything but pikis/animMgr.bin, which standin_anims.cpp owns) except the
// two gates, whose table and bundles standin_sandbox.cpp and standin_world.cpp write. Every
// number is chosen here from the key a state machine in games/pikmin1/src reads; none is a
// recorded value from the retail game. The rules are in standin_objects.h.
//
// The recipe, per info { frame count N, 'p00' flags, 'spd', anim-key frames, info keys }:
//  - an info nobody waits on is "plain": the shared 40 frames, anim keys [0, 39], no keys,
//    the source default flags (2) and 30 frames a second;
//  - an info the AI waits on to finish only needs a finite length ("finite");
//  - an info that must loop has KEY_LoopStart at its first anim key and KEY_LoopEnd at its last;
//  - an info with a KEY_Action0 consumer carries it at an authored frame ("action0");
//  - enemy Move1/Move2 use flags 6 (UseDynamicSpeed plus the drive-length speed that
//    BTeki::getVelocityAnimationSpeed reads).
// An info key is { index into the anim-key list, key type } with the types of AnimKeyRecord:
// 0 loop start, 1 loop end, 2 KEY_Action0, 3 KEY_Action1. Event keys are omitted except the
// Onion boot motions' one route-opening effect. Names and curves are authored by
// standin_objects.cpp. Joint counts come from the stand-in model of each bundle.

namespace nectar {

namespace {

using Info = ObjectAnimInfoProfile;
constexpr int kN = kStandInDefaultFrames;

Info plain() { return { kN, 2, 30.0f, { 0, kN - 1 }, {}, {} }; }
Info finite(int frames) { return { frames, 2, 30.0f, { 0, frames - 1 }, {}, {} }; }
Info loop() { return { kN, 2, 30.0f, { 0, kN - 1 }, { { 0, 0 }, { 1, 1 } }, {} }; }
Info action0(int frame, int flags = 2) { return { kN, flags, 30.0f, { 0, frame, kN - 1 }, { { 1, 2 } }, {} }; }
std::vector<Info> plains(int count) { return std::vector<Info>(std::size_t(count), plain()); }

// GoalAI (itemAI.cpp): info 0 emits one seed per LoopStart/LoopEnd pass; infos 1 and 12 idle in
// a loop; infos 3 to 5 are the boot motions (BootEmit runs on KEY_Action0, and effect value 2
// opens the Onion's route way point, GoalItem::playEffect), so the route opens before the seed;
// 6 to 8 take off and 9 to 11 land (KEY_Finished only).
std::vector<Info> goalInfos()
{
	std::vector<Info> v = plains(13);
	v[0] = { 60, 2, 30.0f, { 0, 20, 40, 59 }, { { 1, 0 }, { 2, 1 } }, {} };
	v[1] = loop();
	v[12] = loop();
	for (int boot = 3; boot <= 5; boot++) v[std::size_t(boot)] = { kOnionBootFrames, 2, 30.0f, { 0, kOnionBootSeedFrame, kOnionBootFrames - 1 }, { { 1, 2 } }, { { kOnionBootRouteFrame, 2 } } };
	return v;
}

// PikiHeadAI (PikiHeadMotion): TaneFall 0, TaneUp 1, Hatuga 2 (finishes), UgWait 3 (loops),
// UgGrow 4 (KEY_Action0 grows the sprout), Sioreru 5 (wilts), TaneDown 6 (finishes).
std::vector<Info> pikiheadInfos()
{
	std::vector<Info> v = plains(7);
	v[3] = loop();
	v[4] = action0(24);
	v[5] = finite(60);
	v[6] = finite(20);
	return v;
}

// UfoItem::animationKeyUpdated (UfoMotion): infos 0 (WaitTutorial), 1 (Wait) and 16 (OpenClose)
// freeze on KEY_Action0 and report KEY_Finished; 11 to 15 are the take-off motions.
std::vector<Info> ufoInfos()
{
	std::vector<Info> v = plains(17);
	for (int i : { 0, 1, 16 }) v[std::size_t(i)] = action0(10);
	for (int i = 11; i <= 15; i++) v[std::size_t(i)] = finite(120);
	return v;
}

// TekiMotion: Dead 0, Damage 1, Wait1/2 2-3, WaitAct1/2 4-5, Move1/2 6-7, Attack 8, Flick 9, Type1-5.
// Attack hits on KEY_Action0 (the Dwarf Bulborb also has a KEY_Action1 that ends the hit window)
// and Flick throws the Pikmin on KEY_Action0 (TaiFlickingAction).
std::vector<Info> tekiInfos(bool bulborb)
{
	std::vector<Info> v = plains(15);
	v[6] = { kN, 6, 30.0f, { 0, kN - 1 }, {}, {} };
	v[7] = v[6];
	v[8] = bulborb ? Info { kN, 2, 30.0f, { 0, 10, 14, kN - 1 }, { { 1, 2 }, { 2, 3 } }, {} } : action0(10);
	v[9] = action0(12);
	return v;
}

} // namespace

const std::vector<ObjectAnimProfile>& objectAnimProfiles()
{
	static const std::vector<ObjectAnimProfile> list = {
		{ "bomb", "dataDir/objects/bomb/anims.bin", "dataDir/objects/bomb/bomb.anm", "objects/bomb/motion", 1, plains(1) },
		{ "fl_water", "dataDir/objects/fl_water/anims.bin", "dataDir/objects/fl_water/fl_water.anm", "objects/fl_water/motion", 1, { action0(36, 5), finite(20) } },
		{ "goal", "dataDir/objects/goal/anims.bin", "dataDir/objects/goal/goal.anm", "objects/goal/motion", 1, goalInfos() },
		{ "pikihead", "dataDir/objects/pikihead/anims.bin", "dataDir/objects/pikihead/pikihead.anm", "objects/pikihead/motion", 1, pikiheadInfos() },
		{ "water", "dataDir/objects/water/anims.bin", "dataDir/objects/water/water.anm", "objects/water/motion", 1, plains(4) },
		{ "ufo0705", "dataDir/objects/ufo/ufo0705.bin", "dataDir/objects/ufo/ufo0705.anm", "objects/ufo/motion", 1, ufoInfos() },
		{ "white1", "dataDir/objects/pellets/white1.bin", "dataDir/objects/pellets/white1.anm", "objects/pellets/motion1", 1, { loop() } },
		{ "white2", "dataDir/objects/pellets/white2.bin", "dataDir/objects/pellets/white2.anm", "objects/pellets/motion2", 1, { loop() } },
		{ "white3", "dataDir/objects/pellets/white3.bin", "dataDir/objects/pellets/white3.anm", "objects/pellets/motion3", 1, { loop() } },
		{ "white4", "dataDir/objects/pellets/white4.bin", "dataDir/objects/pellets/white4.anm", "objects/pellets/motion4", 1, { loop() } },
		{ "soto1", "dataDir/objects/ufoparts/soto1.bin", "dataDir/objects/ufoparts/soto1.anm", "objects/ufoparts/msoto1", 1, plains(5) },
		{ "soto2", "dataDir/objects/ufoparts/soto2.bin", "dataDir/objects/ufoparts/soto2.anm", "objects/ufoparts/msoto2", 1, plains(5) },
		{ "soto3", "dataDir/objects/ufoparts/soto3.bin", "dataDir/objects/ufoparts/soto3.anm", "objects/ufoparts/msoto3", 1, plains(5) },
		{ "soto4", "dataDir/objects/ufoparts/soto4.bin", "dataDir/objects/ufoparts/soto4.anm", "objects/ufoparts/msoto4", 1, plains(5) },
		{ "soto5", "dataDir/objects/ufoparts/soto5.bin", "dataDir/objects/ufoparts/soto5.anm", "objects/ufoparts/msoto5", 1, plains(5) },
		{ "fuzoku1", "dataDir/objects/ufoparts/fuzoku1.bin", "dataDir/objects/ufoparts/fuzoku1.anm", "objects/ufoparts/mfuzoku1", 1, plains(5) },
		{ "fuzoku2", "dataDir/objects/ufoparts/fuzoku2.bin", "dataDir/objects/ufoparts/fuzoku2.anm", "objects/ufoparts/mfuzoku2", 1, plains(5) },
		{ "fuzoku3", "dataDir/objects/ufoparts/fuzoku3.bin", "dataDir/objects/ufoparts/fuzoku3.anm", "objects/ufoparts/mfuzoku3", 1, plains(5) },
		{ "fuzoku4", "dataDir/objects/ufoparts/fuzoku4.bin", "dataDir/objects/ufoparts/fuzoku4.anm", "objects/ufoparts/mfuzoku4", 1, plains(5) },
		{ "fuzoku5", "dataDir/objects/ufoparts/fuzoku5.bin", "dataDir/objects/ufoparts/fuzoku5.anm", "objects/ufoparts/mfuzoku5", 1, plains(5) },
		{ "fuzoku6", "dataDir/objects/ufoparts/fuzoku6.bin", "dataDir/objects/ufoparts/fuzoku6.anm", "objects/ufoparts/mfuzoku6", 1, plains(5) },
		{ "fuzoku7", "dataDir/objects/ufoparts/fuzoku7.bin", "dataDir/objects/ufoparts/fuzoku7.anm", "objects/ufoparts/mfuzoku7", 1, plains(5) },
		{ "fuzoku8", "dataDir/objects/ufoparts/fuzoku8.bin", "dataDir/objects/ufoparts/fuzoku8.anm", "objects/ufoparts/mfuzoku8", 1, plains(5) },
		{ "fuzoku9", "dataDir/objects/ufoparts/fuzoku9.bin", "dataDir/objects/ufoparts/fuzoku9.anm", "objects/ufoparts/mfuzoku9", 1, plains(5) },
		{ "fuzoku10", "dataDir/objects/ufoparts/fuzoku10.bin", "dataDir/objects/ufoparts/fuzoku10.anm", "objects/ufoparts/mfuzoku10", 1, plains(5) },
		{ "fuzoku11", "dataDir/objects/ufoparts/fuzoku11.bin", "dataDir/objects/ufoparts/fuzoku11.anm", "objects/ufoparts/mfuzoku11", 1, plains(5) },
		{ "naka1", "dataDir/objects/ufoparts/naka1.bin", "dataDir/objects/ufoparts/naka1.anm", "objects/ufoparts/mnaka1", 1, plains(5) },
		{ "naka4", "dataDir/objects/ufoparts/naka4.bin", "dataDir/objects/ufoparts/naka4.anm", "objects/ufoparts/mnaka4", 1, plains(4) },
		{ "naka7", "dataDir/objects/ufoparts/naka7.bin", "dataDir/objects/ufoparts/naka7.anm", "objects/ufoparts/mnaka7", 1, plains(4) },
		{ "chidome", "dataDir/objects/plants/chidome.bin", "dataDir/objects/plants/chidome.anm", "objects/plants/chidome", 1, plains(1) },
		{ "clover", "dataDir/objects/plants/clover.bin", "dataDir/objects/plants/clover.anm", "objects/plants/clover", 1, plains(1) },
		{ "hae", "dataDir/objects/plants/hae.bin", "dataDir/objects/plants/hae.anm", "objects/plants/hae", 1, plains(1) },
		{ "hutaba", "dataDir/objects/plants/hutaba.bin", "dataDir/objects/plants/hutaba.anm", "objects/plants/hutaba", 1, plains(1) },
		{ "ine", "dataDir/objects/plants/ine.bin", "dataDir/objects/plants/ine.anm", "objects/plants/ine", 1, plains(1) },
		{ "tanpopo", "dataDir/objects/plants/tanpopo.bin", "dataDir/objects/plants/tanpopo.anm", "objects/plants/tanpopo", 1, plains(1) },
		{ "mizu", "dataDir/bosses/mizu/anims.bin", "dataDir/bosses/mizu/mizu.anm", "bosses/mizu/motion", 1, plains(11) },
		{ "snake", "dataDir/bosses/snake/anims.bin", "dataDir/bosses/snake/snake.anm", "bosses/snake/motion", 1, plains(15) },
		{ "beatle", "dataDir/tekikeys/beatle.key", "dataDir/tekis/beatle/beatle.anm", "tekis/beatle/motion", 1, tekiInfos(false) },
		{ "chappy", "dataDir/tekikeys/chappy.key", "dataDir/tekis/chappy/chappy.anm", "tekis/chappy/motion", 1, tekiInfos(true) },
		{ "iwagon", "dataDir/tekikeys/iwagon.key", "dataDir/tekis/iwagon/iwagon.anm", "tekis/iwagon/motion", 1, tekiInfos(false) },
		{ "kabekuiA", "dataDir/tekikeys/kabekuiA.key", "dataDir/tekis/kabekuiA/kabekuiA.anm", "tekis/kabekuiA/motion", 1, tekiInfos(false) },
		{ "kabekuiB", "dataDir/tekikeys/kabekuiB.key", "dataDir/tekis/kabekuiB/kabekuiB.anm", "tekis/kabekuiB/motion", 1, tekiInfos(false) },
		{ "otama", "dataDir/tekikeys/otama.key", "dataDir/tekis/otama/otama.anm", "tekis/otama/motion", 1, tekiInfos(false) },
		{ "palm", "dataDir/tekikeys/palm.key", "dataDir/tekis/palm/palm.anm", "tekis/palm/motion", 1, tekiInfos(false) },
		{ "swallow", "dataDir/tekikeys/swallow.key", "dataDir/tekis/swallow/swallow.anm", "tekis/swallow/motion", 1, tekiInfos(false) },
	};
	return list;
}

} // namespace nectar
