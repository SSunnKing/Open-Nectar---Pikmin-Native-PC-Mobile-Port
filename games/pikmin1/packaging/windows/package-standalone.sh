#!/usr/bin/env bash
# package-standalone.sh [--clean] [--build-dir DIR] [--pal-build-dir DIR]
#
# Compila las dos versiones (USA y PAL) para Windows x86-64 con MinGW-w64 y
# deja un directorio listo para zippear:
#
#   packaging/windows/out/nectar-windows/
#     nectar.exe
#     nectar-pal.exe
#     nectar-launcher.exe
#     README.txt
#
# y lo comprime en packaging/windows/out/nectar-windows.zip, el nombre que
# busca el auto-update del launcher en el release de GitHub.
#
# SDL2 y el runtime de C/C++ van dentro de cada .exe (PIKMIN_STATIC_RUNTIME):
# no hay DLL que acompañar. El instalador copia nectar.exe o nectar-pal.exe a
# nectar.exe según el disco, así que la carpeta instalada queda en dos
# archivos: nectar.exe y nectar-launcher.exe.
# Sin nectar-pal.exe un ISO europeo "instala bien" y el juego no arranca.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build-windows-standalone"
pal_build_dir="${repo_root}/build-windows-standalone-pal"
output_dir="${script_dir}/out/nectar-windows"
toolchain="${repo_root}/cmake/toolchain-mingw64.cmake"
sdl2_root="${repo_root}/third_party/SDL2-mingw64"

clean=0
while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --build-dir) build_dir="$2"; shift ;;
        --pal-build-dir) pal_build_dir="$2"; shift ;;
        --help|-h)
            printf 'Uso: %s [--clean] [--build-dir DIR] [--pal-build-dir DIR]\n' "$0"
            exit 0
            ;;
        *) printf 'Opción desconocida: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [[ ! -f "${toolchain}" ]]; then
    printf 'No está el toolchain MinGW: %s\n' "${toolchain}" >&2
    exit 1
fi
if [[ ! -f "${sdl2_root}/lib/libSDL2.a" ]]; then
    printf 'Falta SDL2 para MinGW en %s\n' "${sdl2_root}" >&2
    printf 'Descarga SDL2-devel-*-mingw.tar.gz y extrae x86_64-w64-mingw32 ahí.\n' >&2
    exit 1
fi
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    printf 'Instala g++-mingw-w64-x86-64 (no está x86_64-w64-mingw32-g++).\n' >&2
    exit 1
fi

if ((clean)); then
    rm -rf "${build_dir}" "${pal_build_dir}" "${output_dir}"
fi

cmake_common=(
    -DCMAKE_TOOLCHAIN_FILE="${toolchain}"
    -DCMAKE_BUILD_TYPE=Release
    -DPIKMIN_NATIVE_JAUDIO=ON
    -DPIKMIN_STATIC_RUNTIME=ON
    -DOPEN_NECTAR_VERSION="${OPEN_NECTAR_VERSION:-$(sed -n 's/.*set(OPEN_NECTAR_VERSION "\([^"]*\)".*/\1/p' "${repo_root}/CMakeLists.txt")}"
)

printf '%s\n' '[1/4] Configurando y compilando USA (Windows)...'
cmake -S "${repo_root}" -B "${build_dir}" "${cmake_common[@]}"
cmake --build "${build_dir}" --target pikmin_pc pikmin_launcher -j"$(nproc)"

printf '%s\n' '[2/4] Configurando y compilando PAL (Windows)...'
cmake -S "${repo_root}" -B "${pal_build_dir}" \
    "${cmake_common[@]}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00
cmake --build "${pal_build_dir}" --target pikmin_pc -j"$(nproc)"

usa_exe="${build_dir}/bin/nectar.exe"
pal_exe="${pal_build_dir}/bin/nectar.exe"
launcher_exe="${build_dir}/bin/nectar-launcher.exe"

for required in "${usa_exe}" "${pal_exe}" "${launcher_exe}"; do
    if [[ ! -f "${required}" ]]; then
        printf 'No se generó %s\n' "${required}" >&2
        exit 1
    fi
done

printf '%s\n' '[3/4] Montando el paquete...'
rm -rf "${output_dir}"
mkdir -p "${output_dir}"
cp "${usa_exe}" "${output_dir}/nectar.exe"
cp "${pal_exe}" "${output_dir}/nectar-pal.exe"
cp "${launcher_exe}" "${output_dir}/nectar-launcher.exe"
cp "${script_dir}/README.txt" "${output_dir}/README.txt"
# Sin símbolos de depuración: con SDL2 y el runtime dentro, cada .exe pasa de
# ~20 MB a una fracción.
x86_64-w64-mingw32-strip "${output_dir}/nectar.exe" "${output_dir}/nectar-pal.exe" "${output_dir}/nectar-launcher.exe"

# Nada de DLL propias: cada .exe debe pedir solo DLL del sistema.
system_dlls='^(advapi32|comdlg32|gdi32|imm32|kernel32|msvcrt|ole32|oleaut32|opengl32|setupapi|shell32|user32|version|winmm|shlwapi|uuid|dwmapi|dinput8|xinput1_4|hid|cfgmgr32)\.dll$'
for exe in nectar.exe nectar-pal.exe nectar-launcher.exe; do
    while read -r dll; do
        if ! printf '%s\n' "${dll,,}" | grep -Eq "${system_dlls}"; then
            printf '%s necesita %s, que no viene con Windows. Revisa PIKMIN_STATIC_RUNTIME.\n' "${exe}" "${dll}" >&2
            exit 1
        fi
    done < <(x86_64-w64-mingw32-objdump -p "${output_dir}/${exe}" | sed -n 's/.*DLL Name: //p')
done

printf '%s\n' '[4/4] Comprobando que el paquete lleva las dos builds...'
for required in nectar.exe nectar-pal.exe nectar-launcher.exe README.txt; do
    if [[ ! -f "${output_dir}/${required}" ]]; then
        printf 'El paquete está incompleto: falta %s\n' "${required}" >&2
        exit 1
    fi
done

if cmp -s "${output_dir}/nectar.exe" "${output_dir}/nectar-pal.exe"; then
    printf 'nectar.exe y nectar-pal.exe son el mismo archivo; la build PAL no se copió.\n' >&2
    exit 1
fi

zip_path="${script_dir}/out/nectar-windows.zip"
rm -f "${zip_path}"
python3 - "${output_dir}" "${zip_path}" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
base = os.path.dirname(src)
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _, files in os.walk(src):
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, base))
PY

printf '\nPaquete Windows creado:\n  %s\n  %s\n' "${output_dir}" "${zip_path}"
