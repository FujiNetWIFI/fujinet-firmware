#ifdef BUILD_ADAM

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "media.h"
#include "utils.h"
#include "fuji_endian.h"

adamDisk::adamDisk()
{
    device_active = false;
    blockNum = 0;
    status_response.length = htole16(1024);
    status_response.devtype = ADAMNET_DEVTYPE_BLOCK;
}

// Destructor
adamDisk::~adamDisk()
{
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }
}

void adamDisk::reset()
{
    blockNum = INVALID_SECTOR_VALUE;

    if (_media != nullptr)
    {
        _media->_media_last_block = INVALID_SECTOR_VALUE - 1;
        _media->_media_controller_status = 0;
    }
}

mediatype_t adamDisk::mount(FILE *f, const char *filename, uint32_t disksize,
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
    case MEDIATYPE_DDP:
        device_active = true;
        _media = new MediaTypeDDP();
        strcpy(_media->_disk_filename, filename);
        mt = _media->mount(f, disksize);
        break;
    case MEDIATYPE_DSK:
        _media = new MediaTypeDSK();
        strcpy(_media->_disk_filename,filename);
        mt = _media->mount(f, disksize);
        device_active = true;
        break;
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

void adamDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr)
    {
        _media->unmount();
        device_active = false;
    }
}

bool adamDisk::write_blank(FILE *fileh, uint32_t numBlocks)
{
    uint8_t buf[256];

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        memset(buf, 0xE5, 256);

        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
    }

    return false;
}

void adamDisk::adamnet_control_clr()
{
    int64_t t = esp_timer_get_time() - SYSTEM_BUS.start_time;

    if (t < 1500)
    {
        adamnet_response_send();
    }
}

void adamDisk::adamnet_control_receive()
{
    if (_media == nullptr)
        return;

    if (_media->read(blockNum, nullptr))
        adamnet_response_nack();
    else
        adamnet_response_ack();
}

void adamDisk::adamnet_control_send_block_num()
{
    uint8_t x[8];

    for (uint16_t i = 0; i < 5; i++)
        x[i] = adamnet_recv();

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    if (_media->num_blocks() < 0x10000UL) // Smaller than 64MB?
    {
        blockNum &= 0xFFFF; // Mask off upper bits
    }

    if (blockNum == 0xFACE)
    {
        _media->format(NULL);
    }

    SYSTEM_BUS.start_time=esp_timer_get_time();

    adamnet_response_ack();

    Debug_printf("BLOCK: %lu\n", blockNum);
}

void adamDisk::adamnet_control_send_block_data()
{
    if (_media == nullptr)
        return;

    adamnet_recv_buffer(_media->_media_blockbuff, 1024);
    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();
    Debug_printf("Block Data Write\n");

    _media->write(blockNum, false);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
}

void adamDisk::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length();

    if (s == 5)
        adamnet_control_send_block_num();
    else if (s == 1024)
        adamnet_control_send_block_data();
}

void adamDisk::adamnet_response_status()
{
    if (_media == nullptr)
        status_response.status = 0x40 | STATUS_NO_MEDIA;
    else
        status_response.status = 0x40 | _media->_media_controller_status;

    int64_t t = esp_timer_get_time() - SYSTEM_BUS.start_time;

    if (t < 300)
    {
        virtualDevice::adamnet_response_status();
    }
}

void adamDisk::adamnet_response_send()
{
    if (_media == nullptr)
        return;

    uint8_t c = adamnet_checksum(_media->_media_blockbuff, 1024);
    uint8_t b[1028];

    memcpy(&b[3], _media->_media_blockbuff, 1024);

    b[0] = 0xB0 | _devnum;
    b[1] = 0x04;
    b[2] = 0x00;
    b[1027] = c;
    adamnet_send_buffer(b, sizeof(b));
}

void adamDisk::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_RESET:
        reset();
        break;
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        adamnet_control_receive();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }

}

#endif /* BUILD_ADAM */
