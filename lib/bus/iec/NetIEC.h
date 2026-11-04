#ifndef NETIEC_H
#define NETIEC_H

#include "IECBusHandler.h"
#include "IOChannel.h"
#include "global_types.h"

#include <cstdint>

// NetIEC carries IEC bus transactions from an emulator's bus master over a byte stream.
// Each request is a NetIECHeader plus payload and gets exactly one reply whose command
// has NETIEC_REPLY set.
enum class NetIECCommand : uint8_t
{
    HELLO = 0x01, // [version][name]          -> [version][u32le device mask][name]
    ATN   = 0x02, // [primary][secondary]     -> [NetIECAtnStatus]
    WRITE = 0x03, // [flags][data...]         -> [u16le bytes accepted]
    READ  = 0x04, // [u16le max bytes]        -> [flags][data...]
    RESET = 0x05, // []                       -> []
};

enum class NetIECAtnStatus : uint8_t
{
    ACKNOWLEDGED = 0x00,
    NOT_PRESENT  = 0x01,
};

constexpr uint8_t NETIEC_VERSION     = 1;
constexpr uint8_t NETIEC_REPLY       = 0x80;
constexpr uint8_t NETIEC_UNSUPPORTED = 0xFF; // reply to an unknown command
constexpr uint8_t NETIEC_FLAG_EOI    = 0x01; // WRITE: last byte is EOI; READ: transfer ended

struct NetIECHeader
{
    uint8_t command;
    u16le_t length;
} __attribute__((packed));

struct NetIECHelloReply
{
    uint8_t version;
    u32le_t deviceMask;
} __attribute__((packed));

class NetIEC
{
public:
    explicit NetIEC(IECBusHandler &bus) : _bus(bus) {}

    void begin(IOChannel *port);
    void service();

private:
    IECBusHandler &_bus;
    IOChannel *_port = nullptr;

    NetIECHeader _request{};
    ByteBuffer _payload;
    ByteBuffer _reply;
    // WRITE and READ wait across service() calls while the device reports busy
    bool _transferPending = false;
    size_t _transferred = 0;

    success_is_true receiveRequest();
    void startRequest();

    void hello();
    void atn();
    void startWrite();
    void continueWrite();
    void startRead();
    void continueRead();
    void reset();

    void sendReply(uint8_t command);
};

#endif /* NETIEC_H */
