#ifdef BUILD_RS232

#include "rs232.h"
#include "FujiBusPacket.h"

#include "../../include/debug.h"

#include "rs232/rs232Fuji.h"
#include "rs232/rs232Network.h"
#include "modem.h"
#include "siocpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"
#include "fuji_endian.h"

#ifdef ESP_PLATFORM
#define SERIAL_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SERIAL_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

#if FUJINET_OVER_USB
// run USB just above the WiFi task (prio 23) until association, per drivewire.cpp
#define RS232_USB_BOOT_PRIORITY 24
#define RS232_USB_RUN_PRIORITY  20
#endif

// Helper functions outside the class defintions

// Calculate 8-bit checksum
uint8_t rs232_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

void systemBus::transaction_accept(transState_t expectMoreData)
{
    assert(_transaction_state == TRANS_STATE::INVALID);
    _activePacketDataPosition = 0;
    _transaction_state = expectMoreData;
}

void systemBus::transaction_success()
{
    assert(_transaction_state == TRANS_STATE::NO_GET || _transaction_state == TRANS_STATE::DID_GET);
    sendReplyPacket(_activeDev->id(), true, nullptr, 0);
    _transaction_state = TRANS_STATE::INVALID;
}

void systemBus::transaction_error()
{
    sendReplyPacket(_activeDev->id(), false, nullptr, 0);
    _transaction_state = TRANS_STATE::INVALID;
}

success_is_true systemBus::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    _transaction_state = TRANS_STATE::DID_GET;

    if (!len)
        RETURN_SUCCESS_AS_TRUE();

    auto optional_data = _activePacket->data();
    if (!optional_data.has_value())
        RETURN_ERROR_AS_FALSE();
    size_t avail = optional_data.value().size() - _activePacketDataPosition;
    avail = std::min(avail, (size_t) len);
    memcpy(data, optional_data.value().data() + _activePacketDataPosition, avail);
    _activePacketDataPosition += avail;

    if (avail != len)
        RETURN_ERROR_AS_FALSE();
    RETURN_SUCCESS_AS_TRUE();
}

void systemBus::transaction_send(const void *data, size_t len, bool is_error)
{
    assert(_transaction_state == TRANS_STATE::NO_GET);
    sendReplyPacket(_activeDev->id(), !is_error, data, len);
    _transaction_state = TRANS_STATE::INVALID;
}

// Read and process a command frame from RS232
void systemBus::_rs232_process_cmd()
{
    Debug_printf("rs232_process_cmd()\n");

    ByteBuffer packet;
    int val, count;

    while (1)
    {
        val = _port->read();
        if (val < 0 || val == SLIP_END)
            break;
        packet.push_back(val);
    }
    if (packet.size())
        _modemDev->tx(packet);
    if (val < 0)
        return;

    auto tempFrame = readBusPacket(val);
    if (!tempFrame)
    {
        Debug_printv("packet fail");
        return;
    }

    // Turn on the RS232 indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_printf("\nCF: dev:%02x cmd:%02x dlen:%d\n",
                 (uint8_t) tempFrame->device(), (uint8_t) tempFrame->command(),
                 tempFrame->data() ? tempFrame->data()->size() : -1);


    _activePacket = tempFrame.get();
    _activeDev = _daisyChain.deviceWithFujiID(tempFrame->device());
    if (_activeDev != nullptr)
        _activeDev->rs232_process(*tempFrame);

    fnLedManager.set(eLed::LED_BUS, false);
}

/*
 Primary RS232 serivce loop:
 * If MOTOR line asserted, hand RS232 processing over to the TAPE device
 * If CMD line asserted, try reading CMD frame and sending it to appropriate device
 * If CMD line not asserted but MODEM is active, give it a chance to read incoming data
 * Throw out stray input on RS232 if neither of the above two are true
 * Give NETWORK devices an opportunity to signal available data
 */
void systemBus::service()
{
#if FUJINET_OVER_USB
    // one-shot: drop USB back to normal priority once WiFi is up
    if (_usb_boot_priority && fnWiFi.connected())
    {
        _serial.setServicePriority(RS232_USB_RUN_PRIORITY);
        _usb_boot_priority = false;
    }
#endif

    // Check for any messages in our queue (this should always happen, even if any other special
    // modes disrupt normal RS232 handling - should probably make a separate task for this)
    /*_rs232_process_queue();*/

    if (_cpmDev != nullptr && _cpmDev->cpmActive)
    {
        _cpmDev->rs232_handle_cpm();
        return; // break!
    }

    if (_port->available())
    {
        _rs232_process_cmd();
    }
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr /*&& _modemDev->modemActive*/ && Config.get_modem_enabled())
    {
        _modemDev->rs232_handle_modem();
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (_netDev[i] != nullptr)
            _netDev[i]->rs232_poll_interrupt();
    }
}

// Setup RS232 bus
void systemBus::setup()
{
    // shutdown() latches this true; left set across an in-process restart,
    // TNFS's poll loop bails out on its first check and fakes success.
    shuttingDown = false;

    Debug_printf("RS232 SETUP: Baud rate: %u\n",Config.get_serial_baud());

    // Set up UART
#ifndef FUJINET_OVER_USB
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
#if FUJINET_OVER_USB
        _serial.begin();
#else /* ! FUJINET_OVER_USB */
        _serial.begin(ChannelConfig()
                      .baud(Config.get_serial_baud())
                      .readTimeout(200)
                      .deviceID(SERIAL_DEVICE)
                      );
#endif /* FUJINET_OVER_USB */
        _port = &_serial;
    }

#else /* FUJINET_OVER_USB */
#ifdef PINMAP_FUJIVERSAL_INTV
    // VID-only: any Minty build (PID varies with MSC), still rejects BOOTSEL
    _serial.setExpectedDevice(0xCafe, 0);
#endif
    _serial.setServicePriority(RS232_USB_BOOT_PRIORITY);
    _usb_boot_priority = true;
    _serial.begin();
    _port = &_serial;
#endif /* FUJINET_OVER_USB */

    Debug_println("RS232 Setup Flush");
    _port->discardInput();
}

// Add device to RS232 bus
void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    if (device_id == FUJI_DEVICEID::FUJINET)
    {
        _fujiDev = dynamic_cast<rs232Fuji *>(pDevice);
    }
    else if (device_id == FUJI_DEVICEID::SERIAL)
    {
        _modemDev = (rs232Modem *)pDevice;
    }
    else if (device_id >= FUJI_DEVICEID::NETWORK && device_id <= FUJI_DEVICEID::NETWORK_LAST)
    {
        _netDev[device_id - FUJI_DEVICEID::NETWORK] = (rs232Network *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID::MIDI)
    {
        _streamDev = (rs232NetStream *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID::CPM)
    {
        _cpmDev = (rs232CPM *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID::PRINTER)
    {
        _printerdev = (rs232Printer *)pDevice;
    }

    _daisyChain.addDevice(pDevice, device_id);
}

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n", (uint8_t) devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

int systemBus::getBaudrate()
{
    return _rs232Baud;
}

void systemBus::setBaudrate(int baud)
{
    if (_rs232Baud == baud)
    {
        Debug_printf("Baudrate already at %d - nothing to do\n", baud);
        return;
    }

    Debug_printf("Changing baudrate from %d to %d\n", _rs232Baud, baud);
    _rs232Baud = baud;
    _serial.setBaudrate(baud);
}

std::unique_ptr<FujiBusPacket> systemBus::readBusPacket(int first)
{
    ByteBuffer packet;
    int count = 0;

    // Define the logic once in a local lambda
    auto processByte = [&](int val) -> bool
    {
        if (val < 0)
            return false;
        packet.push_back(static_cast<uint8_t>(val));
        if (val == SLIP_END)
            count++;
        return true;
    };

    processByte(first);

    while (count < 2)
    {
        if (!processByte(_port->read()))
            break;
    }

#ifdef DEBUG_RAW_PACKET
    Debug_printv("Received %d:\n%s", packet.size(),
                 util_hexdump(packet.data(), packet.size()).c_str());
#endif // DEBUG_RAW_PACKET
    return FujiBusPacket::fromSerialized(packet);
}

void systemBus::writeBusPacket(FujiBusPacket &packet)
{
    ByteBuffer encoded = packet.serialize();
    _port->write(encoded.data(), encoded.size());
#ifdef DEBUG_RAW_PACKET
    Debug_printv("Sent %d:\n%s", encoded.size(),
                 util_hexdump(encoded.data(), encoded.size()).c_str());
#endif // DEBUG_RAW_PACKET
    return;
}

void systemBus::sendReplyPacket(fujiDeviceID_t source, bool ack, const void *data, size_t length)
{
    // FIXME - check to make sure this wasn't through a bus call
    if (source == _modemDev->id())
    {
        _port->write(data, length);
        return;
    }

    ByteBuffer bb;

    if (ack && data)
    {
        const uint8_t *start = static_cast<const uint8_t*>(data);
        bb.assign(start, start + length);
    }

    FujiBusPacket packet(source, ack ? CMD::FUJI_ACK : CMD::FUJI_NAK, bb);
    writeBusPacket(packet);
    return;
}

fujiDeviceID_t virtualDevice::id()
{
    return SYSTEM_BUS.fujiIDForDevice(this);
}

#endif /* BUILD_RS232 */
