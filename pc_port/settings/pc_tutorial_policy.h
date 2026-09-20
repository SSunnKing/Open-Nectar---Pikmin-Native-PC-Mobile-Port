#ifndef PC_TUTORIAL_POLICY_H
#define PC_TUTORIAL_POLICY_H

#include "zen/ogTutorial.h"

// Only standalone informational hints. Story, recovery, part and ending text
// can drive progression and must retain their original dismissal lifecycle.
inline bool pc_should_skip_tutorial(bool disabled, int textId)
{
    if (!disabled) return false;
    switch (textId) {
    case zen::ogScrTutorialMgr::TUT_BombInfo:
    case zen::ogScrTutorialMgr::TUT_Limit100:
    case zen::ogScrTutorialMgr::TUT_Mitu:
    case zen::ogScrTutorialMgr::TUT_Rute:
    case zen::ogScrTutorialMgr::TUT_NukiAndFree:
    case zen::ogScrTutorialMgr::TUT_InfoDisplay:
        return true;
    default:
        return false;
    }
}

#endif
