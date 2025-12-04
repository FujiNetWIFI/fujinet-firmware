#include "fujiDevice.h"
#include "debug.h"
#include "fuji_endian.h"

// File flags
enum DET_file_flags_t {
    DET_FF_DIR   = 0x01,
    DET_FF_TRUNC = 0x02,
};

// This function will become a method of the fujiDevice class

size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen,
                                       int year_offset, DET_size_endian_t size_endian,
                                       DET_dir_flags_t dir_flags, DET_has_type_t has_type)
{
    unsigned int idx = 0;

    // File modified date-time
    struct tm *modtime = localtime(&f->modified_time);

    dest[idx++] = modtime->tm_year - year_offset;
    dest[idx++] = modtime->tm_mon + 1;
    dest[idx++] = modtime->tm_mday;
    dest[idx++] = modtime->tm_hour;
    dest[idx++] = modtime->tm_min;
    dest[idx++] = modtime->tm_sec;

    switch (size_endian)
    {
    case SIZE_16_LE:
        {
            uint16_t fsize = htole16(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_16_BE:
        {
            uint16_t fsize = htobe16(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_32_LE:
        {
            uint32_t fsize = htole32(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    case SIZE_32_BE:
        {
            uint32_t fsize = htobe32(f->size);
            memcpy(&dest[idx], &fsize, sizeof(fsize));
            idx += sizeof(fsize);
        }
        break;
    default:
        break;
    }

    dest[idx++] = f->isDir ? DET_FF_DIR : 0;

    // Remember where truncate field is, will fill in after we know how many bytes we need
    unsigned int trunc_field_idx = idx;
    if (dir_flags == HAS_DIR_ENTRY_FLAGS_COMBINED)
        trunc_field_idx--;
    else
        dest[idx++] = 0;

    // File type
    if (has_type == HAS_DIR_ENTRY_TYPE)
        dest[idx++] = MediaType::discover_mediatype(f->filename);

    // Adjust the truncated flag using total bytes of dir entry
    maxlen -= idx;

    // Also subtract a byte for a terminating slash on directories
    if (f->isDir)
        maxlen--;

    // Now that we know actual maxlen we can set the truncated flag
    if (strlen(f->filename) >= maxlen)
        dest[trunc_field_idx] |= DET_FF_TRUNC;

    Debug_printf("Addtl: ");
    for (int i = 0; i < idx; i++)
        Debug_printf("%02x ", dest[i]);
    Debug_printf("\n");
    return idx;
}
