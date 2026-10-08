# Toolchain de compilación cruzada Linux → Windows x86-64 (MinGW-w64).
#
# Uso:
#   cmake -S . -B build-windows \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-windows -j"$(nproc)"
#
# SDL2 para MinGW se espera en third_party/SDL2-mingw64 (ver README del port).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

# Buscar programas en el host, pero bibliotecas y cabeceras solo en el sysroot
# de destino: evita enlazar por accidente con las .so nativas de Linux.
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# El código heredado conserva 162 ramas #ifdef WIN32 del build original de
# Pikmin para Win32/OpenGL. Ese backend es incompatible con la traducción
# GX → OpenGL de este port: cambia el número de joints (16 vs 12), el cálculo
# de matrices y la carga de texturas. MinGW define WIN32 automáticamente, así
# que hay que retirarlo de forma explícita. _WIN32 SÍ se conserva: sus dos
# únicos usos (endianness en stream.cpp y el windows.h de glext.h) son
# correctos y necesarios en Windows.
add_compile_options(-UWIN32)

# Consola oculta en el juego se decide por target, no aquí.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# SDL2 para MinGW, descargado en third_party (ver README-WINDOWS.md).
set(_sdl2_mingw "${CMAKE_CURRENT_LIST_DIR}/../third_party/SDL2-mingw64")
if (EXISTS "${_sdl2_mingw}")
    list(APPEND CMAKE_PREFIX_PATH "${_sdl2_mingw}")
    list(APPEND CMAKE_FIND_ROOT_PATH "${_sdl2_mingw}")
endif()
