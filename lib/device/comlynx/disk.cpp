#ifdef BUILD_LYNX

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "media.h"
#include "utils.h"

lynxDisk::lynxDisk()
{
    device_active = false;
    blockNum = 0;
    //status_response[1] = 0x00;
    //status_response[2] = 0x01; // 256 bytes

    status_response[1] = MEDIA_BLOCK_SIZE % 256;
    status_response[2] = MEDIA_BLOCK_SIZE / 256;
    status_response[3] = 0x01; // Block device
}

// Destructor
lynxDisk::~lynxDisk()
{
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }
}

void lynxDisk::reset()
{
    blockNum = INVALID_SECTOR_VALUE;

    if (_media != nullptr)
    {
        _media->_media_last_block = INVALID_SECTOR_VALUE - 1;
        _media->_media_controller_status = 0;
    }
}

mediatype_t lynxDisk::mount(FILE *f, const char *filename, uint32_t disksize,
                            disk_access_flags_t access_mode, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("disk MOUNT %s\n", filename);

    // Destroy any existing MediaType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_ROM:
        _media = new MediaTypeROM();
        mt = _media->mount(f, disksize);
        device_active = true;
        break;
    default:
        device_active = false;
        break;
    }

    return mt;
}

void lynxDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr)
    {
        _media->unmount();
        device_active = false;
    }
}

bool lynxDisk::write_blank(FILE *fileh, uint32_t numBlocks)
{
    uint8_t buf[MEDIA_BLOCK_SIZE];

    memset(buf, 0x00, MEDIA_BLOCK_SIZE);

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);                // I don't understand why we do this four times? -SJ
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);
    }

    return false;
}

void lynxDisk::comlynx_control_clr()
{
    comlynx_response_send();
}

void lynxDisk::comlynx_control_receive()
{
    Debug_println("comlynx_control_receive() - Disk block read started\n");

    if (_media == nullptr)
        return;

    if (_media->read(blockNum, nullptr))
        comlynx_response_nack();
    else
        comlynx_response_ack();
}

void lynxDisk::comlynx_control_send_block_num()
{
    uint8_t x[5];       // 32-bit block num, plus 1 byte reserved

    Debug_printf("comlynx_control_send_block_num\n");
    for (uint16_t i = 0; i < 5; i++)
        x[i] = comlynx_recv();

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    if (_media->num_blocks() < 0x10000UL) // Smaller than 64MB?
    {
        blockNum &= 0xFFFF; // Mask off upper bits
    }

    if (blockNum == 0xFACE)
    {
        _media->format(NULL);
    }

    comlynx_response_ack();

    Debug_printf("BLOCK: %lu\n", blockNum);
}

void lynxDisk::comlynx_control_send_block_data()
{
    if (_media == nullptr)
        return;

    comlynx_recv_buffer(_media->_media_blockbuff, MEDIA_BLOCK_SIZE);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    comlynx_response_ack();
    Debug_printf("Block Data Write\n");

    _media->write(blockNum, false);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
}

void lynxDisk::comlynx_control_send()
{
    uint16_t s = comlynx_recv_length();

    Debug_printf("disk_control_send, S is %u\n",s);
    if (s == 5)
        comlynx_control_send_block_num();
    else if (s == MEDIA_BLOCK_SIZE)
        comlynx_control_send_block_data();
}

void lynxDisk::comlynx_response_status()
{
    if (_media == nullptr)
        status_response[4] = 0x40 | STATUS_NO_MEDIA;
    else
        status_response[4] = 0x40 | _media->_media_controller_status;

    virtualDevice::comlynx_response_status();
}

void lynxDisk::comlynx_response_send()
{
    if (_media == nullptr)
        return;

    uint8_t c = comlynx_checksum(_media->_media_blockbuff, MEDIA_BLOCK_SIZE);
    uint8_t b[MEDIA_BLOCK_SIZE+4];

    memcpy(&b[3], _media->_media_blockbuff, MEDIA_BLOCK_SIZE);

    b[0] = 0xB0 | _devnum;
    b[1] = MEDIA_BLOCK_SIZE / 256;          // block length
    b[2] = MEDIA_BLOCK_SIZE % 256;
    b[MEDIA_BLOCK_SIZE+3] = c;
    comlynx_send_buffer(b, sizeof(b));

    Debug_println("comlynx_send_buffer - disk block sent to Lynx");

    // get ACK or NACK from lynx (but don't care, they can request again if needed)
    recvbuffer_len = 0;         // reset recvbuffer to grab ACK/NACK from Lynx
    c = comlynx_recv();
    Debug_printf("comlynx_disk_send NAK/ACK: %02X\n", c);
}

void lynxDisk::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_RESET:
        reset();
        break;
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_RECEIVE:
        comlynx_control_receive();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
        break;
    }

}

#endif /* BUILD_LYNX */
