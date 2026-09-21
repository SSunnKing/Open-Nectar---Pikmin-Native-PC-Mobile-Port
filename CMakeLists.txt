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
    $<$<CXX_COMPILER_ID:Clang>:-Wno-register>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-address-of-temporary>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unknown-warning-option>
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

# Testing shortcuts (F5 stocks Pikmin, F6 skips an hour) and the menu row that
# switches them on. Off by default so a release carries no trace of them; turn
# on with -DPIKMIN_DEBUG_KEYS=ON while working on the Mods settings.
option(PIKMIN_DEBUG_KEYS "Build the F5/F6 testing shortcuts" OFF)

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

set(PC_PORT_SOURCES
    pc_port/pc_main.cpp
    pc_port/pc_window.cpp
    pc_port/pc_pad_axis.cpp
    pc_port/pc_menu_repeat.cpp
    pc_port/pc_photo_mode.cpp
    pc_port/pc_permadeath.cpp
    pc_port/pc_coop.cpp
    pc_port/pc_art.cpp
    pc_port/pc_icon.cpp
    pc_port/settings/pc_glass_menu.cpp
    pc_port/pc_gpu_preference.cpp
    pc_port/mods/pc_hd_models.cpp
    pc_port/mods/pc_hd_model_convert.cpp
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
    src/jaudio/pikiseq.c
    pc_port/dolphin_stubs/os_stubs.cpp
    pc_port/dolphin_stubs/gx_stubs.cpp
    pc_port/dolphin_stubs/dvd_stubs.cpp
    pc_port/dolphin_stubs/pad_stubs.cpp
    pc_port/dolphin_stubs/vi_stubs.cpp
    pc_port/dolphin_stubs/card_stubs.cpp
    pc_port/dolphin_stubs/audio_stubs.cpp
    pc_port/dolphin_stubs/jaudio_hw_stubs.cpp
    pc_port/dolphin_stubs/misc_stubs.cpp
)
if (ANDROID)
    list(APPEND PC_PORT_SOURCES
        pc_port/android/pc_android.cpp
        pc_port/android/pc_texpack_android.cpp
        pc_port/android/pc_save_android.cpp)
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
)

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
target_compile_options(${NECTAR_GAME} PRIVATE ${NATIVE_COMPILE_OPTIONS})
target_link_libraries(${NECTAR_GAME} PRIVATE ${NECTAR_LEGACY})

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

option(PIKMIN_NATIVE_OPTIMIZE "Use this build machine's CPU instruction set" ON)
if(PIKMIN_NATIVE_OPTIMIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IN LISTS PIKMIN_OPTIMIZED_TARGETS)
        target_compile_options(${target} PRIVATE -march=native)
    endforeach()
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
    target_link_libraries(${NECTAR_GAME} PRIVATE ${SDL2_LIBRARIES} opengl32 comdlg32)
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
        target_link_libraries(${NECTAR_GAME} PRIVATE m ${SDL2_LIBRARIES} OpenGL::GLES3 X11)
    else()
        target_link_libraries(${NECTAR_GAME} PRIVATE m ${SDL2_LIBRARIES} OpenGL::GL X11)
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
if (NOT ANDROID)
    add_custom_command(TARGET ${NECTAR_GAME} POST_BUILD
        COMMAND /bin/chmod 775 "$<TARGET_FILE:${NECTAR_GAME}>"
        COMMENT "Ensuring the native Pikmin executable is runnable")
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

if(PIKMIN_BUILD_LAUNCHER)

    # La capa de plataforma aísla lo único del launcher que depende del sistema:
    # rutas del ejecutable, diálogos y arranque del juego.
    if (WIN32)
        set(LAUNCHER_PLATFORM_SOURCE pc_port/launcher/launcher_platform_win32.cpp)
    else()
        set(LAUNCHER_PLATFORM_SOURCE pc_port/launcher/launcher_platform_posix.cpp)
    endif()

    find_package(Threads REQUIRED)
    add_executable(pikmin_launcher
        pc_port/launcher/launcher_main.cpp
        pc_port/launcher/installer_ui.cpp
        pc_port/pc_icon.cpp
        ${LAUNCHER_PLATFORM_SOURCE})
    if (WIN32)
        target_sources(pikmin_launcher PRIVATE packaging/windows/nectar.rc)
    endif()
    target_include_directories(pikmin_launcher PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(pikmin_launcher PRIVATE pikmin_disc_image ${SDL2_LIBRARIES} Threads::Threads)
    if (WIN32)
        # Diálogo de archivo clásico, selector de carpeta del shell y COM.
        target_link_libraries(pikmin_launcher PRIVATE comdlg32 ole32 shell32 uuid)
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

    add_executable(installer_ui_test
        pc_port/launcher/installer_ui_test.cpp
        pc_port/launcher/installer_ui.cpp
        pc_port/pc_icon.cpp)
    target_include_directories(installer_ui_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/launcher
        ${SDL2_INCLUDE_DIRS})
    target_link_libraries(installer_ui_test PRIVATE ${SDL2_LIBRARIES})
    target_compile_options(installer_ui_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME installer_ui_test COMMAND installer_ui_test)

    add_executable(asset_finalize_test pc_port/launcher/asset_finalize_test.cpp)
    target_link_libraries(asset_finalize_test PRIVATE Threads::Threads)
    target_compile_options(asset_finalize_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME asset_finalize_test COMMAND asset_finalize_test)

    add_executable(prepared_image_test pc_port/launcher/prepared_image_test.cpp ${LAUNCHER_PLATFORM_SOURCE})
    target_compile_options(prepared_image_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    if (WIN32)
        target_link_libraries(prepared_image_test PRIVATE comdlg32 ole32 shell32 uuid)
    endif()
    add_test(NAME prepared_image_test COMMAND prepared_image_test)
endif()

if(PIKMIN_BUILD_TESTS)
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

    # Host-backed ARAM test (not linked into main game)
    add_executable(pc_aram_test pc_port/audio/pc_aram_test.cpp pc_port/audio/pc_aram.cpp)
    target_compile_options(pc_aram_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_aram_test COMMAND pc_aram_test)

    # Pad axis conversion (no controller needed)
    add_executable(pc_pad_axis_test pc_port/pc_pad_axis_test.cpp pc_port/pc_pad_axis.cpp)
    target_include_directories(pc_pad_axis_test PRIVATE ${SDL2_INCLUDE_DIRS} pc_port)
    target_compile_options(pc_pad_axis_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_pad_axis_test COMMAND pc_pad_axis_test)

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
        pc_port/timing/pc_tick_profiler.cpp)
    target_include_directories(pc_tick_profiler_test PRIVATE pc_port/timing)
    target_compile_options(pc_tick_profiler_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
    add_test(NAME pc_tick_profiler_test COMMAND pc_tick_profiler_test)
endif()

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════╗")
message(STATUS "║   Pikmin PC Port - Build Configuration   ║")
message(STATUS "╠══════════════════════════════════════════╣")
message(STATUS "║   Version:  USA Rev 1 (GPIE01_01)       ║")
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
