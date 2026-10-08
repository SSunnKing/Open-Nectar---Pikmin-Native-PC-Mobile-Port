# Prompt de continuidad: corrección por fases del port de Pikmin 2

Actúa como ingeniero sénior especializado en C++, ports nativos de GameCube, OpenGL/GX, concurrencia y depuración de memoria.

Debes continuar el trabajo en un port nativo de Pikmin 2 a PC. No quiero solamente recomendaciones: debes implementar las correcciones, añadir pruebas de regresión y verificar cada fase.

IMPORTANTE: trabaja únicamente en una fase cada vez. Cuando termines una fase:

1. Resume qué has cambiado.
2. Indica qué archivos tocaste.
3. Enumera las pruebas ejecutadas y sus resultados.
4. Explica cualquier riesgo o problema pendiente.
5. Detente y pregúntame si quiero que continúes con la fase siguiente.

No empieces otra fase sin mi confirmación.

## Repositorio

Ruta:

`/home/sunking/Documentos/antigravity/pikmin2-port`

Componentes principales:

- `pikmin2-decomp/`: submódulo con la decompilación de Pikmin 2.
- `patches/pikmin2-decomp-host.patch`: adaptaciones del código decompilado al host.
- `pc_port/dolphin_stubs/p2_os_host.cpp`: hilos, interrupciones, mutex, colas, DVD y VI.
- `pc_port/gl/pc_gfx.cpp`: traducción de GX a OpenGL.
- `pc_port/gl/pc_tev_shader.cpp`: generador de shaders TEV.
- `pc_port/audio/`: ARAM, DSP, audio y JAudio.
- `pc_port/settings/pc_settings.cpp`: menú F1.
- `pc_port/pc_p2_cheats.cpp`: trucos.
- `pc_port/pc_achievements.cpp`: logros.
- Target principal: `pikmin2_pc`.
- Compilación habitual: `cmake --build build-nat`.

Ignora los targets correspondientes a Pikmin 1.

## Estado delicado del árbol de trabajo

El repositorio ya estaba muy modificado antes de comenzar este trabajo. Hay cambios del usuario y archivos sin seguimiento.

NO debes ejecutar:

- `git reset`
- `git checkout --`
- `git clean`
- `git stash`
- ninguna operación que descarte cambios
- refactors masivos no relacionados

No reviertas ni sobrescribas modificaciones existentes. Inspecciona siempre `git status` y el diff exacto del archivo antes de editarlo. Si tus cambios se solapan con trabajo previo, conserva ambos.

Estado conocido aproximado:

- Modificados: `CMakeLists.txt`, `README.md`, `patches/pikmin2-decomp-host.patch`, el submódulo `pikmin2-decomp` y múltiples archivos de `pc_port`.
- Sin seguimiento: logs, copias de seguridad, `pc_achievements.cpp/.h` y `pc_p2_cheats.cpp`.
- Las entradas de `pc_achievements.cpp` y `pc_p2_cheats.cpp` en CMake ya existían como cambios del usuario; no fueron añadidas durante la fase 0.

No hagas commits salvo que te lo pida expresamente.

## Objetivo técnico general

Encontrar y corregir problemas reales, no cuestiones cosméticas:

- crashes y corrupción de memoria;
- asignaciones en el heap incorrecto;
- use-after-free;
- carreras y deadlocks;
- problemas de 64 bits;
- comportamiento dependiente de 30/60/120 FPS;
- errores en la emulación GX→OpenGL;
- fallos de audio y DVD asíncrono;
- nulos, estados incompletos y bucles infinitos;
- incompatibilidades entre pruebas, shaders y runtime.

Prioridad:

1. Corrupción de memoria y crashes.
2. Deadlocks y carreras.
3. Errores de portabilidad de 64 bits.
4. Lógica dependiente de los FPS.
5. Renderer y audio incorrectos.
6. Robustez de trucos, menús y estados excepcionales.
7. Mantenibilidad solamente cuando prevenga fallos reales.

## Reglas técnicas importantes

### Heap del host

El `operator new` global puede reservar en `JKRHeap::sCurrentHeap`. Ese heap puede pertenecer a una sección del juego y destruirse al cambiar de sección.

Cualquier contenedor o dato del port que sobreviva a la sección —especialmente globales, estáticos, cachés, audio, renderer o DVD— debe usar memoria del host mediante:

- `pc_host_alloc_set(true)`;
- `pc_host_alloc_active()`;
- un asignador de host como `PcGfxHostAllocator`;
- o almacenamiento cuya vida útil y heap sean demostrablemente seguros.

No asumas que usar `std::vector`, `std::string`, `unordered_map`, `unordered_set` o `new` es automáticamente seguro.

### Concurrencia

Hay hilos reales:

- hilo principal;
- `JASAudioThread`;
- hilo DVD;
- callback de audio SDL.

El juego original utilizaba `OSDisableInterrupts` como sección crítica en una máquina esencialmente mononúcleo. En el port es un cerrojo global recursivo por hilo, implementado en `p2_os_host.cpp`, y puede soltarse temporalmente durante esperas bloqueantes.

Revisa siempre:

- orden de adquisición de locks;
- `OSDisableInterrupts` combinado con mutex de OS;
- `SDL_LockAudioDevice`;
- `sMsgMutex`;
- colas de mensajes;
- callbacks que esperan;
- caminos de error;
- equilibrio de Disable/Enable/Restore;
- datos compartidos sin sincronización;
- listas JSU/JSUPtrList usadas por audio y juego;
- colas de `JASPortCmd`.

No mantengas el bloqueo global de interrupciones durante una espera potencialmente indefinida.

### FPS

El juego original funciona a 30 Hz. El port soporta 30, 60 y 120 FPS.

Herramientas existentes:

- `sys->mDeltaTime`
- `PC_ORIG_TICK()`
- `PC_ORIG_DT_SCALE()`

Busca contadores de frames, temporizadores que avanzan por `1.0f`, comparaciones float con `==`, eventos observados solamente en fotogramas alternos e interpolaciones fuera del rango.

### 64 bits y endianness

No conviertas punteros a `u32` salvo cuando se trate de una dirección virtual GameCube codificada mediante la infraestructura correspondiente.

Usa `uintptr_t`, handles estables o tablas laterales cuando proceda.

Los recursos de GameCube son big-endian. Comprueba si cada estructura se convierte exactamente una vez.

### Renderer GX

Ten en cuenta:

- fuentes y tipos de texgen;
- matrices de textura y post-textura;
- registros XF `0x500` y `0x1050`;
- orden de etapas TEV;
- selección de mapa y coordenada por etapa;
- wrap;
- iluminación;
- niebla y profundidad;
- copias EFB;
- alineación y layout de UBO;
- cachés de shader y estado.

En esta decompilación los nombres `GX_TG_MTX2X4` y `GX_TG_MTX3X4` están intercambiados respecto a la semántica del hardware. El valor 0 es el modo proyectivo STQ.

## Fase 0 ya terminada: no la repitas

Se conectó la infraestructura de pruebas con CTest.

Cambios realizados:

- `include(CTest)` en `CMakeLists.txt`.
- Creación de ejecutables pequeños para las pruebas existentes.
- Registro de 16 pruebas.
- Las pruebas autónomas también reciben instrumentación ASan.
- `pikmin2_pc --audio-self-test` devuelve actualmente 77 y se registra como omitida porque el backend JAudio nativo todavía no está enlazado.
- README actualizado para reflejar esa omisión.

Archivos modificados durante la fase 0:

- `CMakeLists.txt`
- `pc_port/pc_main.cpp`
- `README.md`

Pruebas registradas:

- `p2_aram_test`
- `p2_envelope_test`
- `p2_postprocess_test`
- `p2_tev_shader_test`
- `p2_menu_repeat_test`
- `p2_pad_axis_test`
- `p2_photo_mode_test`
- `p2_generator_cache_validation_test`
- `p2_camera_snapshot_test`
- `p2_frame_scheduler_test`
- `p2_render_packet_test`
- `p2_render_phase_test`
- `p2_tick_profiler_test`
- `p2_visual_snapshot_test`
- `p2_visual_runtime_test`
- `p2_jaudio_integration_test`

Configuraciones verificadas:

- RelWithDebInfo: compila.
- Debug: compila.
- ASan: compila.
- Los tests autónomos están instrumentados realmente con ASan.

Resultado idéntico en las tres configuraciones:

- 14 pruebas pasan.
- `p2_jaudio_integration_test` se omite.
- `p2_tev_shader_test` falla con 28 comprobaciones.
- Las 14 pruebas correctas no muestran errores de ASan usando `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`.

El fallo TEV denuncia, entre otras cosas:

- uniformes generados que no aparecen en el ubershader;
- incompatibilidad de iluminación;
- ausencia del recorrido esperado de etapas;
- selección incorrecta o no comprobable del mapa y texcoord de cada etapa;
- linealización incorrecta o no encontrada de profundidad para la niebla.

No conviertas esa prueba en `skip`, no rebajes sus comprobaciones y no marques el fallo como esperado. Debe quedar verde mediante una corrección real durante la fase del renderer.

Comandos útiles:

```bash
cmake -S . -B build-nat -DBUILD_TESTING=ON
cmake --build build-nat -j2
ctest --test-dir build-nat --output-on-failure
```

Debug limpio:

```bash
cmake -S . -B /tmp/pikmin2-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build /tmp/pikmin2-debug -j2
ctest --test-dir /tmp/pikmin2-debug --output-on-failure
```

ASan:

```bash
cmake -S . -B /tmp/pikmin2-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON \
  -DPIKMIN2_ASAN=ON
cmake --build /tmp/pikmin2-asan -j2
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  ctest --test-dir /tmp/pikmin2-asan --output-on-failure
```

El árbol heredado genera una cantidad enorme de warnings (`register`, casts de punteros, tipos incompletos, etc.). No intentes arreglarlos todos. Filtra la salida y céntrate en errores nuevos y warnings directamente relacionados con el código modificado.

## Plan de fases restante

Antes de implementar cada fase, vuelve a leer el código relevante, confirma el fallo y añade una prueba de regresión siempre que sea viable.

### Fase 1 — Propiedad de memoria y vida útil de los objetos

Objetivo: eliminar reservas que puedan quedar atrapadas en un `JKRHeap` temporal y prevenir use-after-free al cambiar de sección.

Trabajo:

1. Inventariar globales, estáticos, cachés y objetos longevos dentro de `pc_port` y de los bloques `PIKI_PC_PORT`/`PIKI_P2_HOST`.
2. Revisar contenedores STL que crecen durante el juego.
3. Revisar cualquier `new`, `make_unique`, `make_shared` o asignación indirecta ejecutada desde audio, DVD o renderer.
4. Determinar para cada asignación quién es el propietario, en qué heap se realiza, en qué hilo se destruye y si sobrevive a un cambio de sección.
5. Migrar únicamente las asignaciones peligrosas al heap del host.
6. Asegurar que el cambio de sección, entrada/salida de Piklopedia, reinicio de partida y cierre no dejan punteros colgantes.
7. Añadir pruebas unitarias o instrumentación mínima para reproducir vida útil y liberación.
8. Verificar con ASan.

Criterios de cierre:

- Todas las reservas longevas revisadas tienen propietario y heap explícitos.
- No aparecen errores ASan en las rutas reproducibles.
- No se han convertido indiscriminadamente todas las reservas a `malloc`.
- Se documentan los casos que requieran pruebas manuales dentro del juego.

### Fase 2 — Concurrencia, primitivas OS y prevención de deadlocks

Objetivo: hacer seguras las interacciones entre main, audio, DVD y callback SDL.

Archivos prioritarios:

- `pc_port/dolphin_stubs/p2_os_host.cpp`
- `pc_port/audio/`
- rutas de `JASAudioThread`
- hilo DVD y colas de mensajes
- listas o estructuras compartidas del parche host

Trabajo:

1. Dibujar una jerarquía real de locks.
2. Seguir cada camino que combine interrupciones, mutex OS, mutex internos, bloqueo del dispositivo SDL y espera en condición o cola.
3. Revisar las esperas con el bloqueo global de interrupciones adquirido.
4. Auditar el equilibrio de `OSDisableInterrupts`, `OSEnableInterrupts` y `OSRestoreInterrupts`, incluidos retornos anticipados.
5. Revisar destrucción de mutex, colas y threads mientras otros hilos aún pueden accederlos.
6. Revisar races sobre estados, flags, contadores, listas y callbacks.
7. Evitar asignaciones y bloqueos potencialmente largos dentro del callback de audio.
8. Añadir tests multihilo focalizados en colas, wakeups, cierre y saturación.
9. Usar ThreadSanitizer si el proyecto lo permite; si no, explicar el bloqueo técnico y usar stress tests controlados.

Criterios de cierre:

- Orden de locks definido y coherente.
- Ninguna espera bloqueante mantiene innecesariamente el lock global.
- Cierre y wakeup de colas comprobados.
- No hay Disable/Restore desequilibrados en las rutas modificadas.
- Tests repetidos sin cuelgues.

### Fase 3 — Portabilidad de 64 bits y endianness

Objetivo: eliminar truncamientos y conversiones incorrectas de datos GameCube.

Trabajo:

1. Buscar casts entre punteros y `u32`, `s32`, registros de 16 bits o handles.
2. Clasificarlos como dirección virtual GameCube intencionada, offset dentro de un recurso, handle o truncamiento accidental.
3. Usar `uintptr_t`, offsets explícitos o tablas laterales según el caso.
4. Verificar `pc_host_to_gc_phys`/`pc_host_from_gc_phys` y la duración de sus entradas.
5. Revisar cachés que utilicen punteros truncados como clave.
6. Revisar estructuras de disco y conversiones big-endian.
7. Añadir tests con direcciones superiores a 4 GiB cuando sea posible.
8. Verificar Linux x86-64 y no introducir suposiciones incompatibles con Windows.

Criterios de cierre:

- No quedan truncamientos accidentales en las rutas host revisadas.
- Los valores de 32 bits que representan direcciones GC están documentados y codificados.
- Las pruebas cubren round-trip de direcciones y recursos endian.

### Fase 4 — Independencia de 30/60/120 FPS

Objetivo: que la lógica y animaciones produzcan el mismo resultado temporal en todos los modos.

Trabajo:

1. Revisar contadores por frame y temporizadores float.
2. Sustituir incrementos dependientes de frames por `mDeltaTime`, `PC_ORIG_DT_SCALE()` o `PC_ORIG_TICK()`, según la semántica.
3. Revisar comparaciones con `==` y umbrales que pueden saltarse.
4. Revisar contactos, triggers y acciones que solo ocurren en ticks originales.
5. Revisar interpolaciones y wrap de animaciones.
6. Comparar secuencias equivalentes a 30, 60 y 120 FPS.
7. Añadir tests deterministas para el scheduler y la lógica extraída.
8. No acelerar ni ralentizar deliberadamente la simulación para ocultar el problema.

Criterios de cierre:

- Misma duración y mismos eventos lógicos en los tres modos.
- Sin acumulación significativa de deriva.
- Tests parametrizados para 30/60/120 FPS.

### Fase 5 — Renderer GX→OpenGL y shaders TEV

Objetivo: corregir la semántica GX y dejar verde `p2_tev_shader_test`.

Archivos prioritarios:

- `pc_port/gl/pc_gfx.cpp`
- `pc_port/gl/pc_gfx.h`
- `pc_port/gl/pc_tev_shader.cpp`
- `pc_port/gl/pc_gx_lighting_glsl.h`
- `pc_port/gl/pc_texpack.cpp`
- `pc_port/gl/pc_postprocess.cpp`

Trabajo:

1. Determinar si el runtime usa shader especializado, ubershader o ambos para cada material.
2. Unificar el contrato de uniforms/UBO entre generador y ubershader.
3. Validar layout, alineación `std140`, offsets, tamaños y actualización del UBO.
4. Corregir el recorrido y orden de etapas TEV.
5. Asegurar que cada etapa usa su propio `texMap` y `texCoord`.
6. Revisar konst colors, alpha, swap tables, comparaciones y clamps.
7. Revisar iluminación por canal y alpha.
8. Implementar o modelar correctamente matrices post-tex XF.
9. Revisar la peculiaridad de `GX_TG_MTX2X4`/`GX_TG_MTX3X4`.
10. Corregir niebla y linealización de profundidad dentro del rango GameCube.
11. Revisar copias EFB, wrap, resolución y orientación.
12. No actualizar la prueba para aceptar un shader incorrecto.
13. Añadir casos de regresión por etapa y combinaciones mínimas.

Criterios de cierre:

- `p2_tev_shader_test` pasa completo.
- `p2_postprocess_test` continúa pasando.
- No hay errores de compilación o enlace GLSL.
- No hay lecturas fuera de rango ni UBO con layout incoherente.
- Se realizan pruebas visuales si existen assets y entorno gráfico; si no, se documenta.

### Fase 6 — Audio, ARAM, DSP y DVD asíncrono

Objetivo: eliminar carreras, buffers inválidos y rutas de audio no verificables.

Trabajo:

1. Revisar propietario y duración de buffers ARAM/DSP.
2. Revisar comunicación entre hilo de audio, callback SDL y main.
3. Revisar operaciones DVD que rellenan memoria usada por audio.
4. Verificar cierre, cambio de escena y dispositivo de audio ausente.
5. Evitar liberar memoria mientras existan callbacks pendientes.
6. Conectar correctamente el backend JAudio nativo si es viable dentro del alcance.
7. Si se conecta, sustituir el `skip` de `--audio-self-test` por la prueba integrada real.
8. La prueba debe seguir devolviendo 77 solamente cuando falten assets legalmente extraídos o exista una condición de omisión explícita.
9. Mantener verdes `p2_aram_test` y `p2_envelope_test`.
10. Añadir stress tests para shutdown, mute/unmute y colas saturadas.

Criterios de cierre:

- Sin errores ASan en pruebas de audio.
- Sin callback accediendo a objetos destruidos.
- Shutdown idempotente o claramente protegido.
- Estado de la integración JAudio documentado con precisión.

### Fase 7 — Guardas, trucos, logros y estados excepcionales

Objetivo: impedir crashes introducidos por rutas que no existían en GameCube.

Archivos prioritarios:

- `pc_port/settings/pc_settings.cpp`
- `pc_port/settings/pc_settings_p2_shim.cpp`
- `pc_port/pc_p2_cheats.cpp`
- `pc_port/pc_achievements.cpp`
- cámaras, cinemáticas y `pcHoldFrameEntry`

Trabajo:

1. Revisar acciones F1/F9 que saltan progresión normal.
2. Validar punteros a sección, cámara, jugador, curso y datos de guardado.
3. Revisar llamadas durante transiciones y fotogramas intermedios.
4. Asegurar que los trucos no dejan estados parciales.
5. Revisar bucles que buscan un elemento disponible y pueden no terminar.
6. Revisar índices de zona, cueva, Pikmin, tesoro y logro.
7. Revisar persistencia y compatibilidad del guardado.
8. Añadir pruebas unitarias para lógica extraíble y validaciones.
9. No ocultar estados inválidos con guardas silenciosas si requieren rollback.

Criterios de cierre:

- Las acciones fuera de contexto fallan de forma segura.
- No hay bucles sin límite.
- No se corrompe el guardado.
- Los trucos y logros no desreferencian objetos pertenecientes a otra sección.

### Fase 8 — Validación final y endurecimiento

Objetivo: comprobar el conjunto completo sin introducir regresiones.

Trabajo:

1. Ejecutar compilaciones RelWithDebInfo, Debug y ASan.
2. Ejecutar toda la batería CTest.
3. Investigar cualquier diferencia entre configuraciones.
4. Ejecutar los escenarios manuales disponibles: arranque, menú principal, carga y guardado, entrada y salida de una zona, entrada y salida de una cueva, cambio de sección, Piklopedia, cinemáticas, 30/60/120 FPS, dispositivo de audio disponible y ausente, y cierre normal.
5. Revisar el diff final para detectar cambios cosméticos innecesarios, código de depuración olvidado, logs excesivos, APIs sin usar y pruebas rebajadas.
6. Actualizar documentación solamente cuando describa comportamiento real.
7. Entregar una tabla final de fallo, corrección, prueba y riesgo restante.

Criterios de cierre:

- Todos los tests aplicables pasan.
- Las omisiones están justificadas.
- ASan no detecta errores en las rutas ejecutadas.
- No quedan fallos conocidos ocultos o convertidos en skips.
- Los riesgos no comprobables manualmente están enumerados claramente.

## Forma de trabajar en cada fase

Para cada fallo:

1. Confirma la causa siguiendo la ruta completa.
2. Describe brevemente el escenario que lo dispara.
3. Añade primero una prueba que falle, cuando sea viable.
4. Implementa el cambio mínimo correcto.
5. Ejecuta pruebas focalizadas.
6. Ejecuta después CTest completo.
7. Ejecuta ASan si hay memoria o vida útil implicada.
8. Revisa el diff para no arrastrar cambios ajenos.
9. Informa y detente.

No hagas refactors cosméticos. No rebajes pruebas. No marques como resuelto algo que solamente no has podido reproducir.

Empieza ahora por la fase 1. Antes de modificar archivos, inspecciona `git status`, los diffs existentes y las instrucciones locales del repositorio.
