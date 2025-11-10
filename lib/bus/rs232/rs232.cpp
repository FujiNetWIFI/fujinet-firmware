#ifdef BUILD_RS232

#include "rs232.h"
#include "FujiBusPacket.h"

#include "../../include/debug.h"

#include "rs232/rs232Fuji.h"
#include "udpstream.h"
#include "modem.h"
#include "siocpm.h"
#include "network.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"
#include "../../include/fuji_endian.h"

#ifdef ESP_PLATFORM
#define SERIAL_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SERIAL_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

// Helper functions outside the class defintions

#ifdef OBSOLETE
uint16_t virtualDevice::rs232_get_aux16_lo()
{
    return le16toh(cmdFrame.aux12);
}

uint16_t virtualDevice::rs232_get_aux16_hi()
{
    return le16toh(cmdFrame.aux34);
}

uint32_t virtualDevice::rs232_get_aux32()
{
    return le32toh(cmdFrame.aux);
}
#endif /* OBSOLETE */

// Calculate 8-bit checksum
uint8_t rs232_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

/*
   RS232 WRITE to COMPUTER from DEVICE
   buf = buffer to send to Atari
   len = length of buffer
   err = along with data, send ERROR status to Atari rather than COMPLETE
*/
#ifdef OBSOLETE
void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // Write data frame to computer
    Debug_printf("->RS232 write %hu bytes\n", len);
#ifdef VERBOSE_RS232
    Debug_printf("SEND <%u> BYTES\n", len);
    Debug_printf("\n%s\n", util_hexdump(buf, len).c_str());
#endif

    // Write ERROR or COMPLETE status
    if (err == true)
        rs232_error();
    else
        rs232_complete();

    // Write data frame
    SYSTEM_BUS.write(buf, len);
    // Write checksum
    SYSTEM_BUS.write(rs232_checksum(buf, len));

    SYSTEM_BUS.flushOutput();
}
#else /* ! OBSOLETE */
void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    SYSTEM_BUS.sendReplyPacket(_devnum, !err, buf, len);
    return;
}
#endif /* OBSOLETE */

/*
   RS232 READ from ATARI by DEVICE
   buf = buffer from atari to fujinet
   len = length
   Returns checksum
*/
uint8_t virtualDevice::bus_to_peripheral(uint8_t *buf, unsigned short len)
{
    // Retrieve data frame from computer
    Debug_printf("<-RS232 read %hu bytes\n", len);

    __BEGIN_IGNORE_UNUSEDVARS
    size_t l = SYSTEM_BUS.read(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = SYSTEM_BUS.read();

    uint8_t ck_tst = rs232_checksum(buf, len);

#ifdef VERBOSE_RS232
    Debug_printf("RECV <%u> BYTES, checksum: %hu\n", l, ck_rcv);
    Debug_printf("\n%s\n", util_hexdump(buf, len).c_str());
#endif

    fnSystem.delay_microseconds(DELAY_T4);

    if (ck_rcv != ck_tst)
    {
        rs232_nak();
        return false;
    }
    else
        rs232_ack();

    return ck_rcv;
}

// RS232 NAK
void virtualDevice::rs232_nak()
{
    SYSTEM_BUS.write('N');
    SYSTEM_BUS.flushOutput();
    Debug_println("NAK!");
}

// RS232 ACK
void virtualDevice::rs232_ack()
{
#ifdef OBSOLETE
    SYSTEM_BUS.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    SYSTEM_BUS.flushOutput();
    Debug_println("ACK!");
#endif /* OBSOLETE */
}

// RS232 COMPLETE
void virtualDevice::rs232_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    SYSTEM_BUS.write('C');
    Debug_println("COMPLETE!");
}

// RS232 ERROR
void virtualDevice::rs232_error()
{
    abort();
    fnSystem.delay_microseconds(DELAY_T5);
    SYSTEM_BUS.write('E');
    Debug_println("ERROR!");
}

#ifdef OBSOLETE
// RS232 HIGH SPEED REQUEST
void virtualDevice::rs232_high_speed()
{
    Debug_print("rs232 HRS232 INDEX\n");
    uint8_t hsd = SYSTEM_BUS.getHighSpeedIndex();
    bus_to_computer((uint8_t *)&hsd, 1, false);
}
#endif /* OBSOLETE */

// Read and process a command frame from RS232
void systemBus::_rs232_process_cmd()
{
    Debug_printf("rs232_process_cmd()\n");
    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting RS232 baud");
#if !H89_HACKERY
        _serial.setBaudrate(_rs232Baud);
#endif /* ! H89_HACKERY */
    }

    // Read CMD frame
    std::string packet;
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

    auto tempFrame = FujiBusPacket::fromSerialized(packet);
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
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->rs232_handle_modem();
    }
#ifdef OBSOLETE
    else
    // Neither CMD nor active modem, so throw out any stray input data
    {
        //Debug_println("RS232 Srvc Flush");
        _port->discardInput();
    }
#endif /* OBSOLETE */

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

    if (Config.get_boip_enabled())
    {
        Debug_printf("RS232 SETUP: BOIP host: %s\n", Config.get_boip_host().c_str());
        _becker.begin(Config.get_boip_host(), Config.get_rs232_baud());
        _port = &_becker;
    }
    // Set up UART
    else {
#if H89_HACKERY
        _serial.begin();
#else /* ! H89_HACKERY */
#ifndef FUJINET_OVER_USB
        _serial.begin(ChannelConfig()
                      .baud(Config.get_rs232_baud())
                      .readTimeout(200)
                      .deviceID(SERIAL_DEVICE))
            ;
#ifdef ESP_PLATFORM
        // // INT PIN
        // fnSystem.set_pin_mode(PIN_RS232_RI, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
        // fnSystem.digital_write(PIN_RS232_RI, DIGI_HIGH);
        // PROC PIN
        fnSystem.set_pin_mode(PIN_RS232_RI, gpio_mode_t::GPIO_MODE_OUTPUT, SystemManager::pull_updown_t::PULL_UP);
        fnSystem.digital_write(PIN_RS232_RI, DIGI_HIGH);
        // INVALID PIN
        //fnSystem.set_pin_mode(PIN_RS232_INVALID, PINMODE_INPUT | PINMODE_PULLDOWN); // There's no PULLUP/PULLDOWN on pins 34-39
        fnSystem.set_pin_mode(PIN_RS232_INVALID, gpio_mode_t::GPIO_MODE_INPUT);
        // CMD PIN
        //fnSystem.set_pin_mode(PIN_RS232_DTR, PINMODE_INPUT | PINMODE_PULLUP); // There's no PULLUP/PULLDOWN on pins 34-39
        fnSystem.set_pin_mode(PIN_RS232_DTR, gpio_mode_t::GPIO_MODE_INPUT);
        // CKI PIN
        //fnSystem.set_pin_mode(PIN_CKI, PINMODE_OUTPUT);
        // CKO PIN

        fnSystem.set_pin_mode(PIN_RS232_CTS, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_RS232_CTS,DIGI_LOW);

        fnSystem.set_pin_mode(PIN_RS232_DSR,gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_RS232_DSR,DIGI_LOW);
#endif /* ESP_PLATFORM */
#else /* FUJINET_OVER_USB */
        _serial.begin();
#endif /* FUJINET_OVER_USB */
#endif /* H89_HACKERY */
        _port = &_serial;
    }


    Debug_println("RS232 Setup Flush");
    _port->discardInput();
}

// Add device to RS232 bus
void systemBus::addDevice(virtualDevice *pDevice, FujiDeviceID device_id)
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

void systemBus::changeDeviceId(virtualDevice *p, FujiDeviceID device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(FujiDeviceID device_id)
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

#ifdef OBSOLETE
void systemBus::toggleBaudrate()
{
    int baudrate = _rs232Baud == RS232_BAUDRATE ? _rs232BaudHigh : RS232_BAUDRATE;

    if (useUltraHigh == true)
        baudrate = _rs232Baud == RS232_BAUDRATE ? _rs232BaudUltraHigh : RS232_BAUDRATE;

    // Debug_printf("Toggling baudrate from %d to %d\n", _rs232Baud, baudrate);
    _rs232Baud = baudrate;
    _serial.setBaudrate(_rs232Baud);
}
#endif /* OBSOLETE */

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
#if !H89_HACKERY
    _serial.setBaudrate(baud);
#endif /* ! H89_HACKERY */
}

#ifdef OBSOLETE
// Set HRS232 index. Sets high speed RS232 baud and also returns that value.
int systemBus::setHighSpeedIndex(int hrs232_index)
{
    return 0;
}

int systemBus::getHighSpeedIndex()
{
    return 0;
}

int systemBus::getHighSpeedBaud()
{
    return _rs232BaudHigh;
}

void systemBus::setUDPHost(const char *hostname, int port)
{
}

void systemBus::setUltraHigh(bool _enable, int _ultraHighBaud)
{
}
#endif /* OBSOLETE */

void systemBus::sendReplyPacket(FujiDeviceID source, bool ack, void *data, size_t length)
{
    FujiBusPacket packet(source, ack ? FUJICMD_ACK : FUJICMD_NAK,
                         ack ? std::string(static_cast<const char*>(data), length) : "");
    std::string encoded = packet.serialize();
    _port->write(encoded.data(), encoded.size());
    return;
}

/* Convert direct bus access into bus packets? */
size_t systemBus::read(void *buffer, size_t length)
{
    abort();
    return _port->read(buffer, length);
}

size_t systemBus::read()
{
    abort();
    return _port->read();
}

size_t systemBus::write(const void *buffer, size_t length)
{
    abort();
    return _port->write(buffer, length);
}

size_t systemBus::write(int n)
{
    abort();
    return _port->write(n);
}

size_t systemBus::available()
{
    abort();
    return _port->available();
}

void systemBus::flushOutput()
{
    abort();
    _port->flushOutput();
}

size_t systemBus::print(int n, int base)
{
    abort();
    return _port->print(n, base);
}

size_t systemBus::print(const char *str)
{
    abort();
    return _port->print(str);
}

size_t systemBus::print(const std::string &str)
{
    abort();
    return _port->print(str);
}

#endif /* BUILD_RS232 */
