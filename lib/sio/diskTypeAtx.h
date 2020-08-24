#ifndef _DISKTYPE_ATX_
#define _DISKTYPE_ATX_

#include <vector>

#include "diskType.h"

/*
    ATX file format data from:
    http://a8preservation.com/#/guides/atx
*/
#define ATX_SECTOR_STATUS_FDC_LOSTDATA_ERROR 0x04
#define ATX_SECTOR_STATUS_FDC_CRC_ERROR 0x08
#define ATX_SECTOR_STATUS_MISSING_DATA 0x10
#define ATX_SECTOR_STATUS_DELETED 0x20
#define ATX_SECTOR_STATUS_EXTENDED 0x40

#define ATX_WEAKOFFSET_NONE 0xFFFF

#define ATX_EXTENDEDSIZE_128 0x00
#define ATX_EXTENDEDSIZE_256 0x01
#define ATX_EXTENDEDSIZE_512 0x02
#define ATX_EXTENDEDSIZE_1024 0x03

#define ATX_TRACK_FLAGS_MFM 0x0002
#define ATX_TRACK_FLAGS_UNKNOWN_SKEW 0x0100

#define ATX_DENSITY_SINGLE 0x00
#define ATX_DENSITY_MEDIUM 0x01
#define ATX_DENSITY_DOUBLE 0x02

#define ATX_BYTES_PER_SECTOR_SINGLE 128
#define ATX_BYTES_PER_SECTOR_DOUBLE 256

#define ATX_SECTORS_PER_TRACK_NORMAL 18
#define ATX_SECTORS_PER_TRACK_ENHANCED 26

#define ATX_CHUNKTYPE_SECTOR_DATA 0x00
#define ATX_CHUNKTYPE_SECTOR_LIST 0x01
#define ATX_CHUNKTYPE_WEAK_SECTOR 0x10
#define ATX_CHUNKTYPE_EXTENDED_HEADER 0x11

#define ATX_RECORDTYPE_TRACK 0x0000
#define ATX_RECORDTYPE_HOST 0x0100

struct atx_header
{
    uint32_t magic;
    uint16_t version;
    uint16_t min_version;
    uint16_t creator;
    uint16_t creator_version;
    uint32_t flags;
    uint16_t image_type;
    uint8_t density;
    uint8_t reserved1;
    uint32_t image_id;
    uint16_t image_version;
    uint16_t reserved2;
    uint32_t start;
    uint32_t end;
} __attribute__((packed));
typedef struct atx_header atx_header_t; // We shouldn't need these, but VC's C++ linter gets confused

struct record_header
{
    uint32_t length;
    uint16_t type;
    uint16_t reserved;
} __attribute__((packed));
typedef struct record_header record_header_t;

struct track_header
{
    uint8_t track_number;
    uint8_t reserved1;
    uint16_t sector_count;
    uint16_t rate;
    uint16_t reserved2;
    uint32_t flags;
    uint32_t header_size;
    uint64_t reserved3;
} __attribute__((packed));
typedef struct track_header track_header_t;

struct chunk_header
{
    uint32_t length;
    uint8_t type;
    uint8_t sector_index;
    uint16_t header_data;
} __attribute__((packed));
typedef struct chunk_header chunk_header_t;

struct sector_header
{
    uint8_t number;
    uint8_t status;
    uint16_t position;
    uint32_t start_data;
} __attribute__((packed));
typedef struct sector_header sector_header_t;

class AtxSector
{
public:
    // 1-based and possible to have duplicates    
    uint8_t number;
    // ATX_SECTOR_STATUS bit flags     
    uint8_t status;
    // 0-based starting angular position of sector in 8us intervals (1/26042th of a rotation or ~0.0138238 degrees). Nominally 0-26042    
    uint16_t position;
    // Byte offset from start of track data record to first byte of sector data within the sector data chunk. No data is present when sector status bit 4 set
    uint32_t start_data;

    // Byte offset within sector at which weak (random) data should be returned    
    uint16_t weakoffset = ATX_WEAKOFFSET_NONE;
    // Physical size of long sector (one of ATX_EXTENDESIZE)
    uint16_t extendedsize = 0;

    ~AtxSector();
    AtxSector(sector_header_t & header);
};

class AtxTrack
{
public:
    // Number of physical sectors in track
    uint16_t sector_count = 0;
    // ? unknown use ?
    uint16_t rate;
    // ATX_TRACK_FLAGS bit flags
    uint32_t flags;

    // Actual sector data
    uint8_t * data = nullptr;

    // Actual sectors
    std::vector<AtxSector> sectors;

    ~AtxTrack();
    AtxTrack();
};

class DiskTypeATX : public DiskType
{
private:
    std::vector<AtxTrack> _tracks;

    uint32_t _num_records = 0;
    uint32_t _num_bytes = 0;
    uint32_t _num_image_bytes = 0;

    uint8_t _num_tracks = 0;
    uint16_t _bytes_per_sector = ATX_BYTES_PER_SECTOR_SINGLE;

    // ATX header.density
    uint8_t _density = ATX_DENSITY_SINGLE;
    // ATX header.end - normally the size of the entire ATX file
    uint32_t _size = 0;

    bool _load_atx_data(atx_header_t &atx_hdr);
    bool _load_atx_record();
    bool _load_atx_track_record(uint32_t length);
    int _load_atx_track_chunk(track_header_t &trk_hdr, AtxTrack &track);

    bool _load_atx_chunk_sector_list(AtxTrack &track);
    bool _load_atx_chunk_sector_data(AtxTrack &track);
    bool _load_atx_chunk_weak_sector(chunk_header_t &chunk_hdr, AtxTrack &track);
    bool _load_atx_chunk_extended_sector(chunk_header_t &chunk_hdr, AtxTrack &track);

    uint32_t _sector_to_offset(uint16_t sectorNum);



public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual disktype_t mount(FILE *f, uint32_t disksize) override;

    virtual uint16_t sector_size(uint16_t sectornum) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    DiskTypeATX();
};


#endif // _DISKTYPE_ATX_
