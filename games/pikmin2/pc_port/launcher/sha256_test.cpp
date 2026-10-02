// Vectores de prueba de SHA-256 (FIPS 180-4 / NIST).
//
// La verificación de integridad del instalador depende por completo de esta
// implementación: si el hash fuese incorrecto, rechazaría imágenes buenas o,
// peor, aceptaría imágenes dañadas.

#include "sha256.h"

#include <cstdio>
#include <string>

using pikmin::launcher::Sha256;
using pikmin::launcher::toHex;

namespace {

std::string hashOf(const std::string& text)
{
    Sha256 hash;
    hash.update(text.data(), text.size());
    return toHex(hash.finish());
}

int check(const char* label, const std::string& got, const std::string& want, int& failures)
{
    if (got != want) {
        std::printf("FALLO %s\n  esperado %s\n  obtenido %s\n", label, want.c_str(), got.c_str());
        ++failures;
    }
    return failures;
}

} // namespace

int main()
{
    int failures = 0;

    check("cadena vacía", hashOf(""),
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", failures);
    check("abc", hashOf("abc"),
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", failures);
    // 56 bytes: ejercita justo el límite donde el relleno no cabe en el bloque.
    check("448 bits", hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", failures);
    // 112 bytes: dos bloques más el relleno en uno adicional.
    check("896 bits",
          hashOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                 "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", failures);

    // Un millón de 'a' entregado en trozos de 1.000: comprueba el troceado en
    // bloques de 64 bytes cuando la entrada no está alineada con ellos.
    {
        Sha256 hash;
        const std::string chunk(1000, 'a');
        for (int i = 0; i < 1000; ++i) hash.update(chunk.data(), chunk.size());
        check("un millón de 'a'", toHex(hash.finish()),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", failures);
    }

    // El mismo mensaje entregado byte a byte debe dar el mismo resultado que
    // entregado de una vez: es como lo consume el extractor, a trozos.
    {
        const std::string message = "Nectar verifica la integridad de la imagen";
        Sha256 hash;
        for (char c : message) hash.update(&c, 1);
        check("entrega byte a byte", toHex(hash.finish()), hashOf(message), failures);
    }

    if (failures == 0) std::printf("sha256: todos los vectores correctos\n");
    return failures == 0 ? 0 : 1;
}
