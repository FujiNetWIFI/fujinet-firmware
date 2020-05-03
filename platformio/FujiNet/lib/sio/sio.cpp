#include "sio.h"
#include "modem.h"
#include "fuji.h"
#include "led.h"
#include "network.h"
#include "fnSystem.h"

// helper functions outside the class defintions

/**
 * Get requested buffer length from command frame
 */
unsigned short sioDevice::sio_get_aux()
{
    return (cmdFrame.aux2 * 256) + cmdFrame.aux1;
}

/**
   Drain data out of SIO port
*/
void sio_flush()
{
    while (SIO_UART.available())
    {
        SIO_UART.read(); // toss it.
#ifdef DEBUG
        Debug_printf(".");
#endif
    }
}

// calculate 8-bit checksum.
byte sio_checksum(byte *chunk, int length)
{
    int chkSum = 0;
    for (int i = 0; i < length; i++)
    {
        chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
    }
    return (byte)chkSum;
}

/**
   sio READ from PERIPHERAL to COMPUTER
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

    // Write data frame.
    SIO_UART.write(b, len);

    // Write checksum
    SIO_UART.write(ck);

#ifdef ESP32
    SIO_UART.flush();
#endif

#ifdef DEBUG_VERBOSE
    Debug_printf("TO COMPUTER: ");
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", b[i]);
    Debug_printf("\nCKSUM: %02x\n\n", ck);
#endif
}

/**
   sio WRITE from COMPUTER to PERIPHERAL
   b = buffer from atari to fujinet
   len = length
   returns checksum reported by atari
*/
byte sioDevice::sio_to_peripheral(byte *b, unsigned short len)
{
    byte ck;

// Retrieve data frame from computer
#ifdef DEBUG
    Debug_printf("<-SIO read %hu\n", len);
#endif
#ifdef DEBUG_VERBOSE
    size_t l = SIO_UART.readBytes(b, len);
#else
    SIO_UART.readBytes(b, len);
#endif
    // Wait for checksum
    while (!SIO_UART.available())
        yield();

    // Receive Checksum
    ck = SIO_UART.read();

#ifdef DEBUG_VERBOSE
    Debug_printf("l: %d\n", l);
    Debug_printf("TO PERIPHERAL: ");
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", sector[i]);
    Debug_printf("\nCKSUM: %02x\n\n", ck);
#endif

    //#ifdef ESP8266
    delayMicroseconds(DELAY_T4);
    //#endif

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

/**
   sio NAK
*/
void sioDevice::sio_nak()
{
    SIO_UART.write('N');
#ifdef ESP32
    SIO_UART.flush();
#endif
#ifdef DEBUG
    Debug_println("NAK!");
#endif
}

/**
   sio ACK
*/
void sioDevice::sio_ack()
{
    SIO_UART.write('A');
#ifdef ESP32
    SIO_UART.flush();
#endif
#ifdef DEBUG
    Debug_println("ACK!");
#endif
}

/**
   sio COMPLETE
*/
void sioDevice::sio_complete()
{
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
#ifdef DEBUG
    Debug_println("COMPLETE!");
#endif
}

/**
   sio ERROR
*/
void sioDevice::sio_error()
{
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('E');
#ifdef DEBUG
    Debug_println("ERROR!");
#endif
}

// periodically handle the sioDevice in the loop()
// how to capture command ID from the bus and hand off to the correct device? Right now ...
// when we detect the CMD_PIN active low, we set the state machine to ID, start the command timer,
// then if there's serial data, we go to sio_get_id() by way of sio_incoming().
// then we check the ID and if it's right, we jump to COMMAND state, return and move on.
// Also, during WAIT we toss UART data.
//
// so ...
//
// we move the CMD_PIN handling and sio_get_id() to the sioBus class, grab the ID, start the command timer,
// then search through the daisyChain for a matching ID. Once we find an ID, we set it's sioDevice cmdState to COMMAND.
// We change service() so it only reads the SIO_UART when cmdState != WAIT.
// or rather, only call sioDevice->service() when sioDevice->state() != WAIT.
// we never will call sio_incoming when there's a WAIT state.
// need to figure out reseting cmdTimer when state goes to WAIT or there's a NAK
// if no device is != WAIT, we toss the SIO_UART byte & set cmdTimer to 0.
// Maybe we have a BUSY state for the sioBus that's an OR of all the cmdState != WAIT.
//
// todo: cmdTimer to sioBus, assign cmdState after finding device ID
// when checking cmdState loop through devices?
// make *activeDev when found device ID. call activeDev->sio_incoming() != nullPtr. otherwise toss UART.read
// if activeDev->cmdState == WAIT then activeDev = mullPtr
//
// NEED TO GET device state machine out of the bus state machine
// bus states: WAIT, ID, PROCESS
// WAIT->ID when cmdFlag is set
// ID->PROCESS when device # matches
// bus->WAIT when timeout or when device->WAIT after a NAK or PROCESS
// timeout occurs when a device is active and it's taking too long
//
// dev states inside of sioBus: ID, ACTIVE, WAIT
// device no longer needs ID state - need to remove it from logic
// WAIT -> ID - WHEN IRQ
// ID - >ACTIVE when _devnum matches dn else ID -> WAIT
// ACTIVE -> WAIT at timeout

// now folding in MULTILATOR REV-2 logic
// read all 5 bytes of cmdframe at once
// checksum before passing off control to a device - checksum is now a helper function not part of a class
// determine device ID and then copy tempframe into object cmdFrame, which can be done in most of sio_incoming
// watchout because cmdFrame is accessible from sioBus through friendship, but it's a deadend because sioDevice isn't a real device
// remove cmdTimer
// left off on line 301 but need to call sio_process() line 268
// put things in sioSetup

void sioBus::service()
{
    int a;
#ifdef ESP32
    // Make sure voltage is higher than 4V to determine atari is on, otherwise
    // we get stuck reading a LOW command pin until the atari is turned on
    if (digitalRead(PIN_CMD) == LOW && fnSystem.get_sio_voltage() > 4000)
#else
    if (digitalRead(PIN_CMD) == LOW)
#endif
    {
        ledMgr.set(eLed::LED_SIO, true);
        //memset(cmdFrame.cmdFrameData, 0, 5); // clear cmd frame
        if (modemDev != nullptr && modemDev->modemActive)
        {
            modemDev->modemActive = false;
            SIO_UART.updateBaudRate(sioBaud);
#ifdef DEBUG
            Debug_println("SIO Baud");
#endif
        }

#ifdef ESP8266
        delayMicroseconds(DELAY_T0); // computer is waiting for us to notice.
#endif

        // read cmd frame
        cmdFrame_t tempFrame;
        SIO_UART.readBytes(tempFrame.cmdFrameData, 5);
#ifdef DEBUG
        Debug_printf("\n%s CF: %02x %02x %02x %02x %02x\n", fnSystem.get_uptime_str(),
                     tempFrame.devic, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);
#endif
        byte ck = sio_checksum(tempFrame.cmdFrameData, 4); // Calculate Checksum
        // Wait for CMD line to raise again
        while (digitalRead(PIN_CMD) == LOW)
            yield();
        if (ck == tempFrame.cksum)
        {
#ifdef ESP8266
            delayMicroseconds(DELAY_T1);
#endif
#ifdef ESP8266
            delayMicroseconds(DELAY_T2);
#endif
            if (fujiDev != nullptr && fujiDev->load_config && tempFrame.devic == 0x31)
            {
                activeDev = fujiDev->disk();
#ifdef DEBUG
                Debug_println("FujiNet intercepts D1:");
#endif
                for (int i = 0; i < 5; i++)
                {
                    activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; //  need to copy an array by elements
                }
#ifdef ESP8266
                delayMicroseconds(DELAY_T3);
#endif
                activeDev->sio_process(); // execute command
            }
            else
            {
                // Command $4F is a Type3 poll - send it to every device that cares
                if (tempFrame.devic == 0x4F)
                {
#ifdef DEBUG
                    Debug_println("SIO TYPE3 POLL");
#endif
                    for (int i = 0; i < numDevices(); i++)
                    {
                        if (device(i)->listen_to_type3_polls)
                        {
#ifdef DEBUG
                            Debug_printf("Sending TYPE3 poll to dev %x\n", device(i)->_devnum);
#endif
                            activeDev = device(i);
                            for (int i = 0; i < 5; i++)
                            {
                                activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; //  need to copy an array by elements
                            }
                            activeDev->sio_process(); // execute command
                        }
                    }
                }
                else
                {
                    // find device, ack and pass control
                    // or go back to WAIT
                    // this is what sioBus::sio_get_id() does, but need to pass the tempFrame to it
                    for (int i = 0; i < numDevices(); i++)
                    {
                        if (tempFrame.devic == device(i)->_devnum)
                        {
                            //BUG_UART.print("Found Device "); BUG_UART.println(dn,HEX);
                            activeDev = device(i);
                            for (int i = 0; i < 5; i++)
                            {
                                activeDev->cmdFrame.cmdFrameData[i] = tempFrame.cmdFrameData[i]; //  need to copy an array by elements
                            }
#ifdef ESP8266
                            delayMicroseconds(DELAY_T3);
#endif
                            activeDev->sio_process(); // execute command
                        }
                    }
                }
            }
        } // valid checksum
        else
        {
            // HIGHSPEED
            command_frame_counter++;
            if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == command_frame_counter)
            {
                command_frame_counter = 0;
                if (sioBaud == HISPEED_BAUDRATE)
                {
                    setBaudrate(STANDARD_BAUDRATE);
                }
                else
                {
                    setBaudrate(HISPEED_BAUDRATE);
                }
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
        a = SIO_UART.available();
        if (a)
            while (SIO_UART.available())
                SIO_UART.read(); // dump it.
    }

    // Handle interrupts from network protocols
    for (int i = 0; i < 8; i++)
    {
        if (netDev[i] != nullptr)
        {
            netDev[i]->sio_assert_interrupts();
        }
    }
}

// setup SIO bus
void sioBus::setup()
{
#ifdef DEBUG
    Debug_println("SIO SETUP");
#endif
    // Set up serial
    SIO_UART.begin(sioBaud);
#ifdef ESP8266
    SIO_UART.swap();
#endif

    pinMode(PIN_INT, OUTPUT);
    digitalWrite(PIN_INT, HIGH);
    pinMode(PIN_PROC, OUTPUT);
    digitalWrite(PIN_PROC, HIGH);
    pinMode(PIN_MTR, INPUT_PULLDOWN);
    pinMode(PIN_CMD, INPUT_PULLUP);
    pinMode(PIN_CKI, OUTPUT);
    digitalWrite(PIN_CKI, LOW);
#ifdef ESP32
    pinMode(PIN_CKO, INPUT);
    pinMode(PIN_CKI, OUTPUT);
#endif
}

void sioBus::addDevice(sioDevice *p, int N)
{
    if (N == 0x70)
    {
        fujiDev = (sioFuji *)p;
    }
    else if (N == ADDR_R)
    {
#ifdef DEBUG
        Debug_println("MODEM ADDED!");
#endif
        modemDev = (sioModem *)p;
    }
    else if (N == 0x71 || N == 0x72 || N == 0x73 || N == 0x74 || N == 0x75 || N == 0x76 || N == 0x77 || N == 0x78)
    {
#ifdef DEBUG
        Debug_printf("NETWORK DEVICE 0x%02x ADDED!\n", N - 0x71);
#endif
        netDev[N - 0x71] = (sioNetwork *)p;
    }

    p->_devnum = N;
    daisyChain.add(p);
}

bool sioBus::remDevice(sioDevice *p)
{
    int i = 0;
    while (p != device(i))
    {
        i++;
        if (i > numDevices())
            return false;
    }
    daisyChain.remove(i);
    return true;
}

int sioBus::numDevices()
{
    return daisyChain.size();
}

sioDevice *sioBus::device(int i)
{
    return daisyChain.get(i);
}

int sioBus::getBaudrate()
{
    return sioBaud;
}

void sioBus::setBaudrate(int baudrate)
{
#ifdef DEBUG
    sioBaud = baudrate;
    Debug_printf("Switching to %d baud...\n", sioBaud);
    SIO_UART.updateBaudRate(sioBaud);
#endif
}

sioBus SIO;
