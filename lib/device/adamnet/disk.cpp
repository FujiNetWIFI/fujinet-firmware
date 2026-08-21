#ifdef BUILD_ADAM

#include "disk.h"
#include "debug.h"

#define DDP_BLOCK_SIZE 1024

adamDisk::adamDisk()
{
    device_active = false;
    blockNum = 0;
#ifndef ESP_PLATFORM
    _pc_no_response_deadline = true;
#endif
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
    _receive_acked = false;

    if (_media != nullptr)
    {
        _media->_media_last_block = INVALID_SECTOR_VALUE - 1;
        _media->_media_controller_status = 0;
    }
}

mediatype_t adamDisk::mount(fnFile *f, const char *filename, uint32_t disksize,
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

error_is_true adamDisk::write_blank(fnFile *fileh, uint32_t numBlocks)
{
    uint8_t buf[256];

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        memset(buf, 0xE5, 256);

        fnio::fwrite(buf, 1, 256, fileh);
        fnio::fwrite(buf, 1, 256, fileh);
        fnio::fwrite(buf, 1, 256, fileh);
        fnio::fwrite(buf, 1, 256, fileh);
    }

    RETURN_SUCCESS_AS_FALSE();
}

void adamDisk::adamnet_control_send_block_num(const FujiAdamPacket &packet)
{
    u32le_t num;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    memcpy(&num, packet.data()->data(), sizeof(num));
    blockNum = num;

    if (_media == nullptr) {
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (_media->num_blocks() < 0x10000UL) // Smaller than 64MB?
    {
        blockNum &= 0xFFFF; // Mask off upper bits
    }

    Debug_printf("BLOCK: %lu\n", blockNum);

    if (blockNum == 0xFACE)
    {
        _media->format(NULL);
    }

    if (_media->read(blockNum, nullptr).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }
    SYSTEM_BUS.transaction_send(_media->_media_blockbuff, DDP_BLOCK_SIZE);
}

void adamDisk::adamnet_control_send_block_data()
{
    if (_media == nullptr)
        return;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(_media->_media_blockbuff, DDP_BLOCK_SIZE);

    if (is_config_device)
    {
        Debug_printf("Refusing spurious write to read-only config device, block %lu\n", blockNum);
        blockNum = 0xFFFFFFFF;
        _media->_media_last_block = 0xFFFFFFFE;
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_printf("Block Data Write\n");

    _media->write(blockNum, false);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
    SYSTEM_BUS.transaction_success();
}

void adamDisk::adamnet_control_send(const FujiAdamPacket &packet)
{
    if (packet.data()->size() == 5)
        adamnet_control_send_block_num(packet);
    else if (packet.data()->size() == DDP_BLOCK_SIZE)
        adamnet_control_send_block_data();
}

AdamNetStatus adamDisk::deviceStatus()
{
    AdamNetStatus status;

    status.length = DDP_BLOCK_SIZE;
    status.devtype = ADAMNET_DEVTYPE::BLOCK;

    status.status = 0x40;
    if (_media == nullptr)
        status.status |= STATUS_NO_MEDIA;
    else
        status.status |= _media->_media_controller_status;

    return status;
}

#endif /* BUILD_ADAM */
