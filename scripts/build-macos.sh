#!/usr/bin/env bash
# build-macos.sh
#
# Build de desarrollo para macOS con las dos regiones de Pikmin 1 juntas.
# El launcher pide `nectar-pal` junto a sí mismo cuando el disco es europeo;
# una build normal solo deja `nectar` (USA), así que aquí se compila también la
# PAL y se copia como nectar-pal a build/pikmin1/bin.
#
# Requisitos: brew install cmake (y xcode-select --install).
# Opcional: brew install libusb para un mando USB Xbox 360 (con reconexión).
# Sin libusb se compila igualmente, con los mandos que SDL reconoce de serie.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
jobs="${NECTAR_BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
sdl_version="${SDL_VERSION:-2.32.10}"
sdl_dir="${root}/build/sdl2-${sdl_version}"
archs="$(uname -m)"
usa_dir="${root}/build/pikmin1"
pal_dir="${root}/build/pikmin1-pal"

# SDL2 de verdad, estática (como en scripts/release-macos.sh). El sdl2-compat de
# Homebrew corre sobre SDL3 y en algunos Mac no entrega ningún mando al juego.
if [[ ! -f "${sdl_dir}/install/lib/libSDL2.a" ]]; then
    mkdir -p "${sdl_dir}"
    if [[ ! -d "${sdl_dir}/src" ]]; then
        mkdir -p "${sdl_dir}/src"
        curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${sdl_version}/SDL2-${sdl_version}.tar.gz" \
            | tar -xz -C "${sdl_dir}/src" --strip-components=1
    fi
    cmake -S "${sdl_dir}/src" -B "${sdl_dir}/build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="${archs}" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="${sdl_dir}/install"
    cmake --build "${sdl_dir}/build" -j"${jobs}"
    cmake --install "${sdl_dir}/build"
fi
# SDL2_DIR puede estar en caché tras seguir el README con Homebrew. Cambiar
# solo CMAKE_PREFIX_PATH no lo sustituye: seleccionar el paquete explícitamente.
sdl_args=(-DCMAKE_PREFIX_PATH="${sdl_dir}/install" -DSDL2_DIR="${sdl_dir}/install/lib/cmake/SDL2" -DPIKMIN_STATIC_RUNTIME=ON)

cmake -S "${root}/games/pikmin1" -B "${usa_dir}" -DCMAKE_BUILD_TYPE=Release -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_XINPUT_USB=ON "${sdl_args[@]}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIE01_01
cmake --build "${usa_dir}" --target pikmin_pc pikmin_launcher -j"${jobs}"

cmake -S "${root}/games/pikmin1" -B "${pal_dir}" -DCMAKE_BUILD_TYPE=Release -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_XINPUT_USB=ON "${sdl_args[@]}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${pal_dir}" --target pikmin_pc -j"${jobs}"

cp "${pal_dir}/bin/nectar" "${usa_dir}/bin/nectar-pal"
chmod 755 "${usa_dir}/bin/nectar-pal"

cmake -S "${root}/games/pikmin2" -B "${root}/build/pikmin2" -DCMAKE_BUILD_TYPE=Release "${sdl_args[@]}" -DPIKMIN_XINPUT_USB=ON \
    -DPIKMIN_GAME_VERSION=VERSION_GPVP01
cmake --build "${root}/build/pikmin2" --target pikmin2_pc -j"${jobs}"

printf 'Listo: %s/bin (nectar, nectar-pal, nectar-launcher)\n' "${usa_dir}"
