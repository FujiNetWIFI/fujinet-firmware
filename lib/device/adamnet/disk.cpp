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
}

// Destructor
adamDisk::~adamDisk()
{
    if (_disk != nullptr)
        delete _disk;
}

mediatype_t adamDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    Debug_print("disk MOUNT\n");

    // Destroy any existing DiskType
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }

    // Determine DiskType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_DDP:
        device_active = true;
        break;
    case MEDIATYPE_DSK:
        device_active = true;
        break;
    case MEDIATYPE_ROM:
        device_active = true;
        break;
    }

    return MEDIATYPE_UNKNOWN;
}

void adamDisk::unmount()
{
}

void adamDisk::adamnet_control_status()
{
}

void adamDisk::adamnet_control_ack()
{
}

void adamDisk::adamnet_control_clr()
{
}

void adamDisk::adamnet_control_receive()
{
}

void adamDisk::adamnet_control_send()
{
}

void adamDisk::adamnet_control_nack()
{
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