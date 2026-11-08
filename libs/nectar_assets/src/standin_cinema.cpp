// Stand-in cinemas for the disc-free scenario tests (cinema.h,
// games/pikmin1/tools/scenarios/standin-cinema.md).
//
// Provenance (#190): authored fixtures. The game code fixes only which movie a
// .cin path is (moviePlayer.cpp movieTitles and the translation tables), the
// CinePlayerFlags it reads, and the key events a movie's game logic acts on, by
// their code enums (demoEvent.cpp, Interface.h, ogTutorial.h). Everything else
// is chosen here: the .dsk scene names (all under cinemas/standin/), the
// cameras and lights, every scene length, key frame and loop window. Frames sit
// on a 10-frame grid. Each movie's comment states the contract its frames keep.
#include "nectar_assets/cinema.h"

namespace nectar {
namespace {

// zen::ogScrTutorialMgr::EnumTutorial (ogTutorial.h).
constexpr int kTutRedOnion = 1;  ///< TUT_RedOnion
constexpr int kTutGetEngine = 9; ///< TUT_GetEngine, the ship-upgrade text after the Main Engine
constexpr int kTutGetParts  = 17; ///< TUT_GetParts: with the ship text type CollectEngine, a scene skip and no window
constexpr int kTutZenmetu  = 13; ///< TUT_Zenmetu, the post-extinction text

/// A slow dolly from behind and above the origin towards it, looking at the
/// origin, 30 degrees, one white light overhead. `frames` long.
DskScene standInScene(int frames)
{
	const float end = float(frames);
	DskScene s;
	s.numFrames = frames;
	DskCamera c;
	c.name = "standin_cam";
	c.posX = { { 0, -120, 0 }, { end, 120, 0 } };
	c.posY = { { 0, 420, 0 }, { end, 360, 0 } };
	c.posZ = { { 0, 640, 0 }, { end, 560, 0 } };
	c.latX = { { 0, 0, 0 } };
	c.latY = { { 0, 20, 0 } };
	c.latZ = { { 0, 0, 0 } };
	c.fovy = { { 0, 30, 0 } };
	s.cameras.push_back(c);
	DskLight l;
	l.name = "standin_light";
	l.posX = { { 0, 0, 0 } };
	l.posY = { { 0, 800, 0 } };
	l.posZ = { { 0, 200, 0 } };
	l.r    = { { 0, 255, 0 } };
	l.g    = { { 0, 255, 0 } };
	l.b    = { { 0, 255, 0 } };
	s.lights.push_back(l);
	return s;
}

CinKey action(int sender, int event, int frame) { return { kCinEventAction, sender, event, frame }; }
CinKey notify(int cmd, int value, int frame) { return { kCinEventNotify, cmd, value, frame }; }

struct Movie {
	const char* cin;
	Cinema cinema;
	std::vector<int> sceneFrames; ///< one per scene path, the .dsk length
};

/// The authored scene name for scene `n` (0-based) of movie `id`.
std::string standInDsk(const char* id, int n) { return std::string("cinemas/standin/demo") + id + "_" + char('a' + n) + ".dsk"; }

std::vector<Movie> movies()
{
	std::vector<Movie> list;
	const std::uint32_t hideNavi = kCinHideNavi | kCinNaviNoAI;

	// Bonus seeds after an extinction (DEMOID_Unk60Cat). demo60 is what the
	// sandbox (STAGE_TESTMAP, translated as Impact Site) plays from finalSetup
	// on Day 1; demo61 is the Forest of Hope's. BonusPiki from each Onion (the
	// game emits only for an Onion the player has and with no Pikmin of its
	// colour), then the extinction text. Contract: a non-zero-length movie with
	// a camera, so the intro's START skip has something to skip.
	for (const char* id : { "60", "61" }) {
		Movie m;
		m.cin            = id[1] == '0' ? "dataDir/cinemas/demo60.cin" : "dataDir/cinemas/demo61.cin";
		m.cinema.flags   = hideNavi;
		m.cinema.scenes  = { standInDsk(id, 0), standInDsk(id, 1) };
		m.sceneFrames    = { 60, 30 };
		CinCut first;
		first.scene    = 0;
		first.endFrame = 60;
		first.keys     = { action(kDemoSenderBluOnion, kDemoOnionBonusPiki, 10), action(kDemoSenderRedOnion, kDemoOnionBonusPiki, 10),
                           action(kDemoSenderYelOnion, kDemoOnionBonusPiki, 10), notify(kMovieCmdTextDemo, kTutZenmetu, 50) };
		CinCut second;
		second.scene    = 1;
		second.endFrame = 30;
		m.cinema.cuts   = { first, second };
		list.push_back(m);
	}

	// Forest of Hope landing (DEMOID_LandingForest): played when a scenario
	// enters stages/stage1.ini (onion_day3, throw_flight). The Onion keys are
	// the two that the landing's game logic acts on (both guarded by
	// hasContainer): Land, then EmitCone.
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo41.cin";
		m.cinema.flags  = hideNavi;
		m.cinema.scenes = { standInDsk("41", 0) };
		m.sceneFrames   = { 90 };
		CinCut cut;
		cut.endFrame  = 90;
		cut.keys      = { action(kDemoSenderYelOnion, kDemoOnionLand, 10), action(kDemoSenderYelOnion, kDemoOnionEmitCone, 70) };
		m.cinema.cuts = { cut };
		list.push_back(m);
	}

	// Pikmin into the Onion, Forest of Hope (DEMOID_PikminInOnyonForest): no
	// keys; played on the way into stage1 (demo_timers). Two short scenes.
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo45.cin";
		m.cinema.flags  = hideNavi;
		m.cinema.scenes = { standInDsk("45", 0), standInDsk("45", 1) };
		m.sceneFrames   = { 40, 40 };
		CinCut a, b;
		a.endFrame    = 40;
		b.scene       = 1;
		b.endFrame    = 40;
		m.cinema.cuts = { a, b };
		list.push_back(m);
	}

	// Onion discovery: red (DEMOID_FindRedOnyon, demo04; the sandbox translates
	// DEMOID_FindOnyon to it), blue (DEMOID_FindBlueOnyon, demo06, which
	// DEMOFLAG_DiscoverBlueOnyon starts untranslated; the object stage's Onion is
	// blue) and yellow (DEMOID_FindYellowOnyon, demo05, which
	// DEMOFLAG_DiscoverYellowOnyon starts; scenario onion_day3). Localized at the
	// Onion. Boot at frame kDiscoveryBoot starts the Onion's boot motion (info
	// colour + 3), whose KEY_Action0 emits the first seed; EmitCone follows.
	// Contract (#205 skip cases): Boot < Boot + the boot motion's KEY_Action0
	// frame (the stand-in Onion profile, standin_object_profiles.cpp) < EmitCone,
	// with frames free between each, so a START skip can land before Boot,
	// before the seed and after it. The cone frames are tied to that profile and
	// move with it. demo04 opens the red Onion text; the others have no text key.
	{
		constexpr int kDiscoveryBoot = 10;
		struct Discovery {
			const char* cin;
			const char* id;
			int sender, cone, frames;
		};
		const Discovery discoveries[] = {
			{ "dataDir/cinemas/demo04.cin", "04", kDemoSenderRedOnion, 300, 330 },
			{ "dataDir/cinemas/demo06.cin", "06", kDemoSenderBluOnion, 270, 300 },
			{ "dataDir/cinemas/demo05.cin", "05", kDemoSenderYelOnion, 270, 300 },
		};
		for (const Discovery& d : discoveries) {
			Movie m;
			m.cin           = d.cin;
			m.cinema.flags  = kCinLocalized | hideNavi;
			m.cinema.scenes = { standInDsk(d.id, 0) };
			m.sceneFrames   = { d.frames };
			CinCut cut;
			cut.endFrame = d.frames; // EmitCone: after the seed, before the movie's end
			cut.keys     = { action(d.sender, kDemoOnionBoot, kDiscoveryBoot), action(d.sender, kDemoOnionEmitCone, d.cone) };
			if (d.sender == kDemoSenderRedOnion) cut.keys.push_back(notify(kMovieCmdTextDemo, kTutRedOnion, d.cone + 10));
			m.cinema.cuts = { cut };
			list.push_back(m);
		}
	}

	// Pikmin extinction (DEMOID_Extinction, demo46), then the day end after it
	// (DEMOID_ExtDayEnd, translated per stage: the sandbox plays demo47, Impact
	// Site's). Contract (newPikiGame.cpp, GameMovieInterface::parse): the first
	// GameEndCondition message starts demo46; demo46 must send the second one
	// (PikminZero), which with the end flag set starts the day-end movie and
	// sets the clock to the end hour; demo47 must send ForceResults, which clears
	// the end flag and releases MessageModeState into the day end. Without them
	// the chain never ends. Authored: demo46 fades out at 30 and sends
	// GameEndCondition at 50; demo47 fades in at 0 and sends ForceResults at 50.
	// Neither has actors.
	{
		Movie ext;
		ext.cin           = "dataDir/cinemas/demo46.cin";
		ext.cinema.flags  = kCinLocalized | hideNavi;
		ext.cinema.scenes = { standInDsk("46", 0) };
		ext.sceneFrames   = { 60 };
		CinCut cut;
		cut.endFrame    = 60;
		cut.keys        = { notify(kMovieCmdFadeOut, 0, 30), notify(kMovieCmdGameEndCondition, kEndCausePikminZero, 50) };
		ext.cinema.cuts = { cut };
		list.push_back(ext);

		Movie end;
		end.cin           = "dataDir/cinemas/demo47.cin";
		end.cinema.flags  = hideNavi;
		end.cinema.scenes = { standInDsk("47", 0) };
		end.sceneFrames   = { 60 };
		CinCut endCut;
		endCut.endFrame = 60;
		endCut.keys     = { notify(kMovieCmdFadeIn, 0, 0), notify(kMovieCmdForceResults, 0, 50) };
		end.cinema.cuts = { endCut };
		list.push_back(end);
	}

	// End-of-day results backdrop (DEMOID_EndOfDayResults, demo36): started by
	// DayOverModeState::initialisePhaseTwo together with the results screen, which stays
	// up as long as the movie is active (DayOverModeState::update waits on
	// mMoviePlayer->mIsActive before phases three and four). Contract: the movie stays
	// active until exitDayEnd drops it, so it ends in a looping cut (CINMODE_LoopScene),
	// and it is NonGameMovie so the clock stops. Authored: a 30-frame cut once, then a
	// 15-frame cut in a loop. A missing demo36 is a zero-length movie: phases three and
	// four then run on the next ticks and the day ends through the title exit
	// (ONEPLAYER_GameExit) with the results screen unread.
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo36.cin";
		m.cinema.flags  = kCinNonGameMovie;
		m.cinema.scenes = { standInDsk("36", 0), standInDsk("36", 1) };
		m.sceneFrames   = { 30, 15 };
		CinCut once, loop;
		once.endFrame = 30;
		loop.scene    = 1;
		loop.endFrame = 15;
		loop.flags    = kCutLoop;
		m.cinema.cuts = { once, loop };
		list.push_back(m);
	}

	// Bad ending, Olimar fails to escape (DEMOID_BadEndingFailEscape, demo73, untranslated):
	// started by DayOverModeState::initialisePhaseTwo on Day 30 with fewer than 25 parts,
	// right after makeTotalScoreWindow builds the final results window. Contract: the window
	// does not start itself, the movie's Notify StartTotalResults key does
	// (GameMovieInterface::parse); then the movie must stay active until
	// DayOverModeState::update skips the scene when the window reports it is done, so it
	// ends in a loop; NonGameMovie. Authored: a 40-frame cut once with the key at 20, then
	// a 15-frame cut in a loop, no actors. A missing demo73 is a zero-length movie: the
	// window is built but never started, and phases three and four run on the next ticks.
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo73.cin";
		m.cinema.flags  = kCinNonGameMovie;
		m.cinema.scenes = { standInDsk("73", 0), standInDsk("73", 1) };
		m.sceneFrames   = { 40, 15 };
		CinCut once, loop;
		once.endFrame = 40;
		once.keys     = { notify(kMovieCmdStartTotalResults, 0, 20) };
		loop.scene    = 1;
		loop.endFrame = 15;
		loop.flags    = kCutLoop;
		m.cinema.cuts = { once, loop };
		list.push_back(m);
	}

	// The last ship upgrade (DEMOID_ShipUpgradeLast, demo26, untranslated): queued by
	// PlayerState::preloadHenkaMovie when a delivery brings the 30th part. Contract: its
	// Notify key MOVIECMD_SpecialDayEnd is the happy ending's trigger
	// (GameMovieInterface::parse: forceDayEnd, mIsDayEndTriggered, ENDING_Happy). Authored:
	// a 60-frame scene with FadeOut at 40 and SpecialDayEnd at 45. SpecialDayEnd is listed
	// first on purpose: a START skip replays the remaining keys in frame order
	// (pc_cine_sort_skip_keys), and ending_happy_skip checks that the replay still sends
	// SpecialDayEnd, which only exercises the sort when file order and frame order differ.
	// No ship Action (no ship on the stand-in stages; DemoEventMgr::act ignores it without one).
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo26.cin";
		m.cinema.flags  = hideNavi;
		m.cinema.scenes = { standInDsk("26", 0) };
		m.sceneFrames   = { 60 };
		CinCut cut;
		cut.endFrame  = 60;
		cut.keys      = { notify(kMovieCmdSpecialDayEnd, 0, 45), notify(kMovieCmdFadeOut, 0, 40) };
		m.cinema.cuts = { cut };
		list.push_back(m);
	}

	// Flying through space (DEMOID_EndingSpace, demo77, untranslated): the neutral ending's
	// phase three and the happy ending's phase four (DayOverModeState), each right after
	// makeTotalScoreWindow. Contract, as for demo73: the StartTotalResults key starts the
	// final results window and the movie then loops until the window is done; in the
	// neutral ending DayOverModeState::update leaves for GameExit when the window reports
	// done in phase three, and runs phase four (GameExit, window unread) as soon as no movie
	// is active. NonGameMovie. Authored: one 40-frame scene, played once with FadeIn at 0 and
	// StartTotalResults at 20, then its frames 30-40 in a loop; no actors.
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo77.cin";
		m.cinema.flags  = kCinNonGameMovie;
		m.cinema.scenes = { standInDsk("77", 0) };
		m.sceneFrames   = { 40 };
		CinCut once, loop;
		once.endFrame   = 40;
		once.keys       = { notify(kMovieCmdFadeIn, 0, 0), notify(kMovieCmdStartTotalResults, 0, 20) };
		loop.startFrame = 30;
		loop.endFrame   = 40;
		loop.flags      = kCutLoop;
		m.cinema.cuts   = { once, loop };
		list.push_back(m);
	}

	// The Main Engine reaching the ship on the Impact Site (DEMOFLAG_CollectEngine's
	// movie 20, DEMOID_CollectPartPractice, demo20). The engine is sucked into the
	// ship while it plays (PelletGoalState::exec, UfoItem::finishSuck), which takes
	// about 2 s. Contract: a TextDemo TUT_GetParts (17) key inside a cut that
	// outlasts the suck, then a loop. With gameflow.mShipTextType CollectEngine that
	// key opens no window: GameMovieInterface::parse skips the scene, which ends the
	// loop, and the queued ship-upgrade movie (demo17) follows. Without the movie the
	// day ends (demo17) before the ship has the engine. Authored: a 120-frame cut
	// (4 s at 30 Hz) with the key at 100, then frames 120-130 in a loop.
	// The Whimsical Radar reaching the ship (DEMOID_CollectRadar, demo78) has the same
	// contract and shape: MoviePlayer::startMovie swaps DEMOID_CollectPart for it
	// whenever the part is uf01, on every stage (ship_part_carry). With a part id and
	// the ship text type PartCollect the key opens the part's "get" text
	// (TUT_PartsGetOffset + the part index), whose close skips the scene.
	for (const char* id : { "20", "78" }) {
		Movie m;
		m.cin           = id[0] == '2' ? "dataDir/cinemas/demo20.cin" : "dataDir/cinemas/demo78.cin";
		m.cinema.flags  = hideNavi;
		m.cinema.scenes = { standInDsk(id, 0) };
		m.sceneFrames   = { 130 };
		CinCut once, loop;
		once.endFrame   = 120;
		once.keys       = { notify(kMovieCmdTextDemo, kTutGetParts, 100) };
		loop.startFrame = 120;
		loop.endFrame   = 130;
		loop.flags      = kCutLoop;
		m.cinema.cuts   = { once, loop };
		list.push_back(m);
	}

	// The ship's first upgrade on the Impact Site (DEMOID_ShipUpgradePractice, demo17,
	// movie17table's Impact Site entry), which PlayerState::preloadHenkaMovie queues
	// when the Main Engine reaches the ship (PelletGoalState::init). It is what ends
	// the tutorial's Day 1: the clock is held at 14.8 h there. Contract: the movie's
	// ForceDayEnd key (GameMovieInterface::parse) starts the day end, and its TextDemo
	// TUT_GetEngine key opens the upgrade text (tu_tx15.blo). The ship's Henka Action
	// (DemoEventUFO::Henka) is optional. Authored: one 60-frame cut played once with
	// Henka at 10, ForceDayEnd at 40 and the text at 50, no actors and no loop
	// (tutorial_day1 reads them in that order).
	{
		Movie m;
		m.cin           = "dataDir/cinemas/demo17.cin";
		m.cinema.flags  = hideNavi;
		m.cinema.scenes = { standInDsk("17", 0) };
		m.sceneFrames   = { 60 };
		CinCut cut;
		cut.endFrame  = 60;
		cut.keys      = { action(kDemoSenderUfo, kDemoUfoHenka, 10), notify(kMovieCmdForceDayEnd, 0, 40), notify(kMovieCmdTextDemo, kTutGetEngine, 50) };
		m.cinema.cuts = { cut };
		list.push_back(m);
	}

	// Title opening (titles.cpp CinematicPlayer): ShowStartMenu at frame 0.
	{
		Movie m;
		m.cin            = "dataDir/cinemas/opening.cin";
		m.cinema.flags   = 0;
		m.cinema.scenes  = { "cinemas/standin/opening_a.dsk" };
		m.sceneFrames    = { 30 };
		CinCut cut;
		cut.endFrame  = 30;
		cut.keys      = { notify(0, 0, 0) };
		m.cinema.cuts = { cut };
		list.push_back(m);
	}
	return list;
}

} // namespace

bool writeStandInCinemas(std::vector<StandInCinemaFile>& out, std::string& error)
{
	out.clear();
	for (const Movie& m : movies()) {
		StandInCinemaFile cin;
		cin.path = m.cin;
		if (!writeCin(m.cinema, cin.text, error)) return error = cin.path + ": " + error, false;
		out.push_back(cin);
		for (std::size_t i = 0; i < m.cinema.scenes.size(); i++) {
			StandInCinemaFile dsk;
			dsk.path = "dataDir/" + m.cinema.scenes[i];
			if (!writeDsk(standInScene(m.sceneFrames[i]), dsk.text, error)) return error = dsk.path + ": " + error, false;
			out.push_back(dsk);
		}
	}
	return true;
}

} // namespace nectar
