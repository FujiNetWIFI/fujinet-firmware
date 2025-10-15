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
    fnUartBUS.write(buf, len);
    // Write checksum
    fnUartBUS.write(rs232_checksum(buf, len));

    fnUartBUS.flush();
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
    size_t l = fnUartBUS.readBytes(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (fnUartBUS.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = fnUartBUS.read();

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
    fnUartBUS.write('N');
    fnUartBUS.flush();
    Debug_println("NAK!");
}

// RS232 ACK
void virtualDevice::rs232_ack()
{
    fnUartBUS.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    fnUartBUS.flush();
    Debug_println("ACK!");
}

// RS232 COMPLETE
void virtualDevice::rs232_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartBUS.write('C');
    Debug_println("COMPLETE!");
}

// RS232 ERROR
void virtualDevice::rs232_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartBUS.write('E');
    Debug_println("ERROR!");
}

// RS232 HIGH SPEED REQUEST
void virtualDevice::rs232_high_speed()
{
    Debug_print("rs232 HRS232 INDEX\n");
    uint8_t hsd = RS232.getHighSpeedIndex();
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
        fnUartBUS.set_baudrate(_rs232Baud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    memset(&tempFrame, 0, sizeof(tempFrame));

    if (fnUartBUS.readBytes((uint8_t *)&tempFrame, sizeof(tempFrame)) != sizeof(tempFrame))
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
    // Wait for CMD line to raise again
    while (fnSystem.digital_read(PIN_RS232_DTR) == DIGI_LOW)
        vTaskDelay(1);

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

// Look to see if we have any waiting messages and process them accordingly
/*
void systemBus::_rs232_process_queue()
{
    rs232_message_t msg;
    if (xQueueReceive(qRs232Messages, &msg, 0) == pdTRUE)
    {
        switch (msg.message_id)
        {
        case RS232MSG_DISKSWAP:
            if (_fujiDev != nullptr)
                _fujiDev->image_rotate();
            break;
        case RS232MSG_DEBUG_TAPE:
            if (_fujiDev != nullptr)
                _fujiDev->debug_tape();
            break;
        }
    }
}
*/

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

    // Go process a command frame if the RS232 CMD line is asserted
    if (fnSystem.digital_read(PIN_RS232_DTR) == DIGI_LOW)
    {
        _rs232_process_cmd();
    }
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->rs232_handle_modem();
    }
    else
    // Neither CMD nor active modem, so throw out any stray input data
    {
        //Debug_println("RS232 Srvc Flush");
        fnUartBUS.flush_input();
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
    fnUartBUS.begin(Config.get_rs232_baud());

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
    
    // Create a message queue
    qRs232Messages = xQueueCreate(4, sizeof(rs232_message_t));

    Debug_println("RS232 Setup Flush");
    fnUartBUS.flush_input();
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
    fnUartBUS.set_baudrate(_rs232Baud);
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
    fnUartBUS.set_baudrate(baud);
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
