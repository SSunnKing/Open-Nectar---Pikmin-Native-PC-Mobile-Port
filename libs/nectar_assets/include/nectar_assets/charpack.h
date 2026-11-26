#pragma once

// Personajes y Pikmin propios (.nectarchar): un zip con character.json
// (nombre, autor, a quién reemplaza), model.nhm (la malla con su rig, ver
// pc_port/mods/pc_hd_models.cpp) y portrait.png (su tarjeta).
// Los capitanes se instalan en <juego>/Load/Characters/<id>/ (un hueco del
// selector de capitán cada uno); los Pikmin en <juego>/Load/Pikmin/<id>/ (se
// eligen por color en Opciones > Gráficos o en el launcher).
//
// Lo usan el creador de modelos (exportar e instalar), el launcher (instalar)
// y el juego (listar). Sin dependencias del juego.

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

constexpr int kCharacterFormat = 2; // 2: campo "target" (los de la 1 son capitanes)
constexpr const char* kCharacterExtension = ".nectarchar";

/// A quién reemplaza un pack.
constexpr const char* kTargetCaptain      = "captain";       ///< un capitán más (esqueleto de Olimar)
constexpr const char* kTargetPikminRed    = "pikmin_red";    ///< los Pikmin rojos
constexpr const char* kTargetPikminYellow = "pikmin_yellow";
constexpr const char* kTargetPikminBlue   = "pikmin_blue";
constexpr const char* kTargetPikminPurple = "pikmin_purple"; ///< solo Pikmin 2
constexpr const char* kTargetPikminWhite  = "pikmin_white";  ///< solo Pikmin 2
constexpr const char* kTargetPikminBulbmin = "pikmin_bulbmin"; ///< solo Pikmin 2
constexpr const char* kTargetPikminAll    = "pikmin_all"; ///< todos los colores, teñido de su color

struct CharacterInfo {
	std::string id;     ///< nombre de su carpeta (letras, números y guiones)
	std::string name;   ///< el que sale en el selector
	std::string author;
	std::string target   = kTargetCaptain;
	std::string skeleton = "olimar"; ///< "olimar" (capitanes) o "pikmin" (los tres colores comparten rig)
	int format           = kCharacterFormat;
	std::string dir; ///< carpeta instalada (solo al listar)

	bool isPikmin() const { return target != kTargetCaptain; }
	/// ¿Sirve para los Pikmin de ese color ("red", "yellow", "blue")?
	bool forPikmin(const std::string& color) const { return target == kTargetPikminAll || target == "pikmin_" + color; }
};

struct CharacterFiles {
	CharacterInfo info;
	std::vector<std::uint8_t> nhm, portrait; ///< portrait puede ir vacío
	/// El proyecto del creador de modelos, para volver a editarlo: rutas
	/// relativas dentro de project/ ("project.json", "source/modelo.glb"...).
	std::vector<std::pair<std::string, std::vector<std::uint8_t>>> project;
};

/// ¿Vale como ruta dentro de project/? (relativa, sin "..", sin "\\").
bool safeProjectPath(const std::string& path);
/// Lee la carpeta project/ de un personaje instalado (vacío si no tiene).
bool readProjectFolder(const std::string& characterDir, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out);

/// Id para la carpeta a partir del nombre ("Capitán Nox" -> "capitan-nox-1a2b").
std::string characterId(const std::string& name, const std::vector<std::uint8_t>& nhm);

/// Comprueba la cabecera del pack (NHM1, versión 1-3) sin cargarlo entero.
bool validNhm(const std::vector<std::uint8_t>& nhm, std::string& error);

bool writeCharacterPack(const std::string& path, const CharacterFiles& files, std::string& error);
bool readCharacterPack(const std::string& path, CharacterFiles& files, std::string& error);

/// Carpeta Load/Characters de un juego (gameDir = donde están assets/ y Load/).
std::string charactersDir(const std::string& gameDir);
/// Carpeta Load/Pikmin.
std::string pikminModelsDir(const std::string& gameDir);
/// Instala (o reemplaza el del mismo id). En `installedDir`, su carpeta.
bool installCharacter(const std::string& gameDir, const CharacterFiles& files, std::string& installedDir, std::string& error);
bool installCharacterPack(const std::string& gameDir, const std::string& packPath, CharacterInfo& info, std::string& error);
/// Lee un personaje instalado (su carpeta: character.json, model.nhm,
/// portrait.png y project/) para volver a empaquetarlo con writeCharacterPack.
bool readInstalledCharacter(const std::string& characterDir, CharacterFiles& files, std::string& error);
/// Los capitanes instalados y válidos, por nombre.
std::vector<CharacterInfo> listCharacters(const std::string& gameDir);
/// Los Pikmin instalados y válidos, por nombre.
std::vector<CharacterInfo> listPikminModels(const std::string& gameDir);
/// Quita un capitán o un Pikmin (busca el id en las dos carpetas).
bool removeCharacter(const std::string& gameDir, const std::string& id, std::string& error);

} // namespace nectar
