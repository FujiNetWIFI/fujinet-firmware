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

void adamDisk::adamnet_control_clr()
{
#ifdef ESP_PLATFORM
    // Real bus only: stream the block only inside the master's window.
    if (GET_TIMESTAMP() - SYSTEM_BUS.start_time >= 1500)
        return;
#endif
    adamnet_response_send();
}

void adamDisk::adamnet_control_receive()
{
    if (_media == nullptr)
        return;

    // Already ACKed this block's RECEIVE. The master re-polls RECEIVE while we
    // read; a second ACK would desync the next block, so stay silent until a new
    // block number resets us. (stall_silent: yield without discardInput().)
    if (_receive_acked)
    {
        SYSTEM_BUS.stall_silent = true;
        return;
    }

    // Seek emulation.
    if (GET_TIMESTAMP() < _seek_deadline)
    {
        _seek_is_read = true;
        _media->read(blockNum, nullptr);
        SYSTEM_BUS.stall_silent = true;
        return;
    }

    bool err = _media->read(blockNum, nullptr);

    // Match a real drive's RECEIVE->ACK turnaround so the master masks
    // interrupts for the coming block before we answer.
    SYSTEM_BUS.wait_turnaround(ADAMNET_DISK_RECV_TURNAROUND_US);

    if (err)
        adamnet_response_nack(true);
    else
        adamnet_response_ack(true);

    // Exactly one ACK per block: suppress the master's surplus re-poll RECEIVEs.
    _receive_acked = true;
}

void adamDisk::adamnet_control_send_block_num()
{
    uint8_t x[8];

    for (uint16_t i = 0; i < 5; i++)
        x[i] = adamnet_recv();

    adamnet_recv(); // CK -- consume the trailing checksum so the packet is fully read

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    if (_media == nullptr)
        return;

    if (_media->num_blocks() < 0x10000UL) // Smaller than 64MB?
    {
        blockNum &= 0xFFFF; // Mask off upper bits
    }

    if (blockNum == 0xFACE)
    {
        _media->format(NULL);
    }

    SYSTEM_BUS.start_time=GET_TIMESTAMP();

    adamnet_response_ack();

    Debug_printf("BLOCK: %lu\n", blockNum);

    int64_t now = GET_TIMESTAMP();
    // Each new block# starts unclassified; a following RECEIVE marks it a read.
    _seek_is_read = false;
    // New block operation: allow exactly one ACK for its RECEIVE sequence again.
    _receive_acked = false;

    bool already_cached = (_media->_media_last_block == blockNum);
    if (blockNum != _seek_block ||
        (now - _last_blocknum_us > ADAMNET_DISK_SEEK_NEWOP_US && !already_cached))
    {
        _seek_block = blockNum;
        _seek_deadline = now + ADAMNET_DISK_SEEK_US;
    }
    _last_blocknum_us = now;
}

void adamDisk::adamnet_control_send_block_data()
{
    if (_media == nullptr)
        return;

    adamnet_recv_buffer(_media->_media_blockbuff, 1024);
    adamnet_recv(); // CK -- consume the trailing checksum so the packet is fully read
    SYSTEM_BUS.start_time = GET_TIMESTAMP();
    adamnet_response_ack();

    if (is_config_device)
    {
        Debug_printf("Refusing spurious write to read-only config device, block %lu\n", blockNum);
        blockNum = 0xFFFFFFFF;
        _media->_media_last_block = 0xFFFFFFFE;
        return;
    }

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
    if (_media != nullptr && _seek_is_read && GET_TIMESTAMP() < _seek_deadline)
    {
        SYSTEM_BUS.stall_silent = true;
        return;
    }

    if (_media == nullptr)
        status_response.status = 0x40 | STATUS_NO_MEDIA;
    else
        status_response.status = 0x40 | _media->_media_controller_status;

#ifdef ESP_PLATFORM
    // Real bus only: answer only inside the master's status window.
    if (GET_TIMESTAMP() - SYSTEM_BUS.start_time >= 300)
        return;
#endif
    virtualDevice::adamnet_response_status();
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

    SYSTEM_BUS.wait_turnaround(ADAMNET_DISK_SEND_TURNAROUND_US);
    SYSTEM_BUS.quiet_rx_for_send(true);
    adamnet_send_buffer(b, sizeof(b));
    SYSTEM_BUS.quiet_rx_for_send(false);
}

void adamDisk::adamnet_process(const FujiAdamPacket &packet)
{
    switch (packet.type())
    {
    case APT::MN_RESET:
        reset();
        break;
    case APT::MN_STATUS:
        adamnet_control_status();
        break;
    case APT::MN_CLR:
        adamnet_control_clr();
        break;
    case APT::MN_RECEIVE:
        adamnet_control_receive();
        break;
    case APT::MN_SEND:
        adamnet_control_send();
        break;
    case APT::MN_READY:
        adamnet_control_ready();
        break;
    default:
        break;
    }

}

#endif /* BUILD_ADAM */
