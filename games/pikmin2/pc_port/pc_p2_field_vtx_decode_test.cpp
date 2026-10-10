#include "pc_p2_field_vtx_decode.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
int main(int argc, char** argv) {
    std::array<int, 512> info; info.fill(-1);
    int visits = 0;
    auto visit = [&](unsigned position, unsigned color) {
        ++visits; if (info[position] == -1) info[position] = color;
    };
    // Odd offsets and a high index prove both byte order and unaligned reads.
    std::vector<unsigned char> strip{0x98,0,3, 0xaa,0,1,0,2, 0xaa,1,0,0,3, 0xaa,0,2,0,4};
    assert(p2_field_vtx::decode(strip.data(),strip.size(),5,1,3,512,8,visit));
    assert(visits == 3 && info[1] == 2 && info[256] == 3 && info[2] == 4);
    // Strip and fan entries retain the original first-color-per-position rule.
    std::vector<unsigned char> fan{0xa0,0,3, 0xaa,0,1,0,7, 0xaa,0,3,0,5, 0xaa,0,4,0,6,0};
    assert(p2_field_vtx::decode(fan.data(),fan.size(),5,1,3,512,8,visit));
    assert(visits == 6 && info[1] == 2 && info[3] == 5 && info[4] == 6);
    auto reject = [&](std::vector<unsigned char> bytes,int pos=1,int color=3) {
        const int before = visits;
        assert(!p2_field_vtx::decode(bytes.data(),bytes.size(),5,pos,color,512,8,visit));
        assert(visits == before);
    };
    auto truncated = strip; truncated.pop_back(); reject(truncated);
    reject({0x98,0});
    auto excessive = strip; excessive[1]=1; reject(excessive); // BE count259
    auto shortStrip = strip; shortStrip[2]=2; reject(shortStrip);
    auto badPosition = strip; badPosition[4]=0xff; reject(badPosition);
    auto badColor = strip; badColor[7]=8; reject(badColor);
    reject(strip,-1,3); reject(strip,1,5);
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        assert(file);
        std::vector<unsigned char> actual((std::istreambuf_iterator<char>(file)), {});
        int actualVisits = 0;
        assert(p2_field_vtx::decode(actual.data(),actual.size(),8,0,4,118,120,
            [&](unsigned,unsigned) { ++actualVisits; }));
        assert(actualVisits == 316);
        std::puts("private Awakening Wood farm display list passed (316 vertices)");
    }
    std::puts("field vertex GX BE strip/fan and malformed bounds passed");
}
