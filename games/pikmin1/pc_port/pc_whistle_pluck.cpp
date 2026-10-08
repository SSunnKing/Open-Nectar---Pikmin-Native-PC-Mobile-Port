#include "pc_whistle_pluck.h"
#include "settings/pc_settings.h"
#include "pc_coop.h"
#include "Navi.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiHeadItem.h"
#include "PikiState.h"
#include "ItemMgr.h"
#include "MoviePlayer.h"
#include "gameflow.h"
#include "PlayerState.h"
#include <cmath>

bool pc_whistle_pluck(Navi* navi, float radius)
{
    if (!pc_settings_get_whistle_pluck() || !navi || !pikiMgr || !itemMgr
        || !std::isfinite(radius) || radius <= 0.0f || navi->mHealth <= 0.0f
        || !playerState || playerState->inDayEnd()
        || gameflow.mPauseAll || gameflow.mIsUIOverlayActive
        || (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive)) return false;

    PikiHeadItem* nearest = nullptr;
    float nearestSquared = radius * radius;
    Iterator sprouts(itemMgr->getPikiHeadMgr());
    CI_LOOP(sprouts) {
        Creature* item = *sprouts;
        if (item->mObjType != OBJTYPE_Pikihead) continue;
        PikiHeadItem* sprout = static_cast<PikiHeadItem*>(item);
        if (!sprout->canPullout()) continue;
        // VS: a captain cannot pluck the rival's sprouts, as with manual plucking.
        if (pc_vs_active() && sprout->mPcOwner >= 0 && sprout->mPcOwner != navi->mNaviID) continue;
        const Vector3f delta = sprout->mSRT.t - navi->mCursorWorldPos;
        // Match manual plucking's vertical reach; don't pluck through floors.
        if (std::fabs(delta.y) >= 25.0f) continue;
        const float distanceSquared = delta.x * delta.x + delta.z * delta.z;
        if (distanceSquared < nearestSquared) {
            nearest = sprout;
            nearestSquared = distanceSquared;
        }
    }
    if (!nearest) return false;

    // A sprout already counts toward the field population. Use the same
    // temporary conversion allowance as manual plucking and restore it.
    const bool previousBirthMode = PikiMgr::meBirthMode;
    PikiMgr::meBirthMode = true;
    Piki* piki = static_cast<Piki*>(pikiMgr->birth());
    PikiMgr::meBirthMode = previousBirthMode;
    if (!piki) return false;
    piki->init(navi);
    piki->initColor(nearest->mSeedColor);
    if (pc_vs_active() && nearest->mPcOwner >= 0) piki->mPlayerId = nearest->mPcOwner;
    piki->setFlower(nearest->mFlowerStage);
    piki->resetPosition(nearest->mSRT.t);
    piki->mFSM->transit(piki, PIKISTATE_AutoNuki);
    nearest->finishWaterEffect();
    nearest->kill(false);
    return true;
}
