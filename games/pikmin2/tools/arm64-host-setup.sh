#!/bin/bash
# Fase 2 del port Android: prepara un Ubuntu x86_64 para cross-compilar y
# ejecutar el juego en ARM64 (QEMU usuario + multiarch). Ejecutar con sudo.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Ejecutar con sudo: sudo $0" >&2
    exit 1
fi

SRC=/etc/apt/sources.list.d/ubuntu.sources
SUITE=$(. /etc/os-release && echo "$VERSION_CODENAME")

# 1. Los repositorios de archive/security solo sirven amd64/i386: si no se
#    limitan, apt intentará bajar índices arm64 de ahí y fallará.
if ! grep -q '^Architectures:' "$SRC"; then
    cp "$SRC" /root/ubuntu.sources.bak-arm64
    sed -i 's|^Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg|Architectures: amd64 i386\n&|' "$SRC"
fi

# 2. arm64 vive en ports.ubuntu.com.
cat > /etc/apt/sources.list.d/ubuntu-ports-arm64.sources <<EOS
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports
Suites: $SUITE $SUITE-updates $SUITE-security
Components: main universe restricted multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOS

dpkg --add-architecture arm64
# Repos de terceros rotos (404, SSL) hacen fallar apt-get update aunque los
# índices de Ubuntu se hayan bajado bien; se sigue y falla, si acaso, en install.
apt-get update || echo "aviso: apt-get update devolvió error; se continúa"
rm -f /etc/apt/sources.list.d/ubuntu.sources.bak-arm64

# 3. Cross toolchain, QEMU usuario (registra binfmt) y las bibliotecas arm64
#    que enlaza el juego: SDL2, GL/GLES/EGL, X11, y Mesa (llvmpipe) para
#    ejecutarlo bajo QEMU.
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    qemu-user-binfmt qemu-user \
    g++-aarch64-linux-gnu pkg-config \
    libsdl2-dev:arm64 libgl-dev:arm64 libgles-dev:arm64 libegl-dev:arm64 \
    libx11-dev:arm64 libgl1-mesa-dri:arm64 libegl-mesa0:arm64

echo
echo "Listo. Comprobación:"
aarch64-linux-gnu-g++ --version | head -1
ls /usr/aarch64-linux-gnu/lib/libSDL2* 2>/dev/null || ls /usr/lib/aarch64-linux-gnu/libSDL2* | head -3
ls /proc/sys/fs/binfmt_misc/ | grep -q qemu-aarch64 && echo "binfmt qemu-aarch64 registrado"
