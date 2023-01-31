#ifdef BUILD_ATARI // temporary

#include "diskTypeAtx.h"

#include <memory.h>
#include <string.h>
#include <esp_timer.h>

#include "../../include/debug.h"

#include "disk.h"

#include "fnSystem.h"

#include "utils.h"


#define ATX_MAGIC_HEADER 0x41543858 // "AT8X"
#define ATX_DEFAULT_NUMTRACKS 40

/*
  Assuming 288RPM:
  4.8 revolutions per second
  0.208333... seconds per revolution

  The ATX format stores an angular position of 1-26042
  0.20833... / 26042 = 0.0000079998976013... = 8 microseconds per angular position

*/
/*
 Frequency with which to update the angular position counter.
 Counter is updated every 
 US_ANGULAR_UNIT_TIME * ANGULAR_POSITION_UPDATE_FREQ microseconds.
 ANGULAR_POSITION_UPDATE_FREQ must be at least '8' (updates every
 8*8 = 64 microseconds) because the ESP timer fucntion won't allow
 frequencies lower than every 50 microseconds.
 "29" evenly divides 26042
*/
#define ANGULAR_POSITION_UPDATE_FREQ 29
#define ANGULAR_POSITION_INVALID 65535

// Most of the following timing constants come from S-Drive Max sources atx.c
// (converted from milliseconds to microseconds)

// Number of angular units in a full disk rotation
#define ANGULAR_UNIT_TOTAL 26042
// Number of microseconds for each angular unit
#define US_ANGULAR_UNIT_TIME 8
// Number of microseconds drive takes to process a request
#define US_DRIVE_REQUEST_DELAY_810 3220
#define US_DRIVE_REQUEST_DELAY_1050 3220
// Number of microseconds to calculate CRC
#define US_CRC_CALCULATION_810 2000
#define US_CRC_CALCULATION_1050 2000
// Number of microseconds drive takes to step 1 track
#define US_TRACK_STEP_810 5300
#define US_TRACK_STEP_1050 12410
// Number of microseconds drive head takes to settle after track stepping
#define US_HEAD_SETTLE_810 0
#define US_HEAD_SETTLE_1050 40000

#define MAX_RETRIES_1050 1
#define MAX_RETRIES_810 4

AtxTrack::~AtxTrack()
{
    if (data != nullptr)
        delete[] data;
};

AtxTrack::AtxTrack(){

};

AtxSector::~AtxSector(){

};

AtxSector::AtxSector(sector_header_t &header)
{
    number = header.number;
    position = header.position;
    status = header.status;
    start_data = header.start_data;
};

MediaTypeATX::~MediaTypeATX()
{
    // Destory any timer we may have
    if (_atx_timer != nullptr)
    {
        esp_timer_stop(_atx_timer);
        esp_timer_delete(_atx_timer);
    }
}

// Constructor initializes the AtxTrack vector to assume we have 40 tracks
MediaTypeATX::MediaTypeATX()
{
    _tracks.reserve(ATX_DEFAULT_NUMTRACKS);
    int i = 0;
    for (auto it = _tracks.begin(); i < ATX_DEFAULT_NUMTRACKS; i++)
        _tracks.emplace(it++);

    // Disallow HSIO
    _allow_hsio = false;

    // Create a timer to track our fake disk rotating
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = on_timer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &_atx_timer);
    ESP_ERROR_CHECK(esp_timer_start_periodic(_atx_timer,
        US_ANGULAR_UNIT_TIME * ANGULAR_POSITION_UPDATE_FREQ));
}

/*
    Some notes on esp_timer_get_time() from:
    https://github.com/espressif/arduino-esp32/pull/1424
    * returns monotonic time in microseconds
    * can be called from tasks and interrupts
    * does not use any critical sections/mutexes
    * is thread safe
    * takes less than 1 microsecond to execute
*/
void MediaTypeATX::on_timer(void *info)
{
    MediaTypeATX *pAtx = (MediaTypeATX *)info;

    portENTER_CRITICAL(&pAtx->__atx_timerMux);

    pAtx->__atx_position_time = esp_timer_get_time();
    pAtx->__atx_current_angular_pos += ANGULAR_POSITION_UPDATE_FREQ;

    if (pAtx->__atx_current_angular_pos >= ANGULAR_UNIT_TOTAL)
    {
        pAtx->__atx_current_angular_pos -= ANGULAR_UNIT_TOTAL;
        pAtx->_atx_total_rotations++;
    }

    portEXIT_CRITICAL(&pAtx->__atx_timerMux);
}

uint16_t MediaTypeATX::_get_head_position()
{
    uint64_t us_now = esp_timer_get_time();

    portENTER_CRITICAL(&__atx_timerMux);
    uint64_t us_then = __atx_position_time;
    uint16_t pos = __atx_current_angular_pos;
    portEXIT_CRITICAL(&__atx_timerMux);

    /*
     Adjust the recorded angular position by the number of microseconds passed since
     it was recorded.
    */
    uint64_t us_diff;

    // Handle the rare case of a timer roll-over
    if (us_now > us_then)
    {
        us_diff = us_now - us_then;
    }
    else
    {
        us_diff = UINT64_MAX - us_now + us_then;
    }

    pos += us_diff / US_ANGULAR_UNIT_TIME;
    if (pos >= ANGULAR_UNIT_TOTAL)
        pos -= ANGULAR_UNIT_TOTAL;

    return pos;
}

void MediaTypeATX::_wait_full_rotation()
{
    uint32_t pos = _get_head_position();

    // Wait for a roll-over to occur
    do
    {
        NOP();
    } while (pos <= _get_head_position());

    // Wait for the head to be over the previous position
    do
    {
        NOP();
    } while (pos > _get_head_position());
}

void MediaTypeATX::_wait_head_position(uint16_t pos, uint16_t extra_delay)
{
    pos += extra_delay;
    if (pos >= ANGULAR_UNIT_TOTAL)
        pos -= ANGULAR_UNIT_TOTAL;

    uint16_t current = _get_head_position();

    // The head is ahead of the position we want - wait for a roll-over to occur
    if (pos < current)
    {
        //Debug_print("$$$ DEBUG rollover wait\n");
        do
        {
            NOP();
        } while (pos < (current = _get_head_position()));
    }

    // The head is behind the position we want - wait for it to reach that position
    if (pos > current)
    {
        do
        {
            NOP();
        } while (pos > _get_head_position());
    }
}

void MediaTypeATX::_process_sector(AtxTrack &track, AtxSector *psector, uint16_t sectorsize)
{
    // Pause for the read head to be in the position of the sector
    _wait_head_position(psector->position, ANGULAR_UNIT_TOTAL / _atx_sectors_per_track);

    // Copy data from the sector into the buffer if any is available
    if ((psector->status & ATX_SECTOR_STATUS_MISSING_DATA) == 0)
    {
        // Make sure we have a reasonable offset and data to copy
        if (track.data != nullptr && psector->start_data >= track.offset_to_data_start)
        {
            // Adjust the start_data value by the number of bytes into the Track Record the data chunk started
            uint32_t data_offset = psector->start_data - track.offset_to_data_start;
            memcpy(_disk_sectorbuff, track.data + data_offset, sectorsize);
        }
        else
        {
            Debug_printf("## Invalid sector data offset (%u < %u) or track data buffer (%p)\n",
                         psector->start_data, track.offset_to_data_start, track.data);
            // Act as if the ATX_SECTOR_STATUS_MISSING_DATA bit was set
            _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        }

        // Replace bytes with random data if this sector has a WEAKOFFSET value
        if (psector->weakoffset != ATX_WEAKOFFSET_NONE)
        {
            Debug_printf("## Weak sector data starting at offset %u\n", psector->weakoffset);
            uint32_t rand = esp_random();
            // Fill the buffer from the offset position to the end with our random 32 bit value
            for (int x = psector->weakoffset; x < sectorsize; x += sizeof(uint32_t))
                *((uint32_t *)(_disk_sectorbuff + x)) = rand;
        }
    }
    else
    {
        _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        Debug_printf("## Skipped data copy, setting DISK_CTRL_STATUS_SECTOR_MISSING\n");
    }

    if (psector->status & ATX_SECTOR_STATUS_DELETED)
    {
        _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_DELETED;
        Debug_print("## Setting DISK_CTRL_STATUS_SECTOR_DELETED\n");
    }
    if (psector->status & ATX_SECTOR_STATUS_FDC_CRC_ERROR)
    {
        _disk_controller_status |= DISK_CTRL_STATUS_CRC_ERROR;
        Debug_print("## Setting DISK_CTRL_STATUS_CRC_ERROR\n");
    }
    if (psector->status & ATX_SECTOR_STATUS_FDC_LOSTDATA_ERROR)
    {
        _disk_controller_status |= DISK_CTRL_STATUS_DATA_LOST;
        Debug_print("## Setting DISK_CTRL_STATUS_DATA_LOST\n");
        /* Notes from S-Drive Max source atx.c:
            On an Atari 810, we have to do some specific behavior when a long sector is encountered
            (the lost data bit is set):
            1. ATX images don't normally set the DRQ status bit because the behavior is different on
                810 vs. 1050 drives. In the case of the 810, the DRQ bit should be set.
            2. The 810 is "blind" to CRC errors on long sectors because it interrupts the FDC long
                before performing the CRC check.
        */
        if (_atx_drive_model == ATX_DRIVE_MODEL_810)
            _disk_controller_status |= DISK_CTRL_STATUS_DATA_PENDING;
    }
}

// Copies data for given track sector into disk buffer and sets status bits as appropriate
// Returns TRUE on error reading sector
bool MediaTypeATX::_copy_track_sector_data(uint8_t tracknum, uint8_t sectornum, uint16_t sectorsize)
{
    Debug_printf("copy data track %d, sector %d\n", tracknum, sectornum);

    // Real drives don't clear out their buffer and some loaders care about this
    // because they check the checksum value, so we won't do it either...
    // memset(_disk_sectorbuff, 0, sectorsize);

    AtxTrack &track = _tracks[tracknum];

    _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

    int retries = _atx_drive_model == ATX_DRIVE_MODEL_810 ? MAX_RETRIES_810 : MAX_RETRIES_1050;
    while (retries > 0)
    {
        retries--;

        // Iterate through every sector stored for this track and find the one closest to the current drive head position
        uint16_t current_pos = _get_head_position();
        AtxSector *pSector = nullptr;
        for (auto &it : track.sectors)
        {
            if (it.number == sectornum)
            {
                if (pSector == nullptr)
                {
                    pSector = &it;
                }
                else
                // Compare the distance from the head to this sector and the head to the previous matching sector
                {
                    int diff_this = it.position - current_pos;
                    int diff_last = pSector->position - current_pos;

                    // Conditions under which to replace the last matching sector:
                    if (
                        (diff_this == 0) ||                                          // We're currently over the right sector
                        (diff_this > 0 && diff_last < 0) ||                          // This sector is ahead of the current pos and the last was behind
                        (diff_this > 0 && diff_last > 0 && diff_this < diff_last) || // Both are ahead but this one is closer
                        (diff_this < 0 && diff_last < 0 && diff_this < diff_last)    // Both are behind but this one is closer
                    )
                    {
                        pSector = &it;
                    }
                }
            }
        }

        if (pSector != nullptr)
        {
            _process_sector(track, pSector, sectorsize);
            // Skip any retires if our status is clear
            if (_disk_controller_status == DISK_CTRL_STATUS_CLEAR)
                retries = 0;
        }
        else
        {
            Debug_print("## Did not find sector with matching number\n");
            _disk_controller_status |= DISK_CTRL_STATUS_SECTOR_MISSING;
        }

        // Wait a full disk rotation before trying again
        if (retries != 0)
            _wait_full_rotation();
    }

    // Delay for the CRC calculation
    fnSystem.delay_microseconds(_atx_drive_model == ATX_DRIVE_MODEL_810 ? US_CRC_CALCULATION_810 : US_CRC_CALCULATION_1050);

    // Return error condition if our controller status isn't clear
    return _disk_controller_status != DISK_CTRL_STATUS_CLEAR;
}

// Returns TRUE if an error condition occurred
bool MediaTypeATX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_printf("ATX READ (%d) rots=%u\n", sectornum, _atx_total_rotations);

    *readcount = 0;

    uint16_t sectorSize = sector_size(sectornum);

    // Calculate the track/sector we're accessing
    int tracknumber = (sectornum - 1) / _atx_sectors_per_track;
    int tracksector = (sectornum - 1) % _atx_sectors_per_track + 1; // sector numbers are 1-based

    if (tracknumber >= _tracks.size())
    {
        Debug_printf("calculated track number %d > track count %d\n", tracknumber, _tracks.size());
        return true;
    }
    int trackdiff = tracknumber < _atx_last_track ? _atx_last_track - tracknumber : tracknumber - _atx_last_track;
    _atx_last_track = tracknumber;

    // If needed, add a delay for moving to our fake track
    if (trackdiff > 0)
    {
        uint32_t us_delay = _atx_drive_model == ATX_DRIVE_MODEL_810 ? US_TRACK_STEP_810 * trackdiff + US_HEAD_SETTLE_810 : US_TRACK_STEP_1050 * trackdiff + US_HEAD_SETTLE_1050;
        fnSystem.delay_microseconds(us_delay);
    }

    // Add a fake drive CPU request handling delay
    fnSystem.delay_microseconds(
        _atx_drive_model == ATX_DRIVE_MODEL_810 ? US_DRIVE_REQUEST_DELAY_810 : US_DRIVE_REQUEST_DELAY_1050);

    *readcount = sectorSize;

    bool result = _copy_track_sector_data((uint8_t)tracknumber, (uint8_t)tracksector, sectorSize);

    //util_dump_bytes(_disk_sectorbuff, sectorSize);

    return result;
}

void MediaTypeATX::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    // Set the "drive running" bit only if we're emulating an 810
    if(_atx_drive_model == ATX_DRIVE_MODEL_810)
        statusbuff[0] |= DISK_DRIVE_STATUS_MOTOR_RUNNING;

    if (_atx_density == ATX_DENSITY_DOUBLE)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_atx_density == ATX_DENSITY_MEDIUM)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status

    // Set format timeout to the expected for XF551 only if this is a double-density disk
    statusbuff[2] = _atx_density == ATX_DENSITY_DOUBLE ? ATX_FORMAT_TIMEOUT_XF551 : ATX_FORMAT_TIMEOUT_810_1050;
}

bool MediaTypeATX::_load_atx_chunk_weak_sector(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    #ifdef VERBOSE_ATX
    Debug_printf("::_load_atx_chunk_weak_sector (%hu = 0x%04x)\n",
                 chunk_hdr.sector_index, chunk_hdr.header_data);
    #endif

    if (chunk_hdr.sector_index >= track.sector_count)
    {
        Debug_println("ERROR: _load_atx_chunk_weak_sector sector index > sector_count");
        return false;
    }
    track.sectors[chunk_hdr.sector_index].weakoffset = chunk_hdr.header_data;
    return true;
}

bool MediaTypeATX::_load_atx_chunk_extended_sector(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    #ifdef VERBOSE_ATX
    Debug_printf("::_load_atx_chunk_extended_sector (%hu = 0x%04x)\n",
                 chunk_hdr.sector_index, chunk_hdr.header_data);
    #endif

    if (chunk_hdr.sector_index >= track.sector_count)
    {
        Debug_println("ERROR: _load_atx_chunk_extended_sector sector index > sector_count");
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
        Debug_println("WARNING: Invalid extended sector value");
        return false;
    }
    track.sectors[chunk_hdr.sector_index].extendedsize = xsize;
    return true;
}

bool MediaTypeATX::_load_atx_chunk_sector_data(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    #ifdef VERBOSE_ATX
    Debug_print("::_load_atx_chunk_sector_data\n");
    #endif

    // Just in case we already read data for this track
    if (track.data != nullptr)
        delete[] track.data;

    // We take the number of bytes to read from the chunk length header value
    int data_size = chunk_hdr.length - sizeof(chunk_hdr);

    // Skip if there's nothing to do
    if (data_size == 0)
        return true;
    
    // Attempt to the sector data
    track.data = new uint8_t[data_size];

    int i;
    if ((i = fread(track.data, 1, data_size, _disk_fileh)) != data_size)
    {
        Debug_printf("failed reading %d sector data chunk bytes (%d, %d)\n", data_size, i, errno);
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
    track.record_bytes_read += data_size;

    //util_dump_bytes(track.data, 64);

    return true;
}

bool MediaTypeATX::_load_atx_chunk_sector_list(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    #ifdef VERBOSE_ATX
    Debug_print("::_load_atx_chunk_sector_list\n");
    #endif

    // Skip all this if this track has no sectors
    if (track.sector_count == 0)
        return true;

    int readz = sizeof(sector_header) * track.sector_count;
    if(chunk_hdr.length != readz + sizeof(chunk_hdr))
    {
        Debug_printf("WARNING: Chunk length %U != expected\n", chunk_hdr.length);
    }

    // Attempt to read sector_header * sector_count
    sector_header_t *sector_list = new sector_header_t[track.sector_count];
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
    {
        if (sector_list[i].position >= ANGULAR_UNIT_TOTAL)
        {
            Debug_printf("WARNING: sector position = %hu\n", sector_list[i].position);
            sector_list[i].position = 0;
        }
        track.sectors.emplace_back(sector_list[i]);
    }

    delete[] sector_list;

    return true;
}

// Skip over unknown chunks if needed
bool MediaTypeATX::_load_atx_chunk_unknown(chunk_header_t &chunk_hdr, AtxTrack &track)
{
    Debug_print("::_load_atx_chunk_UNKNOWN - skipping\n");


    uint32_t chunk_size = chunk_hdr.length - sizeof(chunk_hdr);
    if (chunk_size > 0)
    {
        Debug_printf("seeking +%u to skip this chunk\n", chunk_size);
        int i;
        if ((i = fseek(_disk_fileh, chunk_size, SEEK_CUR)) < 0)
        {
            Debug_printf("seek failed (%d, %d)\n", i, errno);
            return false;
        }
        // Keep a count of how many bytes we've read into the Track Record
        track.record_bytes_read += chunk_size;
    }
    return true;
}

/*
    Returns:
    0 = Ok
    1 = Done (reached terminator chunk)
   -1 = Error
*/
int MediaTypeATX::_load_atx_track_chunk(track_header_t &trk_hdr, AtxTrack &track)
{
    #ifdef VERBOSE_ATX
    Debug_print("::_load_atx_track_chunk\n");
    #endif

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
        #ifdef VERBOSE_ATX
        Debug_print("track chunk terminator\n");
        #endif
        return 1; // 1 = done
    }

    #ifdef VERBOSE_ATX
    Debug_printf("chunk size=%u, type=0x%02hx, secindex=%d, hdata=0x%04hx\n",
                 chunk_hdr.length, chunk_hdr.type, chunk_hdr.sector_index, chunk_hdr.header_data);
    #endif

    switch (chunk_hdr.type)
    {
    case ATX_CHUNKTYPE_SECTOR_LIST:
        if (false == _load_atx_chunk_sector_list(chunk_hdr, track))
            return -1;
        break;
    case ATX_CHUNKTYPE_SECTOR_DATA:
        if (false == _load_atx_chunk_sector_data(chunk_hdr, track))
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
        if (false == _load_atx_chunk_unknown(chunk_hdr, track))
            return -1;
        break;
    }

    return 0;
}

bool MediaTypeATX::_load_atx_track_record(uint32_t length)
{
    #ifdef VERBOSE_ATX
    Debug_printf("::_load_atx_track_record len %u\n", length);
    #endif

    track_header_t trk_hdr;

    int i;
    if ((i = fread(&trk_hdr, 1, sizeof(trk_hdr), _disk_fileh)) != sizeof(trk_hdr))
    {
        Debug_printf("failed reading track header bytes (%d, %d)\n", i, errno);
        return false;
    }

    #ifdef VERBOSE_ATX
    Debug_printf("track #%hu, sectors=%hu, rate=%hu, flags=0x%04x, headersize=%u\n",
                 trk_hdr.track_number, trk_hdr.sector_count,
                 trk_hdr.rate, trk_hdr.flags, trk_hdr.header_size);
    #endif

    // Make sure we don't have a bogus track number
    if (trk_hdr.track_number >= ATX_DEFAULT_NUMTRACKS)
    {
        Debug_print("ERROR: track number > 40 - aborting\n");
        return false;
    }

    AtxTrack &track = _tracks[trk_hdr.track_number];

    // Check if we've alrady read this track
    if (track.track_number != -1)
    {
        Debug_print("ERROR: duplicate track number - aborting!\n");
        return false;
    }

    // Store basic track info
    track.track_number = trk_hdr.track_number;
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
        #ifdef VERBOSE_ATX
        Debug_printf("seeking +%u to first chunk start pos\n", chunk_start_offset);
        #endif
        if ((i = fseek(_disk_fileh, chunk_start_offset, SEEK_CUR)) < 0)
        {
            Debug_printf("failed seeking to first chunk in track record (%d, %d)\n", i, errno);
            return false;
        }
        // Keep a count of how many bytes we've read into the Track Record
        track.record_bytes_read += chunk_start_offset;
    }

    // Reserve space for the sectors we're eventually going to read for this track
    track.sectors.reserve(track.sector_count);

    // Read the chunks in the track
    while ((i = _load_atx_track_chunk(trk_hdr, track)) == 0)
        ;

    return i == 1; // Return FALSE on error condition
}

/*
  Each record consists of an 8 byte header followed by the actual data
  Since there's only one type of record we care about (RECORD), all we need is the length
  Returns FALSE on error, otherwise TRUE
*/
bool MediaTypeATX::_load_atx_record()
{
    #ifdef VERBOSE_ATX
    Debug_printf("::_load_atx_record #%u\n", ++_atx_num_records);
    #endif

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
            #ifdef VERBOSE_ATX
            Debug_print("reached EOF\n");
            #endif
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
bool MediaTypeATX::_load_atx_data(atx_header_t &atx_hdr)
{
    Debug_println("MediaTypeATX::_load_atx_data starting read");

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
mediatype_t MediaTypeATX::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ATX MOUNT\n");

    _disktype = MEDIATYPE_UNKNOWN;
    _disk_last_sector = INVALID_SECTOR_VALUE;

    // Load what should be the ATX header before attempting to load the rest
    int i;
    if ((i = fseek(f, 0, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to header on disk image (%d, %d)\n", i, errno);
        return MEDIATYPE_UNKNOWN;
    }

    atx_header hdr;

    if ((i = fread(&hdr, 1, sizeof(hdr), f)) != sizeof(hdr))
    {
        Debug_printf("failed reading header bytes (%d, %d)\n", i, errno);
        return MEDIATYPE_UNKNOWN;
    }

    // Check the magic number (flip it around since it automatically gets re-ordered when loaded as a UINT32)
    if (ATX_MAGIC_HEADER != UINT32_FROM_LE_UINT32(hdr.magic))
    {
        Debug_printf("ATX header doesnt match 'AT8X' (0x%008x)\n", hdr.magic);
        return MEDIATYPE_UNKNOWN;
    }

    _atx_size = hdr.end;
    _atx_density = hdr.density;
    _atx_sectors_per_track = _atx_density ==
                                     ATX_DENSITY_MEDIUM
                                 ? ATX_SECTORS_PER_TRACK_ENHANCED
                                 : ATX_SECTORS_PER_TRACK_NORMAL;
    _disk_sector_size = _atx_density ==
                                ATX_DENSITY_DOUBLE
                            ? DISK_BYTES_PER_SECTOR_DOUBLE
                            : DISK_BYTES_PER_SECTOR_SINGLE;

    // Set the drive type to 810 if it's a single density disk, otherwise set it to 1050
    _atx_drive_model = _atx_density == ATX_DENSITY_SINGLE ? ATX_DRIVE_MODEL_810 : ATX_DRIVE_MODEL_1050;

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
        return MEDIATYPE_UNKNOWN;
    }

    _disk_num_sectors = 720;
    
    return _disktype = MEDIATYPE_ATX;
}

/*
    From Altirra manual:
    The format command formats a disk, writing 40 tracks and then verifying all sectors.
    All sectors are filleded with the data byte $00. On completion, the drive returns
    a sector-sized buffer containing a list of 16-bit bad sector numbers terminated by $FFFF.
*/
// Returns TRUE if an error condition occurred
bool MediaTypeATX::format(uint16_t *responsesize)
{
    Debug_print("ATX FORMAT, SEND ERROR.\n");

    // Populate an empty bad sector map
    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));
    _disk_sectorbuff[0] = 0xFF;
    _disk_sectorbuff[1] = 0xFF;

    *responsesize = _disk_sector_size;

    return true; // send ERROR.
}

#endif /* BUILD_ATARI */
