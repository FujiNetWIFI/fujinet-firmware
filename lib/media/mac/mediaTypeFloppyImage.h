#ifndef _MEDIATYPE_FLOPPYIMAGE_
#define _MEDIATYPE_FLOPPYIMAGE_

#include <stdio.h>
#include <stdint.h>

#include "mediaType.h"
#include "mediaTypeMOOF.h" // MAX_TRACKS

/**
 * A 400K or 800K sector image served as a GCR floppy.
 *
 * Accepts raw sector images (.dsk/.img, 409600 or 819200 bytes) and
 * DiskCopy 4.2 images (.image, 84-byte header, data then tags). At mount
 * time every track is run through the GCR encoder (macGCR.cpp) into
 * PSRAM, after which it looks exactly like a MOOF to the floppy device:
 * trackmap()/get_track()/num_bits() hand the RMT streamer a bitstream.
 *
 * Writes: the Pico captures the Mac's write stream and the floppy device
 * hands decoded sectors to write_sector(), which updates the image file
 * and patches the encoded track in place.
 */
class MediaTypeFloppyImage : public MediaType
{
private:
    uint8_t *trk_ptrs[MAX_TRACKS] = {};
    uint32_t trk_bits[MAX_TRACKS] = {};
    uint32_t trk_bytes[MAX_TRACKS] = {};

    uint32_t data_offset = 0;   // start of sector data in the file
    uint32_t data_size = 0;     // 409600 or 819200
    uint32_t tag_offset = 0;    // start of tag data, or 0 if none
    uint8_t format_byte = 0;    // 0x02 single sided, 0x22 double sided

    bool parse_header(uint32_t disksize);
    bool encode_all_tracks();
    void free_tracks();

public:
    MediaTypeFloppyImage() {};
    virtual ~MediaTypeFloppyImage() { free_tracks(); }

    virtual bool read(uint32_t blockNum, uint8_t *buffer) override { return true; };
    virtual bool write(uint32_t blockNum, uint8_t *buffer) override { return true; };
    virtual bool format(uint16_t *responsesize) override { return false; };

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    virtual void unmount() override;

    virtual bool status() override { return (_media_fileh != nullptr); }

    // store one written sector (12 tag + 512 data) in the file and in the
    // encoded track; the caller decides whether the mount is writable
    bool write_sector(int cyl, int side, int sec, const uint8_t *in524);
    uint8_t format() const { return format_byte; }

    uint8_t trackmap(uint8_t t) override { return (t < MAX_TRACKS && trk_ptrs[t]) ? t : 255; }
    uint8_t *get_track(int t) override { return trk_ptrs[t]; }
    int track_len(int t) override { return trk_bytes[t]; }
    int num_bits(int t) override { return trk_bits[t]; }
};

#endif // _MEDIATYPE_FLOPPYIMAGE_
