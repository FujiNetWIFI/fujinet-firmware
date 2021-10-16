#ifdef BUILD_ADAM

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "../device/adamnet/disk.h"
#include "media.h"

adamDisk::adamDisk()
{
    device_active = false;
    blockNum = 0;
}

// Destructor
adamDisk::~adamDisk()
{
    if (_media != nullptr)
        delete _media;
}

mediatype_t adamDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    Debug_print("disk MOUNT\n");

    // Destroy any existing DiskType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine DiskType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_DDP:
        device_active = true;
        _media = new MediaTypeDDP();
        return _media->mount(f, disksize);
        break;
    case MEDIATYPE_DSK:
        device_active = true;
        break;
    case MEDIATYPE_ROM:
        device_active = true;
        break;
    default:
        device_active = false;
        break;
    }

    return MEDIATYPE_UNKNOWN;
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

void adamDisk::adamnet_control_status()
{
    Debug_println("adamnet_control_status()");
    // Temporary, respond 5x5 for now
    ets_delay_us(150);
    adamnet_send(0x84);
    adamnet_send(0x00);
    adamnet_send(0x04);
    adamnet_send(0x01);
    adamnet_send(0x40);
    adamnet_send(0x45);
}

void adamDisk::adamnet_control_ack()
{
    Debug_println("adamnet_control_ack()");
}

void adamDisk::adamnet_control_clr()
{
    uint8_t c = adamnet_checksum(_media->_media_blockbuff, 1024);

    Debug_println("adamnet_control_clr()");

    ets_delay_us(150);
    adamnet_send(0xB4); // response.data.receive
    adamnet_send(0x04); // 1024 bytes hi byte
    adamnet_send(0x00); //            lo byte
    adamnet_send_buffer(_media->_media_blockbuff, 1024);
    adamnet_send(c); // checksum
}

void adamDisk::adamnet_control_receive()
{
    Debug_println("adamnet_control_receive()");
    _media->read(blockNum, nullptr);
}

void adamDisk::adamnet_control_send()
{
    Debug_println("adamnet_control_send()");
    uint16_t s = adamnet_recv_length();
    uint8_t x[8];

    for (uint16_t i = 0; i < s; i++)
        x[i] = adamnet_recv();

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    ets_delay_us(150);
    adamnet_send(0x94);
    Debug_printf("REQ BLOCK: %lu\n", blockNum);
}

void adamDisk::adamnet_control_nack()
{
    Debug_println("adamnet_control_nack()");
}

void adamDisk::adamnet_response_status()
{
}

void adamDisk::adamnet_response_ack()
{
}

void adamDisk::adamnet_response_cancel()
{
}

void adamDisk::adamnet_response_send()
{
}

void adamDisk::adamnet_response_nack()
{
}

void adamDisk::adamnet_control_ready()
{
    Debug_println("adamnet_control_ready()");
    ets_delay_us(200);
    adamnet_send(0x94);
}

void adamDisk::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4;

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_ACK:
        adamnet_control_ack();
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
    case MN_NACK:
        adamnet_control_nack();
        break;
    case NM_STATUS:
        adamnet_response_status();
        break;
    case NM_ACK:
        adamnet_response_ack();
        break;
    case NM_CANCEL:
        adamnet_response_cancel();
        break;
    case NM_SEND:
        adamnet_response_send();
        break;
    case NM_NACK:
        adamnet_response_nack();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

#endif /* BUILD_ADAM */