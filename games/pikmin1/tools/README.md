# tools

Herramientas de diagnóstico. No forman parte del juego ni del build.

## h4m_decode_frame.c

Decodifica **un** fotograma de un `.h4m` fuera del juego y vuelca los planos
YUV. Sirve para tener verdad de referencia sobre el contenido del vídeo sin
depender de lo que se ve en pantalla.

```sh
gcc -O1 -o /tmp/h4m1 tools/h4m_decode_frame.c src/hvqm4dec/hvqm4dec.c \
    -Iinclude -Ipc_port -DPIKI_PC_PORT=1 -DVERSION_GPIE01_01 -lm

# argumentos: archivo  offsetDatos  tamaño  ancho  alto  salida
/tmp/h4m1 assets/dataDir/MovieData/cntA_S.h4m 2606132 103464 640 480 /tmp/frame.yuv
```

El offset y el tamaño salen de recorrer el contenedor: cabecera de archivo de
0x44, cabecera de grupo de 0x14, y luego registros con cabecera de 8 bytes
(tipo u16, flags u16, tamaño u32, todo big-endian). El registro de arriba es la
imagen I del segundo grupo; **la del primero es negra a propósito**, la película
arranca fundida.

El volcado son tres planos: Y de 640×480, luego Cb y Cr de 320×240. Con
BT.601 sobre ese fotograma el color medio es `(110, 112, 73)` — verde oliva,
que es lo que debe verse.
