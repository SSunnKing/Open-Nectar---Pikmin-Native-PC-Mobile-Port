$ErrorActionPreference = "Stop"
$FusionRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

cmake -S "$FusionRoot/games/pikmin1" -B "$FusionRoot/build/pikmin1" -DPIKMIN_BUILD_LAUNCHER=ON -DPIKMIN_NATIVE_JAUDIO=ON -DPIKMIN_DEBUG_KEYS=ON
cmake --build "$FusionRoot/build/pikmin1" --config RelWithDebInfo --target pikmin_pc pikmin_launcher

cmake -S "$FusionRoot/games/pikmin2" -B "$FusionRoot/build/pikmin2"
cmake --build "$FusionRoot/build/pikmin2" --config RelWithDebInfo --target pikmin2_pc

Write-Host "Fusion listo. Ejecuta scripts/run-fusion.ps1"
