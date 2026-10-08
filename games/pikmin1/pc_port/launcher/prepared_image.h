#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <random>
#include <string>

namespace pikmin::launcher {

inline bool isCompressedImage(const std::filesystem::path& image)
{
    std::string ext = image.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".rvz" || ext == ".wia" || ext == ".gcz";
}

// Own only a newly created temporary directory, never the source image or a
// previous conversion. Scope this object before launchGame (which exits).
class PreparedImage {
public:
    PreparedImage() = default;
    PreparedImage(const PreparedImage&) = delete;
    PreparedImage& operator=(const PreparedImage&) = delete;
    ~PreparedImage() { clear(); }

    using Convert = std::function<bool(const std::filesystem::path&,
                                      const std::filesystem::path&, std::string&)>;

    bool prepare(const std::filesystem::path& source, const Convert& convert, std::string& error)
    {
        clear();
        image = source;
        if (!isCompressedImage(source)) return true;
        std::error_code ec;
        const auto root = std::filesystem::temp_directory_path(ec);
        if (ec) { error = "Could not find the temporary folder: " + ec.message(); return false; }
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto candidate = root / ("nectar-disc-" + std::to_string(random())
                                          + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate, ec)) {
                temporary = candidate;
                break;
            }
            if (ec && ec != std::errc::file_exists) {
                error = "Could not create a temporary conversion folder: " + ec.message();
                return false;
            }
        }
        if (temporary.empty()) { error = "Could not reserve a temporary conversion folder."; return false; }
        image = temporary / "disc.iso";
        if (!convert(source, image, error)) { clear(); return false; }
        if (!std::filesystem::is_regular_file(image, ec) || std::filesystem::file_size(image, ec) == 0 || ec) {
            error = "The converter did not produce an ISO. Check free space and your disc image.";
            clear();
            return false;
        }
        return true;
    }

    std::filesystem::path image;

private:
    std::filesystem::path temporary;
    void clear()
    {
        if (!temporary.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(temporary, ec);
            temporary.clear();
        }
    }
};
} // namespace pikmin::launcher
