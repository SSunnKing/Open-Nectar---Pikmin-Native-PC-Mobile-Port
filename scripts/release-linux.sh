#!/usr/bin/env bash
# release-linux.sh [--clean]
#
# Archivos de Linux de una release de Open Nectar Fusion (ver
# scripts/release/make_release.py): paquete completo, uno por parte,
# manifest.json y el AppImage con todo.
#
# Se compila en un contenedor Ubuntu 20.04 (podman) para que los ejecutables
# arranquen en cualquier distro con glibc 2.31 o posterior. SDL2 y el runtime
# de C++ van dentro; solo dependen de glibc, libGL y libX11.
#
# Las versiones salen de versions.json (raíz del repo).
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-release/linux"
out_dir="${root}/release-out/linux"
image="open-nectar-build"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "${build_dir}" "${out_dir}"
fi

launcher_version="$(python3 -c "import json;print(json.load(open('${root}/versions.json'))['launcher'])")"
if ! command -v podman >/dev/null 2>&1; then
    printf 'Hace falta podman: sudo apt install podman\n' >&2
    exit 1
fi

printf '[1/4] Imagen de compilación (Ubuntu 20.04)...\n'
podman build -q -t "${image}" -f "${root}/games/pikmin1/packaging/linux/Containerfile" \
    "${root}/games/pikmin1/packaging/linux" >/dev/null

printf '[2/4] Compilando el launcher, Pikmin 1 y Pikmin 2 en el contenedor...\n'
podman run --rm --userns=keep-id -v "${root}":/src -e OPEN_NECTAR_VERSION="${launcher_version}" \
    "${image}" bash /src/scripts/release/container-build-linux.sh

launcher="${build_dir}/p1-usa/bin/nectar-launcher"
usa="${build_dir}/p1-usa/bin/nectar"
pal="${build_dir}/p1-pal/bin/nectar"
p2="${build_dir}/p2/pikmin2_pc"

# Solo glibc, libGL y libX11: cualquier otra dependencia es un error de empaquetado.
allowed='^(libc|libm|libdl|libpthread|librt|ld-linux-x86-64)\.so|^libGL\.so\.1$|^libX11\.so\.6$'
for exe in "${launcher}" "${usa}" "${pal}" "${p2}"; do
    [[ -f "${exe}" ]] || { printf 'No se generó %s\n' "${exe}" >&2; exit 1; }
    while read -r lib; do
        if ! printf '%s\n' "${lib}" | grep -Eq "${allowed}"; then
            printf '%s depende de %s, que no está en todos los sistemas. Revisa PIKMIN_STATIC_RUNTIME.\n' "${exe}" "${lib}" >&2
            exit 1
        fi
    done < <(readelf -d "${exe}" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
done

printf '[3/4] Montando los archivos de la release...\n'
stripped="${build_dir}/stripped"
mkdir -p "${stripped}"
cp "${launcher}" "${stripped}/nectar-launcher"
cp "${usa}" "${stripped}/nectar"
cp "${pal}" "${stripped}/nectar-pal"
cp "${p2}" "${stripped}/pikmin2_pc"
strip "${stripped}"/* 2>/dev/null || true
python3 "${root}/scripts/release/make_release.py" --os linux --out "${out_dir}" \
    --launcher "${stripped}/nectar-launcher" --p1-usa "${stripped}/nectar" \
    --p1-pal "${stripped}/nectar-pal" --p2 "${stripped}/pikmin2_pc" \
    --extra "${root}/games/pikmin1/packaging/linux/README.txt" \
    --extra "${root}/games/pikmin1/packaging/linux/LEEME.txt" \
    --extra "${root}/games/pikmin1/packaging/icon/open_nectar.png"

printf '[4/4] Montando Open_Nectar-x86_64.AppImage (todo junto)...\n'
appdir="${build_dir}/AppDir"
rm -rf "${appdir}"
mkdir -p "${appdir}/usr/bin"
cp "${out_dir}/staging/nectar-linux"/* "${appdir}/usr/bin/"
ln -s usr/bin/nectar-launcher "${appdir}/AppRun"
cp "${root}/games/pikmin1/packaging/icon/open_nectar.png" "${appdir}/open-nectar.png"
cat >"${appdir}/open-nectar.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Open Nectar
Comment=Native Pikmin and Pikmin 2 ports: install, update and play
Exec=nectar-launcher
Icon=open-nectar
Terminal=false
Categories=Game;
DESKTOP
tool="${build_dir}/appimagetool-x86_64.AppImage"
if [[ ! -x "${tool}" ]]; then
    curl -fsSL -o "${tool}" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "${tool}"
fi
release="$(python3 -c "import json;print(json.load(open('${root}/versions.json'))['release'])")"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION="${release}" \
    "${tool}" --no-appstream "${appdir}" "${out_dir}/Open_Nectar-x86_64.AppImage" >/dev/null
printf '  %s\n' "${out_dir}/Open_Nectar-x86_64.AppImage"
