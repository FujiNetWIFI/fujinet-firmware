#ifdef BUILD_ATARI

#include "sio.h"

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
    fnSioLink.write(buf, len);
    // Write checksum
    fnSioLink.write(sio_checksum(buf, len));

    fnSioLink.flush();
}

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

    if (fnSioLink.get_sio_mode() == SioLink::sio_mode::NETSIO)
    {
        fnSioLink.netsio_write_size(len); // set hint for NetSIO
    }

    __BEGIN_IGNORE_UNUSEDVARS
    size_t l = fnSioLink.readBytes(buf, len);
    __END_IGNORE_UNUSEDVARS

    // Wait for checksum
    while (fnSioLink.available() <= 0)
        fnSystem.yield();
    uint8_t ck_rcv = fnSioLink.read();

    uint8_t ck_tst = sio_checksum(buf, len);

#ifdef VERBOSE_SIO
    Debug_printf("RECV <%u> BYTES, checksum: %hu\n\t", l, ck_rcv);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    fnSystem.delay_microseconds(DELAY_T4);

    if (ck_rcv != ck_tst)
    {
        sio_nak();
        return false;
    }
    else
        sio_ack();

    return ck_rcv;
}

// SIO NAK
void virtualDevice::sio_nak()
{
    fnSioLink.write('N');
    fnSioLink.flush();
    SIO.set_command_processed(true);
    Debug_println("NAK!");
}

// SIO ACK
void virtualDevice::sio_ack()
{
    fnSioLink.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    fnSioLink.flush();
    SIO.set_command_processed(true);
    Debug_println("ACK!");
}

// SIO delayed ACK
// for NetSIO, ACK is delayed until we now how much data will be read from Atari
void virtualDevice::sio_late_ack()
{
    if (fnSioLink.get_sio_mode() == SioLink::sio_mode::NETSIO)
    {
        fnSioLink.netsio_late_sync('A');
        SIO.set_command_processed(true);
        Debug_println("ACK+!");
    }
    else
    {
        sio_ack();
    }
}

// SIO COMPLETE
void virtualDevice::sio_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnSioLink.write('C');
    Debug_println("COMPLETE!");
}

// SIO ERROR
void virtualDevice::sio_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnSioLink.write('E');
    Debug_println("ERROR!");
}

// SIO HIGH SPEED REQUEST
void virtualDevice::sio_high_speed()
{
    Debug_print("sio HSIO INDEX\n");
    uint8_t hsd = SIO.getHighSpeedIndex();
    bus_to_computer((uint8_t *)&hsd, 1, false);
}

// Read and process a command frame from SIO
void systemBus::_sio_process_cmd()
{
    _command_processed = false;

    if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting SIO baud");
        fnSioLink.set_baudrate(_sioBaud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    tempFrame.commanddata = 0;
    tempFrame.checksum = 0;

    if (fnSioLink.readBytes((uint8_t *)&tempFrame, sizeof(tempFrame)) != sizeof(tempFrame))
    {
        // Debug_println("Timeout waiting for data after CMD pin asserted");
        return;
    }
    // Turn on the SIO indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    Debug_printf("\nCF: %02x %02x %02x %02x %02x\n",
                 tempFrame.device, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);
    // Wait for CMD line to raise again
    while (fnSioLink.command_asserted())
        fnSystem.yield();

    uint8_t ck = sio_checksum((uint8_t *)&tempFrame.commanddata, sizeof(tempFrame.commanddata)); // Calculate Checksum
    if (ck == tempFrame.checksum)
    {
        if (tempFrame.device == SIO_DEVICEID_DISK && _fujiDev != nullptr && _fujiDev->boot_config)
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
                _activeDev->sio_process(tempFrame.commanddata, tempFrame.checksum);
            }
        }
        else
        {
            // Command SIO_DEVICEID_TYPE3POLL is a Type3 poll - send it to every device that cares
            if (tempFrame.device == SIO_DEVICEID_TYPE3POLL)
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

    if (!_command_processed)
    {
        // Notify NetSIO hub that we are not interested to handle this command
        sio_empty_ack();
    }
    fnLedManager.set(eLed::LED_BUS, false);
}

// Look to see if we have any waiting messages and process them accordingly
void systemBus::_sio_process_queue()
{
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
        case SIOMSG_SWAP_SIOMODE:
            fnSioLink.reset_sio_port(Config.get_netsio_enabled() ? SioLink::sio_mode::NETSIO : SioLink::sio_mode::SERIAL);
            break;
        }
    }
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
        if (fnSioLink.command_asserted())
        {
#ifdef DEBUG
            Debug_println("CMD Asserted, stopping UDP Stream");
#endif
            _udpDev->sio_disable_udpstream();
        }
        else
        {
            _udpDev->sio_handle_udpstream();
            return; // break!
        }
    }
    else if (_cpmDev != nullptr && _cpmDev->cpmActive)
    {
        _cpmDev->sio_handle_cpm();
        return; // break!
    }

    // check if cassette is mounted and enabled first
    if (_fujiDev->cassette()->is_mounted() && Config.get_cassette_enabled())
    { // the test which tape activation mode
        if (_fujiDev->cassette()->has_pulldown())
        {                                                    // motor line mode
            if (fnSioLink.motor_asserted())
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
    if (fnSioLink.command_asserted())
    {
        _sio_process_cmd();
    }
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive && Config.get_modem_enabled())
    {
        _modemDev->sio_handle_modem();
    }
    else
    // Neither CMD nor active modem, so throw out any stray input data
    {
        fnSioLink.flush_input();
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (_netDev[i] != nullptr)
            _netDev[i]->sio_poll_interrupt();
    }
}

// Setup SIO bus
void systemBus::setup()
{
    Debug_println("SIO SETUP");

    // Setup SIO ports: serial UART and NetSIO
    fnSioLink.setup_serial_port(); // UART
    fnSioLink.set_netsio_host(Config.get_netsio_host().c_str(), Config.get_netsio_port()); // NetSIO
    fnSioLink.set_sio_mode(Config.get_netsio_enabled() ? SioLink::sio_mode::NETSIO : SioLink::sio_mode::SERIAL);
    fnSioLink.begin(_sioBaud);

    fnSioLink.set_interrupt(false);
    fnSioLink.set_proceed(false);

    // Create a message queue
    qSioMessages = xQueueCreate(4, sizeof(sio_message_t));

    // Set the initial HSIO index
    // First see if Config has read a value
    int i = Config.get_general_hsioindex();
    if (i != HSIO_INVALID_INDEX)
        setHighSpeedIndex(i);
    else
        setHighSpeedIndex(_sioHighSpeedIndex);

    fnSioLink.flush_input();
}

// Add device to SIO bus
void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
    if (device_id == SIO_DEVICEID_FUJINET)
    {
        _fujiDev = (sioFuji *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_RS232)
    {
        _modemDev = (sioModem *)pDevice;
    }
    else if (device_id >= SIO_DEVICEID_FN_NETWORK && device_id <= SIO_DEVICEID_FN_NETWORK_LAST)
    {
        _netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (sioNetwork *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_MIDI)
    {
        _udpDev = (sioUDPStream *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_CASSETTE)
    {
        _cassetteDev = (sioCassette *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_CPM)
    {
        _cpmDev = (sioCPM *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_PRINTER)
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

    if (fnSioLink.get_sio_mode() != SioLink::sio_mode::SERIAL) // don't touch SIO UART, it may not like it
        fnSioLink.end();
}

void systemBus::toggleBaudrate()
{
    int baudrate = _sioBaud == SIO_STANDARD_BAUDRATE ? _sioBaudHigh : SIO_STANDARD_BAUDRATE;

    if (useUltraHigh == true)
        baudrate = _sioBaud == SIO_STANDARD_BAUDRATE ? _sioBaudUltraHigh : SIO_STANDARD_BAUDRATE;

    Debug_printf("Toggling baudrate from %d to %d\n", _sioBaud, baudrate);
    _sioBaud = baudrate;
    fnSioLink.set_baudrate(_sioBaud);
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
    fnSioLink.set_baudrate(baud);
}

// Set HSIO index. Sets high speed SIO baud and also returns that value.
int systemBus::setHighSpeedIndex(int hsio_index)
{
    int temp = _sioBaudHigh;
    _sioBaudHigh = (SIO_ATARI_PAL_FREQUENCY * 10) / (10 * (2 * (hsio_index + 7)) + 3);
    _sioHighSpeedIndex = hsio_index;

    int alt = SIO_ATARI_PAL_FREQUENCY / (2 * hsio_index + 14);

    Debug_printf("Set HSIO baud from %d to %d (index %d), alt=%d\n", temp, _sioBaudHigh, hsio_index, alt);
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

// indicate that command was acknowledged
void systemBus::set_command_processed(bool processed)
{
    _command_processed = processed;
}

// Empty acknowledgment message for NetSIO hub
void systemBus::sio_empty_ack()
{
    if (fnSioLink.get_sio_mode() == SioLink::sio_mode::NETSIO)
    {
        fnSioLink.netsio_empty_sync();
    }
}

void systemBus::setUDPHost(const char *hostname, int port)
{
    // Turn off if hostname is STOP
    if (!strcmp(hostname, "STOP"))
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
        ledc_channel_config_t ledc_channel_sio_ckin;
        ledc_channel_sio_ckin.gpio_num = PIN_CKI;
        ledc_channel_sio_ckin.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
        ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
        ledc_channel_sio_ckin.duty = 1;
        ledc_channel_sio_ckin.hpoint = 0;

        // Setup PWM timer for CLOCK IN
        ledc_timer_config_t ledc_timer;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
        ledc_timer.duty_resolution = LEDC_TIMER_1_BIT;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.freq_hz = _ultraHighBaud;

        _sioBaudUltraHigh = _ultraHighBaud;

        Debug_printf("Enabling SIO clock, rate: %lu\n", ledc_timer.freq_hz);

        // Enable PWM on CLOCK IN
        ledc_channel_config(&ledc_channel_sio_ckin);
        ledc_timer_config(&ledc_timer);
        fnSioLink.set_baudrate(_sioBaudUltraHigh);
    }
    else
    {
        Debug_printf("Disabling SIO clock.\n");
        ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, 0);

        _sioBaudUltraHigh = 0;
        fnSioLink.set_baudrate(SIO_STANDARD_BAUDRATE);
    }
}

systemBus SIO; // Global SIO object
#endif /* BUILD_ATARI */