#include "NetSyncTransport.h"

#include "../../include/debug.h"

struct NetSyncHeader {
    uint8_t data_type; // always 1
    uint8_t xid;
    u32be_t length;
} __attribute__((packed));
static_assert(sizeof(NetSyncHeader) == 6, "NetSyncHeader must be 6 bytes");

static constexpr uint32_t NETSYNC_HANDSHAKE_WAIT_MS = 10000;
// Messages above this are corrupt framing, not a real DLP request.
static constexpr uint32_t NETSYNC_MAX_MESSAGE = 1024 * 1024;

// The handshake payloads are opaque; these are the bytes palm-sync and
// pilot-link send, captured from Palm Desktop.
static const uint8_t HANDSHAKE_REQUEST_1_LENGTH = 22;
static const uint8_t HANDSHAKE_RESPONSE_1[] = {
    0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x24, 0xff, 0xff, 0xff, 0xff, 0x3c, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0x21, 0x04, 0x27, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t HANDSHAKE_REQUEST_2_LENGTH = 50;
static const uint8_t HANDSHAKE_RESPONSE_2[] = {
    0x13, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x20, 0xff, 0xff, 0xff, 0xff, 0x00, 0x3c, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t HANDSHAKE_REQUEST_3_LENGTH = 8;

success_is_true NetSyncTransport::accept()
{
    if (expect(HANDSHAKE_REQUEST_1_LENGTH).is_error() ||
        send(ByteBuffer(std::begin(HANDSHAKE_RESPONSE_1), std::end(HANDSHAKE_RESPONSE_1))).is_error() ||
        expect(HANDSHAKE_REQUEST_2_LENGTH).is_error() ||
        send(ByteBuffer(std::begin(HANDSHAKE_RESPONSE_2), std::end(HANDSHAKE_RESPONSE_2))).is_error() ||
        expect(HANDSHAKE_REQUEST_3_LENGTH).is_error())
    {
        Debug_printf("HotSync: NetSync handshake failed\r\n");
        RETURN_ERROR_AS_FALSE();
    }
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true NetSyncTransport::expect(size_t length)
{
    ByteBuffer message;
    RETURN_SUCCESS_IF(receive(message, NETSYNC_HANDSHAKE_WAIT_MS).is_success() &&
                      message.size() == length);
}

success_is_true NetSyncTransport::send(const ByteBuffer &message)
{
    _xid = (_xid % 0xFE) + 1;
    NetSyncHeader header;
    header.data_type = 1;
    header.xid = _xid;
    header.length = static_cast<uint32_t>(message.size());

    const uint8_t *header_bytes = reinterpret_cast<const uint8_t *>(&header);
    ByteBuffer frame(header_bytes, header_bytes + sizeof(header));
    frame.insert(frame.end(), message.begin(), message.end());
    RETURN_SUCCESS_IF(_link.write(frame.data(), frame.size()) == static_cast<int>(frame.size()));
}

success_is_true NetSyncTransport::receive(ByteBuffer &message, uint32_t timeout_ms)
{
    NetSyncHeader header;
    if (read_exact(reinterpret_cast<uint8_t *>(&header), sizeof(header), timeout_ms).is_error())
        RETURN_ERROR_AS_FALSE();
    if (header.length > NETSYNC_MAX_MESSAGE)
        RETURN_ERROR_AS_FALSE();
    message.resize(header.length);
    RETURN_SUCCESS_IF(read_exact(message.data(), message.size(), timeout_ms).is_success());
}

success_is_true NetSyncTransport::read_exact(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    size_t done = 0;
    while (done < len)
    {
        int got = _link.read(buf + done, len - done, timeout_ms);
        if (got <= 0)
            RETURN_ERROR_AS_FALSE();
        done += got;
    }
    RETURN_SUCCESS_AS_TRUE();
}
