#include "toast_layout.h"
#include <cassert>
#include <iostream>
using pikmin::launcher::fitToast;
int main() {
    auto width = [](const std::string& s) { return float(s.size()); };
    assert(fitToast("short", 20, width) == "short");
    assert(fitToast("one two three four five six", 12, width) == "one two\nthree fou...");
    assert(fitToast("ééééé", 4, width) == "éé\n...");
    for (int w : {4, 8, 12, 24, 80}) {
        const auto s = fitToast("Deleted HD 4K (3.2 GB freed) from /very/long/path/with/no/spaces/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", float(w), width);
        const auto newline = s.find('\n');
        assert(newline == std::string::npos || s.find('\n',newline+1)==std::string::npos);
        assert(width(s.substr(0,newline)) <= w);
        if (newline != std::string::npos) assert(width(s.substr(newline+1)) <= w);
    }
    std::cout << "two-line toast bounds: PASS\n";
}
