#pragma once

// Ajustes del editor que sobreviven entre sesiones:
// ~/.config/nectar_mapmaker/config.json (%APPDATA%\nectar_mapmaker en Windows).

#include <string>
#include <vector>

namespace nm {

struct Config {
	std::string assetsDir; ///< la carpeta "assets" del juego (contiene dataDir)
	std::string gameExe;   ///< ejecutable del juego para "Probar"
	std::vector<std::string> favorites, recents; ///< assets del catálogo
	/// Mi biblioteca de piezas recortadas de escenarios: (nombre, "cut:...").
	std::vector<std::pair<std::string, std::string>> pieces;
	std::string lastMap;   ///< último map.json abierto o guardado
	std::vector<std::string> recentMaps; ///< map.json abiertos o guardados, el más nuevo primero
	bool snap = true;
	bool tutorialDone = false;
	std::string language = "en"; ///< "en" o "es"
	float snapStep = 25.0f, snapAngle = 15.0f;
	// Disposición del editor. Se guardan en píxeles lógicos de ImGui para que
	// el usuario conserve el espacio dedicado a herramientas y propiedades.
	float toolboxWidth = 300.0f, sidePanelWidth = 360.0f;
	bool showToolbox = true, showSidePanel = true;
	bool ignoreDecorSelection = false;
	bool localGizmo = false;

	std::string dataDir() const { return assetsDir + "/dataDir"; }
	/// La carpeta del juego: donde están assets/ y user/ (el juego se abre ahí).
	std::string gameDir() const;
	/// Carpeta de los mapas que ve el juego en su menú.
	std::string userMapsDir() const { return gameDir() + "/user/maps/vs"; }

	bool load();
	bool save() const;
	static std::string directory();
};

/// Normaliza lo que elige el usuario: acepta la carpeta del juego, "assets"
/// o "assets/dataDir" y devuelve la ruta de "assets". Vacía si no hay dataDir
/// con lo que hace falta; en `why`, el motivo.
std::string resolveAssetsDir(const std::string& chosen, std::string& why);

/// Busca "assets" en los sitios habituales (junto al editor, el directorio
/// actual, el árbol de fuentes).
std::string guessAssetsDir();
/// Busca el ejecutable del juego junto al editor.
std::string guessGameExe();

/// Lanza el juego sin esperar (cwd = carpeta del juego). false con el motivo.
bool launchGame(const std::string& exe, const std::string& cwd, const std::vector<std::string>& args, std::string& error);

} // namespace nm
