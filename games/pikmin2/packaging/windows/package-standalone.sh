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
#     SDL2.dll
#     README.txt
#
# El instalador copia nectar.exe o nectar-pal.exe a nectar.exe según el disco.
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
if [[ ! -f "${sdl2_root}/lib/libSDL2.dll.a" ]]; then
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

copy_dll() {
    local src="$1"
    if [[ -f "${src}" ]]; then
        cp -f "${src}" "${output_dir}/$(basename "${src}")"
        return 0
    fi
    return 1
}

sdl2_copied=0
for candidate in \
    "${sdl2_root}/bin/SDL2.dll" \
    "${build_dir}/bin/SDL2.dll"
do
    if copy_dll "${candidate}"; then
        sdl2_copied=1
        break
    fi
done
if ((sdl2_copied == 0)); then
    printf 'No se encontró SDL2.dll (esperado en %s/bin).\n' "${sdl2_root}" >&2
    exit 1
fi

# libgcc/libstdc++ van estáticos en el toolchain; winpthread a veces no.
while IFS= read -r -d '' match; do
    copy_dll "${match}" || true
done < <(find /usr/x86_64-w64-mingw32 /usr/lib/gcc/x86_64-w64-mingw32 \
    -name 'libwinpthread-1.dll' -type f -print0 2>/dev/null || true)

printf '%s\n' '[4/4] Comprobando que el paquete lleva las dos builds...'
for required in nectar.exe nectar-pal.exe nectar-launcher.exe SDL2.dll README.txt; do
    if [[ ! -f "${output_dir}/${required}" ]]; then
        printf 'El paquete está incompleto: falta %s\n' "${required}" >&2
        exit 1
    fi
done

if cmp -s "${output_dir}/nectar.exe" "${output_dir}/nectar-pal.exe"; then
    printf 'nectar.exe y nectar-pal.exe son el mismo archivo; la build PAL no se copió.\n' >&2
    exit 1
fi

printf '\nPaquete Windows creado en:\n  %s\n' "${output_dir}"
printf '%s\n' 'Comprime esa carpeta como nectar-windows.zip.'
