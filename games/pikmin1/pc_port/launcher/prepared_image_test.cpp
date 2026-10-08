#include "prepared_image.h"
#include "launcher_platform.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char** argv)
{
    // Run this executable as a synthetic converter to exercise argument passing
    // and child-process waiting without Dolphin or any retail game data.
    if (argc > 1 && std::string(argv[1]) == "convert") {
        if (argc != 8 || std::string(argv[2]) != "-i" || std::string(argv[4]) != "-o"
            || std::string(argv[6]) != "-f" || std::string(argv[7]) != "iso") return 2;
        std::ifstream source(argv[3]);
        std::string contents;
        std::getline(source, contents);
        std::ofstream(argv[5]) << contents;
        return contents == "success" ? 0 : 9;
    }
    using namespace pikmin::launcher;
    const auto root = fs::temp_directory_path() / ("nectar conversion test " + std::to_string(std::random_device{}()));
    check(fs::create_directory(root), "reserve test directory");
    const auto source = root / "disc & spaced.RVZ";
    std::ofstream(source) << "success";
    std::string error;
    fs::path temporary;
    int pumps = 0;
    const auto converter = fs::absolute(argv[0]);
    const auto run = [&](const fs::path& input, const fs::path& output, std::string& failure) {
        temporary = output.parent_path();
        return platform::convertImage(converter, input, output, [&] { ++pumps; }, failure);
    };
    {
        PreparedImage prepared;
        check(prepared.prepare(source, run, error), error.c_str());
        check(prepared.image != source && fs::file_size(prepared.image) == 7, "converted output");
        check(fs::exists(source), "source preserved");
    }
    check(!fs::exists(temporary), "successful conversion cleaned up");
    std::ofstream(source) << "failure";
    {
        PreparedImage prepared;
        check(!prepared.prepare(source, run, error), "nonzero child exit rejected");
        check(!fs::exists(temporary), "failed conversion cleaned up");
        check(fs::file_size(source) == 7, "failed conversion preserves source");
        check(!prepared.prepare(source, [&](const fs::path&, const fs::path& output, std::string&) {
            temporary = output.parent_path();
            return true;
        }, error), "success without output rejected");
        check(!fs::exists(temporary), "missing output cleaned up");
        const auto iso = root / "original.iso";
        std::ofstream(iso) << "original";
        check(prepared.prepare(iso, [](const fs::path&, const fs::path&, std::string&) {
            check(false, "uncompressed images must bypass converter"); return false;
        }, error), "ISO bypass");
        check(prepared.image == iso, "ISO path unchanged");
    }
    check(fs::exists(root / "original.iso"), "original ISO preserved after destructor");
    fs::remove(root / "original.iso");
    fs::remove(source);
    fs::remove(root);
    std::cout << "PASS conversion process, failure propagation, temporary cleanup and ISO bypass\n";
}
