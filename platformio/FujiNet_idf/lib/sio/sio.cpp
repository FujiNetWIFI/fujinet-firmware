#include "sio.h"
#include "modem.h"
#include "fuji.h"
#include "led.h"
#include "network.h"
#include "fnSystem.h"
#include "utils.h"
#include "../../include/debug.h"

// Helper functions outside the class defintions

// Get requested buffer length from command frame
unsigned short sioDevice::sio_get_aux()
{
    return (cmdFrame.aux2 * 256) + cmdFrame.aux1;
}

// Drain data out of SIO port
void sio_flush()
{
    fnUartSIO.flush_input();
}

// Calculate 8-bit checksum
uint8_t sio_checksum(uint8_t *chunk, int length)
{
    int chkSum = 0;
    for (int i = 0; i < length; i++)
    {
        chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
    }
    return (uint8_t)chkSum;
}

/*
   SIO READ from PERIPHERAL to COMPUTER
   b = buffer to send to Atari
   len = length of buffer
   err = did an error happen before this read?
*/
void sioDevice::sio_to_computer(uint8_t *b, unsigned short len, bool err)
{
    uint8_t ck = sio_checksum(b, len);

    if (err == true)
        sio_error();
    else
        sio_complete();

    // Write data frame
    fnUartSIO.write(b, len);

    // Write checksum
    fnUartSIO.write(ck);

    fnUartSIO.flush();

#ifdef DEBUG_VERBOSE
    Debug_printf("TO COMPUTER: ");
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", b[i]);
    Debug_printf("\nCKSUM: %02x\n\n", ck);
#endif
}

/*
   SIO WRITE from COMPUTER to PERIPHERAL
   b = buffer from atari to fujinet
   len = length
   returns checksum reported by atari
*/
uint8_t sioDevice::sio_to_peripheral(uint8_t *b, unsigned short len)
{
    uint8_t ck;

    // Retrieve data frame from computer
    Debug_printf("<-SIO read %hu\n", len);
#ifdef DEBUG_VERBOSE
    size_t l = fnSIO_UART.readBytes(b, len);
#else
    fnUartSIO.readBytes(b, len);
#endif
    // Wait for checksum
    while (0 == fnUartSIO.available())
        fnSystem.yield();

    // Receive checksum
    ck = fnUartSIO.read();

#ifdef DEBUG_VERBOSE
    Debug_printf("l: %d\n", l);
    Debug_printf("TO PERIPHERAL: ");
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", sector[i]);
    Debug_printf("\nCKSUM: %02x\n\n", ck);
#endif

    fnSystem.delay_microseconds(DELAY_T4);

    if (sio_checksum(b, len) != ck)
    {
        sio_nak();
        return false;
    }
    else
    {
        sio_ack();
    }

    return ck;
}

// SIO NAK
void sioDevice::sio_nak()
{
    fnUartSIO.write('N');
    fnUartSIO.flush();
    Debug_println("NAK!");
}

// SIO ACK
void sioDevice::sio_ack()
{
    fnUartSIO.write('A');
    fnSystem.delay_microseconds(DELAY_T5); //?
    Debug_print("ack flush\n");
    fnUartSIO.flush();
    Debug_print("ACK!\n");
}

// SIO COMPLETE
void sioDevice::sio_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartSIO.write('C');
    Debug_println("COMPLETE!");
}

// SIO ERROR
void sioDevice::sio_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    fnUartSIO.write('E');
    Debug_println("ERROR!");
}

/**
   (disk) High Speed
*/
void sioDevice::sio_high_speed()
{
    uint8_t hsd = SIO_HISPEED_INDEX;
    sio_to_computer((uint8_t *)&hsd, 1, false);
}

// Read and process a command frame from SIO
void sioBus::_sio_process_cmd()
{
    // Turn on the SIO indicator LED
    fnLedManager.set(eLed::LED_SIO, true);

    if (_modemDev != nullptr && _modemDev->modemActive)
    {
        _modemDev->modemActive = false;
        Debug_println("Modem was active - resetting SIO baud");
        fnUartSIO.set_baudrate(_sioBaud);
    }

    // Read CMD frame
    cmdFrame_t tempFrame;
    tempFrame.cmdFrameData[0] = 0;
    tempFrame.cmdFrameData[1] = 0;
    tempFrame.cmdFrameData[2] = 0;
    tempFrame.cmdFrameData[3] = 0;
    tempFrame.cmdFrameData[4] = 0;

    if (fnUartSIO.readBytes((uint8_t *)tempFrame.cmdFrameData, 5) != 5)
    {
        Debug_println("Timeout waiting for data after CMD pin asserted");
        return;
    }

    Debug_printf("\nCF: %02x %02x %02x %02x %02x\n",
                 tempFrame.devic, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);
    // Wait for CMD line to raise again
    while (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
        fnSystem.yield();

    uint8_t ck = sio_checksum(tempFrame.cmdFrameData, 4); // Calculate Checksum
    if (ck == tempFrame.cksum)
    {
        Debug_print("checksum_ok\n");
        if (_fujiDev != nullptr && _fujiDev->load_config && tempFrame.devic == SIO_DEVICEID_DISK)
        {
            _activeDev = _fujiDev->disk();
            Debug_println("FujiNet intercepts D1:");
            for (int i = 0; i < 5; i++)
                _activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer

            _activeDev->sio_process(); // handle command
        }
        else
        {
            // Command SIO_DEVICEID_TYPE3POLL is a Type3 poll - send it to every device that cares
            if (tempFrame.devic == SIO_DEVICEID_TYPE3POLL)
            {
                Debug_println("SIO TYPE3 POLL");
                for (auto devicep : _daisyChain)
                {
                    if (devicep->listen_to_type3_polls)
                    {
                        Debug_printf("Sending TYPE3 poll to dev %x\n", devicep->_devnum);
                        _activeDev = devicep;
                        for (int i = 0; i < 5; i++)
                            _activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer

                        _activeDev->sio_process(); // handle command
                    }
                }
            }
            else
            {
                // find device, ack and pass control
                // or go back to WAIT
                for (auto devicep : _daisyChain)
                {
                    if (tempFrame.devic == devicep->_devnum)
                    {
                        _activeDev = devicep;
                        for (int i = 0; i < 5; i++)
                            _activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer

                        _activeDev->sio_process(); // handle command
                    }
                }
            }
        }
    } // valid checksum
    else
    {
        Debug_print("CHECKSUM_ERROR\n");
        // Switch to/from hispeed SIO if we get enough failed frame checksums
        command_frame_counter++;
        if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == command_frame_counter)
        {
            command_frame_counter = 0;
            toggleBaudrate();
        }
    }
    fnLedManager.set(eLed::LED_SIO, false);
}

/*
 Primary SIO serivce loop:
 1. If CMD line asserted, try reading CMD frame and sending it to appropriate device
 2. If CMD line not asserted but MODEM is active, give it a chance to read incoming data
 3. Throw out stray input on SIO if neither of the above two are true
 4. Give NETWORK devices an opportunity to signal available data
 */
void sioBus::service()
{
    // Go process a command frame if the SIO CMD line is asserted
    if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
    {
        _sio_process_cmd();
    }
    // Go check if the modem needs to read data if it's active
    else if (_modemDev != nullptr && _modemDev->modemActive)
    {
        _modemDev->sio_handle_modem();
    } else
    // Neither CMD nor active modem, so throw out any stray input data
    {
        fnUartSIO.flush_input();
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (_netDev[i] != nullptr)
            _netDev[i]->sio_assert_interrupts();
    }
}

// Setup SIO bus
void sioBus::setup()
{
    Debug_println("SIO SETUP");
    // Set up serial
    fnUartSIO.begin(_sioBaud);

    fnSystem.set_pin_mode(PIN_INT, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_INT, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_PROC, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_MTR, PINMODE_INPUT | PINMODE_PULLDOWN);

    fnSystem.set_pin_mode(PIN_CMD, PINMODE_INPUT | PINMODE_PULLUP);

    fnSystem.set_pin_mode(PIN_CKI, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_CKI, DIGI_LOW);

    fnSystem.set_pin_mode(PIN_CKO, PINMODE_INPUT);

    sio_flush();
}

// Add device to SIO bus
void sioBus::addDevice(sioDevice *pDevice, int device_id)
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

    pDevice->_devnum = device_id;

    _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void sioBus::remDevice(sioDevice *p)
{
    _daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int sioBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void sioBus::changeDeviceId(sioDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

sioDevice *sioBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void sioBus::toggleBaudrate()
{
    int baudrate = _sioBaud == SIO_STANDARD_BAUDRATE ? SIO_HISPEED_BAUDRATE : SIO_STANDARD_BAUDRATE;
    Debug_printf("Toggling baudrate from %d to %d\n", _sioBaud, baudrate);
    _sioBaud = baudrate;
    fnUartSIO.set_baudrate(_sioBaud);
}

sioBus SIO;         // Global SIO object
