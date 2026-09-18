#ifndef _MAC_GCR_H_
#define _MAC_GCR_H_

/**
 * Macintosh 400K/800K GCR ("Sony") track encoder.
 *
 * Turns plain sector data into the bitstream a Mac expects to read from
 * a 3.5" GCR floppy, so sector images (.dsk, DiskCopy 4.2 .image) can be
 * served through the same path as MOOF flux images.
 *
 * Pure C/C++, no platform dependencies, so it can be unit-tested on the
 * host (see tests/mac_gcr_test.cpp).
 *
 * References: MAME src/lib/formats/ap_dsk35.cpp and flopimg.cpp (track
 * format description), greaseweazle codec/macintosh (track layout as
 * captured from real disks), Apple "Sony" driver documentation.
 */

#include <stdint.h>
#include <stddef.h>

#define MAC_GCR_SECTOR_SIZE   512
#define MAC_GCR_TAG_SIZE      12
#define MAC_GCR_SECTOR_TOTAL  (MAC_GCR_TAG_SIZE + MAC_GCR_SECTOR_SIZE) // 524
#define MAC_GCR_ENC_SECTOR    703   // 524 bytes -> 703 GCR nibbles incl. checksum

#define MAC_GCR_CYLINDERS     80
#define MAC_GCR_ZONES         5
#define MAC_GCR_MAX_SECTORS   12
#define MAC_GCR_BIT_TIMING    16    // x 125 ns = 2000 ns per bit cell

#define MAC_GCR_FORMAT_SS     0x02  // format byte: single sided, 2:1 interleave
#define MAC_GCR_FORMAT_DS     0x22  // format byte: double sided, 2:1 interleave

#define MAC_GCR_400K_BYTES    409600
#define MAC_GCR_800K_BYTES    819200

// sectors per track in each of the five speed zones (16 cylinders each)
int mac_gcr_sectors_per_track(int cyl);

// total sectors before the given cylinder (one side), for image offsets
int mac_gcr_sectors_before_cyl(int cyl);

// bits per track for the cylinder's zone, matching a real disk spinning
// at the zone's RPM with a 2 us bit cell. Always a multiple of 8.
uint32_t mac_gcr_track_bits(int cyl);

// bytes needed to hold a track of the given cylinder
size_t mac_gcr_track_bytes(int cyl);

// 6-and-2 encode one 6-bit value
uint8_t mac_gcr_encode6(uint8_t v);

/**
 * Encode one 524-byte sector (12 tag + 512 data) into 703 GCR-ready
 * 6-bit values (not yet passed through the 6&2 table).
 */
void mac_gcr_encode_sector(const uint8_t *in524, uint8_t *out703);

/**
 * Build a complete track bitstream.
 *
 *   sectors     nsec x 512 bytes of sector data in logical order
 *   tags        nsec x 12 bytes of tag data, or NULL for zero tags
 *   cyl, side   physical position
 *   format      MAC_GCR_FORMAT_SS or MAC_GCR_FORMAT_DS
 *   out         buffer of at least mac_gcr_track_bytes(cyl) bytes
 *
 * Returns the number of valid bits in `out` (== mac_gcr_track_bits(cyl)).
 * Bits are stored MSB first, the same layout MOOF uses.
 */
uint32_t mac_gcr_encode_track(const uint8_t *sectors, const uint8_t *tags,
                              int cyl, int side, uint8_t format,
                              uint8_t *out, size_t out_size);

/*
 * ---- Write support -------------------------------------------------------
 *
 * The Mac writes a sector by re-writing only its data field (sync, data
 * mark, sector number, 703 nibbles, epilogue) right after it has read the
 * sector's address field; a format writes whole tracks, address fields
 * included. The Pico captures the IWM's write stream as a bitstream
 * (1 = flux transition, 2 us cells, MSB first) and hands it to the ESP32,
 * which decodes it with mac_gcr_decode_capture(), stores the sector in the
 * image file and patches the encoded track in place with
 * mac_gcr_patch_sector(). Tracks built by mac_gcr_encode_track() are
 * byte aligned, so a sector's 703 data nibbles sit at a fixed byte offset
 * given by mac_gcr_sector_data_offset().
 */

// inverse of mac_gcr_encode6(); -1 if b is not a valid 6&2 code
int mac_gcr_decode6(uint8_t b);

// Inverse of mac_gcr_encode_sector(): 703 6-bit values (already passed
// through mac_gcr_decode6) back to 12 tag + 512 data bytes. Returns 0 when
// the Sony checksum matches, 1 otherwise (out524 is filled either way).
int mac_gcr_decode_sector(const uint8_t *in703, uint8_t *out524);

// Byte offset, in a track built by mac_gcr_encode_track() for `cyl` with
// `format`, of the 703 encoded data nibbles of logical sector `sec`.
// Returns 0 if sec is out of range for the cylinder.
size_t mac_gcr_sector_data_offset(int cyl, uint8_t format, int sec);

// Re-encode one sector's data field in place. in524 = 12 tag + 512 data.
bool mac_gcr_patch_sector(uint8_t *track, size_t track_size, int cyl,
                          uint8_t format, int sec, const uint8_t *in524);

struct mac_gcr_written_sector
{
    int sector;         // sector number from the data field
    int cyl;            // from the address field that preceded it, or -1
    int side;           // from the address field, or -1
    uint8_t format;     // from the address field, or 0
    bool checksum_ok;   // data field checksum matched
    uint8_t data[MAC_GCR_SECTOR_TOTAL]; // 12 tag + 512 data
};

/**
 * Decode a captured write stream. `bits` holds nbits bits MSB first,
 * 1 = flux transition, at any alignment (the marks are searched at every
 * bit offset, like the IWM does). Every data field found is reported;
 * a data field that directly follows an address field with the same
 * sector number gets that field's cylinder/side (a format writes both).
 * Returns the number of entries written to `out`.
 */
int mac_gcr_decode_capture(const uint8_t *bits, uint32_t nbits,
                           mac_gcr_written_sector *out, int max_out);

#endif // _MAC_GCR_H_
