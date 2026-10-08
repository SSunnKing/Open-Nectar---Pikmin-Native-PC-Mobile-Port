#!/usr/bin/env bash
set -euo pipefail

fusion_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
jobs="${NECTAR_BUILD_JOBS:-$(nproc)}"

cmake -S "$fusion_root/games/pikmin1" -B "$fusion_root/build/pikmin1" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPIKMIN_BUILD_LAUNCHER=ON \
  -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_DEBUG_KEYS=ON
cmake --build "$fusion_root/build/pikmin1" --parallel "$jobs" --target pikmin_pc pikmin_launcher

cmake -S "$fusion_root/games/pikmin2" -B "$fusion_root/build/pikmin2" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$fusion_root/build/pikmin2" --parallel "$jobs" --target pikmin2_pc

echo "Fusion listo. Ejecuta: $fusion_root/scripts/run-fusion.sh"
