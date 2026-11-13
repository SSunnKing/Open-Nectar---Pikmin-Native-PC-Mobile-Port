#include "config.h"

#include "launcher_platform.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "nectar_mapcore/i18n.h"

namespace nm {

using nectar::tr;

namespace fs = std::filesystem;
using json   = nlohmann::json;

std::string Config::directory()
{
#ifdef _WIN32
	if (const char* d = std::getenv("APPDATA")) return (fs::path(d) / "nectar_mapmaker").string();
#else
	if (const char* d = std::getenv("XDG_CONFIG_HOME")) {
		if (*d) return (fs::path(d) / "nectar_mapmaker").string();
	}
	if (const char* h = std::getenv("HOME")) return (fs::path(h) / ".config" / "nectar_mapmaker").string();
#endif
	return "nectar_mapmaker_config";
}

std::string Config::gameDir() const { return fs::path(assetsDir).parent_path().string(); }

bool Config::load()
{
	std::ifstream f(fs::path(directory()) / "config.json");
	if (!f) return false;
	try {
		const json j = json::parse(f);
		assetsDir    = j.value("assetsDir", "");
		gameExe      = j.value("gameExe", "");
		lastMap      = j.value("lastMap", "");
		recentMaps   = j.value("recentMaps", std::vector<std::string>());
		favorites    = j.value("favorites", std::vector<std::string>());
		recents      = j.value("recents", std::vector<std::string>());
		pieces       = j.value("pieces", std::vector<std::pair<std::string, std::string>>());
		snap         = j.value("snap", true);
		tutorialDone = j.value("tutorialDone", false);
		language     = j.value("language", std::string("en"));
		snapStep     = j.value("snapStep", 25.0f);
		snapAngle    = j.value("snapAngle", 15.0f);
		toolboxWidth = j.value("toolboxWidth", 300.0f);
		sidePanelWidth = j.value("sidePanelWidth", 360.0f);
		sideSplit    = j.value("sideSplit", 0.58f);
		placeRecents = j.value("placeRecents", std::vector<std::string>());
		showToolbox  = j.value("showToolbox", true);
		showSidePanel = j.value("showSidePanel", true);
		ignoreDecorSelection = j.value("ignoreDecorSelection", false);
		localGizmo = j.value("localGizmo", false);
	} catch (const std::exception&) {
		return false;
	}
	return true;
}

bool Config::save() const
{
	std::error_code ec;
	fs::create_directories(directory(), ec);
	const json j = { { "assetsDir", assetsDir }, { "gameExe", gameExe }, { "lastMap", lastMap }, { "recentMaps", recentMaps }, { "favorites", favorites },
		             { "recents", recents },     { "snap", snap },       { "snapStep", snapStep }, { "snapAngle", snapAngle },
		             { "tutorialDone", tutorialDone }, { "language", language },
		             { "toolboxWidth", toolboxWidth }, { "sidePanelWidth", sidePanelWidth }, { "sideSplit", sideSplit }, { "placeRecents", placeRecents },
		             { "showToolbox", showToolbox }, { "showSidePanel", showSidePanel },
		             { "ignoreDecorSelection", ignoreDecorSelection }, { "localGizmo", localGizmo }, { "pieces", pieces } };
	// Las rutas pueden traer bytes que no son UTF-8 (#326): se sustituyen en
	// vez de lanzar una excepción al guardar.
	std::string text;
	try {
		text = j.dump(2, ' ', false, json::error_handler_t::replace);
	} catch (const std::exception&) {
		return false;
	}
	std::ofstream f(fs::path(directory()) / "config.json");
	if (!f) return false;
	f << text;
	return true;
}

std::string resolveAssetsDir(const std::string& chosen, std::string& why)
{
	if (chosen.empty()) {
		why = tr("elige una carpeta");
		return "";
	}
	std::error_code ec;
	fs::path p = fs::absolute(fs::path(chosen), ec);
	// Se acepta la del juego, assets o assets/dataDir.
	if (p.filename() == "dataDir") p = p.parent_path();
	if (fs::is_directory(p / "assets" / "dataDir", ec) && !fs::is_directory(p / "dataDir", ec)) p = p / "assets";
	if (!fs::is_directory(p / "dataDir", ec)) {
		why = tr("no hay ninguna carpeta dataDir dentro (¿has extraído el disco con el launcher?)");
		return "";
	}
	for (const char* need : { "dataDir/courses/practice/practice.mod", "dataDir/stages/chal0.ini" }) {
		if (!fs::exists(p / need, ec)) {
			why = std::string(tr("falta ")) + need + tr(" (los datos están incompletos)");
			return "";
		}
	}
	return fs::canonical(p, ec).string();
}

std::string guessAssetsDir()
{
	std::vector<fs::path> candidates;
	const fs::path exe = pikmin::launcher::platform::executablePath();
	if (!exe.empty()) {
		// Junto al editor, o en el árbol de fuentes (build/pikmin1/bin -> games/pikmin1).
		candidates.push_back(exe.parent_path() / "assets");
		candidates.push_back(exe.parent_path().parent_path() / "assets");
		candidates.push_back(exe.parent_path().parent_path().parent_path().parent_path() / "games" / "pikmin1" / "assets");
	}
	std::error_code ec;
	candidates.push_back(fs::current_path(ec) / "assets");
	candidates.push_back(fs::current_path(ec) / "games" / "pikmin1" / "assets");
	for (const fs::path& c : candidates) {
		std::string why;
		const std::string ok = resolveAssetsDir(c.string(), why);
		if (!ok.empty()) return ok;
	}
	return "";
}

std::string guessGameExe()
{
	// Junto al editor o en el directorio actual (el launcher lo abre en la
	// carpeta del juego).
	std::error_code ec;
	std::vector<fs::path> dirs { fs::current_path(ec) };
	const fs::path exe = pikmin::launcher::platform::executablePath();
	if (!exe.empty()) dirs.insert(dirs.begin(), exe.parent_path());
	for (const fs::path& dir : dirs) {
		for (const char* name : { "nectar", "nectar.exe", "nectar-pal", "nectar-pal.exe", "pikmin_pc", "pikmin_pc.exe" }) {
			const fs::path p = dir / name;
			if (fs::is_regular_file(p, ec)) return fs::absolute(p, ec).string();
		}
	}
	return "";
}

bool launchGame(const std::string& exe, const std::string& cwd, const std::vector<std::string>& args, std::string& error)
{
#ifdef _WIN32
	std::wstring cmd = L"\"" + fs::path(exe).wstring() + L"\"";
	for (const std::string& a : args) cmd += L" \"" + fs::path(a).wstring() + L"\"";
	STARTUPINFOW si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};
	if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fs::path(cwd).wstring().c_str(), &si, &pi)) {
		error = tr("no se pudo abrir el juego (") + std::to_string(GetLastError()) + ")";
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
#else
	const pid_t pid = fork();
	if (pid < 0) {
		error = tr("no se pudo abrir el juego (fork)");
		return false;
	}
	if (pid == 0) {
		// Nieto huérfano: el editor no tiene que esperarlo ni recogerlo.
		if (fork() != 0) _exit(0);
		setsid();
		if (chdir(cwd.c_str()) != 0) _exit(127);
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>(exe.c_str()));
		for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);
		execv(exe.c_str(), argv.data());
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return true;
#endif
}

} // namespace nm
