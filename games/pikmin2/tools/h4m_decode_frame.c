// Decodifica el primer fotograma I de un .h4m y vuelca los planos YUV.
// Sin juego, sin ventana: verdad de referencia para decidir el empaquetado.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hvqm4.h"

int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb");
    long recOff = atol(argv[2]);        // offset del registro de video (datos, tras la cabecera de 8)
    long recSize = atol(argv[3]);
    int w = atoi(argv[4]), h = atoi(argv[5]);

    unsigned char* rec = malloc(recSize);
    fseek(f, recOff, SEEK_SET);
    fread(rec, 1, recSize, f);
    fclose(f);

    VideoInfo info;
    info.width = w; info.height = h;
    info.h_sampling_rate = 2; info.v_sampling_rate = 2;

    static SeqObj obj;
    HVQM4InitDecoder();
    HVQM4InitSeqObj(&obj, &info);
    void* work = malloc(HVQM4BuffSize(&obj));
    HVQM4SetBuffer(&obj, work);

    long outSize = (long)w * h + 2L * (w / 2) * (h / 2);
    unsigned char* out = calloc(1, outSize);

    // Los 4 primeros bytes del registro son la cabecera de video; el codigo
    // empieza despues, igual que hace hvqm_play.
    HVQM4DecodeIpic(&obj, rec + 4, out);

    FILE* o = fopen(argv[6], "wb");
    fwrite(out, 1, outSize, o);
    fclose(o);
    fprintf(stderr, "escritos %ld bytes (Y=%d*%d, U y V de %d*%d)\n", outSize, w, h, w/2, h/2);
    return 0;
}
