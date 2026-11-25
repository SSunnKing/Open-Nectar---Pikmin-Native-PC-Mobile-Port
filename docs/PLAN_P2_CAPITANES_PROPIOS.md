# Pikmin 2: capitanes y Pikmin propios del creador de modelos

Objetivo: que los personajes del creador de modelos (`tools/modelmaker`,
`.nectarchar`) se puedan usar en Pikmin 2: capitanes nuevos elegibles en el
selector para los dos capitanes, y aspecto propio para los Pikmin de cada
color, como ya pasa en Pikmin 1.

## Decisiones

- **Carpeta por juego.** Cada juego tiene su `Load/Characters/<id>/`
  (capitanes) y `Load/Pikmin/<id>/` (Pikmin). El creador de modelos y el
  launcher dejan elegir dónde se instala: Pikmin, Pikmin 2 o los dos.
- **Capitanes y Pikmin.** Los capitanes se añaden como ranuras nuevas del
  selector de P2; los Pikmin cambian el aspecto de su color (rojo, amarillo,
  azul y, solo en P2, morado y blanco).
- **Mismo formato.** `.nectarchar` (character.json + model.nhm + portrait.png)
  no cambia; P2 acepta además los destinos `pikmin_purple` y `pikmin_white`.

## Por qué es viable

- El rig de los capitanes del creador (el Olimar de P1) usa los mismos nombres
  de hueso que los `.bmd` de capitán de P2: `kosinull`, `legcentre`,
  `llegjnt`, `rlegjnt`, `sebonjnt`, `headjnt`, `happajnt1-3`, `lhandjnt`,
  `rhandjnt`. P1 ya convierte capitanes de P2 a NHM con esos nombres
  (`buildCaptainBmd` en `games/pikmin1/pc_port/mods/pc_hd_model_convert.cpp`).
- P2 ya tiene selector de personaje por capitán (`pc_port/pc_p2_captains.cpp`):
  cambia el `.bmd` al cargar el capitán (`naviMgr.cpp`) y tiene vista previa
  3D (`pcCaptainPreview.cpp`).

## Enfoque

El personaje propio **se dibuja** con su NHM; **todo lo demás** sigue siendo
el modelo del juego: el capitán propio carga `orima_model/orima1.bmd` (el
Pikmin, el `.bmd` de su color) para el esqueleto, las animaciones, la luz de
la antena, la hoja y las colisiones. En el dibujo J3D, si el modelo es de un
personaje propio, no se dibujan sus formas y se dibuja el NHM con las
matrices de hueso del `J3DModel` (como `pc_hd_model_draw_custom` en P1).

## Fases

1. **Lector NHM compartido** (`libs/nectar_assets/nhm.h`). Leer el pack
   (vértices, huesos, partes, texturas, flags v1-3) y la cuenta del retarget
   sin depender de P1. Test con un pack de prueba.
2. **Esqueletos de P2.** Volcar huesos, jerarquía y pose de reposo de
   `orima1.bmd` y de los `piki_p2_*.bmd`; compararlos con los del NHM
   (capitán y Pikmin). Decidir el retarget de los packs que guardan sus
   propias posiciones de hueso (flag 1). Test `game-assets`.
3. **Dibujo en P2.** Gancho en el dibujo de paquetes J3D: el cuerpo de un
   personaje propio no dibuja sus formas y dibuja el NHM con GX inmediato
   (texturas RGBA, luz, visor translúcido al final, partes sin tinte, tinte
   del capitán 2 repetido y del color del Pikmin). Respetar el planificador
   cooperativo (nada de esperas con mutex nativo).
4. **Ranuras de capitán.** Personajes propios detrás de los 9 fijos
   (`PC_P2_CHAR_CUSTOM_FIRST + i`), leídos de `Load/Characters`. En
   `pikmin_settings.conf` se guarda el id de su carpeta, no un índice. Para el
   juego son "tipo Olimar" (escala 1.3, luz, cursor, título de capitán caído).
5. **Pikmin.** Por color, el pack elegido de `Load/Pikmin` (Opciones >
   Gráficos y launcher, como en P1); `pikmin_all` tiñe con el color.
6. **Selector y vista previa.** Tarjetas nuevas con nombre y modelo 3D
   (orima1 + NHM en el heap de la vista previa); `portrait.png` como icono en
   las pantallas de Desafío y VS (si no, el de Olimar).
7. **Creador de modelos y launcher.** Instalar en Pikmin, Pikmin 2 o los dos;
   destinos morado y blanco solo para P2; vista previa sobre el esqueleto de
   P2 si la fase 2 muestra diferencias. El launcher instala y lista por juego.
8. **Android y tests.** `Load/Characters` y `Load/Pikmin` de P2 en Android;
   tests de ranuras, del guardado por id y del lector; compilar el APK.

## Resultados

### Fase 1

`libs/nectar_assets/nhm.h`: `parseNhm`/`loadNhm` (v1-3, mismas comprobaciones
que Pikmin 1), `nhmSkinMatrices` (piel ligada al pack o retarget, la misma
cuenta que `retargetSkin` de `pc_hd_models.cpp` pero sobre matrices
genéricas: animadas, de reposo y padres) y `nhmSkinVertex`. Test:
`nectar_nhm_test`. Pikmin 1 sigue con su copia; pasarlo a la librería es
una limpieza aparte.

### Fase 2

Volcado de `pikis.szs` (JNT1 + INF1): `orima1.bmd`, `orima3.bmd`,
`syatyou.bmd`, `piki_p2_red.bmd` y `piki_p2_black.bmd` tienen el **mismo
esqueleto de 11 huesos**, con los nombres del pack y la misma pose de reposo
(salvo un hombro del Presidente):

```
0 kosinull (raíz, y=3.55)   4 sebonjnt (0)   8 happajnt3 (7)
1 legcentre (0)             5 headjnt (4)    9 lhandjnt (4)
2 llegjnt (1)               6 happajnt1 (5) 10 rhandjnt (4)
3 rlegjnt (1)               7 happajnt2 (6)
```

Frente al de Pikmin 1 (12 huesos), solo falta su ayudante de la cara
(índice 3), que el pack no usa. Pikmin 1 ya dibuja el Presidente y los
Pikmin de P2 con sus propias inversas sobre su esqueleto, así que los ejes
locales coinciden. Decisión: en P2 los huesos se buscan **por nombre** en el
`J3DModelData` y se usa `nhmSkinMatrices` con el reposo del `.bmd` y las
matrices animadas del `J3DModel`; el creador escribe siempre NHM v3 con
retarget, así que el personaje conserva sus proporciones. No hace falta
vista previa sobre el esqueleto de P2 en el creador (fase 7).

## Riesgos

- Retarget (fase 2): proporciones distintas desplazan brazos y antena.
- Gancho J3D (fase 3): matrices en espacio de vista, sombras y orden de lo
  translúcido.
- Memoria: el heap de la vista previa con muchos personajes instalados.

## Estado

- [x] 1. Lector NHM compartido
- [x] 2. Esqueletos de P2
- [x] 3. Dibujo en P2
- [x] 4. Ranuras de capitán (la vista previa 3D de sus tarjetas, en la fase 6)
- [x] 5. Pikmin
- [x] 6. Selector y vista previa (en las pantallas de Desafío y VS, de momento el icono de Olimar)
- [x] 7. Creador de modelos y launcher
- [x] 8. Android y tests (P2 en Android hace chdir a su carpeta, así que Load/ vale tal cual; el APK no se ha compilado aquí: Gradle compila lo nativo sin límite de trabajos)
