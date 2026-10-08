#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <string>

namespace pikmin {
namespace launcher {

struct DiscIdentity {
    std::string gameId;
    std::uint8_t revision = 0;
};

// Un disco que el port sabe usar. La lista vive en gamecube_image.cpp.
struct KnownDisc {
    const char* gameId;
    std::uint8_t revision;
    const char* description;   // como se le enseña al usuario
    const char* sha256;        // volcado integro de referencia
    // Codigos de idioma que trae el disco, en el orden en que el juego los
    // numera. Una sola entrada significa que no hay nada que elegir.
    const char* const* languages;
    int languageCount;
    // Nombre base del ejecutable compilado para esta version. El juego se
    // compila desde la decompilacion y esa decompilacion es condicional segun
    // la version, asi que cada disco necesita el suyo.
    const char* executable;
};

// El disco de la lista que coincide con la identidad dada, o nullptr.
const KnownDisc* findKnownDisc(const DiscIdentity& identity);

using ProgressCallback = std::function<void(std::uint32_t current, std::uint32_t total,
                                            const std::string& path)>;

bool inspectGameCubeImage(const std::filesystem::path& image, DiscIdentity& identity,
                          std::string& error);
// Sobre un stream ya abierto y posicionable: en Android la imagen llega como
// descriptor del selector de archivos, que no se puede reabrir por ruta.
bool inspectGameCubeImage(std::istream& input, DiscIdentity& identity, std::string& error);

// `verifyWrites` relee cada archivo tras escribirlo y compara su contenido con
// el que salió de la imagen. Detecta daños del medio de destino, que producen
// archivos del tamaño correcto con contenido incorrecto. Cuesta una lectura
// extra por archivo, casi siempre servida por la caché del sistema.
bool extractGameCubeImage(const std::filesystem::path& image,
                          const std::filesystem::path& destination,
                          std::string& error, ProgressCallback progress = {},
                          bool verifyWrites = true);
bool extractGameCubeImage(std::istream& input, std::uint64_t imageSize,
                          const std::filesystem::path& destination,
                          std::string& error, ProgressCallback progress = {},
                          bool verifyWrites = true);

bool isSupportedPikminDisc(const DiscIdentity& identity, std::string& error);

// Comprueba que la imagen coincide byte a byte con un volcado conocido de
// Pikmin USA Rev. 1. Detecta copias dañadas cuya FST sigue siendo legible: el
// caso que produce una instalación de aspecto correcto y un juego que aborta
// después con errores incomprensibles.
//
// `progress` recibe el porcentaje leído. Devuelve false y describe el problema
// si el hash no coincide o la imagen no se puede leer entera.
bool verifyImageIntegrity(const std::filesystem::path& image, std::string& error,
                          const std::function<void(std::uint32_t percent)>& progress = {});

// SHA-256 de la imagen, en hexadecimal. Útil para diagnósticos y para informar
// al usuario de qué volcado tiene realmente.
bool hashImage(const std::filesystem::path& image, std::string& hexDigest,
               std::string& error,
               const std::function<void(std::uint32_t percent)>& progress = {});

} // namespace launcher
} // namespace pikmin
