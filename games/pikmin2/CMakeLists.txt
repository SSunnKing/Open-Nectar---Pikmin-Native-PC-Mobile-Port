###############################################################################
#  Pikmin 2 PC Port — CMake Build System (Fase 2: entry point nativo)
#
#  Compila la decompilacion de Pikmin 2 (projectPiki/pikmin2, submódulo
#  pikmin2-decomp) nativamente para Linux x86_64, Android y Windows,
#  reemplazando las llamadas al Dolphin SDK por los stubs del port (pc_port).
#
#  Diferencias con el port de Pikmin 1 (Open Nectar):
#    - Pikmin 2 usa JSystem (J3D/J2D/JKernel/...) sobre GX (J3D → GD → GX)
#    - El código del juego está en src/plugProject*U/ (no plugPiki*)
#    - include/types.h usa `typedef long s32`; en el port se fuerza el bloque
#      PIKI_PC_PORT (patch local en el submodulo) con tipos de ancho fijo.
#    - include/Version.h exige exactamente una macro VERSION_<gameID>.
#      Se compila VERSION_GPVP01 (PAL) por defecto.
#
#  Arquitectura (igual que Pikmin 1):
#    pikmin2_legacy   - biblioteca estática: JSystem + código del juego.
#                       Compilada con flags permisivos (Metrowerks-era).
#    pikmin2_pc       - ejecutable de escritorio / .so en Android: la capa
#                       de port (pc_port), conduce los stubs del SDK.
#    pikmin2_decomp_adapter - headers de adaptación forzados (-include).
###############################################################################
cmake_minimum_required(VERSION 3.16)
project(Pikmin2PC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# ─── Build type ───
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
endif()

# ─── Rutas ───
set(PIKMIN2_DECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pikmin2-decomp")
set(PC_PORT_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/pc_port")
set(ADAPTER_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/pikmin2-decomp-adapter")

if(NOT EXISTS "${PIKMIN2_DECOMP_DIR}/src/plugProjectKandoU")
    message(FATAL_ERROR "Submodulo pikmin2-decomp no encontrado. Ejecuta: git submodule update --init")
endif()

# ─── Opciones ───
# Fase 2: ejecutable pikmin2_pc (pc_main → System/GameFlow). Los stubs Dolphin
# (GX/OS/DVD/PAD) son Fase 3; el enlace puede fallar hasta entonces.
option(PIKMIN2_ENABLE_PORT_LAYER "Compile the pc_port native layer (main + host)" ON)
option(PIKMIN_NATIVE_JAUDIO "Drive audio with the original engine (WIP)" OFF)
option(PIKMIN2_ASAN "Build with AddressSanitizer: catches double free / use-after-free at the instant it happens" OFF)
option(PIKMIN2_TSAN "Build with ThreadSanitizer: data races and lock-order inversions between game, audio and DVD threads" OFF)
include(CTest)

if(ANDROID)
    set(PIKMIN_GLES_DEFAULT ON)
else()
    set(PIKMIN_GLES_DEFAULT OFF)
endif()
option(PIKMIN_GLES "Build with OpenGL ES 3.0 renderer" ${PIKMIN_GLES_DEFAULT})

# ─── Flags de compilación: código decompilado ───
# Fuerza nuestro header de adaptación ANTES de cualquier include del juego.
set(CLANG_LEGACY_RELAXATIONS
    $<$<CXX_COMPILER_ID:Clang>:-Wno-register>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-address-of-temporary>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unknown-warning-option>
)
set(LEGACY_COMPILE_OPTIONS
    -include "${ADAPTER_DIR}/pikmin2_pc_types.h"
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
    # A non-void function without return is UB: GCC omits the epilogue and
    # control falls into the next symbol (doDrawRuby crash). Never allow it.
    -Werror=return-type
)

set(NATIVE_COMPILE_OPTIONS
    -include "${ADAPTER_DIR}/pikmin2_pc_types.h"
    -fsigned-char
    -fpermissive
    ${CLANG_LEGACY_RELAXATIONS}
    -Wall
    -Wextra
    -Wno-narrowing
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-parentheses
    -Wno-delete-non-virtual-dtor
    -Wno-reorder
    -Wno-switch
    -Wno-attributes
    -Wno-comment
    -Werror=return-type
)

# ─── Definiciones de compilacion ───
add_compile_definitions(
    PIKI_PC_PORT=1
    # Capa de host de Pikmin 2: excluye lo exclusivo del port de Pikmin 1
    # (audio pc_audio) en el codigo compartido de pc_port.
    PIKI_P2_HOST=1
    PIKI_PC_SETTINGS_MENU=1
    # J3D hornea materiales y shapes como registros BP/CP/XF (GD + FIFO). Con
    # esta macro los escritores del decomp pliegan punteros LP64 a direcciones
    # físicas y escriben big-endian; pc_gfx los decodifica (ver pc_gfx.cpp).
    PIKI_PC_GXREGS=1
    DTK_CONFIG_NONMATCHING=1
    VERSION_GPVP01
    PIKI_USE_DGX=1
    PIKI_USE_GLES=$<BOOL:${PIKMIN_GLES}>
    $<$<NOT:$<BOOL:${ANDROID}>>:SDL_MAIN_HANDLED=1>
)

# ─── Include paths ───
# El mini-STL del decomp (include/stl/) y sus cabeceras MSL
# (include/PowerPC_EABI_Support/) se SUSTITUYEN por los shims del adapter,
# que reenvian al STL/libc del sistema. Los shims viven bajo ${ADAPTER_DIR}
# (stl/ y PowerPC_EABI_Support/), por lo que las inclusiones explicitas
# "stl/xxx.h" y "PowerPC_EABI_Support/..." los encuentran primero. Las
# cabeceras genéricas del decomp (Game/, JSystem/, Dolphin/, ...) se listan
# despues; el include/stl original nunca se alcanza porque las rutas con
# prefijo ya estan resueltas por los shims, y las inclusiones desnudas
# (<utility>, <algorithm>...) caen al STL real del sistema.
include_directories(
    ${ADAPTER_DIR}
    ${PIKMIN2_DECOMP_DIR}/include
    ${PIKMIN2_DECOMP_DIR}/include/Dolphin
    ${PIKMIN2_DECOMP_DIR}/include/Dolphin/GX
    ${PIKMIN2_DECOMP_DIR}/include/Dolphin/OS
    ${PIKMIN2_DECOMP_DIR}/include/Dolphin/GC
    ${PIKMIN2_DECOMP_DIR}/include/Game
)

###############################################################################
#  Fuentes
###############################################################################

# ─── JSystem (compila nativamente; 23 librerías del decomp) ───
file(GLOB J3D_SOURCES            "${PIKMIN2_DECOMP_DIR}/src/JSystem/J3D/*.cpp")
file(GLOB J3DU_SOURCES           "${PIKMIN2_DECOMP_DIR}/src/JSystem/J3DU/*.cpp")
file(GLOB J2D_SOURCES            "${PIKMIN2_DECOMP_DIR}/src/JSystem/J2D/*.cpp")
file(GLOB JFRAMEWORK_SOURCES     "${PIKMIN2_DECOMP_DIR}/src/JSystem/JFramework/*.cpp")
file(GLOB JKERNEL_SOURCES        "${PIKMIN2_DECOMP_DIR}/src/JSystem/JKernel/*.cpp")
file(GLOB JMATH_SOURCES          "${PIKMIN2_DECOMP_DIR}/src/JSystem/JMath/*.cpp")
file(GLOB JPARTICLE_SOURCES      "${PIKMIN2_DECOMP_DIR}/src/JSystem/JParticle/*.cpp")
file(GLOB JSTAGE_SOURCES         "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStage/*.cpp")
file(GLOB JSTUDIO_SOURCES        "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStudio/*.cpp")
file(GLOB JSUPPORT_SOURCES       "${PIKMIN2_DECOMP_DIR}/src/JSystem/JSupport/*.cpp")
file(GLOB JUTILITY_SOURCES       "${PIKMIN2_DECOMP_DIR}/src/JSystem/JUtility/*.cpp")
file(GLOB JGADGET_SOURCES        "${PIKMIN2_DECOMP_DIR}/src/JSystem/JGadget/*.cpp")
file(GLOB JMESSAGE_SOURCES       "${PIKMIN2_DECOMP_DIR}/src/JSystem/JMessage/*.cpp")
file(GLOB JSTUDIO_JAUDIO_SOURCES "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStudio_JAudio/*.cpp")
file(GLOB JSTUDIO_JMESSAGE_SOURCES "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStudio_JMessage/*.cpp")
file(GLOB JSTUDIO_JPARTICLE_SOURCES "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStudio_JParticle/*.cpp")
file(GLOB JSTUDIO_JSTAGE_SOURCES "${PIKMIN2_DECOMP_DIR}/src/JSystem/JStudio_JStage/*.cpp")

# JAudio (motor de audio; compila, la salida llega en fases posteriores)
foreach(jadir JAD JAI JAL JAS JAU dsp)
    file(GLOB JAUDIO_${jadir}_SOURCES "${PIKMIN2_DECOMP_DIR}/src/JSystem/JAudio/${jadir}/*.cpp")
endforeach()

# ─── Código del juego (src/plugProject*U/) ───
file(GLOB GAME_KAISO_SOURCES   "${PIKMIN2_DECOMP_DIR}/src/plugProjectKandoU/*.cpp")
file(GLOB GAME_NISHIMURA_SOURCES "${PIKMIN2_DECOMP_DIR}/src/plugProjectNishimuraU/*.cpp")
file(GLOB GAME_OGAWA_SOURCES   "${PIKMIN2_DECOMP_DIR}/src/plugProjectOgawaU/*.cpp")
file(GLOB GAME_YAMASHITA_SOURCES "${PIKMIN2_DECOMP_DIR}/src/plugProjectYamashitaU/*.cpp")
file(GLOB GAME_HIKINO_SOURCES  "${PIKMIN2_DECOMP_DIR}/src/plugProjectHikinoU/*.cpp")
file(GLOB GAME_MORIMURA_SOURCES "${PIKMIN2_DECOMP_DIR}/src/plugProjectMorimuraU/*.cpp")
file(GLOB GAME_EBISAWA_SOURCES "${PIKMIN2_DECOMP_DIR}/src/plugProjectEbisawaU/*.cpp")
file(GLOB GAME_KONO_SOURCES    "${PIKMIN2_DECOMP_DIR}/src/plugProjectKonoU/*.cpp")

# ─── Módulos de sistema ───
file(GLOB SYSGCU_SOURCES     "${PIKMIN2_DECOMP_DIR}/src/sysGCU/*.cpp")
file(GLOB SYSCOMMON_SOURCES  "${PIKMIN2_DECOMP_DIR}/src/sysCommonU/*.cpp")
file(GLOB SYSTEMBOOT_SOURCES "${PIKMIN2_DECOMP_DIR}/src/sysBootupU/*.cpp")
file(GLOB UTILITY_SOURCES    "${PIKMIN2_DECOMP_DIR}/src/utilityU/*.cpp")

# ─── GD (constructor de display lists, usado por J3D; puro C, compila) ───
file(GLOB GD_SOURCES "${PIKMIN2_DECOMP_DIR}/src/Dolphin/gd/*.c")

# ─── THP (decodificador de video); compila, el audio DTHP es stubeado ───
file(GLOB THP_SOURCES "${PIKMIN2_DECOMP_DIR}/src/Dolphin/thp/*.c")

###############################################################################
#  pikmin2_legacy — biblioteca estática del juego
###############################################################################
set(JSYSTEM_SOURCES
    ${J3D_SOURCES}
    ${J3DU_SOURCES}
    ${J2D_SOURCES}
    ${JFRAMEWORK_SOURCES}
    ${JKERNEL_SOURCES}
    ${JMATH_SOURCES}
    ${JPARTICLE_SOURCES}
    ${JSTAGE_SOURCES}
    ${JSTUDIO_SOURCES}
    ${JSUPPORT_SOURCES}
    ${JUTILITY_SOURCES}
    ${JGADGET_SOURCES}
    ${JMESSAGE_SOURCES}
    ${JSTUDIO_JAUDIO_SOURCES}
    ${JSTUDIO_JMESSAGE_SOURCES}
    ${JSTUDIO_JPARTICLE_SOURCES}
    ${JSTUDIO_JSTAGE_SOURCES}
    ${JAUDIO_JAD_SOURCES}
    ${JAUDIO_JAI_SOURCES}
    ${JAUDIO_JAL_SOURCES}
    ${JAUDIO_JAS_SOURCES}
    ${JAUDIO_JAU_SOURCES}
    ${JAUDIO_dsp_SOURCES}
)

set(GAME_SOURCES
    ${GAME_KAISO_SOURCES}
    ${GAME_NISHIMURA_SOURCES}
    ${GAME_OGAWA_SOURCES}
    ${GAME_YAMASHITA_SOURCES}
    ${GAME_HIKINO_SOURCES}
    ${GAME_MORIMURA_SOURCES}
    ${GAME_EBISAWA_SOURCES}
    ${GAME_KONO_SOURCES}
)

set(SYSTEM_SOURCES
    ${SYSGCU_SOURCES}
    ${SYSCOMMON_SOURCES}
    ${SYSTEMBOOT_SOURCES}
    ${UTILITY_SOURCES}
)

# sysBootupU/sysBootup.cpp defines GameCube main(); the host owns main() in pc_main.cpp.
list(FILTER SYSTEM_SOURCES EXCLUDE REGEX "sysBootupU/sysBootup\\.cpp$")

add_library(pikmin2_legacy STATIC
    ${JSYSTEM_SOURCES}
    ${GAME_SOURCES}
    ${SYSTEM_SOURCES}
    ${GD_SOURCES}
    ${THP_SOURCES}
)
target_compile_options(pikmin2_legacy PRIVATE ${LEGACY_COMPILE_OPTIONS})
target_include_directories(pikmin2_legacy PRIVATE ${PC_PORT_DIR})

message(STATUS "Pikmin 2 legacy sources:")
message(STATUS "  JSystem : ${JSYSTEM_SOURCES}")
message(STATUS "  Game    : ${GAME_SOURCES}")
message(STATUS "  System  : ${SYSTEM_SOURCES}")
message(STATUS "  GD+THP  : ${GD_SOURCES} ${THP_SOURCES}")

###############################################################################
#  pikmin2_pc — entry nativo + stubs Dolphin + renderer GX→GL (Fase 4)
###############################################################################
if(PIKMIN2_ENABLE_PORT_LAYER)
    set(PC_PORT_SOURCES
        ${PC_PORT_DIR}/pc_main.cpp
        ${PC_PORT_DIR}/pc_gpu_preference.cpp
        ${PC_PORT_DIR}/pc_window.cpp
        ${PC_PORT_DIR}/pc_discord.cpp
        ${PC_PORT_DIR}/pc_gyro.cpp
        ${PC_PORT_DIR}/pc_icon.cpp
        ${PC_PORT_DIR}/pc_file_dialog.cpp
        ${PC_PORT_DIR}/mods/pc_hd_model_convert.cpp
        ${PC_PORT_DIR}/pc_pad_axis.cpp
        ${PC_PORT_DIR}/pc_menu_repeat.cpp
        ${PC_PORT_DIR}/settings/pc_settings.cpp
        ${PC_PORT_DIR}/settings/pc_settings_p2_shim.cpp
        ${PC_PORT_DIR}/pc_achievements.cpp
        ${PC_PORT_DIR}/pc_p2_cheats.cpp
        ${PC_PORT_DIR}/dolphin_stubs/p2_gx_stubs.cpp
        ${PC_PORT_DIR}/dolphin_stubs/p2_os_host.cpp
        ${PC_PORT_DIR}/dolphin_stubs/pc_thp.cpp
        ${PC_PORT_DIR}/dolphin_stubs/card_stubs.cpp
        ${PC_PORT_DIR}/dolphin_stubs/p2_cpp_link.cpp
        ${PC_PORT_DIR}/audio/pc_aram.cpp
        ${PC_PORT_DIR}/audio/pc_dsp_host.cpp
        ${PC_PORT_DIR}/audio/p2_audio_host.cpp
        ${PC_PORT_DIR}/audio/jaudio_sink.cpp
        ${PC_PORT_DIR}/audio/pc_ast_stream.cpp
        ${PC_PORT_DIR}/gl/pc_gfx.cpp
        ${PC_PORT_DIR}/gl/pc_tev_shader.cpp
        ${PC_PORT_DIR}/gl/pc_postprocess.cpp
        ${PC_PORT_DIR}/gl/pc_texpack.cpp
        ${PC_PORT_DIR}/timing/pc_render_packet.cpp
        ${PC_PORT_DIR}/timing/pc_render_phase.cpp
        ${PC_PORT_DIR}/timing/pc_tick_profiler.cpp
    )

    find_package(Threads REQUIRED)
    find_package(SDL2 QUIET)
    if(TARGET SDL2::SDL2)
        set(P2_SDL_LIBS SDL2::SDL2)
        set(P2_SDL_INCLUDES "")
    elseif(SDL2_FOUND)
        set(P2_SDL_LIBS ${SDL2_LIBRARIES})
        set(P2_SDL_INCLUDES ${SDL2_INCLUDE_DIRS})
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
        set(P2_SDL_LIBS ${SDL2_LIBRARIES})
        set(P2_SDL_INCLUDES ${SDL2_INCLUDE_DIRS})
    endif()

    find_package(OpenGL REQUIRED)

    add_executable(pikmin2_pc ${PC_PORT_SOURCES})
    target_compile_options(pikmin2_pc PRIVATE ${NATIVE_COMPILE_OPTIONS} -Wno-unused-parameter)
    # GX-exact spot/distance light attenuation in the shader (see doLights).
    target_compile_definitions(pikmin2_pc PRIVATE PIKMIN2_GX_LIGHT_ATTN=1)
    target_include_directories(pikmin2_pc PRIVATE
        ${PC_PORT_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PC_PORT_DIR}/gl
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcdec
        ${P2_SDL_INCLUDES}
    )
    target_link_libraries(pikmin2_pc PRIVATE pikmin2_legacy m Threads::Threads ${P2_SDL_LIBS} OpenGL::GL)
    # -rdynamic + libdl: readable symbol names in the crash backtrace (pc_main.cpp).
    if(NOT WIN32)
        target_link_options(pikmin2_pc PRIVATE -rdynamic)
        target_link_libraries(pikmin2_pc PRIVATE ${CMAKE_DL_LIBS})
    endif()
    # pc_window.cpp usa Xlib en Linux de escritorio (como el port de Pikmin 1).
    if(UNIX AND NOT APPLE AND NOT ANDROID)
        target_link_libraries(pikmin2_pc PRIVATE X11)
    endif()

    # Source-only regression tests. Keep these as small executables so failures
    # identify the host subsystem involved and never require a window or game
    # assets. The JAudio integration check is routed through pikmin2_pc below;
    # it reports CTest's standard skip code until its native backend is wired.
    if(BUILD_TESTING AND NOT ANDROID)
        set(P2_HOST_TEST_TARGETS)
        function(pikmin2_add_host_test target)
            add_executable(${target} ${ARGN})
            target_compile_options(${target} PRIVATE ${NATIVE_COMPILE_OPTIONS} -Wno-unused-parameter)
            target_include_directories(${target} PRIVATE
                ${PC_PORT_DIR}
                ${PC_PORT_DIR}/audio
                ${PC_PORT_DIR}/gl
                ${PC_PORT_DIR}/save
                ${PC_PORT_DIR}/timing
                ${P2_SDL_INCLUDES}
            )
            target_link_libraries(${target} PRIVATE Threads::Threads ${P2_SDL_LIBS})
            add_test(NAME ${target} COMMAND ${target})
            set_tests_properties(${target} PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            )
            set(P2_HOST_TEST_TARGETS ${P2_HOST_TEST_TARGETS} ${target} PARENT_SCOPE)
        endfunction()

        pikmin2_add_host_test(p2_aram_test
            ${PC_PORT_DIR}/audio/pc_aram_test.cpp
            ${PC_PORT_DIR}/audio/pc_aram.cpp
        )
        pikmin2_add_host_test(p2_envelope_test
            ${PC_PORT_DIR}/audio/pc_envelope_test.cpp
            ${PC_PORT_DIR}/audio/pc_envelope.cpp
        )
        pikmin2_add_host_test(p2_postprocess_test
            ${PC_PORT_DIR}/gl/pc_postprocess_test.cpp
            ${PC_PORT_DIR}/gl/pc_postprocess.cpp
        )
        pikmin2_add_host_test(p2_tev_shader_test
            ${PC_PORT_DIR}/gl/pc_tev_shader_test.cpp
            ${PC_PORT_DIR}/gl/pc_tev_shader.cpp
        )
        pikmin2_add_host_test(p2_menu_repeat_test
            ${PC_PORT_DIR}/pc_menu_repeat_test.cpp
            ${PC_PORT_DIR}/pc_menu_repeat.cpp
        )
        pikmin2_add_host_test(p2_pad_axis_test
            ${PC_PORT_DIR}/pc_pad_axis_test.cpp
            ${PC_PORT_DIR}/pc_pad_axis.cpp
        )
        pikmin2_add_host_test(p2_photo_mode_test
            ${PC_PORT_DIR}/pc_photo_mode_test.cpp
            ${PC_PORT_DIR}/pc_photo_mode.cpp
        )
        pikmin2_add_host_test(p2_generator_cache_validation_test
            ${PC_PORT_DIR}/save/pc_generator_cache_validation_test.cpp
        )
        pikmin2_add_host_test(p2_camera_snapshot_test
            ${PC_PORT_DIR}/timing/pc_camera_snapshot_test.cpp
            ${PC_PORT_DIR}/timing/pc_camera_snapshot.cpp
        )
        pikmin2_add_host_test(p2_frame_scheduler_test
            ${PC_PORT_DIR}/timing/pc_frame_scheduler_test.cpp
            ${PC_PORT_DIR}/timing/pc_frame_scheduler.cpp
        )
        pikmin2_add_host_test(p2_render_packet_test
            ${PC_PORT_DIR}/timing/pc_render_packet_test.cpp
            ${PC_PORT_DIR}/timing/pc_render_packet.cpp
        )
        pikmin2_add_host_test(p2_render_phase_test
            ${PC_PORT_DIR}/timing/pc_render_phase_test.cpp
            ${PC_PORT_DIR}/timing/pc_render_phase.cpp
        )
        pikmin2_add_host_test(p2_tick_profiler_test
            ${PC_PORT_DIR}/timing/pc_tick_profiler_test.cpp
            ${PC_PORT_DIR}/timing/pc_tick_profiler.cpp
        )
        pikmin2_add_host_test(p2_visual_snapshot_test
            ${PC_PORT_DIR}/timing/pc_visual_snapshot_test.cpp
            ${PC_PORT_DIR}/timing/pc_visual_snapshot.cpp
        )
        pikmin2_add_host_test(p2_visual_runtime_test
            ${PC_PORT_DIR}/timing/pc_visual_runtime_test.cpp
            ${PC_PORT_DIR}/timing/pc_visual_runtime.cpp
            ${PC_PORT_DIR}/timing/pc_visual_snapshot.cpp
            ${PC_PORT_DIR}/timing/pc_render_phase.cpp
        )

        add_test(NAME p2_jaudio_integration_test COMMAND pikmin2_pc --audio-self-test)
        set_tests_properties(p2_jaudio_integration_test PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            SKIP_RETURN_CODE 77
            TIMEOUT 60
        )
    endif()
    message(STATUS "Port layer: ENABLED (Fase 4 — GX fifo → pc_gfx / OpenGL)")
else()
    message(STATUS "Port layer: DISABLED (origa PIKMIN2_ENABLE_PORT_LAYER=ON)")
endif()

###############################################################################
#  PIKMIN2_ASAN — diagnostic build
#
#  The GC code relies on JKR heaps, which tolerate patterns glibc's malloc does
#  not (double delete, destructor run twice, free of a stale pointer). Those
#  corrupt the heap silently and surface hundreds of frames later somewhere
#  unrelated. ASan aborts at the offending free instead, with both stacks.
#
#    cmake -S . -B build-asan -DPIKMIN2_ASAN=ON && cmake --build build-asan -j
#    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 ./build-asan/pikmin2_pc
#
#  Roughly 2x slower: a debugging build, not one to play on.
###############################################################################
if(PIKMIN2_ASAN)
    set(P2_ASAN_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
    foreach(tgt pikmin2_legacy pikmin2_pc ${P2_HOST_TEST_TARGETS})
        if(TARGET ${tgt})
            target_compile_options(${tgt} PRIVATE ${P2_ASAN_FLAGS})
            target_link_options(${tgt} PRIVATE -fsanitize=address)
        endif()
    endforeach()
    message(STATUS "AddressSanitizer: ENABLED (diagnostic build, ~2x slower)")
endif()

###############################################################################
#  PIKMIN2_TSAN — race detector for main / JASAudioThread / DVD threads
#
#    cmake -S . -B build-tsan -DPIKMIN2_TSAN=ON && cmake --build build-tsan -j
#    TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1 ./build-tsan/pikmin2_pc
#
#  Cannot be combined with PIKMIN2_ASAN. Expect reports on GX/ARAM buffers the
#  GameCube also shared without locks (DMA semantics); triage by call stack.
###############################################################################
if(PIKMIN2_TSAN)
    if(PIKMIN2_ASAN)
        message(FATAL_ERROR "PIKMIN2_TSAN and PIKMIN2_ASAN are mutually exclusive")
    endif()
    set(P2_TSAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g)
    foreach(tgt pikmin2_legacy pikmin2_pc ${P2_HOST_TEST_TARGETS})
        if(TARGET ${tgt})
            target_compile_options(${tgt} PRIVATE ${P2_TSAN_FLAGS})
            target_link_options(${tgt} PRIVATE -fsanitize=thread)
        endif()
    endforeach()
    message(STATUS "ThreadSanitizer: ENABLED (diagnostic build)")
endif()
