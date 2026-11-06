# nectar_assets: lectores de los formatos de Pikmin 1 (.mod, .bti, .ini de
# escena, .gen) y catálogo con miniaturas para el editor de mapas.
# No depende del juego. Se puede compilar sola:
#   cmake -S libs/nectar_assets -B build/nectar_assets && cmake --build build/nectar_assets
# o añadirla con add_subdirectory desde otro proyecto.
cmake_minimum_required(VERSION 3.16)
project(nectar_assets CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NECTAR_P1_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../games/pikmin1")

add_library(nectar_assets STATIC
    src/image.cpp
    src/mod.cpp
    src/ini.cpp
    src/gen.cpp
    src/raster.cpp
    src/catalog.cpp
    src/mesh_mod.cpp
    src/charpack.cpp
    # Escritores de los ficheros de relleno de los tests de escenarios.
    src/parms.cpp
    src/archive.cpp
    src/stages_ini.cpp
    src/mod_writer.cpp
    src/standin_models.cpp
    src/standin_art.cpp
    src/standin_art_meshes.cpp
    src/standin_terrain.cpp
    src/blo.cpp
    src/pcr.cpp
    src/standin_ui.cpp
    src/standin_pcr_names.cpp
    src/texture.cpp
    src/bundle.cpp
    src/standin_textures.cpp
    src/anim.cpp
    src/standin_anims.cpp
    src/cinema.cpp
    src/standin_cinema.cpp
    src/audio_seq.cpp
    src/standin_audio.cpp
    # The scenario sandbox's gate (#68): item generator, platform .mod.
    src/gen_item_writer.cpp
    src/mod_platform_writer.cpp
    src/standin_sandbox.cpp
    src/standin_objects.cpp
    src/standin_object_profiles.cpp
    src/standin_world.cpp
    src/standin_world_data.cpp
    # Decodificador de texturas del launcher, reutilizado tal cual.
    ${NECTAR_P1_DIR}/pc_port/launcher/bti_decode.cpp
)
# Sin las rutas globales de quien la incluya (el juego trae su propio float.h).
set_property(TARGET nectar_assets PROPERTY INCLUDE_DIRECTORIES "")
target_include_directories(nectar_assets
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            ${NECTAR_P1_DIR}/pc_port/launcher
            ${NECTAR_P1_DIR}/third_party/nlohmann/include
            ${NECTAR_P1_DIR}/third_party/stb
)
if(NOT MSVC)
    target_compile_options(nectar_assets PRIVATE -Wall -Wextra)
endif()

# Herramientas y test, solo si se compila esta librería sola.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(nectar_catalog tools/nectar_catalog.cpp)
    target_link_libraries(nectar_catalog PRIVATE nectar_assets)

    add_executable(nectar_assets_test tests/nectar_assets_test.cpp)
    target_link_libraries(nectar_assets_test PRIVATE nectar_assets)
endif()
