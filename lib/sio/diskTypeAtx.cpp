#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "disk.h"

#include "diskTypeAtx.h"

#define ATX_MAGIC_HEADER 0x41543858 // "AT8X"
#define ATX_DEFAULT_NUMTRACKS 40

AtxTrack::~AtxTrack()
{
    //Debug_print("~AtxTrack\n");
    if (data != nullptr)
        delete[] data;
};

AtxTrack::AtxTrack()
{
    //Debug_print("new AtxTrack\n");
}

AtxSector::~AtxSector(){
    //Debug_printf("~AtxSector %d\n", number);
};

AtxSector::AtxSector(sector_header_t &header)
{
    // Debug_print("new AtxSector\n");
    number = header.number;
    position = header.position;
    status = header.status;
    start_data = header.start_data;
};

// Copies data for given track sector into disk buffer and sets status bits as appropriate
// Returns TRUE on error reading sector
bool DiskTypeATX::_copy_track_sector_data(uint8_t tracknum, uint8_t sectornum, uint16_t sectorsize)
{
    Debug_printf("copy data track %d, sector %d\n", tracknum, sectornum);
    AtxTrack &track = _tracks[tracknum];

    _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

    for (auto &it : track.sectors)
    {
        if (it.number == sectornum)
        {
            // Copy data from the sector into the buffer if any is available
            if ((it.status & ATX_SECTOR_STATUS_MISSING_DATA) == 0 && track.data != nullptr)
            {
                // Adjust the start_data value by the number of bytes into the Track Record the data chunk started
                uint32_t data_offset = it.start_data - track.offset_to_data_start;
                memcpy(_disk_sectorbuff, track.data + data_offset, sectorsize);

                util_dump_bytes(_disk_sectorbuff, 64);
            }

            if (it.status & ATX_SECTOR_STATUS_DELETED)
                _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_DELETED;
            if (it.status & ATX_SECTOR_STATUS_MISSING_DATA)
                _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
            if (it.status & ATX_SECTOR_STATUS_FDC_CRC_ERROR)
                _disk_controller_status |= DISK_CTRL_STATUS_CRC_ERROR;
            if (it.status & ATX_SECTOR_STATUS_FDC_LOSTDATA_ERROR)
                _disk_controller_status |= DISK_CTRL_STATUS_DATA_LOST;

            return false;
        }
    }
    // Return error: didn't find sector
    return true;
}

/*
 Constructor initializes the AtxTrack vector to assume we have 40 tracks
*/
DiskTypeATX::DiskTypeATX()
{
    _tracks.reserve(ATX_DEFAULT_NUMTRACKS);
    int i = 0;
    for (auto it = _tracks.begin(); i < ATX_DEFAULT_NUMTRACKS; i++)
        _tracks.emplace(it++);
}

// Returns TRUE if an error condition occurred
bool DiskTypeATX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_printf("ATX READ (%d)\n", sectornum);

    *readcount = 0;

    uint16_t sectorSize = sector_size(sectornum);

    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));

    // Calculate the track/sector we're accessing
    int tracknumber = (sectornum - 1) / _atx_sectors_per_track;
    int tracksector = (sectornum - 1) % _atx_sectors_per_track + 1; // sector numbers are 1-based

    if(tracknumber >= _tracks.size())
    {
        Debug_printf("calculated track number %d > track count %d\n", tracknumber, _tracks.size());
        return true;
    }

    _copy_track_sector_data((uint8_t)tracknumber, (uint8_t)tracksector, sectorSize);


    *readcount = sectorSize;

    return false;
}

void DiskTypeATX::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_atx_density == ATX_DENSITY_DOUBLE)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_atx_density == ATX_DENSITY_MEDIUM)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

bool DiskTypeATX::_load_atx_chunk_weak_sector(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    Debug_printf("::_load_atx_chunk_weak_sector (%hu = 0x%04x)\n",
                 chunk_hdr.sector_index, chunk_hdr.header_data);

    if (chunk_hdr.sector_index >= track.sector_count)
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

    if (chunk_hdr.sector_index >= track.sector_count)
    {
        Debug_println("sector index > sector_count");
        return false;
    }
    uint16_t xsize;
    switch (chunk_hdr.header_data)
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

    // Skip all this if this track has no sectors
    if (track.sector_count == 0)
        return true;

    // Attempt to read sector_count * sector_size bytes of data
    if (track.data != nullptr)
        delete[] track.data;

    int readz = _disk_sector_size * track.sector_count;
    track.data = new uint8_t[_disk_sector_size * track.sector_count];

    int i;
    if ((i = fread(track.data, 1, readz, _disk_fileh)) != readz)
    {
        Debug_printf("failed reading sector %d data chunk bytes (%d, %d)\n", readz, i, errno);
        delete[] track.data;
        return false;
    }

    /*
    The start_data value in each sector header is an offset into the overall Track Record,
    including headers and other chunks that preceed it, where that sector's actual data begins
    in the data chunk.

    We record the number of bytes into the Track Record the data chunk begins so we
    can adjust the start_data value later when we want to read the right section from this
    array of bytes.
    */
    track.offset_to_data_start = track.record_bytes_read;
    // Keep a count of how many bytes we've read into the Track Record
    track.record_bytes_read += readz;

    util_dump_bytes(track.data, 64);

    return true;
}

bool DiskTypeATX::_load_atx_chunk_sector_list(AtxTrack &track)
{
    Debug_print("::_load_atx_chunk_sector_list\n");

    // Skip all this if this track has no sectors
    if (track.sector_count == 0)
        return true;

    // Attempt to read sector_header * sector_count
    sector_header_t *sector_list = new sector_header_t[track.sector_count];
    int readz = sizeof(sector_header) * track.sector_count;
    int i;
    if ((i = fread(sector_list, 1, readz, _disk_fileh)) != readz)
    {
        Debug_printf("failed reading sector list chunk bytes (%d, %d)\n", i, errno);
        delete[] sector_list;
        return false;
    }

    // Keep a count of how many bytes we've read into the Track Record
    track.record_bytes_read += readz;

    // Stuff the data into our sector objects
    track.sectors.reserve(track.sector_count);
    for (i = 0; i < track.sector_count; i++)
        track.sectors.emplace_back(sector_list[i]);

    delete[] sector_list;

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

    chunk_header_t chunk_hdr;

    int i;
    if ((i = fread(&chunk_hdr, 1, sizeof(chunk_hdr), _disk_fileh)) != sizeof(chunk_hdr))
    {
        Debug_printf("failed reading track chunk bytes (%d, %d)\n", i, errno);
        return -1;
    }

    // Keep a count of how many bytes we've read into the Track Record
    track.record_bytes_read += sizeof(chunk_header_t);

    // Check for a terminating marker
    if (chunk_hdr.length == 0)
    {
        Debug_print("track chunk terminator\n");
        return 1; // 1 = done
    }

    Debug_printf("chunk size=%u, type=0x%02hx, secindex=%d, hdata=0x%04hx\n",
                 chunk_hdr.length, chunk_hdr.type, chunk_hdr.sector_index, chunk_hdr.header_data);

    switch (chunk_hdr.type)
    {
    case ATX_CHUNKTYPE_SECTOR_LIST:
        if (false == _load_atx_chunk_sector_list(track))
            return -1;
        break;
    case ATX_CHUNKTYPE_SECTOR_DATA:
        if (false == _load_atx_chunk_sector_data(track))
            return -1;
        break;
    case ATX_CHUNKTYPE_WEAK_SECTOR:
        if (false == _load_atx_chunk_weak_sector(chunk_hdr, track))
            return -1;
        break;
    case ATX_CHUNKTYPE_EXTENDED_HEADER:
        if (false == _load_atx_chunk_extended_sector(chunk_hdr, track))
            return -1;
        break;
    default:
        Debug_print("UNKNOWN TRACK CHUNK TYPE\n");
        return -1;
    }

    return 0;
}

bool DiskTypeATX::_load_atx_track_record(uint32_t length)
{
    Debug_printf("::_load_atx_track_record len %u\n", length);

    track_header_t trk_hdr;

    int i;
    if ((i = fread(&trk_hdr, 1, sizeof(trk_hdr), _disk_fileh)) != sizeof(trk_hdr))
    {
        Debug_printf("failed reading track header bytes (%d, %d)\n", i, errno);
        return false;
    }

    Debug_printf("track #%hu, sectors=%hu, rate=%hu, flags=0x%04x, headrsize=%u\n",
                 trk_hdr.track_number, trk_hdr.sector_count,
                 trk_hdr.rate, trk_hdr.flags, trk_hdr.header_size);

    // Make sure we don't have a bogus track number
    if (trk_hdr.track_number >= ATX_DEFAULT_NUMTRACKS)
    {
        Debug_print("ERROR: track number > 40 - aborting\n");
        return false;
    }

    AtxTrack &track = _tracks[trk_hdr.track_number];

    // Check if we've alrady read this track
    if (track.sector_count != 0)
    {
        Debug_print("ERROR: duplicate track number - aborting!\n");
        return false;
    }

    // Store basic track info
    track.rate = trk_hdr.rate;
    track.flags = trk_hdr.flags;
    track.sector_count = trk_hdr.sector_count;

    // Keep a count of how many bytes we've read into the Track Record
    // So far we've read record_header + track_header bytes into this record
    track.record_bytes_read = sizeof(record_header_t) + sizeof(track_header_t);

    _atx_num_tracks++;

    // If needed, skip ahead to the first track chunk given the header size value
    // (The 'header_size' value includes both the current track header and the 'parent' record header)
    uint32_t chunk_start_offset = trk_hdr.header_size - sizeof(trk_hdr) - sizeof(record_header);
    if (chunk_start_offset > 0)
    {
        Debug_printf("seeking +%u to first chunk start pos\n", chunk_start_offset);
        if ((i = fseek(_disk_fileh, chunk_start_offset, SEEK_CUR)) < 0)
        {
            Debug_printf("failed seeking to first chunk in track record (%d, %d)\n", i, errno);
            return false;
        }
        // Keep a count of how many bytes we've read into the Track Record        
        track.record_bytes_read += chunk_start_offset;
    }

    // Reserve space for the sectors we're going to read for this track
    track.sectors.reserve(track.sector_count);

    // Read the chunks in the track
    while ((i = _load_atx_track_chunk(trk_hdr, track)) == 0);

    return i == 1; // Return FALSE on error condition
}

/*
  Each record consists of an 8 byte header followed by the actual data
  Returns FALSE on error, otherwise TRUE
*/
bool DiskTypeATX::_load_atx_record()
{
    Debug_printf("::_load_atx_record #%u\n", ++_atx_num_records);

    record_header rec_hdr;

    int i;
    if ((i = fread(&rec_hdr, 1, sizeof(rec_hdr), _disk_fileh)) != sizeof(rec_hdr))
    {
        if (errno != EOF)
        {
            Debug_printf("failed reading record header bytes (%d, %d)\n", i, errno);
        }
        else
        {
            Debug_print("reached EOF\n");
        }
        return false;
    }

    if (rec_hdr.type != ATX_RECORDTYPE_TRACK)
    {
        Debug_print("record type is not TRACK - skipping\n");
        // Skip forward to the next record
        if ((i = fseek(_disk_fileh, rec_hdr.length - sizeof(rec_hdr), SEEK_CUR)) < 0)
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
    if ((i = fseek(_disk_fileh, atx_hdr.start, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to start of ATX data (%d, %d)\n", i, errno);
        return false;
    }

    while (_load_atx_record())
        ;

    if (_atx_num_tracks != ATX_DEFAULT_NUMTRACKS)
    {
        Debug_printf("WARNING: Number of tracks read = %hu\n", _atx_num_tracks);
    }

    Debug_print("ATX load completed\n");

    return true;
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
    _disk_last_sector = INVALID_SECTOR_VALUE;

    // Load what should be the ATX header before attempting to load the rest
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

    _atx_size = hdr.end;
    _atx_density = hdr.density;
    _atx_sectors_per_track = _atx_density ==
        ATX_DENSITY_MEDIUM ? ATX_SECTORS_PER_TRACK_ENHANCED : ATX_SECTORS_PER_TRACK_NORMAL;
    _disk_sector_size = _atx_density ==
        ATX_DENSITY_DOUBLE ? DISK_BYTES_PER_SECTOR_DOUBLE : DISK_BYTES_PER_SECTOR_SINGLE;

    Debug_print("ATX image header values:\n");
    Debug_printf("version: %hd, version min: %hd\n", hdr.version, hdr.min_version);
    Debug_printf("creator: 0x%02x, creator ver: %hd\n", hdr.creator, hdr.creator_version);
    Debug_printf("  flags: 0x%02x\n", hdr.flags);
    Debug_printf("   type: %hu, density: %hu\n", hdr.image_type, hdr.density);
    Debug_printf("imageid: 0x%02x, image ver: %hd\n", hdr.image_id, hdr.image_version);
    Debug_printf("  start: 0x%04x\n", hdr.start);
    Debug_printf("    end: 0x%04x\n", hdr.end);

    _disk_fileh = f;

    // Load all the actual ATX records into memory (return immediately if we fail)
    if (_load_atx_data(hdr) == false)
    {
        _disk_fileh = nullptr;
        _tracks.clear();
        return DISKTYPE_UNKNOWN;
    }

    return _disktype = DISKTYPE_ATX;
}
