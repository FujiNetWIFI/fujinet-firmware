#ifdef BUILD_MAC
//  https://applesaucefdc.com/moof-reference/

#include "mediaTypeMOOF.h"
#include "../../include/debug.h"
// #include <string.h>

mediatype_t MediaTypeMOOF::mount(FILE *f, uint32_t disksize)
{
    _media_fileh = f;
    floppy_emulation = true;
    // check MOOF header
    if (moof_check_header())
        return MEDIATYPE_UNKNOWN;

    // work through INFO chunk
    if (moof_read_info())
        return MEDIATYPE_UNKNOWN;

    if (moof_read_tmap())
        return MEDIATYPE_UNKNOWN;
    if (moof_read_tracks())
        return MEDIATYPE_UNKNOWN;

    return MEDIATYPE_MOOF;
}

void MediaTypeMOOF::unmount()
{
    MediaType::unmount();
#ifdef CACHE_IMAGE
    for (int i = 0; i < MAX_TRACKS; i++)
    {
        if (trk_ptrs[i] != nullptr)
            free(trk_ptrs[i]);
            
        trk_ptrs[i] = nullptr;
    }
#else
    free(trk_buffer);
    trk_buffer = nullptr;
#endif
}

bool MediaTypeMOOF::moof_check_header()
{
    char hdr[12];
    fread(&hdr, sizeof(char), 12, _media_fileh);
    if (hdr[0] == 'M' && hdr[1] == 'O' && hdr[2] == 'O' && hdr[3] == 'F')
    {
        Debug_printf("\nMOOF file confirmed!");
    }
    else
    {
        Debug_printf("\nNot a MOOF file!");
        return true;
    }
    // check for file integrity
    if (hdr[4] == 0xFF && hdr[5] == 0x0A && hdr[6] == 0x0D && hdr[7] == 0x0A)
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

bool MediaTypeMOOF::moof_read_info()
{
    if (fseek(_media_fileh, 12, SEEK_SET))
    {
        Debug_printf("\nError seeking INFO chunk");
        return true;
    }
    uint32_t chunk_id, chunk_size;
    fread(&chunk_id, sizeof(chunk_id), 1, _media_fileh);
    Debug_printf("\nINFO Chunk ID: %08lx", chunk_id);
    fread(&chunk_size, sizeof(chunk_size), 1, _media_fileh);
    Debug_printf("\nINFO Chunk size: %lu", chunk_size);

    uint8_t info_version, disk_type;
    fread(&info_version, sizeof(info_version), 1, _media_fileh);
    Debug_printf("\nINFO Version: %d", info_version);
    fread(&disk_type, sizeof(disk_type), 1, _media_fileh);
    Debug_printf("\nDisk type: %d", disk_type);
    // 1 = SSDD GCR (400K)
    // 2 = DSDD GCR (800K)
    // 3 = DSHD MFM (1.44M)
    switch (disk_type)
    {
    case 1:
        num_sides = 1;
        moof_disktype = moof_disk_type_t::SSDD_GCR;
        Debug_printf("\nSingle sided GCR disk");
        break;
    case 2:
        num_sides = 2;
        moof_disktype = moof_disk_type_t::DSDD_GCR;
        Debug_printf("\nDouble sided GCR disk");
        break;
    case 3:
        num_sides = 2;
        moof_disktype = moof_disk_type_t::DSDD_MFM;
        Debug_printf("\nDouble sided MFM disk - not supported");
        return true;
        break;
    default:
        moof_disktype = moof_disk_type_t::UNKNOWN;
        Debug_printf("\nUnknown Disk Type: %d", disk_type);
        return true;
    }

    Debug_printf("\nNow at byte %lu", ftell(_media_fileh));
    // blow the next two bytes
    getc(_media_fileh);
    getc(_media_fileh);
    // get the bit timing:
    uint8_t bit_timing;
    fread(&bit_timing, sizeof(bit_timing), 1, _media_fileh);
    optimal_bit_timing = bit_timing;
    Debug_printf("\nMOOF Optimal Bit Timing = 125 ns X %d = %d ns", optimal_bit_timing, (int)optimal_bit_timing * 125);
    // and jump to get the track size
    fseek(_media_fileh, 58, SEEK_SET);
    uint16_t largest_track;
    fread(&largest_track, sizeof(largest_track), 1, _media_fileh);
    num_blocks = largest_track;

    return false;
}

bool MediaTypeMOOF::moof_read_tmap()
{ // read TMAP
    if (fseek(_media_fileh, 80, SEEK_SET))
    {
        Debug_printf("\nError seeking TMAP chunk");
        return true;
    }

    uint32_t chunk_id, chunk_size;
    fread(&chunk_id, sizeof(chunk_id), 1, _media_fileh);
    Debug_printf("\nTMAP Chunk ID: %08lx", chunk_id);
    fread(&chunk_size, sizeof(chunk_size), 1, _media_fileh);
    Debug_printf("\nTMAP Chunk size: %lu", chunk_size);
    Debug_printf("\nNow at byte %lu", ftell(_media_fileh));

    fread(&tmap, sizeof(tmap[0]), MAX_TRACKS, _media_fileh);
#ifdef DEBUG
    Debug_printf("\nTrack, Index");
    for (int i = 0; i < MAX_TRACKS; i++)
        Debug_printf("\n%d - %d/%d, %d", i, i/2, i%2, tmap[i]);
#endif

    return false;
}

uint8_t *MediaTypeMOOF::get_track(int t)
{

#ifdef CACHE_IMAGE
    return trk_ptrs[tmap[t]];
#else
    size_t s = trks[t].block_count * 512;
    Debug_printf("\nReading %d bytes of track %d", s, t);
    fseek(_media_fileh, trks[t].start_block * 512, SEEK_SET);
    fread(trk_buffer, 1, s, _media_fileh);
    // Debug_printf("\n%d, %d, %lu", trks[t].start_block, trks[t].block_count, trks[t].bit_count);
    return trk_buffer; 
#endif

}

bool MediaTypeMOOF::moof_read_tracks()
{ // depend upon little endian-ness
    fseek(_media_fileh, 256, SEEK_SET);
    fread(&trks, sizeof(TRK_t), MAX_TRACKS, _media_fileh);
#ifdef DEBUG
    Debug_printf("\nStart Block, Block Count, Bit Count");
    for (int i = 0; i < MAX_TRACKS; i++)
        Debug_printf("\n%d, %d, %lu", trks[i].start_block, trks[i].block_count, trks[i].bit_count);
#endif

#ifdef CACHE_IMAGE
    // read MOOF tracks into RAM
    for (int i = 0; i < MAX_TRACKS; i++)
    {
        trk_ptrs[i] = nullptr;
        size_t s = trks[i].block_count * 512;
        if (s != 0)
        {
            trk_ptrs[i] = (uint8_t *)heap_caps_malloc(s, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
            if (trk_ptrs[i] != nullptr)
            {
                Debug_printf("\nReading %d bytes of track %d into location %lx", s, i, trk_ptrs[i]);
                fseek(_media_fileh, trks[i].start_block * 512, SEEK_SET);
                fread(trk_ptrs[i], 1, s, _media_fileh);
                Debug_printf("\n%d, %d, %lu", trks[i].start_block, trks[i].block_count, trks[i].bit_count);
            }
            else
            {
                Debug_printf("\nNo RAM allocated!");
                return true;
            }
        }
    }
#else
    size_t s = num_blocks * 512;
    if (s != 0)
    {
        trk_buffer = (uint8_t *)heap_caps_malloc(s, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (trk_buffer != nullptr)
        {
            Debug_printf("\n%d bytes allocated for MOOF track buffer", s);
        }
        else
        {
            Debug_printf("\nNo RAM allocated!");
            return true;
        }
    }
#endif

    return false;
}

#endif // BUILD_MAC
