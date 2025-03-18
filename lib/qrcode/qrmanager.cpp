#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <vector>
#include "../../include/debug.h"
#include "qrmanager.h"
#include "qrcode.h"

QRManager qrManager;

std::vector<uint8_t> QRManager::encode(const void* src, size_t len, size_t version, size_t ecc, size_t *out_len) {
    qrManager.version = version;
    qrManager.ecc_mode = ecc;

    *out_len = 0;
    qrManager.out_buf.clear();
    qrManager.out_buf.shrink_to_fit();

    if (version < 1 || version > 40 || ecc > 3) {
        return qrManager.out_buf;
    }

    QRCode qr_code;
    uint8_t qr_bytes[qrcode_getBufferSize(version)];

    uint8_t err = qrcode_initText(&qr_code, qr_bytes, version, ecc, (const char*)src);

    qrManager.out_buf.push_back(qrManager.size());

    if (err == 0) {
        size_t size = qr_code.size;
        *out_len = size*size;

        for (uint8_t x = 0; x < size; x++) {
            for (uint8_t y = 0; y < size; y++) {
                uint8_t on = qrcode_getModule(&qr_code, x, y);
                qrManager.out_buf.push_back(on);
            }
        }
    }

    return qrManager.out_buf;
}

void QRManager::to_binary(void) {
    auto bytes = qrManager.out_buf;
    size_t len = bytes.size();
    std::vector<uint8_t> out;

    out.push_back(qrManager.size());

    uint8_t val = 0;
    for (auto i = 0; i < len; i++) {
        auto bit = i % 8;
        if (bit == 0 && i > 0) {
            out.push_back(val);
            val = 0;
        }
        val |= bytes[1+i+bit] << bit;
    }
    out.push_back(val);

    qrManager.out_buf = out;
}

void QRManager::to_bitmap(void) {
    auto bytes = qrManager.out_buf;
    size_t size = qrManager.size();
    size_t len = bytes.size();
    size_t bytes_per_row = ceil(size / 8.0);
    std::vector<uint8_t> out;

    out.push_back(size);

    uint8_t val = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    for (auto i = 0; i < len; i++) {
        val |= bytes[1+i];
        x++;
        if (x == size) {
            val = val << (bytes_per_row * 8 - x);
            out.push_back(val);
            val = 0;
            x = 0;
            y++;
        }
        else if (x % 8 == 0 && i > 0) {
            out.push_back(val);
            val = 0;
        }
        else {
            val = val << 1;
        }
        if (y == size) {
            break;
        }
    }

    qrManager.out_buf = out;
}

/*
This collapses each 2x2 groups of modules (pixels) into a single ATASCII character.
Values for ATASCII look up are as calculated as follows. Note that 6 and 9 have no
suitable ATASCII character, so diagonal lines are used instead. These seem to work
in most cases, but for best results you may want to use custom characters for those.

0    1    2    3    4    5    6*   7    8    9*   10   11   12   13   14   15
- -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x
- -  - -  - -  - -  x -  x -  x -  x -  - x  - x  - x  - x  x x  x x  x x  x x
32   12   11   149  15   25  (11)  137  9   (12)  153  143  21   139  140  160
               [21]           ?    [9]       ?    [25] [15]      [11] [12] [32]
*/
//                     0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
uint8_t atascii[16] = {32,  12,  11,  149, 15,  25,  6,   137, 9,   7,   153, 143, 21,  139, 140, 160};

void QRManager::to_atascii(void) {
    auto bytes = qrManager.out_buf;
    size_t size = qrManager.size();
    std::vector<uint8_t> out;

    out.push_back(size);

    for (auto y = 0; y < size; y += 2) {
        for (auto x = 0; x < size; x += 2) {
            uint8_t val = bytes[1+y*size+x];
            // QR Codes have odd number of rows/columns, so last ATASCII char is only half full
            if (x+1 < size) val |= bytes[1+y*size+x+1] << 1;
            if (y+1 < size) val |= bytes[1+(y+1)*size+x] << 2;
            if (y+1 < size && x+1 < size) val |= bytes[1+(y+1)*size+x+1] << 3;
            out.push_back(atascii[val]);
        }
        out.push_back(155); // Atari newline
    }

    qrManager.out_buf = out;
}

/*
This collapses each 2x2 groups of modules (pixels) into a single PETSCII character.
Values for PETSCII look up are as calculated as follows. Some characters need to be
reversed to get the correct glyph.

0    1    2    3    4    5    6*   7    8    9*   10   11   12   13   14   15
- -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x  - -  x -  - x  x x
- -  - -  - -  - -  x -  x -  x -  x -  - x  - x  - x  - x  x x  x x  x x  x x
32   190  188  18   187  161  18   18   172  191  18   18   162  18   18   18
               162            191  172            161  187       188  190  32
               146            146  146            146  146       146  146  146
*/

uint8_t petscii[2][16] = {
//   0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
    {32,  190, 188, 162, 187, 161, 191, 172, 172, 191, 161, 187, 162, 188, 190, 32},
    {0,   0,   0,   1,   0,   0,   1,   1,   0,   0,   1,   1,   0,   1,   1,   1}   // reverse on?
};

void QRManager::to_petscii(void) {
    auto bytes = qrManager.out_buf;
    size_t size = qrManager.size();
    std::vector<uint8_t> out;
    bool reverse = false;

    out.push_back(size);

    for (auto y = 0; y < size; y += 2) {
        for (auto x = 0; x < size; x += 2) {
            uint8_t val = bytes[1+y*size+x];
            // QR Codes have odd number of rows/columns, so last PETSCII char is only half full
            if (x+1 < size) val |= bytes[1+y*size+x+1] << 1;
            if (y+1 < size) val |= bytes[1+(y+1)*size+x] << 2;
            if (y+1 < size && x+1 < size) val |= bytes[1+(y+1)*size+x+1] << 3;
            if (reverse != petscii[1][val]) {
                reverse = !reverse;
                out.push_back(((reverse) ? 18 : 146)); // 18 Reverse On / 146 Reverse Off
            }
            out.push_back(petscii[0][val]);
        }
        out.push_back(13); // Carriage return
    }

    qrManager.out_buf = out;
}
