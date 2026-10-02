#include "audio/pc_sequence_archive.h"

#include <fstream>

namespace {
u16 be16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
u32 be32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16)
         | (static_cast<u32>(p[2]) << 8) | p[3];
}
std::string fixedString(const u8* p, size_t size) {
    size_t count = 0;
    while (count < size && p[count]) ++count;
    return std::string(reinterpret_cast<const char*>(p), count);
}
}

bool PCSequenceArchive::load(const char* arcPath, const u8* header, size_t headerSize) {
    mEntries.clear();
    mError.clear();
    if (!arcPath || !header || headerSize < 0x20
        || std::string(reinterpret_cast<const char*>(header), 4) != "BARC") {
        mError = "invalid BARC header";
        return false;
    }
    const u32 count = be32(header + 0x0C);
    if (count > 256 || headerSize < 0x20 + static_cast<size_t>(count) * 0x20) {
        mError = "truncated BARC entry table";
        return false;
    }
    std::ifstream arc(arcPath, std::ios::binary | std::ios::ate);
    if (!arc) {
        mError = "could not open sequence ARC";
        return false;
    }
    const size_t arcSize = static_cast<size_t>(arc.tellg());
    mArcPath = arcPath;
    mEntries.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* src = header + 0x20 + i * 0x20;
        PCSequenceEntry item;
        item.name = fixedString(src, 14);
        item.redirect = be16(src + 14);
        item.offset = be32(src + 24);
        item.size = be32(src + 28);
        if (item.redirect == 0xFFFF
            && (item.offset > arcSize || item.size > arcSize - item.offset)) {
            mError = "sequence outside ARC at index " + std::to_string(i);
            mEntries.clear();
            return false;
        }
        mEntries.push_back(std::move(item));
    }
    return true;
}

const PCSequenceEntry* PCSequenceArchive::entry(u32 index) const {
    for (size_t redirects = 0; redirects <= mEntries.size(); ++redirects) {
        if (index >= mEntries.size()) return nullptr;
        const PCSequenceEntry& item = mEntries[index];
        if (item.redirect == 0xFFFF) return &item;
        index = item.redirect;
    }
    return nullptr;
}

bool PCSequenceArchive::read(u32 index, std::vector<u8>& sequence) const {
    sequence.clear();
    const PCSequenceEntry* item = entry(index);
    if (!item || !item->size) return false;
    std::ifstream arc(mArcPath, std::ios::binary);
    if (!arc) return false;
    arc.seekg(item->offset);
    sequence.resize(item->size);
    return static_cast<bool>(arc.read(reinterpret_cast<char*>(sequence.data()), item->size));
}
