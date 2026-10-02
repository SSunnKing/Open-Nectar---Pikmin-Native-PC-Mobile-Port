#!/bin/sh
# Vuelca el último informe del tick profiler y de PERF del móvil (fase 0 de
# docs/PLAN_RENDIMIENTO.md). Uso: tools/android_perf_capture.sh <etiqueta>
# Requiere PIKMIN_TICK_STATS=1 y PIKMIN_PERF_STATS=1 en env.txt del juego.
label=${1:-escena}
out=docs/perf/$(date +%Y%m%d-%H%M%S)-$label.txt
mkdir -p docs/perf
{
  echo "# $label  $(date)"
  adb shell dumpsys thermalservice | grep -E "Temperature\{.*(CPU|GPU|SKIN)" | head -8
  adb logcat -d -s OpenNectar | grep -E "PC tick|mean|PERF|PC Port\] FPS|Textures" | tail -40
} > "$out"
echo "$out"; cat "$out"
