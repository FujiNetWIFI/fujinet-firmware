#ifdef BUILD_RS232

#include "rs232.h"

#include "../../include/debug.h"

#include "fuji.h"
#include "udpstream.h"
#include "modem.h"
#include "rs232cpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"

// Helper functions outside the class defintions

// Get requested buffer length from command frame
unsigned short virtualDevice::rs232_get_aux()
{
    return (cmdFrame.aux2 * 256) + cmdFrame.aux1;
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
    fnUartRS232.write(buf, len);
    // Write checksum
    fnUartRS232.write(rs232_checksum(buf, len));

    fnUartRS232.flush();
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
    size_t l = fnUartRS232.readBytes(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (fnUartRS232.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = fnUartRS232.read();

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
    fnUartRS232.write('N');
    fnUartRS232.flush();
    Debug_println("NAK!");
}

// RS232 ACK
void virtualDevice::rs232_ack()
{
    fnUartRS232.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    fnUartRS232.flush();
    Debug_println("ACK!");
}

// RS232 COMPLETE
void virtualDevice::rs232_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartRS232.write('C');
    Debug_println("COMPLETE!");
}

// RS232 ERROR
void virtualDevice::rs232_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartRS232.write('E');
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
    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting RS232 baud");
        fnUartRS232.set_baudrate(_rs232Baud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    tempFrame.commanddata = 0;
    tempFrame.checksum = 0;

    if (fnUartRS232.readBytes((uint8_t *)&tempFrame, sizeof(tempFrame)) != sizeof(tempFrame))
    {
        // Debug_println("Timeout waiting for data after CMD pin asserted");
        return;
    }
    // Turn on the RS232 indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_printf("\nCF: %02x %02x %02x %02x %02x\n",
                 tempFrame.device, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);
    // Wait for CMD line to raise again
    while (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
        fnSystem.yield();

    uint8_t ck = rs232_checksum((uint8_t *)&tempFrame.commanddata, sizeof(tempFrame.commanddata)); // Calculate Checksum
    if (ck == tempFrame.checksum)
    {
        if (tempFrame.device == RS232_DEVICEID_DISK && _fujiDev != nullptr && _fujiDev->boot_config)
        {
            _activeDev = _fujiDev->bootdisk();
            if (_activeDev->status_wait_count > 0 && tempFrame.comnd == 'R' && _fujiDev->status_wait_enabled)
            {
                Debug_printf("Disabling CONFIG boot.\n");
                _fujiDev->boot_config = false;
                return;
            }
            else
            {
                Debug_println("FujiNet CONFIG boot");
                // handle command
                _activeDev->rs232_process(tempFrame.commanddata, tempFrame.checksum);
            }
        }
        else
        {
            // Command RS232_DEVICEID_TYPE3POLL is a Type3 poll - send it to every device that cares
            if (tempFrame.device == RS232_DEVICEID_TYPE3POLL)
            {
                Debug_println("RS232 TYPE3 POLL");
                for (auto devicep : _daisyChain)
                {
                    if (devicep->listen_to_type3_polls)
                    {
                        Debug_printf("Sending TYPE3 poll to dev %x\n", devicep->_devnum);
                        _activeDev = devicep;
                        // handle command
                        _activeDev->rs232_process(tempFrame.commanddata, tempFrame.checksum);
                    }
                }
            }
            else
            {
                // find device, ack and pass control
                // or go back to WAIT
                for (auto devicep : _daisyChain)
                {
                    if (tempFrame.device == devicep->_devnum)
                    {
                        _activeDev = devicep;
                        // handle command
                        _activeDev->rs232_process(tempFrame.commanddata, tempFrame.checksum);
                    }
                }
            }
        }
    } // valid checksum
    else
    {
        Debug_print("CHECKSUM_ERROR\n");
        // Switch to/from hispeed RS232 if we get enough failed frame checksums
        _command_frame_counter++;
        if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == _command_frame_counter)
        {
            _command_frame_counter = 0;
            toggleBaudrate();
        }
    }
    fnLedManager.set(eLed::LED_BUS, false);
}

// Look to see if we have any waiting messages and process them accordingly
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
    _rs232_process_queue();

    if (_udpDev != nullptr && _udpDev->udpstreamActive)
    {
        if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
        {
#ifdef DEBUG
            Debug_println("CMD Asserted, stopping UDP Stream");
#endif
            _udpDev->rs232_disable_udpstream();
        }
        else
        {
            _udpDev->rs232_handle_udpstream();
            return; // break!
        }
    }
    else if (_cpmDev != nullptr && _cpmDev->cpmActive)
    {
        _cpmDev->rs232_handle_cpm();
        return; // break!
    }

    // check if cassette is mounted and enabled first
    if (_fujiDev->cassette()->is_mounted() && Config.get_cassette_enabled())
    { // the test which tape activation mode
        if (_fujiDev->cassette()->has_pulldown())
        {                                                    // motor line mode
            if (fnSystem.digital_read(PIN_MTR) == DIGI_HIGH) // TODO: use cassette helper function for consistency?
            {
                if (_fujiDev->cassette()->is_active() == false) // keep this logic because motor line mode
                {
                    Debug_println("MOTOR ON: activating cassette");
                    _fujiDev->cassette()->rs232_enable_cassette();
                }
            }
            else // check if need to stop tape
            {
                if (_fujiDev->cassette()->is_active() == true)
                {
                    Debug_println("MOTOR OFF: de-activating cassette");
                    _fujiDev->cassette()->rs232_disable_cassette();
                }
            }
        }

        if (_fujiDev->cassette()->is_active() == true) // handle cassette data traffic
        {
            _fujiDev->cassette()->rs232_handle_cassette(); //
            return;                                      // break!
        }
    }

    // Go process a command frame if the RS232 CMD line is asserted
    if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
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
        fnUartRS232.flush_input();
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
    Debug_println("RS232 SETUP");

    // Set up UART
    fnUartRS232.begin(_rs232Baud);

    // INT PIN
    fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_INT, DIGI_HIGH);
    // PROC PIN
    fnSystem.set_pin_mode(PIN_PROC, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);
    // MTR PIN
    //fnSystem.set_pin_mode(PIN_MTR, PINMODE_INPUT | PINMODE_PULLDOWN); // There's no PULLUP/PULLDOWN on pins 34-39
    fnSystem.set_pin_mode(PIN_MTR, gpio_mode_t::GPIO_MODE_INPUT);
    // CMD PIN
    //fnSystem.set_pin_mode(PIN_CMD, PINMODE_INPUT | PINMODE_PULLUP); // There's no PULLUP/PULLDOWN on pins 34-39
    fnSystem.set_pin_mode(PIN_CMD, gpio_mode_t::GPIO_MODE_INPUT);
    // CKI PIN
    //fnSystem.set_pin_mode(PIN_CKI, PINMODE_OUTPUT);
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_LOW);
    // CKO PIN
    fnSystem.set_pin_mode(PIN_CKO, gpio_mode_t::GPIO_MODE_INPUT);

    // Create a message queue
    qRs232Messages = xQueueCreate(4, sizeof(rs232_message_t));

    // Set the initial HRS232 index
    // First see if Config has read a value
    int i = Config.get_general_hrs232index();
    if (i != HRS232_INVALID_INDEX)
        setHighSpeedIndex(i);
    else
        setHighSpeedIndex(_rs232HighSpeedIndex);

    fnUartRS232.flush_input();
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
    else if (device_id == RS232_DEVICEID_CASSETTE)
    {
        _cassetteDev = (rs232Cassette *)pDevice;
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
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void systemBus::toggleBaudrate()
{
    int baudrate = _rs232Baud == RS232_STANDARD_BAUDRATE ? _rs232BaudHigh : RS232_STANDARD_BAUDRATE;

    if (useUltraHigh == true)
        baudrate = _rs232Baud == RS232_STANDARD_BAUDRATE ? _rs232BaudUltraHigh : RS232_STANDARD_BAUDRATE;

    Debug_printf("Toggling baudrate from %d to %d\n", _rs232Baud, baudrate);
    _rs232Baud = baudrate;
    fnUartRS232.set_baudrate(_rs232Baud);
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
    fnUartRS232.set_baudrate(baud);
}

// Set HRS232 index. Sets high speed RS232 baud and also returns that value.
int systemBus::setHighSpeedIndex(int hrs232_index)
{
    int temp = _rs232BaudHigh;
    _rs232BaudHigh = (RS232_ATARI_PAL_FREQUENCY * 10) / (10 * (2 * (hrs232_index + 7)) + 3);
    _rs232HighSpeedIndex = hrs232_index;

    int alt = RS232_ATARI_PAL_FREQUENCY / (2 * hrs232_index + 14);

    Debug_printf("Set HRS232 baud from %d to %d (index %d), alt=%d\n", temp, _rs232BaudHigh, hrs232_index, alt);
    return _rs232BaudHigh;
}

int systemBus::getHighSpeedIndex()
{
    return _rs232HighSpeedIndex;
}

int systemBus::getHighSpeedBaud()
{
    return _rs232BaudHigh;
}

void systemBus::setUDPHost(const char *hostname, int port)
{
    // Turn off if hostname is STOP
    if (!strcmp(hostname, "STOP"))
    {
        if (_udpDev->udpstreamActive)
            _udpDev->rs232_disable_udpstream();

        return;
    }

    if (hostname != nullptr && hostname[0] != '\0')
    {
        // Try to resolve the hostname and store that so we don't have to keep looking it up
        _udpDev->udpstream_host_ip = get_ip4_addr_by_name(hostname);

        if (_udpDev->udpstream_host_ip == IPADDR_NONE)
        {
            Debug_printf("Failed to resolve hostname \"%s\"\n", hostname);
        }
    }
    else
    {
        _udpDev->udpstream_host_ip = IPADDR_NONE;
    }

    if (port > 0 && port <= 65535)
    {
        _udpDev->udpstream_port = port;
    }
    else
    {
        _udpDev->udpstream_port = 5004;
        Debug_printf("UDPStream port not provided or invalid (%d), setting to 5004\n", port);
    }

    // Restart UDP Stream mode if needed
    if (_udpDev->udpstreamActive)
        _udpDev->rs232_disable_udpstream();
    if (_udpDev->udpstream_host_ip != IPADDR_NONE)
        _udpDev->rs232_enable_udpstream();
}

void systemBus::setUltraHigh(bool _enable, int _ultraHighBaud)
{
    useUltraHigh = _enable;

    if (_enable == true)
    {
        // Setup PWM channel for CLOCK IN
        ledc_channel_config_t ledc_channel_rs232_ckin;
        ledc_channel_rs232_ckin.gpio_num = PIN_CKI;
        ledc_channel_rs232_ckin.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_channel_rs232_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_rs232_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_rs232_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_rs232_ckin.duty = 1;
        ledc_channel_rs232_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_timer.duty_resolution = LEDC_TIMER_1_BIT;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = _ultraHighBaud;

        _rs232BaudUltraHigh = _ultraHighBaud;

        Debug_printf("Enabling RS232 clock, rate: %lu\n", ledc_timer.freq_hz);

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_rs232_ckin);
        ledc_timer_config(&ledc_timer);
        fnUartRS232.set_baudrate(_rs232BaudUltraHigh);
    }
    else
    {
        Debug_printf("Disabling RS232 clock.\n");
        ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);

        _rs232BaudUltraHigh = 0;
        fnUartRS232.set_baudrate(RS232_STANDARD_BAUDRATE);
    }
}

systemBus RS232; // Global RS232 object
#endif /* BUILD_ATARI */