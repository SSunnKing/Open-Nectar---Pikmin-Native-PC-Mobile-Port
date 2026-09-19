/** Native filesystem implementation of the GameCube memory-card API. */
#include "Dolphin/card.h"
#include "Dolphin/dvd.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr std::uintmax_t kCapacity = 16u * 1024u * 1024u;
constexpr s32 kSectorSize = 0x2000;
// El hilo trabajador de la tarjeta (CardUtilMain) escribe estos resultados
// mientras el hilo principal los consulta en bucles de espera activa como
// MemoryCard::waitWhileBusy(), cuyo cuerpo esta vacio. Con tipos normales eso
// es una carrera de datos y, bajo LTO, un bucle infinito: el compilador saca la
// carga fuera del bucle. Atomicos relajados cuestan lo mismo en x86 y obligan a
// releer memoria en cada vuelta.
std::atomic<s32> sLastResult[2] = { CARD_RESULT_READY, CARD_RESULT_READY };
std::atomic<s32> sTransferred[2] = {};

bool validChannel(s32 channel) { return channel >= 0 && channel < 2; }

// Where the cards live. This used to be the relative path "save", which meant
// the memory card followed the working directory: the launcher chdir()s into
// the data root before starting the game, but running the binary straight from
// Steam or a file manager leaves the working directory somewhere else, and the
// two ended up with different cards. Players saved at sunset, relaunched the
// other way, and found nothing.
//
// Resolved once, and never from the working directory unless there is already
// a card there.
// The folder the game was installed into. The launcher copies the game binary
// into its data root, so the executable's own directory is that install
// folder on both systems -- and unlike the working directory it does not
// change with how the game was started.
fs::path installDir()
{
	if (const char* env = std::getenv("NECTAR_EXECUTABLE_PATH"); env != nullptr && *env != '\0')
		return fs::path(env).parent_path();
	if (const char* env = std::getenv("PIKMIN_EXECUTABLE_PATH"); env != nullptr && *env != '\0')
		return fs::path(env).parent_path();
#if defined(_WIN32)
	std::vector<wchar_t> path(32768); // Windows long-path limit
	const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (count > 0 && count < path.size()) return fs::path(std::wstring(path.data(), count)).parent_path();
#elif defined(__linux__)
	std::vector<char> path(4096);
	const ssize_t count = readlink("/proc/self/exe", path.data(), path.size() - 1);
	if (count > 0) { path[static_cast<std::size_t>(count)] = '\0'; return fs::path(path.data()).parent_path(); }
#endif
	return {};
}

// Per-user data folder, for installs whose own folder cannot be written to
// (Program Files, /usr, /opt, an AppImage's read-only mount, Proton prefixes
// with a read-only game dir). Issue #34: on Linux the card was written beside
// the executable, silently failed there and the progress was gone on restart.
fs::path userDataSaveDir()
{
#if defined(_WIN32)
	if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0')
		return fs::path(local) / "Nectar" / "save";
#else
	if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
		return fs::path(xdg) / "pikmin-native" / "save";
	if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
		return fs::path(home) / ".local" / "share" / "pikmin-native" / "save";
#endif
	return {};
}

// True when files can actually be created under `dir` (creating it if needed).
bool writableDir(const fs::path& dir)
{
	std::error_code error;
	fs::create_directories(dir, error);
	if (error) return false;
	const fs::path probe = dir / ".write_test";
	{
		std::ofstream out(probe, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << '1';
		if (!out) return false;
	}
	fs::remove(probe, error);
	return true;
}

fs::path saveRoot()
{
	static const fs::path resolved = [] {
		std::error_code error;
		// An explicit override always wins; portable setups can pin it.
		if (const char* env = std::getenv("NECTAR_SAVE_DIR"); env != nullptr && *env != '\0')
			return fs::path(env);

		// Where the card belongs: beside the installed game.
		const fs::path install = installDir();
		const fs::path preferred = install.empty() ? fs::path("save") : install / "save";
		if (fs::exists(preferred / "card0", error)) return preferred;

		// Places earlier builds wrote to. A card in one of these is somebody's
		// progress, so find it and bring it along rather than start empty.
		std::vector<fs::path> legacy;
		legacy.emplace_back("save"); // relative to the working directory
#if defined(_WIN32)
		if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0')
			legacy.push_back(fs::path(local) / "Nectar" / "save");
		if (const char* roaming = std::getenv("APPDATA"); roaming != nullptr && *roaming != '\0')
			legacy.push_back(fs::path(roaming) / "OpenNectar" / "save");
#else
		if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
			legacy.push_back(fs::path(xdg) / "pikmin-native" / "save");
		if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
			legacy.push_back(fs::path(home) / ".local" / "share" / "pikmin-native" / "save");
#endif
		for (const fs::path& candidate : legacy) {
			if (fs::equivalent(candidate, preferred, error) && !error) continue;
			if (!fs::exists(candidate / "card0", error)) continue;
			// Copy rather than move: if anything goes wrong the original is
			// still there, and an orphaned folder is cheaper than a lost save.
			fs::create_directories(preferred, error);
			std::error_code copyError;
			fs::copy(candidate, preferred,
			         fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyError);
			if (!copyError && fs::exists(preferred / "card0", error)) {
				std::printf("[PC Port] Moved your memory card into the game folder:\n  from %s\n  to   %s\n",
				            candidate.string().c_str(), preferred.string().c_str());
				return preferred;
			}
			// Could not write beside the game -- read-only install, most likely.
			return candidate;
		}
		if (writableDir(preferred)) return preferred;
		// The game folder is read-only: keep the card in the user's data
		// folder instead of pretending to save (issue #34).
		const fs::path fallback = userDataSaveDir();
		if (!fallback.empty() && writableDir(fallback)) {
			std::printf("[PC Port] Game folder is not writable; memory card lives in %s\n",
			            fallback.string().c_str());
			return fallback;
		}
		return preferred;
	}();
	return resolved;
}

fs::path root(s32 channel) { return saveRoot() / (channel == 0 ? "card0" : "card1"); }
fs::path dataPath(s32 channel, const std::string& name) { return root(channel) / name; }
fs::path metaPath(s32 channel, const std::string& name) { return root(channel) / (".meta_" + name); }

bool ensureCard(s32 channel)
{
	if (!validChannel(channel)) return false;
	std::error_code error;
	fs::create_directories(root(channel), error);
	return !error;
}

std::string safeName(const char* source)
{
	if (!source) return {};
	std::string name(source, strnlen(source, CARD_FILENAME_MAX));
	for (char& c : name) if (c == '/' || c == '\\') c = '_';
	if (name == "." || name == "..") name.insert(name.begin(), '_');
	return name;
}

std::vector<std::string> entries(s32 channel)
{
	std::vector<std::string> result;
	if (!ensureCard(channel)) return result;
	std::error_code error;
	for (const auto& entry : fs::directory_iterator(root(channel), error)) {
		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file() && name.rfind(".meta_", 0) != 0) result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	if (result.size() > CARD_MAX_FILE) result.resize(CARD_MAX_FILE);
	return result;
}

u32 cardTimeNow()
{
	using namespace std::chrono;
	const auto unixTime = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
	return unixTime > 946684800 ? static_cast<u32>(unixTime - 946684800) : 0;
}

CARDStat defaultStat(s32 channel, const std::string& name)
{
	CARDStat stat{};
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	std::error_code error;
	stat.length = static_cast<u32>(fs::file_size(dataPath(channel, name), error));
	stat.time = cardTimeNow();
	if (DVDDiskID* disk = DVDGetCurrentDiskID()) {
		std::memcpy(stat.gameName, disk->gameName, sizeof(stat.gameName));
		std::memcpy(stat.company, disk->company, sizeof(stat.company));
	}
	stat.iconAddr = stat.commentAddr = 0xffffffffu;
	stat.offsetData = stat.length;
	return stat;
}

CARDStat loadStat(s32 channel, const std::string& name)
{
	CARDStat stat = defaultStat(channel, name), stored{};
	std::ifstream input(metaPath(channel, name), std::ios::binary);
	if (input.read(reinterpret_cast<char*>(&stored), sizeof(stored))) {
		// Earlier builds let CARDSetStatus overwrite the game identity with
		// whatever the caller left in it, which was zeros. The European game
		// compares those bytes against the disc and refuses a file that does
		// not match, so it kept deleting and recreating its own save. Heal
		// such a record on read rather than make anyone start over.
		static const u8 kNoGame[4] = { 0, 0, 0, 0 };
		if (std::memcmp(stored.gameName, kNoGame, sizeof(kNoGame)) == 0) {
			std::memcpy(stored.gameName, stat.gameName, sizeof(stored.gameName));
			std::memcpy(stored.company, stat.company, sizeof(stored.company));
		}
		stat = stored;
	}
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	stat.fileName[CARD_FILENAME_MAX - 1] = '\0';
	std::error_code error;
	stat.length = static_cast<u32>(fs::file_size(dataPath(channel, name), error));
	return stat;
}

bool saveStat(s32 channel, const std::string& name, const CARDStat& source)
{
	CARDStat stat = source;
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	stat.fileName[CARD_FILENAME_MAX - 1] = '\0';
	std::ofstream output(metaPath(channel, name), std::ios::binary | std::ios::trunc);
	return !!output.write(reinterpret_cast<const char*>(&stat), sizeof(stat));
}

// Card tracing (NECTAR_CARD_DEBUG=1). The save flow is create-temp, write,
// delete-old, rename; a single failing step makes the game retry the whole
// thing, which is what a loop of "creating save data" looks like from outside.
bool cardlog_on()
{
	static const int on = [] {
		const char* value = std::getenv("NECTAR_CARD_DEBUG");
		return (value != nullptr && value[0] != '0') ? 1 : 0;
	}();
	return on != 0;
}

// The operation in flight, so finish() can name it without every call site
// having to log its own return paths.
thread_local const char* sTraceOp = "-";
thread_local std::string sTraceName;

void cardtrace(const char* op, const char* name)
{
	if (!cardlog_on()) return;
	sTraceOp = op;
	sTraceName = name ? name : "-";
}

void cardtrace(const char* op, s32 fileNo)
{
	if (!cardlog_on()) return;
	sTraceOp = op;
	sTraceName = "#" + std::to_string(fileNo);
}

s32 finish(s32 channel, s32 result, CARDCallback callback = nullptr)
{
	if (cardlog_on()) {
		std::printf("[CARD] %-14s %-34s -> %d   (root %s)\n", sTraceOp, sTraceName.c_str(),
		            (int)result, saveRoot().string().c_str());
		std::fflush(stdout);
		sTraceOp = "-";
	}
	if (validChannel(channel)) sLastResult[channel] = result;
	if (callback) callback(channel, result);
	return result;
}

bool resolve(const CARDFileInfo* info, std::string& name)
{
	if (!info || !validChannel(info->chan)) return false;
	const auto files = entries(info->chan);
	if (info->fileNo < 0 || static_cast<size_t>(info->fileNo) >= files.size()) return false;
	name = files[info->fileNo];
	return true;
}
} // namespace

extern "C" {

void CARDInit(void)
{
	ensureCard(0);
	// Print where it actually landed. The old message named a fixed relative
	// path, which is exactly the detail a save-file bug report needs to be true.
	std::error_code error;
	const fs::path shown = fs::absolute(root(0), error);
	printf("[PC Port] CARDInit() - persistent filesystem card: %s\n",
	       (error ? root(0) : shown).string().c_str());
}

BOOL CARDProbe(s32 channel) { return validChannel(channel) && ensureCard(channel); }
s32 CARDProbeEx(s32 channel, s32* memSize, s32* sectorSize)
{
	cardtrace("CARDProbeEx", "-");
	if (memSize) *memSize = 128;
	if (sectorSize) *sectorSize = kSectorSize;
	return finish(channel, CARDProbe(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD);
}
s32 CARDMountAsync(s32 channel, CARDMemoryCard*, CARDCallback, CARDCallback callback)
{
	cardtrace("CARDMountAsync", "-");
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD, callback);
}
s32 CARDMount(s32 channel, CARDMemoryCard*, CARDCallback)
{
	cardtrace("CARDMount", "-");
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD);
}
s32 CARDUnmount(s32 channel) { return finish(channel, CARD_RESULT_READY); }

s32 CARDOpen(s32 channel, const char* fileName, CARDFileInfo* info)
{
	cardtrace("CARDOpen", fileName);
	const std::string wanted = safeName(fileName);
	const auto files = entries(channel);
	const auto item = std::find(files.begin(), files.end(), wanted);
	if (!info || item == files.end()) return finish(channel, CARD_RESULT_NOFILE);
	info->chan = channel;
	info->fileNo = static_cast<s32>(item - files.begin());
	info->offset = 0;
	info->length = static_cast<s32>(loadStat(channel, wanted).length);
	info->iBlock = 0;
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDFastOpen(s32 channel, s32 fileNo, CARDFileInfo* info)
{
	cardtrace("CARDFastOpen", fileNo);
	const auto files = entries(channel);
	if (!info || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	info->chan = channel; info->fileNo = fileNo; info->offset = 0;
	info->length = static_cast<s32>(loadStat(channel, files[fileNo]).length); info->iBlock = 0;
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDClose(CARDFileInfo* info) { return finish(info ? info->chan : 0, CARD_RESULT_READY); }

s32 CARDCreate(s32 channel, const char* fileName, u32 size, CARDFileInfo* info)
{
	cardtrace("CARDCreate", fileName);
	const std::string name = safeName(fileName);
	if (name.empty() || name.size() >= CARD_FILENAME_MAX) return finish(channel, CARD_RESULT_NAMETOOLONG);
	if (!ensureCard(channel)) return finish(channel, CARD_RESULT_NOCARD);
	if (fs::exists(dataPath(channel, name))) return finish(channel, CARD_RESULT_EXIST);
	if (entries(channel).size() >= CARD_MAX_FILE) return finish(channel, CARD_RESULT_LIMIT);
	s32 freeBytes = 0, freeFiles = 0;
	CARDFreeBlocks(channel, &freeBytes, &freeFiles);
	if (size > static_cast<u32>(freeBytes)) return finish(channel, CARD_RESULT_INSSPACE);
	std::ofstream output(dataPath(channel, name), std::ios::binary | std::ios::trunc);
	if (!output) return finish(channel, CARD_RESULT_IOERROR);
	if (size) { output.seekp(size - 1); output.put('\0'); }
	output.close();
	CARDStat stat = defaultStat(channel, name); stat.length = size; saveStat(channel, name, stat);
	return CARDOpen(channel, name.c_str(), info);
}
s32 CARDCreateAsync(s32 channel, const char* name, u32 size, CARDFileInfo* info, CARDCallback callback)
{
	const s32 result = CARDCreate(channel, name, size, info); if (callback) callback(channel, result); return result;
}

s32 CARDRead(CARDFileInfo* info, void* address, s32 length, s32 offset)
{
	cardtrace("CARDRead", info ? info->fileNo : -1);
	std::string name;
	if (!address || length < 0 || offset < 0 || !resolve(info, name)) return finish(info ? info->chan : 0, CARD_RESULT_NOFILE);
	std::ifstream input(dataPath(info->chan, name), std::ios::binary);
	input.seekg(offset);
	if (!input.read(static_cast<char*>(address), length)) return finish(info->chan, CARD_RESULT_IOERROR);
	info->offset = offset + length; sTransferred[info->chan] = length;
	return finish(info->chan, CARD_RESULT_READY);
}
s32 CARDReadAsync(CARDFileInfo* info, void* address, s32 length, s32 offset, CARDCallback callback)
{
	const s32 result = CARDRead(info, address, length, offset); if (callback) callback(info ? info->chan : 0, result); return result;
}
s32 CARDWrite(CARDFileInfo* info, void* address, s32 length, s32 offset)
{
	cardtrace("CARDWrite", info ? info->fileNo : -1);
	std::string name;
	if (!address || length < 0 || offset < 0 || !resolve(info, name)) return finish(info ? info->chan : 0, CARD_RESULT_NOFILE);
	std::fstream output(dataPath(info->chan, name), std::ios::binary | std::ios::in | std::ios::out);
	output.seekp(offset);
	if (!output.write(static_cast<const char*>(address), length)) return finish(info->chan, CARD_RESULT_IOERROR);
	output.flush();
	info->offset = offset + length; info->length = std::max(info->length, info->offset); sTransferred[info->chan] = length;
	CARDStat stat = loadStat(info->chan, name); stat.length = info->length; stat.time = cardTimeNow(); saveStat(info->chan, name, stat);
	return finish(info->chan, CARD_RESULT_READY);
}
s32 CARDWriteAsync(CARDFileInfo* info, void* address, s32 length, s32 offset, CARDCallback callback)
{
	const s32 result = CARDWrite(info, address, length, offset); if (callback) callback(info ? info->chan : 0, result); return result;
}
s32 CARDGetXferredBytes(s32 channel) { return validChannel(channel) ? sTransferred[channel].load() : 0; }

s32 CARDFastDelete(s32 channel, s32 fileNo)
{
	cardtrace("CARDFastDelete", fileNo);
	const auto files = entries(channel);
	if (fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	std::error_code error;
	fs::remove(dataPath(channel, files[fileNo]), error); if (error) return finish(channel, CARD_RESULT_IOERROR);
	fs::remove(metaPath(channel, files[fileNo]), error);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDFastDeleteAsync(s32 channel, s32 fileNo, CARDCallback callback)
{
	const s32 result = CARDFastDelete(channel, fileNo); if (callback) callback(channel, result); return result;
}

s32 CARDRename(s32 channel, const char* oldName, const char* newName)
{
	cardtrace("CARDRename", (std::string(oldName ? oldName : "-") + " -> " + (newName ? newName : "-")).c_str());
	const std::string oldSafe = safeName(oldName), newSafe = safeName(newName);
	if (!fs::exists(dataPath(channel, oldSafe))) return finish(channel, CARD_RESULT_NOFILE);
	if (fs::exists(dataPath(channel, newSafe))) return finish(channel, CARD_RESULT_EXIST);
	std::error_code error;
	fs::rename(dataPath(channel, oldSafe), dataPath(channel, newSafe), error);
	if (error) return finish(channel, CARD_RESULT_IOERROR);
	if (fs::exists(metaPath(channel, oldSafe))) fs::rename(metaPath(channel, oldSafe), metaPath(channel, newSafe), error);
	CARDStat stat = loadStat(channel, newSafe); saveStat(channel, newSafe, stat);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDRenameAsync(s32 channel, const char* oldName, const char* newName, CARDCallback callback)
{
	const s32 result = CARDRename(channel, oldName, newName); if (callback) callback(channel, result); return result;
}

s32 CARDGetStatus(s32 channel, s32 fileNo, CARDStat* stat)
{
	cardtrace("CARDGetStatus", fileNo);
	const auto files = entries(channel);
	if (!stat || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	*stat = loadStat(channel, files[fileNo]);
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDSetStatus(s32 channel, s32 fileNo, CARDStat* stat)
{
	cardtrace("CARDSetStatus", fileNo);
	const auto files = entries(channel);
	if (!stat || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	// The real CARDSetStatus only takes the banner, icon and comment layout
	// from the caller. Name, size and above all the game identity belong to
	// the card and never change. Copying the whole struct let a caller that
	// had not filled gameName/company wipe them, and the PAL game then no
	// longer recognised the file as its own.
	CARDStat copy       = loadStat(channel, files[fileNo]);
	copy.bannerFormat   = stat->bannerFormat;
	copy.iconAddr       = stat->iconAddr;
	copy.iconFormat     = stat->iconFormat;
	copy.iconSpeed      = stat->iconSpeed;
	copy.commentAddr    = stat->commentAddr;
	copy.time           = cardTimeNow();
	return finish(channel, saveStat(channel, files[fileNo], copy) ? CARD_RESULT_READY : CARD_RESULT_IOERROR);
}
s32 CARDSetStatusAsync(s32 channel, s32 fileNo, CARDStat* stat, CARDCallback callback)
{
	const s32 result = CARDSetStatus(channel, fileNo, stat); if (callback) callback(channel, result); return result;
}

s32 CARDGetSerialNo(s32 channel, u64* serial) { if (serial) *serial = 0x50494b4d494e0001ULL; return finish(channel, CARD_RESULT_READY); }
s32 CARDGetSectorSize(s32 channel, u32* size) { if (size) *size = kSectorSize; return finish(channel, CARD_RESULT_READY); }
s32 CARDFormat(s32 channel)
{
	if (!validChannel(channel)) return finish(channel, CARD_RESULT_NOCARD);
	std::error_code error; fs::remove_all(root(channel), error); ensureCard(channel);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDFormatAsync(s32 channel, CARDCallback callback) { const s32 result = CARDFormat(channel); if (callback) callback(channel, result); return result; }
s32 CARDFreeBlocks(s32 channel, s32* bytesUnused, s32* filesUnused)
{
	std::uintmax_t used = 0; const auto files = entries(channel); std::error_code error;
	for (const auto& name : files) { used += fs::file_size(dataPath(channel, name), error); error.clear(); }
	if (bytesUnused) *bytesUnused = static_cast<s32>(used < kCapacity ? kCapacity - used : 0);
	if (filesUnused) *filesUnused = CARD_MAX_FILE - static_cast<s32>(files.size());
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDGetResultCode(s32 channel) { return validChannel(channel) ? sLastResult[channel].load() : CARD_RESULT_NOCARD; }
s32 CARDCheck(s32 channel) { return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD); }
s32 CARDCheckAsync(s32 channel, CARDCallback callback) { const s32 result = CARDCheck(channel); if (callback) callback(channel, result); return result; }
s32 CARDCheckExAsync(s32 channel, s32* bytes, CARDCallback callback) { if (bytes) *bytes = 0; return CARDCheckAsync(channel, callback); }

void __CARDSetDiskID(DVDDiskID*) { }
void __CARDDefaultApiCallback(s32, s32) { }
void __CARDSyncCallback(s32, s32) { }
u16 __CARDGetFontEncode() { return CARD_ENCODE_ANSI; }
s32 __CARDSync(s32 channel) { return CARDGetResultCode(channel); }

} // extern "C"
