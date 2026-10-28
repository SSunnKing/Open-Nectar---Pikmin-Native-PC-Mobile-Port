#!/usr/bin/env bash
# build-macos.sh
#
# Build de desarrollo para macOS con las dos regiones de Pikmin 1 juntas.
# El launcher pide `nectar-pal` junto a sí mismo cuando el disco es europeo;
# una build normal solo deja `nectar` (USA), así que aquí se compila también la
# PAL y se copia como nectar-pal a build/pikmin1/bin.
#
# Requisitos: brew install cmake sdl2-compat (y xcode-select --install).
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
jobs="${NECTAR_BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
usa_dir="${root}/build/pikmin1"
pal_dir="${root}/build/pikmin1-pal"

cmake -S "${root}/games/pikmin1" -B "${usa_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${usa_dir}" --target pikmin_pc pikmin_launcher -j"${jobs}"

cmake -S "${root}/games/pikmin1" -B "${pal_dir}" -DCMAKE_BUILD_TYPE=Release \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${pal_dir}" --target pikmin_pc -j"${jobs}"

cp "${pal_dir}/bin/nectar" "${usa_dir}/bin/nectar-pal"
chmod 755 "${usa_dir}/bin/nectar-pal"

cmake -S "${root}/games/pikmin2" -B "${root}/build/pikmin2" -DCMAKE_BUILD_TYPE=Release
cmake --build "${root}/build/pikmin2" --target pikmin2_pc -j"${jobs}"

printf 'Listo: %s/bin (nectar, nectar-pal, nectar-launcher)\n' "${usa_dir}"
