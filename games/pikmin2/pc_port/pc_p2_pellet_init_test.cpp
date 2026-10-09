#include "Game/pelletMgr.h"
#include <cassert>
#include <cstring>
#include <new>
#include <cstdio>

int main() {
    alignas(Game::PelletInitArg) unsigned char hostBytes[sizeof(Game::PelletInitArg)];
    alignas(Game::PelletInitArg) unsigned char guestBytes[sizeof(Game::PelletInitArg)];
    std::memset(hostBytes, 0x27, sizeof(hostBytes));
    std::memset(guestBytes, 0xb7, sizeof(guestBytes));
    auto* host = new (hostBytes) Game::PelletInitArg;
    auto* guest = new (guestBytes) Game::PelletInitArg;
    // Reproduce generatorBirth: leave color defaulted, then copy the field
    // that Pellet::onInit unconditionally copies for every kind.
    char name[] = "treasure";
    host->mTextIdentifier = guest->mTextIdentifier = name;
    host->mPelletType = guest->mPelletType = PelletType::Treasure;
    host->mPelletIndex = guest->mPelletIndex = 17;
    const u16 hostColor = host->mPelletColor, guestColor = guest->mPelletColor;
    assert(hostColor == 0 && guestColor == 0 && hostColor == guestColor);
    // Number and berry producers retain their explicitly assigned colors.
    host->mPelletColor = 2;
    assert(static_cast<u16>(host->mPelletColor) == 2);
    host->~PelletInitArg(); guest->~PelletInitArg();
    std::puts("pellet birth defaults resist poisoned stack and preserve explicit colors");
}

