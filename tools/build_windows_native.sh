#!/usr/bin/env bash
# Builds the game natively on Windows with MSYS2's MinGW-w64 toolchain.
#
# One-time setup (from an MSYS2 shell):
#   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
#       mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf \
#       mingw-w64-x86_64-SDL2 mingw-w64-x86_64-openxr-sdk
#
# Usage, from any shell:
#   C:\msys64\usr\bin\bash.exe -lc "/c/path/to/repo/tools/build_windows_native.sh [extra cmake args]"
#
# Output: build-win/bin/nectar.exe, with the DLLs it needs copied beside it.
set -euo pipefail

export MSYSTEM=MINGW64
export PATH="/mingw64/bin:$PATH"

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$repo/build-win"

# PIKMIN_NATIVE_JAUDIO=ON matches packaging/windows/package-standalone.sh: the
# release configuration, and the only one that links (moviePlayer.cpp calls
# into src/jaudio/pikidemo.c, which the legacy mixer build leaves out).
cmake -S "$repo" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPIKMIN_NATIVE_JAUDIO=ON \
    -DPIKMIN_BUILD_TESTS=OFF \
    "$@"
cmake --build "$build" -j"$(nproc)"

# The game links SDL2 (and the OpenXR loader when VR is on) dynamically, and
# MinGW's C++ runtime is not linked statically on a native build. Copy every
# non-system DLL the executable imports so it runs outside the MSYS2 shell.
bin="$build/bin"
for exe in "$bin"/*.exe; do
    ldd "$exe" | awk '/\/mingw64\/bin\// { print $3 }' | while read -r dll; do
        cp -u "$dll" "$bin/"
    done
done
echo "Built: $bin"
