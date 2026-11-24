# nectar_mapcore: mapas VS (map.json) y su compilador a .mod/.ini.
# Lo usan el juego (pc_vs_arena) y el editor. Depende de nectar_assets
# (para leer los .mod de los objetos).
#   cmake -S libs/nectar_mapcore -B build/nectar_mapcore && cmake --build build/nectar_mapcore
cmake_minimum_required(VERSION 3.16)
project(nectar_mapcore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET nectar_assets)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../nectar_assets ${CMAKE_CURRENT_BINARY_DIR}/nectar_assets)
endif()

add_library(nectar_mapcore STATIC
    src/map_json.cpp
    src/i18n.cpp
    src/compile.cpp
    src/campaign.cpp
    src/pack.cpp
)
# Sin las rutas globales de quien la incluya (el juego trae su propio float.h).
set_property(TARGET nectar_mapcore PROPERTY INCLUDE_DIRECTORIES "")
target_include_directories(nectar_mapcore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../games/pikmin1/third_party/nlohmann/include
)
target_link_libraries(nectar_mapcore PUBLIC nectar_assets)
if(NOT MSVC)
    target_compile_options(nectar_mapcore PRIVATE -Wall -Wextra)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(nectar_compile_map tools/nectar_compile_map.cpp)
    target_link_libraries(nectar_compile_map PRIVATE nectar_mapcore)
endif()
# Test sin assets del disco: bloques de terreno en el map.json (#376).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR (PIKMIN_BUILD_TESTS AND NOT ANDROID))
    add_executable(nectar_mapcore_blocks_test tests/map_blocks_test.cpp)
    target_link_libraries(nectar_mapcore_blocks_test PRIVATE nectar_mapcore)
    add_test(NAME nectar_mapcore_blocks_test COMMAND nectar_mapcore_blocks_test)
endif()
