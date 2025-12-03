#ifdef BUILD_ATARI

#include "sio.h"

#include "../../include/debug.h"

#include "sio/sioFuji.h"
#include "udpstream.h"
#include "modem.h"
#include "siocpm.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnDNS.h"
#include "led.h"
#include "utils.h"

#ifdef ESP_PLATFORM
#define SIO_UART_DEVICE FN_UART_BUS
#else /* !ESP_PLATFORM */
#define SIO_UART_DEVICE Config.get_serial_port()
#endif /* ESP_PLATFORM */

// Helper functions outside the class defintions

// Get requested buffer length from command frame
unsigned short virtualDevice::sio_get_aux()
{
    return (cmdFrame.aux2 * 256) + cmdFrame.aux1;
}

// Calculate 8-bit checksum
uint8_t sio_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

/*
   SIO WRITE to ATARI from DEVICE
   buf = buffer to send to Atari
   len = length of buffer
   err = along with data, send ERROR status to Atari rather than COMPLETE
*/
void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // Write data frame to computer
    Debug_printf("->SIO write %hu bytes\n", len);
#ifdef VERBOSE_SIO
    Debug_printf("SEND <%u> BYTES\n\t", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    // Write ERROR or COMPLETE status
    if (err == true)
        sio_error();
    else
        sio_complete();

    // Write data frame
    SYSTEM_BUS.write(buf, len);
    // Write checksum
    SYSTEM_BUS.write(sio_checksum(buf, len));

    SYSTEM_BUS.flushOutput();
}

// TODO apc: change return type to indicate valid/invalid checksum
/*
   SIO READ from ATARI by DEVICE
   buf = buffer from atari to fujinet
   len = length
   Returns checksum
*/
uint8_t virtualDevice::bus_to_peripheral(uint8_t *buf, unsigned short len)
{
    // Retrieve data frame from computer
    Debug_printf("<-SIO read %hu bytes\n", len);

#ifndef ESP_PLATFORM
    if (SYSTEM_BUS.isBoIP())
    {
        SYSTEM_BUS.netsio_write_size(len); // set hint for NetSIO
    }
#endif /* ! ESP_PLATFORM */

    __BEGIN_IGNORE_UNUSEDVARS
    size_t l = SYSTEM_BUS.read(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (SYSTEM_BUS.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = SYSTEM_BUS.read();

    uint8_t ck_tst = sio_checksum(buf, len);

#ifdef VERBOSE_SIO
    Debug_printf("RECV <%u> BYTES, checksum: %hu\n\t", (unsigned int)l, ck_rcv);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    fnSystem.delay_microseconds(DELAY_T4);

    if (ck_rcv != ck_tst)
    {
        sio_nak();
        Debug_printf("bus_to_peripheral() - Data Frame Chksum error, calc %02x, rcv %02x\n", ck_tst, ck_rcv);
        // return false; // apc
    }
    else
        sio_ack();

    return ck_rcv; // TODO apc: change to true and update all callers, no need to calculate/check checksum again
}

// SIO NAK
void virtualDevice::sio_nak()
{
    SYSTEM_BUS.write('N');
    SYSTEM_BUS.flushOutput();
#ifndef ESP_PLATFORM
    SYSTEM_BUS.set_command_processed(true);
#endif
    Debug_println("NAK!");
}

// SIO ACK
void virtualDevice::sio_ack()
{
    SYSTEM_BUS.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    SYSTEM_BUS.flushOutput();
#ifndef ESP_PLATFORM
    SYSTEM_BUS.set_command_processed(true);
#endif
    Debug_println("ACK!");
}

// SIO ACK, delayed for NetSIO sync
#ifndef ESP_PLATFORM
void virtualDevice::sio_late_ack()
{
    if (SYSTEM_BUS.isBoIP())
    {
        SYSTEM_BUS.netsio_late_sync('A');
        SYSTEM_BUS.set_command_processed(true);
        Debug_println("ACK+!");
    }
    else
    {
        sio_ack();
    }
}
#endif

// SIO COMPLETE
void virtualDevice::sio_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    SYSTEM_BUS.write('C');
    Debug_println("COMPLETE!");
}

// SIO ERROR
void virtualDevice::sio_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    SYSTEM_BUS.write('E');
    Debug_println("ERROR!");
}

// SIO HIGH SPEED REQUEST
void virtualDevice::sio_high_speed()
{
    Debug_print("sio HSIO INDEX\n");
#ifdef ESP_PLATFORM
    uint8_t hsd = SYSTEM_BUS.getHighSpeedIndex();
#else
    int index = SYSTEM_BUS.getHighSpeedIndex();
    uint8_t hsd = index == HSIO_INVALID_INDEX ? 40 : (uint8_t)index;
#endif
    bus_to_computer((uint8_t *)&hsd, 1, false);
}

// Read and process a command frame from SIO
void systemBus::_sio_process_cmd()
{
#ifdef ESP_PLATFORM
    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
#else
    if (_modemDev != nullptr && _modemDev->modemActive)
#endif
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting SIO baud");
        SYSTEM_BUS.setBaudrate(_sioBaud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    tempFrame.commanddata = 0;
    tempFrame.checksum = 0;

    if (_port->read((uint8_t *)&tempFrame, sizeof(tempFrame)) != sizeof(tempFrame))
    {
        // Debug_println("Timeout waiting for data after CMD pin asserted");
        return;
    }
    // Turn on the SIO indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_print("\n");
    Debug_printf("CF: %02x %02x %02x %02x %02x\n",
                 tempFrame.device, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.checksum);

    // Wait for CMD line to raise again
#ifdef ESP_PLATFORM
    while (commandAsserted())
        fnSystem.yield();
#else
    int i = 0;
    while (commandAsserted())
    {
        fnSystem.delay_microseconds(500);
        if (++i == 100)
        {
            Debug_println("Timeout waiting for CMD pin de-assert");
            return;
        }
    }

    int bytes_pending = _port->available();
    if (bytes_pending > 0)
    {
        Debug_printf("!!! Extra bytes pending (%d)\n", bytes_pending);
        // TODO use last 5 received bytes as command frame
        // _port->flush_input();
    }
#endif

    uint8_t ck = sio_checksum((uint8_t *)&tempFrame.commanddata, sizeof(tempFrame.commanddata)); // Calculate Checksum
    if (ck == tempFrame.checksum)
    {
#ifndef ESP_PLATFORM
        // reset counter if checksum was correct
        _command_frame_counter = 0;
#endif
        if (tempFrame.device == FUJI_DEVICEID_DISK && _fujiDev != nullptr && _fujiDev->boot_config)
        {
            _activeDev = &_fujiDev->bootdisk;
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
                _activeDev->sio_process(tempFrame.commanddata, tempFrame.checksum);
            }
        }
        else
        {
            // Command FUJI_DEVICEID_TYPE3POLL is a Type3 poll - send it to every device that cares
            if (tempFrame.device == FUJI_DEVICEID_TYPE3POLL)
            {
                Debug_println("SIO TYPE3 POLL");
                for (auto devicep : _daisyChain)
                {
                    if (devicep->listen_to_type3_polls)
                    {
                        Debug_printf("Sending TYPE3 poll to dev %x\n", devicep->_devnum);
                        _activeDev = devicep;
                        // handle command
                        _activeDev->sio_process(tempFrame.commanddata, tempFrame.checksum);
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
                        _activeDev->sio_process(tempFrame.commanddata, tempFrame.checksum);
                    }
                }
            }
        }
    } // valid checksum
    else
    {
        Debug_print("CHECKSUM_ERROR\n");
        // Switch to/from hispeed SIO if we get enough failed frame checksums
        _command_frame_counter++;
        if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == _command_frame_counter)
        {
            _command_frame_counter = 0;
            toggleBaudrate();
        }
    }

#ifndef ESP_PLATFORM
    if (!_command_processed)
    {
        // Notify NetSIO hub that we are not interested to handle this command
        sio_empty_ack();
    }
#endif
    fnLedManager.set(eLed::LED_BUS, false);
    //Debug_printv("free low heap: %lu\r\n",esp_get_free_internal_heap_size());
}

// Look to see if we have any waiting messages and process them accordingly
void systemBus::_sio_process_queue()
{
#ifdef ESP_PLATFORM
    sio_message_t msg;
    if (xQueueReceive(qSioMessages, &msg, 0) == pdTRUE)
    {
        switch (msg.message_id)
        {
        case SIOMSG_DISKSWAP:
            if (_fujiDev != nullptr)
                _fujiDev->image_rotate();
            break;
        case SIOMSG_DEBUG_TAPE:
            if (_fujiDev != nullptr)
                _fujiDev->debug_tape();
            break;
        }
    }
#endif
}

/*
 Primary SIO serivce loop:
 * If MOTOR line asserted, hand SIO processing over to the TAPE device
 * If CMD line asserted, try reading CMD frame and sending it to appropriate device
 * If CMD line not asserted but MODEM is active, give it a chance to read incoming data
 * Throw out stray input on SIO if neither of the above two are true
 * Give NETWORK devices an opportunity to signal available data
 */
void systemBus::service()
{
    // Check for any messages in our queue (this should always happen, even if any other special
    // modes disrupt normal SIO handling - should probably make a separate task for this)
    _sio_process_queue();

    if (_udpDev != nullptr && _udpDev->udpstreamActive)
    {
        if (commandAsserted())
        {
            Debug_println("CMD Asserted, stopping UDP Stream");
            _udpDev->sio_disable_udpstream();
        }
        else
        {
            _udpDev->sio_handle_udpstream();
            return; // break!
        }
    }
    else if (_cpmDev != nullptr && _cpmDev->cpmActive && Config.get_cpm_enabled())
    {
        _cpmDev->sio_handle_cpm();
        return; // break!
    }

    // check if cassette is mounted and enabled first
    if (_fujiDev->cassette()->is_mounted() && Config.get_cassette_enabled())
    { // the test which tape activation mode
        if (_fujiDev->cassette()->has_pulldown())
        {                                                    // motor line mode
#ifdef ESP_PLATFORM
            if (fnSystem.digital_read(PIN_MTR) == DIGI_HIGH) // TODO: use cassette helper function for consistency?
#else
            if (motor_asserted())
#endif
            {
                if (_fujiDev->cassette()->is_active() == false) // keep this logic because motor line mode
                {
                    Debug_println("MOTOR ON: activating cassette");
                    _fujiDev->cassette()->sio_enable_cassette();
                }
            }
            else // check if need to stop tape
            {
                if (_fujiDev->cassette()->is_active() == true)
                {
                    Debug_println("MOTOR OFF: de-activating cassette");
                    _fujiDev->cassette()->sio_disable_cassette();
                }
            }
        }

        if (_fujiDev->cassette()->is_active() == true) // handle cassette data traffic
        {
            _fujiDev->cassette()->sio_handle_cassette(); //
            return;                                      // break!
        }
    }

    // Go process a command frame if the SIO CMD line is asserted
    if (commandAsserted())
    {
#ifndef ESP_PLATFORM
        unsigned long startms = fnSystem.millis();
#endif
        _sio_process_cmd();
#ifndef ESP_PLATFORM
        unsigned long endms = fnSystem.millis();
        if (_command_processed)
            Debug_printf("SIO CMD processed in %lu ms\n", (long unsigned)endms-startms);
        else
            Debug_printf("SIO CMD ignored (%lu ms)\n", (long unsigned)endms-startms);
#endif
    }
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->sio_handle_modem();
    }
    else if (_modemDev != nullptr)
    // Neither CMD nor active modem, so throw out any stray input data
    {
        // flush UART input
#ifdef ESP_PLATFORM
        _port->discardInput();
#else
        if (!SYSTEM_BUS.isBoIP())
            _port->discardInput();
#endif
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (_netDev[i] != nullptr)
            _netDev[i]->sio_poll_interrupt();
    }
}

#ifdef ESP_PLATFORM
void systemBus::configureGPIO()
{
    // INT PIN
    fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_INT, DIGI_HIGH);
    // PROC PIN
    fnSystem.set_pin_mode(PIN_PROC, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);
    // MTR PIN
    fnSystem.set_pin_mode(PIN_MTR, gpio_mode_t::GPIO_MODE_INPUT);
    // CMD PIN
    fnSystem.set_pin_mode(PIN_CMD, gpio_mode_t::GPIO_MODE_INPUT);
    // configured by SerialSIO, not here
    // CKI PIN
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_HIGH);
    // CKO PIN
    fnSystem.set_pin_mode(PIN_CKO, gpio_mode_t::GPIO_MODE_INPUT);

    return;
}
#endif /* ESP_PLATFORM */

// Setup SIO bus
void systemBus::setup()
{
    Debug_println("SIO SETUP");

#ifdef ESP_PLATFORM
    configureGPIO();

    // Create a message queue
    qSioMessages = xQueueCreate(4, sizeof(sio_message_t));
#endif /* ESP_PLATFORM */

    // Setup SIO ports: serial UART and NetSIO
    if (Config.get_boip_enabled())
    {
        _netsio.begin(Config.get_boip_host(), Config.get_boip_port(), _sioBaud);
        _port = &_netsio;
    }
    else
    {
        _serial.begin(ChannelConfig()
                      .baud(_sioBaud)
                      .deviceID(SIO_UART_DEVICE)
#ifdef ESP_PLATFORM
                      .readTimeout(pdTICKS_TO_MS(200))
                      .discardTimeout(0)
#endif /* ESP_PLATFORM */
                      );

#ifdef ESP_PLATFORM
        _commandPin = PIN_CMD;
#else /* ! ESP_PLATFORM */
        _commandPin = Config.get_serial_command();
#endif /* ESP_PLATFORM */

        _port = &_serial;
    }

    // Set the initial HSIO index
    // First see if Config has read a value
    int i = Config.get_general_hsioindex();
    if (i != HSIO_INVALID_INDEX)
        setHighSpeedIndex(i);
    else
        setHighSpeedIndex(_sioHighSpeedIndex);

    _port->discardInput();
}

// Add device to SIO bus
void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id)
{
    if (device_id == FUJI_DEVICEID_FUJINET)
    {
        _fujiDev = (sioFuji *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_SERIAL)
    {
        _modemDev = (modem *)pDevice;
    }
    else if (device_id >= FUJI_DEVICEID_NETWORK && device_id <= FUJI_DEVICEID_NETWORK_LAST)
    {
        _netDev[device_id - FUJI_DEVICEID_NETWORK] = (sioNetwork *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_MIDI)
    {
        _udpDev = (sioUDPStream *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_CASSETTE)
    {
        _cassetteDev = (sioCassette *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_CPM)
    {
        _cpmDev = (sioCPM *)pDevice;
    }
    else if (device_id == FUJI_DEVICEID_PRINTER)
    {
        _printerdev = (sioPrinter *)pDevice;
    }

    pDevice->_devnum = device_id;

    _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
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
#ifndef ESP_PLATFORM
    _port->end();
#endif
}

void systemBus::toggleBaudrate()
{
    int baudrate = _sioBaud == SIO_STANDARD_BAUDRATE ? _sioBaudHigh : SIO_STANDARD_BAUDRATE;

    if (useUltraHigh == true)
        baudrate = _sioBaud == SIO_STANDARD_BAUDRATE ? _sioBaudUltraHigh : SIO_STANDARD_BAUDRATE;

    // Debug_printf("Toggling baudrate from %d to %d\n", _sioBaud, baudrate);
#ifndef ESP_PLATFORM
    _port->discardInput();
    _port->flushOutput();
    // hmm, calling flush() may not be enough to empty TX buffer
    fnSystem.delay_microseconds(2000);
#endif

    setBaudrate(baudrate);
}

int systemBus::getBaudrate()
{
    return _sioBaud;
}

void systemBus::setBaudrate(int baud)
{
    if (_sioBaud == baud)
    {
        Debug_printf("Baudrate already at %d - nothing to do\n", baud);
        return;
    }

    Debug_printf("Changing baudrate from %d to %d\n", _sioBaud, baud);
    _sioBaud = baud;

    // Yah this looks stupid but C++ doesn't do true polymorphism
    if (isBoIP())
        _netsio.setBaudrate(_sioBaud);
    else
        _serial.setBaudrate(_sioBaud);
}

// Set HSIO index. Sets high speed SIO baud and also returns that value.
int systemBus::setHighSpeedIndex(int hsio_index)
{
    int temp = _sioBaudHigh;

#ifdef ESP_PLATFORM
    _sioBaudHigh = (SIO_ATARI_PAL_FREQUENCY * 10) / (10 * (2 * (hsio_index + 7)) + 3);
    _sioHighSpeedIndex = hsio_index;

    int alt = SIO_ATARI_PAL_FREQUENCY / (2 * hsio_index + 14);

    Debug_printf("Set HSIO baud from %d to %d (index %d), alt=%d\n", temp, _sioBaudHigh, hsio_index, alt);
#else
    if (hsio_index == HSIO_INVALID_INDEX)
    {
        _sioHighSpeedIndex = HSIO_INVALID_INDEX;
        _sioBaudHigh = SIO_STANDARD_BAUDRATE; // 19200
        Debug_print("HSIO disabled\n");
        return _sioBaudHigh;
    }

        switch (hsio_index)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
                // compensate for late pokey sampling, add 3 cycles to byte time
                _sioBaudHigh = (SIO_ATARI_PAL_FREQUENCY * 10) / (10 * (2 * (hsio_index + 7)) + 3);
        break;
        case 8:
                _sioBaudHigh = 57600;
        break;
        case 16:
                _sioBaudHigh = 38400;
        break;
        case 40:
                _sioBaudHigh = SIO_STANDARD_BAUDRATE; // 19200
         break;
        default:
                _sioBaudHigh = SIO_ATARI_PAL_FREQUENCY / (2 * (hsio_index + 7));
        }

    _sioHighSpeedIndex = hsio_index;

    // int alt = SIO_ATARI_PAL_FREQUENCY / (2 * hsio_index + 14);

    Debug_printf("Set HSIO baud from %d to %d (index %d)\n", temp, _sioBaudHigh, hsio_index);
#endif
    return _sioBaudHigh;
}

int systemBus::getHighSpeedIndex()
{
    return _sioHighSpeedIndex;
}

int systemBus::getHighSpeedBaud()
{
    return _sioBaudHigh;
}

#ifndef ESP_PLATFORM
// indicate command was handled by some device
void systemBus::set_command_processed(bool processed)
{
    _command_processed = processed;
}

// Empty acknowledgment message for NetSIO hub
void systemBus::sio_empty_ack()
{
    if (isBoIP())
    {
        netsio_empty_sync();
    }
}
#endif

void systemBus::setUDPHost(const char *hostname, int port)
{
    if (_udpDev == nullptr)
    {
        Debug_printf("ERROR: UDP Device is not set. Cannot set HOST/PORT");
        return;
    }

    // Turn off if hostname is STOP
    if (hostname != nullptr && !strcmp(hostname, "STOP"))
    {
        if (_udpDev->udpstreamActive)
            _udpDev->sio_disable_udpstream();

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

    // Set if server mode or not
    _udpDev->udpstreamIsServer = Config.get_network_udpstream_servermode();

    // Restart UDP Stream mode if needed
    if (_udpDev->udpstreamActive)
        _udpDev->sio_disable_udpstream();
    if (_udpDev->udpstream_host_ip != IPADDR_NONE)
        _udpDev->sio_enable_udpstream();
}

void systemBus::setUltraHigh(bool _enable, int _ultraHighBaud)
{
    useUltraHigh = _enable;

    if (_enable == true)
    {
        // Setup PWM channel for CLOCK IN
#ifdef ESP_PLATFORM
        ledc_channel_config_t ledc_channel_sio_ckin;
        ledc_channel_sio_ckin.gpio_num = PIN_CKI;
        ledc_channel_sio_ckin.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_sio_ckin.duty = 1;
        ledc_channel_sio_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
        ledc_timer.duty_resolution = LEDC_TIMER_1_BIT;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = _ultraHighBaud;
#endif

        _sioBaudUltraHigh = _ultraHighBaud;

        Debug_printf("Enabling SIO clock, rate: %u\n", _ultraHighBaud);

        // Enable PWM on CLOCK IN
#ifdef ESP_PLATFORM
        ledc_channel_config(&ledc_channel_sio_ckin);
        ledc_timer_config(&ledc_timer);
#endif
        setBaudrate(_sioBaudUltraHigh);
    }
    else
    {
        Debug_printf("Disabling SIO clock.\n");
        _sioBaudUltraHigh = 0;
#ifdef ESP_PLATFORM
        ledc_stop(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 0);
        ledc_stop(LEDC_SPEED_MODE_MAX, LEDC_CHANNEL_1, 0);
#endif
        setBaudrate(SIO_STANDARD_BAUDRATE);
    }
}

bool systemBus::motor_asserted()
{
    if (_port == &_netsio)
        return _netsio.motorAsserted();

#ifdef ESP_PLATFORM
    return (bool)fnSystem.digital_read(PIN_MTR);
#else
    return false;
#endif
}

bool systemBus::commandAsserted()
{
#ifdef ESP_PLATFORM
    return fnSystem.digital_read(_commandPin) == DIGI_LOW;
#else /* ! ESP_PLATFORM */
    if (_port == &_netsio)
        return _netsio.commandAsserted();

    RS232ChannelProtocol *rs232 = dynamic_cast<RS232ChannelProtocol*>(_port);
    switch (_commandPin)
    {
    case fnConfig::SERIAL_COMMAND_DSR:
        return rs232->getDTR();
    case fnConfig::SERIAL_COMMAND_CTS:
        return rs232->getRTS();
    case fnConfig::SERIAL_COMMAND_RI:
        return rs232->getRI();
    default:
        break;
    }
    return false;
#endif /* ESP_PLATFORM */
}

#endif /* BUILD_ATARI */
