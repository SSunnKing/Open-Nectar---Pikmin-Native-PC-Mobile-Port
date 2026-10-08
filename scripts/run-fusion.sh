#!/usr/bin/env bash
set -euo pipefail

fusion_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
launcher="$fusion_root/build/pikmin1/bin/nectar-launcher"

if [[ ! -x "$launcher" ]]; then
  echo "Falta el launcher. Ejecuta primero scripts/build-fusion.sh" >&2
  exit 1
fi

export NECTAR_PIKMIN1_DIR="$fusion_root/games/pikmin1"
export NECTAR_PIKMIN1_EXECUTABLE="$fusion_root/build/pikmin1/bin/nectar"
export NECTAR_PIKMIN2_DIR="$fusion_root/games/pikmin2"
export NECTAR_PIKMIN2_EXECUTABLE="$fusion_root/build/pikmin2/pikmin2_pc"

exec "$launcher"

