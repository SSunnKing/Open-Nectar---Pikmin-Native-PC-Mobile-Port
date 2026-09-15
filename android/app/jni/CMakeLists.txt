# Punto de entrada del build nativo de Android (lo invoca Gradle).
#
# SDL2 se compila en árbol y queda como target `SDL2` antes de añadir el
# CMakeLists.txt de la raíz, que en Android lo espera así en vez de buscarlo
# con find_package. El árbol del juego se añade dos veces, una por versión del
# disco (la decompilación es condicional según la versión, así que son dos
# bibliotecas distintas): libnectar.so (USA Rev 1) y libnectar-pal.so
# (Europa). NectarActivity carga la que pida el disco instalado
# (marcador assets/.pikmin-build). El instalador (libnectar_installer.so)
# solo se define en la primera pasada.
cmake_minimum_required(VERSION 3.16)
project(OpenNectarAndroid LANGUAGES C CXX)

set(NECTAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${NECTAR_ROOT}/third_party/SDL2-android" SDL2)

set(PIKMIN_GLES ON CACHE BOOL "" FORCE)

set(PIKMIN_GAME_VERSION "VERSION_GPIE01_01" CACHE STRING "" FORCE)
set(PIKMIN_TARGET_SUFFIX "" CACHE STRING "" FORCE)
add_subdirectory("${NECTAR_ROOT}" nectar-usa)

set(PIKMIN_GAME_VERSION "VERSION_GPIP01_00" CACHE STRING "" FORCE)
set(PIKMIN_TARGET_SUFFIX "_pal" CACHE STRING "" FORCE)
add_subdirectory("${NECTAR_ROOT}" nectar-pal)
