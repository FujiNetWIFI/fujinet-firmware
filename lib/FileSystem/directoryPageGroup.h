#ifndef DIRECTORY_PAGE_GROUP_H
#define DIRECTORY_PAGE_GROUP_H

#include <cstdint>
#include <vector>
#include <cstring>
#include <ctime>

#include "fnFS.h"  // for fsdir_entry_t
#include "../../../include/debug.h"  // for Debug_printf

// Include platform-specific media type definitions
#include "mediaTypeProxy.h"

// Helper class to manage directory entry page groups
class DirectoryPageGroup {
public:
    static const uint8_t HEADER_SIZE = 5;  // Flags(1) + EntryCount(1) + Size(2) + Index(1)
    static const uint8_t ENTRY_HEADER_SIZE = 8;  // Timestamp(4) + Size(3) + Flags/Type(1)

    std::vector<uint8_t> data;
    uint8_t entry_count;
    bool is_last_group;
    uint8_t index;

    DirectoryPageGroup() : entry_count(0), is_last_group(false) {
        // Pre-allocate space for header to avoid reallocation
        data.resize(HEADER_SIZE, 0);
    }

    bool add_entry(fsdir_entry_t* f) {
        if (f == nullptr) {
            return false;
        }

        size_t filename_len = strlen(f->filename) + 1;
        size_t entry_size = ENTRY_HEADER_SIZE + filename_len;

        // Pre-allocate the space needed for this entry
        size_t new_size = data.size() + entry_size;
        data.resize(new_size);

        // Get pointer to where we'll write the entry
        size_t current_pos = data.size() - entry_size;

        set_block_entry_details(f, &data[current_pos]);
        // WE DO ***NOT*** NEED TO ADD A TRAILING "/" TO THE FILENAME FOR DIRS!
        // we're already indicating this through flags.
        // so please do not add one here if you read this, as it will break client code.
        memcpy(&data[current_pos + ENTRY_HEADER_SIZE], f->filename, filename_len);

        // Debug_printf("Entry added at pos %d: %s\n", current_pos, f->filename);
        entry_count++;
        return true;
    }

    void finalize() {
        if (data.size() < HEADER_SIZE) {
            return;
        }

        // Set flags (bit 7 indicates last group)
        data[0] = is_last_group ? 0x80 : 0x00;
        // Set entry count
        data[1] = entry_count;
        // Set data size (excluding header)
        uint16_t data_size = data.size() - HEADER_SIZE;
        data[2] = data_size & 0xFF;
        data[3] = (data_size >> 8) & 0xFF;
        // Set group index
        data[4] = index;
    }

private:
    // Helper function to set file details in block format
    static size_t set_block_entry_details(fsdir_entry_t *f, uint8_t *dest) {
        size_t bytes_written = 0;

        // Pack timestamp
        struct tm *modtime = localtime(&f->modified_time);

        // Byte 0: Year since 1970
        dest[bytes_written++] = modtime->tm_year - 70;  // Convert from years since 1900 to years since 1970

        // Byte 1: FFFF MMMM (4 bits flags, 4 bits month 1-12)
        // Bit 7: Directory flag
        // Bits 6-4: Reserved
        // Bits 3-0: Month 1-12
        uint8_t flags = (f->isDir ? 0x80 : 0x00) |      // Directory flag in highest bit
                       ((modtime->tm_mon + 1) & 0x0F);   // Month 1-12
        dest[bytes_written++] = flags;

        // Byte 2: DDDDD HHH (5 bits day 1-31, 3 high bits of hour)
        dest[bytes_written++] = ((modtime->tm_mday & 0x1F) << 3) |
                               ((modtime->tm_hour >> 2) & 0x07);

        // Byte 3: HH mmmmmm (2 low bits of hour, 6 bits minute 0-59)
        dest[bytes_written++] = ((modtime->tm_hour & 0x03) << 6) |
                               (modtime->tm_min & 0x3F);

        // File size (3 bytes, little-endian, 0 for directories)
        uint32_t size = f->isDir ? 0 : f->size;
        for(int i = 0; i < 3; i++) {
            dest[bytes_written++] = (size >> (i*8)) & 0xFF;
        }

        // Media type (full byte)
        uint8_t media_type = MediaType::discover_mediatype(f->filename);
        dest[bytes_written++] = media_type;

        return bytes_written;
    }
};

#endif /* DIRECTORY_PAGE_GROUP_H */
