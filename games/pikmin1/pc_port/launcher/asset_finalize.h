#pragma once

#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

namespace pikmin::launcher {

// A scanner may briefly hold newly extracted files open on Windows. Bound the
// finalization delay to 2.5 seconds (five attempts); never repeat extraction.
template <typename Rename, typename Wait>
std::error_code retryAssetRename(Rename rename, Wait wait)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::error_code ec = rename();
        const bool transient = ec == std::errc::permission_denied
                            || ec == std::errc::device_or_resource_busy;
        if (!ec || !transient || attempt == 4) return ec;
        wait(std::chrono::milliseconds(250 * (attempt + 1)));
    }
    return {}; // All paths return inside the bounded loop.
}

inline std::error_code finalizeAssets(const std::filesystem::path& partial,
                                     const std::filesystem::path& destination)
{
    return retryAssetRename([&] {
        std::error_code ec;
        if (std::filesystem::exists(destination, ec))
            return std::make_error_code(std::errc::file_exists);
        if (ec) return ec;
        std::filesystem::rename(partial, destination, ec);
        return ec;
    }, [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); });
}

} // namespace pikmin::launcher
