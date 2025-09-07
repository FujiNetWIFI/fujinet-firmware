

#include "qrmanager.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

#include "qrcode.h"

std::vector<uint8_t> QRManager::encode(const void* input, uint16_t length, uint8_t version, qr_ecc_t ecc)
{
    if (version)
        qrcode.version = version;

    if (ecc)
        qrcode.ecc = ecc;

    int8_t err = 0;
    if (length)
    {
        data = std::string((char*)input, length);

        //printf("bytes[%d]\n", length);
        err = qrcode_initBytes(&qrcode, (uint8_t*)data.c_str(), length);
    }
    else
    {
        if (strlen((char*)input))
            data = std::string((char*)input);

        //printf("text[%s]\n", (char*)data);
        err = qrcode_initText(&qrcode, (char*)data.c_str());
    }
    if (err != 0) {
        // Error!
        //printf("error[%d]\n", err);
        return std::vector<uint8_t>();
    }

    // auto encoded = to_ansi();
    // printf("%s\n", encoded.data());

    //printf("version[%d] ecc[%d] mode[%d] output_mode[%d]\n", qrcode.version, qrcode.ecc, qrcode.mode, output_mode);
    switch (output_mode) {
        case QR_OUTPUT_MODE_ANSI:
            //printf("ansi\n");
            code = to_ansi();
            break;
        case QR_OUTPUT_MODE_BITMAP:
            //printf("bitmap\n");
            code =  to_bitmap();
            break;
        case QR_OUTPUT_MODE_SVG:
            //printf("svg\n");
            code =  to_svg();
            break;
        case QR_OUTPUT_MODE_ATASCII:
            //printf("atascii\n");
            code =  to_atascii();
            break;
        case QR_OUTPUT_MODE_PETSCII:
            //printf("petscii\n");
            code =  to_petscii();
            break;
        case QR_OUTPUT_MODE_BINARY:
        default:
            //printf("binary\n");
            code =  to_binary();
    }

    return code;
}

std::vector<uint8_t> QRManager::to_ansi() {
    std::vector<uint8_t> out;
    std::stringstream out_str;

    size_t size = qrcode.size;
    for (uint8_t y = 0; y < size; y++) {
        for (uint8_t x = 0; x < size; x++) {
            uint8_t on = qrcode_getModule(&qrcode, x, y);
            if (on == 1)
                out_str << ANSI_WHITE_BACKGROUND;
            else
                out_str << ANSI_RESET;

            out_str << "  ";
        }
        out_str << ANSI_RESET "\n";
    }

    out = std::vector<uint8_t>(out_str.str().begin(), out_str.str().end());
    return out;
}


std::vector<uint8_t> QRManager::to_binary(void)
{
    std::vector<uint8_t> out;

    out.push_back(qrcode.size);

    uint8_t val = 0;
    for (auto i = 0; i < qrcode.size; i++) {
        auto bit = i % 8;
        if (bit == 0 && i > 0) {
            out.push_back(val);
            val = 0;
        }
        val |= qrcode_getModule(&qrcode, i, 0) << bit;
    }
    out.push_back(val);

    return out;
}


std::vector<uint8_t> QRManager::to_bitmap(void) {
    std::vector<uint8_t> out;

    out.push_back(qrcode.size);

    uint8_t val = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    for (auto i = 0; i < qrcode.size; i++) {
        val |= qrcode_getModule(&qrcode, x, y);
        x++;
        if (x == qrcode.size) {
            val = val << (qrcode.size - x);
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
        if (y == qrcode.size) {
            break;
        }
    }

    return out;
}


std::vector<uint8_t> QRManager::to_svg(uint8_t scale, uint8_t padding) {
    std::vector<uint8_t> out;
    std::stringstream out_str;

    // Write SVG to stdout...
    //printf("<svg width=\"%d\" height=\"%d\" xmlns=\"http://www.w3.org/2000/svg\">\n", (qrcode.size + 2 * padding) * scale, (qrcode.size + 2 * padding) * scale);
    out_str << "<svg width=\"" << (qrcode.size + 2 * padding) * scale << "\" height=\"" << (qrcode.size + 2 * padding) * scale << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    //printf("  <rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"white\" />\n", (qrcode.size + 2 * padding) * scale, (qrcode.size + 2 * padding) * scale);
    out_str << "  <rect x=\"0\" y=\"0\" width=\"" << (qrcode.size + 2 * padding) * scale << "\" height=\"" << (qrcode.size + 2 * padding) * scale << "\" fill=\"white\" />\n";

    for (uint8_t y = 0; y < qrcode.size; y++) {
        uint8_t xstart = 0, xcount = 0;

        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                if (xcount == 0) { xstart = x; }
                xcount ++;
            } else if (xcount > 0) {
                //printf("  <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"black\" />\n", (xstart + padding) * scale, (y + padding) * scale, xcount * scale, scale);
                out_str << "  <rect x=\"" << (xstart + padding) * scale << "\" y=\"" << (y + padding) * scale << "\" width=\"" << xcount * scale << "\" height=\"" << scale << "\" fill=\"black\" />\n";
                xcount = 0;
            }
        }

        if (xcount > 0) {
            //printf("  <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"black\" />\n", (xstart + padding) * scale, (y + padding) * scale, xcount * scale, scale);
            out_str << "  <rect x=\"" << (xstart + padding) * scale << "\" y=\"" << (y + padding) * scale << "\" width=\"" << xcount * scale << "\" height=\"" << scale << "\" fill=\"black\" />\n";
        }
    }

    //puts("</svg>");
    out_str << "</svg>";

    out = std::vector<uint8_t>(out_str.str().begin(), out_str.str().end());
    return out;
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

std::vector<uint8_t> QRManager::to_atascii(void)
{
    uint8_t size = qrcode.size;
    std::vector<uint8_t> out;

    out.push_back(size);

    for (auto y = 0; y < size; y += 2) {
        for (auto x = 0; x < size; x += 2) {
            uint8_t val = qrcode_getModule(&qrcode, x, y);
            // QR Codes have odd number of rows/columns, so last ATASCII char is only half full
            if (x+1 < size) val |= qrcode_getModule(&qrcode, x+1, y) << 1;
            if (y+1 < size) val |= qrcode_getModule(&qrcode, x, y+1) << 2;
            if (y+1 < size && x+1 < size) val |= qrcode_getModule(&qrcode, x+1, y+1) << 3;
            out.push_back(atascii[val]);
        }
        out.push_back(155); // Atari newline
    }

    return out;
}

/*
This collapses each 2x2 groups of modules (pixels) into a single PETSCII character.
Values for PETSCII look up are as calculated as follows. Some characters need to be
reversed to get the correct glyph.

0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
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

std::vector<uint8_t> QRManager::to_petscii(void) 
{
    uint8_t size = qrcode.size;
    std::vector<uint8_t> out;
    bool reverse = false;

    out.push_back(13); // Carriage return

    for (auto y = 0; y < size; y += 2) {
        out.push_back(32); // Space
        for (auto x = 0; x < size; x += 2) {
            uint8_t val = qrcode_getModule(&qrcode, x, y);
            // QR Codes have odd number of rows/columns, so last PETSCII char is only half full
            if (x+1 < size) val |= qrcode_getModule(&qrcode, x+1, y) << 1;
            if (y+1 < size) val |= qrcode_getModule(&qrcode, x, y+1) << 2;
            if (y+1 < size && x+1 < size) val |= qrcode_getModule(&qrcode, x+1, y+1) << 3;
            if (reverse != petscii[1][val]) {
                reverse = !reverse;
                out.push_back(((reverse) ? 18 : 146)); // 18 Reverse On / 146 Reverse Off
            }
            out.push_back(petscii[0][val]);
        }
        out.push_back(13); // Carriage return
    }

    return out;
}

