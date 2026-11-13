# Nectar Model Maker: Olimar propio a partir de un glTF/GLB/VRM, FBX u OBJ. Escribe
# Load/Models/Custom/olimar.nhm, que el juego dibuja en lugar de Olimar
# (pc_port/mods/pc_hd_models.cpp). Se compila con el juego (opción
# NECTAR_BUILD_MODELMAKER de games/pikmin1) o solo:
#   cmake -S tools/modelmaker -B build/modelmaker && cmake --build build/modelmaker
cmake_minimum_required(VERSION 3.16)
project(nectar_modelmaker C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(MM_P1 "${MM_ROOT}/games/pikmin1")
set(MM_MAPMAKER "${MM_ROOT}/tools/mapmaker/src")

if(NOT TARGET nectar_mapcore)
    add_subdirectory("${MM_ROOT}/libs/nectar_mapcore" "${CMAKE_CURRENT_BINARY_DIR}/nectar_mapcore")
endif()
if(NOT TARGET nectar_paths)
    add_subdirectory("${MM_ROOT}/libs/nectar_paths" "${CMAKE_CURRENT_BINARY_DIR}/nectar_paths")
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

# ImGui: la del launcher o la del editor de mapas si ya existen; si no, una propia.
if(TARGET launcher_imgui)
    set(MM_IMGUI launcher_imgui)
elseif(TARGET nm_imgui)
    set(MM_IMGUI nm_imgui)
else()
    find_package(OpenGL REQUIRED)
    add_library(mm_imgui STATIC
        "${MM_P1}/third_party/imgui/imgui.cpp"
        "${MM_P1}/third_party/imgui/imgui_draw.cpp"
        "${MM_P1}/third_party/imgui/imgui_tables.cpp"
        "${MM_P1}/third_party/imgui/imgui_widgets.cpp"
        "${MM_P1}/third_party/imgui/backends/imgui_impl_sdl2.cpp"
        "${MM_P1}/third_party/imgui/backends/imgui_impl_opengl3.cpp")
    set_property(TARGET mm_imgui PROPERTY INCLUDE_DIRECTORIES "")
    target_include_directories(mm_imgui PUBLIC "${MM_P1}/third_party/imgui" "${MM_P1}/third_party/imgui/backends" ${SDL2_INCLUDE_DIRS})
    target_link_libraries(mm_imgui PUBLIC ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(mm_imgui PUBLIC opengl32)
    else()
        target_link_libraries(mm_imgui PUBLIC OpenGL::GL)
    endif()
    set(MM_IMGUI mm_imgui)
endif()

include("${MM_P1}/cmake/EmbedFile.cmake")
set(MM_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MM_GEN}")
embed_file("${MM_P1}/pc_port/launcher/assets/Fredoka.ttf" "${MM_GEN}/font_fredoka.h" kFontFredoka)

if(WIN32)
    set(MM_PLATFORM "${MM_P1}/pc_port/launcher/launcher_platform_win32.cpp")
else()
    set(MM_PLATFORM "${MM_P1}/pc_port/launcher/launcher_platform_posix.cpp")
endif()
# The launcher's shortcut helpers (#336), which its platform layer uses.
list(APPEND MM_PLATFORM "${MM_P1}/pc_port/launcher/game_shortcut.cpp")

add_executable(nectar_modelmaker
    src/main.cpp
    src/app.cpp
    src/olimar.cpp
    src/import_gltf.cpp
    src/import_obj.cpp
    src/import_fbx.cpp
    src/import_nhm.cpp
    # FBX: ufbx (MIT, third_party/ufbx/LICENSE).
    third_party/ufbx/ufbx.c
    src/rig.cpp
    src/export_nhm.cpp
    src/simplify.cpp
    src/view.cpp
    src/stb_impl.cpp
    # Del editor de mapas, tal cual: OpenGL, ajustes (carpeta del juego) y OBJ.
    "${MM_MAPMAKER}/gl.cpp"
    "${MM_MAPMAKER}/config.cpp"
    "${MM_MAPMAKER}/obj_import.cpp"
    "${MM_MAPMAKER}/dae_import.cpp"
    ${MM_PLATFORM}
    "${MM_GEN}/font_fredoka.h")
set_property(TARGET nectar_modelmaker PROPERTY INCLUDE_DIRECTORIES "")
target_include_directories(nectar_modelmaker PRIVATE
    src
    third_party/ufbx
    "${MM_MAPMAKER}"
    "${MM_GEN}"
    "${MM_P1}/pc_port/launcher"
    "${MM_P1}/third_party/stb"
    "${MM_P1}/third_party/nlohmann/include"
    "${MM_P1}/third_party/imgui"
    "${MM_P1}/third_party/imgui/backends"
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(nectar_modelmaker PRIVATE nectar_mapcore nectar_paths ${MM_IMGUI} ${SDL2_LIBRARIES} Threads::Threads)
if(WIN32)
    target_link_libraries(nectar_modelmaker PRIVATE comdlg32 ole32 shell32 uuid winhttp)
    set_target_properties(nectar_modelmaker PROPERTIES WIN32_EXECUTABLE ON)
endif()
if(NOT MSVC)
    target_compile_options(nectar_modelmaker PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    set_source_files_properties(src/stb_impl.cpp third_party/ufbx/ufbx.c PROPERTIES COMPILE_OPTIONS "-w")
endif()
set_target_properties(nectar_modelmaker PROPERTIES
    OUTPUT_NAME "nectar-modelmaker"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
install(TARGETS nectar_modelmaker RUNTIME DESTINATION bin)
