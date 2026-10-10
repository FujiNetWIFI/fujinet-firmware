#ifndef HOTSYNC_SLP_H
#define HOTSYNC_SLP_H

// Serial Link Protocol: the framing layer of serial HotSync.
// Reference: palm-sync src/protocols/slp-protocol.ts, pilot-link libpisock/slp.c

#include "HotSyncLink.h"
#include "global_types.h"

#include <cstdint>

enum class SlpType : uint8_t {
    SYSTEM = 0,
    PADP = 2,
    LOOPBACK = 3,
};

constexpr uint8_t SLP_SOCKET_DLP = 3;

struct SlpHeader {
    uint8_t signature[3];
    uint8_t dest_socket;
    uint8_t src_socket;
    uint8_t type;
    u16be_t data_length;
    uint8_t xid;
    uint8_t checksum;
} __attribute__((packed));
static_assert(sizeof(SlpHeader) == 10, "SlpHeader must be 10 bytes");

struct SlpDatagram {
    uint8_t dest_socket = SLP_SOCKET_DLP;
    uint8_t src_socket = SLP_SOCKET_DLP;
    SlpType type = SlpType::PADP;
    uint8_t xid = 0;
    ByteBuffer payload;
};

uint16_t slp_crc16(const uint8_t *data, size_t len, uint16_t crc = 0);

ByteBuffer slp_encode(const SlpDatagram &datagram);

// Pulls bytes from a link and yields whole, checksum-verified datagrams.
// Garbage before a signature and corrupt frames are skipped.
class SlpReader
{
public:
    explicit SlpReader(HotSyncLink &link) : _link(link) {}

    // Returns success when a datagram arrives before timeout_ms elapses.
    success_is_true read(SlpDatagram &out, uint32_t timeout_ms);
    bool link_closed() const { return _closed; }

private:
    success_is_true fill(uint32_t timeout_ms);
    success_is_true try_parse(SlpDatagram &out);

    HotSyncLink &_link;
    ByteBuffer _pending;
    bool _closed = false;
};

#endif // HOTSYNC_SLP_H
