#include "PadpTransport.h"

#include "../../include/debug.h"

#include <algorithm>

// PADP fragment types. TICKLE keeps a link alive and ABORT ends it.
static constexpr uint8_t PADP_DATA = 0x01;
static constexpr uint8_t PADP_ACK = 0x02;

// PADP fragment flags.
static constexpr uint8_t PADP_FLAG_FIRST = 0x80;
static constexpr uint8_t PADP_FLAG_LAST = 0x40;
static constexpr uint8_t PADP_FLAG_LONG_FORM = 0x10;

// Palm OS SDK allows 1024-byte fragments, but devices drop the link when a long
// fragment delays our ACK of their traffic, so palm-sync sends half that.
static constexpr size_t PADP_FRAGMENT_SIZE = 512;
static constexpr uint32_t PADP_ACK_WAIT_MS = 2000;
static constexpr int PADP_MAX_RETRIES = 10;

static constexpr uint8_t CMP_WAKEUP = 1;
static constexpr uint8_t CMP_INIT = 2;

// CMP INIT flags.
static constexpr uint8_t CMP_CHANGE_BAUD = 0x80;
static constexpr uint8_t CMP_LONG_FORM_PADP = 0x10;

struct CmpPacket {
    uint8_t type;
    uint8_t flags;
    uint8_t major_version;
    uint8_t minor_version;
    u16be_t reserved;
    u32be_t baud_rate;
} __attribute__((packed));
static_assert(sizeof(CmpPacket) == 10, "CmpPacket must be 10 bytes");

static constexpr uint8_t CMP_XID = 0xFF;
static constexpr uint32_t CMP_WAKEUP_WAIT_MS = 1000;

PadpTransport::PadpTransport(HotSyncLink &link, uint32_t max_baud_rate)
    : _link(link), _reader(link), _max_baud_rate(max_baud_rate)
{
}

success_is_true PadpTransport::accept()
{
    if (wait_for_wakeup().is_error())
        RETURN_ERROR_AS_FALSE();
    return answer_wakeup();
}

success_is_true PadpTransport::wait_for_wakeup()
{
    ByteBuffer message;
    if (receive(message, CMP_WAKEUP_WAIT_MS).is_error())
        RETURN_ERROR_AS_FALSE();

    CmpPacket wakeup;
    if (message.size() < sizeof(wakeup))
        RETURN_ERROR_AS_FALSE();
    std::memcpy(&wakeup, message.data(), sizeof(wakeup));
    if (wakeup.type != CMP_WAKEUP)
    {
        Debug_printf("HotSync: expected CMP WAKEUP, got type %u\r\n", wakeup.type);
        RETURN_ERROR_AS_FALSE();
    }
    _wakeup_baud_rate = wakeup.baud_rate;
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true PadpTransport::answer_wakeup()
{
    uint32_t baud = std::min<uint32_t>(_wakeup_baud_rate, _max_baud_rate);
    bool change_baud = baud != CMP_INITIAL_BAUD_RATE;

    CmpPacket init{};
    init.type = CMP_INIT;
    init.flags = static_cast<uint8_t>(CMP_LONG_FORM_PADP | (change_baud ? CMP_CHANGE_BAUD : 0));
    init.major_version = 1;
    init.minor_version = 1;
    init.reserved = 0;
    init.baud_rate = change_baud ? baud : 0;

    const uint8_t *init_bytes = reinterpret_cast<const uint8_t *>(&init);
    Fragment fragment{PADP_DATA, PADP_FLAG_FIRST | PADP_FLAG_LAST, sizeof(init),
                      ByteBuffer(init_bytes, init_bytes + sizeof(init))};
    if (send_fragment(CMP_XID, fragment).is_error())
        RETURN_ERROR_AS_FALSE();

    // Any WAKEUP retries that crossed our INIT are stale now.
    _received.clear();
    if (change_baud)
        _link.set_baud_rate(baud);
    _baud_rate = baud;
    Debug_printf("HotSync: CMP handshake complete at %lu baud\r\n",
                 static_cast<unsigned long>(baud));
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true PadpTransport::send(const ByteBuffer &message)
{
    uint8_t xid = next_xid();
    size_t offset = 0;
    do
    {
        size_t piece = std::min(PADP_FRAGMENT_SIZE, message.size() - offset);
        Fragment fragment;
        fragment.type = PADP_DATA;
        fragment.flags = static_cast<uint8_t>((offset == 0 ? PADP_FLAG_FIRST : 0) |
                                              (offset + piece == message.size() ? PADP_FLAG_LAST : 0));
        fragment.size_or_offset = offset == 0 ? message.size() : offset;
        if (message.size() > 0xFFFF)
            fragment.flags |= PADP_FLAG_LONG_FORM;
        fragment.data.assign(message.begin() + offset, message.begin() + offset + piece);
        if (send_fragment(xid, fragment).is_error())
            RETURN_ERROR_AS_FALSE();
        offset += piece;
    } while (offset < message.size());
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true PadpTransport::receive(ByteBuffer &message, uint32_t timeout_ms)
{
    while (_received.empty())
    {
        SlpDatagram datagram;
        if (_reader.read(datagram, timeout_ms).is_error())
            RETURN_ERROR_AS_FALSE();
        bool acked = false;
        handle_incoming(datagram, 0, 0, acked);
    }
    message = std::move(_received.front());
    _received.pop_front();
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true PadpTransport::send_fragment(uint8_t xid, const Fragment &fragment)
{
    SlpDatagram datagram;
    datagram.xid = xid;
    datagram.payload = encode_fragment(fragment);
    ByteBuffer frame = slp_encode(datagram);

    for (int attempt = 0; attempt < PADP_MAX_RETRIES; ++attempt)
    {
        if (_link.write(frame.data(), frame.size()) != static_cast<int>(frame.size()))
            RETURN_ERROR_AS_FALSE();
        if (wait_for_ack(xid, fragment.size_or_offset).is_success())
            RETURN_SUCCESS_AS_TRUE();
        if (_reader.link_closed())
            RETURN_ERROR_AS_FALSE();
        Debug_printf("HotSync: no ACK for xid %u, retry %d\r\n", xid, attempt + 1);
    }
    RETURN_ERROR_AS_FALSE();
}

success_is_true PadpTransport::wait_for_ack(uint8_t xid, uint32_t size_or_offset)
{
    bool acked = false;
    while (!acked)
    {
        SlpDatagram datagram;
        if (_reader.read(datagram, PADP_ACK_WAIT_MS).is_error())
            RETURN_ERROR_AS_FALSE();
        handle_incoming(datagram, xid, size_or_offset, acked);
    }
    RETURN_SUCCESS_AS_TRUE();
}

// awaited_xid 0 means no send is waiting for an ACK; xid 0 is never sent.
success_is_true PadpTransport::handle_incoming(const SlpDatagram &datagram, uint8_t awaited_xid,
                                               uint32_t awaited_offset, bool &acked)
{
    if (datagram.type != SlpType::PADP)
        RETURN_SUCCESS_AS_TRUE();

    Fragment fragment;
    if (decode_fragment(datagram.payload, fragment).is_error())
        RETURN_ERROR_AS_FALSE();

    switch (fragment.type)
    {
    case PADP_ACK:
        if (awaited_xid != 0 && datagram.xid == awaited_xid &&
            fragment.size_or_offset == awaited_offset)
            acked = true;
        RETURN_SUCCESS_AS_TRUE();
    case PADP_DATA:
        if (is_duplicate(datagram))
        {
            send_ack(datagram, fragment);
            RETURN_SUCCESS_AS_TRUE();
        }
        // A new reply carrying our xid means the ACK for our request was lost.
        if (awaited_xid != 0 && datagram.xid == awaited_xid)
            acked = true;
        return accept_data(datagram, fragment);
    default:
        // A TICKLE needs no answer; an ABORT surfaces as a timeout upstream.
        RETURN_SUCCESS_AS_TRUE();
    }
}

// The device resends a fragment verbatim when our ACK was lost.
bool PadpTransport::is_duplicate(const SlpDatagram &datagram) const
{
    return datagram.xid == _last_acked_xid && datagram.payload == _last_acked_payload;
}

success_is_true PadpTransport::accept_data(const SlpDatagram &datagram, const Fragment &fragment)
{
    if (fragment.flags & PADP_FLAG_FIRST)
    {
        _assembling.clear();
        _assembling_size = fragment.size_or_offset;
        _assembling_active = true;
    }
    else if (!_assembling_active || fragment.size_or_offset != _assembling.size())
    {
        Debug_printf("HotSync: out-of-sequence PADP fragment at %lu\r\n",
                     static_cast<unsigned long>(fragment.size_or_offset));
        _assembling_active = false;
        RETURN_ERROR_AS_FALSE();
    }

    _assembling.insert(_assembling.end(), fragment.data.begin(), fragment.data.end());
    send_ack(datagram, fragment);
    _last_acked_xid = datagram.xid;
    _last_acked_payload = datagram.payload;

    if (_assembling.size() >= _assembling_size)
    {
        _assembling_active = false;
        _assembling.resize(_assembling_size);
        _received.push_back(std::move(_assembling));
        _assembling = ByteBuffer();
    }
    RETURN_SUCCESS_AS_TRUE();
}

void PadpTransport::send_ack(const SlpDatagram &datagram, const Fragment &fragment)
{
    Fragment ack{PADP_ACK,
                 static_cast<uint8_t>(PADP_FLAG_FIRST | PADP_FLAG_LAST |
                                      (fragment.flags & PADP_FLAG_LONG_FORM)),
                 fragment.size_or_offset, ByteBuffer()};
    SlpDatagram reply;
    reply.dest_socket = datagram.src_socket;
    reply.src_socket = datagram.dest_socket;
    reply.xid = datagram.xid;
    reply.payload = encode_fragment(ack);
    ByteBuffer frame = slp_encode(reply);
    _link.write(frame.data(), frame.size());
}

ByteBuffer PadpTransport::encode_fragment(const Fragment &fragment) const
{
    ByteBuffer out{fragment.type, fragment.flags};
    if (fragment.flags & PADP_FLAG_LONG_FORM)
    {
        u32be_t size;
        size = fragment.size_or_offset;
        out.insert(out.end(), size.bytes, size.bytes + sizeof(size));
    }
    else
    {
        u16be_t size;
        size = static_cast<uint16_t>(fragment.size_or_offset);
        out.insert(out.end(), size.bytes, size.bytes + sizeof(size));
    }
    out.insert(out.end(), fragment.data.begin(), fragment.data.end());
    return out;
}

success_is_true PadpTransport::decode_fragment(const ByteBuffer &payload, Fragment &out) const
{
    if (payload.size() < 4)
        RETURN_ERROR_AS_FALSE();
    out.type = payload[0];
    out.flags = payload[1];
    size_t header_length;
    if (out.flags & PADP_FLAG_LONG_FORM)
    {
        if (payload.size() < 6)
            RETURN_ERROR_AS_FALSE();
        u32be_t size;
        std::memcpy(&size, payload.data() + 2, sizeof(size));
        out.size_or_offset = size;
        header_length = 6;
    }
    else
    {
        u16be_t size;
        std::memcpy(&size, payload.data() + 2, sizeof(size));
        out.size_or_offset = size;
        header_length = 4;
    }
    out.data.assign(payload.begin() + header_length, payload.end());
    RETURN_SUCCESS_AS_TRUE();
}

uint8_t PadpTransport::next_xid()
{
    uint8_t xid = _next_xid;
    _next_xid = (_next_xid % 0xFE) + 1;
    return xid;
}
