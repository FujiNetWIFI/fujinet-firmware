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

typedef enum {
    QR_ECC_LOW,
    QR_ECC_MEDIUM,
    QR_ECC_QUARTILE,
    QR_ECC_HIGH
} qr_ecc_t;

typedef enum {
    QR_OUTPUT_MODE_BINARY,
    QR_OUTPUT_MODE_ANSI,
    QR_OUTPUT_MODE_BITMAP,
    QR_OUTPUT_MODE_SVG,
    QR_OUTPUT_MODE_ATASCII,
    QR_OUTPUT_MODE_PETSCII
} ouput_mode_t;


#define ANSI_WHITE_BACKGROUND "\e[47m"
#define ANSI_RESET "\e[0m"

// SVG constants...
#define SVG_SCALE    5                  // Nominal size of modules
#define SVG_PADDING  4                  // White padding around QR code

class QRManager {
    QRCode qrcode{};

public:

    QRManager(uint8_t version = 0, qr_ecc_t ecc = QR_ECC_LOW, ouput_mode_t mode = QR_OUTPUT_MODE_BINARY) {
        qrcode.version = version;
        qrcode.ecc = ecc;

        output_mode = mode;
        code = std::vector<uint8_t>();
    };
    ~QRManager() {
        free(qrcode.modules);
    }

    /**
    * encode - generate QR code as bytes
    * @src: Data to be encoded
    * @len: Length of the data to be encoded
    * @version: 1-40 (Size=17+4*Version)
    * @ecc: Error correction level (0=Low, 1=Medium, 2=Quartile, 3=High)
    * Returns: Allocated buffer of out_len bytes of encoded data,
    * or nullptr on failure
    *
    * Returned buffer consists of a 1 or 0 for each QR module, indicating
    * whether it is on (black) or off (white).
    */
    std::vector<uint8_t> encode(const void* input = nullptr, uint16_t length = 0, uint8_t version = 0, qr_ecc_t ecc = QR_ECC_LOW);;

    /**
    * to_ansi - Convert QR code in out_buf to ATASCII
    *
    * Replaces data in out_buf, with ATASCII code for drawing the QR code.
    */
    std::vector<uint8_t> to_ansi(void);

    /**
    * to_binary - Convert QR code in out_buf to compact binary format
    *
    * Replaces data in out_buf, where each byte is 0x00 or 0x01 with, with
    * compact data where each bit represents a single QR module (pixel).
    * So a 21x21 QR code will be 56 bytes (21*21/8). Data is returned LSB->MSB.
    */
    std::vector<uint8_t> to_binary(void);

    /**
    * to_bitmap - Convert QR code in out_buf to compact binary format
    *
    * Replaces data in out_buf, with data suitable for copying directly into
    * bitmap graphics memory. Each QR module (pixel) is represented by 1 bit.
    * Most significant bit is leftmost pixel. The last bits of the final byte
    * of each row are returned as 0s. A 21x21 QR code will be 63 bytes (3 bytes
    * per row of 21 bits (= 24 bits with 3 unused) * 21 rows).
    */
    std::vector<uint8_t> to_bitmap(void);

    /**
    * to_svg - Convert QR code in out_buf to SVG
    *
    * Replaces data in out_buf, with svg code for drawing the QR code.
    */
    std::vector<uint8_t> to_svg(uint8_t scale = SVG_SCALE, uint8_t padding = SVG_PADDING);

    /**
    * to_atascii - Convert QR code in out_buf to ATASCII
    *
    * Replaces bytes in out_buf with vector of ATASCII characters. Each
    * ATASCII character can represent 4 bits. Atari newlines (0x9B) are
    * added at the end of each row to facilitate printing direct to screen.
    */
    std::vector<uint8_t> to_atascii(void);

    /**
    * to_petscii - Convert QR code in out_buf to PETSCII
    *
    * Replaces bytes in out_buf with vector of PETSCII characters. Each
    * PETSCII character can represent 4 bits. Carriage returns (0x0D) are
    * added at the end of each row to facilitate printing direct to screen.
    */
    std::vector<uint8_t> to_petscii(void);

    uint8_t version() { return qrcode.version; }
    void version(uint8_t v) { qrcode.version = v; }
    qr_ecc_t ecc() { return (qr_ecc_t)qrcode.ecc; }
    void ecc(qr_ecc_t e) { qrcode.ecc = e; }
    uint8_t size() { return qrcode.size; }
    ouput_mode_t output_mode = QR_OUTPUT_MODE_BINARY;

    std::string data;
    std::vector<uint8_t> code;
};

#endif /* QRCODE_MANAGER_H */
