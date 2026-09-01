#ifndef _DECB_LAYOUT_H_
#define _DECB_LAYOUT_H_

#include <stdint.h>
#include <stddef.h>

#include "casIndex.h"

/* RS-DOS geometry. 35 tracks x 18 sectors x 256 bytes; LSN = track*18 + (sector-1). */
#define DECB_SECTOR_SIZE    256
#define DECB_SECTORS_TRACK  18
#define DECB_TRACKS         35
#define DECB_TOTAL_SECTORS  (DECB_TRACKS * DECB_SECTORS_TRACK)   /* 630 */

#define DECB_DIR_TRACK      17
#define DECB_LSN_TRACK17    (DECB_DIR_TRACK * DECB_SECTORS_TRACK) /* 306 */
#define DECB_LSN_GAT        (DECB_LSN_TRACK17 + 1)                /* 307 */
#define DECB_LSN_DIR_FIRST  (DECB_LSN_TRACK17 + 2)                /* 308 */
#define DECB_DIR_SECTORS    9                                     /* 308..316 */
#define DECB_LSN_DISKNAME   (DECB_LSN_TRACK17 + 16)               /* 322 */

#define DECB_GRAN_SECTORS   9
#define DECB_GRAN_SIZE      (DECB_GRAN_SECTORS * DECB_SECTOR_SIZE) /* 2304 */
#define DECB_MAX_GRANULES   68
#define DECB_DIRENT_SIZE    32
#define DECB_DIRENTS_SECTOR 8
#define DECB_MAX_DIRENTS    (DECB_DIR_SECTORS * DECB_DIRENTS_SECTOR) /* 72 */

#define DECB_GRAN_FREE      0xff
#define DECB_GRAN_LAST      0xc0

#define DECB_ML_WRAP_BYTES  10

struct DecbFile
{
    char     name[9];        // sanitized, deduplicated
    char     ext[4];
    uint8_t  ftype;          // 0 BASIC, 1 data, 2 machine language, 3 text
    bool     ascii;
    int      cas_index;      // which CasIndex file this came from
    bool     wrap_ml;        // payload needed the machine-language wrapper
    uint16_t load;
    uint16_t exec;

    // On disk a file is prefix + body + suffix. The body comes from the tape
    // payload; the rest is synthesized. Tokenized BASIC takes a 3 byte header,
    // raw machine language a 5 byte preamble and postamble, others neither.
    uint8_t  pre[5];
    uint8_t  pre_len;
    uint8_t  post[5];
    uint8_t  post_len;
    uint32_t body_len;       // bytes drawn from the tape payload

    uint32_t disk_size;      // pre_len + body_len + post_len
    uint8_t  first_granule;
    uint8_t  granule_count;
    uint16_t last_sector_size;
    uint8_t  sectors_last_gran;
};

// Projects a decoded tape onto a read-only RS-DOS disk. Granules are allocated
// contiguously per file, which Disk BASIC accepts because it only follows the
// allocation chain, and which makes the reverse mapping a range check.
class DecbLayout
{
public:
    bool build(CasIndex *index, const char *volume_name = nullptr);

    int  file_count() const { return _nfiles; }
    const DecbFile &file(int i) const { return _files[i]; }

    uint32_t granules_used() const { return _grans_used; }
    int      dropped_full() const { return _dropped_full; }
    int      dropped_empty() const { return _dropped_empty; }

    // Returns false if the LSN is off the disk.
    bool read_sector(uint32_t lsn, uint8_t *buf);

    static uint32_t granule_to_lsn(uint8_t g);
    static bool     lsn_to_granule(uint32_t lsn, uint8_t *g, uint32_t *offset);

private:
    void build_gat(uint8_t *buf);
    void build_dir_sector(int which, uint8_t *buf);
    int  file_for_granule(uint8_t g) const;
    size_t read_disk_bytes(const DecbFile &f, uint32_t offset, uint8_t *buf, size_t len);

    CasIndex *_idx = nullptr;
    DecbFile  _files[DECB_MAX_DIRENTS];
    int       _nfiles = 0;
    uint32_t  _grans_used = 0;
    int       _dropped_full = 0;
    int       _dropped_empty = 0;
    char      _volume[9] = {0};
};

#endif // _DECB_LAYOUT_H_
