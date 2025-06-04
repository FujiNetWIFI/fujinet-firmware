#ifdef BUILD_RC2014

#include "mediaTypeIMG.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

#ifdef ESP_PLATFORM
#include <unistd.h>  // for fsync
#endif

#include "../../include/debug.h"

// From https://github.com/wwarthen/RomWBW/blob/master/Source/CPM3/biosldr.z80

const std::map<mediatype_t, MediaTypeIMG::CpmDiskImageDetails> disk_paramater_blocks =
{
    // dpb$hd:		; 8MB Hard Disk Drive
    // 	dw  	64		; spt: sectors per track
    // 	db  	5		; bsh: block shift factor
    // 	db  	31		; blm: block mask
    // 	db  	1		; exm: extent mask
    // 	dw  	2047		; dsm: total storage in blocks - 1 = (8mb / 4k bls) - 1 = 2047
    // 	dw  	511		; drm: dir entries - 1 = 512 - 1 = 511
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	8000h		; cks: directory check vector size - permanent storage = 8000H
    // 	dw  	16		; off: reserved tracks = 16 trks * (16 trks * 16 heads * 16 secs * 512 bytes) = 128k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_HD, { "IMG", (8 * 1024 * 1024), { 64, 5, 31, 1, 2047, 511, 0b11110000, 0, 0x8000, 16, 2, 3 } } },

    // dpb$fd720:	; 3.5" DS/DD Floppy Drive (720K)
    // 	dw  	36		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	0		; exm: extent mask
    // 	dw  	350		; dsm: total storage in blocks - 1 blk = ((720k - 18k off) / 2k bls) - 1 = 350
    // 	dw  	127		; drm: dir entries - 1 = 128 - 1 = 127
    // 	db  	11000000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	32		; cks: directory check vector size = 128 / 4
    // 	dw  	4		; off: reserved tracks = 4 trks * (512 b/sec * 36 sec/trk) = 18k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD720, { "IMG", (80 * 2 * 9 * 512), { 36, 4, 15, 0, 350, 127, 0b11000000, 0, 32, 4, 2, 3 } } },

    // The PCW256/Pro-DOS definition is listed here;

    // BEGIN A2 SAM COUPE Pro-DOS - DSDD 160 tpi 3.5"
    // DENSITY MFM ,LOW
    // CYLINDERS 80
    // SIDES 2
    // SECTORS 9,512
    // SIDE1 0 1,2,3,4,5,6,7,8,9
    // SIDE2 1 1,2,3,4,5,6,7,8,9
    // ORDER SIDES
    // BSH 4 BLM 15 EXM 0 DSM 356 DRM 255 AL0 0F0H AL1 0 OFS 1
    // END
    // dpb$fd720_prosdos:	; 3.5" DS/DD Floppy Drive (720K) - SAM Coupe Pro-DOS
    // 	dw  	36		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	0		; exm: extent mask
    // 	dw  	356		; dsm: total storage in blocks - 1 blk = 350
    // 	dw  	255		; drm: dir entries - 1
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	64	; cks: directory check vector size = 256 / 4
    // 	dw  	1		; off: reserved tracks = 4 trks * (512 b/sec * 36 sec/trk) = 18k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD720_PCW256, { "DSK", (80 * 2 * 9 * 512), { 36, 4, 15, 0, 356, 255, 0b11110000, 0, 64, 1, 2, 3 } } },

    // dpb_fd144:	; 3.5" DS/HD Floppy Drive (1.44M)
    // 	dw  	72		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	0		; exm: extent mask
    // 	dw  	710		; dsm: total storage in blocks - 1 blk = ((1,440k - 18k off) / 2k bls) - 1 = 710
    // 	dw  	255		; drm: dir entries - 1 = 256 - 1 = 255
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	64		; cks: directory check vector size = 256 / 4
    // 	dw  	2		; off: reserved tracks = 2 trks * (512 b/sec * 72 sec/trk) = 18k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD144, { "IMG", (80 * 2 * 18 * 512), { 72, 4, 15, 0, 710, 255, 0b11110000, 0, 64, 2, 2, 3 } } },

    // dpb_fd360:	; 5.25" DS/DD Floppy Drive (360K) 
    // 	dw  	36		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	1		; exm: extent mask
    // 	dw  	170		; dsm: total storage in blocks - 1 blk = ((360k - 18k off) / 2k bls) - 1 = 170
    // 	dw  	127		; drm: dir entries - 1 = 128 - 1 = 127
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	32		; cks: directory check vector size = 128 / 4
    // 	dw  	4		; off: reserved tracks = 4 trks * (512 b/sec * 36 sec/trk) = 18k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD360, { "IMG", (40 * 2 * 9 * 512), { 36, 4, 15, 1, 170, 127, 0b11110000, 0, 32, 4, 2, 3 } } },

    // dpb_fd120:	; 5.25" DS/HD Floppy Drive (1.2M)
    // 	dw  	60		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	0		; exm: extent mask
    // 	dw  	591		; dsm: total storage in blocks - 1 blk = ((1,200k - 15k off) / 2k bls) - 1 = 591
    // 	dw  	255		; drm: dir entries - 1 = 256 - 1 = 255
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	64		; cks: directory check vector size = 256 / 4
    // 	dw  	2		; off: reserved tracks = 2 trks * (512 b/sec * 60 sec/trk) = 15k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD120, { "IMG", (80 * 2 * 15 * 512), { 60, 4, 15, 0, 591, 255, 0b11110000, 0, 64, 2, 2, 3 } } },

    // dpb_fd111:	; 8" DS/DD Floppy Drive (1.11M)
    // 	dw  	60		; spt: sectors per track
    // 	db  	4		; bsh: block shift factor
    // 	db  	15		; blm: block mask
    // 	db  	0		; exm: extent mask
    // 	dw  	569		; dsm: total storage in blocks - 1 blk = ((1,155k - 15k off) / 2k bls) - 1 = 569
    // 	dw  	255		; drm: dir entries - 1 = 256 - 1 = 255
    // 	db  	11110000b	; al0: dir blk bit map, first byte
    // 	db  	00000000b	; al1: dir blk bit map, second byte
    // 	dw  	64		; cks: directory check vector size = 256 / 4
    // 	dw  	2		; off: reserved tracks = 2 trks * (512 b/sec * 60 sec/trk) = 15k
    // 	db	2		; psh: 2 for 512 byte sectors
    // 	db	3		; phm: (512 / 128) - 1
    { MEDIATYPE_IMG_FD111, { "IMG", (77 * 2 * 15 * 512), { 60, 4, 15, 0, 569, 255, 0b11110000, 0, 64, 2, 2, 3 } } },



    //The IBM 3740 format of 8" floppy disks was used in the 1970s IBM 3740 data
    //entry system, which replaced keypunch systems that stored data on punched cards.
    //The disk format was single-sided, single-density, and had 73 tracks with 26
    //sectors storing 128 bytes per sector. The total capacity was 237.25 KB.
};




// Returns byte offset of given sector number (1-based)
uint32_t MediaTypeIMG::_sector_to_offset(uint16_t sectorNum)
{
    return (uint32_t )sectorNum * DISK_BYTES_PER_SECTOR_SINGLE;
}

// Returns TRUE if an error condition occurred
bool MediaTypeIMG::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("IMG READ\r\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if (sectornum > _media_num_sectors)
    {
        Debug_printf("::read sector %d > %lu\r\n", sectornum, _media_num_sectors);
        return true;
    }

    uint16_t sectorSize = DISK_BYTES_PER_SECTOR_BLOCK;

    memset(_media_sectorbuff, 0, sizeof(_media_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _media_last_sector + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fseek(_media_fileh, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fread(_media_sectorbuff, 1, sectorSize, _media_fileh) != sectorSize;

    if (err == false)
        _media_last_sector = sectornum;
    else
        _media_last_sector = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeIMG::write(uint16_t sectornum, bool verify)
{
    Debug_printf("IMG WRITE %u of %lu\r\n", sectornum, _media_num_sectors);

    // Return an error if we're trying to write beyond the end of the disk
    if (sectornum > _media_num_sectors)
    {
        Debug_printf("::write sector %d > %lu\r\n", sectornum, _media_num_sectors);
        _media_controller_status=2;
        return true;
    }

    uint32_t offset = _sector_to_offset(sectornum);

    _media_last_sector = INVALID_SECTOR_VALUE;

    // Perform a seek if we're writing to the sector after the last one
    int e;
    if (sectornum != _media_last_sector + 1)
    {
        e = fseek(_media_fileh, offset, SEEK_SET);
        if (e != 0)
        {
            Debug_printf("::write seek error %d\r\n", e);
            return true;
        }
    }
    // Write the data
    e = fwrite(_media_sectorbuff, 1, DISK_BYTES_PER_SECTOR_BLOCK, _media_fileh);
    if (e != DISK_BYTES_PER_SECTOR_BLOCK)
    {
        Debug_printf("::write error %d, %d\r\n", e, errno);
        return true;
    }

    int ret = fflush(_media_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_media_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("IMG::write fsync:%d\r\n", ret);

    _media_last_sector = sectornum;
    _media_controller_status=0;

    return false;
}

void MediaTypeIMG::status(uint8_t statusbuff[4])
{
}

/*
    From Altirra manual:
    The format command formats a disk, writing 40 tracks and then verifying all sectors.
    All sectors are filleded with the data byte $00. On completion, the drive returns
    a sector-sized buffer containing a list of 16-bit bad sector numbers terminated by $FFFF.
*/
// Returns TRUE if an error condition occurred
bool MediaTypeIMG::format(uint16_t *responsesize)
{
    Debug_print("IMG FORMAT\r\n");

    // Populate an empty bad sector map
    memset(_media_sectorbuff, 0, sizeof(_media_sectorbuff));

    *responsesize = _media_sector_size;

    return false;
}

/* 
 Mount 8MB RC2014 CP/M "slice"
*/
mediatype_t MediaTypeIMG::mount(FILE *f, uint32_t disksize, mediatype_t disk_type)
{
    Debug_print("IMG MOUNT\r\n");

    _media_fileh = f;
    _media_num_sectors = disksize / 512;
    _mediatype = disk_type;

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeIMG::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    return true;
}

#endif /* BUILD_ADAM */
