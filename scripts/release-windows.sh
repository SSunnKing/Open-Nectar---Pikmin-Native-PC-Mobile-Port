#!/usr/bin/env bash
# release-windows.sh [--clean]
#
# Archivos de Windows de una release de Open Nectar Fusion (ver
# scripts/release/make_release.py), compilados con MinGW desde Linux.
# SDL2 y el runtime de C/C++ van dentro de cada .exe: ninguna DLL propia.
#
# Las versiones salen de versions.json (raíz del repo).
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-release/windows"
out_dir="${root}/release-out/windows"
p1="${root}/games/pikmin1"
p2="${root}/games/pikmin2"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "${build_dir}" "${out_dir}"
fi
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    printf 'Instala g++-mingw-w64-x86-64 (no está x86_64-w64-mingw32-g++).\n' >&2
    exit 1
fi
launcher_version="$(python3 -c "import json;print(json.load(open('${root}/versions.json'))['launcher'])")"
jobs="$(nproc)"

# PIKMIN_NATIVE_OPTIMIZE=OFF: -march=native compilaría para la CPU de esta
# máquina y el .exe moriría sin ventana ni log en CPUs sin esas instrucciones
# (issues #41 y #77). Igual que el release de Linux.
common=(-DCMAKE_BUILD_TYPE=Release -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_STATIC_RUNTIME=ON -DPIKMIN_NATIVE_OPTIMIZE=OFF)

printf '[1/4] Pikmin 1 USA y el launcher...\n'
cmake -S "${p1}" -B "${build_dir}/p1-usa" "${common[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${p1}/cmake/toolchain-mingw64.cmake" -DOPEN_NECTAR_VERSION="${launcher_version}"
cmake --build "${build_dir}/p1-usa" --target pikmin_pc pikmin_launcher -j"${jobs}"

printf '[2/4] Pikmin 1 PAL...\n'
cmake -S "${p1}" -B "${build_dir}/p1-pal" "${common[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${p1}/cmake/toolchain-mingw64.cmake" -DOPEN_NECTAR_VERSION="${launcher_version}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${build_dir}/p1-pal" --target pikmin_pc -j"${jobs}"

printf '[3/4] Pikmin 2...\n'
cmake -S "${p2}" -B "${build_dir}/p2" "${common[@]}" -DBUILD_TESTING=OFF \
    -DCMAKE_TOOLCHAIN_FILE="${p2}/cmake/toolchain-mingw64.cmake"
cmake --build "${build_dir}/p2" --target pikmin2_pc -j"${jobs}"

launcher="${build_dir}/p1-usa/bin/nectar-launcher.exe"
usa="${build_dir}/p1-usa/bin/nectar.exe"
pal="${build_dir}/p1-pal/bin/nectar.exe"
p2exe="${build_dir}/p2/pikmin2_pc.exe"

printf '[4/4] Montando los archivos de la release...\n'
stripped="${build_dir}/stripped"
mkdir -p "${stripped}"
cp "${launcher}" "${stripped}/nectar-launcher.exe"
cp "${usa}" "${stripped}/nectar.exe"
cp "${pal}" "${stripped}/nectar-pal.exe"
cp "${p2exe}" "${stripped}/pikmin2_pc.exe"
x86_64-w64-mingw32-strip "${stripped}"/*.exe

# Nada de DLL propias: cada .exe debe pedir solo DLL del sistema.
system_dlls='^(advapi32|comdlg32|gdi32|imm32|kernel32|msvcrt|ole32|oleaut32|opengl32|setupapi|shell32|user32|version|winmm|shlwapi|uuid|dwmapi|dinput8|xinput1_4|hid|cfgmgr32|ws2_32|bcrypt|dbghelp)\.dll$'
for exe in "${stripped}"/*.exe; do
    while read -r dll; do
        if ! printf '%s\n' "${dll,,}" | grep -Eq "${system_dlls}"; then
            printf '%s necesita %s, que no viene con Windows. Revisa PIKMIN_STATIC_RUNTIME.\n' "${exe}" "${dll}" >&2
            exit 1
        fi
    done < <(x86_64-w64-mingw32-objdump -p "${exe}" | sed -n 's/.*DLL Name: //p')
done

python3 "${root}/scripts/release/make_release.py" --os windows --out "${out_dir}" \
    --launcher "${stripped}/nectar-launcher.exe" --p1-usa "${stripped}/nectar.exe" \
    --p1-pal "${stripped}/nectar-pal.exe" --p2 "${stripped}/pikmin2_pc.exe" \
    --extra "${p1}/packaging/windows/README.txt"
