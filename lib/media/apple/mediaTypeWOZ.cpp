#ifdef BUILD_APPLE

#ifndef DEV_RELAY_SLIP
#include "esp_heap_caps.h"
#endif
#include "mediaTypeWOZ.h"
#include "../../include/debug.h"
#include <string.h>

#define WOZ1 '1'
#define WOZ2 '2'

bool MediaTypeWOZ::write_sector(int track, int sector, uint8_t *buffer)
{
  Debug_printf("\r\nWOZ disk needs to write sector!");
  return false;
}

mediatype_t MediaTypeWOZ::mount(fnFile *f, uint32_t disksize)
{
    _media_fileh = f;
    diskiiemulation = true;
    // check WOZ header
    if (wozX_check_header())
        return MEDIATYPE_UNKNOWN;

    // work through INFO chunk
    if (wozX_read_info())
        return MEDIATYPE_UNKNOWN;

    if (wozX_read_tmap())
        return MEDIATYPE_UNKNOWN;
        
    // read TRKS table
    switch (woz_version)
    {
    case WOZ1:
        if (woz1_read_tracks())
            return MEDIATYPE_UNKNOWN;
        break;
    case WOZ2:
        if (woz2_read_tracks())
            return MEDIATYPE_UNKNOWN;
        break;
    default:
        Debug_printf("\nWOZ version %c not supported", woz_version);
        return MEDIATYPE_UNKNOWN;
    }

    return MEDIATYPE_WOZ;
}

void MediaTypeWOZ::unmount()
{
    MediaType::unmount();
    for (int i = 0; i < MAX_TRACKS; i++)
    {
        if (trk_data[i] != nullptr)
            free(trk_data[i]);
    }
}

bool MediaTypeWOZ::wozX_check_header()
{
    char hdr[12];
    fnio::fread(&hdr, sizeof(char), 12, _media_fileh);
    if (hdr[0] == 'W' && hdr[1] == 'O' && hdr[2] == 'Z')
    {
        woz_version = hdr[3];
        Debug_printf("\nWOZ-%c file confirmed!",woz_version);
    }
    else
    {
        Debug_printf("\nNot a WOZ file!");
        return true;
    }
    // check for file integrity
    if ((unsigned char)(hdr[4]) == 0xFF && hdr[5] == 0x0A && hdr[6] == 0x0D && hdr[7] == 0x0A)
    {
        Debug_printf("\n8-bit binary file verified");
    }
    else
    {
        return true;
    }

    // could check CRC if one wanted
    
    return false;  
}

bool MediaTypeWOZ::wozX_read_info()
{
    if (fnio::fseek(_media_fileh, 12, SEEK_SET))
    {
        Debug_printf("\nError seeking INFO chunk");
        return true;
    }
    uint32_t chunk_id, chunk_size;
    fnio::fread(&chunk_id, sizeof(chunk_id), 1, _media_fileh);
    Debug_printf("\nINFO Chunk ID: %08lx", chunk_id);
    fnio::fread(&chunk_size, sizeof(chunk_size), 1, _media_fileh);
    Debug_printf("\nINFO Chunk size: %lu", chunk_size);
    Debug_printf("\nNow at byte %lu", fnio::ftell(_media_fileh));
    // could read a whole bunch of other stuff  ...

    switch (woz_version)
    {
    case WOZ1:
        num_blocks = WOZ1_NUM_BLKS; //WOZ1_TRACK_LEN / 512;
        optimal_bit_timing = WOZ1_BIT_TIME; // 4 x 8 x 125 ns = 4 us
        break;
    case WOZ2:
        // but jump to offset 44 to get the track size
        {
            // jump to offset 39 to get bit timing
            fnio::fseek(_media_fileh, 39, SEEK_CUR);
            uint8_t bit_timing;
            fnio::fread(&bit_timing, sizeof(uint8_t), 1, _media_fileh);
            optimal_bit_timing = bit_timing;
            Debug_printf("\nWOZ2 Optimal Bit Timing = 125 ns X %d = %d ns",optimal_bit_timing, (int)optimal_bit_timing * 125);
            // and jump to offset 44 to get the track size
            fnio::fseek(_media_fileh, 4, SEEK_CUR);
            uint16_t largest_track;
            fnio::fread(&largest_track, sizeof(uint16_t), 1, _media_fileh);
            num_blocks = largest_track;
        }
        break;
    default:
        Debug_printf("\nWOZ version %c not supported", woz_version);
        return true;
    }

    return false;
}

bool MediaTypeWOZ::wozX_read_tmap()
{ // read TMAP
    if (fnio::fseek(_media_fileh, 80, SEEK_SET))
    {
        Debug_printf("\nError seeking TMAP chunk");
        return true;
    }

    uint32_t chunk_id, chunk_size;
    fnio::fread(&chunk_id, sizeof(chunk_id), 1, _media_fileh);
    Debug_printf("\nTMAP Chunk ID: %08lx", chunk_id);
    fnio::fread(&chunk_size, sizeof(chunk_size), 1, _media_fileh);
    Debug_printf("\nTMAP Chunk size: %lu", chunk_size);
    Debug_printf("\nNow at byte %lu", fnio::ftell(_media_fileh));

    fnio::fread(&tmap, sizeof(tmap[0]), MAX_TRACKS, _media_fileh);
#ifdef DEBUG
    Debug_printf("\nTrack, Index");
    for (int i = 0; i < MAX_TRACKS; i++)
        Debug_printf("\n%d/4, %d", i, tmap[i]);
#endif

    return false;
}

bool MediaTypeWOZ::woz1_read_tracks()
{    // depend upon little endian-ness
    fnio::fseek(_media_fileh, 256, SEEK_SET);

    Debug_printf("\nStart Block, Block Count, Bit Count");
    
#ifdef ESP_PLATFORM
    TRK_bitstream *bitstream = (TRK_bitstream *) heap_caps_malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
#else
    TRK_bitstream *bitstream = (TRK_bitstream *) malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
#endif
    uint16_t bytes_used;
    uint16_t bit_count;
    uint8_t data[WOZ1_TRACK_LEN];

    for (int i = 0; i < MAX_TRACKS; i++)
    {

        // woz1 track data organized as:
        // Offset  Size        Name              Usage
        // +0      6646 bytes  Bitstream         The bitstream data padded out to 6646 bytes
        // +6646   uint16      Bytes Used        The actual byte count for the bitstream.
        // +6648   uint16      Bit Count         The number of bits in the bitstream.
        // +6650   uint16      Splice Point      Index of first bit after track splice
        //                                       (write hint). If no splice information is
        //                                       provided, then will be 0xFFFF.
        // +6652   uint8       Splice Nibble     Nibble value to use for splice (write hint).
        // +6653   uint8       Splice Bit Count  Bit count of splice nibble (write hint).
        // +6654   uint16      Reserved for future use.

        fnio::fread(data, 1, WOZ1_TRACK_LEN, _media_fileh);
        fnio::fread(&bytes_used, sizeof(bytes_used), 1, _media_fileh);
        fnio::fread(&bit_count, sizeof(bit_count), 1, _media_fileh);
        if (bit_count > 0)
        {
#ifdef ESP_PLATFORM
            TRK_bitstream *bitstream = (TRK_bitstream *) heap_caps_malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
#else
            TRK_bitstream *bitstream = (TRK_bitstream *) malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
#endif
            if (bitstream != nullptr)
            {
                memset(bitstream, 0, BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
                bitstream->len_bytes = bytes_used;
                bitstream->len_bits = bit_count;
                bitstream->len_blocks = (bitstream->len_bytes + 511) / 512;
                trk_data[i] = bitstream;
                Debug_printf("\nStoring %d bytes of track %d into location %lx", bytes_used, i, trk_data[i]);
                memcpy(bitstream->data, data, bytes_used);
            }
            else
            {
                Debug_printf("\nNo RAM allocated!");
                return true;
            }
        }
        else
        {
            trk_data[i] = nullptr;
            Debug_printf("\nTrack %d is blank!",i);
        }
        fnio::fread(bitstream, 1, 6, _media_fileh); // read through rest of bytes in track
    }
    free(bitstream);
    return false;
}

struct WOZ2_TRK_t
{
    uint16_t start_block;
    uint16_t block_count;
    uint32_t bit_count;
};

bool MediaTypeWOZ::woz2_read_tracks()
{    // depend upon little endian-ness
    WOZ2_TRK_t trks[MAX_TRACKS];


    fnio::fseek(_media_fileh, 256, SEEK_SET);
    fnio::fread(trks, sizeof(WOZ2_TRK_t), MAX_TRACKS, _media_fileh);
#ifdef DEBUG
    Debug_printf("\nStart Block, Block Count, Bit Count");
    for (int i=0; i<MAX_TRACKS; i++)
        Debug_printf("\n%d, %d, %lu", trks[i].start_block, trks[i].block_count, trks[i].bit_count);
#endif
    // read WOZ tracks into RAM
    for (int i=0; i<MAX_TRACKS; i++)
    {
        size_t s = std::max(trks[i].block_count * 512, WOZ1_TRACK_LEN);
        if (s != 0)
        {
#ifdef ESP_PLATFORM
            TRK_bitstream *bitstream = (TRK_bitstream *) heap_caps_malloc(BITSTREAM_ALLOC_SIZE(s), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
#else
            TRK_bitstream *bitstream = (TRK_bitstream *) malloc(BITSTREAM_ALLOC_SIZE(s));
#endif
            if (bitstream != nullptr)
            {
                Debug_printf("\nReading %d bytes of track %d into location %lx", s, i, trk_data[i]);
                fnio::fseek(_media_fileh, trks[i].start_block * 512, SEEK_SET);
                fnio::fread(bitstream->data, 1, s, _media_fileh);
                bitstream->len_blocks = trks[i].block_count;
                bitstream->len_bytes = s;
                bitstream->len_bits = trks[i].bit_count;
                trk_data[i] = bitstream;
                Debug_printf("\n%d, %d, %lu", trks[i].start_block, trks[i].block_count, trks[i].bit_count);
            }
            else
            {
                Debug_printf("\nNo RAM allocated!");
                return true;
            }
        }
    }
    return false;
}

#endif // BUILD_APPLE
