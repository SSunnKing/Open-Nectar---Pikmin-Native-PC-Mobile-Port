# Toolchain de compilación cruzada Linux x86-64 → Linux ARM64 (fase 2 del plan
# de Android, docs/ANDROID_PLAN.md). Requiere el multiarch preparado por
# tools/arm64-host-setup.sh; con qemu-user-static registrado, los binarios
# resultantes se ejecutan directamente en el host, tests incluidos.
#
# Uso:
#   cmake -S . -B build-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-aarch64.cmake \
#         -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPIKMIN_NATIVE_JAUDIO=ON \
#         -DPIKMIN_NATIVE_OPTIMIZE=OFF
#   cmake --build build-arm64 -j"$(nproc)"
#
# PIKMIN_NATIVE_OPTIMIZE debe ir OFF: -march=native describe la CPU del host
# x86, que no es la de destino.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_PREFIX aarch64-linux-gnu)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)

# Bibliotecas y cabeceras arm64 del multiarch de Debian/Ubuntu. Los programas
# (pkg-config, etc.) siguen siendo los del host.
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX} /usr/lib/${TOOLCHAIN_PREFIX})
set(CMAKE_LIBRARY_ARCHITECTURE ${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# Las cabeceras del multiarch son compartidas y viven en /usr/include.
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# pkg-config debe responder con los .pc de arm64, no con los del host.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${TOOLCHAIN_PREFIX}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# Los tests se ejecutan en el host vía binfmt/qemu-user-static. Si binfmt no
# estuviera registrado, ctest los lanza explícitamente con qemu.
find_program(_qemu_aarch64 NAMES qemu-aarch64-static qemu-aarch64)
if(_qemu_aarch64 AND NOT EXISTS /proc/sys/fs/binfmt_misc/qemu-aarch64)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_qemu_aarch64};-L;/usr/${TOOLCHAIN_PREFIX}")
endif()
