#include "png_printer.h"
#include "../../include/debug.h"

// rewrite of TinyPngOut https://www.nayuki.io/page/tiny-png-output

#define DEFLATE_MAX_BLOCK_SIZE 0xFFFF

void pngPrinter::uint32_to_array(uint32_t src, uint8_t dest[4])
{
    dest[0] = (uint8_t)((src >> 24) & 0xff);
    dest[1] = (uint8_t)((src >> 16) & 0xff);
    dest[2] = (uint8_t)((src >> 8) & 0xff);
    dest[3] = (uint8_t)(src & 0xff);
}

uint32_t pngPrinter::update_adler32(uint32_t adler, uint8_t data)
{
    // https://gist.github.com/kornelski/710db9d30a64db0807c5bfbdbdecf85e
    unsigned s1 = adler & 0xffff;
    unsigned s2 = (adler >> 16) & 0xffff;

    s1 += data;
    s1 %= 65521;

    s2 += s1;
    s2 %= 65521;

    return (s2 << 16) | s1;
}

uint32_t pngPrinter::rc_crc32(uint32_t crc, const uint8_t *buf, size_t len)
// https://rosettacode.org/wiki/CRC-32#Implementation_2
{
    static uint32_t table[256];
    static int have_table = 0;
    uint32_t rem;
    uint8_t octet;
    int i, j;
    const uint8_t *p, *q;

    /* This check is not thread safe; there is no mutex. */
    if (have_table == 0)
    {
        /* Calculate CRC table. */
        for (i = 0; i < 256; i++)
        {
            rem = i; /* remainder from polynomial division */
            for (j = 0; j < 8; j++)
            {
                if (rem & 1)
                {
                    rem >>= 1;
                    rem ^= 0xedb88320;
                }
                else
                    rem >>= 1;
            }
            table[i] = rem;
        }
        have_table = 1;
    }

    crc = ~crc;
    q = buf + len;
    for (p = buf; p < q; p++)
    {
        octet = *p; /* Cast to unsigned octet. */
        crc = (crc >> 8) ^ table[(crc & 0xff) ^ octet];
    }
    return ~crc;
}

// uint32_t pngPrinter::rc_crc32(uint32_t crc, uint8_t c)
// // pass a single character
// {
//     rc_crc32(crc, &c, 1);
// }

void pngPrinter::png_signature()
{
#ifdef DEBUG
    Debug_println("Writing PNG Signature.");
#endif
    uint8_t sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, _file);
}

void pngPrinter::png_header()
{
    /*
        https://www.w3.org/TR/REC-png.pdf

        4.1.1 IHDR Image header
        The IHDR chunk must appear FIRST. It contains:
        Width: 4 bytes
        Height: 4 bytes
        Bit depth: 1 byte
        Color type: 1 byte
        Compression method: 1 byte
        Filter method: 1 byte
        Interlace method: 1 byte
    */
#ifdef DEBUG
    Debug_println("Writing PNG Header.");
#endif

    uint8_t header[] = {
        // IHDR chunk
        0x00, 0x00, 0x00, 0x0D, // 0-3      13 byte data length (indexes 8-20)
        'I', 'H', 'D', 'R',     // 4-7      IHDR
        0, 0, 0, 0,             // 8-11     'width' placeholder
        0, 0, 0, 0,             // 12-15    'height' placeholder
        0x08,                   // 16       1 byte depth
        0x03,                   // 17       0x03 color with palette
        0x00,                   // 18       compression method always 0
        0x00,                   // 19       no filter
        0x00,                   // 20       no interlace
        0, 0, 0, 0,             // 21-24    IHDR CRC-32 placeholder
    };

    uint32_to_array(width, &header[8]);
    uint32_to_array(height, &header[12]);
    /* 
        https://www.w3.org/TR/REC-png.pdf
        3.2 Chunk layout
        A 4-byte CRC (Cyclic Redundancy Check) calculated 
        on the preceding bytes in the chunk, including the 
        chunk type code and chunk data fields, but 
        not including the length field.
    */
    crc_value = rc_crc32(0, &header[4], 17);
    uint32_to_array(crc_value, &header[21]);
    fwrite(header, 1, 25, _file);
}

void pngPrinter::png_palette()
{
#ifdef DEBUG
    Debug_println("Writing PNG Palette.");
#endif
    uint8_t len[] = {0x00, 0x00, 0x00, 0x00}; // 0-3      size placeholder
    const uint8_t data[] = {
        // IDAT chunk data
        'P', 'L', 'T', 'E', // 4-7      PLTE
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x13, 0x13, 0x25, 0x25, 0x25, 0x37, 0x37, 0x37, 0x49,
        0x49, 0x49, 0x5F, 0x5F, 0x5F, 0x71, 0x71, 0x71, 0x7A, 0x7A, 0x7A, 0x8C, 0x8C, 0x8C, 0xA1, 0xA1,
        0xA1, 0xB3, 0xB3, 0xB3, 0xC5, 0xC5, 0xC5, 0xD7, 0xD7, 0xD7, 0xED, 0xED, 0xED, 0xFF, 0xFF, 0xFF,
        0x0A, 0x00, 0x00, 0x1C, 0x0A, 0x00, 0x32, 0x1F, 0x00, 0x44, 0x31, 0x00, 0x56, 0x43, 0x00, 0x68,
        0x55, 0x00, 0x7D, 0x6B, 0x00, 0x90, 0x7D, 0x00, 0x98, 0x86, 0x00, 0xAA, 0x98, 0x00, 0xC0, 0xAD,
        0x13, 0xD2, 0xBF, 0x25, 0xE4, 0xD1, 0x37, 0xF6, 0xE3, 0x49, 0xFF, 0xF9, 0x5F, 0xFF, 0xFF, 0x71,
        0x2A, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x51, 0x08, 0x00, 0x63, 0x1A, 0x00, 0x75, 0x2C, 0x00, 0x87,
        0x3E, 0x00, 0x9D, 0x54, 0x00, 0xAF, 0x66, 0x08, 0xB8, 0x6E, 0x11, 0xCA, 0x81, 0x23, 0xDF, 0x96,
        0x38, 0xF1, 0xA8, 0x4A, 0xFF, 0xBA, 0x5C, 0xFF, 0xCC, 0x6F, 0xFF, 0xE2, 0x84, 0xFF, 0xF4, 0x96,
        0x3D, 0x00, 0x00, 0x4F, 0x00, 0x00, 0x64, 0x00, 0x00, 0x77, 0x05, 0x00, 0x89, 0x17, 0x08, 0x9B,
        0x29, 0x1A, 0xB0, 0x3F, 0x30, 0xC2, 0x51, 0x42, 0xCB, 0x59, 0x4A, 0xDD, 0x6B, 0x5D, 0xF2, 0x81,
        0x72, 0xFF, 0x93, 0x84, 0xFF, 0xA5, 0x96, 0xFF, 0xB7, 0xA8, 0xFF, 0xCD, 0xBE, 0xFF, 0xDF, 0xD0,
        0x40, 0x00, 0x00, 0x52, 0x00, 0x12, 0x68, 0x00, 0x27, 0x7A, 0x00, 0x39, 0x8C, 0x08, 0x4B, 0x9E,
        0x1A, 0x5D, 0xB4, 0x30, 0x73, 0xC6, 0x42, 0x85, 0xCE, 0x4B, 0x8D, 0xE1, 0x5D, 0xA0, 0xF6, 0x72,
        0xB5, 0xFF, 0x84, 0xC7, 0xFF, 0x96, 0xD9, 0xFF, 0xA8, 0xEB, 0xFF, 0xBE, 0xFF, 0xFF, 0xD0, 0xFF,
        0x33, 0x00, 0x3F, 0x45, 0x00, 0x51, 0x5B, 0x00, 0x66, 0x6D, 0x00, 0x78, 0x7F, 0x03, 0x8A, 0x91,
        0x15, 0x9C, 0xA7, 0x2A, 0xB2, 0xB9, 0x3C, 0xC4, 0xC1, 0x45, 0xCD, 0xD3, 0x57, 0xDF, 0xE9, 0x6D,
        0xF4, 0xFB, 0x7F, 0xFF, 0xFF, 0x91, 0xFF, 0xFF, 0xA3, 0xFF, 0xFF, 0xB8, 0xFF, 0xFF, 0xCA, 0xFF,
        0x18, 0x00, 0x6E, 0x2A, 0x00, 0x80, 0x40, 0x00, 0x95, 0x52, 0x00, 0xA7, 0x64, 0x07, 0xB9, 0x76,
        0x19, 0xCB, 0x8C, 0x2F, 0xE1, 0x9E, 0x41, 0xF3, 0xA6, 0x4A, 0xFC, 0xB8, 0x5C, 0xFF, 0xCE, 0x71,
        0xFF, 0xE0, 0x83, 0xFF, 0xF2, 0x95, 0xFF, 0xFF, 0xA8, 0xFF, 0xFF, 0xBD, 0xFF, 0xFF, 0xCF, 0xFF,
        0x00, 0x00, 0x83, 0x07, 0x00, 0x95, 0x1C, 0x00, 0xAB, 0x2E, 0x03, 0xBD, 0x40, 0x15, 0xCF, 0x52,
        0x27, 0xE1, 0x68, 0x3D, 0xF6, 0x7A, 0x4F, 0xFF, 0x83, 0x58, 0xFF, 0x95, 0x6A, 0xFF, 0xAA, 0x7F,
        0xFF, 0xBC, 0x91, 0xFF, 0xCE, 0xA3, 0xFF, 0xE0, 0xB6, 0xFF, 0xF6, 0xCB, 0xFF, 0xFF, 0xDD, 0xFF,
        0x00, 0x00, 0x7B, 0x00, 0x00, 0x8D, 0x00, 0x06, 0xA3, 0x09, 0x18, 0xB5, 0x1B, 0x2A, 0xC7, 0x2D,
        0x3C, 0xD9, 0x42, 0x52, 0xEF, 0x54, 0x64, 0xFF, 0x5D, 0x6C, 0xFF, 0x6F, 0x7E, 0xFF, 0x85, 0x94,
        0xFF, 0x97, 0xA6, 0xFF, 0xA9, 0xB8, 0xFF, 0xBB, 0xCA, 0xFF, 0xD0, 0xE0, 0xFF, 0xE2, 0xF2, 0xFF,
        0x00, 0x00, 0x57, 0x00, 0x08, 0x6A, 0x00, 0x1D, 0x7F, 0x00, 0x2F, 0x91, 0x00, 0x41, 0xA3, 0x0D,
        0x53, 0xB5, 0x22, 0x69, 0xCB, 0x35, 0x7B, 0xDD, 0x3D, 0x83, 0xE5, 0x4F, 0x96, 0xF8, 0x65, 0xAB,
        0xFF, 0x77, 0xBD, 0xFF, 0x89, 0xCF, 0xFF, 0x9B, 0xE1, 0xFF, 0xB0, 0xF7, 0xFF, 0xC3, 0xFF, 0xFF,
        0x00, 0x0B, 0x1E, 0x00, 0x1D, 0x31, 0x00, 0x32, 0x46, 0x00, 0x44, 0x58, 0x00, 0x57, 0x6A, 0x00,
        0x69, 0x7C, 0x0E, 0x7E, 0x92, 0x20, 0x90, 0xA4, 0x29, 0x99, 0xAD, 0x3B, 0xAB, 0xBF, 0x51, 0xC0,
        0xD4, 0x63, 0xD2, 0xE6, 0x75, 0xE5, 0xF8, 0x87, 0xF7, 0xFF, 0x9C, 0xFF, 0xFF, 0xAE, 0xFF, 0xFF,
        0x00, 0x1A, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x42, 0x03, 0x00, 0x54, 0x15, 0x00, 0x66, 0x27, 0x00,
        0x78, 0x3A, 0x0A, 0x8D, 0x4F, 0x1C, 0x9F, 0x61, 0x25, 0xA8, 0x6A, 0x37, 0xBA, 0x7C, 0x4C, 0xD0,
        0x91, 0x5E, 0xE2, 0xA3, 0x70, 0xF4, 0xB5, 0x82, 0xFF, 0xC8, 0x98, 0xFF, 0xDD, 0xAA, 0xFF, 0xEF,
        0x00, 0x20, 0x00, 0x00, 0x32, 0x00, 0x00, 0x48, 0x00, 0x00, 0x5A, 0x00, 0x00, 0x6C, 0x00, 0x01,
        0x7E, 0x00, 0x16, 0x93, 0x0F, 0x28, 0xA6, 0x21, 0x31, 0xAE, 0x2A, 0x43, 0xC0, 0x3C, 0x58, 0xD6,
        0x52, 0x6A, 0xE8, 0x64, 0x7C, 0xFA, 0x76, 0x8F, 0xFF, 0x88, 0xA4, 0xFF, 0x9D, 0xB6, 0xFF, 0xAF,
        0x00, 0x1C, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x44, 0x00, 0x00, 0x56, 0x00, 0x09, 0x68, 0x00, 0x1B,
        0x7A, 0x00, 0x30, 0x8F, 0x00, 0x42, 0xA2, 0x00, 0x4B, 0xAA, 0x00, 0x5D, 0xBC, 0x0C, 0x73, 0xD2,
        0x21, 0x85, 0xE4, 0x33, 0x97, 0xF6, 0x46, 0xA9, 0xFF, 0x58, 0xBE, 0xFF, 0x6D, 0xD0, 0xFF, 0x7F,
        0x00, 0x0F, 0x00, 0x00, 0x21, 0x00, 0x08, 0x36, 0x00, 0x1A, 0x48, 0x00, 0x2C, 0x5A, 0x00, 0x3E,
        0x6C, 0x00, 0x54, 0x82, 0x00, 0x66, 0x94, 0x00, 0x6E, 0x9D, 0x00, 0x81, 0xAF, 0x00, 0x96, 0xC4,
        0x0A, 0xA8, 0xD6, 0x1C, 0xBA, 0xE8, 0x2E, 0xCC, 0xFA, 0x40, 0xE2, 0xFF, 0x56, 0xF4, 0xFF, 0x68,
        0x06, 0x00, 0x00, 0x18, 0x0C, 0x00, 0x2E, 0x22, 0x00, 0x40, 0x34, 0x00, 0x52, 0x46, 0x00, 0x64,
        0x58, 0x00, 0x79, 0x6E, 0x00, 0x8B, 0x80, 0x00, 0x94, 0x88, 0x00, 0xA6, 0x9A, 0x00, 0xBC, 0xB0,
        0x10, 0xCE, 0xC2, 0x22, 0xE0, 0xD4, 0x34, 0xF2, 0xE6, 0x47, 0xFF, 0xFC, 0x5C, 0xFF, 0xFF, 0x6E};
    uint8_t ccc[] = {0, 0, 0, 0}; // crc placeholder

    uint32_to_array(768, &len[0]);
    crc_value = rc_crc32(0, &data[0], 4 + 768);
    uint32_to_array(crc_value, &ccc[0]);

    fwrite(len, 1, 4, _file);
    fwrite(data, 1, 4 + 768, _file);
    fwrite(ccc, 1, 4, _file);
}

void pngPrinter::png_data()
{
    /*  
    https://www.w3.org/TR/REC-png.pdf

    4.1.3 IDAT Image data
    The IDAT chunk contains the actual image data. To create this data:
    1. Begin with image scanlines represented as described in Image layout (Section 2.3); the layout and total
    size of this raw data are determined by the fields of IHDR.
    2. Filter the image data according to the filtering method specified by the IHDR chunk. (Note that with
    filter method 0, the only one currently defined, this implies prepending a filter type byte to each scanline.)
    3. Compress the filtered data using the compression method specified by the IHDR chunk.
    The IDAT chunk contains the output datastream of the compression algorithm.
    To read the image data, reverse this process.
    There can be multiple IDAT chunks; if so, they must appear consecutively with no other intervening chunks.
    The compressed datastream is then the concatenation of the contents of all the IDAT chunks. The encoder
    can divide the compressed datastream into IDAT chunks however it wishes. (Multiple IDAT chunks are
    allowed so that encoders can work in a fixed amount of memory; typically the chunk size will correspond
    to the encoder’s buffer size.) It is important to emphasize that IDAT chunk boundaries have no semantic
    significance and can occur at any point in the compressed datastream
*/
#ifdef DEBUG
    Debug_println("Starting PNG Image Data...");
#endif
    uint8_t data[] = {
        // IDAT chunk
        0x00, 0x00, 0x00, 0x00, // 0-3      size placeholder
        'I', 'D', 'A', 'T',     // 4-7      IDAT
    };
    crc_value = rc_crc32(0, &data[4], 4); // begin CRC calculation

    // Compute data size
    // imgSize = (width + 1) * height; // +1 per line for filter 0's
    uint32_t numBlocks = imgSize / DEFLATE_MAX_BLOCK_SIZE;
    if (imgSize % DEFLATE_MAX_BLOCK_SIZE != 0)
        numBlocks++; // Round up

    dataSize += numBlocks * 5; // 5 bytes per DEFLATE uncompressed block header
    dataSize += 2;             // 2 bytes for zlib header
    dataSize += 4;             // 4 bytes for zlib Adler-32 checksum
    dataSize += imgSize;       // plus the image data

    uint32_to_array(dataSize, &data[0]); // store the computed size

    fwrite(data, 1, 8, _file); // write out the IDAT header
}

void pngPrinter::png_add_data(uint8_t *buf, uint32_t n)
{
    uint8_t c = 0;
    // Deflate-compressed datastreams within PNG are stored in the “zlib” format
    // https://tools.ietf.org/html/rfc1950#page-4

    if (img_pos == 0)
    {
#ifdef DEBUG
        Debug_println("Writing ZLIB header.");
#endif
        // write out a ZLIB header
        // Compression method/flags code: 1 byte (For PNG compression method 0, the zlib compression method/flags code must specify method code 8 (“deflate” compression))
        c = 0x08; // ZLIB "Deflate" compression scheme
        crc_value = rc_crc32(crc_value, c);
        fputc(c, _file);
        //  Additional flags/check bits: 1 byte (must be such that method + flags, when viewed as a 16-bit unsigned integer stored in MSB order (CMF*256 + FLG), is a multiple of 31.)
        c = 0x1D; // precompute so that 0x081D is divisible by 31 [ (0x800 / 31 + 1) * 31 - 0x800 ]
        crc_value = rc_crc32(crc_value, c);
        fputc(c, _file);

        //printf("new image\n");
    }

    uint32_t idx = 0;
    while (idx < n)
    {
        // at beginning of block?
        if (blk_pos == 0)
        {
            blkSize = DEFLATE_MAX_BLOCK_SIZE;
            uint8_t c = 0;
            if (imgSize - img_pos <= (uint32_t)blkSize)
            {
                c = 1; // final block
                blkSize = (uint16_t)(imgSize - img_pos);
            }

#ifdef DEBUG
            Debug_println("Writing ZLIB block header.");
#endif
            // write out block header
            crc_value = rc_crc32(crc_value, c);
            fputc(c, _file);

            // write out block size
            c = (uint8_t)(blkSize >> 0);
            crc_value = rc_crc32(crc_value, c);
            fputc(c, _file);
            c = (uint8_t)(blkSize >> 8);
            crc_value = rc_crc32(crc_value, c);
            fputc(c, _file);
            c = (uint8_t)((blkSize >> 0) ^ 0xFF);
            crc_value = rc_crc32(crc_value, c);
            fputc(c, _file);
            c = (uint8_t)((blkSize >> 8) ^ 0xFF);
            crc_value = rc_crc32(crc_value, c);
            fputc(c, _file);

            //printf("new block\n");
        }

        //at beginning of a line?
        if (Xpos == 0)
        {
#ifdef DEBUG
            Debug_printf("Starting PNG line %d ... ",Ypos);
#endif
            c = 0;
            crc_value = rc_crc32(crc_value, c);
            adler_value = update_adler32(adler_value, c);
            fputc(c, _file);
            //printf("\nnew line %d ", c);

            img_pos++;
            blk_pos++;
        }

        // put byte from buffer
        c = buf[idx];
        crc_value = rc_crc32(crc_value, c);
        adler_value = update_adler32(adler_value, c);
        fputc(c, _file);
        //printf("%d ", c);

        Xpos++;
        idx++;
        img_pos++;
        blk_pos++;

        // check for end of's
        if (Xpos == width)
        {
#ifdef DEBUG
            Debug_println("Finished PNG line.");
#endif
            Xpos = 0;
            Ypos++;
        }
        if (blk_pos == blkSize)
        {
#ifdef DEBUG
            Debug_println("End of ZLIB block.");
#endif
            blk_pos = 0;
        }
    };

    if (img_pos == imgSize)
    {
#ifdef DEBUG
        Debug_println("Writing ZLIB Adler checksum and PNG data CRC.");
#endif
        uint8_t data[] = {
            0, 0, 0, 0, // Adler32 Check value: 4 bytes
            0, 0, 0, 0  // CRC32: 4 bytes
        };
        uint32_to_array(adler_value, &data[0]);
        for (int i = 0; i < 4; i++)
            crc_value = rc_crc32(crc_value, data[i]); // crc_value = rc_crc32(crc_value, data[0],4); ?????????
        uint32_to_array(crc_value, &data[4]);
        fwrite(data, 1, 8, _file);
        png_end();
    }
}

void pngPrinter::png_end()
{
#ifdef DEBUG
    Debug_println("Writing PNG footer.");
#endif
    unsigned char end[] = {
        0x00, 0x00, 0x00, 0x00,  // zero length
        'I', 'E', 'N', 'D',      // IEND
        0xAE, 0x42, 0x60, 0x82}; // crc32 - precompute because always the same
    fwrite(end, 1, 12, _file);
}

// TODO: Anything here?
void pngPrinter::pre_close_file()
{

}

void pngPrinter::post_new_file()
{
    // call PNG header routines
    png_signature();
    png_header();
    png_palette();
    // start IDAT chunk and now ready for data
    png_data();
}

bool pngPrinter::process_buffer(uint8_t n, uint8_t aux1, uint8_t aux2)
{
// copy buffer[] into linebuffer[]
#ifdef DEBUG
    Debug_printf("%d bytes rx'd by PNG printer\n", n);
#endif
    uint16_t i = 0;
    while (i < n && img_pos < imgSize)
    {
        //Debug_println("processing buffer.");
        if (BOLflag)
        {
#ifdef DEBUG
            Debug_println("Processing new line!");
#endif
            rep_code = buffer[i++];
            BOLflag = false;
        }
        else
        {
            line_buffer[line_index++] = buffer[i++];
        }
        if (line_index == 320)
        {
            while (rep_code-- > 0)
            {
#ifdef DEBUG
                Debug_printf("Adding line %d\n", rep_code);
#endif
                png_add_data(&line_buffer[0], 320);
            }
            BOLflag = true;
            line_index = 0;
        }
    }
    return true;
}

