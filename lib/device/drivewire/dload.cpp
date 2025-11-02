#ifdef BUILD_COCO

#include <cstring>
#include "dload.h"
#include "fnFsSPIFFS.h"
#include "../../include/debug.h"

/**
 * @enum dload_state - the DLOAD state machine current state.
 */
static enum dload_state {
    PFILR_FROM_COCO,
    PFILR_TO_COCO,
    GET_FILENAME,
    INVALID_FILENAME,
    VALID_FILENAME,
    SEND_FILETYPE_TO_COCO,
    P_BLKR_FROM_COCO,
    P_BLKR_TO_COCO,
    GET_BLOCK_NUMBER,
    GET_P_NAK,
    GET_P_ACK,
    PUT_BLOCK,
    PABRT_FROM_COCO
} dState;

/**
 * @brief protocol command bytes
 */
#define P_ACK 0xC8
#define P_ABRT 0xBC
#define P_BLKR 0x97
#define P_FILR 0x8A
#define P_NAK 0xDE

/**
 * @brief filename requested by COCO
 */
static char fn[9];

uint8_t drivewireDload::xor_sum(uint8_t *buf, uint16_t len)
{
    uint8_t s = 0;

    // Calculate xor sum
    for (uint16_t i = 0; i < len; i++)
        s ^= buf[i];

    return s;
}

void drivewireDload::pfilr_from_coco()
{
    blockNum = 0;

    if (SYSTEM_BUS.available())
    {
        switch (SYSTEM_BUS.read())
        {
        case P_FILR:
            Debug_printv("P.FILR");
            dState = PFILR_TO_COCO;
            break;
        case P_ABRT:
            Debug_printv("P.ABRT");
            dState = PABRT_FROM_COCO;
            break;
        }
    }
}

void drivewireDload::pfilr_to_coco()
{
    Debug_printv("Replying P_FILR");
    SYSTEM_BUS.write(P_FILR);
    dState = GET_FILENAME;
}

void drivewireDload::get_filename()
{
    uint8_t sum_from_host = 0;

    memset(fn, 0x00, sizeof(fn));

    SYSTEM_BUS.read((uint8_t *)fn, 8);

    Debug_printv("Requested Filename %s", fn);

    sum_from_host = SYSTEM_BUS.read();

    if (xor_sum((uint8_t *)fn, 8) == sum_from_host)
    {
        dState = VALID_FILENAME;
    }
    else
    {
        dState = INVALID_FILENAME;
    }
}

void drivewireDload::invalid_filename()
{
    Debug_printv("Invalid Filename");
    SYSTEM_BUS.write(P_NAK);
    dState = PFILR_FROM_COCO;
}

void drivewireDload::valid_filename()
{
    Debug_printv("Valid Filename");
    SYSTEM_BUS.write(P_ACK);
    dState = SEND_FILETYPE_TO_COCO;
}

void drivewireDload::send_filetype()
{
    // // as part of this, we tokenize filename
    // // to get rid of the padded spaces
    // char *p = strtok(fn, " ");

    // // Handle null filename as not found
    // if (!p)
    //     send_filetype_file_not_found();

    // if (!fsFlash.exists(p))
    // {
    //     // File not found
    //     send_filetype_file_not_found();
    // }

    // // File found, let's open it.
    // fp = fsFlash.file_open(p);

    // // Could not open file, send file not found, report to console
    // if (!fp)
    // {
    //     Debug_printv("Could not open file %s, errno = %d", p, errno);
    //     send_filetype_file_not_found();
    // }

    // Debug_printv("File opened.");

    // //send_filetype_binary();
    // send_filetype_file_not_found();

    // dState = P_BLKR_FROM_COCO;
}

void drivewireDload::send_filetype_file_not_found()
{
    Debug_printv("Sending file not found response.");

    SYSTEM_BUS.write(0xFF); // File type
    SYSTEM_BUS.write(0x00); // ASCII flag
    SYSTEM_BUS.write(0xFF); // XOR byte

    // Return to initial state.
    dState = PFILR_FROM_COCO;
}

void drivewireDload::send_filetype_binary()
{
    Debug_printv("Sending binary filetype");
    SYSTEM_BUS.write(0x02); // Binary file
    SYSTEM_BUS.write(0x00); // ASCII flag = 0
    SYSTEM_BUS.write(0x02); // XOR value :)

    dState = P_BLKR_FROM_COCO;
}

void drivewireDload::pblkr_from_coco()
{
    uint8_t c = 0;

    if (!SYSTEM_BUS.available())
        return;

    c = SYSTEM_BUS.read();

    if (c != P_BLKR)
    {
        Debug_printv("Expected P.BLKR, got 0x%02x. Aborting.", c);
        dState = PFILR_FROM_COCO;
        return;
    }
}

void drivewireDload::pblkr_to_coco()
{
    SYSTEM_BUS.write(P_BLKR);
    dState = GET_BLOCK_NUMBER;
}

void drivewireDload::get_block_number()
{
    uint8_t b[2] = {0, 0}; // MSB/LSB of block #
    uint8_t s = 0;         // XOR sum

    // Read block bytes
    b[0] = SYSTEM_BUS.read();
    b[1] = SYSTEM_BUS.read();

    // Go ahead and get sum byte
    s = SYSTEM_BUS.read();

    // quickly check bit MSB of each block byte and bail if set.
    if ((b[0] & 0x80) || (b[1] & 0x80))
    {
        Debug_printv("Block # had bit 7 set, aborting. b1 = 0x%02x, b2 = 0x%02x", b[0], b[1]);
        dState = PFILR_FROM_COCO;
        return;
    }

    if (xor_sum(b, 2) != s)
    {
        // Invalid block #, send NAK
        Debug_printv("Invalid Block # checksum, sending NAK.");
        dState = GET_P_NAK;
        return;
    }

    // Reconstruct block number.
    blockNum = b[0] << 7;
    blockNum |= b[1];

    // Display block # in console
    Debug_printv("Block #%05u", blockNum);

    dState = GET_P_ACK;
}

void drivewireDload::get_p_nak()
{
    Debug_printv("Sending NAK");
    SYSTEM_BUS.write(P_NAK);
    dState = P_BLKR_FROM_COCO;
}

void drivewireDload::get_p_ack()
{
    Debug_printv("Sending ACK");
    SYSTEM_BUS.write(P_ACK);
    dState = PUT_BLOCK;
}

void drivewireDload::put_block()
{
    uint8_t b[128];
    uint8_t len = 0;
    size_t pos = blockNum * sizeof(b);

    // Clear block buffer
    memset(b, 0x00, sizeof(b));

    if (!feof(fp))
    {
        // Seek to desired file position
        fseek(fp, pos, SEEK_SET);

        // Read from file into block buffer
        fread(b, sizeof(uint8_t), sizeof(b), fp);

        // We always send 128 bytes
        len = 128;
    }

    // And spit out block length, block data and XOR sum.
    SYSTEM_BUS.write(len);
    SYSTEM_BUS.write(b, sizeof(b));
    SYSTEM_BUS.write(xor_sum(b, sizeof(b)));

    // Return to P.BLKR
    dState = P_BLKR_FROM_COCO;
}

void drivewireDload::pabrt_from_coco()
{
    Debug_printv("We got an abort from COCO.");
    dState = PFILR_FROM_COCO;
}

void drivewireDload::dload_process()
{
    switch (dState)
    {
    case PFILR_FROM_COCO:
        pfilr_from_coco();
        break;
    case PFILR_TO_COCO:
        pfilr_to_coco();
        break;
    case GET_FILENAME:
        get_filename();
        break;
    case INVALID_FILENAME:
        invalid_filename();
        break;
    case VALID_FILENAME:
        valid_filename();
        break;
    case SEND_FILETYPE_TO_COCO:
        send_filetype();
        break;
    case P_BLKR_FROM_COCO:
        pblkr_from_coco();
        break;
    case P_BLKR_TO_COCO:
        pblkr_to_coco();
        break;
    case GET_BLOCK_NUMBER:
        get_block_number();
        break;
    case GET_P_NAK:
        get_p_nak();
        break;
    case GET_P_ACK:
        get_p_ack();
        break;
    case PUT_BLOCK:
        put_block();
        break;
    case PABRT_FROM_COCO:
        pabrt_from_coco();
        break;
    }
}

#endif /* BUILD_COCO */
