#include "gci_save_import.h"

#include "dolphin_stubs/pc_card_save_root.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

std::string trimCardName(const char* source, std::size_t maxLen)
{
    std::string name(source, strnlen(source, maxLen));
    for (char& c : name) {
        if (c == '/' || c == '\\') c = '_';
    }
    if (name == "." || name == "..") name.insert(name.begin(), '_');
    return name;
}

bool readFileBytes(const fs::path& path, std::vector<std::uint8_t>& bytes, std::string& error)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "Could not read " + path.string();
        return false;
    }
    const auto size = input.tellg();
    if (size < 0) {
        error = "Could not read " + path.string();
        return false;
    }
    input.seekg(0);
    bytes.resize(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Could not read " + path.string();
        return false;
    }
    return true;
}

} // namespace

std::string gameIdFromGciDir(const GciDirEntry& dir)
{
    return std::string(dir.gameName, dir.gameName + 4) + std::string(dir.company, dir.company + 2);
}

namespace {

bool notAGci(std::string& error)
{
    error = kNotAGciMessage;
    return false;
}

bool isIdChar(std::uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

bool parseGci(const std::vector<std::uint8_t>& bytes, ParsedGci& out, std::string& error)
{
    // A .gci is a 64-byte directory entry (game id, file name, block count)
    // followed by exactly blockCount card blocks. Anything else is some other
    // file the picker let through.
    if (bytes.size() < kGciHeaderSize) return notAGci(error);
    for (std::size_t i = 0; i < 6; ++i)
        if (!isIdChar(bytes[i])) return notAGci(error);
    if (bytes[8] == 0) return notAGci(error);
    const std::uint16_t blocks = static_cast<std::uint16_t>((bytes[0x38] << 8) | bytes[0x39]);
    if (blocks == 0 || blocks > kCardMaxUserBlocks
        || bytes.size() != kGciHeaderSize + static_cast<std::size_t>(blocks) * kCardBlockSize)
        return notAGci(error);
    std::memcpy(&out.dir, bytes.data(), kGciHeaderSize);
    // GCI headers are stored big-endian (GameCube byte order); convert to host order.
    const auto be16 = [&](std::size_t o) {
        return static_cast<std::uint16_t>((bytes[o] << 8) | bytes[o + 1]);
    };
    const auto be32 = [&](std::size_t o) {
        return (static_cast<std::uint32_t>(bytes[o]) << 24) | (static_cast<std::uint32_t>(bytes[o + 1]) << 16)
             | (static_cast<std::uint32_t>(bytes[o + 2]) << 8) | static_cast<std::uint32_t>(bytes[o + 3]);
    };
    out.dir.time = be32(0x28);
    out.dir.iconAddr = be32(0x2C);
    out.dir.iconFormat = be16(0x30);
    out.dir.iconSpeed = be16(0x32);
    out.dir.startBlock = be16(0x36);
    out.dir.blockCount = be16(0x38);
    out.dir.reserved3A = be16(0x3A);
    out.dir.commentAddr = be32(0x3C);
    const std::size_t expectedSize = bytes.size();
    out.payload.assign(bytes.begin() + kGciHeaderSize, bytes.begin() + static_cast<std::ptrdiff_t>(expectedSize));
    out.saveFileName = trimCardName(out.dir.fileName, kCardFilenameMax);
    if (out.saveFileName.empty()) {
        error = "The save header has an empty filename.";
        return false;
    }
    out.gameId = gameIdFromGciDir(out.dir);
    return true;
}

} // namespace

bool parseGciBytes(const std::vector<std::uint8_t>& bytes, ParsedGci& out, std::string& error)
{
    return parseGci(bytes, out, error);
}

bool parseGciFile(const fs::path& path, ParsedGci& out, std::string& error)
{
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(path, bytes, error)) return false;
    return parseGci(bytes, out, error);
}

bool gciMatchesInstalledGame(const std::string& gciGameId, const std::string& installedDiscId)
{
    if (gciGameId.size() != 6) return false;
    std::string expected = installedDiscId;
    if (expected.size() < 6) {
        if (expected.size() == 4) expected += "01";
        else return false;
    } else if (expected.size() > 6) {
        expected.resize(6);
    }
    for (char& c : expected) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string actual = gciGameId;
    for (char& c : actual) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return actual == expected;
}

fs::path card0DirectoryForInstall(const fs::path& gameInstallDir)
{
    PcCardSaveRootOptions options;
    options.installDir = gameInstallDir;
    options.logMigration = false;
    return pc_card_card0_directory(options);
}

GciCardMeta metaFromGci(const ParsedGci& gci)
{
    GciCardMeta meta {};
    std::strncpy(meta.fileName, gci.saveFileName.c_str(), kCardFilenameMax - 1);
    meta.length = static_cast<std::uint32_t>(gci.payload.size());
    meta.time = gci.dir.time;
    std::memcpy(meta.gameName, gci.dir.gameName, sizeof(meta.gameName));
    std::memcpy(meta.company, gci.dir.company, sizeof(meta.company));
    meta.bannerFormat = gci.dir.bannerFormat;
    meta.iconAddr = gci.dir.iconAddr;
    meta.iconFormat = gci.dir.iconFormat;
    meta.iconSpeed = gci.dir.iconSpeed;
    meta.commentAddr = gci.dir.commentAddr;
    meta.offsetData = meta.length;
    return meta;
}

namespace {

CardWriteStep gFailStep = CardWriteStep::None;

bool injectedFailure(CardWriteStep step)
{
    if (gFailStep != step) return false;
    gFailStep = CardWriteStep::None;
    return true;
}

bool writeWholeFile(const fs::path& path, const void* data, std::size_t size, CardWriteStep step,
                    std::string& error)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (output && size) output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (output) output.flush();
    if (!output || injectedFailure(step)) {
        error = "Could not write " + path.string();
        return false;
    }
    return true;
}

bool renameInto(const fs::path& from, const fs::path& to, CardWriteStep step, std::string& error)
{
    std::error_code ec;
    if (injectedFailure(step)) ec = std::make_error_code(std::errc::io_error);
    else fs::rename(from, to, ec);
    if (ec) {
        error = "Could not move the new save into place: " + ec.message();
        return false;
    }
    return true;
}

fs::path cleanCardDir(const fs::path& card0Dir)
{
    fs::path dir = card0Dir.lexically_normal();
    if (dir.filename().empty()) dir = dir.parent_path();
    return dir;
}

bool parseBackupName(const std::string& name, CardBackup& out)
{
    if (name.size() != 15 && name.size() != 18) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const bool dash = i == 8 || i == 15;
        if (dash ? name[i] != '-' : !std::isdigit(static_cast<unsigned char>(name[i]))) return false;
    }
    const auto num = [&](std::size_t at, std::size_t len) { return std::stoi(name.substr(at, len)); };
    out.name = name;
    out.year = num(0, 4);
    out.month = num(4, 2);
    out.day = num(6, 2);
    out.hour = num(9, 2);
    out.minute = num(11, 2);
    out.second = num(13, 2);
    return out.month >= 1 && out.month <= 12;
}

std::string backupStampNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    return stamp;
}

// Writes the pair through .tmp_ files, backs up the old pair, then renames.
// `meta` null means the new save has no .meta_ file (an old backup without one).
bool commitCardPair(const fs::path& card0Dir, const std::string& name, const std::vector<std::uint8_t>& data,
                    const std::vector<std::uint8_t>* meta, std::string& error, CardBackup* backupMade)
{
    std::error_code ec;
    fs::create_directories(card0Dir, ec);
    if (ec) {
        error = "Could not create the memory-card folder: " + ec.message();
        return false;
    }
    const fs::path dataPath = card0Dir / name;
    const fs::path metaPath = card0Dir / (".meta_" + name);
    const fs::path tmpData = card0Dir / (".tmp_" + name);
    const fs::path tmpMeta = card0Dir / (".tmp_.meta_" + name);
    const auto cleanup = [&] {
        std::error_code ignored;
        fs::remove(tmpData, ignored);
        fs::remove(tmpMeta, ignored);
    };

    if (!writeWholeFile(tmpData, data.data(), data.size(), CardWriteStep::WriteData, error)
        || (meta && !writeWholeFile(tmpMeta, meta->data(), meta->size(), CardWriteStep::WriteMeta, error))) {
        cleanup();
        error += ". The save on the memory card was not changed.";
        return false;
    }

    CardBackup backup;
    if (!backupCardSave(card0Dir, name, error, &backup)) {
        cleanup();
        error += " The save on the memory card was not changed.";
        return false;
    }
    if (backupMade) *backupMade = backup;

    if (!renameInto(tmpData, dataPath, CardWriteStep::RenameData, error)) {
        cleanup();
        error += ". The save on the memory card was not changed.";
        return false;
    }
    if (meta) {
        if (!renameInto(tmpMeta, metaPath, CardWriteStep::RenameMeta, error)) {
            // The data is already in place: put the old data back from the
            // backup, or drop the new file if there was no old save.
            std::error_code undo;
            const fs::path oldData = backup.dir.empty() ? fs::path() : backup.dir / name;
            if (!oldData.empty() && fs::exists(oldData, undo)) {
                fs::copy_file(oldData, tmpData, fs::copy_options::overwrite_existing, undo);
                if (!undo) fs::rename(tmpData, dataPath, undo);
            } else {
                fs::remove(dataPath, undo);
            }
            cleanup();
            error += undo ? ". Your previous save is in " + backup.dir.string() + "."
                          : ". The save on the memory card was not changed.";
            return false;
        }
    } else {
        fs::remove(metaPath, ec); // already in the backup; would not match the restored data
    }
    return true;
}

bool readIfExists(const fs::path& path, std::vector<std::uint8_t>& bytes, bool& exists, std::string& error)
{
    std::error_code ec;
    exists = fs::is_regular_file(path, ec);
    return !exists || readFileBytes(path, bytes, error);
}

} // namespace

void failNextCardWriteForTesting(CardWriteStep step) { gFailStep = step; }

fs::path cardBackupRoot(const fs::path& card0Dir)
{
    const fs::path dir = cleanCardDir(card0Dir);
    return dir.parent_path() / (dir.filename().string() + "-backups");
}

std::vector<CardBackup> listCardBackups(const fs::path& card0Dir)
{
    std::vector<CardBackup> result;
    std::error_code ec;
    const fs::path root = cardBackupRoot(card0Dir);
    if (!fs::is_directory(root, ec)) return result;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        CardBackup backup;
        if (!entry.is_directory(ec) || !parseBackupName(entry.path().filename().string(), backup)) continue;
        backup.dir = entry.path();
        result.push_back(backup);
    }
    std::sort(result.begin(), result.end(), [](const CardBackup& a, const CardBackup& b) { return a.name > b.name; });
    return result;
}

std::string describeBackupTime(const CardBackup& backup)
{
    static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const int hour12 = backup.hour % 12 == 0 ? 12 : backup.hour % 12;
    char text[48];
    std::snprintf(text, sizeof(text), "%s %d, %d:%02d %s", months[(backup.month - 1) % 12], backup.day, hour12,
                  backup.minute, backup.hour < 12 ? "AM" : "PM");
    return text;
}

bool backupCardSave(const fs::path& card0Dir, const std::string& saveFileName, std::string& error, CardBackup* made)
{
    std::error_code ec;
    const fs::path dataPath = card0Dir / saveFileName;
    const fs::path metaPath = card0Dir / (".meta_" + saveFileName);
    const bool hasData = fs::is_regular_file(dataPath, ec);
    const bool hasMeta = fs::is_regular_file(metaPath, ec);
    if (!hasData && !hasMeta) return true;

    const fs::path root = cardBackupRoot(card0Dir);
    const std::string stamp = backupStampNow();
    // Several backups in one second get -01, -02, ... after the newest one
    // with that stamp (not the first free number: pruning may have removed
    // the plain stamp while a later -NN is still there), so names keep
    // sorting in the order they were made.
    int next = 0;
    for (const CardBackup& existing : listCardBackups(card0Dir)) {
        if (existing.name.compare(0, stamp.size(), stamp) != 0) continue;
        const int n = existing.name.size() > stamp.size() ? std::stoi(existing.name.substr(stamp.size() + 1)) : 0;
        next = std::max(next, n + 1);
    }
    fs::path dir = root / stamp;
    if (next > 0) {
        char suffix[16]; // room for "-" plus any int
        std::snprintf(suffix, sizeof(suffix), "-%02d", next);
        dir = root / (stamp + suffix);
    }
    const auto fail = [&](const std::string& why) {
        std::error_code ignored;
        fs::remove_all(dir, ignored);
        error = "Could not back up the current save (" + why + ").";
        return false;
    };
    if (next > 99 || fs::exists(dir, ec)) {
        error = "Could not back up the current save (too many backups this second).";
        return false;
    }
    fs::create_directories(dir, ec);
    if (ec) return fail(ec.message());
    if (injectedFailure(CardWriteStep::Backup)) return fail("test failure");
    if (hasData && !fs::copy_file(dataPath, dir / saveFileName, ec)) return fail(ec ? ec.message() : "copy failed");
    if (hasMeta && !fs::copy_file(metaPath, dir / metaPath.filename(), ec))
        return fail(ec ? ec.message() : "copy failed");

    const std::vector<CardBackup> all = listCardBackups(card0Dir);
    for (std::size_t i = kCardBackupKeep; i < all.size(); ++i) fs::remove_all(all[i].dir, ec);
    if (made) {
        parseBackupName(dir.filename().string(), *made);
        made->dir = dir;
    }
    return true;
}

bool importGciToCard(const ParsedGci& gci, const fs::path& card0Dir, bool overwrite, std::string& error,
                     CardBackup* backupMade)
{
    std::error_code ec;
    const fs::path dataPath = card0Dir / gci.saveFileName;
    const fs::path metaPath = card0Dir / (".meta_" + gci.saveFileName);
    if (!overwrite && (fs::exists(dataPath, ec) || fs::exists(metaPath, ec))) {
        error = "A save named \"" + gci.saveFileName + "\" is already on the memory card.";
        return false;
    }
    const GciCardMeta meta = metaFromGci(gci);
    const auto* metaBytes = reinterpret_cast<const std::uint8_t*>(&meta);
    const std::vector<std::uint8_t> metaData(metaBytes, metaBytes + sizeof(meta));
    return commitCardPair(card0Dir, gci.saveFileName, gci.payload, &metaData, error, backupMade);
}

SaveGame saveGameForId(const std::string& gameId)
{
    if (gameId.rfind("GPI", 0) == 0) return SaveGame::Pikmin1;
    if (gameId.rfind("GPV", 0) == 0) return SaveGame::Pikmin2;
    return SaveGame::Unknown;
}

const char* saveGameName(SaveGame game)
{
    return game == SaveGame::Pikmin1 ? "Pikmin" : game == SaveGame::Pikmin2 ? "Pikmin 2" : "another game";
}

const char* nativeSaveFileName(SaveGame game)
{
    return game == SaveGame::Pikmin1 ? "Pikmin dataFile" : game == SaveGame::Pikmin2 ? "Pikmin2_SaveData" : "";
}

namespace {

std::uint32_t readBe32(const std::vector<std::uint8_t>& b, std::size_t o)
{
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) | (std::uint32_t(b[o + 2]) << 8)
         | std::uint32_t(b[o + 3]);
}

// MemoryCard::calcChecksum (plugPikiColin/memoryCard.cpp): words read
// big-endian, as on GameCube. `bigEndian` false is calcChecksumLegacy, the
// host-order sum older PC builds wrote, which the game still accepts.
std::uint32_t pikmin1Checksum(const std::uint8_t* data, std::uint32_t length, bool bigEndian)
{
    std::uint32_t sum = 0x32546532u;
    for (std::uint32_t i = 0; i < length / 4; ++i) {
        const std::uint8_t* w = data + i * 4;
        const std::uint32_t value = bigEndian
            ? (std::uint32_t(w[0]) << 24) | (std::uint32_t(w[1]) << 16) | (std::uint32_t(w[2]) << 8) | w[3]
            : (std::uint32_t(w[3]) << 24) | (std::uint32_t(w[2]) << 16) | (std::uint32_t(w[1]) << 8) | w[0];
        const std::uint8_t i2 = static_cast<std::uint8_t>(i);
        sum += (std::uint32_t(i2) << 24) | (std::uint32_t(std::uint8_t(i2 + 1)) << 16)
             | (std::uint32_t(std::uint8_t(i2 - 1)) << 8) | std::uint32_t(std::uint8_t(i2 + 2));
        sum += value;
    }
    return 0x32546532u - sum;
}

bool hasMagic(const std::vector<std::uint8_t>& b, std::size_t o, const char* magic)
{
    // Either byte order: the PC port may store the u32 host-endian.
    return (b[o] == magic[0] && b[o + 1] == magic[1] && b[o + 2] == magic[2] && b[o + 3] == magic[3])
        || (b[o] == magic[3] && b[o + 1] == magic[2] && b[o + 2] == magic[1] && b[o + 3] == magic[0]);
}

std::string idFromMeta(const GciCardMeta& meta)
{
    return std::string(meta.gameName, meta.gameName + 4) + std::string(meta.company, meta.company + 2);
}

std::uint32_t cardTimeNow()
{
    const std::time_t now = std::time(nullptr);
    return now > 946684800 ? static_cast<std::uint32_t>(now - 946684800) : 0; // seconds since 2000, as on GC
}

std::string mismatchMessage(const std::string& saveId, SaveGame saveGame, const std::string& installedDiscId)
{
    const std::string what = saveGame == SaveGame::Unknown ? saveId
                                                           : std::string(saveGameName(saveGame)) + " (" + saveId + ")";
    return "This save is for " + what + ", but this install is " + installedDiscId + ".";
}

} // namespace

SaveGame detectNativeSave(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() == kPikmin1SaveFileSize) {
        // Banner block: checksum of 0x1FFC bytes stored big-endian at 0x1FFC.
        const std::uint32_t stored = readBe32(bytes, 0x1FFC);
        if (stored == pikmin1Checksum(bytes.data(), 0x1FFC, true)
            || stored == pikmin1Checksum(bytes.data(), 0x1FFC, false))
            return SaveGame::Pikmin1;
    }
    if (bytes.size() == kPikmin2SaveFileSize) {
        // Header 0x2000, two 0x2000 option blocks ('OpVa'), then four
        // 0xC000 player blocks ('PlVa'); see Game/MemoryCard/Mgr.h.
        for (std::size_t o : { 0x2000, 0x4000 })
            if (hasMagic(bytes, o, "OpVa")) return SaveGame::Pikmin2;
        for (std::size_t i = 0; i < 4; ++i)
            if (hasMagic(bytes, 0x6000 + i * 0xC000, "PlVa")) return SaveGame::Pikmin2;
    }
    return SaveGame::Unknown;
}

GciCardMeta synthesizeNativeMeta(const std::string& fileName, std::uint32_t length, const std::string& discId)
{
    GciCardMeta meta {};
    std::strncpy(meta.fileName, fileName.c_str(), kCardFilenameMax - 1);
    meta.length = length;
    meta.time = cardTimeNow();
    std::memcpy(meta.gameName, discId.data(), std::min<std::size_t>(4, discId.size()));
    if (discId.size() >= 6) std::memcpy(meta.company, discId.data() + 4, 2);
    else std::memcpy(meta.company, "01", 2);
    meta.iconAddr = meta.commentAddr = 0xffffffffu;
    meta.offsetData = length;
    return meta;
}

PrepareResult prepareSaveImport(const fs::path& picked, const std::string& installedDiscId, const fs::path& card0Dir,
                                PreparedSave& out, std::string& message)
{
    out = PreparedSave {};
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(picked, bytes, message)) return PrepareResult::Error;
    const std::string pickedName = picked.filename().string();
    const SaveGame installGame = saveGameForId(installedDiscId);

    std::string gciError;
    ParsedGci gci;
    if (parseGci(bytes, gci, gciError)) {
        if (!gciMatchesInstalledGame(gci.gameId, installedDiscId)) {
            message = mismatchMessage(gci.gameId, saveGameForId(gci.gameId), installedDiscId);
            return PrepareResult::Error;
        }
        out.saveFileName = gci.saveFileName;
        // metaFromGci uses payload.size() for length and offsetData.
        const GciCardMeta meta = metaFromGci(gci);
        out.data = std::move(gci.payload);
        const auto* m = reinterpret_cast<const std::uint8_t*>(&meta);
        out.meta.assign(m, m + sizeof(meta));
    } else {
        if (pickedName.rfind(".meta_", 0) == 0) {
            message = "That is a save's .meta_ file. Pick the save file next to it, \"" + pickedName.substr(6)
                    + "\", instead.";
            return PrepareResult::Error;
        }
        const SaveGame native = detectNativeSave(bytes);
        if (native == SaveGame::Unknown) {
            const bool namedLikeSave = pickedName == nativeSaveFileName(SaveGame::Pikmin1)
                                    || pickedName == nativeSaveFileName(SaveGame::Pikmin2);
            const bool sizedLikeSave = bytes.size() == kPikmin1SaveFileSize || bytes.size() == kPikmin2SaveFileSize;
            message = namedLikeSave || sizedLikeSave
                        ? "This looks like an Open Nectar save file, but it is damaged or incomplete, so it can't be "
                          "imported."
                        : kNotAGciMessage;
            return PrepareResult::Error;
        }
        if (installGame != SaveGame::Unknown && native != installGame) {
            message = std::string("This is a ") + saveGameName(native) + " save file, but this install is "
                    + saveGameName(installGame) + ".";
            return PrepareResult::Error;
        }
        out.native = true;
        out.saveFileName = nativeSaveFileName(native);
        out.data = std::move(bytes);

        // Its .meta_ record, when one sits beside it; otherwise one like the
        // card layer makes for a new file.
        GciCardMeta meta {};
        std::vector<std::uint8_t> siblingBytes;
        bool hasSibling = false;
        std::string ignored;
        const fs::path sibling = picked.parent_path() / (".meta_" + pickedName);
        if (readIfExists(sibling, siblingBytes, hasSibling, ignored) && hasSibling
            && siblingBytes.size() == sizeof(GciCardMeta)) {
            std::memcpy(&meta, siblingBytes.data(), sizeof(meta));
            static const char kNoGame[4] = { 0, 0, 0, 0 };
            if (std::memcmp(meta.gameName, kNoGame, 4) == 0) {
                // Old builds left the id zeroed; the card layer heals that on read.
                const GciCardMeta fresh = synthesizeNativeMeta(out.saveFileName, 0, installedDiscId);
                std::memcpy(meta.gameName, fresh.gameName, 4);
                std::memcpy(meta.company, fresh.company, 2);
            } else if (!gciMatchesInstalledGame(idFromMeta(meta), installedDiscId)) {
                message = mismatchMessage(idFromMeta(meta), saveGameForId(idFromMeta(meta)), installedDiscId);
                return PrepareResult::Error;
            }
            std::memset(meta.fileName, 0, sizeof(meta.fileName));
            std::strncpy(meta.fileName, out.saveFileName.c_str(), kCardFilenameMax - 1);
            meta.length = static_cast<std::uint32_t>(out.data.size());
        } else {
            meta = synthesizeNativeMeta(out.saveFileName, static_cast<std::uint32_t>(out.data.size()),
                                        installedDiscId);
            out.syntheticMeta = true;
        }
        const auto* m = reinterpret_cast<const std::uint8_t*>(&meta);
        out.meta.assign(m, m + sizeof(meta));
    }

    // The very file already on this card, or the same bytes.
    const fs::path current = card0Dir / out.saveFileName;
    std::error_code ec;
    if (fs::is_regular_file(current, ec)) {
        std::vector<std::uint8_t> currentBytes;
        std::string readError;
        if (fs::equivalent(picked, current, ec)
            || (readFileBytes(current, currentBytes, readError) && currentBytes == out.data)) {
            message = kAlreadyCurrentMessage;
            return PrepareResult::AlreadyCurrent;
        }
    }
    return PrepareResult::Ready;
}

bool importPreparedSave(const PreparedSave& save, const fs::path& card0Dir, bool overwrite, std::string& error,
                        CardBackup* backupMade)
{
    std::error_code ec;
    if (!overwrite && (fs::exists(card0Dir / save.saveFileName, ec)
                       || fs::exists(card0Dir / (".meta_" + save.saveFileName), ec))) {
        error = "A save named \"" + save.saveFileName + "\" is already on the memory card.";
        return false;
    }
    return commitCardPair(card0Dir, save.saveFileName, save.data, &save.meta, error, backupMade);
}

bool restoreLatestCardBackup(const fs::path& card0Dir, std::string& error, CardBackup* restoredFrom,
                             std::string* restoredName)
{
    const std::vector<CardBackup> backups = listCardBackups(card0Dir);
    if (backups.empty()) {
        error = "There is no backup to restore.";
        return false;
    }
    const CardBackup& source = backups.front();
    std::string name;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(source.dir, ec)) {
        const std::string file = entry.path().filename().string();
        if (entry.is_regular_file(ec) && !file.empty() && file[0] != '.') {
            name = file;
            break;
        }
    }
    if (name.empty()) {
        error = "The newest backup has no save file in it.";
        return false;
    }
    std::vector<std::uint8_t> data, meta;
    bool hasData = false, hasMeta = false;
    if (!readIfExists(source.dir / name, data, hasData, error)
        || !readIfExists(source.dir / (".meta_" + name), meta, hasMeta, error))
        return false;
    if (!commitCardPair(card0Dir, name, data, hasMeta ? &meta : nullptr, error, nullptr)) return false;
    if (restoredFrom) *restoredFrom = source;
    if (restoredName) *restoredName = name;
    return true;
}

} // namespace launcher
} // namespace pikmin
