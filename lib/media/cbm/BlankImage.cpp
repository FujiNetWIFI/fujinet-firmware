#ifdef BUILD_IEC

#include "BlankImage.h"
#include "mediaType.h"

#include <ctype.h>
#include <string.h>

#include "../../include/debug.h"

#define SECTOR_SIZE 256
#define NAME_SIZE 16
#define PAD 0xA0 // shifted space, which pads the names in a disk header

// A CMD native partition, the largest Commodore image
static const uint32_t MAX_RAW_SIZE = 16UL * 1024 * 1024;

static const uint8_t DISK_ID[2] = {'F', 'N'};

// A sector a blank image has something in: its index in the image, and how to fill it
namespace
{
struct Special
{
    uint32_t sector;
    void (*fill)(uint8_t *sector, const uint8_t *name);
};
} // namespace

// Sectors on a 1541 track; the 1571's second side repeats the first
static uint8_t sectors_in_track(uint8_t track)
{
    if (track > 35)
        track -= 35;
    return track < 18 ? 21 : track < 25 ? 19 : track < 31 ? 18 : 17;
}

// A BAM entry: the number of free sectors, then a bit for each free sector
static void track_entry(uint8_t *entry, uint8_t sectors)
{
    entry[0] = sectors;
    for (uint8_t s = 0; s < sectors; s++)
        entry[1 + s / 8] |= 1 << (s % 8);
}

static void allocate(uint8_t *entry, uint8_t sector)
{
    entry[0]--;
    entry[1 + sector / 8] &= (uint8_t) ~(1 << (sector % 8));
}

static void empty_directory(uint8_t *s, const uint8_t *)
{
    s[1] = 0xFF; // the last directory sector
}

// 18/0, the 1541's header and BAM
static void bam_1541(uint8_t *s, const uint8_t *name)
{
    s[0] = 18;
    s[1] = 1; // the first directory sector
    s[2] = 'A'; // DOS version
    for (uint8_t t = 1; t <= 35; t++)
        track_entry(s + 4 * t, sectors_in_track(t));
    allocate(s + 4 * 18, 0);
    allocate(s + 4 * 18, 1);
    memset(s + 0x90, PAD, 27);
    memcpy(s + 0x90, name, NAME_SIZE);
    memcpy(s + 0xA2, DISK_ID, sizeof(DISK_ID));
    memcpy(s + 0xA5, "2A", 2); // DOS type
}

// 18/0 on the 1571 adds the free counts of the second side, whose bitmaps are in 53/0
static void bam_1571(uint8_t *s, const uint8_t *name)
{
    bam_1541(s, name);
    s[3] = 0x80; // double sided
    for (uint8_t t = 36; t <= 70; t++)
        s[0xDD + t - 36] = t == 53 ? 0 : sectors_in_track(t);
}

// 53/0; the 1571 allocates all of track 53
static void bam_1571_side_2(uint8_t *s, const uint8_t *)
{
    for (uint8_t t = 36; t <= 70; t++)
    {
        uint8_t entry[4] = {};
        if (t != 53)
            track_entry(entry, sectors_in_track(t));
        memcpy(s + 3 * (t - 36), entry + 1, 3);
    }
}

// 40/0, the 1581's header
static void header_1581(uint8_t *s, const uint8_t *name)
{
    s[0] = 40;
    s[1] = 3; // the first directory sector
    s[2] = 'D'; // DOS version
    memset(s + 4, PAD, 25);
    memcpy(s + 4, name, NAME_SIZE);
    memcpy(s + 0x16, DISK_ID, sizeof(DISK_ID));
    memcpy(s + 0x19, "3D", 2); // DOS type
}

// What the 1581's two BAM sectors share: 40 tracks of 40 free sectors
static void bam_1581(uint8_t *s)
{
    s[2] = 'D';
    s[3] = 0xBB; // its complement
    memcpy(s + 4, DISK_ID, sizeof(DISK_ID));
    s[6] = 0xC0; // verify on, check header CRC
    for (uint8_t t = 0; t < 40; t++)
        track_entry(s + 0x10 + 6 * t, 40);
}

// 40/1, tracks 1-40; the first four sectors of track 40 are in use
static void bam_1581_first(uint8_t *s, const uint8_t *)
{
    s[0] = 40;
    s[1] = 2; // the second BAM sector
    bam_1581(s);
    for (uint8_t i = 0; i < 4; i++)
        allocate(s + 0x10 + 6 * 39, i);
}

// 40/2, tracks 41-80
static void bam_1581_second(uint8_t *s, const uint8_t *)
{
    s[1] = 0xFF; // the last BAM sector
    bam_1581(s);
}

// 18/0 follows 17 tracks of 21 sectors
static const Special D64_SPECIAL[] = {
    {17 * 21, bam_1541},
    {17 * 21 + 1, empty_directory},
};
// The second side starts at sector 683, and 53/0 follows 17 tracks of 21 sectors
static const Special D71_SPECIAL[] = {
    {17 * 21, bam_1571},
    {17 * 21 + 1, empty_directory},
    {683 + 17 * 21, bam_1571_side_2},
};
// 40/0 follows 39 tracks of 40 sectors
static const Special D81_SPECIAL[] = {
    {39 * 40, header_1581},
    {39 * 40 + 1, bam_1581_first},
    {39 * 40 + 2, bam_1581_second},
    {39 * 40 + 3, empty_directory},
};

static success_is_true write_zeros(fnFile *f, uint32_t count)
{
    const uint8_t zeros[2 * SECTOR_SIZE] = {};

    while (count)
    {
        size_t n = count < sizeof(zeros) ? count : sizeof(zeros);
        if (fnio::fwrite(zeros, 1, n, f) != n)
            RETURN_ERROR_AS_FALSE();
        count -= n;
    }
    RETURN_SUCCESS_AS_TRUE();
}

// An image of `total` sectors that is zeros but for the special ones, which come in order
template <size_t N>
static success_is_true write_image(fnFile *f, uint32_t total, const Special (&special)[N],
                                   const uint8_t *name)
{
    uint8_t sector[SECTOR_SIZE];
    uint32_t next = 0;

    for (const Special &s : special)
    {
        memset(sector, 0, sizeof(sector));
        s.fill(sector, name);
        if (write_zeros(f, (s.sector - next) * SECTOR_SIZE).is_error()
            || fnio::fwrite(sector, 1, sizeof(sector), f) != sizeof(sector))
            RETURN_ERROR_AS_FALSE();
        next = s.sector + 1;
    }
    return write_zeros(f, (total - next) * SECTOR_SIZE);
}

// The base name without its extension, in upper case, padded to a disk name's 16 characters
static void disk_name(uint8_t *name, const char *filename)
{
    size_t length = strlen(filename);
    size_t end = length > 4 ? length - 4 : 0;
    size_t start = 0;

    for (size_t i = 0; i < end; i++)
        if (filename[i] == '/')
            start = i + 1;

    memset(name, PAD, NAME_SIZE);
    for (size_t i = 0; i < NAME_SIZE && start + i < end; i++)
    {
        uint8_t c = toupper((unsigned char) filename[start + i]);
        name[i] = c >= ' ' && c <= '_' ? c : ' ';
    }
}

success_is_true BlankImage::create(fnFile *f, const char *filename, uint16_t sectorSize,
                                   uint16_t numSectors)
{
    uint8_t name[NAME_SIZE];
    uint32_t size = (uint32_t) sectorSize * numSectors;

    Debug_printf("BLANK IMAGE %s\r\n", filename);
    disk_name(name, filename);

    switch (MediaType::discover_mediatype(filename))
    {
    case MEDIATYPE_D64:
        return write_image(f, 683, D64_SPECIAL, name); // 35 tracks
    case MEDIATYPE_D71:
        return write_image(f, 2 * 683, D71_SPECIAL, name);
    case MEDIATYPE_D81:
        return write_image(f, 80 * 40, D81_SPECIAL, name);
    default:
        if (size == 0 || size > MAX_RAW_SIZE)
        {
            Debug_printf("BLANK IMAGE %lu bytes refused\r\n", (unsigned long) size);
            RETURN_ERROR_AS_FALSE();
        }
        return write_zeros(f, size);
    }
}

#endif /* BUILD_IEC */
