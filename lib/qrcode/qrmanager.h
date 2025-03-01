/*
 * Wrapper around QRCode library
 *
 */

#ifndef QRCODE_MANAGER_H
#define QRCODE_MANAGER_H

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>

#include "qrcode.h"

#define QR_OUTPUT_MODE_BYTES   0
#define QR_OUTPUT_MODE_BINARY  1
#define QR_OUTPUT_MODE_ATASCII 2
#define QR_OUTPUT_MODE_BITMAP  3

class QRManager {
public:
    /**
    * encode - generate QR code as bytes
    * @src: Data to be encoded
    * @len: Length of the data to be encoded
    * @version: 1-40 (Size=17+4*Version)
    * @ecc: Error correction level (0=Low, 1=Medium, 2=Quartile, 3=High)
    * @out_len: Pointer to output length variable, or %NULL if not used
    * Returns: Allocated buffer of out_len bytes of encoded data,
    * or nullptr on failure
    *
    * Returned buffer consists of a 1 or 0 for each QR module, indicating
    * whether it is on (black) or off (white).
    */
    static std::vector<uint8_t> encode(const void* src, size_t len, size_t version, size_t ecc, size_t* out_len);

    /**
    * to_binary - Convert QR code in out_buf to compact binary format
    *
    * Replaces data in out_buf, where each byte is 0x00 or 0x01 with, with
    * compact data where each bit represents a single QR module (pixel).
    * So a 21x21 QR code will be 56 bytes (21*21/8). Data is returned LSB->MSB.
    */
    void to_binary(void);

    /**
    * to_bitmap - Convert QR code in out_buf to compact binary format
    *
    * Replaces data in out_buf, with data suitable for copying directly into
    * bitmap graphics memory. Each QR module (pixel) is represented by 1 bit.
    * Most significant bit is leftmost pixel. The last bits of the final byte
    * of each row are returned as 0s. A 21x21 QR code will be 63 bytes (3 bytes
    * per row of 21 bits (= 24 bits with 3 unused) * 21 rows).
    */
    void to_bitmap(void);

    /**
    * to_atascii - Convert QR code in out_buf to ATASCII
    *
    * Replaces bytes in out_buf with vector of ATASCII characters. Each
    * ATASCII character can represent 4 bits. Atari newlines (0x9B) are
    * added at the end of each row to facilitate printing direct to screen.
    */
    void to_atascii(void);

    /**
    * to_petscii - Convert QR code in out_buf to PETSCII
    *
    * Replaces bytes in out_buf with vector of PETSCII characters. Each
    * PETSCII character can represent 4 bits. Carriage returns (0x0D) are
    * added at the end of each row to facilitate printing direct to screen.
    */
   void to_petscii(void);

    size_t size() { return version * 4 + 17; }
    void set_buffer(const std::string& buffer) { in_buf = buffer; }
    void clear_buffer() { in_buf.clear(); }
    void add_buffer(const std::string& extra) { in_buf += extra; }

    std::string in_buf;
    std::vector<uint8_t> out_buf;

    uint8_t version = 1;
    uint8_t ecc_mode = ECC_LOW;
    uint8_t output_mode = QR_OUTPUT_MODE_BYTES;
};

extern QRManager qrManager;

#endif /* QRCODE_MANAGER_H */
