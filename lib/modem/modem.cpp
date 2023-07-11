#include "modem.h"

#include "../../../include/debug.h"
#include "../../../include/atascii.h"

#include "modem-sniffer.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "siocpm.h"

#include "utils.h"

#define RECVBUFSIZE 1024

#define SIO_MODEMCMD_LOAD_RELOCATOR 0x21
#define SIO_MODEMCMD_LOAD_HANDLER 0x26
#define SIO_MODEMCMD_TYPE1_POLL 0x3F
#define SIO_MODEMCMD_TYPE3_POLL 0x40
#define SIO_MODEMCMD_CONTROL 0x41
#define SIO_MODEMCMD_CONFIGURE 0x42
#define SIO_MODEMCMD_SET_DUMP 0x44
#define SIO_MODEMCMD_LISTEN 0x4C
#define SIO_MODEMCMD_UNLISTEN 0x4D
#define SIO_MODEMCMD_BAUDRATELOCK 0x4E
#define SIO_MODEMCMD_AUTOANSWER 0x4F
#define SIO_MODEMCMD_STATUS 0x53
#define SIO_MODEMCMD_WRITE 0x57
#define SIO_MODEMCMD_STREAM 0x58

#define FIRMWARE_850RELOCATOR "/850relocator.bin"
#define FIRMWARE_850HANDLER "/850handler.bin"

/* Tested this delay several times on an 800 with Incognito
   using HSIO routines. Anything much lower gave inconsistent
   firmware loading. Delay is unnoticeable when running at
   normal speed.
*/
#define DELAY_FIRMWARE_DELIVERY 5000

/**
 * List of Telnet options to process
 */
static const telnet_telopt_t telopts[] = {
    {TELNET_TELOPT_ECHO, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_TTYPE, TELNET_WILL, TELNET_DONT},
    {TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_MSSP, TELNET_WONT, TELNET_DO},
    {-1, 0, 0}};

/**
 * Event handler for libtelnet
 */
static void _telnet_event_handler(telnet_t *telnet, telnet_event_t *ev, void *user_data)
{
    modem *m = (modem *)user_data; // somehow it thinks this is unused?

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        if (ev->data.size && m->get_uart()->write((uint8_t *)ev->data.buffer, ev->data.size) != ev->data.size)
            Debug_printf("_telnet_event_handler(%d) - Could not write complete buffer to SIO.\n", ev->type);
        break;
    case TELNET_EV_SEND:
        m->get_tcp_client().write((uint8_t *)ev->data.buffer, ev->data.size);
        break;
    case TELNET_EV_WILL:
        if (ev->neg.telopt == TELNET_TELOPT_ECHO)
            m->set_do_echo(false);
        break;
    case TELNET_EV_WONT:
        if (ev->neg.telopt == TELNET_TELOPT_ECHO)
            m->set_do_echo(true);
        break;
    case TELNET_EV_DO:
        break;
    case TELNET_EV_DONT:
        break;
    case TELNET_EV_TTYPE:
        if (ev->ttype.cmd == TELNET_TTYPE_SEND)
            telnet_ttype_is(telnet, m->get_term_type().c_str());
        break;
    case TELNET_EV_SUBNEGOTIATION:
        break;
    case TELNET_EV_ERROR:
        Debug_printf("_telnet_event_handler ERROR: %s\n", ev->error.msg);
        break;
    default:
        Debug_printf("_telnet_event_handler: Uncaught event type: %d", ev->type);
        break;
    }
}

modem::modem(FileSystem *_fs, bool snifferEnable)
{
    listen_to_type3_polls = true;
    activeFS = _fs;
    modemSniffer = new ModemSniffer(activeFS, snifferEnable);
    set_term_type("dumb");
    telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
}

modem::~modem()
{
    if (modemSniffer != nullptr)
    {
        delete modemSniffer;
    }

    if (telnet != nullptr)
    {
        telnet_free(telnet);
    }
}

// 0x40 / '@' - TYPE 3 POLL
void modem::sio_poll_3(uint8_t device, uint8_t aux1, uint8_t aux2)
{
    bool respond = false;

    // When AUX1 and AUX == 0x4E, it's a normal/general poll
    // Since XL/XE OS always does this during boot, we're going to ignore these, otherwise
    // we'd load our handler every time, and that's probably not desireable
    if (aux1 == 0 && aux2 == 0)
    {
        Debug_printf("MODEM TYPE 3 POLL #%d\n", ++count_PollType3);
        if (count_PollType3 == 26)
        {
            //Debug_print("RESPONDING to poll #26\n");
            //respond = true;
        }
        else
            return;
    }
    // When AUX1 and AUX == 0x4F, it's a request to reset the whole polling process
    if (aux1 == 0x4F && aux2 == 0x4F)
    {
        Debug_print("MODEM TYPE 3 POLL <<RESET POLL>>\n");
        count_PollType3 = 0;
        firmware_sent = false;
        return;
    }
    // When AUX1 and AUX == 0x4E, it's a request to reset poll counters
    if (aux1 == 0x4E && aux2 == 0x4E)
    {
        Debug_print("MODEM TYPE 3 POLL <<NULL POLL>>\n");
        count_PollType3 = 0;
        return;
    }
    // When AUX1 = 0x52 'R' and AUX == 1 or DEVICE == x050, it's a directed poll to "R1:"
    if ((aux1 == 0x52 && aux2 == 0x01) || device == SIO_DEVICEID_RS232)
    {
        Debug_print("MODEM TYPE 4 \"R1:\" DIRECTED POLL\n");
        respond = true;
    }

    // Get out if nothing above indicated we should respond to this poll
    if (respond == false)
        return;

    // Get size of handler
    int filesize = fnSystem.load_firmware(FIRMWARE_850HANDLER, NULL);

    // Simply return (without ACK) if we failed to get this
    if (filesize < 0)
        return;

    Debug_println("Modem acknowledging Type 4 Poll");
    sio_ack();

    // Acknowledge and return expected
    uint16_t fsize = filesize;
    uint8_t type4response[4];
    type4response[0] = LOBYTE_FROM_UINT16(fsize);
    type4response[1] = HIBYTE_FROM_UINT16(fsize);
    type4response[2] = SIO_DEVICEID_RS232;
    type4response[3] = 0;

    fnSystem.delay_microseconds(DELAY_FIRMWARE_DELIVERY);

    bus_to_computer(type4response, sizeof(type4response), false);

    // TODO: Handle the subsequent request to load the handler properly by providing the relocation blocks
}

// 0x3F / '?' - TYPE 1 POLL
void modem::sio_poll_1()
{
    /*  From Altirra sources - rs232.cpp
        Send back SIO command for booting. This is a 12 uint8_t + chk block that
        is meant to be written to the SIO parameter block starting at DDEVIC ($0300).

		The boot block MUST start at $0500. There are both BASIC-based and cart-based
        loaders that use JSR $0506 to run the loader.
    */

    // According to documentation, we're only supposed to respond to this once
    /*
    if (count_PollType1 != 0)
        return;
    count_PollType1++;
    */

    // Get size of relocator
    int filesize = fnSystem.load_firmware(FIRMWARE_850RELOCATOR, NULL);
    // Simply return (without ACK) if we failed to get this
    if (filesize < 0)
        return;

    // Acknoledge before continuing
    sio_ack();

    uint8_t bootBlock[12] = {
        0x50,       // DDEVIC
        0x01,       // DUNIT
        0x21,       // DCOMND = '!' (boot)
        0x40,       // DSTATS
        0x00, 0x05, // DBUFLO, DBUFHI == $0500
        0x08,       // DTIMLO = 8 vblanks
        0x00,       // not used
        0x00, 0x00, // DBYTLO, DBYTHI
        0x00,       // DAUX1
        0x00,       // DAUX2
    };

    // Stuff the size into the block
    uint32_t relsize = (uint32_t)filesize;
    bootBlock[8] = (uint8_t)relsize;
    bootBlock[9] = (uint8_t)(relsize >> 8);

    Debug_println("Modem acknowledging Type 1 Poll");

    fnSystem.delay_microseconds(DELAY_FIRMWARE_DELIVERY * 2);

    bus_to_computer(bootBlock, sizeof(bootBlock), false);
}

// 0x21 / '!' - RELOCATOR DOWNLOAD
// 0x26 / '&' - HANDLER DOWNLOAD
void modem::sio_send_firmware(uint8_t loadcommand)
{
    const char *firmware;
    if (loadcommand == SIO_MODEMCMD_LOAD_RELOCATOR)
    {
        firmware = FIRMWARE_850RELOCATOR;
    }
    else
    {
        if (loadcommand == SIO_MODEMCMD_LOAD_HANDLER)
        {
            firmware = FIRMWARE_850HANDLER;
        }
        else
            return;
    }

    // Load firmware from file
    uint8_t *code;
    int codesize = fnSystem.load_firmware(firmware, &code);
    // NAK if we failed to get this
    if (codesize < 0 || code == NULL)
    {
        sio_nak();
        return;
    }
    // Acknoledge before continuing
    sio_ack();

    // We need a delay here when working in high-speed mode.
    // Doesn't negatively affect normal speed operation.
    fnSystem.delay_microseconds(DELAY_FIRMWARE_DELIVERY);

    // Send it

    Debug_printf("Modem sending %d bytes of %s code\n", codesize,
                 loadcommand == SIO_MODEMCMD_LOAD_RELOCATOR ? "relocator" : "handler");

    bus_to_computer(code, codesize, false);

    // Free the buffer!
    free(code);
    DTR = XMT = RTS = 0;
}

// 0x57 / 'W' - WRITE
void modem::sio_write()
{
    uint8_t ck;

    Debug_println("Modem cmd: WRITE");

    /* AUX1: Bytes in payload, 0-64
       AUX2: NA
       Payload always padded to 64 bytes
    */
    if (cmdFrame.aux1 == 0)
    {
        sio_complete();
    }
    else
    {
        memset(txBuf, 0, sizeof(txBuf));

        ck = bus_to_peripheral(txBuf, 64);

        if (ck != sio_checksum(txBuf, 64))
        {
            sio_error();
        }
        else
        {
            if (cmdMode == true)
            {
                cmdOutput = false;
                cmd.assign((char *)txBuf, cmdFrame.aux1);

                if (cmd == "ATA\r")
                    answerHack = true;
                else
                    modemCommand();

                cmdOutput = true;
            }
            else
            {
                if (tcpClient.connected())
                    tcpClient.write(txBuf, cmdFrame.aux1);
            }

            sio_complete();
        }
    }
}

// 0x53 / 'S' - STATUS
void modem::sio_status()
{

    Debug_println("Modem cmd: STATUS");

    /* AUX1: NA
       AUX2: NA
       First payload uint8_t = error status bits
      Second payload uint8_t = handshake state bits
             00 Always low since last check
             01 Currently low, but has been high since last check
             10 Currently high, but has been low since last check
             11 Always high since last check
        7,6: DSR state
        5,4: CTS state
        3,2: CTX state
          1: 0
          0: RCV state (0=space, 1=mark)
    */

    memset(mdmStatus, 0, sizeof(mdmStatus));

    mdmStatus[1] &= 0b00111111;
    mdmStatus[1] |= (tcpClient.connected() == true || tcpServer.hasClient() == true ? 192 : 0);

    mdmStatus[1] &= 0b11110011;
    mdmStatus[1] |= (tcpClient.connected() == true || tcpServer.hasClient() ? 12 : 0);

    mdmStatus[1] &= 0b11111110;
    mdmStatus[1] |= ((tcpClient.available() > 0) || (tcpServer.hasClient() == true) ? 1 : 0);

    if (autoAnswer == true && tcpServer.hasClient())
    {
        modemActive = true;
        answered = false;
        answerTimer = fnSystem.millis();
    }

    Debug_printf("modem::sio_status(%02x,%02x)\n", mdmStatus[0], mdmStatus[1]);

    bus_to_computer(mdmStatus, sizeof(mdmStatus), false);
}

// 0x41 / 'A' - CONTROL
void modem::sio_control()
{
    /* AUX1: Set control state
        7: Enable DTR (Data Terminal Ready) change (1=change, 0=ignore)
        6: New DTR state (0=negate, 1=assert)
        5: Enable RTS (Request To Send) change
        4: New RTS state
        3: NA
        2: NA
        1 Enable XMT (Transmit) change
        0: New XMT state
      AUX2: NA
    */

    Debug_println("Modem cmd: CONTROL");

    if (cmdFrame.aux1 & 0x02)
    {
        XMT = (cmdFrame.aux1 & 0x01 ? true : false);
        Debug_printf("XMT=%d\n", XMT);
    }

    if (cmdFrame.aux1 & 0x20)
    {
        RTS = (cmdFrame.aux1 & 0x10 ? true : false);
        Debug_printf("RTS=%d\n", RTS);
    }

    if (cmdFrame.aux1 & 0x80)
    {
        DTR = (cmdFrame.aux1 & 0x40 ? true : false);

        Debug_printf("DTR=%d\n", DTR);

        if (DTR == 0 && tcpClient.connected())
        {
            tcpClient.stop(); // Hang up if DTR drops.
            CRX = false;
            cmdMode = true;
            
            if (listenPort > 0)
            {
                // tcpServer.stop();
                // tcpServer.begin(listenPort); // and re-listen if listenPort set.
            }
        }
    }
    // for now, just complete
    sio_complete();
}

// 0x42 / 'B' - CONFIGURE
void modem::sio_config()
{

    Debug_println("Modem cmd: CONFIGURE");

    /* AUX1:
         7: Stop bits (0=1, 1=2)
         6: NA
       4,5: Word size (00=5,01=6,10=7,11=8)
       3-0: Baud rate:
    */
#define BAUD_300 0x8
#define BAUD_600 0x9
#define BAUD_1200 0xA
#define BAUD_1800 0xB
#define BAUD_2400 0xC
#define BAUD_4800 0xD
#define BAUD_9600 0xE
#define BAUD_19200 0xF
    /*
       AUX2:
       7-3: NA
         2: Watch DSR line (0=ignore, 1=block writes when line negated)
         1: Watch CTS line
         0: Watch CRX line
    */
    // Complete and then set newbaud
    sio_complete();

    uint8_t newBaud = 0x0F & cmdFrame.aux1; // Get baud rate
    //uint8_t wordSize = 0x30 & cmdFrame.aux1; // Get word size
    //uint8_t stopBit = (1 << 7) & cmdFrame.aux1; // Get stop bits

    // Do not reset MODEM baud rate if locked.
    if (baudLock == true)
        return;

    switch (newBaud)
    {
    case BAUD_300:
        modemBaud = 300;
        break;
    case BAUD_600:
        modemBaud = 600;
        break;
    case BAUD_1200:
        modemBaud = 1200;
        break;
    case BAUD_1800:
        modemBaud = 1800;
        break;
    case BAUD_2400:
        modemBaud = 2400;
        break;
    case BAUD_4800:
        modemBaud = 4800;
        break;
    case BAUD_9600:
        modemBaud = 9600;
        break;
    case BAUD_19200:
        modemBaud = 19200;
        break;
    default:
        Debug_printf("Unexpected baud value: %hu", newBaud);
        modemBaud = 300;
        break;
    }
}

// 0x44 / 'D' - Dump
void modem::sio_set_dump()
{
    modemSniffer->setEnable(cmdFrame.aux1);
    sio_complete();
}

// 0x58 / 'X' - STREAM
void modem::sio_stream()
{
    Debug_println("Modem cmd: STREAM");
    /* AUX1: I/O direction
        7-2: NA
          1: Read from 850 direction enable
          0: Write to 850 direction enable
      AUX2: NA

      RESPONSE
      Payload: 9 bytes to configure POKEY baud rate ($D200-$D208)
    */
    char response[] = {0x28, 0xA0, 0x00, 0xA0, 0x28, 0xA0, 0x00, 0xA0, 0x78}; // 19200

    switch (modemBaud)
    {
    case 300:
        response[0] = response[4] = 0xA0;
        response[2] = response[6] = 0x0B;
        break;
    case 600:
        response[0] = response[4] = 0xCC;
        response[2] = response[6] = 0x05;
        break;
    case 1200:
        response[0] = response[4] = 0xE3;
        response[2] = response[6] = 0x02;
        break;
    case 1800:
        response[0] = response[4] = 0xEA;
        response[2] = response[6] = 0x01;
        break;
    case 2400:
        response[0] = response[4] = 0x6E;
        response[2] = response[6] = 0x01;
        break;
    case 4800:
        response[0] = response[4] = 0xB3;
        response[2] = response[6] = 0x00;
        break;
    case 9600:
        response[0] = response[4] = 0x56;
        response[2] = response[6] = 0x00;
        break;
    case 19200:
        response[0] = response[4] = 0x28;
        response[2] = response[6] = 0x00;
        break;
    }

    bus_to_computer((uint8_t *)response, sizeof(response), false);

    get_uart()->set_baudrate(modemBaud);
    modemActive = true;
    Debug_printf("Modem streaming at %u baud\n", modemBaud);
}

/**
 * Set listen port
 */
void modem::sio_listen()
{
    if (listenPort != 0)
    {
        tcpClient.stop();
        tcpServer.stop();
    }

    listenPort = cmdFrame.aux2 * 256 + cmdFrame.aux1;

    if (listenPort < 1)
        sio_nak();
    else
        sio_ack();

    tcpServer.setMaxClients(1);
    int res = tcpServer.begin(listenPort);
    if (res == 0)
    {
        sio_error();
    }
    else
    {
        sio_complete();
    }
}

/**
 * Stop listen
 */
void modem::sio_unlisten()
{
    sio_ack();
    tcpClient.stop();
    tcpServer.stop();
    sio_complete();
}

/**
 * Lock MODEM baud rate to last configured value
 */
void modem::sio_baudlock()
{
    sio_ack();
    baudLock = (cmdFrame.aux1 > 0 ? true : false);
    modemBaud = sio_get_aux();

    Debug_printf("baudLock: %d\n", baudLock);

    sio_complete();
}

/**
 * enable/disable auto-answer
 */
void modem::sio_autoanswer()
{
    sio_ack();
    autoAnswer = (cmdFrame.aux1 > 0 ? true : false);

    Debug_printf("autoanswer: %d\n", autoAnswer);

    sio_complete();
}

void modem::at_connect_resultCode(int modemBaud)
{
    int resultCode = 0;
    switch (modemBaud)
    {
    case 300:
        resultCode = 1;
        break;
    case 1200:
        resultCode = 5;
        break;
    case 2400:
        resultCode = 10;
        break;
    case 4800:
        resultCode = 18;
        break;
    case 9600:
        resultCode = 13;
        break;
    case 19200:
        resultCode = 85;
        break;
    default:
        resultCode = 1;
        break;
    }
    get_uart()->print(resultCode);
    get_uart()->write(ASCII_CR);
}

/**
 * Emit result code if ATV0
 * No Atascii translation here, as this is intended for machine reading.
 */
void modem::at_cmd_resultCode(int resultCode)
{
    get_uart()->print(resultCode);
    get_uart()->write(ASCII_CR);
    get_uart()->write(ASCII_LF);
}

/**
   replacement println for AT that is CR/EOL aware
*/
void modem::at_cmd_println()
{
    if (cmdOutput == false)
        return;

    if (cmdAtascii == true)
    {
        get_uart()->write(ATASCII_EOL);
    }
    else
    {
        get_uart()->write(ASCII_CR);
        get_uart()->write(ASCII_LF);
    }
    get_uart()->flush();
}

void modem::at_cmd_println(const char *s, bool addEol)
{
    if (cmdOutput == false)
        return;

    get_uart()->print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            get_uart()->write(ATASCII_EOL);
        }
        else
        {
            get_uart()->write(ASCII_CR);
            get_uart()->write(ASCII_LF);
        }
    }
    get_uart()->flush();
}

void modem::at_cmd_println(int i, bool addEol)
{
    if (cmdOutput == false)
        return;

    get_uart()->print(i);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            get_uart()->write(ATASCII_EOL);
        }
        else
        {
            get_uart()->write(ASCII_CR);
            get_uart()->write(ASCII_LF);
        }
    }
    get_uart()->flush();
}

void modem::at_cmd_println(std::string s, bool addEol)
{
    if (cmdOutput == false)
        return;

    get_uart()->print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            get_uart()->write(ATASCII_EOL);
        }
        else
        {
            get_uart()->write(ASCII_CR);
            get_uart()->write(ASCII_LF);
        }
    }
    get_uart()->flush();
}

void modem::at_handle_wificonnect()
{
    int keyIndex = cmd.find(',');
    std::string ssid, key;
    if (keyIndex != std::string::npos)
    {
        ssid = cmd.substr(13, keyIndex - 13 + 1);
        //key = cmd.substring(keyIndex + 1, cmd.length());
        key = cmd.substr(keyIndex + 1);
    }
    else
    {
        //ssid = cmd.substring(6, cmd.length());
        ssid = cmd.substr(6);
        key = "";
    }

    at_cmd_println(HELPWIFICONNECTING, false);
    at_cmd_println(ssid, false);
    at_cmd_println("/", false);
    at_cmd_println(key);

    fnWiFi.connect(ssid.c_str(), key.c_str());

    int retries = 0;
    while ((!fnWiFi.connected()) && retries < 20)
    {
        fnSystem.delay(1000);
        retries++;
        at_cmd_println(".", false);
    }
    if (retries >= 20)
    {
        if (numericResultCode == true)
        {
            at_cmd_resultCode(RESULT_CODE_ERROR);
        }
        else
        {
            at_cmd_println("ERROR");
        }
    }
    else
    {
        if (numericResultCode == true)
        {
            at_cmd_resultCode(RESULT_CODE_OK);
        }
        else
        {
            at_cmd_println("OK");
        }
    }
}

void modem::at_handle_port()
{
    //int port = cmd.substring(6).toInt();
    int port = std::stoi(cmd.substr(6));
    if (port > 65535 || port < 0)
    {
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_ERROR);
        else
            at_cmd_println("ERROR");
    }
    else
    {
        if (listenPort != 0)
        {
            tcpClient.stop();
            tcpServer.stop();
        }

        listenPort = port;
        tcpServer.setMaxClients(1);
        int res = tcpServer.begin(listenPort);
        if (res == 0)
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_ERROR);
            else
                at_cmd_println("ERROR");
        }
        else {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_OK);
            else
                at_cmd_println("OK");
        }
    }
}

void modem::at_handle_get()
{
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.find(':', 12); // Index where port number might begin
    int pathIndex = cmd.find('/', 12); // Index first host name and possible port ends and path begins
    int port;
    std::string path, host;
    if (pathIndex < 0)
    {
        pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
        port = 80;
        portIndex = pathIndex;
    }
    else
    {
        //port = cmd.substring(portIndex + 1, pathIndex).toInt();
        port = std::stoi(cmd.substr(portIndex + 1, pathIndex - (portIndex + 1) + 1));
    }
    //host = cmd.substring(12, portIndex);
    host = cmd.substr(12, portIndex - 12 + 1);
    //path = cmd.substring(pathIndex, cmd.length());
    path = cmd.substr(pathIndex);
    if (path.empty())
        path = "/";

    // Establish connection
    if (!tcpClient.connect(host.c_str(), port))
    {
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_NO_CARRIER);
        else
            at_cmd_println("NO CARRIER");
        telnet_free(telnet);
        telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
        CRX = false;
    }
    else
    {
        if (numericResultCode == true)
        {
            at_connect_resultCode(modemBaud);
            CRX = true;
        }
        else
        {
            at_cmd_println("CONNECT ", false);
            at_cmd_println(modemBaud);
            CRX = true;
        }

        cmdMode = false;

        // Send a HTTP request before continuing the connection as usual
        std::string request = "GET ";
        request += path;
        request += " HTTP/1.1\r\nHost: ";
        request += host;
        request += "\r\nConnection: close\r\n\r\n";
        tcpClient.write(request);
    }
}

void modem::at_handle_help()
{
    at_cmd_println(HELPL01);
    at_cmd_println(HELPL02);
    at_cmd_println(HELPL03);
    at_cmd_println(HELPL04);
    at_cmd_println(HELPL05);
    at_cmd_println(HELPL06);
    at_cmd_println(HELPL07);
    at_cmd_println(HELPL08);
    at_cmd_println(HELPL09);
    at_cmd_println(HELPL10);
    at_cmd_println(HELPL11);
    at_cmd_println(HELPL12);
    at_cmd_println(HELPL13);
    at_cmd_println(HELPL14);
    at_cmd_println(HELPL15);
    at_cmd_println(HELPL16);
    at_cmd_println(HELPL17);
    at_cmd_println(HELPL18);
    at_cmd_println(HELPL19);
    at_cmd_println(HELPL20);
    at_cmd_println(HELPL21);
    at_cmd_println(HELPL22);
    at_cmd_println(HELPL23);
    at_cmd_println(HELPL24);
    at_cmd_println(HELPL25);
    at_cmd_println(HELPL26);

    at_cmd_println();

    if (listenPort > 0)
    {
        at_cmd_println(HELPPORT1, false);
        at_cmd_println(listenPort);
        //at_cmd_println(HELPPORT2);
        //at_cmd_println(HELPPORT3);
    }
    else
    {
        at_cmd_println(HELPPORT4);
    }
    //at_cmd_println();

    if (numericResultCode == true)
        at_cmd_resultCode(RESULT_CODE_OK);
    else
        at_cmd_println("OK");
}

void modem::at_handle_wifilist()
{
    at_cmd_println();
    at_cmd_println(HELPSCAN1);

    int n = fnWiFi.scan_networks();

    at_cmd_println();

    if (n == 0)
    {
        at_cmd_println(HELPSCAN2);
    }
    else
    {
        at_cmd_println(n, false);
        at_cmd_println(HELPSCAN3);
        at_cmd_println();

        char ssid[32];
        char bssid[18];
        uint8_t rssi;
        uint8_t channel;
        uint8_t encryption;

        for (int i = 0; i < n; ++i)
        {
            // Print SSID and RSSI for each network found
            fnWiFi.get_scan_result(i, ssid, &rssi, &channel, bssid, &encryption);
            at_cmd_println(i + 1, false);
            at_cmd_println(": ", false);
            at_cmd_println(ssid, false);
            at_cmd_println(" [", false);
            at_cmd_println(channel, false);
            at_cmd_println("/", false);
            at_cmd_println(rssi, false);
            at_cmd_println("]");
            at_cmd_println("    ", false);
            at_cmd_println(bssid, false);
            at_cmd_println(encryption == WIFI_AUTH_OPEN ? HELPSCAN4 : HELPSCAN5);
        }
    }
    at_cmd_println();

    if (numericResultCode == true)
        at_cmd_resultCode(RESULT_CODE_OK);
    else
        at_cmd_println("OK");
}

void modem::at_handle_answer()
{
    Debug_printf("HANDLE ANSWER !!!\n");
    if (tcpServer.hasClient())
    {
        tcpClient = tcpServer.available();
        tcpClient.setNoDelay(true); // try to disable naggle
                                    //        tcpServer.stop();
        answerTimer = fnSystem.millis();
        answered = false;
        CRX = true;

        cmdMode = false;
        get_uart()->flush();
        answerHack = false;
    }
}

void modem::at_handle_dial()
{
    int portIndex = cmd.find(':');
    std::string host, port;
    std::string hostpb;
    if (portIndex != std::string::npos)
    {
        host = cmd.substr(4, portIndex - 4);
        port = cmd.substr(portIndex + 1);
    }
    else
    {
        host = cmd.substr(4);
        port = "23"; // Telnet default
    }

    util_string_trim(host); // allow spaces or no spaces after AT command

    Debug_printf("DIALING: %s\n", host.c_str());

    /*Phonebook Entry?, check first if the only numeric host*/
    if (host.find_first_not_of("0123456789") == std::string::npos)
    {
        hostpb = Config.get_pb_host_name(host.c_str());
        /*Check if the number is in the phonebook*/
        if (!hostpb.empty())
        {

            /*replace host:port with phonebook information*/
            port = Config.get_pb_host_port(host.c_str());
            host = hostpb;
        }
    }

    if (host == "5551234") // Fake it for BobTerm
    {
        CRX = true;
        answered = false;
        answerTimer = fnSystem.millis();
        // This is so macros in Bobterm can do the actual connect.
        fnSystem.delay(ANSWER_TIMER_MS);
        at_cmd_println("CONNECT ", false);
        at_cmd_println(modemBaud);
    }
    else
    {
        at_cmd_println("Connecting to ", false);
        at_cmd_println(host, false);
        at_cmd_println(":", false);
        at_cmd_println(port);

        int portInt = std::stoi(port);

        if (tcpClient.connect(host.c_str(), portInt))
        {
            tcpClient.setNoDelay(true); // Try to disable naggle
            answered = false;
            answerTimer = fnSystem.millis();
            cmdMode = false;
        }
        else
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_NO_CARRIER);
            else
                at_cmd_println("NO CARRIER");
            CRX = false;
            telnet_free(telnet);
            telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
        }
    }
}
/*Following functions manage the phonebook*/
/*Display current Phonebook*/
void modem::at_handle_pblist()
{
    at_cmd_println();
    at_cmd_println("Phone#       Host");
    for (int i = 0; i < MAX_PB_SLOTS; ++i)
    {
        // Check if empty
        std::string pbEntry = Config.get_pb_entry(i);
        if (!pbEntry.empty())
            at_cmd_println(pbEntry);
    }
    at_cmd_println();

    if (numericResultCode == true)
        at_cmd_resultCode(RESULT_CODE_OK);
    else
        at_cmd_println("OK");
}

/*Add and del entry in the phonebook*/
void modem::at_handle_pb()
{
    // From the AT command get the info to add. Ex: atpb4321=irata.online:8002
    //or delete ex: atpb4321
    // ("ATPB" length 4)
    std::string phnumber, host, port;
    int hostIndex = cmd.find('=');
    int portIndex = cmd.find(':');

    //Equal symbol found, so assume adding entry
    if (hostIndex != std::string::npos)
    {
        phnumber = cmd.substr(4, hostIndex - 4);
        //Check pure numbers entry
        if (phnumber.find_first_not_of("0123456789") == std::string::npos)
        {
            if (portIndex != std::string::npos)
            {
                host = cmd.substr(hostIndex + 1, portIndex - hostIndex - 1);
                port = cmd.substr(portIndex + 1);
            }
            else
            {
                host = cmd.substr(hostIndex + 1);
                port = "23";
            }
            if (Config.add_pb_number(phnumber.c_str(), host.c_str(), port.c_str()))
            {
                if (numericResultCode == true)
                    at_cmd_resultCode(RESULT_CODE_OK);
                else
                    at_cmd_println("OK");
            }
            else
            {
                if (numericResultCode == true)
                    at_cmd_resultCode(RESULT_CODE_ERROR);
                else
                    at_cmd_println("ERROR");
            }
        }
        else
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_ERROR);
            else
                at_cmd_println("ERROR");
        }
    }
    //No Equal symbol present, so Delete an entry
    else
    {
        std::string phnumber = cmd.substr(4);
        // Check here if no number and skip delete? https://forums.atariage.com/topic/309560-fujinet-testing-and-bug-reporting-thread/?do=findComment&comment=5252703
        if (Config.del_pb_number(phnumber.c_str()))
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_OK);
            else
                at_cmd_println("OK");
        }
        else
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_ERROR);
            else
                at_cmd_println("ERROR");
        }
    }
}

/*
   Perform a command given in AT Modem command mode
*/
void modem::modemCommand()
{
    /* Some of these are ignored; to see their meanings,
     * review `modem.h`'s sioModem class's _at_cmds enums. */
    static const char *at_cmds[_at_cmds::AT_ENUMCOUNT] =
        {
            "AT",
            "ATNET0",
            "ATNET1",
            "ATA",
            "ATIP",
            "AT?",
            "ATH",
            "+++ATH",
            "ATDT",
            "ATDP",
            "ATDI",
            "ATWIFILIST",
            "ATWIFICONNECT",
            "ATGET",
            "ATPORT",
            "ATV0",
            "ATV1",
            "AT&F",
            "ATS0=0",
            "ATS0=1",
            "ATS2=43",
            "ATS5=8",
            "ATS6=2",
            "ATS7=30",
            "ATS12=20",
            "ATE0",
            "ATE1",
            "ATM0",
            "ATM1",
            "ATX1",
            "AT&C1",
            "AT&D2",
            "AT&W",
            "ATH2",
            "+++ATZ",
            "ATS2=128 X1 M0",
            "AT+SNIFF",
            "AT-SNIFF",
            "AT+TERM=VT52",
            "AT+TERM=VT100",
            "AT+TERM=ANSI",
            "AT+TERM=DUMB",
            "ATCPM",
            "ATPBLIST",
            "ATPBCLEAR",
            "ATPB",
            "ATO"};

    //cmd.trim();
    util_string_trim(cmd);
    if (cmd.empty())
        return;

    std::string upperCaseCmd = cmd;
    //upperCaseCmd.toUpperCase();
    util_string_toupper(upperCaseCmd);

    if (commandEcho == true)
        at_cmd_println();

    Debug_printf("AT Cmd: %s\n", upperCaseCmd.c_str());

    // Replace EOL with CR
    //if (upperCaseCmd.indexOf(ATASCII_EOL) != 0)
    //    upperCaseCmd[upperCaseCmd.indexOf(ATASCII_EOL)] = ASCII_CR;
    int eol1 = upperCaseCmd.find(ATASCII_EOL);
    if (eol1 != std::string::npos)
        upperCaseCmd[eol1] = ASCII_CR;

    // Just AT
    int cmd_match = AT_ENUMCOUNT;
    if (upperCaseCmd.compare("AT") == 0)
    {
        cmd_match = AT_AT;
    }
    else
    {
        // Make sure we skip the plain AT command when matching
        for (cmd_match = _at_cmds::AT_AT + 1; cmd_match < _at_cmds::AT_ENUMCOUNT; cmd_match++)
            if (upperCaseCmd.compare(0, strlen(at_cmds[cmd_match]), at_cmds[cmd_match]) == 0)
                break;
    }

    switch (cmd_match)
    {
    // plain AT
    case AT_AT:
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_OFFHOOK: // Off hook, should be ignored.
    // hangup
    case AT_H:
    case AT_H1:
        if (tcpClient.connected() == true)
        {
            tcpClient.flush();
            tcpClient.stop();
            cmdMode = true;
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_NO_CARRIER);
            else
                at_cmd_println("NO CARRIER");
            telnet_free(telnet);
            telnet = telnet_init(telopts, _telnet_event_handler, 0, this);

            CRX = false;

            if (listenPort > 0)
            {
                //                tcpServer.stop();
                //                tcpServer.begin(listenPort);
            }
        }
        else
        {
            if (numericResultCode == true)
                at_cmd_resultCode(RESULT_CODE_OK);
            else
                at_cmd_println("OK");
        }
        break;
    // dial to host
    case AT_DT:
    case AT_DP:
    case AT_DI:
        at_handle_dial();
        break;
    case AT_WIFILIST:
        at_handle_wifilist();
        break;
    case AT_WIFICONNECT:
        at_handle_wificonnect();
        break;
    // Change telnet mode
    case AT_NET0:
        use_telnet = false;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_NET1:
        use_telnet = true;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_A:
        at_handle_answer();
        break;
    // See my IP address
    case AT_IP:
        if (fnWiFi.connected())
            at_cmd_println(fnSystem.Net.get_ip4_address_str());
        else
            at_cmd_println(HELPNOWIFI);
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_HELP:
        at_handle_help();
        break;
    case AT_GET:
        at_handle_get();
        break;
    case AT_PORT:
        at_handle_port();
        break;
    case AT_V0:
        at_cmd_resultCode(RESULT_CODE_OK);
        numericResultCode = true;
        break;
    case AT_V1:
        at_cmd_println("OK");
        numericResultCode = false;
        break;
    case AT_S0E0:
        autoAnswer = false;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_S0E1:
        autoAnswer = true;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_E0:
        commandEcho = false;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_E1:
        commandEcho = true;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_ANDF_ignored: // These are all ignored.
    case AT_S2E43_ignored:
    case AT_S5E8_ignored:
    case AT_S6E2_ignored:
    case AT_S7E30_ignored:
    case AT_S12E20_ignored:
    case AT_M0_ignored:
    case AT_M1_ignored:
    case AT_X1_ignored:
    case AT_AC1_ignored:
    case AT_AD2_ignored:
    case AT_AW_ignored:
    case AT_ZPPP_ignored:
    case AT_BBSX_ignored:
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_SNIFF:
        get_modem_sniffer()->setEnable(true);
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_UNSNIFF:
        get_modem_sniffer()->setEnable(false);
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_TERMVT52:
        term_type = "VT52";
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_TERMVT100:
        term_type = "VT100";
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_TERMDUMB:
        term_type = "DUMB";
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_TERMANSI:
        term_type = "ANSI";
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_CPM:
        modemActive = false;
        SIO.getCPM()->init_cpm(modemBaud);
        SIO.getCPM()->cpmActive = true;
        break;
    case AT_PHONEBOOKLIST:
        at_handle_pblist();
        break;
    case AT_PHONEBOOK:
        at_handle_pb();
        break;
    case AT_PHONEBOOKCLR:
        Config.clear_pb();
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
        break;
    case AT_O:
        if (tcpClient.connected())
        {
            if (numericResultCode == true)
            {
                at_cmd_resultCode(modemBaud);
            }
            else
            {
                at_cmd_println("CONNECT ", false);
                at_cmd_println(modemBaud);
            }
            cmdMode = false;
        }
        else
        {
            if (numericResultCode == true)
            {
                at_cmd_resultCode(RESULT_CODE_OK);
            }
            else
            {
                at_cmd_println("OK");
            }
        }
        break;
    default:
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_ERROR);
        else
            at_cmd_println("ERROR");
        break;
    }

    cmd = "";
}

/*
  Handle incoming & outgoing data for modem
*/
void modem::sio_handle_modem()
{
    /**** AT command mode ****/
    if (cmdMode == true)
    {
        if (answerHack == true)
        {
            Debug_printf("XXX ANSWERHACK !!! SENDING ATA! ");
            cmd = "ATA";
            modemCommand();
            answerHack = false;
            return;
        }

        // In command mode but new unanswered incoming connection on server listen socket
        if ((listenPort > 0) && (tcpServer.hasClient()))
        {
            if (autoAnswer == true)
            {
                at_handle_answer();
            }
            else
            {
                if ((fnSystem.millis() - lastRingMs) > RING_INTERVAL)
                {
                    if (ringCount < RING_TIMEOUT)
                    {
                        // Print RING every now and then while the new incoming connection exists
                        if (numericResultCode == true)
                            at_cmd_resultCode(RESULT_CODE_RING);
                        else
                            at_cmd_println("RING");
                        lastRingMs = fnSystem.millis();
                        ringCount++;
                    }
                    else
                    {
                        // Answer and hangup since host system did not pickup
                        fnTcpClient c = tcpServer.accept();
                        c.write("The host system did not answer. Please try again later.\x0d\x0a\x9b");
                        c.stop();
                        ringCount = 0;
                    }
                }
            }
        }
        else
        {
            ringCount = 0; // Keep the counter reset
        }

        // In command mode - don't exchange with TCP but gather characters to a string
        //if (SIO_UART.available() /*|| blockWritePending == true */ )
        if (get_uart()->available() > 0)
        {
            // get char from Atari SIO
            //char chr = SIO_UART.read();
            char chr = get_uart()->read();

            // Return, enter, new line, carriage return.. anything goes to end the command
            if ((chr == ASCII_LF) || (chr == ASCII_CR) || (chr == ATASCII_EOL))
            {
                // flip which EOL to display based on last CR or EOL received.
                if (chr == ATASCII_EOL)
                    cmdAtascii = true;
                else
                    cmdAtascii = false;

                modemCommand();
            }
            // Backspace or delete deletes previous character
            else if ((chr == ASCII_BACKSPACE) || (chr == ASCII_DELETE))
            {
                size_t len = cmd.length();

                if (len > 0)
                {
                    cmd.erase(len - 1);
                    // We don't assume that backspace is destructive
                    // Clear with a space
                    if (commandEcho == true)
                    {
                        get_uart()->write(ASCII_BACKSPACE);
                        get_uart()->write(' ');
                        get_uart()->write(ASCII_BACKSPACE);
                    }
                }
            }
            else if (chr == ATASCII_BACKSPACE)
            {
                size_t len = cmd.length();

                // ATASCII backspace
                if (len > 0)
                {
                    cmd.erase(len - 1);
                    if (commandEcho == true)
                        get_uart()->write(ATASCII_BACKSPACE);
                }
            }
            // Take into account arrow key movement and clear screen
            else if (chr == ATASCII_CLEAR_SCREEN ||
                     ((chr >= ATASCII_CURSOR_UP) && (chr <= ATASCII_CURSOR_RIGHT)))
            {
                if (commandEcho == true)
                    get_uart()->write(chr);
            }
            else
            {
                if (cmd.length() < MAX_CMD_LENGTH)
                {
                    //cmd.concat(chr);
                    cmd += chr;
                }
                if (commandEcho == true)
                    get_uart()->write(chr);
            }
        }
    }
    // Connected mode
    else
    {
        // If another client is waiting, accept and turn away.
        if (tcpServer.hasClient())
        {
            fnTcpClient c = tcpServer.accept();
            c.write("The MODEM is currently serving another caller. Please try again later.\x0d\x0a\x9b");
            c.stop();
        }

        // Emit a CONNECT if we're connected, and a few seconds have passed.
        if ((answered == false) && (answerTimer > 0) && ((fnSystem.millis() - answerTimer) > ANSWER_TIMER_MS))
        {
            answered = true;
            answerTimer = 0;
            if (numericResultCode == true)
            {
                at_cmd_resultCode(modemBaud);
            }
            else
            {
                at_cmd_println("CONNECT ", false);
                at_cmd_println(modemBaud);
            }
        }

        int sioBytesAvail = get_uart()->available();
        //int sioBytesAvail = min(0, get_uart()->available());

        // send from Atari to Fujinet
        if (sioBytesAvail && tcpClient.connected())
        {
            // In telnet in worst case we have to escape every uint8_t
            // so leave half of the buffer always free
            //int max_buf_size;
            //if (telnet == true)
            //  max_buf_size = TX_BUF_SIZE / 2;
            //else
            //  max_buf_size = TX_BUF_SIZE;

            // Read from serial, the amount available up to
            // maximum size of the buffer
            int sioBytesRead = get_uart()->readBytes(&txBuf[0], //SIO_UART.readBytes(&txBuf[0],
                                                   (sioBytesAvail > TX_BUF_SIZE) ? TX_BUF_SIZE : sioBytesAvail);

            // Disconnect if going to AT mode with "+++" sequence
            for (int i = 0; i < (int)sioBytesRead; i++)
            {
                if (txBuf[i] == '+')
                    plusCount++;
                else
                    plusCount = 0;
                if (plusCount >= 3)
                {
                    plusTime = fnSystem.millis();
                }
                if (txBuf[i] != '+')
                {
                    plusCount = 0;
                }
            }

            // Write the buffer to TCP finally
            if (use_telnet == true)
            {
                telnet_send(telnet, (const char *)txBuf, sioBytesRead);
            }
            else
                tcpClient.write(&txBuf[0], sioBytesRead);

            // And send it off to the sniffer, if enabled.
            modemSniffer->dumpOutput(&txBuf[0], sioBytesRead);
            _lasttime = fnSystem.millis();
        }

        // read from Fujinet to Atari
        unsigned char buf[RECVBUFSIZE];
        int bytesAvail = 0;

        // check to see how many bytes are avail to read
        while ((bytesAvail = tcpClient.available()) > 0)
        {
            // read as many as our buffer size will take (RECVBUFSIZE)
            unsigned int bytesRead =
                tcpClient.read(buf, (bytesAvail > RECVBUFSIZE) ? RECVBUFSIZE : bytesAvail);

            if (use_telnet == true)
            {
                telnet_recv(telnet, (const char *)buf, bytesRead);
            }
            else
            {
                get_uart()->write(buf, bytesRead);
                get_uart()->flush();
            }

            // And dump to sniffer, if enabled.
            modemSniffer->dumpInput(buf, bytesRead);
            _lasttime = fnSystem.millis();
        }
    }

    // If we have received "+++" as last bytes from serial port and there
    // has been over a second without any more bytes, go back to command mode.
    if (plusCount >= 3)
    {
        if (fnSystem.millis() - plusTime > 1000)
        {
            Debug_println("Going back to command mode");

            at_cmd_println("OK");
    
            cmdMode = true;

            plusCount = 0;
        }
    }

    // Go to command mode if TCP disconnected and not in command mode
    if (!tcpClient.connected() && (cmdMode == false) && (DTR == 0))
    {
        tcpClient.flush();
        tcpClient.stop();
        cmdMode = true;
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_NO_CARRIER);
        else
            at_cmd_println("NO CARRIER");
        telnet_free(telnet);
        telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
        CRX = false;
        if (listenPort > 0)
        {
            // tcpServer.stop();
            // tcpServer.begin(listenPort);
        }
    }
    else if ((!tcpClient.connected()) && (cmdMode == false))
    {
        cmdMode = true;
        telnet_free(telnet);
        telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_NO_CARRIER);
        else
            at_cmd_println("NO CARRIER");
        telnet_free(telnet);
        telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
        CRX = false;
        if (listenPort > 0)
        {
            // tcpServer.stop();
            // tcpServer.begin(listenPort);
        }
    }
}

void modem::shutdown()
{
    if (modemSniffer != nullptr)
        if (modemSniffer->getEnable())
            modemSniffer->closeOutput();
}

/*
  Process command
*/
void modem::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    if (!Config.get_modem_enabled())
        Debug_println("modem::disabled, ignoring");
    else
    {
        Debug_println("modem::sio_process() called");

        switch (cmdFrame.comnd)
        {
        case SIO_MODEMCMD_LOAD_RELOCATOR:
            Debug_printf("MODEM $21 RELOCATOR #%d\n", ++count_ReqRelocator);
            sio_send_firmware(cmdFrame.comnd);
            break;

        case SIO_MODEMCMD_LOAD_HANDLER:
            Debug_printf("MODEM $26 HANDLER DL #%d\n", ++count_ReqHandler);
            sio_send_firmware(cmdFrame.comnd);
            break;

        case SIO_MODEMCMD_TYPE1_POLL:
            Debug_printf("MODEM TYPE 1 POLL #%d\n", ++count_PollType1);
            // The 850 is only supposed to respond to this if AUX1 = 1 or on the 26th poll attempt
            if (cmdFrame.aux1 == 1 || count_PollType1 == 16)
            {
                sio_poll_1();
                count_PollType1 = 0; // Reset the counter so we can respond again if asked
            }
            break;

        case SIO_MODEMCMD_TYPE3_POLL:
            sio_poll_3(cmdFrame.device, cmdFrame.aux1, cmdFrame.aux2);
            break;

        case SIO_MODEMCMD_CONTROL:
            sio_ack();
            sio_control();
            break;
        case SIO_MODEMCMD_CONFIGURE:
            sio_ack();
            sio_config();
            break;
        case SIO_MODEMCMD_SET_DUMP:
            sio_ack();
            sio_set_dump();
            break;
        case SIO_MODEMCMD_LISTEN:
            sio_listen();
            break;
        case SIO_MODEMCMD_UNLISTEN:
            sio_unlisten();
            break;
        case SIO_MODEMCMD_BAUDRATELOCK:
            sio_baudlock();
            break;
        case SIO_MODEMCMD_AUTOANSWER:
            sio_autoanswer();
            break;
        case SIO_MODEMCMD_STATUS:
            sio_ack();
            sio_status();
            break;
        case SIO_MODEMCMD_WRITE:
            sio_ack();
            sio_write();
            break;
        case SIO_MODEMCMD_STREAM:
            sio_ack();
            sio_stream();
            break;
        default:
            sio_nak();
        }
    }
}