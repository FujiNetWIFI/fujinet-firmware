#ifdef BUILD_IEC

#include "virtualDevice.h"
#include "bus.h"
#include "debug.h"
#include "utils.h"

void virtualDevice::talk(uint8_t secondary)
{
#ifdef UNUSED
    Debug_printv("secondary=%d\n", secondary);
#endif /* UNUSED */
    // only talk on channel 15
    if ((secondary & 0x0F) == 15)
        _state = DEVICE_TALK;
}

void virtualDevice::listen(uint8_t secondary)
{
#ifdef UNUSED
    Debug_printv("secondary=%d\n", secondary);
#endif /* UNUSED */
    // only listen on channel 15
    if ((secondary & 0x0F) == 15)
    {
        _state = DEVICE_LISTEN;
        _payload.clear();
    }
}

void virtualDevice::untalk()
{
#ifdef UNUSED
    Debug_printv();
#endif /* UNUSED */
    _state = DEVICE_IDLE;
}

void virtualDevice::unlisten()
{
#ifdef UNUSED
    Debug_printv();
#endif /* UNUSED */
    if (_state == DEVICE_LISTEN)
        _state = DEVICE_ACTIVE;
}

int8_t virtualDevice::canWrite()
{
#ifdef UNUSED
    Debug_printv();
#endif /* UNUSED */
    return _state == DEVICE_LISTEN;
}

int8_t virtualDevice::canRead()
{
#ifdef UNUSED
    Debug_printv();
#endif /* UNUSED */
    return SYSTEM_BUS.iecCanRead(_state);
}

void virtualDevice::write(uint8_t data, bool eoi)
{
#ifdef UNUSED
    Debug_printv("data=0x%02x eoi=%d\n", data, eoi);
#endif /* UNUSED */
    _payload.push_back(data);
}

uint8_t virtualDevice::read()
{
#ifdef UNUSED
    Debug_printv();
#endif /* UNUSED */
    return SYSTEM_BUS.iecRead();
}

void virtualDevice::task()
{
    // this gets called whenever the IEC bus is NOT in a time-sensitive state.
    // Any possibly time-comsuming tasks should be processed within here.

    // first call the underlying class task function
    IECDevice::task();

    if (_state == DEVICE_ACTIVE)
    {
        if (!_payload.empty())
            process_cmd();
        _state = DEVICE_IDLE;
    }
}

void virtualDevice::process_cmd()
{
    if (!_activePacket)
    {
        if (_payload.size() != 2
            || (_payload[0] == OPCODE_NO_PAYLOAD && _payload[0] == OPCODE_HAS_PAYLOAD))
            return;

        Debug_printv("RAW command:\n%s",
                     util_hexdump((uint8_t *) _payload.data(), _payload.size()).c_str());

        auto packet = std::make_unique<FujiIECPacket>(m_devnr, (fujiCommandID_t) _payload[1]);
        _activePacket = std::move(packet);

        if (_payload[0] == OPCODE_HAS_PAYLOAD)
            return;
    }
    else
    {
        Debug_printf("RAW payload len=%d\n%s", _payload.size(),
                     util_hexdump((uint8_t *) _payload.data(), _payload.size()).c_str());
        _activePacket->setPayload(_payload);
    }

    SYSTEM_BUS._activePacket = _activePacket.get();
    processCommand(*_activePacket);
    _activePacket = NULL;
}

#ifdef UNUSED
bool virtualDevice::open(uint8_t channel, const char *name)
{
    Debug_printv("channel=%d name=\"%s\"", channel, name);
    return false;
}

void virtualDevice::close(uint8_t channel)
{
    Debug_printv("channel=%d", channel);
}

uint8_t virtualDevice::read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi)
{
    Debug_printv("channel=%d len=%d eoi=%d", channel, bufferSize, eoi);
    return 0;
}

uint8_t virtualDevice::write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi)
{
    Debug_printv("channel=%d len=%d eoi=%d", channel, bufferSize, eoi);
    return 0;
}
#endif /* UNUSED */

#endif /* BUILD_IEC */
