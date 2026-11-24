#ifndef _CAS_READER_H_
#define _CAS_READER_H_

#include <stdint.h>
#include <stddef.h>

#include "casSource.h"

#define CAS_SYNC_BYTE   0x3c
#define CAS_LEADER_BYTE 0x55

#define CAS_BLOCK_HEADER 0x00
#define CAS_BLOCK_DATA   0x01
#define CAS_BLOCK_EOF    0xff

#define CAS_WINDOW_SIZE  1024

struct CasBlock
{
    uint32_t sync_bit;    // bit position of the type byte, just past the $3C
    uint32_t data_bit;    // bit position of the first data byte
    uint32_t leader_bits; // leader/gap preceding this block
    uint8_t  type;
    uint8_t  len;
    bool     sum_ok;
};

// Bit-level reader for CAS images. CAS is a bitstream: bits are LSB-first
// within each byte and blocks are not guaranteed to start on a byte boundary,
// so the $3C sync byte has to be found by a bit-wise search.
class CasReader
{
public:
    CasReader(CasSource *src) : _src(src) {}

    uint64_t bit_count() const { return (uint64_t)_size * 8; }
    void     begin();

    // The 8 bits starting at bitpos, LSB first, or -1 past the end.
    int  byte_at(uint64_t bitpos);

    // Returns the bit position just past the next $3C, or -1.
    int64_t find_sync(uint64_t bitpos, uint32_t *leader_bits = nullptr);

    // Reads one block, advancing *bitpos past its checksum. Returns the block
    // type, or -1 at end of image or on a truncated block.
    int read_block(uint64_t *bitpos, CasBlock *blk, uint8_t *payload);

    size_t read_bytes(uint64_t bitpos, uint8_t *buf, size_t len);

private:
    int raw_byte(uint32_t offset);

    CasSource *_src;
    uint32_t   _size = 0;

    uint8_t    _win[CAS_WINDOW_SIZE];
    uint32_t   _win_off = 0;
    uint32_t   _win_len = 0;
};

#endif // _CAS_READER_H_
