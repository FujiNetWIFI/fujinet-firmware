#if defined(BUILD_IEC) && !defined(ESP_PLATFORM)

#include "NetIEC.h"
#include "fnSystem.h"
#include "debug.h"

#include <cstring>
#include <string>

void NetIEC::begin(IOChannel *port)
{
    _port = port;
    _transferPending = false;
}

void NetIEC::service()
{
    if (_port == nullptr)
        return;

    if (!_transferPending)
    {
        if (receiveRequest().is_error())
            return;
        startRequest();
    }

    if (!_transferPending)
        return;

    if (static_cast<NetIECCommand>(_request.command) == NetIECCommand::WRITE)
        continueWrite();
    else
        continueRead();
}

success_is_true NetIEC::receiveRequest()
{
    if (_port->available() == 0)
        RETURN_ERROR_AS_FALSE();

    if (_port->read(&_request, sizeof(_request)) != sizeof(_request))
    {
        Debug_printf("NetIEC: short request header\r\n");
        _port->discardInput();
        RETURN_ERROR_AS_FALSE();
    }

    _payload.resize(_request.length);
    if (!_payload.empty() && _port->read(_payload.data(), _payload.size()) != _payload.size())
    {
        Debug_printf("NetIEC: short payload for command 0x%02x\r\n", _request.command);
        _port->discardInput();
        RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}

void NetIEC::startRequest()
{
    switch (static_cast<NetIECCommand>(_request.command))
    {
    case NetIECCommand::HELLO:
        hello();
        break;
    case NetIECCommand::ATN:
        atn();
        break;
    case NetIECCommand::WRITE:
        startWrite();
        break;
    case NetIECCommand::READ:
        startRead();
        break;
    case NetIECCommand::RESET:
        reset();
        break;
    default:
        Debug_printf("NetIEC: unsupported command 0x%02x\r\n", _request.command);
        _reply.assign(1, _request.command);
        sendReply(NETIEC_UNSUPPORTED);
        break;
    }
}

void NetIEC::hello()
{
    NetIECHelloReply hello{};
    hello.version = NETIEC_VERSION;
    hello.deviceMask = _bus.deviceMask();

    std::string name = std::string("FujiNet-PC ") + fnSystem.get_fujinet_version(true);
    const uint8_t *fixed = reinterpret_cast<const uint8_t *>(&hello);
    _reply.assign(fixed, fixed + sizeof(hello));
    _reply.insert(_reply.end(), name.begin(), name.end());

    Debug_printf("NetIEC: bus master connected, devices 0x%08x\r\n",
                 static_cast<unsigned>(_bus.deviceMask()));
    sendReply(_request.command);
}

void NetIEC::atn()
{
    NetIECAtnStatus status = NetIECAtnStatus::NOT_PRESENT;
    if (!_payload.empty())
    {
        uint8_t secondary = _payload.size() > 1 ? _payload[1] : 0;
        if (_bus.atnCommand(_payload[0], secondary).is_success())
            status = NetIECAtnStatus::ACKNOWLEDGED;
    }

#ifdef VERBOSE_NETIEC
    Debug_printf("NetIEC ATN %02x %02x -> %s\r\n", _payload.empty() ? 0 : _payload[0],
                 _payload.size() > 1 ? _payload[1] : 0,
                 status == NetIECAtnStatus::ACKNOWLEDGED ? "ack" : "not present");
#endif
    _reply.assign(1, static_cast<uint8_t>(status));
    sendReply(_request.command);
}

void NetIEC::startWrite()
{
    _transferred = 0;
    // A WRITE without its flags byte carries no data
    if (_payload.empty())
        _payload.assign(1, 0);
    _transferPending = true;
}

void NetIEC::continueWrite()
{
    bool eoi = (_payload[0] & NETIEC_FLAG_EOI) != 0;
    size_t count = _payload.size() - 1;

    while (_transferred < count)
    {
        int8_t ready = _bus.listenerCanWrite();
        if (ready < 0)
            return; // device busy, ask again on the next service()
        if (ready == 0)
            break;

        _bus.listenerWrite(_payload[1 + _transferred], eoi && _transferred == count - 1);
        _transferred++;
    }

#ifdef VERBOSE_NETIEC
    Debug_printf("NetIEC WRITE %u/%u\r\n", static_cast<unsigned>(_transferred),
                 static_cast<unsigned>(count));
#endif
    u16le_t accepted;
    accepted = static_cast<uint16_t>(_transferred);
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&accepted);
    _reply.assign(bytes, bytes + sizeof(accepted));

    _transferPending = false;
    sendReply(_request.command);
}

void NetIEC::startRead()
{
    _reply.assign(1, 0);
    _transferred = 0;
    _transferPending = true;
}

void NetIEC::continueRead()
{
    u16le_t requested{};
    if (_payload.size() >= sizeof(requested))
        std::memcpy(&requested, _payload.data(), sizeof(requested));
    size_t maximum = static_cast<uint16_t>(requested);

    while (_transferred < maximum)
    {
        int8_t available = _bus.talkerCanRead();
        if (available < 0)
            return; // device busy, ask again on the next service()
        if (available == 0)
        {
            _reply[0] |= NETIEC_FLAG_EOI;
            break;
        }

        _reply.push_back(_bus.talkerRead());
        _transferred++;
        if (available == 1)
        {
            _reply[0] |= NETIEC_FLAG_EOI;
            break;
        }
    }

#ifdef VERBOSE_NETIEC
    Debug_printf("NetIEC READ %u/%u%s\r\n", static_cast<unsigned>(_transferred),
                 static_cast<unsigned>(maximum), (_reply[0] & NETIEC_FLAG_EOI) ? " EOI" : "");
#endif
    _transferPending = false;
    sendReply(_request.command);
}

void NetIEC::reset()
{
    _bus.busReset();
    _reply.clear();
    sendReply(_request.command);
}

void NetIEC::sendReply(uint8_t command)
{
    NetIECHeader header{};
    header.command = command | NETIEC_REPLY;
    header.length = static_cast<uint16_t>(_reply.size());

    const uint8_t *fixed = reinterpret_cast<const uint8_t *>(&header);
    ByteBuffer frame(fixed, fixed + sizeof(header));
    frame.insert(frame.end(), _reply.begin(), _reply.end());
    _port->write(frame.data(), frame.size());
}

#endif /* BUILD_IEC && !ESP_PLATFORM */
