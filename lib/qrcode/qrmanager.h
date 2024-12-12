/*
 * Wrapper around QRCode library
 *
 */

#ifndef QRCODE_MANAGER_H
#define QRCODE_MANAGER_H

#include <_types/_uint8_t.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>

class QRManager {
public:
    /**
    * encode - generate QR code as bits
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

    void set_buffer(const std::string& buffer) { qr_buffer = buffer; }
    void clear_buffer() { qr_buffer.clear(); }
    void add_buffer(const std::string& extra) { qr_buffer += extra; }

    std::string qr_buffer;
    std::vector<uint8_t> qr_output;
};

extern QRManager qrManager;

#endif /* QRCODE_MANAGER_H */
