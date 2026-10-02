#!/usr/bin/env bash
# package-standalone.sh [--clean] [--skip-tests] [--build-dir DIR] [--lang es|en]
#
# Genera un paquete Linux x86-64 totalmente autocontenido en
# packaging/linux/out/pikmin-native-linux — sin Docker ni herramientas
# externas:
#
#   nectar-linux/
#     pikmin            <- script lanzador (sh)
#     pikmin-launcher   <- script lanzador (sh)
#     pikmin.real           binario real
#     pikmin-launcher.real  binario real
#     lib/              <- glibc, libstdc++, SDL2 y demás librerías
#     lib/licenses/     <- avisos de licencia de las librerías incluidas
#     README.txt       <- LEEME.txt con --lang es
#
# Los lanzadores invocan el ld-linux incluido en lib/, así que el juego
# arranca con la glibc del paquete aunque la distro del usuario sea más
# antigua. Solo se excluyen las librerías acopladas al driver de GPU
# (libGL/libEGL/libgbm/libdrm), que debe aportar el sistema.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build-linux-standalone"
# La version europea es otro ejecutable: el codigo del juego se compila desde la
# decompilacion y esa decompilacion es condicional segun la version. El paquete
# lleva los dos y el instalador copia el que pida el disco.
pal_build_dir="${repo_root}/build-linux-standalone-pal"
output_dir="${script_dir}/out/nectar-linux"
stage_dir="${build_dir}/stage"

clean=0
run_tests=1
# El paquete se publica en inglés, así que ese es el idioma por defecto.
lang=en
while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --skip-tests) run_tests=0 ;;
        --build-dir) build_dir="$2"; stage_dir="${build_dir}/stage"; shift ;;
        --lang)
            case "${2-}" in
                en|es) lang="$2" ;;
                *) printf 'Idioma no soportado: %s (usa en o es)\n' "${2-}" >&2; exit 2 ;;
            esac
            shift
            ;;
        --help|-h)
            printf 'Uso: %s [--clean] [--skip-tests] [--build-dir DIR] [--lang es|en]\n' "$0"
            exit 0
            ;;
        *) printf 'Opción desconocida: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "${lang}" == es ]]; then
    readme_source=LEEME.txt
else
    readme_source=README.txt
fi

if ((clean)); then
    rm -rf "${build_dir}" "${pal_build_dir}" "${output_dir}"
fi

printf '%s\n' '[1/5] Configurando y compilando (x86-64 genérico)...'
# NATIVE_OPTIMIZE apagado: -march=native produciría instrucciones que la
# máquina de destino puede no tener, y es justo lo que verify-portable.sh
# comprueba. IPO es otra cosa: la optimización entre unidades de traducción no
# cambia el juego de instrucciones, sólo permite alinear a través de archivos.
# Dejarla apagada sólo hacía el paquete más lento en los equipos modestos que
# es su razón de existir.
cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF \
    -DPIKMIN_ENABLE_IPO=ON \
    -DPIKMIN_NATIVE_JAUDIO=ON \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "${build_dir}" -j"$(nproc)"

# La europea, con las mismas opciones. Solo el ejecutable del juego: el
# lanzador y las librerias son los mismos.
cmake -S "${repo_root}" -B "${pal_build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00 \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF \
    -DPIKMIN_ENABLE_IPO=ON \
    -DPIKMIN_NATIVE_JAUDIO=ON \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "${pal_build_dir}" --target pikmin_pc -j"$(nproc)"

if ((run_tests)); then
    printf '%s\n' '[2/5] Pruebas offline...'
    ctest --test-dir "${build_dir}" --output-on-failure
else
    printf '%s\n' '[2/5] Pruebas omitidas por --skip-tests.'
fi

printf '%s\n' '[3/5] Montando estructura del paquete...'
rm -rf "${stage_dir}" "${output_dir}"
DESTDIR="${stage_dir}" cmake --install "${build_dir}" --strip >/dev/null

mkdir -p "${output_dir}/lib"
cp "${stage_dir}/usr/bin/nectar" "${output_dir}/nectar.real"
# Sin pasar por "cmake --install": ese instala bajo el mismo nombre y
# sobrescribiria el americano.
cp "${pal_build_dir}/bin/nectar" "${output_dir}/nectar-pal.real"
strip "${output_dir}/nectar-pal.real" 2>/dev/null || true
cp "${stage_dir}/usr/bin/nectar-launcher" "${output_dir}/nectar-launcher.real"
cp "${script_dir}/${readme_source}" "${output_dir}/${readme_source}"

printf '%s\n' '[4/5] Copiando librerías del sistema al paquete...'
# Librerías acopladas al driver de GPU o al kernel: las aporta el sistema.
blocklist='^(linux-vdso|libOpenGL\.|libGL\.|libGLX\.|libEGL\.|libGLdispatch\.|libEGL_mesa|libgbm\.|libdrm|libnvidia|libvulkan\.)'

collect_libs() {
    ldd "$1" 2>/dev/null | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }'
}

copied=0
declare -a source_libs=()
for binary in "${output_dir}/nectar.real" "${output_dir}/nectar-pal.real" "${output_dir}/nectar-launcher.real"; do
    while IFS= read -r lib; do
        name="$(basename "$lib")"
        if [[ "$name" =~ $blocklist ]]; then
            continue
        fi
        if [ ! -f "${output_dir}/lib/${name}" ]; then
            cp -Lf "$lib" "${output_dir}/lib/${name}"
            source_libs+=("$lib")
            copied=$((copied + 1))
        fi
    done < <(collect_libs "$binary")
done

# El lanzador dinámico es imprescindible: sin él los wrappers no arrancan.
if [ ! -f "${output_dir}/lib/ld-linux-x86-64.so.2" ]; then
    for candidate in /lib64/ld-linux-x86-64.so.2 /lib/ld-linux-x86-64.so.2; do
        if [ -f "$candidate" ]; then
            cp -Lf "$candidate" "${output_dir}/lib/ld-linux-x86-64.so.2"
            source_libs+=("$candidate")
            copied=$((copied + 1))
            break
        fi
    done
fi
if [ ! -f "${output_dir}/lib/ld-linux-x86-64.so.2" ]; then
    printf 'No se encontró ld-linux-x86-64.so.2 en este sistema.\n' >&2
    exit 1
fi
chmod 755 "${output_dir}/lib/ld-linux-x86-64.so.2"
printf '  %d librerías incluidas.\n' "$copied"

# Avisos de licencia de las librerías incluidas (glibc es LGPL, SDL2 es
# zlib, etc.): redistribuirlas exige acompañarlas de sus textos legales.
licenses_dir="${output_dir}/lib/licenses"
mkdir -p "${licenses_dir}"
if command -v dpkg-query >/dev/null 2>&1; then
    declare -A seen_packages=()
    for lib in "${source_libs[@]}"; do
        real_lib="$(readlink -f "$lib")"
        package="$(dpkg-query -S "$real_lib" 2>/dev/null | head -n1 | cut -d: -f1 || true)"
        if [ -n "$package" ] && [ -z "${seen_packages[$package]:-}" ]; then
            seen_packages[$package]=1
            for notice in "/usr/share/doc/${package}/copyright" "/usr/share/doc/${package}/COPYING"; do
                if [ -f "$notice" ]; then
                    cp "$notice" "${licenses_dir}/${package}.$(basename "$notice")" 2>/dev/null || true
                fi
            done
        fi
    done
    printf '  Avisos de licencia copiados para %d paquetes.\n' "${#seen_packages[@]}"
else
    printf '  dpkg-query no disponible: revisa manualmente las licencias de lib/.\n' >&2
fi

make_wrapper() {
    local wrapper="$1" real="$2"
    cat >"$wrapper" <<'WRAPPER_EOF'
#!/bin/sh
# Lanzador autocontenido: usa la glibc y las librerías de lib/ para que el
# juego funcione en cualquier Linux x86-64. Generado por package-standalone.sh.
# Resuelve el directorio real aunque se invoque mediante PATH o un enlace.
self=$0
case "$self" in */*) ;; *) self=$(command -v -- "$self" 2>/dev/null || printf '%s' "$self");; esac
if command -v readlink >/dev/null 2>&1; then self=$(readlink -f "$self"); fi
here=$(CDPATH= cd -- "$(dirname -- "$self")" && pwd)
export NECTAR_EXECUTABLE_PATH="$here/WRAPPER_REAL"
exec "$here/lib/ld-linux-x86-64.so.2" --library-path "$here/lib" "$here/WRAPPER_REAL" "$@"
WRAPPER_EOF
    sed -i "s|WRAPPER_REAL|${real}|g" "$wrapper"
    chmod 755 "$wrapper"
}
make_wrapper "${output_dir}/nectar" "nectar.real"
make_wrapper "${output_dir}/nectar-launcher" "nectar-launcher.real"

printf '%s\n' '[5/5] Verificando el paquete...'
# La ISA debe seguir siendo x86-64 base. El techo de glibc no aplica:
# el paquete lleva su propia glibc.
    PIKMIN_SKIP_LIBC_CHECK=1 "${script_dir}/verify-portable.sh" \
    "${output_dir}/nectar.real" "${output_dir}/nectar-pal.real" "${output_dir}/nectar-launcher.real"

# Ninguna dependencia debe quedar sin resolver usando las librerías incluidas.
if LD_LIBRARY_PATH="${output_dir}/lib" ldd "${output_dir}/nectar.real" | grep -q 'not found'; then
    printf 'Faltan librerías en el paquete:\n' >&2
    LD_LIBRARY_PATH="${output_dir}/lib" ldd "${output_dir}/nectar.real" | grep 'not found' >&2
    exit 1
fi

# El lanzador debe ejecutarse a través del wrapper.
"${output_dir}/nectar-launcher" --help >/dev/null

printf '\nPaquete autocontenido creado en:\n  %s\n' "${output_dir}"
printf '%s\n' 'Cópialo a cualquier Linux x86-64 y ejecuta ./nectar-launcher.'
