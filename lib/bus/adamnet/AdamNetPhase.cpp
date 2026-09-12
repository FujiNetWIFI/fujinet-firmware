#ifdef BUILD_ADAM

#include "AdamNetPhase.h"
#include "debug.h"

inline const char* AdamNetPhase::phase_to_string(PHASE state)
{
    switch (state)
    {
    case PHASE::IDLE:         return "IDLE";
    case PHASE::NEED_ACK:     return "NEED_ACK";
    case PHASE::DID_ACK:      return "DID_ACK";
    case PHASE::DID_NAK:      return "DID_NAK";
    case PHASE::DID_IGNORE:   return "DID_IGNORE";
    case PHASE::HAVE_PAYLOAD: return "HAVE_PAYLOAD";
    case PHASE::GOT_PAYLOAD:  return "GOT_PAYLOAD";
    case PHASE::NEED_STATUS:  return "NEED_STATUS";
    case PHASE::SENT_STATUS:  return "SENT_STATUS";
    case PHASE::FEED_ME:      return "FEED_ME";
    case PHASE::BEEN_FED:     return "BEEN_FED";
    default:                  return "UNKNOWN_PHASE";
    }
}

const char* AdamNetPhase::type_to_string(adamPacketType_t type)
{
    switch (type)
    {
    case APT::MN_RESET:   return "MN_RESET";
    case APT::MN_STATUS:  return "MN_STATUS";
    case APT::MN_ACK:     return "MN_ACK";
    case APT::MN_CLR:     return "MN_CLR";
    case APT::MN_RECEIVE: return "MN_RECEIVE";
    case APT::MN_CANCEL:  return "MN_CANCEL";
    case APT::MN_SEND:    return "MN_SEND";
    case APT::MN_NAK:     return "MN_NAK";
    case APT::MN_READY:   return "MN_READY";

    case APT::NM_STATUS:  return "NM_STATUS";
    case APT::NM_ACK:     return "NM_ACK";
    case APT::NM_CANCEL:  return "NM_CANCEL";
    case APT::NM_SEND:    return "NM_SEND";
    case APT::NM_NAK:     return "NM_NAK";
    default:              return "UNKNOWN_TYPE";
    }
}

const char* AdamNetPhase::device_to_string(fujiDeviceID_t device)
{
    switch (device)
    {
    case FUJI_DEVICEID::FUJINET:      return "FUJINET";
    case FUJI_DEVICEID::KEYBOARD:     return "KEYBOARD";
    case FUJI_DEVICEID::PRINTER:      return "PRINTER";
    case FUJI_DEVICEID::DISK:         return "DISK";
    case FUJI_DEVICEID::DISK2:        return "DISK2";
    case FUJI_DEVICEID::DISK3:        return "DISK3";
    case FUJI_DEVICEID::DISK4:        return "DISK4";
    case FUJI_DEVICEID::TAPE:         return "TAPE";
    case FUJI_DEVICEID::NETWORK:      return "NETWORK";
    case FUJI_DEVICEID::NETWORK_LAST: return "NETWORK_LAST";
    default:                return "UNKNOWN_DEVICE";
    }
}

inline bool AdamNetPhase::bus_expected_state(const FujiAdamPacket &packet, PHASE actual,
                                             std::initializer_list<PHASE> expected_list)
{
    for (auto expected : expected_list)
    {
        if (actual == expected)
        {
            return true;
        }
    }

    // Print an explicit block detailing what went wrong using Debug_printf
    Debug_printf("\n=== BUS STATE ASSERTION FAILED ===\n");
    Debug_printf("Actual State:   %s (%d)\n", phase_to_string(actual), static_cast<int>(actual));

    Debug_printf("Expected one of:\n");
    for (auto expected : expected_list)
    {
        Debug_printf("  - %s\n", phase_to_string(expected));
    }

    Debug_printf("Packet device: %s (0x%02x)  type: %s (0x%02x)\n",
                 device_to_string(packet.device()), packet.device(),
                 type_to_string(packet.type()), packet.type());
    if ((packet.device() == FUJI_DEVICEID::FUJINET || packet.device() == FUJI_DEVICEID::NETWORK)
        && packet.type() == APT::MN_SEND)
        Debug_printf("Fuji command: 0x%02x\n", packet.command());

    Debug_printf("==================================\n\n");

    return false;
}

void AdamNetPhase::begin(const FujiAdamPacket &packet)
{
    _packet = &packet;

    switch (_packet->type())
    {
    case APT::MN_STATUS:  // 0x01
        // No payload, no ACK, expects AdamNetPacket reply
        _busPhase = PHASE::NEED_STATUS;
        break;

    case APT::MN_SEND:    // 0x06
        // Payload needs to be read
        _busPhase = PHASE::HAVE_PAYLOAD;
        break;

    case APT::MN_RECEIVE: // 0x04
        // Expects ACK if data is available, IGNORE if still fetching, NAK if no more data
        _busPhase = PHASE::NEED_ACK;
        break;

    case APT::MN_READY:   // 0x0d
        // Expects ACK if device is ready for a command, otherwise IGNORE (or NAK?)
        _busPhase = PHASE::NEED_ACK;
        break;

    case APT::MN_CLR:     // 0x03
        // Expects data to be sent
        _busPhase = PHASE::FEED_ME;
        break;

    case APT::MN_ACK:     // 0x02
    case APT::MN_NAK:     // 0x07
        // These can be ignored, there's no way to respond to them
        break;

    case APT::MN_RESET:   // 0x00
        // Doesn't change current phase
        break;

    case APT::MN_CANCEL:  // 0x05
        Debug_printf("PACKET device=0x%02x type=0x%02x\n", packet.device(), packet.type());
        //abort();
        break;

    default:
        //Debug_printf("PACKET invalid dest\n");
        break;
    }

    return;
}

void AdamNetPhase::finish()
{
    // Make sure device finished
    switch (_packet->type()) {
    case APT::MN_STATUS:  // 0x01
        assert(bus_expected_state(*_packet, _busPhase, {PHASE::SENT_STATUS,
                                                               PHASE::DID_IGNORE}));
        _busPhase = PHASE::IDLE;
        break;

    case APT::MN_RECEIVE: // 0x04
    case APT::MN_SEND:    // 0x06
    case APT::MN_READY:   // 0x0d
        assert(bus_expected_state(*_packet, _busPhase,
                                  {PHASE::DID_ACK, PHASE::DID_NAK,
                                   PHASE::DID_IGNORE}));
        _busPhase = PHASE::IDLE;
        break;

    case APT::MN_CLR:     // 0x03
        assert(bus_expected_state(*_packet, _busPhase, {PHASE::BEEN_FED}));
        _busPhase = PHASE::IDLE;
        break;

    case APT::MN_ACK:     // 0x02
    case APT::MN_NAK:     // 0x07
        // These can be ignored, there's no way to respond to them
        _busPhase = PHASE::IDLE;
        break;

    case APT::NM_STATUS:
    case APT::NM_ACK:
    case APT::NM_CANCEL:
    case APT::NM_SEND:
    case APT::NM_NAK:
        // Are these echoes of our own reply? We shouldn't ever see these.
        break;

    default:
        Debug_printf("UNHANDLED device=0x%02x type=0x%02x\n",
                     _packet->device(), _packet->type());
        //abort();
        break;
    }

    return;
}

void AdamNetPhase::didAck()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::NEED_ACK, PHASE::GOT_PAYLOAD}));
    _busPhase = PHASE::DID_ACK;
    return;
}

void AdamNetPhase::didNak()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::NEED_ACK, PHASE::GOT_PAYLOAD}));
    _busPhase = PHASE::DID_NAK;
    return;
}

void AdamNetPhase::didIgnore()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::NEED_ACK, PHASE::NEED_STATUS}));
    _busPhase = PHASE::DID_IGNORE;
    return;
}

void AdamNetPhase::sentStatus()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::NEED_STATUS}));
    _busPhase = PHASE::SENT_STATUS;
    return;
}

void AdamNetPhase::sentData()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::FEED_ME, PHASE::NEED_STATUS}));
    if (_busPhase == PHASE::FEED_ME)
        _busPhase = PHASE::BEEN_FED;
    return;
}

void AdamNetPhase::gotPayload()
{
    assert(bus_expected_state(*_packet, _busPhase, {PHASE::HAVE_PAYLOAD}));
    _busPhase = PHASE::GOT_PAYLOAD;
    return;
}

#endif /* BUILD_ADAM */
