/** Native filesystem implementation of the GameCube memory-card API. */
#include "Dolphin/card.h"
#include "dolphin_stubs/pc_card_paths.h"
#include "dolphin_stubs/pc_card_save_root.h"
#include "Dolphin/dvd.h"
#include "pc_speedrun.h"
#include "randomizer/pc_randomizer.h"
#if PIKI_SCENARIOS
#include "dolphin_stubs/pc_card_fault.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
fs::path saveRoot()
{
	static const fs::path resolved = pc_card_resolve_save_root({});
	return resolved;
}

// El modo Speedrun tiene su propia tarjeta (save/speedrun/card0): sus
// partidas vanilla no se mezclan con las normales ni al revés. El Randomizer
// igual (save/randomizer/card0): sus tres ranuras son independientes de las
// del juego normal. Dentro del Speedrun, 5 Parts usa su propia tarjeta
// (save/speedrun/5-parts/card0, la del race file) y el Desafío la suya
// (save/speedrun/challenge/card0).
fs::path root(s32 channel)
{
	const char* speedrunSub = pc_speedrun_active() ? pc_speedrun_card_subdir() : nullptr;
	const fs::path base = speedrunSub                     ? saveRoot() / "speedrun" / speedrunSub
	                      : pc_randomizer_menu_pending() ? saveRoot() / "randomizer"
	                                                     : saveRoot();
	return base / (channel == 0 ? "card0" : "card1");
}
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
		if (entry.is_regular_file() && name.rfind(".meta_", 0) != 0 && name.rfind(".tmp_", 0) != 0) result.push_back(name);
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

// Swap a fully written temp file over the save. POSIX rename() already
// replaces the target atomically; on Windows rename refuses an existing
// destination, so ReplaceFileW (or MoveFileExW on first write) does it. On
// failure the previous save is untouched: the caller only drops the temp.
bool replaceFile(const fs::path& tmp, const fs::path& target)
{
#if defined(_WIN32)
	if (fs::exists(target)) {
		if (ReplaceFileW(target.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
	}
	return MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	std::error_code error;
	fs::rename(tmp, target, error);
	return !error;
#endif
}

// Writes a whole file image through a temp in the same folder plus
// replaceFile(), so a crash or full disk mid-save leaves the old file intact.
bool writeFileAtomic(const fs::path& path, const fs::path& tmp, const char* data, size_t size)
{
	{
		std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
		bool ok = !!output;
		if (ok && size) ok = !!output.write(data, std::streamsize(size));
		if (ok) ok = !!output.flush();
		output.close();
		if (!ok || output.fail()) {
			std::error_code ignored;
			fs::remove(tmp, ignored);
			return false;
		}
	}
	if (!replaceFile(tmp, path)) {
		std::error_code ignored;
		fs::remove(tmp, ignored);
		return false;
	}
	return true;
}

bool saveStat(s32 channel, const std::string& name, const CARDStat& source)
{
	CARDStat stat = source;
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	stat.fileName[CARD_FILENAME_MAX - 1] = '\0';
	return writeFileAtomic(metaPath(channel, name), root(channel) / (".tmp_.meta_" + name), reinterpret_cast<const char*>(&stat),
	                       sizeof(stat));
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

#if PIKI_SCENARIOS
// Scenario fault injection (pc_card_fault.h). 0 means no fault.
std::atomic<s32> sFault[PC_CARD_FAULT_COUNT] = {};
std::atomic<s32> sFaultHits[PC_CARD_FAULT_COUNT] = {};
s32 cardFault(PcCardFault fault)
{
	const s32 code = sFault[fault].load();
	if (code != 0) sFaultHits[fault]++;
	return code;
}
#define CARD_FAULT(name) cardFault(PC_CARD_FAULT_##name)
#else
#define CARD_FAULT(name) s32(0)
#endif
} // namespace

#if PIKI_SCENARIOS
void pc_card_fault_set(int fault, int code)
{
	if (fault < 0 || fault >= PC_CARD_FAULT_COUNT) return;
	sFault[fault]     = code;
	sFaultHits[fault] = 0;
}
void pc_card_fault_clear_all(void)
{
	for (auto& f : sFault) f = 0;
}
int pc_card_fault_hits(int fault) { return fault >= 0 && fault < PC_CARD_FAULT_COUNT ? sFaultHits[fault].load() : 0; }
#endif

// Carpeta de las tarjetas (F1 > Data > Open Save Folder).
std::string pc_card_save_dir()
{
	std::error_code error;
	return fs::absolute(saveRoot(), error).string();
}

// Carpeta de la tarjeta en uso (normal, Randomizer...): historial de días.
std::string pc_card_root_dir()
{
	return root(0).string();
}

// Carpeta de datos del modo Speedrun (tarjeta propia y registro de tiempos).
std::string pc_card_speedrun_dir()
{
	std::error_code error;
	const fs::path dir = saveRoot() / "speedrun";
	fs::create_directories(dir, error);
	return dir.string();
}

// Race file de 5 Parts: copia de su tarjeta guardada al empezar la primera run.
namespace {
fs::path raceDir() { return saveRoot() / "speedrun" / "5-parts" / "race"; }
fs::path raceCardDir() { return saveRoot() / "speedrun" / "5-parts" / "card0"; }
}

// Puntos de práctica: save/speedrun/practice/<categoría>/dayN, copias de la
// tarjeta de las runs de historia (save/speedrun/card0).
namespace {
fs::path practiceDir(int day)
{
	const char* slug = pc_speedrun_category() == PC_SR_CAT_LOW_PIKMIN ? "low-pikmin" : "all-parts";
	return saveRoot() / "speedrun" / "practice" / slug / ("day" + std::to_string(day));
}
fs::path storyCardDir() { return saveRoot() / "speedrun" / "card0"; }
}

bool pc_speedrun_practice_exists(int day)
{
	std::error_code error;
	return fs::is_directory(practiceDir(day), error);
}

bool pc_speedrun_practice_restore(int day)
{
	std::error_code error;
	if (!pc_speedrun_practice_exists(day)) return false;
	fs::remove_all(storyCardDir(), error);
	fs::copy(practiceDir(day), storyCardDir(), fs::copy_options::recursive, error);
	return !error;
}

void pc_speedrun_practice_save(int day)
{
	std::error_code error;
	if (!fs::is_directory(storyCardDir(), error)) return;
	const fs::path dir = practiceDir(day);
	fs::remove_all(dir, error);
	fs::create_directories(dir.parent_path(), error);
	fs::copy(storyCardDir(), dir, fs::copy_options::recursive, error);
	if (error) fs::remove_all(dir, error);
}

void pc_speedrun_practice_delete(int day)
{
	std::error_code error;
	fs::remove_all(practiceDir(day), error);
}

bool pc_speedrun_race_restore(void)
{
	std::error_code error;
	if (!fs::is_directory(raceDir(), error)) return false;
	fs::remove_all(raceCardDir(), error);
	fs::copy(raceDir(), raceCardDir(), fs::copy_options::recursive, error);
	return !error;
}

void pc_speedrun_race_save(void)
{
	std::error_code error;
	if (fs::exists(raceDir(), error) || !fs::is_directory(raceCardDir(), error)) return;
	fs::copy(raceCardDir(), raceDir(), fs::copy_options::recursive, error);
	if (error) fs::remove_all(raceDir(), error);
}

void pc_speedrun_race_delete(void)
{
	std::error_code error;
	fs::remove_all(raceDir(), error);
}

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

BOOL CARDProbe(s32 channel)
{
	if (CARD_FAULT(PROBE)) return FALSE;
	return validChannel(channel) && ensureCard(channel);
}
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
	if (const s32 fault = CARD_FAULT(MOUNT)) return finish(channel, fault, callback);
	if (const s32 fault = CARD_FAULT(MOUNT_RESULT)) {
		finish(channel, fault, callback);
		return CARD_RESULT_READY;
	}
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD, callback);
}
s32 CARDMount(s32 channel, CARDMemoryCard*, CARDCallback)
{
	cardtrace("CARDMount", "-");
	if (const s32 fault = CARD_FAULT(MOUNT)) return finish(channel, fault);
	if (const s32 fault = CARD_FAULT(MOUNT_RESULT)) return finish(channel, fault);
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
	if (const s32 fault = CARD_FAULT(CREATE)) return finish(channel, fault);
	if (name.empty() || name.size() >= CARD_FILENAME_MAX) return finish(channel, CARD_RESULT_NAMETOOLONG);
	if (!ensureCard(channel)) return finish(channel, CARD_RESULT_NOCARD);
	if (fs::exists(dataPath(channel, name))) return finish(channel, CARD_RESULT_EXIST);
	if (entries(channel).size() >= CARD_MAX_FILE) return finish(channel, CARD_RESULT_LIMIT);
	s32 freeBytes = 0, freeFiles = 0;
	CARDFreeBlocks(channel, &freeBytes, &freeFiles);
	if (size > static_cast<u32>(freeBytes)) return finish(channel, CARD_RESULT_INSSPACE);
	const std::vector<char> blank(size, '\0');
	if (!writeFileAtomic(dataPath(channel, name), root(channel) / (".tmp_" + name), blank.data(), blank.size()))
		return finish(channel, CARD_RESULT_IOERROR);
	CARDStat stat = defaultStat(channel, name); stat.length = size;
	if (!saveStat(channel, name, stat)) {
		std::error_code ignored;
		fs::remove(dataPath(channel, name), ignored);
		return finish(channel, CARD_RESULT_IOERROR);
	}
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
	if (const s32 fault = CARD_FAULT(WRITE)) return finish(info->chan, fault);
	// Copy, patch, swap: a crash or a window close in the middle of a save
	// must leave either the old file or the new one, never a torn mix (the
	// game's checksum would then reject the whole card). Save files are a few
	// dozen KB, so rewriting one per call costs nothing.
	const size_t end = size_t(offset) + size_t(length); // both >= 0 and < 2^31: no overflow
	if (end > kCapacity) return finish(info->chan, CARD_RESULT_INSSPACE);
	const fs::path path = dataPath(info->chan, name);
	std::vector<char> image;
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) return finish(info->chan, CARD_RESULT_IOERROR);
		image.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		if (input.bad()) return finish(info->chan, CARD_RESULT_IOERROR);
	}
	if (image.size() < end) image.resize(end, '\0');
	std::memcpy(image.data() + offset, address, size_t(length));
	if (!writeFileAtomic(path, root(info->chan) / (".tmp_" + name), image.data(), image.size())) // .tmp_ hidden from entries()
		return finish(info->chan, CARD_RESULT_IOERROR);
	info->offset = offset + length; info->length = std::max(info->length, info->offset); sTransferred[info->chan] = length;
	CARDStat stat = loadStat(info->chan, name); stat.length = info->length; stat.time = cardTimeNow();
	if (!saveStat(info->chan, name, stat)) return finish(info->chan, CARD_RESULT_IOERROR);
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
s32 CARDGetSectorSize(s32 channel, u32* size)
{
	const s32 fault = CARD_FAULT(SECTOR_SIZE);
	if (size) *size = fault ? u32(fault) : u32(kSectorSize);
	return finish(channel, CARD_RESULT_READY);
}
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
	if (bytesUnused && CARD_FAULT(NO_FREE_BYTES)) *bytesUnused = 0;
	if (filesUnused && CARD_FAULT(NO_FREE_FILES)) *filesUnused = 0;
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDGetResultCode(s32 channel) { return validChannel(channel) ? sLastResult[channel].load() : CARD_RESULT_NOCARD; }
s32 CARDCheck(s32 channel)
{
	if (const s32 fault = CARD_FAULT(CHECK_RESULT)) return finish(channel, fault);
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD);
}
s32 CARDCheckAsync(s32 channel, CARDCallback callback)
{
	const s32 result = CARDCheck(channel);
	if (callback) callback(channel, result);
#if PIKI_SCENARIOS
	// An injected check failure arrives as the async result, not the return value.
	if (result != CARD_RESULT_READY && sFault[PC_CARD_FAULT_CHECK_RESULT].load() != 0) return CARD_RESULT_READY;
#endif
	return result;
}
s32 CARDCheckExAsync(s32 channel, s32* bytes, CARDCallback callback) { if (bytes) *bytes = 0; return CARDCheckAsync(channel, callback); }

void __CARDSetDiskID(DVDDiskID*) { }
void __CARDDefaultApiCallback(s32, s32) { }
void __CARDSyncCallback(s32, s32) { }
u16 __CARDGetFontEncode() { return CARD_ENCODE_ANSI; }
s32 __CARDSync(s32 channel) { return CARDGetResultCode(channel); }

} // extern "C"

std::string pc_card_channel_dir(int channel) { return root(channel == 1 ? 1 : 0).string(); }
