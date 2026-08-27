#ifdef BUILD_LYNX

/**
 * Comlynx Functions
 */
#include "comlynx.h"
#include "netstream.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "utils.h"
#include "debug.h"

#ifdef ESP_PLATFORM
#define SERIAL_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SERIAL_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

//#define IDLE_TIME 500 // Idle tolerance in microseconds (roughly three characters at 62500 baud)


void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

bool systemBus::wait_for_idle()
{
    int64_t start, current, dur;

    // SJ notes: we really don't need to do this unless we are in netstream mode
    // Likely we want to just wait until the bus is "idle" for about 3 character times
    // which is about 0.5 ms at 62500 baud 8N1
    //
    // Check that the bus is truly idle for the whole duration, and then we can start sending?

    start = GET_TIMESTAMP();

    do {
        current = GET_TIMESTAMP();
        dur = current - start;

        // Did we get any data in the FIFO while waiting?
        if (_port->available() > 0)
            return false;

    } while (dur < COMLYNX_IDLE_TIME);

    // Must have been idle at least IDLE_TIME to get here
    return true;

    //fnSystem.yield();         // not sure if we need to do this, from old function - SJ
}

bool systemBus::netstreamActive() const
{
    return _streamDev != nullptr && _streamDev->netstreamActive;
}

void virtualDevice::comlynx_process(const FujiLynxPacket &packet)
{
    Debug_printf("comlynx_process() not implemented yet for this device.\n");
}

void systemBus::_comlynx_process_cmd()
{
    fujiDeviceID_t dev;
    u16be_t len;
    ByteBuffer buffer, payload;
    uint8_t ck;
    size_t rlen;

    buffer.resize(3, 0);
    rlen = _port->read(buffer.data(), buffer.size());
    if (rlen != buffer.size())
    {
        Debug_printf("failed to read packet header\n");
        sendNakPacket();
        return;
    }

    memcpy(&len, buffer.data() + 1, sizeof(len));
    payload.resize(len, 0);
    rlen = _port->read(payload.data(), payload.size());
    if (rlen != payload.size())
        payload.resize(rlen);
    buffer.insert(buffer.end(), payload.begin(), payload.end());
    buffer.push_back(_port->read());

    Debug_printf("Received packet\n%s", util_hexdump(buffer.data(), buffer.size()).c_str());

    auto tmpPacket = FujiLynxPacket::fromSerialized(buffer);
    if (!tmpPacket)
    {
        Debug_printf("bad packet\n");
        sendNakPacket();
        _port->discardInput();
        goto done;
    }

    sendAckPacket();

    for (auto devicep : _daisyChain)
    {
        if (tmpPacket->device() == devicep->_devnum)
        {
            _activeDev = devicep;
            _activePacket = tmpPacket.get();

            #ifdef DEBUG
            Debug_println("---");
            Debug_printf("comlynx_process_cmd - dev:%X\n", tmpPacket->device());
            #endif

            // turn on Comlynx Indicator LED
            fnLedManager.set(eLed::LED_BUS, true);
            devicep->comlynx_process(*tmpPacket);
            // turn off Comlynx Indicator LED
            fnLedManager.set(eLed::LED_BUS, false);
            assert(_transaction_state == TRANS_STATE::INVALID);
        }
    }

 done:
    _port->flushOutput();
}

void systemBus::_comlynx_process_queue()
{
}

void systemBus::service()
{
    // Handle NetStream if active
    if (_streamDev != nullptr && _streamDev->netstreamActive) {
        if (_streamDev->redeye_mode)
            _streamDev->comlynx_handle_redeye_netstream();
        else
            _streamDev->comlynx_handle_netstream();
    }
    // Process anything waiting
    else if (_port->available() > 0)
        _comlynx_process_cmd();
}

void systemBus::setup()
{
    Debug_println("COMLYNX SETUP");

    // Set up NetStream device
    //_streamDev = new lynxnetstream();

    if (Config.get_boip_enabled())
    {
        Debug_printf("RS232 SETUP: BOIP host: %s\n", Config.get_boip_host().c_str());
        _boip.begin(BoIPConfig()
                    .hostName(Config.get_boip_host())
                    .portNum(Config.get_boip_port())
                    );
        _port = &_boip;
    }
    else {
        // Set up UART
        _serial.begin(ChannelConfig()
                      .deviceID(SERIAL_DEVICE)
                      .baud(COMLYNX_BAUDRATE)
#ifdef ESP_PLATFORM
                      .parity(UART_PARITY_ODD)
                      .halfDuplex(true)
#endif /* ESP_PLATFORM */
                      );
        _port = &_serial;
    }
}

void systemBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);

    if (device_id == FUJI_DEVICEID::FUJINET)
    {
        _fujiDev = (lynxFuji *)pDevice;
    }
    else if (device_id >= FUJI_DEVICEID::NETWORK && device_id <= FUJI_DEVICEID::NETWORK_LAST)
    {
        _netDev[device_id - FUJI_DEVICEID::NETWORK] = (lynxNetwork*)pDevice;
    }
    else if (device_id == FUJI_DEVICEID::PRINTER)
    {
        _printerDev = (lynxPrinter *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID::MIDI)
    {
        _streamDev = (lynxNetStream *)pDevice;
    }

    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);
}

void systemBus::remDevice(virtualDevice *pDevice)
{
    _daisyChain.remove(pDevice);
}

void systemBus::remDevice(fujiDeviceID_t device_id)
{
}

int systemBus::numDevices()
{
      int i = 0;
    //__BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    //__END_IGNORE_UNUSEDVARS
}

void systemBus::changeDeviceId(virtualDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = (fujiDeviceID_t) device_id;
    }
}

virtualDevice *systemBus::deviceById(fujiDeviceID_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void systemBus::reset()
{
    for (auto devicep : _daisyChain)
        devicep->reset();
}

void systemBus::enableDevice(fujiDeviceID_t device_id)
{
}

void systemBus::disableDevice(fujiDeviceID_t device_id)
{
}

void systemBus::setStreamHost(const char *hostname, int port)
{
    setStreamHostWithOptions(hostname, port, 0, false, true);
}

void systemBus::setStreamHostWithOptions(const char *hostname, int port, int mode, bool register_enabled, bool redeye_enabled)
{
    // Turn off if hostname is STOP
    if (hostname != nullptr && !strcmp(hostname, "STOP"))
    {
        if (_streamDev->netstreamActive)
            _streamDev->comlynx_disable_netstream();

        return;
    }

    if (hostname != nullptr && hostname[0] != '\0')
    {
        // Try to resolve the hostname and store that so we don't have to keep looking it up
        _streamDev->netstream_host_ip = get_ip4_addr_by_name(hostname);
        //_streamDev->netstream_host_ip = IPADDR_NONE;

        if (_streamDev->netstream_host_ip == IPADDR_NONE)
        {
            Debug_printf("Failed to resolve hostname \"%s\"\n", hostname);
        }
    }
    else
    {
        _streamDev->netstream_host_ip = IPADDR_NONE;
    }

    if (port > 0 && port <= 65535)
    {
        _streamDev->netstream_port = port;
    }
    else
    {
        _streamDev->netstream_port = 5004;
        Debug_printf("netstream port not provided or invalid (%d), setting to 5004\n", port);
    }

    _streamDev->netstreamMode = (mode == 0)
        ? lynxNetStream::NetStreamMode::UDP
        : lynxNetStream::NetStreamMode::TCP;
    _streamDev->netstreamRegisterEnabled = register_enabled;
    _streamDev->redeye_mode = redeye_enabled;

    // Restart NetStream mode if needed
    if (_streamDev->netstreamActive) {
        _streamDev->comlynx_disable_netstream();
        _streamDev->comlynx_disable_redeye();
    }
    if (_streamDev->netstream_host_ip != IPADDR_NONE) {
        _streamDev->comlynx_enable_netstream();
        if (_streamDev->redeye_mode)
            _streamDev->comlynx_enable_redeye();
    }
}

void systemBus::setRedeyeMode(bool enable)
{
    Debug_printf("setRedeyeMode, %d\n", enable);
    _streamDev->redeye_mode = enable;
    _streamDev->redeye_reset_game();
}

void systemBus::setRedeyeGameRemap(uint32_t remap)
{
    Debug_printf("setRedeyeGameRemap, %d\n", (int) remap);

    // handle pure updstream games
    if ((remap >> 8) == 0xE1) {
        _streamDev->redeye_mode = false;           // turn off redeye
        _streamDev->game.game_id = remap;          // set game, since we can't detect it
        _streamDev->game.remap_game_id = remap;
        return;
    }

    // handle redeye game that need remapping
    _streamDev->redeye_mode = true;
    if (remap != 0xFFFF) {
        _streamDev->game.remap_game_id = remap;
    }
    else {
        _streamDev->game.remap_game_id = 0;
    }
}

void systemBus::transaction_accept(transState_t expectMoreData)
{
    assert(_transaction_state == TRANS_STATE::INVALID);
    _transaction_state = expectMoreData;
}

void systemBus::transaction_success()
{
    assert(_transaction_state == TRANS_STATE::NO_GET || _transaction_state == TRANS_STATE::DID_GET);
    Debug_println("transaction_complete - sent ACK");
    sendAckPacket();
    _transaction_state = TRANS_STATE::INVALID;
}

void systemBus::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    sendNakPacket();

    // throw away any waiting bytes
    _port->discardInput();
    _transaction_state = TRANS_STATE::INVALID;
}

success_is_true systemBus::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    _transaction_state = TRANS_STATE::DID_GET;
    auto to_copy = std::min<size_t>(len, _activePacket->data()->size());
    std::copy(_activePacket->data()->begin(), _activePacket->data()->begin() + to_copy,
              static_cast<uint8_t *>(data));
    RETURN_SUCCESS_IF(to_copy != 0);
}

void systemBus::transaction_send(const void *data, size_t len, bool err)
{
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);

    assert(_transaction_state == TRANS_STATE::NO_GET);

    // send all data back to Lynx
    FujiLynxPacket packet(CMD::FUJI_SEND_RESPONSE, ByteBuffer(ptr, ptr + len));
    writeBusPacket(packet);
    _transaction_state = TRANS_STATE::INVALID;
    return;
}

void systemBus::writeBusPacket(const FujiLynxPacket &packet)
{
    auto encoded = packet.serialize();
#ifdef DEBUG_RAW_PACKET
    Debug_printf("Sending reply\n%s", util_hexdump(encoded.data(), encoded.size()).c_str());
#endif // DEBUG_RAW_PACKET
    _port->write(encoded.data(), encoded.size());

    if (packet.command() == CMD::FUJI_SEND_RESPONSE)
    {
        // get ACK or NACK from Lynx, we're ignoring currently
        fujiCommandID_t r = (fujiCommandID_t) _port->read();
#ifdef DEBUG
        if (r == CMD::FUJI_ACK)
            Debug_println("writeBusPacket - Lynx ACKed");
        else
            Debug_printf("writeBusPacket - Lynx NAKed 0x%02x\n", (uint8_t) r);
#endif
    }

    return;
}

void systemBus::sendAckPacket()
{
    writeBusPacket(FujiLynxPacket(CMD::FUJI_ACK));
}

void systemBus::sendNakPacket()
{
    writeBusPacket(FujiLynxPacket(CMD::FUJI_NAK));
}

void systemBus::change_baud(int32_t baud)
{
    _port->flushOutput();
    if (_port == &_serial)
    {
        _serial.begin(ChannelConfig()
                      .deviceID(SERIAL_DEVICE)
                      .baud(baud)
#ifdef ESP_PLATFORM
                      .parity(UART_PARITY_ODD)
#endif /* ESP_PLATFORM */
                      );
    }

#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(10));
#endif /* ESP_PLATFORM */

    //uart_set_baudrate(FN_UART_BUS, baud);
    //_port->setBaudrate(baud);
}

#endif /* BUILD_LYNX */
