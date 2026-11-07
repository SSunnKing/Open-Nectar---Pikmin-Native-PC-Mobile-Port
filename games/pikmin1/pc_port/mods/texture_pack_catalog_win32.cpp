// Windows half of texture_pack_catalog.h. Kept out of the header: the game's
// decompiled headers (AtxStream.h) typedef their own HWND, which clashes with
// <windows.h> in any translation unit that sees both (pc_settings.cpp).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "texture_pack_catalog.h"

namespace nectar::textures {

bool windowsUnsafeEntry(const fs::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

} // namespace nectar::textures
#endif
