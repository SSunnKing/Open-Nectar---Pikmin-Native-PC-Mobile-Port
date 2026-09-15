#!/usr/bin/env bash
# package-apk.sh [--clean] [--debug-key]
#
# Genera el APK de release de Open Nectar para Android en
# packaging/android/out/:
#
#   open_nectar_<versión>.apk         <- APK firmado (arm64-v8a, USA+PAL)
#   open_nectar_<versión>.apk.sha256  <- suma para publicar junto al APK
#   README.txt                           <- instrucciones de instalación
#
# La firma sale de android/keystore.properties o de
# ~/.config/opennectar/keystore.properties (ver android/app/build.gradle).
# Sin clave el build se firma con la clave debug: vale para probar por adb,
# pero un APK así no puede actualizar una instalación firmada de verdad (ni
# al revés), así que el script se niega salvo con --debug-key.
#
# Necesita el JDK y el SDK de Android que usa el proyecto: JAVA_HOME o
# ~/Android/jdk, y android/local.properties apuntando al SDK.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
android_dir="${repo_root}/android"
output_dir="${script_dir}/out"

clean=0
allow_debug_key=0
while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --debug-key) allow_debug_key=1 ;;
        --help|-h)
            printf 'Uso: %s [--clean] [--debug-key]\n' "$0"
            exit 0
            ;;
        *) printf 'Opción desconocida: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

export JAVA_HOME="${JAVA_HOME:-${HOME}/Android/jdk}"
if [[ ! -x "${JAVA_HOME}/bin/java" ]]; then
    printf 'No hay JDK en %s (pon JAVA_HOME)\n' "${JAVA_HOME}" >&2
    exit 1
fi

keystore_props=""
for candidate in "${android_dir}/keystore.properties" "${HOME}/.config/opennectar/keystore.properties"; do
    if [[ -f "${candidate}" ]]; then keystore_props="${candidate}"; break; fi
done
if [[ -z "${keystore_props}" && "${allow_debug_key}" -eq 0 ]]; then
    printf 'Sin keystore.properties: el APK se firmaría con la clave debug.\n' >&2
    printf 'Crea la clave (ver packaging/android/README-firma.txt) o pasa --debug-key.\n' >&2
    exit 1
fi

if (( clean )); then
    (cd "${android_dir}" && ./gradlew clean -q)
fi
(cd "${android_dir}" && ./gradlew assembleRelease -q)

apk="$(ls -1 "${android_dir}"/app/build/outputs/apk/release/open_nectar_*.apk | head -n1)"
[[ -f "${apk}" ]] || { printf 'Gradle no ha producido el APK\n' >&2; exit 1; }

# Comprobar la firma con apksigner del SDK, si está.
sdk_dir="$(sed -n 's/^sdk\.dir=//p' "${android_dir}/local.properties" 2>/dev/null || true)"
apksigner="$(ls -1 "${sdk_dir}"/build-tools/*/apksigner 2>/dev/null | sort -V | tail -n1 || true)"
if [[ -n "${apksigner}" ]]; then
    PATH="${JAVA_HOME}/bin:${PATH}" "${apksigner}" verify --print-certs "${apk}" | sed -n 's/^Signer #1 certificate SHA-256 digest: /Firma SHA-256: /p'
fi

rm -rf "${output_dir}"
mkdir -p "${output_dir}"
cp "${apk}" "${output_dir}/"
cp "${script_dir}/README.txt" "${output_dir}/README.txt"
(cd "${output_dir}" && sha256sum "$(basename "${apk}")" > "$(basename "${apk}").sha256")

printf '\nPaquete en %s:\n' "${output_dir}"
ls -la "${output_dir}"
