#include "asset_finalize.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace pikmin::launcher;
    using namespace std::chrono;

    int calls = 0;
    std::vector<int> delays;
    auto wait = [&](milliseconds delay) { delays.push_back(static_cast<int>(delay.count())); };
    const auto denied = std::make_error_code(std::errc::permission_denied);
    auto ec = retryAssetRename([&] { return ++calls < 3 ? denied : std::error_code{}; }, wait);
    check(!ec && calls == 3 && delays == std::vector<int>({250, 500}), "transient recovery");

    calls = 0;
    delays.clear();
    const auto busy = std::make_error_code(std::errc::device_or_resource_busy);
    ec = retryAssetRename([&] { return ++calls == 1 ? busy : std::error_code{}; }, wait);
    check(!ec && calls == 2 && delays == std::vector<int>({250}), "busy recovery");

    calls = 0;
    delays.clear();
    ec = retryAssetRename([&] { ++calls; return denied; }, wait);
    check(ec == denied && calls == 5 && delays == std::vector<int>({250, 500, 750, 1000}),
          "bounded exhaustion");

    calls = 0;
    delays.clear();
    const auto missing = std::make_error_code(std::errc::no_such_file_or_directory);
    ec = retryAssetRename([&] { ++calls; return missing; }, wait);
    check(ec == missing && calls == 1 && delays.empty(), "permanent failure is immediate");

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path()
                    / ("nectar finalize test "
                       + std::to_string(steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root), "unique scratch directory");
    const auto partial = root / "assets.partial";
    const auto final = root / "assets";
    check(fs::create_directory(partial), "partial directory");
    {
        std::ofstream file(partial / "marker");
        file << "preserve me";
    }
    check(fs::create_directory(final), "destination directory");
    check(finalizeAssets(partial, final) == std::errc::file_exists,
          "existing destination rejected");
    check(fs::exists(partial / "marker"), "failed finalization preserves extraction");
    fs::remove(final);

#ifdef _WIN32
    HANDLE lock = CreateFileW(partial.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "acquire real directory lock");
    fs::rename(partial, final, ec);
    check(bool(ec), "lock prevents initial rename");
    std::thread release([&] {
        std::this_thread::sleep_for(milliseconds(300));
        CloseHandle(lock);
    });
    ec = finalizeAssets(partial, final);
    release.join();
#else
    ec = finalizeAssets(partial, final);
#endif

    check(!ec && fs::exists(final / "marker") && !fs::exists(partial),
          "real directory finalization");
    fs::remove(final / "marker");
    fs::remove(final);
    fs::remove(root);
    std::cout << "PASS bounded retry, permanent errors, destination preservation and filesystem finalization\n";
}
