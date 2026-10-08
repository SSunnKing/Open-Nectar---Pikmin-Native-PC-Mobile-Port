#ifndef _BUILDSETTINGS_H
#define _BUILDSETTINGS_H

#include "Version.h"

// TODO: These should probably go into a precompiled header or build flags or
// something.
#define LOCALIZED   true
#define MATCHING    true
#define BUGFIX      false
#define FOR_MODDING false

// The following are constants that mods might be interested in tweaking.
#define CHALLENGE_COURSE_COUNT 30

// Game Heap sizes, these often need to be adjusted with modding (TODO: some of these are used in files that are not yet linked, and thus
// these defines will not work)

#ifdef PIKI_PC_PORT
// LP64 objects and host containers are larger than on the console; the heaps
// below are carved from a 480 MB arena, so give the fixed ones headroom.
#define SYSTEM_HEAP_SIZE               (0x428000 * 4)
#else
#define SYSTEM_HEAP_SIZE               (0x428000)
#endif // Contains pikmin/navi/onion models and anims, game text, fonts, and other global things
#ifdef PIKI_PC_PORT
#define RESOURCE_MGR2D_HEAP_SIZE ((0xD4800) * 4)
#else
#define RESOURCE_MGR2D_HEAP_SIZE       (0xD4800)
#endif  // A subset of the system heap, contains the active 2d screen
#ifdef PIKI_PC_PORT
#define PSM_FACTORY_HEAP_SIZE ((0x900000) * 4)
#else
#define PSM_FACTORY_HEAP_SIZE          (0x900000)
#endif // Used for some global sound effect data
#ifdef PIKI_PC_PORT
#define SYSFACTORY_HEAP_SIZE ((0x151800) * 4)
#else
#define SYSFACTORY_HEAP_SIZE           (0x151800)
#endif // Also seems to be for sound effect data?
#ifdef PIKI_PC_PORT
#define PARTICLE_MGR_HEAP_SIZE ((0x180000) * 4)
#else
#define PARTICLE_MGR_HEAP_SIZE         (0x180000)
#endif // Global heap for particle effects that spawn in the game world
#ifdef PIKI_PC_PORT
#define PARTICLE_MGR2D_HEAP_SIZE ((0x3E800) * 4)
#else
#define PARTICLE_MGR2D_HEAP_SIZE       (0x3E800)
#endif  // Global heap for particle effects used in 2d menus
#ifdef PIKI_PC_PORT
#define TITLESCREEN_PARTICLE_HEAP_SIZE ((0x100000) * 4)
#else
#define TITLESCREEN_PARTICLE_HEAP_SIZE (0x100000)
#endif // 2d Particle heap for the title screen and bootup screen
#define GENERATOR_CACHE_HEAP_SIZE      (0xA000)   // Max size of generator cache as read from save data
#ifdef PIKI_PC_PORT
#define MOVIEPLAYER_HEAP_SIZE ((0x60400) * 4)
#else
#define MOVIEPLAYER_HEAP_SIZE          (0x60400)
#endif  // Max size of the MoviePlayer (cutscene) manager
#ifdef PIKI_PC_PORT
#define THP_PLAYER_HEAP_SIZE ((0x300000) * 4)
#else
#define THP_PLAYER_HEAP_SIZE           (0x300000)
#endif // Max size of THP (pre-rendered movie) player manager
#ifdef PIKI_PC_PORT
#define ENEMY_HEAP_SIZE_STORY ((0x200800) * 4)
#else
#define ENEMY_HEAP_SIZE_STORY          (0x200800)
#endif // Enemy heap for story mode
#ifdef PIKI_PC_PORT
#define ENEMY_HEAP_SIZE_CM ((0x177000) * 4)
#else
#define ENEMY_HEAP_SIZE_CM             (0x177000)
#endif // Enemy heap for challenge mode
#define ENEMY_HEAP_SIZE_VS             (0x1C2000) // Enemy heap for 2 player battle
#define ENEMY_HEAP_SIZE_ZUKAN          (0xFA000)  // Enemy heap for piklopedia (file not linked)

#define SCREEN_WIDTH  (608.0f)
#define SCREEN_HEIGHT (480.0f)

#define SCREEN_SCISSOR_WIDTH  (608)
#define SCREEN_SCISSOR_HEIGHT (448)

#endif
