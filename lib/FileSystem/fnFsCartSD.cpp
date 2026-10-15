#include "fnFsCartSD.h"

#if defined(ESP_PLATFORM) && defined(HAS_CART_SD_BRIDGE)

#include <cstring>
#include <cerrno>
#include <cstdio>

#include <esp_timer.h>

#include "../../include/debug.h"
#include "../bus/rs232/rs232.h"

// Matches the RP2040 side's own cap (pico/intellivision/firmware/src/
// cartfs_bridge.c, CARTFS_READ_CHUNK) -- one CARTFS_FILE_READ round trip
// never asks for more than this. Bounded by the RP2040's 384-byte
// fujibus_build_request() packet buffer, so 512 does not fit.
#define CARTFS_READ_CHUNK 256

// How long to wait for the cart's reply. A single readBusPacket() gives up
// after IOChannel's 100ms per-read timeout, which a large OPEN_DIRECTORY
// scan on the cart can exceed -- so cartfs_transact() keeps re-reading
// until this deadline instead.
#define CARTFS_REPLY_TIMEOUT_MS 2000

FileSystemCartSD fnCartSD;

// ---------------------------------------------------------------------------
// cartfs_transact: sends one CARTFS request and waits for its reply.
//
// Every request payload starts with a 1-byte sequence tag, which the cart
// echoes as the first byte of its reply (ACK or NAK). A reply that arrives
// after we gave up on it would otherwise be taken as the answer to the next
// request, and every reply after that would be off by one -- so anything
// with the wrong tag, or not from CARTFS, is discarded and we keep reading.
//
// Returns true on ACK; *out (if given) gets the reply payload minus the tag.
// ---------------------------------------------------------------------------
static bool cartfs_transact(fujiCommandID_t command, const std::string &payload = std::string(),
                            ByteBuffer *out = nullptr)
{
    static uint8_t s_seq = 0;
    uint8_t tag = ++s_seq;

    std::string tagged(1, (char)tag);
    tagged += payload;
    FujiBusPacket request(FUJI_DEVICEID::CARTFS, command, tagged);
    SYSTEM_BUS.writeBusPacket(request);

    int64_t deadline = esp_timer_get_time() + (int64_t)CARTFS_REPLY_TIMEOUT_MS * 1000;
    while (esp_timer_get_time() < deadline)
    {
        auto reply = SYSTEM_BUS.readBusPacket();
        if (!reply)
            continue; // nothing yet (or a bad frame) -- keep waiting

        const auto &data = reply->data();
        if (reply->device() != FUJI_DEVICEID::CARTFS || !data || data->empty() ||
            (*data)[0] != tag)
        {
            Debug_printf("FileSystemCartSD: discarding stale/unexpected reply "
                         "(dev=0x%02x tag=%d, want tag=%u)\r\n",
                         (uint8_t)reply->device(),
                         (data && !data->empty()) ? (*data)[0] : -1, tag);
            continue;
        }

        if (reply->command() != CMD::FUJI_ACK)
        {
            Debug_printf("FileSystemCartSD: cart MCU NAKed cmd 0x%02x\r\n", (uint8_t)command);
            return false;
        }
        if (out != nullptr)
            out->assign(data->begin() + 1, data->end());
        return true;
    }

    Debug_printf("FileSystemCartSD: no reply from cart MCU to cmd 0x%02x\r\n", (uint8_t)command);
    return false;
}

static void put_le16(uint8_t *out, uint16_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFF);
    out[1] = (uint8_t)((v >> 8) & 0xFF);
    out[2] = (uint8_t)((v >> 16) & 0xFF);
    out[3] = (uint8_t)((v >> 24) & 0xFF);
}

// ---------------------------------------------------------------------------
// FileHandlerCartFS
// ---------------------------------------------------------------------------

FileHandlerCartFS::FileHandlerCartFS(long size, uint8_t handle)
    : _size(size), _position(0), _handle(handle)
{
}

FileHandlerCartFS::~FileHandlerCartFS()
{
    if (!_closed)
        close(false);
}

size_t FileHandlerCartFS::read(void *ptr, size_t size, size_t n)
{
    if (size == 0 || n == 0 || ptr == nullptr)
        return 0;

    size_t want = size * n;
    size_t got = 0;
    uint8_t *out = (uint8_t *)ptr;

    while (got < want)
    {
        uint32_t chunk = (uint32_t)(want - got);
        if (chunk > CARTFS_READ_CHUNK)
            chunk = CARTFS_READ_CHUNK;

        uint8_t hdr[9];
        hdr[0] = _handle;
        put_le32(&hdr[1], (uint32_t)_position);
        put_le32(&hdr[5], chunk);

        ByteBuffer data;
        if (!cartfs_transact(CMD::CARTFS_FILE_READ,
                             std::string((const char *)hdr, sizeof(hdr)), &data))
            break;

        size_t datalen = data.size();
        if (datalen == 0)
            break; // EOF
        if (datalen > chunk)
            datalen = chunk; // never overrun the caller's buffer

        memcpy(out + got, data.data(), datalen);
        got += datalen;
        _position += datalen;

        if (datalen < chunk)
            break; // short read -- EOF
    }

    return got / size;
}

size_t FileHandlerCartFS::write(const void *ptr, size_t size, size_t n)
{
    // Read-only bridge -- see fnFsCartSD.h's class banner.
    return 0;
}

int FileHandlerCartFS::seek(long int off, int whence)
{
    long new_pos;
    switch (whence)
    {
    case SEEK_SET:
        new_pos = off;
        break;
    case SEEK_CUR:
        new_pos = _position + off;
        break;
    case SEEK_END:
        new_pos = _size + off;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (new_pos < 0)
    {
        errno = EINVAL;
        return -1;
    }

    // Purely logical -- read() carries the offset on every CARTFS_FILE_READ,
    // so there is nothing to do on the wire until the next read.
    _position = new_pos;
    return 0;
}

long int FileHandlerCartFS::tell()
{
    return _position;
}

int FileHandlerCartFS::flush()
{
    return 0;
}

int FileHandlerCartFS::eof()
{
    return (_size >= 0 && _position >= _size) ? 1 : 0;
}

int FileHandlerCartFS::close(bool destroy)
{
    if (!_closed)
    {
        _closed = true;
        uint8_t handle = _handle;
        Debug_printf("FileSystemCartSD: FILE_CLOSE handle=%u\r\n", handle);
        cartfs_transact(CMD::CARTFS_FILE_CLOSE, std::string((const char *)&handle, 1));
    }
    if (destroy)
        delete this;
    return 0;
}

// ---------------------------------------------------------------------------
// FileSystemCartSD
// ---------------------------------------------------------------------------

FILE * FileSystemCartSD::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemCartSD::file_open() - ERROR! Use filehandler_open() instead\r\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler * FileSystemCartSD::filehandler_open(const char *path, const char *mode)
{
    // Read-only bridge -- refuse anything that wants to write, including
    // "r+"/"rb+" (writes would otherwise be accepted and silently lost).
    if (mode != nullptr && (mode[0] != 'r' || strpbrk(mode, "+wa") != nullptr))
    {
        Debug_printf("FileSystemCartSD: refusing write mode \"%s\" (read-only)\r\n", mode);
        return nullptr;
    }

    ByteBuffer data;
    if (!cartfs_transact(CMD::CARTFS_FILE_OPEN, std::string(path), &data))
        return nullptr;

    if (data.size() < 5)
    {
        Debug_println("FileSystemCartSD: FILE_OPEN reply missing the handle byte");
        return nullptr;
    }

    long size = (long)(data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24));
    uint8_t handle = data[4];

    Debug_printf("FileSystemCartSD: FILE_OPEN \"%s\" -> handle=%u size=%ld\r\n", path, handle, size);
    return new FileHandlerCartFS(size, handle);
}
#endif

bool FileSystemCartSD::exists(const char *path)
{
    return cartfs_transact(CMD::CARTFS_FILE_EXISTS, std::string(path));
}

bool FileSystemCartSD::is_dir(const char *path)
{
    // Not needed by FujiNet CONFIG's browse-and-boot flow (dir_read()'s
    // fsdir_entry_t already carries isDir for every entry along the way);
    // stat-by-path isn't part of the CARTFS wire protocol.
    return false;
}

success_is_true FileSystemCartSD::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    std::string payload(path ? path : "/");
    payload.push_back('\0');
    if (pattern != nullptr)
        payload += pattern;

    RETURN_SUCCESS_IF(cartfs_transact(CMD::CARTFS_OPEN_DIRECTORY, payload));
}

fsdir_entry * FileSystemCartSD::dir_read()
{
    ByteBuffer data;
    if (!cartfs_transact(CMD::CARTFS_READ_DIR_ENTRY, std::string(), &data))
        return nullptr;

    if (data.empty())
        return nullptr; // EOF

    _direntry.isDir = data[0] != 0;
    size_t namelen = data.size() - 1;
    if (namelen >= sizeof(_direntry.filename))
        namelen = sizeof(_direntry.filename) - 1;
    memcpy(_direntry.filename, data.data() + 1, namelen);
    _direntry.filename[namelen] = '\0';
    _direntry.size = 0;
    _direntry.modified_time = 0;

    return &_direntry;
}

void FileSystemCartSD::dir_close()
{
    cartfs_transact(CMD::CARTFS_CLOSE_DIRECTORY);
}

uint16_t FileSystemCartSD::dir_tell()
{
    ByteBuffer data;
    if (!cartfs_transact(CMD::CARTFS_DIR_TELL, std::string(), &data) || data.size() < 2)
        return FNFS_INVALID_DIRPOS;

    return (uint16_t)(data[0] | (data[1] << 8));
}

success_is_true FileSystemCartSD::dir_seek(uint16_t position)
{
    uint8_t payload[2];
    put_le16(payload, position);

    RETURN_SUCCESS_IF(cartfs_transact(CMD::CARTFS_DIR_SEEK,
                                      std::string((const char *)payload, sizeof(payload))));
}

#endif // ESP_PLATFORM && HAS_CART_SD_BRIDGE
