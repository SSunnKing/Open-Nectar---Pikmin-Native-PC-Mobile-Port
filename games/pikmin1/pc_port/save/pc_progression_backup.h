#pragma once

/**
 * @file pc_progression_backup.h
 * @brief Backing up the save before a repaired state is written to disk.
 *
 * A repair edits the loaded game in memory. Nothing reaches the disk until the
 * normal day-end save. At that moment the file as it was loaded is copied
 * aside first. If that copy cannot be made, the save is refused, so a repair
 * can never replace the only good copy. If the process stops before the save,
 * the disk still holds the original and the same deterministic repair happens
 * on the next load.
 */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pc::progression {

enum class BackupResult { Written, AlreadyBackedUp, Failed };

struct BackupOutcome {
	BackupResult result = BackupResult::Failed;
	std::filesystem::path path;
	std::string error;
};

/**
 * Write a verified copy of a save image to <folder>/progression-repair/slotS-dayDD[-N].bin.
 * The bytes go to a temp file, are read back and compared, then renamed into place.
 * An identical existing backup is reused. A different one is never overwritten.
 */
BackupOutcome backupSaveImage(const std::filesystem::path& folder, int slot, int day, const void* data, std::size_t size);

/** The repair that is waiting for the next save. */
class PendingRepair {
public:
	/** Remember the image the save was loaded from. Called for every load; cheap. */
	void rememberLoadedImage(const void* data, std::size_t size);

	/** A repair changed the loaded game. The remembered image is the one to back up. */
	void markRepaired(int slot, int day);

	/** True from markRepaired until a save succeeds. */
	bool active() const { return mActive; }

	/**
	 * Call before the day-end save writes anything. Returns true when the save may
	 * proceed (nothing pending, or the backup exists). Returns false when the backup
	 * failed: the save must be refused. `error` is filled on failure.
	 */
	bool beforeSave(const std::filesystem::path& folder, std::string* error);

	/** Call after the save was written. The repaired state is now the saved state. */
	void afterSave(bool succeeded);

	/** The file on the card changed (a save succeeded): later backups must copy this, not the file loaded earlier. */
	void replaceImage(const void* data, std::size_t size);

	/** Forget everything (a different save was loaded or a new game started). */
	void reset();

	const std::filesystem::path& lastBackupPath() const { return mLastBackup; }

private:
	std::vector<std::uint8_t> mImage;
	bool mActive    = false;
	bool mBackedUp  = false;
	int mSlot       = 0;
	int mDay        = 0;
	std::filesystem::path mLastBackup;
};

} // namespace pc::progression
