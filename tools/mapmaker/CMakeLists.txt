# Nectar Map Maker: editor de mapas del modo VS (docs/PLAN_EDITOR_MAPAS.md).
# Se compila con el juego (opción NECTAR_BUILD_MAPMAKER de games/pikmin1) o
# solo:
#   cmake -S tools/mapmaker -B build/mapmaker && cmake --build build/mapmaker
cmake_minimum_required(VERSION 3.16)
project(nectar_mapmaker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(NM_P1 "${NM_ROOT}/games/pikmin1")

if(NOT TARGET nectar_mapcore)
    add_subdirectory("${NM_ROOT}/libs/nectar_mapcore" "${CMAKE_CURRENT_BINARY_DIR}/nectar_mapcore")
endif()

find_package(Threads REQUIRED)
if(NOT SDL2_LIBRARIES)
    find_package(SDL2 QUIET)
    if(TARGET SDL2::SDL2)
        set(SDL2_LIBRARIES SDL2::SDL2)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
endif()

# ImGui: la del launcher si se compila con el juego; si no, una propia.
if(TARGET launcher_imgui)
    set(NM_IMGUI launcher_imgui)
else()
    find_package(OpenGL REQUIRED)
    add_library(nm_imgui STATIC
        "${NM_P1}/third_party/imgui/imgui.cpp"
        "${NM_P1}/third_party/imgui/imgui_draw.cpp"
        "${NM_P1}/third_party/imgui/imgui_tables.cpp"
        "${NM_P1}/third_party/imgui/imgui_widgets.cpp"
        "${NM_P1}/third_party/imgui/backends/imgui_impl_sdl2.cpp"
        "${NM_P1}/third_party/imgui/backends/imgui_impl_opengl3.cpp")
    set_property(TARGET nm_imgui PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(nm_imgui PUBLIC "${NM_P1}/third_party/imgui" "${NM_P1}/third_party/imgui/backends" ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nm_imgui PUBLIC ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(nm_imgui PUBLIC opengl32)
    else()
        target_link_libraries(nm_imgui PUBLIC OpenGL::GL)
    endif()
    set(NM_IMGUI nm_imgui)
endif()

# Fuente (la del launcher) y la arena de serie, dentro del ejecutable.
include("${NM_P1}/cmake/EmbedFile.cmake")
set(NM_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${NM_GEN}")
embed_file("${NM_P1}/pc_port/launcher/assets/Fredoka.ttf" "${NM_GEN}/font_fredoka.h" kFontFredoka)
# Canonical path: games/pikmin1 registers the same map without "../", and Ninja rejects a duplicate output.
get_filename_component(NM_DEFAULT_MAP_JSON "${NM_P1}/maps/vs/impact_arena/map.json" ABSOLUTE)
file(READ "${NM_DEFAULT_MAP_JSON}" PC_VS_DEFAULT_MAP)
configure_file("${NM_P1}/pc_port/mods/pc_vs_default_map.h.in" "${NM_GEN}/pc_vs_default_map.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NM_DEFAULT_MAP_JSON}")

if(WIN32)
    set(NM_PLATFORM "${NM_P1}/pc_port/launcher/launcher_platform_win32.cpp")
else()
    set(NM_PLATFORM "${NM_P1}/pc_port/launcher/launcher_platform_posix.cpp")
endif()

add_executable(nectar_mapmaker
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/course_import.cpp
    src/document.cpp
    src/gl.cpp
    src/preview.cpp
    src/renderer.cpp
    src/stb_impl.cpp
    src/templates.cpp
    ${NM_PLATFORM}
    "${NM_GEN}/font_fredoka.h")
set_property(TARGET nectar_mapmaker PROPERTY INCLUDE_DIRECTORIES "")
target_include_directories(nectar_mapmaker PRIVATE
    src
    "${NM_GEN}"
    "${NM_P1}/pc_port/launcher"
    "${NM_P1}/third_party/stb"
    # <GL/gl.h> and <GL/glext.h>: a native Windows build has no Khronos copy, and the
    # port ships the minimal glext.h the renderer needs under games/pikmin1/include.
    "${NM_P1}/include"
    "${NM_P1}/include/gl"
    "${NM_P1}/third_party/nlohmann/include"
    "${NM_P1}/third_party/imgui"
    "${NM_P1}/third_party/imgui/backends"
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(nectar_mapmaker PRIVATE nectar_mapcore ${NM_IMGUI} ${SDL2_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(nectar_mapmaker PRIVATE comdlg32 ole32 shell32 uuid)
    # Deliberately a console executable, like the game itself (see the -mconsole
    # note in games/pikmin1/CMakeLists.txt). The editor reports progress on
    # stdout, and a GUI-subsystem build would need a WinMain entry point this
    # program does not define: it links with "unresolved external symbol WinMain
    # referenced in __scrt_common_main_seh". MinGW's -mwindows masked this.
endif()
if(NOT MSVC)
    target_compile_options(nectar_mapmaker PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
set_target_properties(nectar_mapmaker PROPERTIES
    OUTPUT_NAME "nectar-mapmaker"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS nectar_mapmaker RUNTIME DESTINATION bin)

# On Windows, SDL2 is a DLL: copy it next to the editor so it starts from the
# build tree. Same logic as pikmin_copy_sdl2_runtime() in games/pikmin1, which
# cannot be used here because add_custom_command only accepts a target created in
# the calling directory.
if(WIN32)
    set(_mapmaker_sdl2_dll "")
    if(TARGET SDL2::SDL2)
        get_target_property(_imported SDL2::SDL2 IMPORTED)
        get_target_property(_type SDL2::SDL2 TYPE)
        if(_imported AND _type STREQUAL "SHARED_LIBRARY")
            get_target_property(_loc SDL2::SDL2 IMPORTED_LOCATION)
            if(_loc)
                get_filename_component(_dir "${_loc}" DIRECTORY)
                foreach(_name SDL2.dll SDL2d.dll)
                    if(EXISTS "${_dir}/${_name}")
                        set(_mapmaker_sdl2_dll "${_dir}/${_name}")
                        break()
                    endif()
                endforeach()
            endif()
        endif()
    endif()
    if(_mapmaker_sdl2_dll)
        add_custom_command(TARGET nectar_mapmaker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mapmaker_sdl2_dll}" "$<TARGET_FILE_DIR:nectar_mapmaker>"
            COMMENT "Placing the SDL2 runtime next to nectar-mapmaker"
            VERBATIM)
    endif()
endif()
