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

error_is_true adamDisk::write_blank(FILE *fileh, uint32_t numBlocks)
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

    RETURN_SUCCESS_AS_FALSE();
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

    // Seek emulation: while the seek timer (armed at block-number time) is still
    // running, give NO response so the master keeps re-polling CONTROL.RECEIVE.
    // EOS runs its ~60Hz keyboard scan in those gaps -- off the block stream.
    if (esp_timer_get_time() < _seek_deadline)
    {
        // A CONTROL.RECEIVE arriving during the seek window confirms this is a
        // READ (a WRITE never sends RECEIVE -- it follows the block number with a
        // 1024-byte SEND). Only now do we let STATUS go silent: writes must keep
        // getting STATUS answers or the master abandons the block before its data
        // phase (observed: dev5 backup writes sent the block# then never the data).
        _seek_is_read = true;
        // Read the block HERE (during the stall, where we answer nothing anyway) so
        // the post-seek RECEIVE finds it cached and ACKs within the 300us deadline.
        // Crucially NOT at block#-time: a WRITE's block# must be ACKed instantly so
        // the master's immediately-following READY gets a prompt answer -- the 1-5ms
        // SD read there delayed it ~1.3ms and the master abandoned the write.
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
}

void adamDisk::adamnet_control_send_block_num()
{
    uint8_t x[8];

    for (uint16_t i = 0; i < 5; i++)
        x[i] = adamnet_recv();

    adamnet_recv(); // CK -- consume the trailing checksum so the packet is fully read

    blockNum = x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];

    // No media mounted on this drive (an unmounted slot, or a bus desync routed
    // the packet here): we already consumed the packet, so just bail rather than
    // dereferencing a null _media (was a hard LoadProhibited crash).
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

    SYSTEM_BUS.start_time=esp_timer_get_time();

    adamnet_response_ack();

    Debug_printf("BLOCK: %lu\n", blockNum);

    // Arm the seek for the upcoming RECEIVE, but only for a genuinely new request:
    // a different block number, or the same block a full read later (the
    // double-read's 2nd pass). NOT the master's ~12.5ms same-block retries while
    // we stall -- resetting on those would loop forever; sharing across blocks
    // would slip reads.
    int64_t now = esp_timer_get_time();
    // Each new block# starts unclassified; a following RECEIVE marks it a read.
    _seek_is_read = false;
    // Arm a fresh seek only for a genuinely new fetch. EOS's double-read asks for
    // the SAME block twice: the 1st pass seeks+reads it into the buffer, the 2nd
    // pass must deliver from cache WITHOUT re-seeking. Re-seeking the 2nd pass makes
    // it re-stall and the master then abandons the block. The old gate let the 2nd
    // pass re-arm once it arrived > NEWOP after the 1st -- fine on fast local media
    // (2nd pass lands well inside NEWOP) but broken over TNFS, where the slow read
    // pushes the 2nd pass past NEWOP for some blocks (network jitter), so only some
    // delivered. Gating on "already cached" instead is independent of read latency.
    // A genuinely new read of the same block comes after other blocks, so it is no
    // longer cached (and _seek_block has moved) and re-arms correctly.
    bool already_cached = (_media->_media_last_block == blockNum);
    if (blockNum != _seek_block ||
        (now - _last_blocknum_us > ADAMNET_DISK_SEEK_NEWOP_US && !already_cached))
    {
        _seek_block = blockNum;
        _seek_deadline = now + ADAMNET_DISK_SEEK_US;
        // The actual block read happens in the first stalled CONTROL.RECEIVE (a
        // read), NOT here -- so this block#-ACK path stays instant for writes (a
        // write follows the block# with a READY that must be answered promptly).
    }
    _last_blocknum_us = now;
}

void adamDisk::adamnet_control_send_block_data()
{
    if (_media == nullptr)
        return;

    // The 6801 master pauses up to ~220us between bytes mid-write (it services its
    // own ~60Hz keyboard-scan ISR), which exceeds the 180us inter-byte read timeout
    // and truncates this 1024-byte receive -- leaving the tail of the block buffer
    // stale (proven on the wire: short data SENDs == corrupted block tails). Widen
    // the gap tolerance for just this fixed-length receive; dataIn still returns the
    // instant all 1024 bytes are in, so a normal write isn't slowed.
    double saved_to = SYSTEM_BUS.get_read_timeout();
    SYSTEM_BUS.set_read_timeout(2.0); // ms; ride out the master's mid-stream pauses
    adamnet_recv_buffer(_media->_media_blockbuff, 1024);
    SYSTEM_BUS.set_read_timeout(saved_to);
    adamnet_recv(); // CK -- consume the trailing checksum so the packet is fully read
    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    // CONFIG / the boot image is read-only and must never be written back to.
    // A write addressed to it is spurious (the master does not write the config
    // disk), so consume the packet to stay in bus sync and ACK it, but protect
    // the image -- do not write or reopen it.
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
    // Seek emulation: a real drive is SILENT to STATUS polls while it seeks --
    // that silence is exactly how the master knows the seek is still running. It
    // keeps polling STATUS (every ~8.7ms, patiently, for as long as it takes) and
    // only restarts the READ once we answer again. If we answer "ready" mid-seek
    // (as we used to), the master restarts the read prematurely, races our seek
    // deadline, and the block stream never delivers -- proven on the wire: only
    // 1 of 8 CP/M boot blocks made it out. Stay silent until the seek expires.
    // ONLY for reads (_seek_is_read, set by a RECEIVE): a write polls STATUS
    // between its block# and its data, and silencing that makes it abandon the
    // write -- and a write has no FujiNet stream to protect from the scan anyway.
    if (_media != nullptr && _seek_is_read && esp_timer_get_time() < _seek_deadline)
    {
        SYSTEM_BUS.stall_silent = true;
        return;
    }

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

    // Pace CLR->block to a real drive's turnaround (done after building the
    // buffer so the B4 byte itself lands at the target, not the prep work).
    SYSTEM_BUS.wait_turnaround(ADAMNET_DISK_SEND_TURNAROUND_US);
    // Silence our own RX-echo interrupt storm for the duration of the 1028-byte
    // stream so it can't jitter/underrun the outgoing bytes (see quiet_rx_for_send).
    SYSTEM_BUS.quiet_rx_for_send(true);
    adamnet_send_buffer(b, sizeof(b));
    SYSTEM_BUS.quiet_rx_for_send(false);
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
