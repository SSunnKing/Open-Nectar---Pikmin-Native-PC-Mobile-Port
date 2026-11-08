#pragma once

// Cutscene ("cinema") writers: the .cin movie script and the .dsk scene
// (camera and light curves). Both are CmdStream text, read by
// CinematicPlayer::loadCin (plugPikiColin/cinePlayer.cpp) and SceneData::parse /
// getAnimInfo (sysCommon/shapeBase.cpp). The format notes and the stand-in set
// are in games/pikmin1/tools/scenarios/standin-cinema.md.
//
// Pure functions: a plain input struct, text out, an error string, no file I/O.
// The writers refuse what the loaders would crash on: a cut with no scene to
// point at (addSceneCut reads findScene(0)->mNumFrames), a cut actor that is not
// in the actor list (findActor returns null), actor fields before an actor, and
// a scene with no camera (MovieInfo::setCamera reads mCameraData[0]).

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

// ── .cin ────────────────────────────────────────────────────────────────────

/// AnimKey event types (Animator.h EAnimEventType).
constexpr int kCinEventNotify = 0; ///< gameInterface->message(cmd, value): a MOVIECMD_* (Interface.h)
constexpr int kCinEventAction = 1; ///< demoEventMgr->act(cmd, value): cmd is the sender, value the event

/// DemoEventMgr senders and events (demoEvent.cpp DemoEventSender, DemoEventOnion).
constexpr int kDemoSenderBluOnion = 2;
constexpr int kDemoSenderRedOnion = 3;
constexpr int kDemoSenderYelOnion = 4;
constexpr int kDemoOnionLand      = 3;
constexpr int kDemoOnionEmitCone  = 4;
constexpr int kDemoOnionBoot      = 5;
constexpr int kDemoOnionBonusPiki = 6;

/// MOVIECMD_TextDemo (Interface.h): the value is a zen::ogScrTutorialMgr::EnumTutorial.
constexpr int kMovieCmdTextDemo = 0;
/// More MOVIECMD_* values (Interface.h) that the extinction movies send.
constexpr int kMovieCmdGameEndCondition = 5; ///< value: GameEndCause (FlowController.h)
constexpr int kMovieCmdForceResults     = 6; ///< clears mGameEndFlag (newPikiGame.cpp)
constexpr int kMovieCmdFadeOut          = 9;
constexpr int kMovieCmdFadeIn           = 10;
constexpr int kMovieCmdCleanupDayEnd    = 11;
constexpr int kMovieCmdStartTotalResults = 12; ///< starts the final results window (demo73 bad ending, demo77 neutral and happy)
constexpr int kMovieCmdSpecialDayEnd     = 13; ///< all 30 parts: ends the day with the happy ending (demo26)
constexpr int kEndCausePikminZero       = 0; ///< ENDCAUSE_PikminZero
constexpr int kMovieCmdForceDayEnd      = 2; ///< gamecore->forceDayEnd(), mIsDayEndTriggered (demo17, the tutorial's day end)

/// The ship as a DemoEventMgr sender and its Henka event (demoEvent.cpp DemoEventSender::UFO, DemoEventUFO::Henka).
constexpr int kDemoSenderUfo = 1;
constexpr int kDemoUfoHenka  = 2;

/// CinePlayerFlags (CinematicPlayer.h).
constexpr std::uint32_t kCinLocalized = 1u << 0;
constexpr std::uint32_t kCinHideNavi  = 1u << 2;
constexpr std::uint32_t kCinNaviNoAI  = 1u << 6;
constexpr std::uint32_t kCinNonGameMovie = 1u << 7; ///< NonGameMovie: time stops, the AI does not update

/// SceneCut flags: playback mode = (flags >> 1) & 7. 3 plays once, 5 loops
/// until skipped (CINMODE_LoopScene).
constexpr int kCutPlayOnce = 3;
constexpr int kCutLoop     = 5;

struct CinActorFiles {
	std::string shape;  ///< .mod path, also the actor's name in cuts
	std::string bundle; ///< .anm path; empty: not written
	std::string anims;  ///< AnimMgr .bin path; empty: not written
};

struct CinCutActor {
	std::string shape;        ///< must name an entry of Cinema::actors
	std::uint32_t acflags = 2; ///< CineActorFlags; 2 is the ActorInstance default
	int animState  = 1;
	int colourAnim = -1;
};

struct CinKey {
	int eventType = kCinEventAction;
	int cmd       = 0;
	int value     = 0;
	int frame     = 0; ///< scene frame (not movie time) at which the key fires
};

struct CinCut {
	int scene      = 0; ///< index into Cinema::scenes
	int startFrame = 0;
	int endFrame   = 0;
	int flags      = kCutPlayOnce;
	std::vector<CinCutActor> actors;
	std::vector<CinKey> keys;
};

struct Cinema {
	int type            = 0; ///< read and never used by the game
	std::uint32_t flags = 0; ///< CinePlayerFlags
	std::vector<std::string> scenes; ///< .dsk paths, in scene-index order
	std::vector<CinActorFiles> actors;
	std::vector<CinCut> cuts;
};

bool writeCin(const Cinema& cin, std::string& out, std::string& error);

// ── .dsk ────────────────────────────────────────────────────────────────────

/// One key of a curve. A track of one key is a constant (only `value` is
/// written). Two or more keys are a Hermite curve (shapeBase.cpp extract): three
/// floats per key, frame, value, tangent, frames strictly increasing.
struct DskKey {
	float frame   = 0;
	float value   = 0;
	float tangent = 0;
};
using DskTrack = std::vector<DskKey>; ///< empty: not written (the game uses its default)

struct DskCamera {
	std::string name = "cam";
	DskTrack posX, posY, posZ;
	DskTrack latX, latY, latZ; ///< look-at point
	DskTrack twist;
	DskTrack fovy;             ///< empty: the game uses 1.0, so always set it
	DskTrack nearZ, farZ;      ///< empty: the camera keeps 1 / 15000
};

struct DskLight {
	std::string name = "light";
	DskTrack posX, posY, posZ;
	DskTrack r, g, b;  ///< 0..255
	DskTrack visible;  ///< the light is on where this is exactly 1.0; empty: on
};

struct DskScene {
	int numFrames = 0; ///< the default end frame of a cut on this scene
	std::vector<DskCamera> cameras; ///< at least one
	std::vector<DskLight> lights;
};

bool writeDsk(const DskScene& scene, std::string& out, std::string& error);

// ── Stand-in cinemas (issue #59 follow-up) ──────────────────────────────────

struct StandInCinemaFile {
	std::string path; ///< relative to assets/, e.g. "dataDir/cinemas/demo60.cin"
	std::string text;
};

/// Every stand-in .cin and .dsk, in a fixed order. See standin-cinema.md.
bool writeStandInCinemas(std::vector<StandInCinemaFile>& out, std::string& error);

} // namespace nectar
