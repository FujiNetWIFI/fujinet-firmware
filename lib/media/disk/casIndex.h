#ifndef _CAS_INDEX_H_
#define _CAS_INDEX_H_

#include <stdint.h>
#include <stddef.h>

#include "casReader.h"

#define CAS_MAX_FILES   80     // a disk holds 72 directory entries
#define CAS_MAX_BLOCKS  1024   // more than a full disk of granules can hold
#define CAS_BAD_RUN     3      // consecutive bad blocks means trailing noise

// Position and length only; the bytes stay in the image.
struct CasDataBlock
{
    uint32_t data_bit;
    uint8_t  len;
} __attribute__((packed));

struct CasFileEntry
{
    char     name[9];        // as recorded on tape, trailing spaces trimmed
    bool     named;          // came from a real namefile block
    uint8_t  ftype;          // 0 BASIC, 1 data, 2 machine language, 3 text
    bool     ascii;
    bool     gap;
    uint16_t exec;
    uint16_t load;
    uint32_t payload;        // total data bytes across this file's blocks
    uint16_t first_block;    // index into the block table
    uint16_t block_count;
    bool     seg_ok;         // payload is already a DECB segment chain
    bool     unterminated;   // no end-of-file block
    uint16_t bad_blocks;
};

// Walks a CAS image once and records what it contains. Only an index is kept;
// file contents are never held in memory.
class CasIndex
{
public:
    bool build(CasReader *reader);

    int  file_count() const { return _nfiles; }
    const CasFileEntry &file(int i) const { return _files[i]; }

    uint32_t noise_bytes() const { return _noise_bytes; }
    bool     truncated() const { return _truncated; }
    uint32_t blocks_indexed() const { return _nblocks; }
    uint32_t index_bytes() const;

    size_t read_file(int idx, uint32_t offset, uint8_t *buf, size_t len);

    // Identified by content, not length: valid namefile blocks are usually 15
    // bytes but not always, and some tapes carry payload in type-0 blocks.
    static bool is_namefile(const uint8_t *d, int len);

private:
    void close_file(bool unterminated);
    bool check_segment_chain(int idx);

    CasReader   *_r = nullptr;

    CasFileEntry _files[CAS_MAX_FILES];
    int          _nfiles = 0;

    CasDataBlock _blocks[CAS_MAX_BLOCKS];
    uint32_t     _nblocks = 0;

    // state while scanning
    bool         _in_file = false;
    CasFileEntry _cur;
    int          _unnamed_seq = 0;

    uint32_t     _noise_bytes = 0;
    bool         _truncated = false;
};

#endif // _CAS_INDEX_H_
