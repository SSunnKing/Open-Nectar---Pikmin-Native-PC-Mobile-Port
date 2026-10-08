# Pruebas pendientes (plan por fases)

Comandos base:

```bash
cd /home/sunking/Documentos/antigravity/pikmin2-port
cmake --build build-nat -j4 && ctest --test-dir build-nat --output-on-failure
cmake --build build-asan -j4
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 ctest --test-dir build-asan --output-on-failure
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 ./build-asan/pikmin2_pc
```

Esperado en CTest: todo verde salvo `p2_jaudio_integration_test` (omitida) y
`p2_tev_shader_test` (falla; se arregla en la fase 5).

## Fase 1 — Memoria y vida útil

- [ ] Piklopedia: entrar y salir varias veces con post-proceso activo (bloom/SSAO/DOF).
- [ ] Cambiar de sección (área → cueva → base) con Lock-On fijado sobre un enemigo.
- [ ] Cambiar de sección con First Person activo; el capitán debe verse al salir.
- [ ] Reiniciar partida desde el menú y volver a cargar.
- [ ] Cerrar el juego tras varias secciones: sin errores ASan al salir.

## Fase 2 — Concurrencia

Build nuevo con ThreadSanitizer (no combinable con ASan):

```bash
cmake -S . -B build-tsan -DPIKMIN2_TSAN=ON -DBUILD_TESTING=ON && cmake --build build-tsan -j4
ctest --test-dir build-tsan --output-on-failure
TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1 ./build-tsan/pikmin2_pc 2> tsan.log
```

- [ ] Partida de 10-15 min con música y efectos: sin cortes ni cuelgues de audio.
- [ ] Guardar y cargar partida varias veces (hilo de tarjeta: OSWaitCond/OSSignalCond).
- [ ] Ver una cinemática THP y saltarla a medias (OSCancelThread del hilo de lectura).
- [ ] Cambiar de sección muchas veces seguidas (liberación de mutex en heaps destruidos);
      buscar en la salida "freed memory holds a locked OSMutex".
- [ ] Cerrar la ventana en mitad de partida y durante una carga: debe salir sin colgarse.
- [ ] Revisar `tsan.log`: interesan sobre todo "lock-order-inversion" y races en
      p2_os_host.cpp / JAS*; las de buffers GX/ARAM son esperables.

## Fase 3 — 64 bits y endianness

- [ ] Entrar en varias cuevas y quedarse unos minutos: la música dinámica (PSAutoBgm)
      debe cargar y cambiar con los enemigos cerca, sin crash al cargar el conductor.
- [ ] Zonas con muchos Pikmin y enemigos juntos (colisiones): nada de empujones
      dobles ni temblores raros al amontonarse; comparar fluidez con antes.
- [ ] Música/efectos que usen filtro FIR (cualquier escena con eco/filtro de sonido).
- [ ] Ver el menú de título y los vídeos THP (JUTGraphFifo/JUTDirectFile tocados).
- [ ] Revisar que no sale "side table exhausted" en la consola tras una partida larga.

## Fase 4 — 30/60/120 FPS

Comparar cada punto en los tres modos (F1 → FPS Mode 30 / 60 / 120):

- [ ] Sonido: fundidos de música al pausar/entrar en menús, al empezar y acabar el día,
      y al acercarse a un jefe (BossBgmFader) deben durar lo mismo en los tres modos.
- [ ] Morir un jefe: el sonido de desaparición/muerte debe cortar al mismo tiempo.
- [ ] Tarareo de los Pikmin al caminar: debe empezar tras el mismo tiempo andando.
- [ ] Silbar y reagrupar: la ventana de la formación (50 frames) y las huellas del capitán
      (ruta de los Pikmin que le siguen) iguales en los tres modos.
- [ ] Pan-Modoki (ladrón de tesoros): velocidad al llegar a un waypoint y frecuencia
      con que recalcula ruta, iguales en los tres modos.
- [ ] Pausa corta del juego (GameSystem::mIsPaused) tras cinemáticas: misma duración.

## Fase 5 — Renderer / TEV

- [ ] `ctest --test-dir build-nat -R "p2_tev_shader_test|p2_postprocess_test" --output-on-failure`
      (ambos deben pasar; el TEV ya no debe fallar).
- [ ] Jugar con la salida en consola: no debe aparecer
      "Specialised TEV shader failed to compile" ni "failed to link".
- [ ] Niebla: zona exterior con niebla lejana (y cueva) — la niebla debe verse a
      distancia y el HUD/menús no deben salir cubiertos de color de niebla.
- [ ] Agua y superficies con textura proyectada (texgen STQ): sin estiramientos.
- [ ] Borrar `shader_cache/` una vez y arrancar, para forzar la recompilación de todos los programas.

## Fase 6 — Audio / ARAM / DVD

- [ ] `ctest --test-dir build-asan -R "p2_aram_test|p2_envelope_test" --output-on-failure`
      con `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`.
- [ ] Cerrar la ventana en mitad de la partida con música sonando, 5-10 veces seguidas
      (mejor con `build-asan/pikmin2_pc`): nunca debe colgarse ni dar crash/ASan al salir.
- [ ] Reinicio del juego (combinación de reset / volver al título que llame a OSResetSystem):
      debe salir limpio, sin "abort".
- [ ] Arrancar sin dispositivo de audio (p. ej. `SDL_AUDIODRIVER=dummy_inexistente ./build-nat/pikmin2_pc`):
      el juego debe ir fluido, sin tirones cada frame; conectar audio después no es necesario.
- [ ] Música y efectos durante cambios de escena (área → cueva → base) sin cortes raros.

## Fase 7 — Guardas, trucos, logros

- [ ] Jugar un día, salir al título (y a la selección de partida), quedarse ahí 1-2 min
      y volver a entrar: sin congelaciones ni crash (antes se leía moviePlayer liberado).
- [ ] Ver una cinemática, terminar el día y volver al título: el título debe animarse normal.
- [ ] F9 (fin de día): pulsarlo en el título y luego entrar en partida → NO debe acabar el día.
      Pulsarlo en una cueva o durante una cinemática → no hace nada. En superficie → acaba el día.
- [ ] Guardar partida y cerrar la ventana justo mientras guarda (varias veces):
      la partida debe cargar bien después (nunca "datos dañados"). No deben quedar
      archivos `.tmp_*` visibles en la lista de partidas del juego.
- [ ] Cambiar ajustes en F1 y cerrar: `pikmin2_settings`/config se conserva completo.
- [ ] Desbloquear un logro y cerrar enseguida: el logro sigue al volver.

## Fase 8 — Validación final

Las tres configuraciones ya compilan (RelWithDebInfo en build-nat, ASan en build-asan;
Debug y TSan se compilaron en una carpeta temporal: usa los comandos de arriba para tenerlas en el repo).

- [ ] `ctest --test-dir build-nat --output-on-failure`  → todo verde salvo el skip de audio (77).
- [ ] `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 ctest --test-dir build-asan --output-on-failure`
- [ ] Recorrido manual con `build-asan/pikmin2_pc`: arranque → menú → cargar partida → zona →
      cueva → salir de cueva → Piklopedia → cinemática → guardar → volver al título → cerrar.
- [ ] Repetir el recorrido corto a 30, 60 y 120 FPS.
- [ ] Con y sin dispositivo de audio.
