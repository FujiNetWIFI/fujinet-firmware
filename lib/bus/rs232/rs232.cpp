#ifdef BUILD_RS232

#include "rs232.h"

#include "../../include/debug.h"

#include "fuji.h"
#include "udpstream.h"
#include "modem.h"
#include "siocpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"
#include <endian.h>

#ifdef ESP_PLATFORM
#define SERIAL_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SERIAL_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

// Helper functions outside the class defintions

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

// Calculate 8-bit checksum
uint8_t rs232_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

/*
   RS232 WRITE to ATARI from DEVICE
   buf = buffer to send to Atari
   len = length of buffer
   err = along with data, send ERROR status to Atari rather than COMPLETE
*/
void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // Write data frame to computer
    Debug_printf("->RS232 write %hu bytes\n", len);
#ifdef VERBOSE_RS232
    Debug_printf("SEND <%u> BYTES\n\t", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
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
    Debug_printf("RECV <%u> BYTES, checksum: %hu\n\t", l, ck_rcv);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
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
    SYSTEM_BUS.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    SYSTEM_BUS.flushOutput();
    Debug_println("ACK!");
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
    fnSystem.delay_microseconds(DELAY_T5);
    SYSTEM_BUS.write('E');
    Debug_println("ERROR!");
}

// RS232 HIGH SPEED REQUEST
void virtualDevice::rs232_high_speed()
{
    Debug_print("rs232 HRS232 INDEX\n");
    uint8_t hsd = SYSTEM_BUS.getHighSpeedIndex();
    bus_to_computer((uint8_t *)&hsd, 1, false);
}

// Read and process a command frame from RS232
void systemBus::_rs232_process_cmd()
{
    Debug_printf("rs232_process_cmd()\n");
    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting RS232 baud");
        _port.setBaudrate(_rs232Baud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    memset(&tempFrame, 0, sizeof(tempFrame));

    if (_port.read((uint8_t *)&tempFrame, sizeof(tempFrame)) != sizeof(tempFrame))
    {
        Debug_println("Timeout waiting for data after CMD pin asserted");
        return;
    }
    // Turn on the RS232 indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_printf("\nCF: %02x %02x %02x %02x %02x %02x %02x\n",
                 tempFrame.device, tempFrame.comnd,
                 tempFrame.aux1, tempFrame.aux2, tempFrame.aux3, tempFrame.aux4,
                 tempFrame.cksum);
#if 0 && !defined(FUJINET_OVER_USB)
    // Wait for CMD line to raise again
    while (dsrState())
        vTaskDelay(1);
#endif /* FUJINET_OVER_USB */

    uint8_t ck = rs232_checksum((uint8_t *)&tempFrame, sizeof(tempFrame) - sizeof(tempFrame.cksum)); // Calculate Checksum
    if (ck == tempFrame.cksum)
    {
        if (tempFrame.device == RS232_DEVICEID_DISK && _fujiDev != nullptr && _fujiDev->boot_config)
        {
            _activeDev = _fujiDev->bootdisk();

            Debug_println("FujiNet CONFIG boot");
            // handle command
            _activeDev->rs232_process(&tempFrame);
        }
        else
        {
            {
                // find device, ack and pass control
                // or go back to WAIT
                for (auto devicep : _daisyChain)
                {
                    if (tempFrame.device == devicep->_devnum)
                    {
                        _activeDev = devicep;
                        // handle command
                        _activeDev->rs232_process(&tempFrame);
                    }
                }
            }
        }
    } // valid checksum
    else
    {
        Debug_printf("CHECKSUM_ERROR: Calc checksum: %02x\n",ck);
        // Switch to/from hispeed RS232 if we get enough failed frame checksums
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

#if 0 && !defined(FUJINET_OVER_USB)
    // Go process a command frame if the RS232 CMD line is asserted
    if (_port.dsrState())
    {
        _rs232_process_cmd();
    }
#else /* FUJINET_OVER_USB */
    // Go process a command frame if the RS232 CMD line is asserted
    if (_port.available())
    {
        _rs232_process_cmd();
    }
#endif /* FUJINET_OVER_USB */
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->rs232_handle_modem();
    }
    else
    // Neither CMD nor active modem, so throw out any stray input data
    {
        //Debug_println("RS232 Srvc Flush");
        _port.discardInput();
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
    _port.begin(ChannelConfig().baud(Config.get_rs232_baud()).deviceID(SERIAL_DEVICE));

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
    _port.begin();
#endif /* FUJINET_OVER_USB */

    Debug_println("RS232 Setup Flush");
    _port.discardInput();
}

// Add device to RS232 bus
void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
    if (device_id == RS232_DEVICEID_FUJINET)
    {
        _fujiDev = (rs232Fuji *)pDevice;
    }
    else if (device_id == RS232_DEVICEID_RS232)
    {
        _modemDev = (rs232Modem *)pDevice;
    }
    else if (device_id >= RS232_DEVICEID_FN_NETWORK && device_id <= RS232_DEVICEID_FN_NETWORK_LAST)
    {
        _netDev[device_id - RS232_DEVICEID_FN_NETWORK] = (rs232Network *)pDevice;
    }
    else if (device_id == RS232_DEVICEID_MIDI)
    {
        _udpDev = (rs232UDPStream *)pDevice;
    }
    else if (device_id == RS232_DEVICEID_CPM)
    {
        _cpmDev = (rs232CPM *)pDevice;
    }
    else if (device_id == RS232_DEVICEID_PRINTER)
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
            devicep->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(int device_id)
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

void systemBus::toggleBaudrate()
{
    int baudrate = _rs232Baud == RS232_BAUDRATE ? _rs232BaudHigh : RS232_BAUDRATE;

    if (useUltraHigh == true)
        baudrate = _rs232Baud == RS232_BAUDRATE ? _rs232BaudUltraHigh : RS232_BAUDRATE;

    // Debug_printf("Toggling baudrate from %d to %d\n", _rs232Baud, baudrate);
    _rs232Baud = baudrate;
    _port.setBaudrate(_rs232Baud);
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
    _port.setBaudrate(baud);
}

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
#endif /* BUILD_RS232 */
