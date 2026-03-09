#ifdef BUILD_RS232

#include "rs232.h"
#include "FujiBusPacket.h"

#include "../../include/debug.h"

#include "rs232/rs232Fuji.h"
#include "rs232/network.h"
#include "udpstream.h"
#include "modem.h"
#include "siocpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"
#include "fuji_endian.h"

#ifdef ESP_PLATFORM
#define SERIAL_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SERIAL_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

// Helper functions outside the class defintions

// Calculate 8-bit checksum
uint8_t rs232_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

void virtualDevice::transaction_continue(transState_t expectMoreData)
{
    assert(_transaction_state == TRANS_STATE::INVALID);
    _transaction_state = expectMoreData;
}

void virtualDevice::transaction_complete()
{
    assert(_transaction_state == TRANS_STATE::NO_GET || _transaction_state == TRANS_STATE::DID_GET);
    SYSTEM_BUS.sendReplyPacket(_devnum, true, nullptr, 0);
    _transaction_state = TRANS_STATE::INVALID;
}

void virtualDevice::transaction_error()
{
    SYSTEM_BUS.sendReplyPacket(_devnum, false, nullptr, 0);
    _transaction_state = TRANS_STATE::INVALID;
}

bool virtualDevice::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    _transaction_state = TRANS_STATE::DID_GET;

    // FIXME - This is a terrible hack to allow devices to continue to
    // use the pattern of fetching data on their own instead of
    // upgrading them fully to work with packets.
    auto optional_data = _legacyPacketData->data();
    if (!optional_data.has_value())
        return 0;
    size_t avail = optional_data.value().size() - _legacyDataPosition;
    avail = std::min(avail, (size_t) len);
    memcpy(data, optional_data.value().data() + _legacyDataPosition, avail);
    _legacyDataPosition += avail;

    if (avail != len)
        return false;
    return true;
}

void virtualDevice::transaction_put(const void *data, size_t len, bool err)
{
    assert(_transaction_state == TRANS_STATE::NO_GET);
    SYSTEM_BUS.sendReplyPacket(_devnum, !err, data, len);
    _transaction_state = TRANS_STATE::INVALID;
}

// Read and process a command frame from RS232
void systemBus::_rs232_process_cmd()
{
    Debug_printf("rs232_process_cmd()\n");
    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting RS232 baud");
        _serial.setBaudrate(_rs232Baud);
    }

    auto tempFrame = readBusPacket();
    if (!tempFrame)
    {
        Debug_printv("packet fail");
        return;
    }

    // Turn on the RS232 indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_printf("\nCF: dev:%02x cmd:%02x dlen:%d\n",
                 tempFrame->device(), tempFrame->command(),
                 tempFrame->data() ? tempFrame->data()->size() : -1);

    if (tempFrame->device() == FUJI_DEVICEID_DISK && _fujiDev != nullptr
        && _fujiDev->boot_config)
    {
        _activeDev = &_fujiDev->bootdisk;

        Debug_println("FujiNet CONFIG boot");
        // handle command
        _activeDev->rs232_process(*tempFrame);
    }
    else
    {
        // find device, ack and pass control
        // or go back to WAIT
        for (auto devicep : _daisyChain)
        {
            if (tempFrame->device() == devicep->_devnum)
            {
                _activeDev = devicep;

                // FIXME - This is a terrible hack to allow devices to continue to
                // use the pattern of fetching data on their own instead of
                // upgrading them fully to work with packets.
                _activeDev->_legacyPacketData = tempFrame.get();
                _activeDev->_legacyDataPosition = 0;

                // handle command
                _activeDev->rs232_process(*tempFrame);
                break;
            }
        }
    }

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
    Debug_printf("RS232 SETUP: Baud rate: %u\n",Config.get_rs232_baud());

    // Set up UART
#ifndef FUJINET_OVER_USB
    if (Config.get_boip_enabled())
    {
        Debug_printf("RS232 SETUP: BOIP host: %s\n", Config.get_boip_host().c_str());
        _becker.setHost(Config.get_boip_host(), Config.get_boip_port());
        _becker.begin(Config.get_boip_host(), Config.get_rs232_baud());
        _port = &_becker;
    }
    else {
        _serial.begin(ChannelConfig()
                    .baud(Config.get_rs232_baud())
                    .readTimeout(200)
                    .deviceID(SERIAL_DEVICE))
            ;
        _port = &_serial;
    }

#else /* FUJINET_OVER_USB */
    _serial.begin();
    _port = &_serial;
#endif /* FUJINET_OVER_USB */

    Debug_println("RS232 Setup Flush");
    _port->discardInput();
}

// Add device to RS232 bus
void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    if (device_id == FUJI_DEVICEID_FUJINET)
    {
        _fujiDev = (rs232Fuji *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_SERIAL)
    {
        _modemDev = (rs232Modem *)pDevice;
    }
    else if (device_id >= FUJI_DEVICEID_NETWORK && device_id <= FUJI_DEVICEID_NETWORK_LAST)
    {
        _netDev[device_id - FUJI_DEVICEID_NETWORK] = (rs232Network *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_MIDI)
    {
        _udpDev = (rs232UDPStream *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_CPM)
    {
        _cpmDev = (rs232CPM *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_PRINTER)
    {
        _printerdev = (rs232Printer *)pDevice;
    }

    pDevice->_devnum = device_id;

    _daisyChain.push_front(pDevice);
}

// Removes device from the RS232 bus.
// Note that the destructor is called on the device!
void systemBus::remDevice(virtualDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int systemBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
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

// Give devices an opportunity to clean up before a reboot
void systemBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
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

std::unique_ptr<FujiBusPacket> systemBus::readBusPacket()
{
    ByteBuffer packet;
    int val, count;

    for (count = 0; count < 2; )
    {
        val = _port->read();
        if (val < 0)
            break;
        packet.push_back(val);
        if (val == SLIP_END)
            count++;
    }

    Debug_printv("Received %d:\n%s", packet.size(),
                 util_hexdump(packet.data(), packet.size()).c_str());
    return FujiBusPacket::fromSerialized(packet);
}

void systemBus::writeBusPacket(FujiBusPacket &packet)
{
    ByteBuffer encoded = packet.serialize();
    _port->write(encoded.data(), encoded.size());
    Debug_printv("Sent %d:\n%s", encoded.size(),
                 util_hexdump(encoded.data(), encoded.size()).c_str());
    return;
}

void systemBus::sendReplyPacket(fujiDeviceID_t source, bool ack, const void *data, size_t length)
{
    ByteBuffer bb;

    if (ack && data)
    {
        const uint8_t *start = static_cast<const uint8_t*>(data);
        bb.assign(start, start + length);
    }

    FujiBusPacket packet(source, ack ? FUJICMD_ACK : FUJICMD_NAK, bb);
    writeBusPacket(packet);
    return;
}

#endif /* BUILD_RS232 */
