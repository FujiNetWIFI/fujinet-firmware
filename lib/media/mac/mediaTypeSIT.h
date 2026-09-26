/*
 * mediaTypeSIT.h - a StuffIt/BinHex/MacBinary archive in a Mac slot. At
 * mount the disk image inside is unpacked into PSRAM and handed to the media
 * a plain image of its type would get in that slot; everything else is
 * passed through to that media.
 */
#ifndef _MEDIATYPE_SIT_
#define _MEDIATYPE_SIT_

#include <cstdint>
#include <cstdio>
#include <memory>
#include "mediaType.h"

class MediaTypeSIT : public MediaType
{
public:
    explicit MediaTypeSIT(bool floppy_slot) : _floppy_slot(floppy_slot) {}
    ~MediaTypeSIT() override;

    mediatype_t mount(FILE *f, uint32_t disksize) override;
    void unmount() override;

    bool read(uint32_t blockNum, uint8_t *buffer) override { return _inner->read(blockNum, buffer); }
    bool write(uint32_t blockNum, uint8_t *buffer) override { return _inner->write(blockNum, buffer); }
    bool format(uint16_t *responsesize) override { return _inner->format(responsesize); }
    bool status() override { return _inner != nullptr && _inner->status(); }

    uint8_t trackmap(uint8_t t) override { return _inner->trackmap(t); }
    uint8_t *get_track(int t) override { return _inner->get_track(t); }
    int track_len(int t) override { return _inner->track_len(t); }
    int num_bits(int t) override { return _inner->num_bits(t); }

    void set_readonly(bool ro) override { _readonly = ro; }
    error_is_true flush() override { return _inner->flush(); }
    void flush_if_idle() override { _inner->flush_if_idle(); }
    bool accepts_sector_writes() const override { return _inner->accepts_sector_writes(); }
    success_is_true write_sector(int cyl, int side, int sec, const uint8_t *in524) override
    {
        return _inner->write_sector(cyl, side, sec, in524);
    }
    const MediaTypeDCD *dcd() const override { return _inner ? _inner->dcd() : nullptr; }

private:
    bool _floppy_slot;
    bool _readonly = true;
    std::unique_ptr<MediaType> _inner;  // the unpacked image's media

    FILE *_image_fh = nullptr;          // fmemopen over _image_buf, owned by _inner once mounted
    uint8_t *_image_buf = nullptr;      // PSRAM
    uint32_t _image_len = 0;
    char _inner_filename[256] = {0};
    mediatype_t _image_type = MEDIATYPE_UNKNOWN; // MEDIATYPE_DC42 or MEDIATYPE_DSK
    uint8_t *_rsrc_buf = nullptr;       // resource fork, only kept to decode NDIF
    uint32_t _rsrc_len = 0;

    success_is_true extract(FILE *archive_fh);
    success_is_true classify();
    void release();
};

#endif // _MEDIATYPE_SIT_
