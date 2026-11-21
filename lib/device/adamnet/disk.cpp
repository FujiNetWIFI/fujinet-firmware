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

error_is_true adamDisk::mount(fnFile *f, const char *filename, uint32_t disksize,
                              disk_access_flags_t access_mode, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("disk MOUNT %s\n", filename);

    _read_only = !(access_mode & DISK_ACCESS_MODE_WRITE);

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
    default:
        device_active = false;
        fnio::fclose(f);
        break;
    }

    if (mt == MEDIATYPE_UNKNOWN)
    {
        unmount();
        RETURN_ERROR_AS_TRUE();
    }

    RETURN_SUCCESS_AS_FALSE();
}

void adamDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr)
    {
        _media->unmount();
        delete _media;
        _media = nullptr;
        device_active = false;
    }
}

error_is_true adamDisk::write_blank(fnFile *fileh, const NewDiskRequest &req)
{
    uint8_t buf[256];

    // mount() takes the image size in 32 bits.
    if (req.numBlocks == 0 || req.numBlocks > UINT32_MAX / 1024)
        RETURN_ERROR_AS_TRUE();

    for (uint32_t b = 0; b < req.numBlocks; b++)
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

    if (blockNum == 0xFACE && !_read_only)
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
    if (SYSTEM_BUS.transaction_get(_media->_media_blockbuff, DDP_BLOCK_SIZE).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (is_config_device || _read_only)
    {
        Debug_printf("Refusing write to read-only disk, block %lu\n", blockNum);
        blockNum = 0xFFFFFFFF;
        _media->_media_last_block = 0xFFFFFFFE;
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_printf("Block Data Write\n");

    error_is_true result = _media->write(blockNum, false);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
    if (result.is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }
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
