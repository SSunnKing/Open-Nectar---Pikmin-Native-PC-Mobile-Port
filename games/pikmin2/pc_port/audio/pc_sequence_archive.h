#ifndef PC_SEQUENCE_ARCHIVE_H
#define PC_SEQUENCE_ARCHIVE_H

#include "types.h"

#include <string>
#include <vector>

struct PCSequenceEntry {
    std::string name;
    u32 offset = 0;
    u32 size = 0;
    u16 redirect = 0xFFFF;
};

class PCSequenceArchive {
public:
    bool load(const char* arcPath, const u8* header, size_t headerSize);
    const PCSequenceEntry* entry(u32 index) const;
    bool read(u32 index, std::vector<u8>& sequence) const;
    size_t size() const { return mEntries.size(); }
    const std::string& error() const { return mError; }

private:
    std::string mArcPath;
    std::vector<PCSequenceEntry> mEntries;
    std::string mError;
};

#endif
