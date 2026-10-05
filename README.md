# Open Nectar Fusion

Entorno de trabajo conjunto para los ports nativos de Pikmin y Pikmin 2.

## Estructura

- `games/pikmin1`: copia independiente de Open Nectar.
- `games/pikmin2`: copia independiente de pikmin2-port.
- `build/pikmin1` y `build/pikmin2`: compilaciones aisladas; nunca comparten
  objetos, nombres de targets ni cachés de CMake.
- `scripts`: entrada común para compilar y abrir el launcher.

Los datos, partidas, ajustes y cachés permanecen dentro del directorio de cada
juego. El launcher recibe las cuatro rutas mediante variables `NECTAR_PIKMIN*`,
por lo que iniciar uno no cambia el directorio de trabajo del otro.

En Pikmin 2, la pestaña de ajustes del launcher se genera directamente a partir
del inventario completo del menú F1 (`--settings-dump`). No mantiene una copia
manual de sus opciones: los grupos o filas que se añadan al F1 aparecerán en el
launcher al recompilar Pikmin 2.

## Linux

```sh
./scripts/build-fusion.sh
./scripts/run-fusion.sh
```

## Windows (PowerShell)

```powershell
./scripts/build-fusion.ps1
./scripts/run-fusion.ps1
```

## Android

Los dos árboles Android se conservan y pueden evolucionar por separado. En este
momento Pikmin 2 aún hereda identificadores y targets Android de Pikmin 1, así
que el APK Fusion con selector de juego requiere una integración posterior. El
launcher común de esta primera fase corresponde a Windows/Linux.

## Aislamiento

Fusion no contiene metadatos `.git` de los proyectos de origen y no usa enlaces
hacia ellos. Los cambios realizados aquí no modifican `Open Nectar Android`,
`Open Nectar Windows` ni `pikmin2-port`.
