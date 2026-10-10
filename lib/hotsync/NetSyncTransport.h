#ifndef HOTSYNC_NETSYNC_TRANSPORT_H
#define HOTSYNC_NETSYNC_TRANSPORT_H

// Network HotSync framing (TCP port 14238): a 6-byte header per DLP message
// and a fixed three-step handshake.
// Reference: palm-sync src/protocols/net-sync-protocol.ts and sync-connections.ts

#include "DlpTransport.h"
#include "HotSyncLink.h"

constexpr uint16_t NETSYNC_PORT = 14238;

class NetSyncTransport : public DlpTransport
{
public:
    explicit NetSyncTransport(HotSyncLink &link) : _link(link) {}

    success_is_true accept() override;
    success_is_true send(const ByteBuffer &message) override;
    success_is_true receive(ByteBuffer &message, uint32_t timeout_ms) override;

private:
    success_is_true read_exact(uint8_t *buf, size_t len, uint32_t timeout_ms);
    success_is_true expect(size_t length);

    HotSyncLink &_link;
    uint8_t _xid = 0;
};

#endif // HOTSYNC_NETSYNC_TRANSPORT_H
