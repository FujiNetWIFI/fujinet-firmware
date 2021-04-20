#ifndef _DISKTYPE_ATX_
#define _DISKTYPE_ATX_

#include <vector>

#include "diskType.h"

/*
    ATX file format data from:
    http://a8preservation.com/#/guides/atx
*/
// Sector data exists but is incomplete
#define ATX_SECTOR_STATUS_FDC_LOSTDATA_ERROR 0x04
// Sector data exists but is incorrect
#define ATX_SECTOR_STATUS_FDC_CRC_ERROR 0x08
// No sector data available
#define ATX_SECTOR_STATUS_MISSING_DATA 0x10
// Sector data exists but is marked as deleted
#define ATX_SECTOR_STATUS_DELETED 0x20
// Sector has extended information chunk
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

#define ATX_SECTORS_PER_TRACK_NORMAL 18
#define ATX_SECTORS_PER_TRACK_ENHANCED 26

#define ATX_CHUNKTYPE_SECTOR_DATA 0x00
#define ATX_CHUNKTYPE_SECTOR_LIST 0x01
#define ATX_CHUNKTYPE_WEAK_SECTOR 0x10
#define ATX_CHUNKTYPE_EXTENDED_HEADER 0x11

#define ATX_RECORDTYPE_TRACK 0x0000
#define ATX_RECORDTYPE_HOST 0x0100

#define ATX_DRIVE_MODEL_810 0
#define ATX_DRIVE_MODEL_1050 1

#define ATX_FORMAT_TIMEOUT_810_1050 0xE0
#define ATX_FORMAT_TIMEOUT_XF551 0xFE

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
    uint8_t number = 0;
    // ATX_SECTOR_STATUS bit flags     
    uint8_t status = 0;
    // 0-based starting angular position of sector in 8us intervals (1/26042th of a rotation or ~0.0138238 degrees). Nominally 0-26042    
    uint16_t position = 0;
    // Byte offset from start of track data record to first byte of sector data within the sector data chunk. No data is present when sector status bit 4 set
    uint32_t start_data = 0;

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
    // We assume there are 40 tracks and no duplicates, but this serves as a safety check
    int8_t track_number = -1;
    // Number of physical sectors in track
    uint16_t sector_count = 0;
    // ? unknown use ?
    uint16_t rate;
    // ATX_TRACK_FLAGS bit flags
    uint32_t flags;

    // Keep count of bytes read into ATX track record
    uint32_t record_bytes_read = 0;
    uint32_t offset_to_data_start = 0;

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
    uint8_t _atx_num_tracks = 0;
    uint8_t _atx_num_records = 0;


    uint8_t _atx_controller_status = 0;

    uint8_t _atx_last_track = 0;
    uint8_t _atx_sectors_per_track = ATX_SECTORS_PER_TRACK_NORMAL;

    uint8_t _atx_drive_model = ATX_DRIVE_MODEL_810;

    portMUX_TYPE __atx_timerMux = portMUX_INITIALIZER_UNLOCKED;

    uint64_t __atx_position_time;
    uint16_t __atx_current_angular_pos = 0;
    uint32_t _atx_total_rotations = 0;

    esp_timer_handle_t _atx_timer = nullptr;

    std::vector<AtxTrack> _tracks;

    // ATX header.density
    uint8_t _atx_density = ATX_DENSITY_SINGLE;
    // ATX header.end - normally the size of the entire ATX file
    uint32_t _atx_size = 0;

    bool _load_atx_data(atx_header_t &atx_hdr);
    bool _load_atx_record();
    bool _load_atx_track_record(uint32_t length);
    int _load_atx_track_chunk(track_header_t &trk_hdr, AtxTrack &track);

    bool _load_atx_chunk_sector_list(chunk_header_t &chunk_hdr, AtxTrack &track);
    bool _load_atx_chunk_sector_data(chunk_header_t &chunk_hdr, AtxTrack &track);
    bool _load_atx_chunk_weak_sector(chunk_header_t &chunk_hdr, AtxTrack &track);
    bool _load_atx_chunk_extended_sector(chunk_header_t &chunk_hdr, AtxTrack &track);
    bool _load_atx_chunk_unknown(chunk_header_t &chunk_hdr, AtxTrack &track);

    bool _copy_track_sector_data(uint8_t tracknum, uint8_t sectornum, uint16_t sectorsize);
    void _process_sector(AtxTrack &track, AtxSector *sectorp, uint16_t sectorsize);

    uint16_t _get_head_position();
    void _wait_full_rotation();
    void _wait_head_position(uint16_t pos, uint16_t extra_delay);

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool format(uint16_t *respopnsesize) override;

    virtual disktype_t mount(FILE *f, uint32_t disksize) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static void on_timer(void *info);

    DiskTypeATX();
    ~DiskTypeATX();
};


#endif // _DISKTYPE_ATX_
