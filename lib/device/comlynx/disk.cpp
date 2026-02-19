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


void lynxDisk::transaction_complete()
{
    Debug_println("transaction_complete - sent ACK");
    comlynx_response_ack();
}

void lynxDisk::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    comlynx_response_nack();
    
    // throw away any waiting bytes
    while (SYSTEM_BUS.available() > 0)
        SYSTEM_BUS.read();
}
    
bool lynxDisk::transaction_get(void *data, size_t len) 
{
    size_t remaining = recvbuffer_len - (recvbuf_pos - recvbuffer);
    size_t to_copy = (len > remaining) ? remaining : len;

    memcpy(data, recvbuf_pos, to_copy);
    recvbuf_pos += to_copy;

    return len;
}


void lynxDisk::transaction_put(const void *data, size_t len, bool err)
{
    uint8_t b;

    // set response buffer
    memcpy(response, data, len);
    response_len = len;

    // send all data back to Lynx
    uint8_t ck = comlynx_checksum(response, response_len);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(ck);

    // get ACK or NACK from Lynx, we're ignoring currently
    uint8_t r = comlynx_recv();
    #ifdef DEBUG
            if (r == FUJICMD_ACK)
                Debug_println("transaction_put - Lynx ACKed");
            else
                Debug_println("transaction put - Lynx NAKed");
    #endif

    return;
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

    // clear the temporary buffer
    memset(buf, 0x00, MEDIA_BLOCK_SIZE);

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);
    }

    return false;
}

void lynxDisk::read_block(uint32_t block)
{
    if (_media == nullptr) {
        Debug_println("lynxdisk::read_block - _media is null");
        transaction_error();
        return;
    }

    blockNum = block;       // save the block (caching?)

    // Read the block
    Debug_printf("lynxdisk::read_block - block: %lu\n", block);
    if (_media->read(block, nullptr)) {         // returns TRUE if error occurred
        Debug_println("lynxdisk::read_block - media->read returned false");
        transaction_error();
    }

    transaction_put(_media->_media_blockbuff, MEDIA_BLOCK_SIZE);
}

void lynxDisk::write_block(uint32_t block)
{
    if (_media == nullptr) {
        transaction_error();
        return;
    }

    transaction_get(_media->_media_blockbuff, MEDIA_BLOCK_SIZE);
    //memcpy(_media->_media_blockbuff, data, MEDIA_BLOCK_SIZE);
    _media->write(block, false);

    Debug_printf("lynxdisk::write_block - block:%ld written\n", block);

    blockNum = 0xFFFFFFFF;
    _media->_media_last_block = 0xFFFFFFFE;
    
    transaction_complete();
}

void lynxDisk::comlynx_process()
{
    unsigned char c;
    int32_t block;

 
     // Get the entire payload from Lynx
    uint16_t len = comlynx_recv_length();
    Debug_printf("lynxDisk::comlynx_process - len: %ld, ", len);

    comlynx_recv_buffer(recvbuffer, len);
    if (comlynx_recv_ck()) {
        Debug_printf("checksum good\n");
        comlynx_response_ack();        // good checksum
    }
    else {
        Debug_printf(" checksum bad\n");
        comlynx_response_nack();       // good checksum
        return;
    }

    // get command
    transaction_get(&c, sizeof(c));
    Debug_printf("lynxDisk::comlynx_process - command: %02X\n", c);

    switch (c)
    {
    case FUJICMD_READ:
        transaction_get(&block, sizeof(block));
        read_block(block);
        break;
    case FUJICMD_WRITE:
        transaction_get(&block, sizeof(block));
        write_block(block);
        break;
    }
}


#endif /* BUILD_LYNX */
