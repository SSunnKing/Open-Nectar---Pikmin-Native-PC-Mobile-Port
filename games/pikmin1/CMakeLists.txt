###############################################################################
#  Pikmin PC Port — CMake Build System
#  Stage 1: Compilation Skeleton
#
#  This CMakeLists.txt compiles the decompiled Pikmin source code natively
#  for Linux x86_64, replacing all GameCube Dolphin SDK calls with our stubs.
#
#  Usage:
#    mkdir build && cd build
#    cmake ..
#    make -j$(nproc)
###############################################################################
cmake_minimum_required(VERSION 3.16)
project(PikminPC VERSION 0.1.0 LANGUAGES C CXX)
if (WIN32)
    enable_language(RC) # icono .rc de los ejecutables (windres de MinGW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# ─── Build type ───
# An unset build type means no -O flag at all, which is -O0: the game then runs
# several times slower with nothing on screen to say why. This is a game, so a
# build that is merely usable is the wrong default; RelWithDebInfo keeps the
# symbols a crash report needs while still optimising.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE was empty; defaulting to RelWithDebInfo.")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING
        "Building Debug: no optimisation. Expect a fraction of the normal frame "
        "rate. For play or for any performance measurement, configure with "
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo.")
endif()

# ─── Compiler Flags ───
# Force-include our PC types header BEFORE anything else.
# This overrides MetroWerks and MSVC macros, fixes type sizes, etc.
set(PC_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pc_port")

# Legacy code (decompiled GameCube sources) needs permissive compilation.
# Modern GCC would reject many Metrowerks-era constructs.
#
# -fsigned-char: plain char is signed on x86-64 but UNSIGNED on ARM64 (and on
# the original PowerPC). The desktop port has only ever run with signed char,
# and the decompilation has latent bugs in both directions (Font::charToIndex,
# see AI_HANDOFF.md). Pinning the sign here makes every platform and compiler
# behave like the tested desktop build, instead of inheriting the ABI.
# Clang (el compilador del NDK) convierte en error dos cosas que GCC deja
# pasar con -fpermissive: el especificador `register` y tomar la dirección de
# un temporal (stack_new). Ambas se relajan sólo en Clang.
set(CLANG_LEGACY_RELAXATIONS
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-register>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-address-of-temporary>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unknown-warning-option>
)
set(LEGACY_COMPILE_OPTIONS
    -include "${PC_PORT_DIR}/pc_types.h"
    -fsigned-char
    -fpermissive
    ${CLANG_LEGACY_RELAXATIONS}
    -Wno-narrowing
    -Wno-multichar
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-parentheses
    -Wno-delete-non-virtual-dtor
    -Wno-reorder
    -Wno-switch
    -Wno-format
    -Wno-attributes
    -Wno-comment
)

# Endurecimiento del código decompilado. El compilador de GameCube no
# aprovechaba el comportamiento indefinido que GCC/Clang -O2 sí aprovechan:
# variables sin inicializar (crash de Armor::StateFail, leía la dirección
# 0x2fc), comprobaciones de this == nullptr, punteros reinterpretados entre
# tipos y overflow con signo. Con estas opciones ese código se compila como
# lo que el juego espera en vez de eliminarse o volverse basura.
include(CheckCXXCompilerFlag)
foreach(flag -fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv -ftrivial-auto-var-init=zero)
    string(MAKE_C_IDENTIFIER "LEGACY_HAS${flag}" flagVar)
    check_cxx_compiler_flag(${flag} ${flagVar})
    if(${flagVar})
        list(APPEND LEGACY_COMPILE_OPTIONS ${flag})
    endif()
endforeach()

# Native code (pc_port, tests, launcher) should compile with strict warnings.
# We keep -fpermissive because native code includes legacy Dolphin headers.
set(NATIVE_COMPILE_OPTIONS
    -include "${PC_PORT_DIR}/pc_types.h"
    -fsigned-char
    -fpermissive
    ${CLANG_LEGACY_RELAXATIONS}
    -Wall
    -Wextra
    $<$<CXX_COMPILER_ID:GNU>:-Wno-changes-meaning>
    -Wno-narrowing
    -Wno-multichar
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-parentheses
    -Wno-delete-non-virtual-dtor
    -Wno-reorder
    -Wno-attributes
    -Wno-comment
    -Werror=return-type
    -Werror=switch
)

# Off by default: the native mixer is the configuration known to work. Turn on
# with -DPIKMIN_NATIVE_JAUDIO=ON to drive audio from the original engine.
option(PIKMIN_NATIVE_JAUDIO "Drive audio with the original src/jaudio engine" OFF)

# Developer shortcuts are compiled in, but remain disabled in the user's
# configuration until Cheats > Debug Keys is switched on. Keeping the runtime
# default off prevents an accidental F5/F7 from changing a normal save.
option(PIKMIN_DEBUG_KEYS "Build the F5-F8 testing shortcuts" ON)

# ─── Render backend ───
# GLES 3.0 for Android and as an optional desktop path. When ON, shaders emit
# #version 300 es and the GL context requests ES via SDL. When OFF (default for
# desktop), the classic OpenGL 3.3 compatibility path is used.
if(ANDROID)
    set(PIKMIN_GLES_DEFAULT ON)
else()
    set(PIKMIN_GLES_DEFAULT OFF)
endif()
option(PIKMIN_GLES "Build with OpenGL ES 3.0 renderer" ${PIKMIN_GLES_DEFAULT})

# Controles táctiles (pc_port/touch): de serie en Android; en escritorio se
# pueden activar para probarlos con ratón (SDL_HINT_MOUSE_TOUCH_EVENTS=1).
option(PIKMIN_TOUCH "Build the touch control layer" ${PIKMIN_GLES_DEFAULT})

# ─── Qué partes del árbol se construyen ───
# El launcher es un programa de escritorio (diálogos nativos, dolphin-tool): en
# Android no se compila; se rehace como interfaz Android en la fase 7 del plan
# de docs/ANDROID_PLAN.md. Los tests se ejecutan en la máquina que compila,
# así que tampoco tienen sentido al cruzar hacia un dispositivo.
if(ANDROID)
    set(PIKMIN_DESKTOP_DEFAULT OFF)
else()
    set(PIKMIN_DESKTOP_DEFAULT ON)
endif()
option(PIKMIN_BUILD_LAUNCHER "Build the standalone first-run launcher" ${PIKMIN_DESKTOP_DEFAULT})
option(PIKMIN_BUILD_TESTS "Build the host-run test executables" ${PIKMIN_DESKTOP_DEFAULT})
option(NECTAR_BUILD_MAPMAKER "Build the VS map editor (tools/mapmaker)" ${PIKMIN_DESKTOP_DEFAULT})
option(NECTAR_BUILD_MODELMAKER "Build the model maker (tools/modelmaker)" ${PIKMIN_DESKTOP_DEFAULT})
option(NECTAR_ONLINE "Build the experimental direct online co-op layer" OFF)

if(PIKMIN_BUILD_TESTS)
    enable_testing()
endif()

# ─── Which release of the game this binary is built for ───
#
# The port compiles the game's own code, and that code is conditional on the
# release in 404 places across 83 files, so this is a build-time choice and not
# a setting. The disc is only ever used for assets.
set(PIKMIN_GAME_VERSION "VERSION_GPIE01_01" CACHE STRING
    "Game release to build: VERSION_GPIE01_01 (USA Rev 1), VERSION_GPIE01_00, \
VERSION_GPIP01_00 (PAL), VERSION_GPIJ01_01, VERSION_GPIJ01_02")

# __FILE__ (asserts, informes de crash) sin la ruta absoluta de la máquina
# que compila.
add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_compile_definitions(
    PIKI_PC_PORT=1
    ${PIKMIN_GAME_VERSION}
    DTK_CONFIG_NONMATCHING=1
    PIKI_USE_GLES=$<BOOL:${PIKMIN_GLES}>
    PIKI_PC_TOUCH=$<BOOL:${PIKMIN_TOUCH}>
    # The Linux renderer translates the game's native GX command stream to
    # OpenGL.  Selecting the old Win32/OpenGL code path here mixes two
    # incompatible texture/rendering backends and leaves GXTexObj uninitialised.
    PIKI_USE_DGX=1
    # Selects which engine drives audio. 0 keeps the native PC mixer in
    # pc_port/audio; 1 compiles the original engine in src/jaudio and renders
    # its DSPchannel_ voice parameter blocks through pc_dsp_host. The two
    # cannot coexist -- see the guards in dolphin_stubs/audio_stubs.cpp.
    PIKI_USE_JAUDIO=$<BOOL:${PIKMIN_NATIVE_JAUDIO}>
    PIKI_DEBUG_KEYS=$<BOOL:${PIKMIN_DEBUG_KEYS}>
    # Some SDL2 builds (including current Arch configurations) remap main to
    # SDL_main unless the application explicitly owns its entry point. En
    # Android es al revés: SDLActivity busca el símbolo SDL_main en la
    # biblioteca, así que ahí se deja que SDL_main.h renombre main.
    $<$<NOT:$<BOOL:${ANDROID}>>:SDL_MAIN_HANDLED=1>
    # In-game settings menu (F1). Remove this to disable the overlay entirely.
    PIKI_PC_SETTINGS_MENU=1
)

# Librerías compartidas con el editor de mapas (raíz del repo).
set(NECTAR_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")

if(NOT TARGET nectar_paths)
    add_subdirectory("${NECTAR_LIBS_DIR}/nectar_paths" "${CMAKE_BINARY_DIR}/nectar_paths")
endif()

# ─── Include paths ───
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Dolphin
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Dolphin/OS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Dolphin/GX
    ${PC_PORT_DIR}
    ${PC_PORT_DIR}/gl
)

###############################################################################
#  Source file lists
#  We compile only the game logic modules, NOT the original Dolphin SDK
#  implementations (those are replaced by our stubs).
###############################################################################

# ─── External Dependencies (SDL2 & OpenGL) ───
# En Android SDL2 se compila en el mismo árbol: el proyecto Gradle
# (android/app/jni/CMakeLists.txt) añade las fuentes de SDL2 antes que este
# fichero y deja el target `SDL2` definido, con sus cabeceras como interfaz.
if (ANDROID)
    if (NOT TARGET SDL2)
        message(FATAL_ERROR "Android: SDL2 must be added as a target before this project (see android/app/jni/CMakeLists.txt)")
    endif()
    set(SDL2_LIBRARIES SDL2)
    # El port incluye <SDL2/SDL.h> (la disposición de los paquetes de
    # escritorio); el árbol de fuentes de SDL2 tiene SDL.h suelto. Un enlace
    # simbólico en el directorio de build reproduce esa disposición, y sirve
    # también a pikmin_legacy, que ve las cabeceras sin enlazar SDL2.
    get_target_property(_sdl2_src SDL2 SOURCE_DIR)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdl2-include")
    file(CREATE_LINK "${_sdl2_src}/include" "${CMAKE_CURRENT_BINARY_DIR}/sdl2-include/SDL2" SYMBOLIC)
    set(SDL2_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/sdl2-include" "${_sdl2_src}/include")
else()
    find_package(SDL2 QUIET)
    if (NOT SDL2_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
endif()

# Paquetes de release: SDL2 y el runtime de C/C++ dentro de cada ejecutable,
# para que la carpeta instalada sea solo el juego y el launcher. En Windows
# evita SDL2.dll y libwinpthread-1.dll; en Linux se compila así dentro del
# contenedor de packaging/linux (glibc antigua), junto a -static-libstdc++.
if (WIN32)
    set(_pikmin_static_default ON)
else()
    set(_pikmin_static_default OFF)
endif()
option(PIKMIN_STATIC_RUNTIME "Link SDL2 and the C/C++ runtime into the executables" ${_pikmin_static_default})
if (PIKMIN_STATIC_RUNTIME AND NOT ANDROID)
    if (TARGET SDL2::SDL2-static)
        set(SDL2_LIBRARIES SDL2::SDL2-static)
    endif()
    if (WIN32)
        add_link_options(-static)
    elseif (NOT APPLE) # macOS: libc++ viene con el sistema; basta SDL2 estática
        # --exclude-libs: el libstdc++/SDL2 estáticos no se exportan. Si no,
        # pisan al libstdc++ del sistema en los drivers que lo usan (NVIDIA
        # egl-wayland2) y el contexto cae a llvmpipe.
        add_link_options(-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
    endif()
endif()

# SDL2main supplies a WinMain that forwards to SDL_main. This project declares
# SDL_MAIN_HANDLED and defines its own main, so linking it leaves SDL_main
# undefined in every executable. Drop it wherever the SDL2 package offers it.
if (SDL2_LIBRARIES)
    list(FILTER SDL2_LIBRARIES EXCLUDE REGEX "SDL2main")
endif()

# OpenGL de escritorio no existe en Android: allí el contexto es GLES/EGL y lo
# aporta el NDK (fase 3 del plan de Android).
if (NOT WIN32 AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

# Versión y commit en el informe de crash (pikmin_crash.log), como en Pikmin 2.
set(NECTAR_RELEASE_VERSION "unknown")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../versions.json")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../versions.json" NECTAR_VERSIONS_JSON)
    string(REGEX MATCH "\"release\"[ \t]*:[ \t]*\"([^\"]*)\"" _ "${NECTAR_VERSIONS_JSON}")
    if(CMAKE_MATCH_1)
        set(NECTAR_RELEASE_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()
set(NECTAR_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE _nectar_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_nectar_commit)
        set(NECTAR_GIT_COMMIT "${_nectar_commit}")
    endif()
endif()
set_source_files_properties(pc_port/pc_crash.cpp PROPERTIES COMPILE_DEFINITIONS
    "NECTAR_RELEASE_VERSION=\"${NECTAR_RELEASE_VERSION}\";NECTAR_GIT_COMMIT=\"${NECTAR_GIT_COMMIT}\"")

set(PC_PORT_SOURCES
    pc_port/pc_main.cpp
    pc_port/pc_crash.cpp
    pc_port/pc_window.cpp
    pc_port/pc_keycaps.cpp
    pc_port/pc_gyro.cpp
    pc_port/pc_pad_axis.cpp
    pc_port/pc_menu_pad.cpp
    pc_port/pc_menu_mode.cpp
    pc_port/pc_menu_repeat.cpp
    pc_port/pc_menu_click.cpp
    pc_port/pc_button_prompt.cpp
    pc_port/pc_hand_cursor.cpp
    pc_port/pc_photo_mode.cpp
    pc_port/pc_record.cpp
    pc_port/pc_permadeath.cpp
    pc_port/randomizer/pc_randomizer.cpp
    pc_port/randomizer/pc_randomizer_catalog.cpp
    pc_port/randomizer/pc_randomizer_audit.cpp
    pc_port/randomizer/pc_randomizer_logic.cpp
    pc_port/randomizer/pc_randomizer_apply.cpp
    pc_port/pc_coop.cpp
    pc_port/pc_captain_preview.cpp
    pc_port/pc_whistle_pluck.cpp
    pc_port/pc_discord.cpp
    pc_port/pc_speedrun.cpp
    pc_port/pc_livesplit.cpp
    pc_port/pc_day_history.cpp
    pc_port/pc_art.cpp
    pc_port/pc_icon.cpp
    pc_port/settings/pc_glass_menu.cpp
    pc_port/pc_gpu_preference.cpp
    pc_port/mods/pc_hd_models.cpp
    pc_port/mods/pc_hd_model_convert.cpp
    pc_port/mods/pc_vs_arena.cpp
    pc_port/mods/pc_vs_map.cpp
    pc_port/mods/pc_map_catalog.cpp
    pc_port/mods/pc_mission.cpp
    pc_port/mods/pc_campaign.cpp
    pc_port/mods/pc_characters.cpp
    pc_port/mods/pc_pikmin_models.cpp
    # Mapas VS: formato y compilador compartidos con el editor de mapas.
    ${NECTAR_LIBS_DIR}/nectar_mapcore/src/map_json.cpp
    ${NECTAR_LIBS_DIR}/nectar_mapcore/src/i18n.cpp
    ${NECTAR_LIBS_DIR}/nectar_mapcore/src/compile.cpp
    ${NECTAR_LIBS_DIR}/nectar_mapcore/src/campaign.cpp
    ${NECTAR_LIBS_DIR}/nectar_mapcore/src/pack.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/mod.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/gen.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/image.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/mesh_mod.cpp
    # Personajes propios (Load/Characters), compartido con el launcher y el creador.
    ${NECTAR_LIBS_DIR}/nectar_assets/src/charpack.cpp
    ${PC_PORT_DIR}/launcher/bti_decode.cpp
    pc_port/pc_vs.cpp
    pc_port/pc_achievements.cpp
    pc_port/pc_file_dialog.cpp
    pc_port/gl/pc_gfx.cpp
    pc_port/gl/pc_tev_shader.cpp
    pc_port/gl/pc_postprocess.cpp
    pc_port/gl/pc_texpack.cpp
    pc_port/timing/pc_frame_scheduler.cpp
    pc_port/timing/pc_render_phase.cpp
    pc_port/timing/pc_visual_snapshot.cpp
    pc_port/timing/pc_camera_snapshot.cpp
    pc_port/timing/pc_visual_runtime.cpp
    pc_port/timing/pc_render_packet.cpp
    pc_port/timing/pc_tick_profiler.cpp
    # Netplay determinism seams. Inert unless a netplay switch is set (see
    # pc_netplay_det.h, pc_input_log.h and pc_state_hash.h).
    pc_port/netplay/pc_netplay_det.cpp
    pc_port/netplay/pc_netplay_policy.cpp
    pc_port/netplay/pc_sim_rng.cpp
    pc_port/netplay/pc_input_log.cpp
    pc_port/netplay/pc_state_hash.cpp
    # Two-pass sim/presentation frame and the session lead camera.
    pc_port/netplay/pc_netplay_present.cpp
    pc_port/netplay/pc_netplay_camlead.cpp
    pc_port/netplay/pc_netplay_navilead.cpp
    # Co-op switch and direct boot (--coop-direct, netplay).
    pc_port/netplay/pc_coop_switch.cpp
    pc_port/audio/pc_audio.cpp
    pc_port/audio/pc_envelope.cpp
    pc_port/audio/pc_jam.cpp
    pc_port/audio/pc_instrument_bank.cpp
    pc_port/audio/pc_wave_bank.cpp
    pc_port/audio/pc_sequence_archive.cpp
    pc_port/audio/pc_aram.cpp
    pc_port/audio/pc_dsp_host.cpp
    pc_port/settings/pc_settings.cpp
    pc_port/settings/pc_settings_p2d.cpp
    pc_port/settings/pc_prompt_pointer.cpp
    src/jaudio/pikiseq.c
    pc_port/dolphin_stubs/os_stubs.cpp
    pc_port/dolphin_stubs/pc_host_readable.cpp
    pc_port/dolphin_stubs/pc_host_readable_os.cpp
    pc_port/dolphin_stubs/gx_stubs.cpp
    pc_port/dolphin_stubs/dvd_stubs.cpp
    pc_port/dolphin_stubs/pad_stubs.cpp
    pc_port/dolphin_stubs/vi_stubs.cpp
    pc_port/dolphin_stubs/card_stubs.cpp
    pc_port/dolphin_stubs/pc_card_save_root.cpp
    pc_port/dolphin_stubs/audio_stubs.cpp
    pc_port/dolphin_stubs/jaudio_hw_stubs.cpp
    pc_port/dolphin_stubs/misc_stubs.cpp
)
if (ANDROID)
    list(APPEND PC_PORT_SOURCES
        pc_port/android/pc_android.cpp
        pc_port/android/pc_texpack_android.cpp
        pc_port/android/pc_save_android.cpp
        pc_port/android/pc_net_gateway_android.cpp)
endif()
if (PIKMIN_TOUCH)
    list(APPEND PC_PORT_SOURCES pc_port/touch/pc_touch.cpp)
endif()

include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcdec
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${NECTAR_LIBS_DIR}/nectar_mapcore/include
    ${NECTAR_LIBS_DIR}/nectar_assets/include
)
# Cabeceras internas de nectar_assets solo para sus fuentes.
set_source_files_properties(
    ${NECTAR_LIBS_DIR}/nectar_assets/src/mod.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/gen.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/image.cpp
    ${NECTAR_LIBS_DIR}/nectar_assets/src/mesh_mod.cpp
    PROPERTIES INCLUDE_DIRECTORIES
    "${NECTAR_LIBS_DIR}/nectar_assets/src;${NECTAR_LIBS_DIR}/nectar_assets/third_party;${PC_PORT_DIR}/launcher")

# Mapa VS de serie incrustado en el ejecutable (así también vale en Android).
set(PC_VS_DEFAULT_MAP_JSON "${CMAKE_CURRENT_SOURCE_DIR}/maps/vs/impact_arena/map.json")
file(READ "${PC_VS_DEFAULT_MAP_JSON}" PC_VS_DEFAULT_MAP)
configure_file(pc_port/mods/pc_vs_default_map.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/pc_vs_default_map.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PC_VS_DEFAULT_MAP_JSON}")

# ─── System Common (platform-independent game engine) ───
file(GLOB SYSCOMMON_SOURCES "src/sysCommon/*.cpp")

# ─── System Dolphin (GameCube-specific system layer — we compile it but stubs handle the calls) ───
set(SYSDOLPHIN_SOURCES
    src/sysDolphin/controllerMgr.cpp
    src/sysDolphin/dgxGraphics.cpp
    src/sysDolphin/gameApp.cpp
    src/sysDolphin/sysNew.cpp
    src/sysDolphin/system.cpp
    src/sysDolphin/texture.cpp
)

# JAudio uses NextOS's host boundary, never the GameCube DSP boot/mailboxes.
set(JAUDIO_TARGET_SOURCES "")
if(PIKMIN_NATIVE_JAUDIO)
    set(JAUDIO_TARGET_SOURCES
        src/jaudio/aramcall.c
        src/jaudio/audiomesg.c
        src/jaudio/bankdrv.c
        src/jaudio/bankloader.c
        src/jaudio/bankread.c
        src/jaudio/centcalc.c
        src/jaudio/cmdqueue.c
        src/jaudio/cmdstack.c
        src/jaudio/connect.c
        src/jaudio/driverinterface.c
        src/jaudio/dspdriver.c
        src/jaudio/dspinterface.c
        src/jaudio/dummyprobe.c
        src/jaudio/dummyrom.c
        src/jaudio/dvdthread.c
        src/jaudio/fat.c
        src/jaudio/file_seq.c
        src/jaudio/filter3d.c
        src/jaudio/fxinterface.c
        src/jaudio/heapctrl.c
        src/jaudio/interface.c
        src/jaudio/interleave.c
        # H4M video. The decoder is complete -- no TRAP_UNIMPLEMENTED anywhere
        # in hvqm4dec.c -- and hvqm_play.c's three are all in jaudio's own
        # decode thread, which this game never uses: movSample.cpp drives
        # Jac_HVQM_Update from a thread of its own, and that function is whole.
        src/hvqm4dec/hvqm4dec.c
        src/jaudio/hvqm_play.c
        src/jaudio/app_inter.c
        src/jaudio/ja_calc.c
        src/jaudio/jammain_2.c
        src/jaudio/jamosc.c
        src/jaudio/memory.c
        src/jaudio/midplay.c
        src/jaudio/noteon.c
        src/jaudio/oneshot.c
        src/jaudio/piki_bgm.c
        src/jaudio/piki_player.c
        src/jaudio/piki_scene.c
        src/jaudio/pikidemo.c
        src/jaudio/pikiinter.c
        src/jaudio/pikiseq.c
        src/jaudio/playercall.c
        src/jaudio/random.c
        src/jaudio/rate.c
        src/jaudio/sample.c
        src/jaudio/seqsetup.c
        src/jaudio/stackchecker.c
        src/jaudio/streamctrl.c
        src/jaudio/syncstream.c
        src/jaudio/tables.c
        src/jaudio/verysimple.c
        src/jaudio/virload.c
        src/jaudio/waveread.c
    )
    set_source_files_properties(${JAUDIO_TARGET_SOURCES} PROPERTIES LANGUAGE CXX)
    list(REMOVE_ITEM PC_PORT_SOURCES src/jaudio/pikiseq.c
        pc_port/audio/pc_dsp_host.cpp pc_port/dolphin_stubs/jaudio_hw_stubs.cpp)
    list(APPEND PC_PORT_SOURCES pc_port/audio/jaudio_integration_test.cpp)
    list(APPEND JAUDIO_TARGET_SOURCES pc_port/audio/jaudio_host.cpp
        pc_port/audio/jaudio_bank_host.cpp pc_port/audio/jaudio_sink.cpp)
    message(STATUS "Audio: JAudio + NextOS software DSP (SDL2)")
else()
    message(STATUS "Audio: legacy native PC mixer (pc_port/audio)")
endif()

# ─── Game plugin modules (the actual game code) ───
file(GLOB PLUGPIKI_KANDO_SOURCES   "src/plugPikiKando/*.cpp")
file(GLOB PLUGPIKI_NAKATA_SOURCES  "src/plugPikiNakata/*.cpp")
file(GLOB PLUGPIKI_NISHIMURA_SOURCES "src/plugPikiNishimura/*.cpp")
file(GLOB PLUGPIKI_COLIN_SOURCES   "src/plugPikiColin/*.cpp")
file(GLOB PLUGPIKI_OGAWA_SOURCES   "src/plugPikiOgawa/*.cpp")
file(GLOB PLUGPIKI_YAMASHITA_SOURCES "src/plugPikiYamashita/*.cpp")

###############################################################################
#  Files we EXCLUDE from compilation
#  These are GameCube hardware drivers that our stubs replace entirely.
###############################################################################
# We do NOT compile:
#   src/os/          — Dolphin OS implementation (replaced by os_stubs)
#   src/gx/          — GX hardware driver (replaced by gx_stubs)
#   src/dvd/         — DVD hardware driver (replaced by dvd_stubs)
#   src/pad/         — PAD hardware driver (replaced by pad_stubs)
#   src/vi/          — VI hardware driver (replaced by vi_stubs)
#   src/card/        — Memory card driver (replaced by card_stubs)
#   src/dsp/         — DSP driver (replaced by audio_stubs)
#   src/ai/          — Audio interface (replaced by audio_stubs)
#   src/ar/          — ARAM driver (replaced by os_stubs)
#   src/si/          — Serial interface (replaced by misc_stubs)
#   src/exi/         — Expansion interface (replaced by misc_stubs)
#   src/jaudio/      — JAudio library (replaced by audio_stubs)
#   src/mtx/         — Matrix math (replaced by misc_stubs)
#   src/mtxD/        — Matrix math (replaced by misc_stubs)
#   src/base/        — PPCArch (replaced by misc_stubs)
#   src/db/          — Debug (replaced by misc_stubs)
#   src/hio/         — Host I/O (replaced by misc_stubs)
#   src/hvqm4dec/    — Video decoder (built with PIKMIN_NATIVE_JAUDIO)
#   src/MSL_C/       — MetroWerks Standard Library (use system libc)
#   src/Runtime/     — CodeWarrior Runtime (use system runtime)
#   src/TRK_MINNOW_DOLPHIN/ — Debugger (not needed)
#   src/OdemuExi2/   — Dev kit hardware (not needed)
#   src/amcExi2/     — Dev kit hardware (not needed)
#   src/amcnotstub/  — Dev kit hardware (not needed)
#   src/amcstubs/    — Dev kit hardware (not needed)
#   src/odemustubs/  — Dev kit hardware (not needed)
#   src/odenotstub/  — Dev kit hardware (not needed)
#   src/sysCore/     — Windows DLL code (uses Win32 APIs, will adapt later)
#   src/sysBootup.cpp — Original entry point (replaced by pc_main.cpp)

###############################################################################
#  Build the executable
###############################################################################
# Create a static library for legacy (decompiled) sources with permissive flags.
# This isolates the legacy compilation from the strict native compilation.
# En Android el mismo árbol se añade dos veces (USA y PAL) desde
# android/app/jni/CMakeLists.txt; los nombres de target llevan entonces un
# sufijo para no chocar, y la biblioteca resultante se llama según la
# versión (libnectar.so / libnectar-pal.so), igual que los ejecutables del
# paquete de escritorio.
set(PIKMIN_TARGET_SUFFIX "" CACHE STRING "Suffix for the game targets when the tree is added more than once")
set(NECTAR_LEGACY pikmin_legacy${PIKMIN_TARGET_SUFFIX})
set(NECTAR_GAME pikmin_pc${PIKMIN_TARGET_SUFFIX})
# Solo en Android: en escritorio el ejecutable PAL sigue saliendo como
# `nectar` en su propio directorio de build y lo renombra el empaquetado.
if(ANDROID AND PIKMIN_GAME_VERSION STREQUAL "VERSION_GPIP01_00")
    set(NECTAR_OUTPUT_NAME "nectar-pal")
else()
    set(NECTAR_OUTPUT_NAME "nectar")
endif()

add_library(${NECTAR_LEGACY} STATIC
    ${SYSCOMMON_SOURCES}
    ${SYSDOLPHIN_SOURCES}
    ${PLUGPIKI_KANDO_SOURCES}
    ${PLUGPIKI_NAKATA_SOURCES}
    ${PLUGPIKI_NISHIMURA_SOURCES}
    ${PLUGPIKI_COLIN_SOURCES}
    ${PLUGPIKI_OGAWA_SOURCES}
    ${PLUGPIKI_YAMASHITA_SOURCES}
    # Empty unless PIKMIN_NATIVE_JAUDIO is on. The engine belongs here rather
    # than in ${NECTAR_GAME} because it is decompiled code and needs the permissive
    # legacy flags, not the strict ones the port's own sources build with.
    ${JAUDIO_TARGET_SOURCES}
)
target_compile_options(${NECTAR_LEGACY} PRIVATE ${LEGACY_COMPILE_OPTIONS})

# Native executable with strict flags. En Android es una biblioteca
# compartida que carga la actividad Java de SDL (libnectar.so, con SDL_main
# como punto de entrada).
if (ANDROID)
    add_library(${NECTAR_GAME} SHARED ${PC_PORT_SOURCES})
else()
    add_executable(${NECTAR_GAME} ${PC_PORT_SOURCES})
    if (WIN32)
        # Icono del .exe (recurso Windows).
        target_sources(${NECTAR_GAME} PRIVATE packaging/windows/nectar.rc)
    endif()
endif()
if (WIN32)
    # VERSIONINFO del .rc (juego y launcher): un .exe sin empresa ni versión
    # tiene menos reputación ante Defender.
    set(OPEN_NECTAR_VERSION "1.0.9" CACHE STRING "Open Nectar version of this build (GitHub release tag)")
    string(REGEX MATCHALL "[0-9]+" _nectar_ver_parts "${OPEN_NECTAR_VERSION}")
    list(APPEND _nectar_ver_parts 0 0 0 0)
    list(SUBLIST _nectar_ver_parts 0 4 _nectar_ver_parts)
    string(REPLACE ";" "," OPEN_NECTAR_VERSION_COMMAS "${_nectar_ver_parts}")
    configure_file(packaging/windows/nectar_version.h.in
                   "${CMAKE_BINARY_DIR}/generated/nectar_version.h" @ONLY)
    set_source_files_properties(packaging/windows/nectar.rc PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated"
        OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/generated/nectar_version.h")
endif()
target_compile_options(${NECTAR_GAME} PRIVATE ${NATIVE_COMPILE_OPTIONS})
target_link_libraries(${NECTAR_GAME} PRIVATE ${NECTAR_LEGACY} nectar_paths)
if(NECTAR_ONLINE)
    if(NOT TARGET nectar_net)
        add_subdirectory("${NECTAR_LIBS_DIR}/nectar_net" "${CMAKE_BINARY_DIR}/nectar_net")
    endif()
    target_link_libraries(${NECTAR_GAME} PRIVATE nectar_net)
    target_compile_definitions(${NECTAR_GAME} PRIVATE PIKI_NECTAR_ONLINE=1)
    if(PIKMIN_NETPLAY_BUILD)
        target_sources(${NECTAR_GAME} PRIVATE pc_port/netplay/pc_online_navi.cpp)
    endif()
else()
    target_compile_definitions(${NECTAR_GAME} PRIVATE PIKI_NECTAR_ONLINE=0)
endif()

# GX display lists, game logic and the software audio mixer execute on the host
# CPU. Keep optimization level, machine-specific ISA and link-time optimization
# independent so release archives can remain portable.
#
# Both targets must receive all three. The bulk of the per-frame CPU work is in
# ${NECTAR_LEGACY}, not in ${NECTAR_GAME}: renderall(), the scene traversal, the matrix
# and collision maths and every GX call the translator consumes are decompiled
# sources. When these options were attached to ${NECTAR_GAME} alone -- which they
# were from the moment the legacy sources were split out into their own library
# -- the game code silently lost -march=native and LTO, and the library
# boundary also stopped the translator from inlining into it. That is a
# performance regression with no visible symptom other than the frame time.
set(PIKMIN_OPTIMIZED_TARGETS ${NECTAR_GAME} ${NECTAR_LEGACY})

foreach(target IN LISTS PIKMIN_OPTIMIZED_TARGETS)
    target_compile_options(${target} PRIVATE
        $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU,Clang>>:-O3>
        $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O3>
        $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:C,GNU,Clang>>:-O3>
        $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O3>)
endforeach()

# OFF by default: a binary built with -march=native dies on the first
# unsupported instruction, before any window or log, on every CPU older than
# the build machine's (issues #41 and #77). Turn it on for local builds only.
option(PIKMIN_NATIVE_OPTIMIZE "Use this build machine's CPU instruction set" OFF)
if(PIKMIN_NATIVE_OPTIMIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IN LISTS PIKMIN_OPTIMIZED_TARGETS)
        target_compile_options(${target} PRIVATE -march=native)
    endforeach()
endif()

# Deterministic netplay build. OFF by default, so the default build's flags
# and libm are unchanged. -ffp-contract=off forbids fused multiply-add (the
# same binary gives the same FP results on any CPU) and
# -ftrivial-auto-var-init=zero removes uninitialised-stack garbage from the
# decompiled code. -march=native would make two machines run different code,
# so the two options exclude each other.
option(PIKMIN_NETPLAY_BUILD "Deterministic netplay build" OFF)
if(PIKMIN_NETPLAY_BUILD AND PIKMIN_NATIVE_OPTIMIZE)
    message(FATAL_ERROR "PIKMIN_NETPLAY_BUILD cannot be combined with PIKMIN_NATIVE_OPTIMIZE")
endif()
if(PIKMIN_NETPLAY_BUILD)
    set(PIKMIN_NETPLAY_SOURCE_ID "" CACHE STRING
        "Cross-platform source identity used by the netplay compatibility handshake")
    if(NOT PIKMIN_NETPLAY_SOURCE_ID)
        execute_process(
            COMMAND git rev-parse "HEAD^{tree}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE PIKMIN_NETPLAY_GIT_RESULT
            OUTPUT_VARIABLE PIKMIN_NETPLAY_GIT_TREE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT PIKMIN_NETPLAY_GIT_RESULT EQUAL 0 OR NOT PIKMIN_NETPLAY_GIT_TREE MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR
                "A deterministic netplay build needs PIKMIN_NETPLAY_SOURCE_ID when built outside a Git checkout")
        endif()
        execute_process(
            COMMAND git status --porcelain --untracked-files=all -- games/pikmin1 libs/nectar_net
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
            RESULT_VARIABLE PIKMIN_NETPLAY_STATUS_RESULT
            OUTPUT_VARIABLE PIKMIN_NETPLAY_DIRTY_FILES
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT PIKMIN_NETPLAY_STATUS_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not verify that the netplay source tree is clean")
        endif()
        string(REGEX MATCH
            "(^|\n)[^\n]*(CMakeLists\\.txt|\\.(c|cc|cpp|h|hpp|inl|cmake))($|\n)"
            PIKMIN_NETPLAY_DIRTY_SOURCE "${PIKMIN_NETPLAY_DIRTY_FILES}")
        if(PIKMIN_NETPLAY_DIRTY_SOURCE)
            message(FATAL_ERROR
                "Refusing an ambiguous netplay source identity for a dirty tree. Commit the source changes, "
                "or set PIKMIN_NETPLAY_SOURCE_ID explicitly for a private development build.")
        endif()
        set(PIKMIN_NETPLAY_SOURCE_ID "${PIKMIN_NETPLAY_GIT_TREE}")
    endif()
    message(STATUS "Netplay cross-platform source identity: ${PIKMIN_NETPLAY_SOURCE_ID}")
    target_compile_definitions(${NECTAR_GAME} PRIVATE
        PIKMIN_NETPLAY_SOURCE_ID="${PIKMIN_NETPLAY_SOURCE_ID}"
        PIKMIN_NETPLAY_GAME_VERSION="${PIKMIN_GAME_VERSION}")
    foreach(target IN LISTS PIKMIN_OPTIMIZED_TARGETS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -ffp-contract=off -ftrivial-auto-var-init=zero)
        endif()
        target_compile_definitions(${target} PRIVATE PIKI_NETPLAY_BUILD=1)
    endforeach()
endif()

# Software libm for the netplay build. pc_netplay_libm.c defines sinf, cosf,
# atan2f, powf, sqrtf... (and the double versions) as strong symbols, so the
# static link never pulls the mingw-w64 members that use x87 transcendentals,
# whose results differ between CPU vendors. It is an OBJECT library so the game
# and its test share one object and one set of flags. -fno-builtin-<name> keeps
# GCC from lowering those names back to x87 code (memcpy and sqrt stay
# builtins). -fno-lto keeps it out of LTO, which can discard builtin-named
# definitions that other partitions still reference. tools/netplay/fp_gate.py
# checks the result.
set(PC_NETPLAY_LIBM_NO_BUILTIN_FLAGS
    -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-sincosf -fno-builtin-tanf
    -fno-builtin-atanf -fno-builtin-atan2f -fno-builtin-asinf -fno-builtin-acosf
    -fno-builtin-expf -fno-builtin-logf -fno-builtin-log10f -fno-builtin-log2f
    -fno-builtin-exp2f -fno-builtin-powf -fno-builtin-fmodf -fno-builtin-hypotf
    -fno-builtin-sin -fno-builtin-cos -fno-builtin-sincos -fno-builtin-tan
    -fno-builtin-asin -fno-builtin-acos -fno-builtin-atan -fno-builtin-atan2
    -fno-builtin-exp -fno-builtin-log -fno-builtin-log10 -fno-builtin-log2
    -fno-builtin-exp2 -fno-builtin-pow -fno-builtin-fmod -fno-builtin-hypot)
# Android too: the online mode (NECTAR_ONLINE) replays the host's ticks, so
# its simulation must match bit for bit on every platform.
if(PIKMIN_NETPLAY_BUILD)
    # Created once: the Android project adds this directory for USA and PAL.
    if(NOT TARGET pc_netplay_libm_obj)
        add_library(pc_netplay_libm_obj OBJECT pc_port/netplay/pc_netplay_libm.c)
        target_compile_options(pc_netplay_libm_obj PRIVATE
            ${PC_NETPLAY_LIBM_NO_BUILTIN_FLAGS} -fno-lto -ffp-contract=off -ftrivial-auto-var-init=zero)
    endif()
    target_sources(${NECTAR_GAME} PRIVATE $<TARGET_OBJECTS:pc_netplay_libm_obj>)
endif()

# Netplay lockstep session (third_party/NETPLAY_VENDOR.md). GekkoNet is its own
# static library: GekkoLib/src/*.cpp with -std=gnu++20, GEKKONET_NO_ASIO and
# GEKKONET_STATIC. Quoted includes search its own directories first
# (-iquote): on case-insensitive file systems the game's include/Event.h would
# otherwise shadow GekkoNet's private event.h. The game's session and UDP
# transport compile as C++17 and include only gekkonet.h.
if(PIKMIN_NETPLAY_BUILD)
    if(NOT TARGET gekkonet)
        set(GEKKONET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gekkonet/GekkoLib")
        file(GLOB GEKKONET_SOURCES "${GEKKONET_DIR}/src/*.cpp")
        add_library(gekkonet STATIC ${GEKKONET_SOURCES})
        target_include_directories(gekkonet PUBLIC ${GEKKONET_DIR}/include
            PRIVATE ${GEKKONET_DIR}/include/private ${GEKKONET_DIR}/thirdparty)
        target_compile_definitions(gekkonet PUBLIC GEKKONET_STATIC PRIVATE GEKKONET_NO_ASIO)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(gekkonet PRIVATE -std=gnu++20
                "-iquote${GEKKONET_DIR}/include/private"
                "-iquote${GEKKONET_DIR}/include"
                "-iquote${GEKKONET_DIR}/thirdparty")
        endif()
    endif()
    target_sources(${NECTAR_GAME} PRIVATE
        pc_port/netplay/pc_netplay_session.cpp
        pc_port/netplay/pc_netplay_udp.cpp
        # Title Local/Online choice: rooms are host-authoritative (NECTAR1)
        # with NECTAR_ONLINE, the lockstep ICE (NPIX) room otherwise or with
        # PIKMIN_ONLINE_LEGACY_ICE=1; joining follows the pasted code.
        pc_port/netplay/pc_online_menu.cpp
        pc_port/netplay/pc_online_relaunch.cpp)
    target_link_libraries(${NECTAR_GAME} PRIVATE gekkonet)
    if(WIN32)
        target_link_libraries(${NECTAR_GAME} PRIVATE ws2_32 winmm)
    endif()
endif()

# Netplay ICE transport (connection codes, NAT traversal). libjuice is its own
# static C11 library with JUICE_STATIC and USE_NETTLE=0; the game's C++
# options (NATIVE_COMPILE_OPTIONS) are C++-only and not applied to it.
if(PIKMIN_NETPLAY_BUILD)
    if(NOT TARGET juice)
        file(GLOB LIBJUICE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjuice/src/*.c")
        add_library(juice STATIC ${LIBJUICE_SOURCES})
        set_target_properties(juice PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        target_include_directories(juice PUBLIC third_party/libjuice/include
            PRIVATE third_party/libjuice/include/juice third_party/libjuice/src)
        target_compile_definitions(juice PUBLIC JUICE_STATIC PRIVATE USE_NETTLE=0)
        if(WIN32)
            target_compile_definitions(juice PRIVATE WIN32_LEAN_AND_MEAN)
            target_link_libraries(juice PUBLIC ws2_32 bcrypt)
        endif()
        find_package(Threads REQUIRED)
        target_link_libraries(juice PRIVATE Threads::Threads)
    endif()
    target_sources(${NECTAR_GAME} PRIVATE
        pc_port/netplay/pc_netplay_ice.cpp
        pc_port/netplay/pc_netplay_launch.cpp
        # Session HUD, desync forensics and the failure-path diagnostics.
        pc_port/netplay/pc_netplay_hud.cpp
        pc_port/netplay/pc_netplay_forensics.cpp
        pc_port/netplay/pc_netplay_inlog.cpp
        pc_port/netplay/pc_state_dump.cpp
        pc_port/netplay/pc_netplay_diag.cpp
        pc_port/netplay/pc_netplay_diag_bt.cpp)
    target_link_libraries(${NECTAR_GAME} PRIVATE juice)
    # Router port mapping for ICE rooms (nectar_net's PortPublisher: PCP and
    # NAT-PMP always, UPnP when miniupnpc is found, e.g. with CMAKE_PREFIX_PATH
    # at the scripts/netplay-deps.sh prefix). Created once: Android adds this
    # directory twice, and NECTAR_ONLINE may have added it already.
    if(NOT TARGET nectar_net)
        find_package(miniupnpc CONFIG QUIET)
        if(miniupnpc_FOUND)
            set(NECTAR_NET_ENABLE_UPNP ON)
        endif()
        add_subdirectory("${NECTAR_LIBS_DIR}/nectar_net" "${CMAKE_BINARY_DIR}/nectar_net")
    endif()
    target_link_libraries(${NECTAR_GAME} PRIVATE nectar_net)
    target_compile_definitions(${NECTAR_GAME} PRIVATE PIKI_NETPLAY_PORTMAP=1)
endif()

# Default build: the netplay hooks are no-op stubs, so the weak call sites link
# on Mach-O too (see pc_port/netplay/pc_netplay_stubs.cpp).
if(NOT PIKMIN_NETPLAY_BUILD)
    target_sources(${NECTAR_GAME} PRIVATE pc_port/netplay/pc_netplay_stubs.cpp)
endif()

option(PIKMIN_ENABLE_IPO "Enable portable interprocedural/link-time optimization" ON)
if(PIKMIN_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT PIKMIN_IPO_SUPPORTED OUTPUT PIKMIN_IPO_ERROR)
    if(PIKMIN_IPO_SUPPORTED)
        foreach(target IN LISTS PIKMIN_OPTIMIZED_TARGETS)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endforeach()
    else()
        message(WARNING "IPO/LTO unavailable: ${PIKMIN_IPO_ERROR}")
    endif()
endif()

# Link against math, SDL2, and OpenGL libraries
if (WIN32)
    # MinGW ya trae las funciones matemáticas en su runtime: no existe libm
    # suelta, y OpenGL lo expone opengl32.
    #
    # SDL2main NO se enlaza: aporta un WinMain que llama a SDL_main, y este
    # proyecto declara SDL_MAIN_HANDLED y define su propio main. Enlazarlo deja
    # SDL_main sin definir en todos los ejecutables.
    target_link_libraries(${NECTAR_GAME} PRIVATE ${SDL2_LIBRARIES} opengl32 comdlg32 ws2_32)
elseif (ANDROID)
    # En Android no hay X11 ni OpenGL de escritorio. GLESv3 y EGL vienen del
    # NDK; log y android los necesita la capa de plataforma (logcat, ciclo de
    # vida de la actividad) cuando el ejecutable se convierta en biblioteca
    # compartida en la fase 4.
    target_link_libraries(${NECTAR_GAME} PRIVATE m ${SDL2_LIBRARIES} GLESv3 EGL log android)
    # Android 15 exige bibliotecas alineadas a páginas de 16 KB en arm64.
    target_link_options(${NECTAR_GAME} PRIVATE "-Wl,-z,max-page-size=16384")
else()
    if(PIKMIN_GLES)
        target_link_libraries(${NECTAR_GAME} PRIVATE m ${SDL2_LIBRARIES} OpenGL::GLES3 $<$<NOT:$<PLATFORM_ID:Darwin>>:X11>)
    else()
        target_link_libraries(${NECTAR_GAME} PRIVATE m ${SDL2_LIBRARIES} OpenGL::GL $<$<NOT:$<PLATFORM_ID:Darwin>>:X11>)
    endif()
endif()
# Export symbol names so the diagnostic backtrace in pc_gfx.cpp prints function
# names rather than bare addresses. Costs nothing at runtime.
#
# GNU-ld only: MinGW rejects the flag outright, and the backtrace it serves is
# compiled out on Windows anyway (glibc's execinfo.h has no equivalent there).
# En Android tampoco: el binario será una biblioteca compartida, no un
# ejecutable con símbolos exportados.
if (NOT WIN32 AND NOT ANDROID)
    target_link_options(${NECTAR_GAME} PRIVATE -rdynamic)
elseif (WIN32)
    # SDL2's CMake package links -mwindows, which hides the console and with it
    # every printf the port uses to report what it is doing. Keep a console on
    # the game so a failed launch says why instead of vanishing. The launcher
    # stays windowed: it talks through native dialogs.
    target_link_options(${NECTAR_GAME} PRIVATE -mconsole)
endif()

# Set output name
set_target_properties(${NECTAR_GAME} PROPERTIES
    OUTPUT_NAME "${NECTAR_OUTPUT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
if (NOT ANDROID AND NOT WIN32)
    add_custom_command(TARGET ${NECTAR_GAME} POST_BUILD
        COMMAND /bin/chmod 775 "$<TARGET_FILE:${NECTAR_GAME}>"
        COMMENT "Ensuring the native Pikmin executable is runnable")
endif()
# El launcher de escritorio busca la build europea como nectar-pal a su lado
# (gameSource/gameBinaryFor). Una build PAL hecha a mano solo dejaba `nectar`
# y el launcher pedía nectar-pal (issue #214). La copia va junto al original:
# los scripts de release siguen cogiendo bin/nectar.
if (NOT ANDROID AND PIKMIN_GAME_VERSION STREQUAL "VERSION_GPIP01_00")
    add_custom_command(TARGET ${NECTAR_GAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${NECTAR_GAME}>"
                "$<TARGET_FILE_DIR:${NECTAR_GAME}>/nectar-pal$<TARGET_FILE_SUFFIX:${NECTAR_GAME}>"
        COMMENT "Copying the PAL game as nectar-pal for the launcher")
endif()

# Standalone first-run launcher. It accepts an original, uncompressed
# GameCube ISO/GCM supplied by the user, extracts its FST into the per-user
# data directory and then starts the game from there. Keeping the extractor in
# a small library also lets CI test the legal install path without game assets.
#
# Es un programa de escritorio con diálogos nativos y dolphin-tool: en Android
# se rehace como interfaz propia (fase 7 del plan de docs/ANDROID_PLAN.md),
# así que con PIKMIN_BUILD_LAUNCHER=OFF aquí ni se compila.
if((PIKMIN_BUILD_LAUNCHER OR ANDROID) AND NOT TARGET pikmin_disc_image)
    add_library(pikmin_disc_image STATIC
        pc_port/launcher/gamecube_image.cpp
        pc_port/launcher/sha256.cpp)
    target_include_directories(pikmin_disc_image PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher)
    target_compile_options(pikmin_disc_image PRIVATE ${NATIVE_COMPILE_OPTIONS})
    set_property(TARGET pikmin_disc_image PROPERTY POSITION_INDEPENDENT_CODE ON)
    # macOS keeps iconv in its own library. gamecube_image.cpp converts disc
    # names from Shift-JIS with it.
    target_link_libraries(pikmin_disc_image PUBLIC $<$<PLATFORM_ID:Darwin>:iconv>)
endif()

# Instalador Android: la misma biblioteca de extracción, con un puente JNI
# fino, en una .so aparte para que la pantalla de instalación no cargue el
# juego (y su arena de 256 MB) antes de tiempo.
if(ANDROID AND NOT TARGET nectar_installer)
    add_library(nectar_installer SHARED pc_port/android/pc_installer_jni.cpp)
    target_compile_options(nectar_installer PRIVATE ${NATIVE_COMPILE_OPTIONS})
    target_link_libraries(nectar_installer PRIVATE pikmin_disc_image log)
    target_link_options(nectar_installer PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# Launcher en Android: la misma ventana del hub (launcher_gui.cpp) en una .so
# que carga LauncherActivity, con su propio arranque (launcher_android_main)
# y plataforma (launcher_platform_android). Ver games/pikmin2/docs/ANDROID.md.
if(ANDROID AND NOT TARGET nectar_launcher)
    add_library(launcher_imgui_android STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_sdl2.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp)
    set_property(TARGET launcher_imgui_android PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(launcher_imgui_android PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(launcher_imgui_android PUBLIC IMGUI_IMPL_OPENGL_ES3)
    target_link_libraries(launcher_imgui_android PUBLIC ${SDL2_LIBRARIES} GLESv3)

    include(cmake/EmbedFile.cmake)
    set(LAUNCHER_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/launcher_embedded")
    file(MAKE_DIRECTORY "${LAUNCHER_EMBED_DIR}")
    embed_file("${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher/assets/logo.png" "${LAUNCHER_EMBED_DIR}/logo_open_nectar.h" kLogoOpenNectar)
    embed_file("${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher/assets/Fredoka.ttf" "${LAUNCHER_EMBED_DIR}/font_fredoka.h" kFontFredoka)

    add_library(nectar_launcher SHARED
        pc_port/launcher/launcher_android_main.cpp
        pc_port/launcher/launcher_gui.cpp
        pc_port/launcher/button_style.cpp
        pc_port/launcher/button_layout.cpp
        pc_port/launcher/crash_report.cpp
        pc_port/launcher/bti_decode.cpp
        pc_port/launcher/settings_model.cpp
        pc_port/launcher/release_update.cpp
        pc_port/launcher/texture_pack_download.cpp
        pc_port/launcher/installer_ui.cpp
        pc_port/launcher/launcher_platform_android.cpp
        pc_port/pc_icon.cpp
        "${LAUNCHER_EMBED_DIR}/logo_open_nectar.h"
        "${LAUNCHER_EMBED_DIR}/font_fredoka.h")
    set_property(TARGET nectar_launcher PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(nectar_launcher PRIVATE
        ${PC_PORT_DIR}
        "${LAUNCHER_EMBED_DIR}"
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${SDL2_INCLUDE_DIRS})
    set(OPEN_NECTAR_VERSION "1.0.9" CACHE STRING "Open Nectar version of this build (GitHub release tag)")
    target_compile_definitions(nectar_launcher PRIVATE OPEN_NECTAR_VERSION="${OPEN_NECTAR_VERSION}")
    target_compile_options(nectar_launcher PRIVATE ${NATIVE_COMPILE_OPTIONS})
    target_link_libraries(nectar_launcher PRIVATE launcher_imgui_android pikmin_disc_image ${SDL2_LIBRARIES} GLESv3 EGL log android)
    target_link_options(nectar_launcher PRIVATE "-Wl,-z,max-page-size=16384")
endif()

if(PIKMIN_BUILD_LAUNCHER)

    # La capa de plataforma aísla lo único del launcher que depende del sistema:
    # rutas del ejecutable, diálogos y arranque del juego.
    if (WIN32)
        set(LAUNCHER_PLATFORM_SOURCE pc_port/launcher/launcher_platform_win32.cpp)
    else()
        set(LAUNCHER_PLATFORM_SOURCE pc_port/launcher/launcher_platform_posix.cpp)
    endif()

    find_package(Threads REQUIRED)

    # Dear ImGui (third_party/imgui, MIT) para la interfaz del launcher. Va en
    # su propia biblioteca: es código de terceros y no pasa los avisos
    # estrictos de NATIVE_COMPILE_OPTIONS.
    add_library(launcher_imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_sdl2.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp)
    # Sin las rutas globales del juego: include/Dolphin trae su propio float.h
    # y tapa el del sistema.
    set_property(TARGET launcher_imgui PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(launcher_imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${SDL2_INCLUDE_DIRS})
    target_link_libraries(launcher_imgui PUBLIC ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
    if (WIN32)
        target_link_libraries(launcher_imgui PUBLIC opengl32)
    else()
        target_link_libraries(launcher_imgui PUBLIC OpenGL::GL)
    endif()

    # Logo y fuente van dentro del ejecutable. Las portadas no: se descargan de
    # GameTDB al abrir el launcher, como hace Dolphin.
    include(cmake/EmbedFile.cmake)
    set(LAUNCHER_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/launcher_embedded")
    file(MAKE_DIRECTORY "${LAUNCHER_EMBED_DIR}")
    embed_file("${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher/assets/logo.png" "${LAUNCHER_EMBED_DIR}/logo_open_nectar.h" kLogoOpenNectar)
    embed_file("${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher/assets/Fredoka.ttf" "${LAUNCHER_EMBED_DIR}/font_fredoka.h" kFontFredoka)

    add_executable(pikmin_launcher
        pc_port/launcher/launcher_main.cpp
        pc_port/launcher/launcher_gui.cpp
        pc_port/launcher/gci_save_import.cpp
        pc_port/dolphin_stubs/pc_card_save_root.cpp
        pc_port/launcher/button_style.cpp
        pc_port/launcher/button_layout.cpp
        pc_port/launcher/crash_report.cpp
        pc_port/launcher/bti_decode.cpp
        pc_port/launcher/settings_model.cpp
        pc_port/launcher/release_update.cpp
        pc_port/launcher/texture_pack_download.cpp
        pc_port/launcher/installer_ui.cpp
        ${NECTAR_LIBS_DIR}/nectar_assets/src/charpack.cpp
        pc_port/pc_icon.cpp
        ${LAUNCHER_PLATFORM_SOURCE}
        "${LAUNCHER_EMBED_DIR}/logo_open_nectar.h"
        "${LAUNCHER_EMBED_DIR}/font_fredoka.h")
    # El launcher no usa cabeceras del juego (ver launcher_imgui).
    set_property(TARGET pikmin_launcher PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(pikmin_launcher PRIVATE
        ${PC_PORT_DIR}
        "${LAUNCHER_EMBED_DIR}"
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
    target_link_libraries(pikmin_launcher PRIVATE launcher_imgui)
    # Instalar personajes (.nectarchar) con charpack, como el juego y el creador.
    target_include_directories(pikmin_launcher PRIVATE ${NECTAR_LIBS_DIR}/nectar_assets/include)
    set_source_files_properties(${NECTAR_LIBS_DIR}/nectar_assets/src/charpack.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
    # Versión que el launcher compara con el último release de GitHub para
    # avisar y actualizarse. Súbela en cada release (la etiqueta de GitHub).
    set(OPEN_NECTAR_VERSION "1.0.9" CACHE STRING "Open Nectar version of this build (GitHub release tag)")
    target_compile_definitions(pikmin_launcher PRIVATE OPEN_NECTAR_VERSION="${OPEN_NECTAR_VERSION}")
    if (WIN32)
        target_sources(pikmin_launcher PRIVATE packaging/windows/nectar.rc)
    endif()
    # Host Co-op / Join Co-op en el launcher: apagado por defecto, el online se
    # abre desde el propio juego (Co-op / VS -> Online).
    option(PIKMIN_LAUNCHER_NETPLAY_BUTTONS "Show the Host/Join Co-op buttons in the launcher" OFF)
    if(PIKMIN_NETPLAY_BUILD AND PIKMIN_LAUNCHER_NETPLAY_BUTTONS)
        target_sources(pikmin_launcher PRIVATE pc_port/launcher/launcher_coop.cpp)
        target_compile_definitions(pikmin_launcher PRIVATE PIKMIN_LAUNCHER_NETPLAY=1)
    endif()
    target_include_directories(pikmin_launcher PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(pikmin_launcher PRIVATE pikmin_disc_image nectar_paths ${SDL2_LIBRARIES} Threads::Threads)
    if (WIN32)
        # Diálogo de archivo clásico, selector de carpeta del shell y COM.
        target_link_libraries(pikmin_launcher PRIVATE comdlg32 ole32 shell32 uuid winhttp)
    endif()
    target_compile_options(pikmin_launcher PRIVATE ${NATIVE_COMPILE_OPTIONS})
    set_target_properties(pikmin_launcher PROPERTIES
        OUTPUT_NAME "nectar-launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Both executables must remain side by side: the launcher resolves the game
    # binary relative to itself, so this also works in relocatable archives.
    install(TARGETS pikmin_launcher RUNTIME DESTINATION bin)
endif()

install(TARGETS ${NECTAR_GAME} RUNTIME DESTINATION bin)

# Tests del launcher: necesitan tanto los tests habilitados como el launcher.
if(PIKMIN_BUILD_TESTS AND PIKMIN_BUILD_LAUNCHER)
    add_executable(gamecube_image_test
        pc_port/launcher/gamecube_image_test.cpp)
    target_link_libraries(gamecube_image_test PRIVATE pikmin_disc_image)
    target_compile_options(gamecube_image_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME gamecube_image_test COMMAND gamecube_image_test)

    add_executable(sha256_test
        pc_port/launcher/sha256_test.cpp)
    target_link_libraries(sha256_test PRIVATE pikmin_disc_image)
    target_compile_options(sha256_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME sha256_test COMMAND sha256_test)

    add_executable(gci_save_import_test pc_port/launcher/gci_save_import_test.cpp
        pc_port/launcher/gci_save_import.cpp
        pc_port/dolphin_stubs/pc_card_save_root.cpp)
    target_include_directories(gci_save_import_test PRIVATE pc_port/launcher ${PC_PORT_DIR})
    target_compile_options(gci_save_import_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME gci_save_import_test COMMAND gci_save_import_test)

    add_executable(installer_ui_test
        pc_port/launcher/installer_ui_test.cpp
        pc_port/launcher/installer_ui.cpp
        pc_port/pc_icon.cpp
        ${LAUNCHER_PLATFORM_SOURCE})
    target_include_directories(installer_ui_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher
        ${SDL2_INCLUDE_DIRS})
    target_link_libraries(installer_ui_test PRIVATE nectar_paths ${SDL2_LIBRARIES} Threads::Threads)
    if (WIN32)
        target_link_libraries(installer_ui_test PRIVATE comdlg32 ole32 shell32 uuid winhttp)
    endif()
    target_compile_options(installer_ui_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME installer_ui_test COMMAND installer_ui_test)

    add_executable(launcher_sidebar_layout_test pc_port/launcher/launcher_sidebar_layout_test.cpp)
    target_compile_options(launcher_sidebar_layout_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME launcher_sidebar_layout_test COMMAND launcher_sidebar_layout_test)

    add_executable(launcher_page_layout_test pc_port/launcher/launcher_page_layout_test.cpp)
    target_compile_options(launcher_page_layout_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME launcher_page_layout_test COMMAND launcher_page_layout_test)

    add_executable(asset_finalize_test pc_port/launcher/asset_finalize_test.cpp)
    target_link_libraries(asset_finalize_test PRIVATE Threads::Threads)
    target_compile_options(asset_finalize_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME asset_finalize_test COMMAND asset_finalize_test)

    add_executable(prepared_image_test pc_port/launcher/prepared_image_test.cpp ${LAUNCHER_PLATFORM_SOURCE})
    target_compile_options(prepared_image_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    if (WIN32)
        target_link_libraries(prepared_image_test PRIVATE comdlg32 ole32 shell32 uuid winhttp)
    endif()
    target_link_libraries(prepared_image_test PRIVATE nectar_paths)
    add_test(NAME prepared_image_test COMMAND prepared_image_test)

    # AppleScript quoting for the macOS dialogs, and closing a dialog process
    # that is still open when the launcher quits.
    if (NOT WIN32)
        add_executable(launcher_dialog_test pc_port/launcher/launcher_dialog_test.cpp ${LAUNCHER_PLATFORM_SOURCE})
        target_link_libraries(launcher_dialog_test PRIVATE nectar_paths Threads::Threads)
        target_compile_options(launcher_dialog_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
        add_test(NAME launcher_dialog_test COMMAND launcher_dialog_test)
    endif()

    # The game's file picker: title prefix, and opening, answering and closing
    # the dialog process without blocking the frame loop.
    if (NOT WIN32)
        add_executable(pc_file_dialog_test pc_port/pc_file_dialog_test.cpp pc_port/pc_file_dialog.cpp)
        target_compile_options(pc_file_dialog_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
        add_test(NAME pc_file_dialog_test COMMAND pc_file_dialog_test)
    endif()

    # POSIX-only: it drives the XDG folders, setenv and chmod'ed read-only
    # installs, while Windows keeps settings under %LOCALAPPDATA%.
    if(NOT WIN32)
        add_executable(nectar_paths_test "${NECTAR_LIBS_DIR}/nectar_paths/tests/paths_test.cpp")
        target_link_libraries(nectar_paths_test PRIVATE nectar_paths)
        target_compile_options(nectar_paths_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
        add_test(NAME nectar_paths_test COMMAND nectar_paths_test)
    endif()

    add_executable(button_style_test
        pc_port/launcher/button_style_test.cpp
        pc_port/launcher/button_style.cpp)
    target_compile_options(button_style_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME button_style_test COMMAND button_style_test)

    # Margen y anchos estándar de los botones: mide los textos reales con
    # Fredoka (atlas en CPU, sin ventana) a 1x y 2x.
    add_executable(button_layout_test
        pc_port/launcher/button_layout_test.cpp
        pc_port/launcher/button_layout.cpp
        "${LAUNCHER_EMBED_DIR}/font_fredoka.h")
    # Como el launcher: sin las cabeceras del juego (traen su propio float.h).
    set_property(TARGET button_layout_test PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(button_layout_test PRIVATE ${PC_PORT_DIR} "${LAUNCHER_EMBED_DIR}")
    target_link_libraries(button_layout_test PRIVATE launcher_imgui)
    target_compile_options(button_layout_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME button_layout_test COMMAND button_layout_test)

    if(TARGET launcher_imgui)
        # Mano sobre desplegables, entradas y botones de opción (ImGui sin ventana).
        add_executable(launcher_cursor_test pc_port/launcher/launcher_cursor_test.cpp)
        # Sin las cabeceras del juego, igual que launcher_imgui.
        set_property(TARGET launcher_cursor_test PROPERTY INCLUDE_DIRECTORIES "")
        target_compile_options(launcher_cursor_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
        target_link_libraries(launcher_cursor_test PRIVATE launcher_imgui)
        add_test(NAME launcher_cursor_test COMMAND launcher_cursor_test)
    endif()

    add_executable(launcher_scroll_test pc_port/launcher/launcher_scroll_test.cpp)
    target_compile_options(launcher_scroll_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME launcher_scroll_test COMMAND launcher_scroll_test)

    if(PIKMIN_NETPLAY_BUILD)
        # Host Co-op / Join Co-op flow with a fake game.
        add_executable(launcher_coop_test pc_port/launcher/launcher_coop_test.cpp pc_port/launcher/launcher_coop.cpp)
        target_compile_options(launcher_coop_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
        add_test(NAME launcher_coop_test COMMAND launcher_coop_test)
    endif()
endif()

if(PIKMIN_BUILD_TESTS)
    add_executable(pc_bundle_compat_test pc_port/pc_bundle_compat_test.cpp)
    target_include_directories(pc_bundle_compat_test PRIVATE pc_port)
    target_compile_options(pc_bundle_compat_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_bundle_compat_test COMMAND pc_bundle_compat_test)

    add_executable(pc_generator_cache_validation_test
        pc_port/save/pc_generator_cache_validation_test.cpp)
    target_include_directories(pc_generator_cache_validation_test PRIVATE pc_port/save)
    target_compile_options(pc_generator_cache_validation_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_generator_cache_validation_test COMMAND pc_generator_cache_validation_test)

    # Envelope smoke test executable (not linked into main game)
    add_executable(pc_envelope_test pc_port/audio/pc_envelope_test.cpp pc_port/audio/pc_envelope.cpp)
    target_link_libraries(pc_envelope_test PRIVATE m)
    target_compile_options(pc_envelope_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_envelope_test COMMAND pc_envelope_test)

    # Co-op formation (C-stick swarm) sound arbitration (header only, no audio engine)
    add_executable(pc_formation_arbiter_test pc_port/audio/pc_formation_arbiter_test.cpp)
    target_compile_options(pc_formation_arbiter_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_formation_arbiter_test COMMAND pc_formation_arbiter_test)

    # Host-backed ARAM test (not linked into main game)
    add_executable(pc_aram_test pc_port/audio/pc_aram_test.cpp pc_port/audio/pc_aram.cpp)
    target_compile_options(pc_aram_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_aram_test COMMAND pc_aram_test)

    # Pad axis conversion (no controller needed)
    add_executable(pc_pad_axis_test pc_port/pc_pad_axis_test.cpp pc_port/pc_pad_axis.cpp)
    target_include_directories(pc_pad_axis_test PRIVATE ${SDL2_INCLUDE_DIRS} pc_port)
    target_compile_options(pc_pad_axis_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_pad_axis_test COMMAND pc_pad_axis_test)

    # Menu pad translation (no window or controller needed)
    add_executable(pc_menu_pad_test pc_port/pc_menu_pad_test.cpp pc_port/pc_menu_pad.cpp)
    target_include_directories(pc_menu_pad_test PRIVATE ${SDL2_INCLUDE_DIRS} pc_port)
    target_compile_options(pc_menu_pad_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_menu_pad_test COMMAND pc_menu_pad_test)

    # Cutscene skip replays its keys by frame (header only)
    add_executable(pc_cine_skip_order_test pc_port/pc_cine_skip_order_test.cpp)
    target_include_directories(pc_cine_skip_order_test PRIVATE pc_port)
    target_compile_options(pc_cine_skip_order_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_cine_skip_order_test COMMAND pc_cine_skip_order_test)

    # Permadeath save block (no game needed)
    add_executable(pc_permadeath_test pc_port/pc_permadeath_test.cpp pc_port/pc_permadeath.cpp src/sysCommon/stream.cpp src/sysCommon/string.cpp)
    target_include_directories(pc_permadeath_test PRIVATE pc_port include)
    target_compile_options(pc_permadeath_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_permadeath_test COMMAND pc_permadeath_test)

    # Photo mode camera convention (no stage needed)
    add_executable(pc_photo_mode_test pc_port/pc_photo_mode_test.cpp pc_port/pc_photo_mode.cpp)
    target_include_directories(pc_photo_mode_test PRIVATE ${SDL2_INCLUDE_DIRS} pc_port)
    target_link_libraries(pc_photo_mode_test PRIVATE ${SDL2_LIBRARIES})
    target_compile_options(pc_photo_mode_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_photo_mode_test COMMAND pc_photo_mode_test)

    # Which GPU to ask for on a switchable-graphics laptop (no GPU needed)
    add_executable(pc_gpu_preference_test pc_port/pc_gpu_preference_test.cpp pc_port/pc_gpu_preference.cpp)
    target_include_directories(pc_gpu_preference_test PRIVATE pc_port)
    target_compile_options(pc_gpu_preference_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_gpu_preference_test COMMAND pc_gpu_preference_test)

    # Menu input repeat (no controller needed)
    add_executable(pc_menu_repeat_test pc_port/pc_menu_repeat_test.cpp pc_port/pc_menu_repeat.cpp)
    target_include_directories(pc_menu_repeat_test PRIVATE pc_port)
    target_compile_options(pc_menu_repeat_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_menu_repeat_test COMMAND pc_menu_repeat_test)

    # Readable host memory for the GX bounds (issue #45: macOS has no /proc)
    add_executable(pc_host_readable_test
        pc_port/dolphin_stubs/pc_host_readable_test.cpp
        pc_port/dolphin_stubs/pc_host_readable.cpp
        pc_port/dolphin_stubs/pc_host_readable_os.cpp)
    target_compile_definitions(pc_host_readable_test PRIVATE PC_HOST_READABLE_TEST_P1)
    target_compile_options(pc_host_readable_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_host_readable_test COMMAND pc_host_readable_test)

    # Sprout landing policy (no map or window)
    add_executable(pc_seed_landing_test pc_port/pc_seed_landing_test.cpp)
    target_include_directories(pc_seed_landing_test PRIVATE pc_port)
    target_compile_options(pc_seed_landing_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_seed_landing_test COMMAND pc_seed_landing_test)

    # Title screen and main menu clicks (no window, no disc)
    add_executable(pc_menu_click_test pc_port/pc_menu_click_test.cpp pc_port/pc_menu_click.cpp)
    target_include_directories(pc_menu_click_test PRIVATE pc_port)
    target_compile_options(pc_menu_click_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_menu_click_test COMMAND pc_menu_click_test)

    add_executable(pc_file_select_pointer_test pc_port/pc_file_select_pointer_test.cpp pc_port/pc_menu_click.cpp)
    target_include_directories(pc_file_select_pointer_test PRIVATE pc_port)
    target_compile_options(pc_file_select_pointer_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_file_select_pointer_test COMMAND pc_file_select_pointer_test)

    # Hand cursor over pointer menu targets: hover state to cursor shape, with a stub setter
    add_executable(pc_hand_cursor_test pc_port/pc_hand_cursor_test.cpp pc_port/pc_hand_cursor.cpp
                   pc_port/pc_menu_click.cpp)
    target_include_directories(pc_hand_cursor_test PRIVATE pc_port)
    target_compile_options(pc_hand_cursor_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_hand_cursor_test COMMAND pc_hand_cursor_test)

    # New Game and Captain prompt mouse targets and their mapping (no window, no disc)
    add_executable(pc_prompt_pointer_test pc_port/pc_prompt_pointer_test.cpp
                   pc_port/settings/pc_prompt_pointer.cpp pc_port/pc_menu_click.cpp)
    target_include_directories(pc_prompt_pointer_test PRIVATE pc_port)
    target_compile_options(pc_prompt_pointer_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_prompt_pointer_test COMMAND pc_prompt_pointer_test)

    # Drawn button prompts (Y Ship's Log, A Read Diary) as click targets: rows, clicks, hover, log
    add_executable(pc_button_prompt_test pc_port/pc_button_prompt_test.cpp pc_port/pc_button_prompt.cpp
                   pc_port/pc_menu_click.cpp)
    target_include_directories(pc_button_prompt_test PRIVATE pc_port)
    target_compile_options(pc_button_prompt_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_button_prompt_test COMMAND pc_button_prompt_test)

    # Host software DSP test (not linked into main game)
    add_executable(pc_dsp_host_test
        pc_port/audio/pc_dsp_host_test.cpp
        pc_port/audio/pc_dsp_host.cpp
        pc_port/audio/pc_aram.cpp)
    target_compile_options(pc_dsp_host_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_dsp_host_test COMMAND pc_dsp_host_test)

    add_executable(pc_jam_test
        pc_port/audio/pc_jam_test.cpp
        pc_port/audio/pc_jam.cpp
        pc_port/audio/pc_instrument_bank.cpp
        pc_port/audio/pc_sequence_archive.cpp
        pc_port/audio/pc_wave_bank.cpp
        src/jaudio/pikiseq.c)
    target_compile_options(pc_jam_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/dataDir/SndData/Banks/pikibank.bx")
        add_test(NAME pc_jam_test COMMAND pc_jam_test)
        set_tests_properties(pc_jam_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    add_executable(pc_audio_mixer_test
        pc_port/audio/pc_audio_mixer_test.cpp
        pc_port/audio/pc_audio.cpp
        pc_port/audio/pc_envelope.cpp
        pc_port/audio/pc_jam.cpp
        pc_port/audio/pc_instrument_bank.cpp
        pc_port/audio/pc_sequence_archive.cpp
        pc_port/audio/pc_wave_bank.cpp
        src/jaudio/pikiseq.c)
    target_link_libraries(pc_audio_mixer_test PRIVATE m ${SDL2_LIBRARIES})
    target_compile_options(pc_audio_mixer_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/dataDir/SndData/Banks/pikibank.bx")
        add_test(NAME pc_audio_mixer_test COMMAND pc_audio_mixer_test)
        set_tests_properties(pc_audio_mixer_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    add_executable(pc_frame_scheduler_test
        pc_port/timing/pc_frame_scheduler_test.cpp
        pc_port/timing/pc_frame_scheduler.cpp)
    target_compile_options(pc_frame_scheduler_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_frame_scheduler_test COMMAND pc_frame_scheduler_test)

    add_executable(pc_render_phase_test
        pc_port/timing/pc_render_phase_test.cpp
        pc_port/timing/pc_render_phase.cpp)
    target_compile_options(pc_render_phase_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_render_phase_test COMMAND pc_render_phase_test)

    add_executable(pc_visual_snapshot_test
        pc_port/timing/pc_visual_snapshot_test.cpp
        pc_port/timing/pc_visual_snapshot.cpp)
    target_compile_options(pc_visual_snapshot_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_visual_snapshot_test COMMAND pc_visual_snapshot_test)

    add_executable(pc_camera_snapshot_test
        pc_port/timing/pc_camera_snapshot_test.cpp
        pc_port/timing/pc_camera_snapshot.cpp)
    target_compile_options(pc_camera_snapshot_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_camera_snapshot_test COMMAND pc_camera_snapshot_test)

    add_executable(pc_visual_runtime_test
        pc_port/timing/pc_visual_runtime_test.cpp
        pc_port/timing/pc_visual_runtime.cpp
        pc_port/timing/pc_render_phase.cpp
        pc_port/timing/pc_visual_snapshot.cpp)
    target_compile_options(pc_visual_runtime_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_visual_runtime_test COMMAND pc_visual_runtime_test)

    add_executable(pc_tev_shader_test
        pc_port/gl/pc_tev_shader_test.cpp
        pc_port/gl/pc_tev_shader.cpp
    )
    target_include_directories(pc_tev_shader_test PRIVATE pc_port/gl)
    target_compile_options(pc_tev_shader_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_tev_shader_test COMMAND pc_tev_shader_test)

    # Post-process shader generation (no GL context needed)
    add_executable(pc_postprocess_test
        pc_port/gl/pc_postprocess_test.cpp
        pc_port/gl/pc_postprocess.cpp
    )
    target_include_directories(pc_postprocess_test PRIVATE pc_port/gl)
    target_compile_options(pc_postprocess_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_postprocess_test COMMAND pc_postprocess_test)

    add_executable(pc_render_packet_test
        pc_port/timing/pc_render_packet_test.cpp
        pc_port/timing/pc_render_packet.cpp)
    target_compile_options(pc_render_packet_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_render_packet_test COMMAND pc_render_packet_test)

    add_executable(pc_tick_profiler_test
        pc_port/timing/pc_tick_profiler_test.cpp
        pc_port/timing/pc_tick_profiler.cpp
        pc_port/netplay/pc_netplay_det.cpp
        pc_port/netplay/pc_sim_rng.cpp)
    target_include_directories(pc_tick_profiler_test PRIVATE pc_port/timing)
    target_compile_options(pc_tick_profiler_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_tick_profiler_test COMMAND pc_tick_profiler_test)

    # Netplay determinism seams (engine-free). They also check that every
    # helper is a pass-through with the deterministic switch off.
    add_executable(pc_sim_rng_test
        pc_port/netplay/pc_sim_rng_test.cpp
        pc_port/netplay/pc_sim_rng.cpp
        pc_port/netplay/pc_netplay_det.cpp)
    target_compile_options(pc_sim_rng_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_sim_rng_test COMMAND pc_sim_rng_test)

    add_executable(pc_netplay_policy_test
        pc_port/netplay/pc_netplay_policy_test.cpp
        pc_port/netplay/pc_netplay_policy.cpp
        pc_port/netplay/pc_netplay_det.cpp
        pc_port/netplay/pc_sim_rng.cpp)
    target_compile_options(pc_netplay_policy_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_policy_test COMMAND pc_netplay_policy_test)

    # Two-pass present module: skip flag, null-GX counters, local player.
    add_executable(pc_netplay_present_test
        pc_port/netplay/pc_netplay_present_test.cpp
        pc_port/netplay/pc_netplay_present.cpp
        pc_port/netplay/pc_netplay_det.cpp
        pc_port/netplay/pc_sim_rng.cpp)
    target_compile_definitions(pc_netplay_present_test PRIVATE PC_NETPLAY_PRESENT_HOST=1)
    target_compile_options(pc_netplay_present_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_present_test COMMAND pc_netplay_present_test)

    # Lead camera core (header-only): key mapping, input history, prediction
    # window, correction filter.
    add_executable(pc_netplay_camlead_test pc_port/netplay/pc_netplay_camlead_test.cpp)
    target_include_directories(pc_netplay_camlead_test PRIVATE pc_port)
    target_compile_options(pc_netplay_camlead_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_camlead_test COMMAND pc_netplay_camlead_test)

    # Netplay audio ownership gate (header-only).
    add_executable(pc_audio_source_test pc_port/audio/pc_audio_source_test.cpp)
    target_include_directories(pc_audio_source_test PRIVATE pc_port)
    target_compile_options(pc_audio_source_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_audio_source_test COMMAND pc_audio_source_test)

    # Netplay session HUD model and the desync forensics data model / input log.
    add_executable(pc_netplay_hud_model_test pc_port/netplay/pc_netplay_hud_model_test.cpp)
    target_include_directories(pc_netplay_hud_model_test PRIVATE pc_port)
    target_compile_options(pc_netplay_hud_model_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_hud_model_test COMMAND pc_netplay_hud_model_test)
    add_executable(pc_netplay_forensics_test
        pc_port/netplay/pc_netplay_forensics_test.cpp
        pc_port/netplay/pc_netplay_forensics.cpp
        pc_port/netplay/pc_netplay_inlog.cpp)
    target_include_directories(pc_netplay_forensics_test PRIVATE pc_port)
    target_compile_options(pc_netplay_forensics_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_forensics_test COMMAND pc_netplay_forensics_test)

    # Netplay handshake Hello codec, --netplay-input parse and the co-op switch.
    add_executable(pc_netplay_transfer_test pc_port/netplay/pc_netplay_transfer_test.cpp)
    target_include_directories(pc_netplay_transfer_test PRIVATE pc_port)
    target_compile_options(pc_netplay_transfer_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_transfer_test COMMAND pc_netplay_transfer_test)
    add_executable(pc_netplay_input_sel_test pc_port/netplay/pc_netplay_input_sel_test.cpp)
    target_include_directories(pc_netplay_input_sel_test PRIVATE pc_port)
    target_compile_options(pc_netplay_input_sel_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_netplay_input_sel_test COMMAND pc_netplay_input_sel_test)
    add_executable(pc_coop_switch_test
        pc_port/netplay/pc_coop_switch_test.cpp
        pc_port/netplay/pc_coop_switch.cpp)
    target_include_directories(pc_coop_switch_test PRIVATE pc_port)
    target_compile_options(pc_coop_switch_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_coop_switch_test COMMAND pc_coop_switch_test)

    # PKNI input-file format, yaw quantisation and the per-tick yaw slots.
    add_executable(pc_input_log_test
        pc_port/netplay/pc_input_log_test.cpp
        pc_port/netplay/pc_input_log.cpp)
    target_compile_options(pc_input_log_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_input_log_test COMMAND pc_input_log_test)
    # A stray switch in the caller's environment would load a real file.
    set_tests_properties(pc_input_log_test PROPERTIES ENVIRONMENT "PIKMIN_INPUT_RECORD=;PIKMIN_INPUT_REPLAY=")
endif()

# Netplay-only checks. The libm test compares against the platform libm
# (msvcrt.dll on Windows), which varies with the OS version, so it is not
# part of the default test run. fp_gate disassembles the linked game and
# fails on any x87 transcendental or fsqrt, any FMA, and any math import
# from the C runtime.
if(PIKMIN_NETPLAY_BUILD AND PIKMIN_BUILD_TESTS AND NOT ANDROID)
    add_executable(pc_netplay_libm_test
        pc_port/netplay/pc_netplay_libm_test.cpp
        $<TARGET_OBJECTS:pc_netplay_libm_obj>)
    target_compile_options(pc_netplay_libm_test PRIVATE ${NATIVE_COMPILE_OPTIONS}
        ${PC_NETPLAY_LIBM_NO_BUILTIN_FLAGS} -ffp-contract=off)
    add_test(NAME pc_netplay_libm_test COMMAND pc_netplay_libm_test)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND AND CMAKE_OBJDUMP)
        add_test(NAME netplay_fp_gate
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/netplay/fp_gate.py"
                    --objdump "${CMAKE_OBJDUMP}" "$<TARGET_FILE:${NECTAR_GAME}>")
    endif()

    # GekkoNet-backed host tests: the UDP transport and lossy wrapper over two
    # in-process sessions, the load guard's GekkoNet assumptions, and the
    # adaptive delay's transitions.
    foreach(test pc_netplay_transport_test pc_netplay_loadguard_test pc_netplay_adaptive_test)
        add_executable(${test} pc_port/netplay/${test}.cpp)
        target_include_directories(${test} PRIVATE pc_port)
        target_compile_options(${test} PRIVATE ${NATIVE_COMPILE_OPTIONS})
        target_link_libraries(${test} PRIVATE gekkonet)
        if(WIN32)
            target_link_libraries(${test} PRIVATE ws2_32)
        endif()
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
    target_sources(pc_netplay_transport_test PRIVATE pc_port/netplay/pc_netplay_udp.cpp)

    # ICE codes and two libjuice agents over loopback (no network needed).
    add_executable(pc_netplay_ice_test
        pc_port/netplay/pc_netplay_ice_test.cpp
        pc_port/netplay/pc_netplay_ice.cpp)
    target_include_directories(pc_netplay_ice_test PRIVATE pc_port)
    target_compile_options(pc_netplay_ice_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    target_link_libraries(pc_netplay_ice_test PRIVATE juice gekkonet)
    add_test(NAME pc_netplay_ice_test COMMAND pc_netplay_ice_test)
endif()

# Loopback UDP impairment proxy for netplay tests (pc_netplay_impair.h). It sits
# between the two peers, so any executable sees the same impaired link.
if(PIKMIN_NETPLAY_BUILD AND WIN32 AND NOT ANDROID)
    add_executable(netplay_impair_proxy tools/netplay/udp_impair_proxy.cpp)
    target_include_directories(netplay_impair_proxy PRIVATE pc_port)
    target_compile_options(netplay_impair_proxy PRIVATE ${NATIVE_COMPILE_OPTIONS})
    target_link_libraries(netplay_impair_proxy PRIVATE ws2_32 winmm)
endif()

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════╗")
message(STATUS "║   Pikmin PC Port - Build Configuration   ║")
message(STATUS "╠══════════════════════════════════════════╣")
# La versión real: antes estaba fija en "USA Rev 1" y una build PAL parecía
# no haber cogido -DPIKMIN_GAME_VERSION (issue #214).
string(REPLACE "VERSION_" "" _nectar_version_label "${PIKMIN_GAME_VERSION}")
message(STATUS "║   Version:  ${_nectar_version_label}")
if(PIKMIN_GLES)
    message(STATUS "║   Graphics: Native GX → OpenGL ES 3.0   ║")
else()
    message(STATUS "║   Graphics: Native GX → OpenGL          ║")
endif()
message(STATUS "║   Audio:    SDL mixer/STX; JAudio WIP   ║")
message(STATUS "║   Input:    Native SDL keyboard/gamepad ║")
message(STATUS "╚══════════════════════════════════════════╝")
message(STATUS "")

if(PIKMIN_NATIVE_JAUDIO AND PIKMIN_BUILD_TESTS)
    add_test(NAME pc_jaudio_integration_test COMMAND ${NECTAR_GAME} --audio-self-test)
    set_tests_properties(pc_jaudio_integration_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30
        SKIP_RETURN_CODE 77 LABELS "game-assets")
endif()

# Editor de mapas VS (tools/mapmaker): sale en bin/ junto al juego, que es
# donde busca el ejecutable para "Probar". Usa la ImGui del launcher.
if (NECTAR_BUILD_MAPMAKER AND NOT ANDROID)
    add_subdirectory("${NECTAR_LIBS_DIR}/../tools/mapmaker" "${CMAKE_CURRENT_BINARY_DIR}/mapmaker")
endif()

# Creador de modelos (tools/modelmaker): Olimar propio a partir de un glTF,
# VRM u OBJ. Comparte con el editor de mapas la ImGui, OpenGL y nectar_assets.
if (NECTAR_BUILD_MODELMAKER AND NOT ANDROID)
    add_subdirectory("${NECTAR_LIBS_DIR}/../tools/modelmaker" "${CMAKE_CURRENT_BINARY_DIR}/modelmaker")
endif()
