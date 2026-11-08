#pragma once

// The authored Onion boot motion of the stand-in tree (goal infos 3 to 5), shared by the
// writer (standin_object_profiles.cpp) and the boot scenarios. The values are chosen here:
// the route way point opens (effect value 2, GoalItem::playEffect) before the seed is born
// on KEY_Action0, and the motion is the same for every colour.

namespace nectar {

constexpr int kOnionBootFrames     = 300; ///< payload length of the boot motion
constexpr int kOnionBootSeedFrame  = 150; ///< KEY_Action0 (GoalAI BootEmit: the first seed)
constexpr int kOnionBootRouteFrame = 100; ///< route-opening effect

} // namespace nectar
