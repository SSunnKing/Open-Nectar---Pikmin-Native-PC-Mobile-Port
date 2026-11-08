#include "pc_progression_backup.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace pc::progression {

namespace {

bool readAll(const fs::path& path, std::vector<char>& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return !in.bad();
}

bool sameBytes(const fs::path& path, const void* data, std::size_t size)
{
	std::vector<char> existing;
	return readAll(path, existing) && existing.size() == size && (size == 0 || std::memcmp(existing.data(), data, size) == 0);
}

} // namespace

BackupOutcome backupSaveImage(const fs::path& folder, int slot, int day, const void* data, std::size_t size)
{
	BackupOutcome out;
	if (!data || size == 0) {
		out.error = "no save image to back up";
		return out;
	}
	std::error_code ec;
	const fs::path dir = folder / "progression-repair";
	fs::create_directories(dir, ec);
	if (ec || !fs::is_directory(dir, ec)) {
		out.error = "cannot create the backup folder";
		return out;
	}

	// First free name; an identical backup is reused, a different one is kept.
	fs::path target;
	for (int n = 1; n <= 99; ++n) {
		char name[48];
		if (n == 1) std::snprintf(name, sizeof(name), "slot%d-day%02d.bin", slot, day);
		else std::snprintf(name, sizeof(name), "slot%d-day%02d-%d.bin", slot, day, n);
		const fs::path candidate = dir / name;
		if (!fs::exists(candidate, ec)) {
			target = candidate;
			break;
		}
		if (sameBytes(candidate, data, size)) {
			out.result = BackupResult::AlreadyBackedUp;
			out.path   = candidate;
			return out;
		}
	}
	if (target.empty()) {
		out.error = "too many backups for this day";
		return out;
	}

	const fs::path tmp = dir / (".tmp_" + target.filename().string());
	{
		std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
		bool ok = !!file;
		if (ok) ok = !!file.write(static_cast<const char*>(data), std::streamsize(size));
		if (ok) ok = !!file.flush();
		file.close();
		if (!ok || file.fail()) {
			fs::remove(tmp, ec);
			out.error = "cannot write the backup";
			return out;
		}
	}
	if (!sameBytes(tmp, data, size)) {
		fs::remove(tmp, ec);
		out.error = "the backup did not read back identical";
		return out;
	}
	fs::rename(tmp, target, ec);
	if (ec) {
		fs::remove(tmp, ec);
		out.error = "cannot move the backup into place";
		return out;
	}
	out.result = BackupResult::Written;
	out.path   = target;
	return out;
}

void PendingRepair::rememberLoadedImage(const void* data, std::size_t size)
{
	mImage.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + (data ? size : 0));
	mActive   = false;
	mBackedUp = false;
}

void PendingRepair::replaceImage(const void* data, std::size_t size)
{
	mImage.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + (data ? size : 0));
}

void PendingRepair::markRepaired(int slot, int day)
{
	mActive   = true;
	mBackedUp = false;
	mSlot     = slot;
	mDay      = day;
}

bool PendingRepair::beforeSave(const fs::path& folder, std::string* error)
{
	if (!mActive || mBackedUp) return true;
	const BackupOutcome outcome = backupSaveImage(folder, mSlot, mDay, mImage.data(), mImage.size());
	if (outcome.result == BackupResult::Failed) {
		if (error) *error = outcome.error;
		return false;
	}
	mBackedUp   = true;
	mLastBackup = outcome.path;
	return true;
}

void PendingRepair::afterSave(bool succeeded)
{
	if (!mActive) return;
	if (succeeded) {
		mActive   = false;
		mBackedUp = false;
	}
	// A failed save keeps the repair pending: the backup stands and the next save retries.
}

void PendingRepair::reset()
{
	mImage.clear();
	mActive   = false;
	mBackedUp = false;
	mLastBackup.clear();
}

} // namespace pc::progression
