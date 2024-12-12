#include <_stdio.h>
#include <_types/_uint8_t.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>
#include "../../include/debug.h"
#include "qrmanager.h"
#include "qrcode.h"

QRManager qrManager;

std::vector<uint8_t> QRManager::encode(const void* src, size_t len, size_t version, size_t ecc, size_t *out_len) {
    QRCode qr_code;
    uint8_t qr_bytes[qrcode_getBufferSize(version)];

    bool err = qrcode_initText(&qr_code, qr_bytes, version, ecc, (const char*)src);

    size_t size = qr_code.size;
    *out_len = size*size;

    qrManager.out_buf.clear();
    qrManager.out_buf.shrink_to_fit();

    if (err) {
        return qrManager.out_buf;
    }

    for (uint8_t x = 0; x < size; x++) {
        for (uint8_t y = 0; y < size; y++) {
            uint8_t on = qrcode_getModule(&qr_code, x, y);
            qrManager.out_buf.push_back(on);
        }
    }

    return qrManager.out_buf;
}

std::vector<uint8_t> QRManager::to_bits(void) {
    auto bytes = qrManager.out_buf;
    size_t len = bytes.size();
    std::vector<uint8_t> out;

    uint8_t val = 0;
    for (auto i = 0; i < len; i++) {
        auto bit = i % 8;
        if (bit == 0 && i > 0) {
            out.push_back(val);
            val = 0;
        }
        val |= bytes[i+bit] << bit;
    }
    out.push_back(val);

    qrManager.out_buf = out;
    return qrManager.out_buf;
}

/*
0    1    2    3    4    5    6*   7    8    9*   10   11   12   13   14   15
- -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x
- -  - -  - -  - -  x -  x -  x -  x -  - x  - x  - x  - x  x x  x x  x x  x x
32   12   11   149  15   25  (11)  137  9   (12)  153  143  21   139  140  160
               [21]           ?    [9]       ?    [25] [15]      [11] [12] [32]
*/
//                     0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
uint8_t atascii[16] = {32,  12,  11,  149, 15,  25,  11,  137, 9,   12,  153, 143, 21,  139, 140, 160};

std::vector<uint8_t> QRManager::to_atascii(void) {
    auto bytes = qrManager.out_buf;
    size_t size = sqrt(bytes.size()); // TODO: Pass through/store?
    std::vector<uint8_t> out;

    for (auto y = 0; y < size; y += 2) {
        for (auto x = 0; x < size; x += 2) {
            uint8_t val = bytes[y*size+x];
            if (x+1 < size) val |= bytes[y*size+x+1] << 1;
            if (y+1 < size) val |= bytes[(y+1)*size+x] << 2;
            if (y+1 < size && x+1 < size) val |= bytes[(y+1)*size+x+1] << 3;
            out.push_back(atascii[val]);
        }
        out.push_back(155); // Atari newline
    }

    qrManager.out_buf = out;
    return qrManager.out_buf;
}
