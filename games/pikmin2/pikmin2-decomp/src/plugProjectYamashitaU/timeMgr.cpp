#include "Game/TimeMgr.h"
#ifdef PIKI_PC_PORT
extern "C" int pc_settings_get_infinite_day(void);
extern "C" int pc_settings_get_day_minutes(void);
extern "C" int pc_settings_get_eternal_night(void);
#include "Game/GameSystem.h"

// Mods "Infinite Day" y "Day Length" (luz natural): el reloj de la partida se
// para o va a otro ritmo, pero la luz sigue este reloj visual, que avanza al
// ritmo original, pasa por la noche y vuelve a amanecer. La barra del sol
// sigue con el reloj de la partida. Fuera de esos mods es el mismo reloj.
static f32 sPcVisualTimeOfDay = 7.0f;
static bool sPcVisualActive   = false;

// El día 1 solo termina al atardecer (su pausa no deja ir al atardecer), así
// que "Infinite Day" no lo para: si no, el día 1 no acabaría nunca.
static bool pcInfiniteDay(u32 dayCount) { return pc_settings_get_infinite_day() && dayCount != 0; }

static bool pcVisualClockWanted(u32 dayCount)
{
	Game::GameSystem* gs = Game::gameSystem;
	return gs && gs->isStoryMode() && gs->mSection && !gs->mIsInCave
	    && (pcInfiniteDay(dayCount) || pc_settings_get_day_minutes() > 0 || pc_settings_get_eternal_night());
}
#endif
#include "JSystem/JKernel/JKRDvdRipper.h"
#include "stream.h"
#include "System.h"

namespace Game {

/**
 * @note Address: 0x80126C00
 * @note Size: 0x90
 */
TimeMgr::TimeMgr()
    : CNode("タイムマネージャ")
    , mSpeedFactor(1.0f)
{
	init();
}

/**
 * @note Address: 0x80126FF0
 * @note Size: 0xB0
 */
void TimeMgr::init()
{
	mNightLength        = TIMEMGR_DAY_HOURS - mParms.mParms.mEveningEndTime.mValue + mParms.mParms.mMorningStartTime.mValue;
	mEarlyMorningLength = mParms.mParms.mMidMorningTime.mValue - mParms.mParms.mMorningStartTime.mValue;
	mMidMorningLength   = mParms.mParms.mMorningEndTime.mValue - mParms.mParms.mMidMorningTime.mValue;
	mMiddayLength       = mParms.mParms.mEveningStartTime.mValue - mParms.mParms.mMorningEndTime.mValue;
	mEarlyEveningLength = mParms.mParms.mMidEveningStartTime.mValue - mParms.mParms.mEveningStartTime.mValue;
	mLateEveningLength  = mParms.mParms.mEveningEndTime.mValue - mParms.mParms.mMidEveningEndTime.mValue;
	mGameDayLength      = mParms.mParms.mDayEndTime.mValue - mParms.mParms.mDayStartTime.mValue;
	mGameNightLength    = TIMEMGR_DAY_HOURS - mGameDayLength;
	mDayCount           = 0;

	setTime(mParms.mParms.mDayStartTime.mValue);
}

/**
 * @note Address: 0x801270A0
 * @note Size: 0x3C
 */
void TimeMgr::setTime(f32 time)
{
	mCurrentTimeOfDay = time;
#ifdef PIKI_PC_PORT
	sPcVisualTimeOfDay = time;
#endif
	mCurrentRealTime  = (mCurrentTimeOfDay / TIMEMGR_DAY_HOURS) * mParms.mParms.mDayLengthSeconds.mValue;

	updateSlot();
}

/**
 * @note Address: 0x801270DC
 * @note Size: 0x40
 */
void TimeMgr::setStartTime()
{
	mCurrentTimeOfDay = mParms.mParms.mDayStartTime.mValue;
	mCurrentRealTime  = (mCurrentTimeOfDay / TIMEMGR_DAY_HOURS) * mParms.mParms.mDayLengthSeconds.mValue;

	updateSlot();
}

/**
 * @note Address: 0x8012711C
 * @note Size: 0x40
 */
void TimeMgr::setEndTime()
{
	mCurrentTimeOfDay = mParms.mParms.mDayEndTime.mValue;
	mCurrentRealTime  = (mCurrentTimeOfDay / TIMEMGR_DAY_HOURS) * mParms.mParms.mDayLengthSeconds.mValue;

	updateSlot();
}

/**
 * @note Address: 0x8012715C
 * @note Size: 0x168
 */
#pragma dont_inline on
void TimeMgr::updateSlot()
{
#ifdef PIKI_PC_PORT
	// La luz lee el reloj visual; se restaura al salir para que el resto del
	// juego siga viendo la hora de la partida.
	const f32 pcGameTime = mCurrentTimeOfDay;
	// "Eternal Night" también en las cinemáticas con gráficos del juego: ahí el
	// reloj se para o lo fija la propia escena (el aterrizaje pone las 7:00) y
	// la luz se recalcula sin pasar por update().
	if (pc_settings_get_eternal_night() && pcVisualClockWanted(mDayCount)) {
		sPcVisualActive    = true;
		sPcVisualTimeOfDay = 0.0f;
	}
	if (sPcVisualActive) {
		mCurrentTimeOfDay = sPcVisualTimeOfDay;
	}
	pcUpdateSlotFor();
	mCurrentTimeOfDay = pcGameTime;
}

void TimeMgr::pcUpdateSlotFor()
{
#endif
	// NIGHT
	if ((mCurrentTimeOfDay < mParms.mParms.mMorningStartTime.mValue) || mCurrentTimeOfDay >= mParms.mParms.mEveningEndTime.mValue) {
		mLightSetting = SUNTIME_Night;

		// linear increase from 0 to 1 as night goes from 7pm to 5:15am
		f32 time = mCurrentTimeOfDay;
		if (mCurrentTimeOfDay < mParms.mParms.mMorningStartTime.mValue) {
			time += TIMEMGR_DAY_HOURS;
		}

		mLightSettingRatio = (time - mParms.mParms.mEveningEndTime.mValue) / mNightLength;
		return;
	}

	// MORNING
	if (mCurrentTimeOfDay < mParms.mParms.mMorningEndTime.mValue) {
		mLightSetting = SUNTIME_Morning;

		// linear increase from 0 to 0.5 as morning goes from 5:15am to 7am (slower)
		if (mCurrentTimeOfDay < mParms.mParms.mMidMorningTime.mValue) {
			mLightSettingRatio = (0.5f * (mCurrentTimeOfDay - mParms.mParms.mMorningStartTime.mValue)) / mEarlyMorningLength;
			return;
		}

		// linear increase from 0.5 to 1 as morning goes from 7am to 8am (faster)
		mLightSettingRatio = 0.5f + ((0.5f * (mCurrentTimeOfDay - mParms.mParms.mMidMorningTime.mValue)) / mMidMorningLength);
		return;
	}

	// NOON
	if (mCurrentTimeOfDay < mParms.mParms.mEveningStartTime.mValue) {
		mLightSetting = SUNTIME_Noon;

		// linear increase from 0 to 1 as day goes from 8am to 3pm
		mLightSettingRatio = (mCurrentTimeOfDay - mParms.mParms.mMorningEndTime.mValue) / mMiddayLength;
		return;
	}

	// EVENING
	if (mCurrentTimeOfDay < mParms.mParms.mEveningEndTime.mValue) {
		mLightSetting = SUNTIME_Evening;

		// linear increase from 0 to 0.5 as evening goes from 3pm to 3:30pm (fast)
		if (mCurrentTimeOfDay < mParms.mParms.mMidEveningStartTime.mValue) {
			mLightSettingRatio = (0.5f * (mCurrentTimeOfDay - mParms.mParms.mEveningStartTime.mValue)) / mEarlyEveningLength;
			return;
		}

		// steady at 0.5 as evening goes from 3:30pm to 6:30pm
		if (mCurrentTimeOfDay < mParms.mParms.mMidEveningEndTime.mValue) {
			mLightSettingRatio = 0.5f;
			return;
		}

		// linear increase from 0.5 to 1 as evening goes from 6:30pm to 7pm (fast), i.e. during countdown
		mLightSettingRatio = 0.5f + ((0.5f * (mCurrentTimeOfDay - mParms.mParms.mMidEveningEndTime.mValue)) / mLateEveningLength);
	}
}
#pragma dont_inline reset

/**
 * @note Address: 0x801272C4
 * @note Size: 0x60
 */
f32 TimeMgr::getSunGaugeRatio()
{
	// if we're during gameplay time, make sun gauge appropriate ratio between 0 and 1 (0 at 7am landing, 1 at 7pm liftoff)
	if (mCurrentTimeOfDay >= mParms.mParms.mDayStartTime.mValue && mCurrentTimeOfDay < mParms.mParms.mDayEndTime.mValue) {
		return (mCurrentTimeOfDay - mParms.mParms.mDayStartTime.mValue) / mGameDayLength;
	}

	// we're (somehow) during unplayable time, make sun gauge appropriate ratio between 0 and 1 (0 at 7pm liftoff, 1 at 7am landing)
	f32 time = mCurrentTimeOfDay;
	if (mCurrentTimeOfDay < mParms.mParms.mDayStartTime.mValue) {
		time += TIMEMGR_DAY_HOURS;
	}
	return 1.0f - ((time - mParms.mParms.mDayEndTime.mValue) / mGameNightLength);
}

/**
 * @note Address: 0x80127324
 * @note Size: 0x74
 */
void TimeMgr::update()
{
	if (!isFlag(TIMEFLAG_Stopped)) {
#ifdef PIKI_PC_PORT
		// Mods "Infinite Day" y "Day Length": el reloj se para o avanza a otro
		// ritmo, pero solo en la superficie de la partida (fuera, el titulo y
		// los menus siguen con su tiempo propio).
		sPcVisualActive = pcVisualClockWanted(mDayCount);
		if (sPcVisualActive && pc_settings_get_eternal_night()) {
			// "Eternal Night": la luz se queda en medianoche; el reloj de la
			// partida (y la barra del sol) sigue a su ritmo.
			sPcVisualTimeOfDay = 0.0f;
		} else if (sPcVisualActive) {
			sPcVisualTimeOfDay += mSpeedFactor * sys->mDeltaTime * (TIMEMGR_DAY_HOURS / mParms.mParms.mDayLengthSeconds.mValue);
			if (sPcVisualTimeOfDay >= TIMEMGR_DAY_HOURS) {
				sPcVisualTimeOfDay -= TIMEMGR_DAY_HOURS;
			}
		} else {
			sPcVisualTimeOfDay = mCurrentTimeOfDay;
		}
		if (gameSystem && gameSystem->isStoryMode() && gameSystem->mSection && !gameSystem->mIsInCave) {
			if (pcInfiniteDay(mDayCount)) {
				updateSlot();
				return;
			}
			const int dayMinutes = pc_settings_get_day_minutes();
			if (dayMinutes > 0) {
				// La parte jugable del dia (mDayStartTime..mDayEndTime) dura
				// mGameDayLength/24 del ciclo: unos 13 min reales en el original.
				const f32 origSeconds = (mGameDayLength / TIMEMGR_DAY_HOURS) * mParms.mParms.mDayLengthSeconds.mValue;
				mCurrentRealTime += mSpeedFactor * sys->mDeltaTime * (origSeconds / (dayMinutes * 60.0f));
			} else {
				mCurrentRealTime += mSpeedFactor * sys->mDeltaTime;
			}
		} else {
			mCurrentRealTime += mSpeedFactor * sys->mDeltaTime;
		}
#else
		mCurrentRealTime += mSpeedFactor * sys->mDeltaTime;
#endif

		if (mCurrentRealTime > mParms.mParms.mDayLengthSeconds.mValue) {
			mCurrentRealTime -= mParms.mParms.mDayLengthSeconds.mValue;
		}

		mCurrentTimeOfDay = TIMEMGR_DAY_HOURS * (mCurrentRealTime / mParms.mParms.mDayLengthSeconds.mValue);

		updateSlot();
	}
}

/**
 * @note Address: 0x80127398
 * @note Size: 0x18
 */
#ifdef PIKI_PC_PORT
/// Como getSunGaugeRatio, pero con la hora de la luz: mueve la dirección del sol.
f32 TimeMgr::pcGetLightSunRatio()
{
	if (!sPcVisualActive) {
		return getSunGaugeRatio();
	}
	const f32 gameTime = mCurrentTimeOfDay;
	mCurrentTimeOfDay  = sPcVisualTimeOfDay;
	const f32 ratio    = getSunGaugeRatio();
	mCurrentTimeOfDay  = gameTime;
	return ratio;
}

bool TimeMgr::pcVisualActive() { return sPcVisualActive; }

/// Tecla F6 (Debug Keys): una hora más. Con "Infinite Day" solo avanza la luz,
/// para no llegar al atardecer y terminar el día.
void TimeMgr::pcDebugAdvanceHour()
{
	if (!pcInfiniteDay(mDayCount)) {
		const f32 next = mCurrentTimeOfDay + 1.0f;
		const f32 cap  = mParms.mParms.mDayEndTime.mValue - 0.01f;
		const f32 keep = sPcVisualTimeOfDay;
		setTime(next > cap ? cap : next);
		sPcVisualTimeOfDay = sPcVisualActive ? keep : mCurrentTimeOfDay;
	}
	if (sPcVisualActive) {
		sPcVisualTimeOfDay += 1.0f;
		if (sPcVisualTimeOfDay >= TIMEMGR_DAY_HOURS) {
			sPcVisualTimeOfDay -= TIMEMGR_DAY_HOURS;
		}
	}
	updateSlot();
}
#endif

bool TimeMgr::isDayOver()
{
	return mCurrentTimeOfDay > mParms.mParms.mDayEndTime.mValue;
}

/**
 * @note Address: 0x801273B0
 * @note Size: 0x30
 */
bool TimeMgr::isDayTime()
{
	return mCurrentTimeOfDay > mParms.mParms.mDayStartTime.mValue && mCurrentTimeOfDay <= mParms.mParms.mDayEndTime.mValue;
}

/**
 * @note Address: 0x801273E0
 * @note Size: 0x20
 */
f32 TimeMgr::getRealDayTime()
{
	return mParms.mParms.mDayLengthSeconds.mValue
	     * ((mParms.mParms.mDayEndTime.mValue - mParms.mParms.mDayStartTime.mValue) / TIMEMGR_DAY_HOURS);
}

/**
 * @note Address: 0x80127400
 * @note Size: 0x150
 */
void TimeMgr::loadSettingFile(char* filename)
{
	loadFromFile(&mParms.mParms, filename, JKRHeap::sSystemHeap);
	init();
}

} // namespace Game
