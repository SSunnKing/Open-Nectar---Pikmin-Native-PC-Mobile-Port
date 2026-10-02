#!/usr/bin/env bash
# Se ejecuta DENTRO del contenedor open-nectar-build (Ubuntu 20.04, glibc 2.31).
# Lo lanza packaging/linux/build-release.sh; no llamarlo a mano.
#
# Deja en build-linux-release/:
#   usa/bin/nectar            juego USA
#   usa/bin/nectar-launcher   launcher
#   pal/bin/nectar            juego PAL
# con SDL2 y el runtime de C++ dentro (solo dependen de glibc, libGL y libX11).
set -euo pipefail

src=/src
out="${src}/build-linux-release"
version="${OPEN_NECTAR_VERSION:?falta OPEN_NECTAR_VERSION}"
jobs="$(nproc)"

# SDL2 estática. Carga X11, Wayland,
# PulseAudio, PipeWire y ALSA del sistema al arrancar (dlopen), así que el
# ejecutable no queda atado a ninguna de ellas.
# Fuentes oficiales: la copia de third_party/SDL2-android es la de Android y no
# trae los protocolos de Wayland.
sdl_version="${SDL_VERSION:-2.32.10}"
if [[ ! -f "${out}/sdl2/lib/libSDL2.a" ]]; then
    printf '%s\n' "[contenedor] Compilando SDL2 ${sdl_version} estática..."
    mkdir -p "${out}"
    if [[ ! -d "${out}/SDL2-${sdl_version}" ]]; then
        curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${sdl_version}/SDL2-${sdl_version}.tar.gz" \
            | tar -xz -C "${out}"
    fi
    cmake -S "${out}/SDL2-${sdl_version}" -B "${out}/sdl2-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="${out}/sdl2"
    cmake --build "${out}/sdl2-build" -j"${jobs}"
    cmake --install "${out}/sdl2-build"
fi

common=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_PREFIX_PATH="${out}/sdl2"
    -DPIKMIN_NATIVE_OPTIMIZE=OFF
    -DPIKMIN_ENABLE_IPO=ON
    -DPIKMIN_NATIVE_JAUDIO=ON
    -DPIKMIN_STATIC_RUNTIME=ON
    # libGL.so.1 está en cualquier escritorio; libOpenGL.so.0 (GLVND) no siempre.
    -DOpenGL_GL_PREFERENCE=LEGACY
    -DOPEN_NECTAR_VERSION="${version}"
)

printf '%s\n' '[contenedor] Compilando USA y el launcher...'
cmake -S "${src}" -B "${out}/usa" "${common[@]}"
cmake --build "${out}/usa" --target pikmin_pc pikmin_launcher -j"${jobs}"

printf '%s\n' '[contenedor] Compilando PAL...'
cmake -S "${src}" -B "${out}/pal" "${common[@]}" -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${out}/pal" --target pikmin_pc -j"${jobs}"
