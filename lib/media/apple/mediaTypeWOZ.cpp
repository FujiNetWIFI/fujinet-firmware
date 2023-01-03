#ifdef BUILD_APPLE

#include "mediaTypeWOZ.h"
#include "../../include/debug.h"

mediatype_t MediaTypeWOZ::mount(FILE *f, uint32_t disksize)
{
    _media_fileh = f;
    // check WOZ header
    char hdr[12];
    fread(&hdr,sizeof(char),12,f);
    if (hdr[0] == 'W' && hdr[1] == 'O' && hdr[2] == 'Z' && hdr[3] == '2')
        Debug_printf("\nWOZ2 file confirmed!");
    else return MEDIATYPE_UNKNOWN;
    // check for file integrity
    if (hdr[4] == 0xFF && hdr[5] == 0x0A && hdr[6] == 0x0D && hdr[7] == 0x0A)
        Debug_printf("\n8-bit binary file verified");
    else return MEDIATYPE_UNKNOWN;
    // could check CRC if one wanted

    // work through INFO chunk
    if (fseek(f, 12, SEEK_SET))
    {
        Debug_printf("\nError seeking INFO chunk");
        return MEDIATYPE_UNKNOWN;
    }
    uint32_t chunk_id, chunk_size;
    fread(&chunk_id, sizeof(chunk_id), 1, f);
    Debug_printf("\nINFO Chunk ID: %08x", chunk_id);
    fread(&chunk_size, sizeof(chunk_size), 1, f);
    Debug_printf("\nINFO Chunk size: %d", chunk_size);
    Debug_printf("\nNow at byte %d", ftell(f));
    // could read a whole bunch of other stuff that I should add to the mediaType file ...
    // but jump to offset 44 to get the track size
    fseek(f, 44, SEEK_CUR);
    uint16_t largest_track;
    fread(&largest_track, sizeof(uint16_t), 1, f);
    num_blocks = largest_track;
    // read TMAP
    fseek(f, 88, SEEK_SET);
    fread(&tmap, sizeof(tmap[0]), MAX_TRACKS, f);
#ifdef DEBUG
    Debug_printf("\nTrack, Index");
    for (int i=0; i<MAX_TRACKS; i++)
        Debug_printf("\n%d/4, %d", i, tmap[i]);
#endif
    // read TRKS table - depend upon little endian-ness
    fseek(f, 256, SEEK_SET);
    fread(&trks, sizeof(trks[0]), MAX_TRACKS, f);
#ifdef DEBUG
    Debug_printf("\nStart Block, Block Count, Bit Count");
    for (int i=0; i<MAX_TRACKS; i++)
        Debug_printf("\n%d, %d, %lu", trks[i].start_block, trks[i].block_count, trks[i].bit_count);
#endif
    // read WOZ tracks into SPIRAM


    return MEDIATYPE_WOZ;
}


// static bool create(FILE *f, uint32_t numBlock)
// {
//     return false;
// }

#endif // BUILD_APPLE