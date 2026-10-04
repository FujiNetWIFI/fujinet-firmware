#ifndef HOTSYNC_PADP_TRANSPORT_H
#define HOTSYNC_PADP_TRANSPORT_H

// Packet Assembly/Disassembly Protocol over SLP, with the CMP handshake that
// opens a serial HotSync. Used for a cradle on a UART and for emulators that
// bridge their serial port to TCP.
// Reference: palm-sync src/protocols/padp-protocol.ts and cmp-protocol.ts

#include "DlpTransport.h"
#include "HotSyncLink.h"
#include "Slp.h"

#include <deque>

constexpr uint32_t CMP_INITIAL_BAUD_RATE = 9600;
constexpr uint32_t CMP_MAX_BAUD_RATE = 115200;

class PadpTransport : public DlpTransport
{
public:
    PadpTransport(HotSyncLink &link, uint32_t max_baud_rate = CMP_MAX_BAUD_RATE);

    // accept() is wait_for_wakeup() then answer_wakeup(). A cradle that only
    // listens for a while bounds the first, never the second.
    success_is_true accept() override;
    success_is_true wait_for_wakeup();
    success_is_true answer_wakeup();
    success_is_true send(const ByteBuffer &message) override;
    success_is_true receive(ByteBuffer &message, uint32_t timeout_ms) override;

    uint32_t baud_rate() const { return _baud_rate; }

private:
    struct Fragment {
        uint8_t type;
        uint8_t flags;
        uint32_t size_or_offset;
        ByteBuffer data;
    };

    success_is_true send_fragment(uint8_t xid, const Fragment &fragment);
    success_is_true wait_for_ack(uint8_t xid, uint32_t size_or_offset);
    success_is_true handle_incoming(const SlpDatagram &datagram, uint8_t awaited_xid,
                                    uint32_t awaited_offset, bool &acked);
    bool is_duplicate(const SlpDatagram &datagram) const;
    success_is_true accept_data(const SlpDatagram &datagram, const Fragment &fragment);
    void send_ack(const SlpDatagram &datagram, const Fragment &fragment);
    ByteBuffer encode_fragment(const Fragment &fragment) const;
    success_is_true decode_fragment(const ByteBuffer &payload, Fragment &out) const;
    uint8_t next_xid();

    HotSyncLink &_link;
    SlpReader _reader;
    uint32_t _max_baud_rate;
    uint32_t _baud_rate = CMP_INITIAL_BAUD_RATE;
    uint8_t _next_xid = 1;
    uint32_t _wakeup_baud_rate = 0;

    ByteBuffer _assembling;
    uint32_t _assembling_size = 0;
    bool _assembling_active = false;
    uint8_t _last_acked_xid = 0;
    ByteBuffer _last_acked_payload;
    std::deque<ByteBuffer> _received;
};

#endif // HOTSYNC_PADP_TRANSPORT_H
