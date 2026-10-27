#include "gci_save_import.h"

#include "dolphin_stubs/pc_card_save_root.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

using pikmin::launcher::kCardBlockSize;
using pikmin::launcher::kGciHeaderSize;

namespace {

void writeBe16(std::vector<std::uint8_t>& out, std::size_t offset, std::uint16_t value)
{
    out[offset] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void writeBe32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value)
{
    out[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

std::vector<std::uint8_t> makeSyntheticGci()
{
    std::vector<std::uint8_t> file(kGciHeaderSize + kCardBlockSize, 0);
    std::memcpy(file.data(), "GPIE", 4);
    std::memcpy(file.data() + 4, "01", 2);
    const char name[] = "Pikmin dataFile";
    std::memcpy(file.data() + 8, name, sizeof(name));
    writeBe32(file, 0x28, 123456u);
    writeBe16(file, 0x38, 1); // one block
    file[kGciHeaderSize] = 0xAB;
    file[kGciHeaderSize + 1] = 0xCD;
    return file;
}

std::vector<std::uint8_t> readAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

pikmin::launcher::ParsedGci withFirstByte(pikmin::launcher::ParsedGci gci, std::uint8_t value)
{
    gci.payload[0] = value;
    return gci;
}

bool hasTempFiles(const fs::path& card0)
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(card0, ec))
        if (entry.path().filename().string().rfind(".tmp_", 0) == 0) return true;
    return false;
}

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            std::fprintf(stderr, "FAIL: %s\n", msg);       \
            ++failures;                                    \
        }                                                  \
    } while (0)

// Test-side copy of MemoryCard::calcChecksum, so the fixtures are what the
// game would write.
std::uint32_t pikmin1Sum(const std::vector<std::uint8_t>& b, std::uint32_t length, bool bigEndian)
{
    std::uint32_t sum = 0x32546532u;
    for (std::uint32_t i = 0; i < length / 4; ++i) {
        const std::uint8_t* w = b.data() + i * 4;
        const std::uint32_t v = bigEndian ? (std::uint32_t(w[0]) << 24) | (std::uint32_t(w[1]) << 16) | (std::uint32_t(w[2]) << 8) | w[3]
                                          : (std::uint32_t(w[3]) << 24) | (std::uint32_t(w[2]) << 16) | (std::uint32_t(w[1]) << 8) | w[0];
        const std::uint8_t i2 = static_cast<std::uint8_t>(i);
        sum += (std::uint32_t(i2) << 24) | (std::uint32_t(std::uint8_t(i2 + 1)) << 16)
             | (std::uint32_t(std::uint8_t(i2 - 1)) << 8) | std::uint32_t(std::uint8_t(i2 + 2));
        sum += v;
    }
    return 0x32546532u - sum;
}

std::vector<std::uint8_t> makePikmin1Save(std::uint8_t marker, bool bigEndianSum = true)
{
    std::vector<std::uint8_t> file(pikmin::launcher::kPikmin1SaveFileSize, 0);
    std::memcpy(file.data(), "Pikmin\0\0", 8);
    file[0x100] = marker;
    file[0x10000] = marker;
    writeBe32(file, 0x1FFC, pikmin1Sum(file, 0x1FFC, bigEndianSum));
    return file;
}

std::vector<std::uint8_t> makePikmin2Save()
{
    std::vector<std::uint8_t> file(pikmin::launcher::kPikmin2SaveFileSize, 0xCD);
    std::memcpy(file.data() + 0x2000, "OpVa0002", 8);
    return file;
}

void writeAll(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

pikmin::launcher::GciCardMeta metaOf(const std::vector<std::uint8_t>& bytes)
{
    pikmin::launcher::GciCardMeta meta {};
    if (bytes.size() == sizeof(meta)) std::memcpy(&meta, bytes.data(), sizeof(meta));
    return meta;
}

// Open Nectar card files picked instead of a .gci.
int nativeTests(const std::vector<std::uint8_t>& gciBytes)
{
    namespace L = pikmin::launcher;
    int failures = 0;
    const fs::path root = fs::temp_directory_path() / "gci_native_import_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path card0 = root / "save" / "card0";
    fs::create_directories(card0);
    const fs::path other = root / "other-install";
    L::PreparedSave save;
    std::string msg;

    // Detection uses the game's own checks.
    CHECK(L::detectNativeSave(makePikmin1Save(1)) == L::SaveGame::Pikmin1, "pikmin 1 save detected");
    CHECK(L::detectNativeSave(makePikmin1Save(1, false)) == L::SaveGame::Pikmin1, "legacy host-order sum accepted");
    CHECK(L::detectNativeSave(makePikmin2Save()) == L::SaveGame::Pikmin2, "pikmin 2 save detected");
    CHECK(L::detectNativeSave(std::vector<std::uint8_t>(L::kPikmin1SaveFileSize, 0)) == L::SaveGame::Unknown,
          "zeroed file of the right size rejected");

    // 1. With a .meta_ beside it: that record is used.
    const std::vector<std::uint8_t> first = makePikmin1Save(1);
    writeAll(other / "Pikmin dataFile", first);
    L::GciCardMeta sibling = L::synthesizeNativeMeta("Pikmin dataFile", 155648, "GPIE01");
    sibling.time = 777;
    sibling.iconAddr = 0x40;
    const auto* sp = reinterpret_cast<const std::uint8_t*>(&sibling);
    writeAll(other / ".meta_Pikmin dataFile", std::vector<std::uint8_t>(sp, sp + sizeof(sibling)));
    CHECK(L::prepareSaveImport(other / "Pikmin dataFile", "GPIE01", card0, save, msg) == L::PrepareResult::Ready,
          "native save with sibling meta is ready");
    CHECK(save.native && !save.syntheticMeta && save.saveFileName == "Pikmin dataFile", "sibling meta used");
    CHECK(metaOf(save.meta).time == 777 && metaOf(save.meta).iconAddr == 0x40, "sibling meta fields kept");
    CHECK(L::importPreparedSave(save, card0, false, msg), "native import with sibling meta");
    CHECK(readAll(card0 / "Pikmin dataFile") == first, "native data written");
    CHECK(metaOf(readAll(card0 / ".meta_Pikmin dataFile")).iconAddr == 0x40, "sibling meta written");

    // 2. No .meta_ beside it (and renamed): one is made like the card layer's.
    const std::vector<std::uint8_t> second = makePikmin1Save(2);
    writeAll(root / "loose" / "my old save", second);
    CHECK(L::prepareSaveImport(root / "loose" / "my old save", "GPIE01", card0, save, msg) == L::PrepareResult::Ready,
          "native save without meta is ready");
    const L::GciCardMeta made = metaOf(save.meta);
    CHECK(save.syntheticMeta && save.saveFileName == "Pikmin dataFile", "meta synthesized");
    CHECK(std::strcmp(made.fileName, "Pikmin dataFile") == 0 && made.length == 155648
              && std::memcmp(made.gameName, "GPIE", 4) == 0 && std::memcmp(made.company, "01", 2) == 0
              && made.iconAddr == 0xffffffffu && made.commentAddr == 0xffffffffu && made.offsetData == 155648
              && made.time > 0,
          "synthesized meta matches card_stubs defaultStat");
    L::CardBackup backup;
    CHECK(L::importPreparedSave(save, card0, true, msg, &backup) && !backup.dir.empty(), "overwrite backs up");
    CHECK(readAll(card0 / "Pikmin dataFile") == second, "renamed native file imported under the game's name");
    CHECK(readAll(backup.dir / "Pikmin dataFile") == first, "backup holds the previous save");
    CHECK(!hasTempFiles(card0), "no temp files after native import");

    // A file from card0-backups imports like any other.
    CHECK(L::prepareSaveImport(backup.dir / "Pikmin dataFile", "GPIE01", card0, save, msg) == L::PrepareResult::Ready
              && !save.syntheticMeta,
          "backup folder save is importable with its meta");

    // 3. Wrong game, by contents and size; and the wrong region in a sibling meta.
    writeAll(other / "Pikmin2_SaveData", makePikmin2Save());
    CHECK(L::prepareSaveImport(other / "Pikmin2_SaveData", "GPIE01", card0, save, msg) == L::PrepareResult::Error
              && msg.find("Pikmin 2 save file") != std::string::npos,
          "pikmin 2 file rejected by a pikmin install");
    CHECK(L::prepareSaveImport(other / "Pikmin dataFile", "GPVE01", card0, save, msg) == L::PrepareResult::Error
              && msg.find("Pikmin save file, but this install is Pikmin 2") != std::string::npos,
          "pikmin file rejected by a pikmin 2 install");
    CHECK(L::prepareSaveImport(other / "Pikmin dataFile", "GPIP01", card0, save, msg) == L::PrepareResult::Error
              && msg.find("GPIE01") != std::string::npos,
          "sibling meta from another region rejected");

    // 4. The install's current save, by path or by bytes: nothing happens.
    const std::size_t backupsBefore = L::listCardBackups(card0).size();
    CHECK(L::prepareSaveImport(card0 / "Pikmin dataFile", "GPIE01", card0, save, msg)
                  == L::PrepareResult::AlreadyCurrent
              && msg == L::kAlreadyCurrentMessage,
          "current save by path");
    writeAll(root / "copy" / "Pikmin dataFile", second);
    CHECK(L::prepareSaveImport(root / "copy" / "Pikmin dataFile", "GPIE01", card0, save, msg)
              == L::PrepareResult::AlreadyCurrent,
          "current save by identical bytes");
    CHECK(L::listCardBackups(card0).size() == backupsBefore && readAll(card0 / "Pikmin dataFile") == second,
          "already-current pick changes nothing");

    // 5. Anything else gets the not-a-.gci message.
    std::vector<std::uint8_t> random(5000);
    for (std::size_t i = 0; i < random.size(); ++i) random[i] = static_cast<std::uint8_t>(i * 131 + 7);
    writeAll(root / "notes.txt", random);
    CHECK(L::prepareSaveImport(root / "notes.txt", "GPIE01", card0, save, msg) == L::PrepareResult::Error
              && msg == L::kNotAGciMessage,
          "random file gets the non-GCI error");
    writeAll(root / "broken" / "Pikmin dataFile", std::vector<std::uint8_t>(L::kPikmin1SaveFileSize, 0));
    CHECK(L::prepareSaveImport(root / "broken" / "Pikmin dataFile", "GPIE01", card0, save, msg)
                  == L::PrepareResult::Error
              && msg.find("damaged") != std::string::npos,
          "save-shaped file failing the checksum is called damaged");
    writeAll(root / ".meta_Pikmin dataFile", std::vector<std::uint8_t>(sizeof(L::GciCardMeta), 0));
    CHECK(L::prepareSaveImport(root / ".meta_Pikmin dataFile", "GPIE01", card0, save, msg) == L::PrepareResult::Error
              && msg.find(".meta_") != std::string::npos,
          "a .meta_ file points at its save");

    // GCI header sanity: a huge block count or a wrong size is not a .gci.
    L::ParsedGci parsed;
    std::vector<std::uint8_t> huge = gciBytes;
    writeBe16(huge, 0x38, 2044);
    CHECK(!L::parseGciBytes(huge, parsed, msg) && msg == L::kNotAGciMessage, "block count over 2043 rejected");
    std::vector<std::uint8_t> longer = gciBytes;
    longer.push_back(0);
    CHECK(!L::parseGciBytes(longer, parsed, msg) && msg == L::kNotAGciMessage, "size mismatch rejected");

    // A .gci still goes through the same entry point.
    writeAll(root / "export.gci", gciBytes);
    CHECK(L::prepareSaveImport(root / "export.gci", "GPIE01", card0, save, msg) == L::PrepareResult::Ready
              && !save.native && save.data.size() == kCardBlockSize,
          "gci through prepareSaveImport");
    CHECK(L::prepareSaveImport(root / "export.gci", "GPVE01", card0, save, msg) == L::PrepareResult::Error
              && msg.find("This save is for Pikmin (GPIE01)") != std::string::npos,
          "gci for the other game rejected");

    // Optional: a real export, e.g. NECTAR_REAL_GCI=~/OpenNectar-pr179/test-gci/...gci
    if (const char* real = std::getenv("NECTAR_REAL_GCI")) {
        CHECK(L::parseGciFile(real, parsed, msg), "real .gci parses");
        CHECK(L::detectNativeSave(parsed.payload) == L::SaveGame::Pikmin1, "real .gci payload passes the game's checksum");
        std::printf("real gci: %s, %zu bytes payload\n", parsed.saveFileName.c_str(), parsed.payload.size());
    }

    fs::remove_all(root, ec);
    return failures;
}

// Backups, restore, atomic writes and pruning, on a card of its own.
int backupTests(const pikmin::launcher::ParsedGci& parsed)
{
    namespace L = pikmin::launcher;
    int failures = 0;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "gci_backup_test";
    fs::remove_all(root, ec);
    const fs::path card0 = root / "save" / "card0";
    const fs::path data = card0 / "Pikmin dataFile";
    const fs::path meta = card0 / ".meta_Pikmin dataFile";
    std::string error;

    CHECK(L::cardBackupRoot(card0) == root / "save" / "card0-backups", "backup root is beside card0");
    CHECK(L::cardBackupRoot(root / "save" / "card0" / "") == root / "save" / "card0-backups",
          "backup root with trailing slash");

    // First import: nothing to back up.
    L::CardBackup made;
    CHECK(L::importGciToCard(parsed, card0, false, error, &made), "first import");
    CHECK(made.dir.empty() && L::listCardBackups(card0).empty(), "no backup when card was empty");
    const std::vector<std::uint8_t> firstData = readAll(data), firstMeta = readAll(meta);

    // Overwrite: the old bytes go to a backup outside card0.
    CHECK(L::importGciToCard(withFirstByte(parsed, 0x11), card0, true, error, &made), "overwrite import");
    std::vector<L::CardBackup> backups = L::listCardBackups(card0);
    CHECK(backups.size() == 1 && !made.dir.empty() && backups[0].dir == made.dir, "overwrite made one backup");
    CHECK(made.dir.parent_path() == L::cardBackupRoot(card0), "backup lives in card0-backups");
    CHECK(readAll(made.dir / "Pikmin dataFile") == firstData, "backup holds the old data bytes");
    CHECK(readAll(made.dir / ".meta_Pikmin dataFile") == firstMeta, "backup holds the old meta bytes");
    CHECK(readAll(data)[0] == 0x11, "card has the new save");
    CHECK(!hasTempFiles(card0), "no temp files after overwrite");

    // Failures before the swap leave the card exactly as it was, with no
    // partial files and no new backup.
    const std::vector<std::uint8_t> beforeData = readAll(data), beforeMeta = readAll(meta);
    for (const L::CardWriteStep step : { L::CardWriteStep::WriteData, L::CardWriteStep::WriteMeta,
                                         L::CardWriteStep::Backup, L::CardWriteStep::RenameData,
                                         L::CardWriteStep::RenameMeta }) {
        const std::size_t backupsBefore = L::listCardBackups(card0).size();
        L::failNextCardWriteForTesting(step);
        std::string failed;
        CHECK(!L::importGciToCard(withFirstByte(parsed, 0x22), card0, true, failed), "injected failure reported");
        CHECK(!failed.empty(), "injected failure has a message");
        CHECK(readAll(data) == beforeData && readAll(meta) == beforeMeta, "failed write left the old save intact");
        CHECK(!hasTempFiles(card0), "failed write left no temp files");
        const std::size_t backupsAfter = L::listCardBackups(card0).size();
        const bool swapStep = step == L::CardWriteStep::RenameData || step == L::CardWriteStep::RenameMeta;
        CHECK(swapStep ? backupsAfter <= backupsBefore + 1 : backupsAfter == backupsBefore,
              "failed write before the swap made no backup");
        L::failNextCardWriteForTesting(L::CardWriteStep::None);
    }

    // Restore: puts the newest backup back and backs up what was on the card.
    const std::vector<std::uint8_t> currentData = readAll(data);
    const L::CardBackup newest = L::listCardBackups(card0).front();
    const std::vector<std::uint8_t> newestData = readAll(newest.dir / "Pikmin dataFile");
    L::CardBackup from;
    std::string restoredName;
    CHECK(L::restoreLatestCardBackup(card0, error, &from, &restoredName), "restore");
    CHECK(from.dir == newest.dir && restoredName == "Pikmin dataFile", "restore used the newest backup");
    CHECK(readAll(data) == newestData, "restore brought back the backed-up bytes");
    backups = L::listCardBackups(card0);
    CHECK(!backups.empty() && readAll(backups.front().dir / "Pikmin dataFile") == currentData,
          "restore backed up the save it replaced");
    CHECK(!hasTempFiles(card0), "no temp files after restore");

    // Restore of the very first backup brings back the original bytes.
    {
        const fs::path solo = root / "solo" / "card0";
        CHECK(L::importGciToCard(parsed, solo, false, error), "solo import");
        const std::vector<std::uint8_t> original = readAll(solo / "Pikmin dataFile");
        CHECK(L::importGciToCard(withFirstByte(parsed, 0x33), solo, true, error), "solo overwrite");
        CHECK(L::restoreLatestCardBackup(solo, error), "solo restore");
        CHECK(readAll(solo / "Pikmin dataFile") == original, "solo restore has the original bytes");
        const std::vector<L::CardBackup> soloBackups = L::listCardBackups(solo);
        CHECK(soloBackups.size() == 2 && readAll(soloBackups.front().dir / "Pikmin dataFile")[0] == 0x33,
              "solo restore can be undone");
        CHECK(L::restoreLatestCardBackup(solo, error) && readAll(solo / "Pikmin dataFile")[0] == 0x33,
              "undoing a restore");
    }

    // Pruning keeps the newest five.
    for (int i = 0; i < 8; ++i)
        CHECK(L::importGciToCard(withFirstByte(parsed, static_cast<std::uint8_t>(0x40 + i)), card0, true, error),
              "repeated import");
    backups = L::listCardBackups(card0);
    CHECK(backups.size() == static_cast<std::size_t>(L::kCardBackupKeep), "pruned to five backups");
    CHECK(!backups.empty() && readAll(backups.front().dir / "Pikmin dataFile")[0] == 0x46,
          "newest backup is the save just replaced");
    CHECK(!backups.empty() && readAll(backups.back().dir / "Pikmin dataFile")[0] == 0x42,
          "oldest kept backup is the fifth newest");

    // Labels and stray folders.
    {
        const fs::path labelCard = root / "label" / "card0";
        fs::create_directories(L::cardBackupRoot(labelCard) / "20261025-203700", ec);
        fs::create_directories(L::cardBackupRoot(labelCard) / "20260102-000500-01", ec);
        fs::create_directories(L::cardBackupRoot(labelCard) / "not-a-backup", ec);
        const std::vector<L::CardBackup> labels = L::listCardBackups(labelCard);
        CHECK(labels.size() == 2, "only dated folders count as backups");
        CHECK(labels.size() == 2 && L::describeBackupTime(labels[0]) == "Oct 25, 8:37 PM", "PM label");
        CHECK(labels.size() == 2 && L::describeBackupTime(labels[1]) == "Jan 2, 12:05 AM", "midnight label");
        std::string none;
        CHECK(!L::restoreLatestCardBackup(root / "empty" / "card0", none) && !none.empty(),
              "restore without backups fails cleanly");
    }

    fs::remove_all(root, ec);
    return failures;
}

} // namespace

int main()
{
    int failures = 0;
    const fs::path root = fs::temp_directory_path() / "gci_save_import_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const std::vector<std::uint8_t> gciBytes = makeSyntheticGci();
    pikmin::launcher::ParsedGci parsed;
    std::string error;
    if (!pikmin::launcher::parseGciBytes(gciBytes, parsed, error)) {
        std::fprintf(stderr, "parse failed: %s\n", error.c_str());
        ++failures;
    } else if (parsed.gameId != "GPIE01" || parsed.saveFileName != "Pikmin dataFile"
               || parsed.payload.size() != kCardBlockSize || parsed.dir.time != 123456u) {
        std::fprintf(stderr, "parse contents mismatch\n");
        ++failures;
    }

    if (!pikmin::launcher::gciMatchesInstalledGame("GPIE01", "GPIE01")
        || !pikmin::launcher::gciMatchesInstalledGame("GPIE01", "GPIE")
        || !pikmin::launcher::gciMatchesInstalledGame("GPVE01", "GPVE01")
        || pikmin::launcher::gciMatchesInstalledGame("GPIE01", "GPVE01")) {
        std::fprintf(stderr, "game id match failed\n");
        ++failures;
    }

    const fs::path card0 = root / "save" / "card0";
    if (!pikmin::launcher::importGciToCard(parsed, card0, true, error)) {
        std::fprintf(stderr, "import failed: %s\n", error.c_str());
        ++failures;
    } else {
        const fs::path data = card0 / "Pikmin dataFile";
        const fs::path meta = card0 / ".meta_Pikmin dataFile";
        if (!fs::exists(data) || !fs::exists(meta)) {
            std::fprintf(stderr, "missing card files\n");
            ++failures;
        } else if (fs::file_size(data) != kCardBlockSize) {
            std::fprintf(stderr, "wrong data size\n");
            ++failures;
        } else {
            std::ifstream in(data, std::ios::binary);
            char bytes[2] {};
            in.read(bytes, 2);
            if (bytes[0] != static_cast<char>(0xAB) || bytes[1] != static_cast<char>(0xCD)) {
                std::fprintf(stderr, "payload mismatch\n");
                ++failures;
            }
            pikmin::launcher::GciCardMeta stored {};
            std::ifstream metaIn(meta, std::ios::binary);
            metaIn.read(reinterpret_cast<char*>(&stored), sizeof(stored));
            if (!metaIn || stored.length != kCardBlockSize || std::memcmp(stored.gameName, "GPIE", 4) != 0) {
                std::fprintf(stderr, "meta mismatch\n");
                ++failures;
            }
        }
        std::string blocked;
        if (pikmin::launcher::importGciToCard(parsed, card0, false, blocked)) {
            std::fprintf(stderr, "expected overwrite guard\n");
            ++failures;
        }
    }

    // Legacy card present, no install/save yet: resolving the card path must
    // migrate first, then GCI import must land beside the migrated save.
    // The legacy ~/.local/share save folder (and setenv) only exist off Windows.
#if !defined(_WIN32)
    {
        const fs::path scenario = fs::temp_directory_path() / "gci_legacy_migration_test";
        fs::remove_all(scenario, ec);
        const fs::path install = scenario / "install";
        const fs::path fakeHome = scenario / "home";
        const fs::path legacyCard = fakeHome / ".local" / "share" / "pikmin-native" / "save" / "card0";
        fs::create_directories(legacyCard, ec);
        {
            std::ofstream legacy(legacyCard / "legacy_progress.dat");
            legacy << "keep";
        }
        if (!fs::exists(install / "save", ec)) {
            setenv("HOME", fakeHome.string().c_str(), 1);
            unsetenv("NECTAR_SAVE_DIR");
            unsetenv("XDG_DATA_HOME");
            PcCardSaveRootOptions options;
            options.installDir = install;
            options.logMigration = false;
            const fs::path card0 = pikmin::launcher::card0DirectoryForInstall(install);
            if (card0 != install / "save" / "card0" || !fs::exists(card0 / "legacy_progress.dat")) {
                std::fprintf(stderr, "legacy migration before import failed\n");
                ++failures;
            } else if (!pikmin::launcher::importGciToCard(parsed, card0, true, error)) {
                std::fprintf(stderr, "import after migration failed: %s\n", error.c_str());
                ++failures;
            } else if (!fs::exists(card0 / "legacy_progress.dat") || !fs::exists(card0 / "Pikmin dataFile")) {
                std::fprintf(stderr, "expected both legacy and imported saves on card0\n");
                ++failures;
            }
        }
        fs::remove_all(scenario, ec);
    }
#endif

    failures += backupTests(parsed);
    failures += nativeTests(makeSyntheticGci());

    fs::remove_all(root, ec);
    if (failures == 0) std::printf("gci_save_import_test: PASS\n");
    else std::printf("gci_save_import_test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
