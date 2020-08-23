#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "disk.h"

#include "diskTypeAtx.h"

#define ATX_MAGIC_HEADER 0x41543858 // "AT8X"
#define ATX_DEFAULT_NUMTRACKS 40
/*
 Constructor initializes the AtxTrack vector to assume we have 40 tracks
*/
DiskTypeATX::DiskTypeATX()
{
    _tracks.reserve(ATX_DEFAULT_NUMTRACKS);
}

// Returns byte offset of given sector number (1-based)
uint32_t DiskTypeATX::_sector_to_offset(uint16_t sectorNum)
{
    uint32_t offset = 0;

    // This should always be true, but just so we don't end up with a negative...
    if (sectorNum > 0)
        offset = _sectorSize * (sectorNum - 1);

    offset += 16; // Adjust for ATR header

    // Adjust for the fact that the first 3 sectors are always 128-bytes even on 256-byte disks
    if (_sectorSize == 256 && sectorNum > 3)
        offset -= 384;

    return offset;
}

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t DiskTypeATX::sector_size(uint16_t sectornum)
{
    return sectornum <= 3 ? 128 : _sectorSize;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("ATX READ\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if(sectornum > _numSectors)
    {
        Debug_printf("::read sector %d > %d\n", sectornum, _numSectors);        
        return true;
    }

    uint16_t sectorSize = sector_size(sectornum);

    memset(_sectorbuff, 0, sizeof(_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _lastSectorUsed + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fseek(_file, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fread(_sectorbuff, 1, sectorSize, _file) != sectorSize;

    if (err == false)
        _lastSectorUsed = sectornum;
    else
        _lastSectorUsed = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    return err;
}

// ATX disks do not support WRITE
bool DiskTypeATX::write(uint16_t sectornum, bool verify)
{
    Debug_print("ATX WRITE (not allowed)\n");
    return true;
}

void DiskTypeATX::status(uint8_t statusbuff[4])
{
    if (_sectorSize == 256)
        statusbuff[0] |= 0x20; // XF551 double-density bit

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= 0x40; // XF551 double-sided bit

    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= 0x80; // 1050 enhanced-density bit
}

// ATX disks do not support FORMAT
bool DiskTypeATX::format(uint16_t *responsesize)
{
    Debug_print("ATX FORMAT (not allowed)\n");
    return true;
}

bool DiskTypeATX::_load_atx_chunk_weak_sector(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    Debug_printf("::_load_atx_chunk_weak_sector (%hu = 0x%04x)\n",
        chunk_hdr.sector_index, chunk_hdr.header_data);

    if(chunk_hdr.sector_index >= track.sector_count)
    {
        Debug_println("sector index > sector_count");
        return false;
    }
    track.sectors[chunk_hdr.sector_index].weakoffset = chunk_hdr.header_data;
    return true;
}

bool DiskTypeATX::_load_atx_chunk_extended_sector(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    Debug_printf("::_load_atx_chunk_extended_sector (%hu = 0x%04x)\n",
        chunk_hdr.sector_index, chunk_hdr.header_data);

    if(chunk_hdr.sector_index >= track.sector_count)
    {
        Debug_println("sector index > sector_count");
        return false;
    }
    uint16_t xsize;
    switch(chunk_hdr.header_data)
    {
    case ATX_EXTENDEDSIZE_128:
        xsize = 128;
        break;
    case ATX_EXTENDEDSIZE_256:
        xsize = 256;
        break;
    case ATX_EXTENDEDSIZE_512:
        xsize = 512;
        break;
    case ATX_EXTENDEDSIZE_1024:
        xsize = 1024;
        break;
    default:
        Debug_println("invalid extended sector value");
        return false;
    }
    track.sectors[chunk_hdr.sector_index].extendedsize = xsize;
    return true;
}

bool DiskTypeATX::_load_atx_chunk_sector_data(AtxTrack &track)
{
    Debug_print("::_load_atx_chunk_sector_data\n");

    // Attempt to read sector_count * sector_size bytes of data
    if(track.data != nullptr)
        delete [] track.data;

    int readz = _bytes_per_sector * track.sector_count;
    track.data = new uint8_t[_bytes_per_sector * track.sector_count];

    int i;
    if ((i = fread(track.data, 1, readz, _file)) != readz)
    {
        Debug_printf("failed reading sector %d data chunk bytes (%d, %d)\n", readz, i, errno);
        delete [] track.data;
        return false;
    }

    return true;
}

bool DiskTypeATX::_load_atx_chunk_sector_list(AtxTrack &track)
{
    Debug_print("::_load_atx_chunk_sector_list\n");

    // Attempt to read sector_header * sector_count
    sector_header_t *sector_list = new sector_header_t[track.sector_count];
    int readz = sizeof(sector_header) * track.sector_count;
    int i;
    if ((i = fread(sector_list, 1, readz, _file)) != readz)
    {
        Debug_printf("failed reading sector list chunk bytes (%d, %d)\n", i, errno);
        delete [] sector_list;
        return false;
    }
    // Stuff the data into our sector objects
    for(i = 0; i < track.sector_count; i++)
    {
        track.sectors[i].number = sector_list[i].number;
        track.sectors[i].position = sector_list[i].position;
        track.sectors[i].status = sector_list[i].status;
        track.sectors[i].start_data = sector_list[i].start_data;
    }

    delete [] sector_list;

    return true;
}

/*
    Returns:
    0 = Ok
    1 = Done (reached terminator chunk)
   -1 = Error
*/
int DiskTypeATX::_load_atx_track_chunk(track_header_t &trk_hdr, AtxTrack &track)
{
    Debug_print("::_load_atx_track_chunk\n");

    chunk_header chunk_hdr;

    int i;
    if ((i = fread(&chunk_hdr, 1, sizeof(chunk_hdr), _file)) != sizeof(chunk_hdr))
    {
        Debug_printf("failed reading track chunk bytes (%d, %d)\n", i, errno);
        return -1;
    }

    // Check for a terminating marker
    if(chunk_hdr.length == 0)
        return 1; // 1 = done

    Debug_printf("header size=%u, type=0x%02hx, secindex=%d, hdata=0x%04hx\n",
        chunk_hdr.length, chunk_hdr.type, chunk_hdr.sector_index, chunk_hdr.header_data);

    switch(chunk_hdr.type)
    {
    case ATX_CHUNKTYPE_SECTOR_LIST:
        return _load_atx_chunk_sector_list(track);
    case ATX_CHUNKTYPE_SECTOR_DATA:
        return _load_atx_chunk_sector_data(track);
    case ATX_CHUNKTYPE_WEAK_SECTOR:
        return _load_atx_chunk_weak_sector(chunk_hdr, track);
    case ATX_CHUNKTYPE_EXTENDED_HEADER:
        return _load_atx_chunk_extended_sector(chunk_hdr, track);
    default:
        Debug_print("UNKNOWN TRACK CHUNK TYPE\n");
        return -1;
    }

    return 0;
}

bool DiskTypeATX::_load_atx_track_record(uint32_t length)
{
    Debug_printf("::_load_atx_track_record len %u\n", length);

    track_header trk_hdr;

    int i;
    if ((i = fread(&trk_hdr, 1, sizeof(trk_hdr), _file)) != sizeof(trk_hdr))
    {
        Debug_printf("failed reading track header bytes (%d, %d)\n", i, errno);
        return false;
    }

    Debug_printf("track #%hu, sectors=%hu, rate=%hu, flags=0x%04x, hdrsize=%u\n",
        trk_hdr.track_number, trk_hdr.sector_count,
        trk_hdr.rate, trk_hdr.flags, trk_hdr.header_size);

    // Make sure we don't have a bogus track number
    if(trk_hdr.track_number >= ATX_DEFAULT_NUMTRACKS)
    {
        Debug_print("ERROR: track number > 40 - aborting\n");
        return false;
    }

    AtxTrack & track = _tracks[trk_hdr.track_number];

    // Check if we've alrady read this track
    if(track.sector_count != 0)
    {
        Debug_print("ERROR: duplicate track number - aborting!\n");
        return false;
    }

    // Store basic track info
    track.rate = trk_hdr.rate;
    track.flags = trk_hdr.flags;
    track.sector_count = trk_hdr.sector_count;

    _num_tracks++;

    if(track.sector_count != _sectors_per_track)
    {
        Debug_printf("WARNING: Num track sectors (%hu) not equal to expected (%hu)\n", track.sector_count, _sectors_per_track);
    }
    
    // If needed, skip ahead to the first track chunk given the header size value
    // (The 'header_size' value includes both the current track header and the 'parent' record header)
    uint32_t chunk_start_offset = trk_hdr.header_size - sizeof(trk_hdr) - sizeof(record_header);
    if(chunk_start_offset > 0)
    {
        Debug_printf("seeking +%u to first chunk start pos\n", chunk_start_offset);
        if ((i = fseek(_file, chunk_start_offset, SEEK_CUR)) < 0)
        {
            Debug_printf("failed seeking to first chunk in track record (%d, %d)\n", i, errno);
            return false;
        }
    }

    // Reserve space for the sectors we're going to read for this track
    track.sectors.reserve(track.sector_count);

    // Read the chunks in the track
    while((i = _load_atx_track_chunk(trk_hdr, track)) == 0);

    return i == 1; // Return FALSE on error condition
}

/*
  Each record consists of an 8 byte header followed by the actual data
  Returns FALSE on error, otherwise TRUE
*/
bool DiskTypeATX::_load_atx_record()
{
    Debug_printf("::_load_atx_record #%u\n", ++_num_records);

    record_header rec_hdr;

    int i;
    if ((i = fread(&rec_hdr, 1, sizeof(rec_hdr), _file)) != sizeof(rec_hdr))
    {
        Debug_printf("failed reading record header bytes (%d, %d)\n", i, errno);
        return false;
    }


    if(rec_hdr.type != ATX_RECORDTYPE_TRACK)
    {
        Debug_print("record type is not TRACK - skipping\n");
        // Skip forward to the next record
        if((i = fseek(_file, rec_hdr.length - sizeof(rec_hdr), SEEK_CUR)) < 0)
        {
            Debug_printf("failed seeking past this record (%d, %d)\n", i, errno);
            return false;            
        }
        return true; // Return TRUE since this isn't an error
    }

    // Try to read the track into memory
    return _load_atx_track_record(rec_hdr.length);
}

/*
 Load the data records that make up the ATX image into memory
 Returns FALSE on failure
*/
bool DiskTypeATX::_load_atx_data(atx_header_t &atx_hdr)
{
    Debug_println("DiskTypeATX::_load_atx_data starting read");

    // Seek to the start of the ATX record data
    int i;
    if ((i = fseek(_file, atx_hdr.start, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to start of ATX data (%d, %d)\n", i, errno);
        return false;
    }

    _num_bytes += atx_hdr.start;

    while(_load_atx_record());

    if(_num_tracks != ATX_DEFAULT_NUMTRACKS)
    {
        Debug_printf("WARNING: Number of tracks read = %hu\n", _num_tracks);
    }

    Debug_print("load completed\n");

    return false;
}

/* 
 Mount ATX disk
 Header layout details from:
 http://a8preservation.com/#/guides/atx

 Since timing is important, we will load the entire image into memory.
 
*/
disktype_t DiskTypeATX::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ATX MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;
    _lastSectorUsed = INVALID_SECTOR_VALUE;

    // Load the first 36 bytes of the file to examine the header before attempting to load the rest
    int i;
    if ((i = fseek(f, 0, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to header on disk image (%d, %d)\n", i, errno);
        return DISKTYPE_UNKNOWN;
    }

    atx_header hdr;

    if ((i = fread(&hdr, 1, sizeof(hdr), f)) != sizeof(hdr))
    {
        Debug_printf("failed reading header bytes (%d, %d)\n", i, errno);
        return DISKTYPE_UNKNOWN;
    }

    // Check the magic number (flip it around since it automatically gets re-ordered when loaded as a UINT32)
    if (ATX_MAGIC_HEADER != UINT32_FROM_LE_UINT32(hdr.magic))
    {
        Debug_printf("ATX header doesnt match 'AT8X' (0x%008x)\n", hdr.magic);
        return DISKTYPE_UNKNOWN;
    }

    _size = hdr.end - hdr.start;
    _density = hdr.density;
    _sectors_per_track = _density == 
        ATX_DENSITY_MEDIUM ? ATX_SECTORS_PER_TRACK_ENHANCED : ATX_SECTORS_PER_TRACK_NORMAL;
    _bytes_per_sector = _density == 
        ATX_DENSITY_DOUBLE ? ATX_BYTES_PER_SECTOR_DOUBLE : ATX_BYTES_PER_SECTOR_SINGLE;

    Debug_print("ATX image header values:\n");
    Debug_printf("version: %hd, version min: %hd\n", hdr.version, hdr.min_version);
    Debug_printf("creator: 0x%02x, creator ver: %hd\n", hdr.creator, hdr.creator_version);
    Debug_printf("  flags: 0x%02x\n", hdr.flags);
    Debug_printf("   type: %hu, density: %hu\n", hdr.image_type, hdr.density);
    Debug_printf("imageid: 0x%02x, image ver: %hd\n", hdr.image_id, hdr.image_version);
    Debug_printf("  start: 0x%04x\n", hdr.start);
    Debug_printf("    end: 0x%04x\n", hdr.end);

    _file = f;

    // Load all the actual ATX records into memory (return immediately if we fail)
    if(_load_atx_data(hdr) == false)
    {
        _file = nullptr;
        return DISKTYPE_UNKNOWN;
    }

    return _disktype = DISKTYPE_ATX;
}

// ATX creation not allowed
bool DiskTypeATX::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("ATX CREATE (not allowed)\n");
    return false;
}
