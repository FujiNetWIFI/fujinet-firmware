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
byte sio_checksum(byte *chunk, int length)
{
    int chkSum = 0;
    for (int i = 0; i < length; i++)
    {
        chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
    }
    return (byte)chkSum;
}

/*
   SIO READ from PERIPHERAL to COMPUTER
   b = buffer to send to Atari
   len = length of buffer
   err = did an error happen before this read?
*/
void sioDevice::sio_to_computer(byte *b, unsigned short len, bool err)
{
    byte ck = sio_checksum(b, len);

#ifdef ESP8266
    delayMicroseconds(DELAY_T5);
#endif

    if (err == true)
        sio_error();
    else
        sio_complete();

    // Write data frame
    fnUartSIO.write(b, len);

    // Write checksum
    fnUartSIO.write(ck);

#ifdef ESP32
    fnUartSIO.flush();
#endif

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
byte sioDevice::sio_to_peripheral(byte *b, unsigned short len)
{
    byte ck;

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
#ifdef ESP32
    fnUartSIO.flush();
#endif
    Debug_println("NAK!");
}

// SIO ACK
void sioDevice::sio_ack()
{
    fnUartSIO.write('A');
#ifdef ESP32
    fnUartSIO.flush();
#endif
    Debug_println("ACK!");
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

/*
 Periodically handle the sioDevice in the loop()
 How to capture command ID from the bus and hand off to the correct device? Right now ...
 When we detect the CMD_PIN active low, we set the state machine to ID, start the command timer,
 then if there's serial data, we go to sio_get_id() by way of sio_incoming().
 Then we check the ID and if it's right, we jump to COMMAND state, return and move on.
 Also, during WAIT we toss UART data.
 
 So ...
 
 We move the CMD_PIN handling and sio_get_id() to the sioBus class, grab the ID, start the command timer,
 then search through the daisyChain for a matching ID. Once we find an ID, we set it's sioDevice cmdState to COMMAND.
 We change service() so it only reads the SIO_UART when cmdState != WAIT.
 Or rather, only call sioDevice->service() when sioDevice->state() != WAIT.
 We never will call sio_incoming when there's a WAIT state.
 Need to figure out reseting cmdTimer when state goes to WAIT or there's a NAK
 if no device is != WAIT, we toss the SIO_UART byte & set cmdTimer to 0.
 Maybe we have a BUSY state for the sioBus that's an OR of all the cmdState != WAIT.
 
 TODO: cmdTimer to sioBus, assign cmdState after finding device ID
 when checking cmdState loop through devices?
 make *activeDev when found device ID. call activeDev->sio_incoming() != nullPtr. otherwise toss UART.read
 if activeDev->cmdState == WAIT then activeDev = mullPtr
 
 NEED TO GET device state machine out of the bus state machine
 bus states: WAIT, ID, PROCESS
 WAIT->ID when cmdFlag is set
 ID->PROCESS when device # matches
 bus->WAIT when timeout or when device->WAIT after a NAK or PROCESS
 timeout occurs when a device is active and it's taking too long
 
 dev states inside of sioBus: ID, ACTIVE, WAIT
 device no longer needs ID state - need to remove it from logic
 WAIT -> ID - WHEN IRQ
 ID - >ACTIVE when _devnum matches dn else ID -> WAIT
 ACTIVE -> WAIT at timeout
 
 now folding in MULTILATOR REV-2 logic
 read all 5 bytes of cmdframe at once
 checksum before passing off control to a device - checksum is now a helper function not part of a class
 determine device ID and then copy tempframe into object cmdFrame, which can be done in most of sio_incoming
 watchout because cmdFrame is accessible from sioBus through friendship, but it's a deadend because sioDevice isn't a real device
 remove cmdTimer
 left off on line 301 but need to call sio_process() line 268
 put things in sioSetup
*/
void sioBus::service()
{
#ifdef ESP32
    // Wait for the SIO_CMD pin to go low, indicating data
    // Make sure voltage is higher than 4V to determine if the Atari is on, otherwise
    // we get stuck reading a LOW command pin until the Atari is turned on
    if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW && fnSystem.get_sio_voltage() > 4000)
#else
    if (digitalRead(PIN_CMD) == LOW)
#endif
    {
        ledMgr.set(eLed::LED_SIO, true);
        if (modemDev != nullptr && modemDev->modemActive)
        {
            modemDev->modemActive = false;
            Debug_println("Modem was active - resetting SIO baud");
            fnUartSIO.set_baudrate(sioBaud);
        }

#ifdef ESP8266
        delayMicroseconds(DELAY_T0); // computer is waiting for us to notice.
#endif

        // Read CMD frame
        cmdFrame_t tempFrame;
        tempFrame.cmdFrameData[0] = 0;
        tempFrame.cmdFrameData[1] = 0;
        tempFrame.cmdFrameData[2] = 0;
        tempFrame.cmdFrameData[3] = 0;
        tempFrame.cmdFrameData[4] = 0;

        fnUartSIO.readBytes((uint8_t *)tempFrame.cmdFrameData, 5);
        Debug_printf("\n%s CF: %02x %02x %02x %02x %02x\n", fnSystem.get_uptime_str(),
                     tempFrame.devic, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);
        // Wait for CMD line to raise again
        while (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
            fnSystem.yield();

        byte ck = sio_checksum(tempFrame.cmdFrameData, 4); // Calculate Checksum
        if (ck == tempFrame.cksum)
        {
#ifdef ESP8266
            delayMicroseconds(DELAY_T1);
#endif
#ifdef ESP8266
            delayMicroseconds(DELAY_T2);
#endif
            if (fujiDev != nullptr && fujiDev->load_config && tempFrame.devic == SIO_DEVICEID_DISK)
            {
                activeDev = fujiDev->disk();
                Debug_println("FujiNet intercepts D1:");
                for (int i = 0; i < 5; i++)
                    activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer
#ifdef ESP8266
                delayMicroseconds(DELAY_T3);
#endif
                activeDev->sio_process(); // handle command
            }
            else
            {
                // Command SIO_DEVICEID_TYPE3POLL is a Type3 poll - send it to every device that cares
                if (tempFrame.devic == SIO_DEVICEID_TYPE3POLL)
                {
                    Debug_println("SIO TYPE3 POLL");
                    for (auto devicep : daisyChain)
                    {
                        if (devicep->listen_to_type3_polls)
                        {
                            Debug_printf("Sending TYPE3 poll to dev %x\n", devicep->_devnum);
                            activeDev = devicep;
                            for (int i = 0; i < 5; i++)
                                activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer

                            activeDev->sio_process(); // handle command
                        }
                    }
                }
                else
                {
                    // find device, ack and pass control
                    // or go back to WAIT
                    for (auto devicep : daisyChain)
                    {
                        if (tempFrame.devic == devicep->_devnum)
                        {
                            activeDev = devicep;
                            for (int i = 0; i < 5; i++)
                                activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; // copy data to device's buffer
#ifdef ESP8266
                            delayMicroseconds(DELAY_T3);
#endif
                            activeDev->sio_process(); // handle command
                        }
                    }
                }
            }
        } // valid checksum
        else
        {
            // Switch to/from hispeed SIO if we get enough failed frame checksums
            command_frame_counter++;
            if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == command_frame_counter)
            {
                command_frame_counter = 0;
                if (sioBaud == HISPEED_BAUDRATE)
                    setBaudrate(STANDARD_BAUDRATE);
                else
                    setBaudrate(HISPEED_BAUDRATE);
            }
        }
        ledMgr.set(eLed::LED_SIO, false);
    } // END command line low
    else if (modemDev != nullptr && modemDev->modemActive)
    {
        modemDev->sio_handle_modem(); // Handle the modem
    }
    else
    {
        ledMgr.set(eLed::LED_SIO, false);

        if (fnUartSIO.available())
            fnUartSIO.flush_input();
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (netDev[i] != nullptr)
            netDev[i]->sio_assert_interrupts();
    }
}

// Setup SIO bus
void sioBus::setup()
{
    Debug_println("SIO SETUP");
    // Set up serial
    fnUartSIO.begin(sioBaud);
#ifdef ESP8266
    SIO_UART.swap();
#endif

    fnSystem.set_pin_mode(PIN_INT, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_INT, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_PROC, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_MTR, (PINMODE_INPUT | PINMODE_PULLDOWN));

    fnSystem.set_pin_mode(PIN_CMD, (PINMODE_INPUT | PINMODE_PULLUP));

    fnSystem.set_pin_mode(PIN_CKI, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_CKI, DIGI_LOW);

#ifdef ESP32
    fnSystem.set_pin_mode(PIN_CKO, PINMODE_INPUT);
#endif
}

// Add device to SIO bus
void sioBus::addDevice(sioDevice *pDevice, int device_id)
{
    if (device_id == SIO_DEVICEID_FUJINET)
    {
        fujiDev = (sioFuji *)pDevice;
    }
    else if (device_id == SIO_DEVICEID_RS232)
    {
        modemDev = (sioModem *)pDevice;
    }
    else if (device_id >= SIO_DEVICEID_FN_NETWORK && device_id <= SIO_DEVICEID_FN_NETWORK_LAST)
    {
        Debug_printf("NETWORK DEVICE 0x%02x ADDED!\n", device_id - SIO_DEVICEID_FN_NETWORK);
        netDev[device_id - SIO_DEVICEID_FN_NETWORK] = (sioNetwork *)pDevice;
    }

    pDevice->_devnum = device_id;

    daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void sioBus::remDevice(sioDevice *p)
{
    daisyChain.remove(p);
}

// Should avoid using this as it requires counting through the list
int sioBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void sioBus::changeDeviceId(sioDevice *p, int device_id)
{
    for (auto devicep : daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

sioDevice *sioBus::deviceById(int device_id)
{
    for (auto devicep : daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

int sioBus::getBaudrate()
{
    return sioBaud;
}

void sioBus::setBaudrate(int baudrate)
{
    Debug_printf("Switching to %d baud...\n", sioBaud);
    sioBaud = baudrate;
    //SIO_UART.updateBaudRate(sioBaud);
    fnUartSIO.set_baudrate(sioBaud);
}

sioBus SIO;
