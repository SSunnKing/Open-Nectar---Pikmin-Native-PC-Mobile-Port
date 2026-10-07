#!/usr/bin/env bash
# Se ejecuta DENTRO del contenedor open-nectar-build (Ubuntu 20.04, glibc 2.31).
# Lo lanza scripts/release-linux.sh con la raíz de Fusion montada en /src; no
# llamarlo a mano.
#
# Deja en build-release/linux/:
#   p1-usa/bin/nectar            Pikmin 1 USA
#   p1-usa/bin/nectar-launcher   launcher
#   p1-pal/bin/nectar            Pikmin 1 PAL
#   p2/pikmin2_pc                Pikmin 2
# con SDL2 y el runtime de C++ dentro (solo dependen de glibc, libGL y libX11).
set -euo pipefail

src=/src
out="${src}/build-release/linux"
version="${OPEN_NECTAR_VERSION:?falta OPEN_NECTAR_VERSION}"
jobs="$(nproc)"

# SDL2 estática (fuentes oficiales: la copia de Android no trae Wayland).
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
    -DPIKMIN_NATIVE_JAUDIO=ON
    -DPIKMIN_STATIC_RUNTIME=ON
    # libGL.so.1 está en cualquier escritorio; libOpenGL.so.0 (GLVND) no siempre.
    -DOpenGL_GL_PREFERENCE=LEGACY
)

printf '%s\n' '[contenedor] Pikmin 1 USA y el launcher...'
cmake -S "${src}/games/pikmin1" -B "${out}/p1-usa" "${common[@]}" \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF -DPIKMIN_ENABLE_IPO=ON -DOPEN_NECTAR_VERSION="${version}"
cmake --build "${out}/p1-usa" --target pikmin_pc pikmin_launcher -j"${jobs}"

printf '%s\n' '[contenedor] Pikmin 1 PAL...'
cmake -S "${src}/games/pikmin1" -B "${out}/p1-pal" "${common[@]}" \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF -DPIKMIN_ENABLE_IPO=ON -DOPEN_NECTAR_VERSION="${version}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${out}/p1-pal" --target pikmin_pc -j"${jobs}"

printf '%s\n' '[contenedor] Pikmin 2...'
cmake -S "${src}/games/pikmin2" -B "${out}/p2" "${common[@]}" -DBUILD_TESTING=OFF
cmake --build "${out}/p2" --target pikmin2_pc -j"${jobs}"
