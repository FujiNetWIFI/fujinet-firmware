#ifdef BUILD_COCO

#include "casIndex.h"

#include <stdio.h>
#include <string.h>

bool CasIndex::is_namefile(const uint8_t *d, int len)
{
    if (len < 15)
        return false;
    if (d[8] > 3)                                   // file type
        return false;
    if (d[9] != 0x00 && d[9] != 0xff)               // ascii flag
        return false;
    if (d[10] != 0x00 && d[10] != 0xff)             // gap flag
        return false;
    for (int i = 0; i < 8; i++)                     // name must be printable
        if (d[i] != 0 && (d[i] < 0x20 || d[i] > 0x7e))
            return false;
    return true;
}

uint32_t CasIndex::index_bytes() const
{
    return (uint32_t)(_nblocks * sizeof(CasDataBlock) +
                      _nfiles * sizeof(CasFileEntry));
}

void CasIndex::close_file(bool unterminated)
{
    if (!_in_file)
        return;

    if (_nfiles < CAS_MAX_FILES && _cur.payload > 0)
    {
        _cur.unterminated = unterminated;
        _files[_nfiles] = _cur;
        _nfiles++;
        // Only valid once the file's block list is complete.
        _files[_nfiles - 1].seg_ok = check_segment_chain(_nfiles - 1);
    }
    _in_file = false;
}

bool CasIndex::build(CasReader *reader)
{
    _r = reader;
    _nfiles = 0;
    _nblocks = 0;
    _in_file = false;
    _unnamed_seq = 0;
    _noise_bytes = 0;
    _truncated = false;

    _r->begin();

    uint64_t pos = 0;
    int bad_run = 0;
    uint32_t noise_start = 0;
    bool noisy = false;

    for (;;)
    {
        CasBlock blk;
        uint8_t buf[256];
        int type = _r->read_block(&pos, &blk, buf);
        if (type < 0)
        {
            // Distinguish a clean end of image from a block cut short.
            if (_r->find_sync(pos, nullptr) >= 0)
                _truncated = true;
            break;
        }

        // Tapes commonly carry noise after the last end-of-file block.
        // Without this guard the sync hunt invents blocks in it.
        if (!blk.sum_ok)
        {
            if (bad_run == 0)
                noise_start = blk.sync_bit / 8;
            if (++bad_run >= CAS_BAD_RUN)
            {
                noisy = true;
                break;
            }
            if (_in_file)
                _cur.bad_blocks++;
            continue;
        }
        bad_run = 0;

        if (type == CAS_BLOCK_HEADER && is_namefile(buf, blk.len))
        {
            close_file(true);
            memset(&_cur, 0, sizeof _cur);
            memcpy(_cur.name, buf, 8);
            _cur.name[8] = 0;
            for (int i = 7; i >= 0 && (_cur.name[i] == ' ' || !_cur.name[i]); i--)
                _cur.name[i] = 0;
            _cur.named = true;
            _cur.ftype = buf[8];
            _cur.ascii = buf[9] != 0;
            _cur.gap = buf[10] != 0;
            _cur.exec = (uint16_t)((buf[11] << 8) | buf[12]);
            _cur.load = (uint16_t)((buf[13] << 8) | buf[14]);
            _cur.first_block = (uint16_t)_nblocks;
            _in_file = true;
        }
        else if (type == CAS_BLOCK_HEADER && blk.len < 15)
        {
            // Non-standard short header: metadata, not payload.
            close_file(true);
            memset(&_cur, 0, sizeof _cur);
            snprintf(_cur.name, sizeof _cur.name, "TAPE%02d", ++_unnamed_seq % 100);
            _cur.ftype = 1;
            _cur.first_block = (uint16_t)_nblocks;
            _in_file = true;
        }
        else if (type == CAS_BLOCK_DATA || type == CAS_BLOCK_HEADER)
        {
            // Tapes with their own loader carry payload in type-0 blocks.
            if (!_in_file)
            {
                memset(&_cur, 0, sizeof _cur);
                snprintf(_cur.name, sizeof _cur.name, "TAPE%02d", ++_unnamed_seq % 100);
                _cur.ftype = 1;
                _cur.first_block = (uint16_t)_nblocks;
                _in_file = true;
            }
            if (_nblocks < CAS_MAX_BLOCKS)
            {
                _blocks[_nblocks].data_bit = blk.data_bit;
                _blocks[_nblocks].len = blk.len;
                _nblocks++;
                _cur.block_count++;
                _cur.payload += blk.len;
            }
        }
        else if (type == CAS_BLOCK_EOF)
        {
            // Some tapes carry payload in the end-of-file block.
            if (blk.len && _in_file && _nblocks < CAS_MAX_BLOCKS)
            {
                _blocks[_nblocks].data_bit = blk.data_bit;
                _blocks[_nblocks].len = blk.len;
                _nblocks++;
                _cur.block_count++;
                _cur.payload += blk.len;
            }
            close_file(false);
        }
    }
    close_file(true);

    if (noisy)
        _noise_bytes = (uint32_t)(_r->bit_count() / 8) - noise_start;

    return _nfiles > 0;
}

size_t CasIndex::read_file(int idx, uint32_t offset, uint8_t *buf, size_t len)
{
    if (idx < 0 || idx >= _nfiles)
        return 0;

    const CasFileEntry &f = _files[idx];
    if (offset >= f.payload)
        return 0;
    if (offset + len > f.payload)
        len = f.payload - offset;

    // Index-only walk, no I/O.
    uint16_t b = f.first_block;
    uint32_t base = 0;
    while (b < f.first_block + f.block_count &&
           base + _blocks[b].len <= offset)
    {
        base += _blocks[b].len;
        b++;
    }

    size_t done = 0;
    while (done < len && b < f.first_block + f.block_count)
    {
        uint32_t within = offset + (uint32_t)done - base;
        uint32_t avail = _blocks[b].len - within;
        size_t want = len - done;
        if (want > avail)
            want = avail;

        size_t got = _r->read_bytes(_blocks[b].data_bit + (uint64_t)within * 8,
                                    buf + done, want);
        done += got;
        if (got < want)
            break;

        base += _blocks[b].len;
        b++;
    }
    return done;
}

// Does the payload already hold a DECB segment chain,
// $00 len16 load16 <data> ... $FF $0000 exec16?
//
// Tape machine-language files are usually raw and need that wrapper added, but
// some already carry it and wrapping twice produces a file that will not load.
// The tape's gap flag is meant to say which, but it is not reliable, so test
// the structure. Walks by seeking rather than buffering the file.
bool CasIndex::check_segment_chain(int idx)
{
    const CasFileEntry &f = _files[idx];
    if (f.ftype != 2 || f.payload < 10)
        return false;

    uint32_t off = 0;
    int segs = 0;

    while (off + 5 <= f.payload)
    {
        uint8_t h[5];
        if (read_file(idx, off, h, 5) != 5)
            return false;

        uint32_t ln = ((uint32_t)h[1] << 8) | h[2];

        if (h[0] == 0xff)                       // postamble
            return (off + 5 == f.payload) && segs > 0;
        if (h[0] != 0x00)
            return false;
        if (off + 5 + ln > f.payload)
            return false;

        off += 5 + ln;
        segs++;
        if (segs > 256)                         // runaway chain
            return false;
    }
    return false;
}

#endif // BUILD_COCO
