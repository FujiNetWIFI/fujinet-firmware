#ifdef BUILD_S100

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "media.h"
#include "utils.h"

s100spiDisk::s100spiDisk()
{
}

// Destructor
s100spiDisk::~s100spiDisk()
{
    if (_media != nullptr)
        delete _media;
}

void s100spiDisk::reset()
{
    blockNum = INVALID_SECTOR_VALUE;

    if (_media != nullptr)
    {
        _media->_media_last_block = INVALID_SECTOR_VALUE - 1;
        _media->_media_controller_status = 0;
    }
}

mediatype_t s100spiDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
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
    case MEDIATYPE_DSK:
        _media = new MediaTypeDSK();
        mt = _media->mount(f, disksize);
        device_active = true;
        break;
    default:
        device_active = false;
        break;
    }

    return mt;
}

void s100spiDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr)
    {
        _media->unmount();
        device_active = false;
    }
}

bool s100spiDisk::write_blank(FILE *fileh, uint32_t numBlocks)
{
    uint8_t buf[256];

    memset(buf, 0x00, 256);

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
        fwrite(buf, 1, 256, fileh);
    }

    return false;
}

void s100spiDisk::s100spi_control_clr()
{
    int64_t t = esp_timer_get_time() - s100Bus.start_time;

    if (t < 1500)
    {
        s100spi_response_send();
    }
}

void s100spiDisk::s100spi_control_receive()
{
    if (_media == nullptr)
        return;

    _media->read(blockNum, nullptr);
    s100spi_response_ack();
}

void s100spiDisk::s100spi_control_send_block_num()
{
    uint8_t x[8];

    for (uint16_t i = 0; i < 5; i++)
        x[i] = s100spi_recv();

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    if (blockNum == 0xFACE)
    {
        _media->format(NULL);
    }

    s100Bus.start_time=esp_timer_get_time();
    
    s100spi_response_ack();

    Debug_printf("BLOCK: %lu\n", blockNum);
}

void s100spiDisk::s100spi_control_send_block_data()
{
    if (_media == nullptr)
        return;

    s100spi_recv_buffer(_media->_media_blockbuff, 1024);
    s100Bus.start_time = esp_timer_get_time();
    s100spi_response_ack();
    Debug_printf("Block Data Write\n");

    _media->write(blockNum, false);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
}

void s100spiDisk::s100spi_control_send()
{
    uint16_t s = s100spi_recv_length();

    if (s == 5)
        s100spi_control_send_block_num();
    else if (s == 1024)
        s100spi_control_send_block_data();
}

void s100spiDisk::set_status(uint8_t s)
{
    if (s == true)
        s = STATUS_NO_BLOCK;
    else
        s = STATUS_OK;

    status_response[4] = _devnum | s;
}

void s100spiDisk::s100spi_response_status()
{
    if (_media == nullptr)
        status_response[4] = 0x40 | STATUS_NO_MEDIA;
    else
        status_response[4] = 0x40 | _media->_media_controller_status;
    
    virtualDevice::s100spi_response_status();
}

void s100spiDisk::s100spi_response_send()
{
    if (_media == nullptr)
        return;

    uint8_t c = s100spi_checksum(_media->_media_blockbuff, 1024);
    uint8_t b[1028];

    memcpy(&b[3], _media->_media_blockbuff, 1024);

    b[0] = 0xB0 | _devnum;
    b[1] = 0x04;
    b[2] = 0x00;
    b[1027] = c;
    s100spi_send_buffer(b, sizeof(b));
}

void s100spiDisk::s100spi_process(uint8_t b)
{
}

#endif /* NEW_TARGET */