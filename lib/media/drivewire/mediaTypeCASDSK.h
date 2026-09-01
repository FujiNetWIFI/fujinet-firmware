#ifndef _MEDIATYPE_CASDSK_
#define _MEDIATYPE_CASDSK_

#include <stdio.h>

#include "mediaType.h"
#include "casSourceFile.h"
#include "casIndex.h"
#include "decbLayout.h"

/*
 * Presents a .CAS cassette image as a read-only RS-DOS disk.
 *
 * This is a disk media type, not a cassette one. Unlike the CAS support on
 * other platforms it does not emulate a tape deck or drive a cassette port:
 * the CoCo mounts one of these in an ordinary drive slot and sees a 35 track
 * Disk BASIC floppy, where DIR, LOAD, RUN and LOADM all behave normally.
 *
 * The tape is decoded once at mount to build an index of the files it holds.
 * Nothing tape-shaped is ever exposed to the CoCo, which only ever sees plain
 * 256 byte sectors. Sector contents, including the directory and the granule
 * allocation table, are synthesized on demand from that index, so the image
 * itself is never held in memory.
 */
class MediaTypeCASDSK : public MediaType
{
public:
    error_is_true read(uint32_t blockNum, uint16_t *readcount) override;
    error_is_true write(uint32_t blockNum, bool verify) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;
    void unmount() override;

    uint8_t status() override;

private:
    CasSourceFile _source;
    CasReader     _reader{&_source};
    CasIndex      _index;
    DecbLayout    _layout;
    bool          _ready = false;
};

#endif // _MEDIATYPE_CASDSK_
