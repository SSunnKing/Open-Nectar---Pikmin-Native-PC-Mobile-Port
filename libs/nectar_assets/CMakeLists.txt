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
elseif(PIKMIN_BUILD_TESTS AND NOT ANDROID)
    # Con el juego: solo la parte que no necesita los assets del disco (#326).
    add_executable(nectar_assets_test tests/nectar_assets_test.cpp)
    target_link_libraries(nectar_assets_test PRIVATE nectar_assets)
    add_test(NAME nectar_assets_catalog_utf8_test COMMAND nectar_assets_test --catalog-utf8)
endif()
