#ifdef BUILD_LYNX

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "lz4.h"
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
                            disk_access_flags_t access_mode, mediatype_t disk_type, fujiHost *host)
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

error_is_true lynxDisk::write_blank(FILE *fileh, uint32_t numBlocks)
{
    uint8_t buf[MEDIA_BLOCK_SIZE];

    // clear the temporary buffer
    memset(buf, 0x00, MEDIA_BLOCK_SIZE);

    for (uint32_t b = 0; b < numBlocks; b++)
    {
        fwrite(buf, 1, MEDIA_BLOCK_SIZE, fileh);
    }

    RETURN_SUCCESS_AS_FALSE();
}

void lynxDisk::read_block(uint32_t block)
{
    uint8_t compressed_block[MEDIA_BLOCK_SIZE*2];           // compressed data may be larger than blocksize
    uint8_t compression_type;

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

    // Try compressing the block
    // using LZ4 for now, since Fujinet already supplied it
    // first byte sent is the compression type field, followed by data, up to 1024 bytes
    //
    // LZ4LIB_API int LZ4_compress_default(const char* src, char* dst, int srcSize, int dstCapacity);
    int c_size = LZ4_compress_default((const char *) _media->_media_blockbuff, (char *) &compressed_block[1], 1024, 1024);

    if ((c_size <= BLOCK_COMPRESS_CUTOFF) && (c_size > 0)) {
        Debug_printf("lynxdisk::read_block - sending compressed LZ4, size:%d\n", c_size);
        compressed_block[0] = BLOCK_LZ4;
        transaction_put(compressed_block, c_size+1);
    }
    else {
        Debug_printf("lynxdisk::read_block - sending raw 1024 bytes, compressed size was: %d\n", c_size);
        compressed_block[0] = BLOCK_RAW;
        memcpy(&compressed_block[1], _media->_media_blockbuff, MEDIA_BLOCK_SIZE);
        transaction_put(&compressed_block, MEDIA_BLOCK_SIZE+1);
    }
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
    Debug_printf("lynxDisk::comlynx_process - len: %ld, ", (long int)len);

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
    case DISKCMD_READ:
        transaction_get(&block, sizeof(block));
        read_block(block);
        break;
    case DISKCMD_WRITE:
        transaction_get(&block, sizeof(block));
        write_block(block);
        break;
    }
}


#endif /* BUILD_LYNX */
