#ifdef BUILD_MAC

#include "mediaTypeFloppyImage.h"
#include "macGCR.h"

#include <string.h>
#include <stdlib.h>
#include "../../include/debug.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#define TRACK_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)
#define TRACK_FREE(p)  heap_caps_free(p)
#else
#define TRACK_ALLOC(n) malloc(n)
#define TRACK_FREE(p)  free(p)
#endif

static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Work out where the sector data lives: raw image or DiskCopy 4.2.
bool MediaTypeFloppyImage::parse_header(uint32_t disksize)
{
    uint8_t hdr[0x54];

    data_offset = 0;
    tag_offset = 0;

    if (disksize == MAC_GCR_400K_BYTES || disksize == MAC_GCR_800K_BYTES)
    {
        data_size = disksize;
    }
    else
    {
        // DiskCopy 4.2: 64-byte pascal name, then data size, tag size,
        // checksums, disk format, format byte, magic 0x0100
        if (fseek(_media_fileh, 0, SEEK_SET) || fread(hdr, 1, sizeof(hdr), _media_fileh) != sizeof(hdr))
        {
            Debug_printf("\nFloppy image: cannot read header");
            return false;
        }
        uint32_t dsize = be32(&hdr[0x40]);
        uint32_t tsize = be32(&hdr[0x44]);
        uint16_t magic = (hdr[0x52] << 8) | hdr[0x53];
        if (magic != 0x0100 || (dsize != MAC_GCR_400K_BYTES && dsize != MAC_GCR_800K_BYTES))
        {
            Debug_printf("\nFloppy image: %lu bytes is not a 400K/800K raw image or a DiskCopy 4.2 image",
                         (unsigned long)disksize);
            return false;
        }
        if (0x54 + dsize + tsize > disksize)
        {
            Debug_printf("\nFloppy image: DiskCopy 4.2 header claims more data than the file holds");
            return false;
        }
        data_offset = 0x54;
        data_size = dsize;
        if (tsize == (dsize / MAC_GCR_SECTOR_SIZE) * MAC_GCR_TAG_SIZE)
            tag_offset = 0x54 + dsize;
        Debug_printf("\nFloppy image: DiskCopy 4.2, %luK data, %s tags, disk format %u, format byte %02x",
                     (unsigned long)dsize / 1024, tag_offset ? "with" : "no", hdr[0x50], hdr[0x51]);
    }

    if (data_size == MAC_GCR_800K_BYTES)
    {
        num_sides = 2;
        format_byte = MAC_GCR_FORMAT_DS;
    }
    else
    {
        num_sides = 1;
        format_byte = MAC_GCR_FORMAT_SS;
    }
    num_blocks = data_size / MAC_GCR_SECTOR_SIZE;
    return true;
}

void MediaTypeFloppyImage::free_tracks()
{
    for (int i = 0; i < MAX_TRACKS; i++)
    {
        if (trk_ptrs[i])
            TRACK_FREE(trk_ptrs[i]);
        trk_ptrs[i] = nullptr;
        trk_bits[i] = 0;
        trk_bytes[i] = 0;
    }
}

bool MediaTypeFloppyImage::encode_all_tracks()
{
    uint8_t sectors[MAC_GCR_MAX_SECTORS * MAC_GCR_SECTOR_SIZE];
    uint8_t tags[MAC_GCR_MAX_SECTORS * MAC_GCR_TAG_SIZE];
    size_t total = 0;

    for (int cyl = 0; cyl < MAC_GCR_CYLINDERS; cyl++)
    {
        int nsec = mac_gcr_sectors_per_track(cyl);
        size_t tbytes = mac_gcr_track_bytes(cyl);

        for (int side = 0; side < num_sides; side++)
        {
            int t = cyl * 2 + side;
            // image layout: per cylinder, side 0 sectors then side 1 sectors
            uint32_t first_sector = (uint32_t)mac_gcr_sectors_before_cyl(cyl) * num_sides + (uint32_t)side * nsec;

            if (fseek(_media_fileh, data_offset + first_sector * MAC_GCR_SECTOR_SIZE, SEEK_SET) ||
                fread(sectors, 1, nsec * MAC_GCR_SECTOR_SIZE, _media_fileh) != (size_t)nsec * MAC_GCR_SECTOR_SIZE)
            {
                Debug_printf("\nFloppy image: read error at cylinder %d side %d", cyl, side);
                return false;
            }
            const uint8_t *tagp = nullptr;
            if (tag_offset)
            {
                if (fseek(_media_fileh, tag_offset + first_sector * MAC_GCR_TAG_SIZE, SEEK_SET) == 0 &&
                    fread(tags, 1, nsec * MAC_GCR_TAG_SIZE, _media_fileh) == (size_t)nsec * MAC_GCR_TAG_SIZE)
                    tagp = tags;
            }

            trk_ptrs[t] = (uint8_t *)TRACK_ALLOC(tbytes);
            if (trk_ptrs[t] == nullptr)
            {
                Debug_printf("\nFloppy image: out of memory at track %d (%u bytes so far)", t, (unsigned)total);
                return false;
            }
            uint32_t nbits = mac_gcr_encode_track(sectors, tagp, cyl, side, format_byte, trk_ptrs[t], tbytes);
            if (nbits == 0)
            {
                Debug_printf("\nFloppy image: encoding failed at cylinder %d side %d", cyl, side);
                return false;
            }
            trk_bits[t] = nbits;
            trk_bytes[t] = tbytes;
            total += tbytes;
        }
    }
    Debug_printf("\nFloppy image: encoded %d cylinders x %d side(s) into %u bytes of GCR track data",
                 MAC_GCR_CYLINDERS, num_sides, (unsigned)total);
    return true;
}

mediatype_t MediaTypeFloppyImage::mount(FILE *f, uint32_t disksize)
{
    _media_fileh = f;
    floppy_emulation = true;
    optimal_bit_timing = MAC_GCR_BIT_TIMING;

    if (!parse_header(disksize))
    {
        _media_fileh = nullptr;
        return MEDIATYPE_UNKNOWN;
    }

    Debug_printf("\nFloppy image: %luK %s-sided GCR, encoding tracks...",
                 (unsigned long)data_size / 1024, num_sides == 2 ? "double" : "single");

    if (!encode_all_tracks())
    {
        free_tracks();
        _media_fileh = nullptr;
        return MEDIATYPE_UNKNOWN;
    }

    // The whole disk now lives in RAM; the file is only touched again by writes.
    return MEDIATYPE_DSK;
}

bool MediaTypeFloppyImage::write_sector(int cyl, int side, int sec, const uint8_t *in524)
{
    if (_media_fileh == nullptr || cyl < 0 || cyl >= MAC_GCR_CYLINDERS || side < 0 || side >= num_sides)
        return false;
    int nsec = mac_gcr_sectors_per_track(cyl);
    if (sec < 0 || sec >= nsec)
        return false;

    uint32_t block = (uint32_t)mac_gcr_sectors_before_cyl(cyl) * num_sides + (uint32_t)side * nsec + (uint32_t)sec;

    if (fseek(_media_fileh, data_offset + block * MAC_GCR_SECTOR_SIZE, SEEK_SET) ||
        fwrite(in524 + MAC_GCR_TAG_SIZE, 1, MAC_GCR_SECTOR_SIZE, _media_fileh) != MAC_GCR_SECTOR_SIZE)
    {
        Debug_printf("\nFloppy image: write of block %lu failed", (unsigned long)block);
        return false;
    }
    if (tag_offset)
    {
        if (fseek(_media_fileh, tag_offset + block * MAC_GCR_TAG_SIZE, SEEK_SET) == 0)
            fwrite(in524, 1, MAC_GCR_TAG_SIZE, _media_fileh);
    }
    fflush(_media_fileh);

    int t = cyl * 2 + side;
    if (trk_ptrs[t] && !mac_gcr_patch_sector(trk_ptrs[t], trk_bytes[t], cyl, format_byte, sec, in524))
        Debug_printf("\nFloppy image: could not patch track %d sector %d", t, sec);
    Debug_printf("\nFloppy image: wrote C%d H%d S%d (block %lu)", cyl, side, sec, (unsigned long)block);
    return true;
}

void MediaTypeFloppyImage::unmount()
{
    MediaType::unmount();
    free_tracks();
}

#endif // BUILD_MAC
