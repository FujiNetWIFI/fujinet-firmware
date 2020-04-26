#include <FS.h>
#include <SPIFFS.h>

#include "modem.h"
#include "fnWiFi.h"
#include "../../include/atascii.h"

#define RECVBUFSIZE 1024

#define SIO_MODEMCMD_LOAD_RELOCATOR 0x21
#define SIO_MODEMCMD_LOAD_HANDLER   0x26
#define SIO_MODEMCMD_TYPE1_POLL     0x3F
#define SIO_MODEMCMD_TYPE3_POLL     0x40
#define SIO_MODEMCMD_CONTROL        0x41
#define SIO_MODEMCMD_CONFIGURE      0x42
#define SIO_MODEMCMD_LISTEN         0x4C
#define SIO_MODEMCMD_UNLISTEN       0x4D
#define SIO_MODEMCMD_STATUS         0x53
#define SIO_MODEMCMD_WRITE          0x57
#define SIO_MODEMCMD_STREAM         0x58

#define FIRMWARE_850RELOCATOR "/850relocator.bin"
#define FIRMWARE_850HANDLER   "/850handler.bin"

#ifdef ESP8266
void sioModem::sioModem()
{
}
#endif

/*
    If buffer is NULL, simply returns size of file
*/
int sioModem::load_firmware(const char * filename, char **buffer)
{
#ifdef DEBUG
    Debug_printf("load_firmware '%s'\n", filename);
#endif    
    if(SPIFFS.exists(filename) == false)
    {
#ifdef DEBUG
        Debug_println("load_firmware FILE NOT FOUND");
#endif    
        return -1;
    }

    File f = SPIFFS.open(filename);
    size_t file_size = f.size();
#ifdef DEBUG
        Debug_printf("load_firmware file size = %u\n", file_size);
#endif

    if(buffer == NULL)
    {
        f.close();
        return file_size;
    }

    int bytes_read = -1;
    char *result = (char *)malloc(file_size);
    if(result == NULL)
    {
#ifdef DEBUG
        Debug_println("load_firmware failed to malloc");
#endif
    }
    else
    {
        bytes_read = f.readBytes(result, file_size);
        if(bytes_read == file_size)
        {
            *buffer = result;
        }
        else
        {
            free(result);
            bytes_read = -1;
#ifdef DEBUG
            Debug_printf("load_firmware only read %u bytes out of %u - failing\n", bytes_read, file_size);
#endif
        }
    }

    f.close();
    return bytes_read;
}

// 0x3F / '?' - TYPE 1 POLL
void sioModem::sio_poll_1()
{
    /*  From Altirra sources - rs232.cpp
        Send back SIO command for booting. This is a 12 byte + chk block that
        is meant to be written to the SIO parameter block starting at DDEVIC ($0300).

		The boot block MUST start at $0500. There are both BASIC-based and cart-based
        loaders that use JSR $0506 to run the loader.
    */

    // Don't respond if we already have previously
    if (count_PollType1 != 0)
        return;
    count_PollType1++;

    // Get size of relocator
    int filesize = load_firmware(FIRMWARE_850RELOCATOR, NULL);
    // Simply return (without ACK) if we failed to get this
    if(filesize < 0)
        return;

    // Acknoledge before continuing
    sio_ack();

    uint8_t bootBlock[12]={
        0x50,		// DDEVIC
        0x01,		// DUNIT
        0x21,		// DCOMND = '!' (boot)
        0x40,		// DSTATS
        0x00, 0x05,	// DBUFLO, DBUFHI == $0500
        0x08,		// DTIMLO = 8 vblanks
        0x00,		// not used
        0x00, 0x00,	// DBYTLO, DBYTHI
        0x00,		// DAUX1
        0x00,		// DAUX2
    };

    // Stuff the size into the block
    uint32_t relsize = (uint32_t)filesize;
    bootBlock[8] = (uint8_t)relsize;
    bootBlock[9] = (uint8_t)(relsize >> 8);

#ifdef DEBUG
    Debug_println("Modem acknowledging Type 1 Poll");
#endif
    sio_to_computer(bootBlock, sizeof(bootBlock), false);
}

// 0x21 / '!' - RELOCATOR DOWNLOAD
// 0x26 / '&' - HANDLER DOWNLOAD
void sioModem::sio_send_firmware(byte loadcommand)
{
    const char * firmware;
    if(loadcommand == SIO_MODEMCMD_LOAD_RELOCATOR)
    {
        firmware = FIRMWARE_850RELOCATOR;
    }
    else
    {
        if(loadcommand == SIO_MODEMCMD_LOAD_HANDLER)
        {
            firmware = FIRMWARE_850HANDLER;
        }
        else
            return;
    }

    // Load firmware from file
    char *code;
    int codesize = load_firmware(firmware, &code);
    // NAK if we failed to get this
    if(codesize < 0 || code == NULL)
    {
        sio_nak();
        return;
    }
    // Acknoledge before continuing
    sio_ack();

    // Send it
#ifdef DEBUG
    Debug_printf("Modem sending %d bytes of %s code\n", codesize, 
            loadcommand == SIO_MODEMCMD_LOAD_RELOCATOR ? "relocator" : "handler");
#endif
    sio_to_computer((byte *)code, codesize, false);

    // Free the buffer!
    free(code);
}

// 0x57 / 'W' - WRITE
void sioModem::sio_write()
{
#ifdef DEBUG
    Debug_println("Modem cmd: WRITE");
#endif    
    /* AUX1: Bytes in payload, 0-64
       AUX2: NA
       Payload always padded to 64 bytes
    */
    // For now, just complete
    sio_complete();
}

// 0x53 / 'S' - STATUS
void sioModem::sio_status()
{
#ifdef DEBUG
    Debug_println("Modem cmd: STATUS");
#endif    
    /* AUX1: NA
       AUX2: NA
       First payload byte = error status bits
      Second payload byte = handshake state bits
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
    byte status[2] = { 0x00, 0x0C };
    sio_to_computer(status, sizeof(status), false);
}

// 0x41 / 'A' - CONTROL
void sioModem::sio_control()
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
#ifdef DEBUG
    Debug_println("Modem cmd: CONTROL");
#endif

    if (cmdFrame.aux1 & 0x02)
    {
        XMT = (cmdFrame.aux1 & 0x01 ? true : false);
#ifdef DEBUG
        Debug_print("XMT=");
        Debug_println(DTR);
#endif
    }

    if (cmdFrame.aux1 & 0x20)
    {
        RTS = (cmdFrame.aux1 & 0x10 ? true : false);
#ifdef DEBUG
        Debug_print("RTS=");
        Debug_println(DTR);
#endif
    }

    if (cmdFrame.aux1 & 0x80)
    {
        DTR = (cmdFrame.aux1 & 0x40 ? true : false);
#ifdef DEBUG
        Debug_print("DTR=");
        Debug_println(DTR);
#endif
    }
    // for now, just complete
    sio_complete();
}

// 0x42 / 'B' - CONFIGURE
void sioModem::sio_config()
{
#ifdef DEBUG
    Debug_println("Modem cmd: CONFIGURE");
#endif    
    /* AUX1:
         7: Stop bits (0=1, 1=2)
         6: NA
       4,5: Word size (00=5,01=6,10=7,11=8)
       3-0: Baud rate:
    */
#define BAUD_300   0x8
#define BAUD_600   0x9
#define BAUD_1200  0xA
#define BAUD_1800  0xB
#define BAUD_2400  0xC
#define BAUD_4800  0xD
#define BAUD_9600  0xE
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

    byte newBaud = 0x0F & cmdFrame.aux1;        // Get baud rate
    //byte wordSize = 0x30 & cmdFrame.aux1; // Get word size
    //byte stopBit = (1 << 7) & cmdFrame.aux1; // Get stop bits

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
#ifdef DEBUG
        Debug_printf("Unexpected baud value: %hu", newBaud);
#endif    
        modemBaud = 300;
        break;
    }
}

// 0x58 / 'X' - STREAM
void sioModem::sio_stream()
{
#ifdef DEBUG
    Debug_println("Modem cmd: STREAM");
#endif
    /* AUX1: I/O direction
        7-2: NA
          1: Read from 850 direction enable
          0: Write to 850 direction enable
      AUX2: NA

      RESPONSE
      Payload: 9 bytes to configure POKEY baud rate ($D200-$D208)
    */
    char response[] = { 0x28, 0xA0, 0x00, 0xA0, 0x28, 0xA0, 0x00, 0xA0, 0x78 }; // 19200

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

    sio_to_computer((byte *) response, sizeof(response), false);
#ifndef ESP32
    SIO_UART.flush();
#endif

    SIO_UART.updateBaudRate(modemBaud);
    modemActive = true;
#ifdef DEBUG
    Debug_printf("Modem streaming at %u baud\n", modemBaud);
#endif
}

/**
 * Set listen port
 */
void sioModem::sio_listen()
{
    if (listenPort != 0)
        tcpServer.close();

    listenPort = cmdFrame.aux2 * 256 + cmdFrame.aux1;

    if (listenPort < 1)
        sio_nak();
    else
        sio_ack();

    tcpServer.begin(listenPort);

    sio_complete();
}

/**
 * Stop listen
 */
void sioModem::sio_unlisten()
{
    sio_ack();
    tcpServer.close();
    sio_complete();
}

/**
   replacement println for AT that is CR/EOL aware
*/
void sioModem::at_cmd_println()
{
    if (cmdAtascii == true)
    {
        SIO_UART.write(ATASCII_EOL);
    }
    else
    {
        SIO_UART.write(ASCII_CR);
        SIO_UART.write(ASCII_LF);
    }
    SIO_UART.flush();
}

void sioModem::at_cmd_println(const char *s, bool addEol)
{
    SIO_UART.print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            SIO_UART.write(ATASCII_EOL);
        }
        else
        {
            SIO_UART.write(ASCII_CR);
            SIO_UART.write(ASCII_LF);
        }
    }
    SIO_UART.flush();
}

void sioModem::at_cmd_println(int i, bool addEol)
{
    SIO_UART.print(i);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            SIO_UART.write(ATASCII_EOL);
        }
        else
        {
            SIO_UART.write(ASCII_CR);
            SIO_UART.write(ASCII_LF);
        }
    }
    SIO_UART.flush();
}

void sioModem::at_cmd_println(String s, bool addEol)
{
    SIO_UART.print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            SIO_UART.write(ATASCII_EOL);
        }
        else
        {
            SIO_UART.write(ASCII_CR);
            SIO_UART.write(ASCII_LF);
        }
    }
    SIO_UART.flush();
}

void sioModem::at_cmd_println(IPAddress ipa, bool addEol)
{
    SIO_UART.print(ipa);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            SIO_UART.write(ATASCII_EOL);
        }
        else
        {
            SIO_UART.write(ASCII_CR);
            SIO_UART.write(ASCII_LF);
        }
    }
    SIO_UART.flush();
}

void sioModem::at_handle_wificonnect()
{
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1)
    {
        ssid = cmd.substring(13, keyIndex);
        key = cmd.substring(keyIndex + 1, cmd.length());
    }
    else
    {
        ssid = cmd.substring(6, cmd.length());
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
        delay(1000);
        retries++;
        at_cmd_println(".", false);
    }
    if (retries >= 20)
        at_cmd_println("ERROR");
    else
        at_cmd_println("OK");
}

void sioModem::at_handle_port()
{
    int port = cmd.substring(6).toInt();
    if (port > 65535 || port < 0)
    {
        at_cmd_println("ERROR");
    }
    else
    {
        if (listenPort != 0)
        {
            tcpServer.stop();
        }

        listenPort = port;
        tcpServer.begin(listenPort);
        at_cmd_println("OK");
    }
}

void sioModem::at_handle_get()
{
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12);   // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12);   // Index first host name and possible port ends and path begins
    int port;
    String path, host;
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
        port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "")
        path = "/";

    // Debug
    at_cmd_println("Getting path ", false);
    at_cmd_println(path, false);
    at_cmd_println(" from port ", false);
    at_cmd_println(port, false);
    at_cmd_println(" of host ", false);
    at_cmd_println(host, false);
    at_cmd_println("...");

    // Establish connection
    if (!tcpClient.connect(host.c_str(), port))
    {
        at_cmd_println("NO CARRIER");
    }
    else
    {
        at_cmd_println("CONNECT ", false);
        at_cmd_println(modemBaud);
        cmdMode = false;

        // Send a HTTP request before continuing the connection as usual
        String request = "GET ";
        request += path;
        request += " HTTP/1.1\r\nHost: ";
        request += host;
        request += "\r\nConnection: close\r\n\r\n";
        tcpClient.print(request);
    }
}

void sioModem::at_handle_help()
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

    at_cmd_println();

    if (listenPort > 0)
    {
        at_cmd_println(HELPPORT1, false);
        at_cmd_println(listenPort);
        at_cmd_println(HELPPORT2);
        at_cmd_println(HELPPORT3);
    }
    else
    {
        at_cmd_println(HELPPORT4);
    }
    at_cmd_println();
    at_cmd_println("OK");
}

void sioModem::at_handle_wifilist()
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
    at_cmd_println("OK");
}

void sioModem::at_handle_dial()
{
        int portIndex = cmd.indexOf(":");
        String host, port;
        if (portIndex != -1)
        {
            host = cmd.substring(4, portIndex);
            port = cmd.substring(portIndex + 1, cmd.length());
        }
        else
        {
            host = cmd.substring(4, cmd.length());
            port = "23";        // Telnet default
        }
#ifdef DEBUG
        Debug_printf("DIALING: %s\n", host.c_str());
#endif
        if (host == "5551234")  // Fake it for BobTerm
        {
            delay(1300);        // Wait a moment so bobterm catches it
            at_cmd_println("CONNECT ", false);
            at_cmd_println(modemBaud);
#ifdef DEBUG
            Debug_println("CONNECT FAKE!");
#endif
        }
        else
        {
            at_cmd_println("Connecting to ", false);
            at_cmd_println(host, false);
            at_cmd_println(":", false);
            at_cmd_println(port);

            int portInt = port.toInt();

            if (tcpClient.connect(host.c_str(), portInt))
            {
                tcpClient.setNoDelay(true);     // Try to disable naggle

                at_cmd_println("CONNECT ", false);
                at_cmd_println(modemBaud);
                cmdMode = false;

                if (listenPort > 0)
                    tcpServer.stop();
            }
            else
            {
                at_cmd_println("NO CARRIER");
            }
        }
}
/*
   Perform a command given in AT Modem command mode
*/
void sioModem::modemCommand()
{
    static const char * at_cmds [_at_cmds::AT_ENUMCOUNT] = 
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
        "ATPORT"
    };

    cmd.trim();
    if (cmd == "")
        return;
    
    String upperCaseCmd = cmd;
    upperCaseCmd.toUpperCase();
    
    at_cmd_println();

#ifdef DEBUG
    Debug_print("AT Cmd: ");
    Debug_println(upperCaseCmd);
#endif

    // Replace EOL with CR
    if (upperCaseCmd.indexOf(ATASCII_EOL) != 0)
        upperCaseCmd[upperCaseCmd.indexOf(ATASCII_EOL)] = ASCII_CR;

    // Just AT
    int cmd_match = AT_ENUMCOUNT;
    if (upperCaseCmd == "AT")
    {
        cmd_match = AT_AT;
    } else {
        // Make sure we skip the plain AT command when matching
        for(cmd_match = _at_cmds::AT_AT + 1; cmd_match < _at_cmds::AT_ENUMCOUNT; cmd_match++)
            if(upperCaseCmd.startsWith(at_cmds[cmd_match]))
                break;
    }

    switch(cmd_match)
    {
    // plain AT
    case AT_AT:
        at_cmd_println("OK");
        break;
    // hangup
    case AT_H:
    case AT_H2:
        tcpClient.flush();
        tcpClient.stop();
        cmdMode = true;
        at_cmd_println("NO CARRIER");
        if (listenPort > 0)
            tcpServer.begin();
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
        telnet = false;
        at_cmd_println("OK");
        break;
    case AT_NET1:
        telnet = true;
        at_cmd_println("OK");
        break;
    case AT_A:
        if(tcpServer.hasClient())
        {
            tcpClient = tcpServer.available();
            tcpClient.setNoDelay(true);     // try to disable naggle
            tcpServer.stop();
            at_cmd_println("CONNECT ", false);
            at_cmd_println(modemBaud);
            cmdMode = false;
            SIO_UART.flush();
        }
        break;
    // See my IP address
    case AT_IP:
        if (WiFi.isConnected())
            at_cmd_println(WiFi.localIP());
        else
            at_cmd_println(HELPNOWIFI);
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
    default:
        at_cmd_println("ERROR");
#ifdef DEBUG
        Debug_println("*** unrecognized modem command");
#endif
        break;
    }

    cmd = "";
}

/*
  Handle incoming & outgoing data for modem
*/
void sioModem::sio_handle_modem()
{
  /**** AT command mode ****/
    if (cmdMode == true)
    {
        // In command mode but new unanswered incoming connection on server listen socket
        if ((listenPort > 0) && (tcpServer.hasClient()))
        {
            // Print RING every now and then while the new incoming connection exists
            if ((millis() - lastRingMs) > RING_INTERVAL)
            {
                at_cmd_println("RING");
                lastRingMs = millis();
            }
        }

        // In command mode - don't exchange with TCP but gather characters to a string
        if (SIO_UART.available() /*|| blockWritePending == true */ )
        {
            // get char from Atari SIO
            char chr = SIO_UART.read();

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
                cmd.remove(cmd.length() - 1);
                // We don't assume that backspace is destructive
                // Clear with a space
                SIO_UART.write(ASCII_BACKSPACE);
                SIO_UART.write(' ');
                SIO_UART.write(ASCII_BACKSPACE);
            }
            else if (chr == ATASCII_BACKSPACE)
            {
                // ATASCII backspace
                cmd.remove(cmd.length() - 1);
                SIO_UART.write(ATASCII_BACKSPACE);   // we can assume ATASCII BS is destructive.
            }
            // Take into account arrow key movement and clear screen            
            else if (chr == ATASCII_CLEAR_SCREEN || 
                ((chr >= ATASCII_CURSOR_UP) && (chr <= ATASCII_CURSOR_RIGHT)))
            {
                SIO_UART.write(chr);
            }
            else
            {
                if (cmd.length() < MAX_CMD_LENGTH)
                    cmd.concat(chr);
                SIO_UART.write(chr);
            }
        }
    }
    // Connected mode
    else
    {
        int sioBytesAvail = SIO_UART.available();

        // send from Atari to Fujinet
        if (sioBytesAvail && tcpClient.connected())
        {
            // In telnet in worst case we have to escape every byte
            // so leave half of the buffer always free
            //int max_buf_size;
            //if (telnet == true)
            //  max_buf_size = TX_BUF_SIZE / 2;
            //else
            //  max_buf_size = TX_BUF_SIZE;


            // Read from serial, the amount available up to
            // maximum size of the buffer
            int sioBytesRead = SIO_UART.readBytes(&txBuf[0], 
                (sioBytesAvail > TX_BUF_SIZE) ? TX_BUF_SIZE : sioBytesAvail);

            // Disconnect if going to AT mode with "+++" sequence
            for (int i = 0; i < (int) sioBytesRead; i++)
            {
                if (txBuf[i] == '+')
                    plusCount++;
                else
                    plusCount = 0;
                if (plusCount >= 3)
                {
                    plusTime = millis();
                }
                if (txBuf[i] != '+')
                {
                    plusCount = 0;
                }
            }

            // Double (escape) every 0xff for telnet, shifting the following bytes
            // towards the end of the buffer from that point
            int len = sioBytesRead;
            if (telnet == true)
            {
                for (int i = len - 1; i >= 0; i--)
                {
                    if (txBuf[i] == 0xff)
                    {
                        for (int j = TX_BUF_SIZE - 1; j > i; j--)
                        {
                            txBuf[j] = txBuf[j - 1];
                        }
                        len++;
                    }
                }
            }

            // Write the buffer to TCP finally
            tcpClient.write(&txBuf[0], sioBytesRead);
        }


        // read from Fujinet to Atari
        unsigned char buf[RECVBUFSIZE];
        int bytesAvail = 0;

        // check to see how many bytes are avail to read
        if ((bytesAvail = tcpClient.available()) > 0)
        {
            // read as many as our buffer size will take (RECVBUFSIZE)
            unsigned int bytesRead = tcpClient.readBytes(buf, 
                (bytesAvail > RECVBUFSIZE) ? RECVBUFSIZE : bytesAvail);

            SIO_UART.write(buf, bytesRead);
            SIO_UART.flush();
        }
    }

    // If we have received "+++" as last bytes from serial port and there
    // has been over a second without any more bytes, disconnect
    if (plusCount >= 3)
    {
        if (millis() - plusTime > 1000)
        {
#ifdef DEBUG
            Debug_println("Hanging up...");
#endif
            tcpClient.stop();
            plusCount = 0;
        }
    }

    // Go to command mode if TCP disconnected and not in command mode
    if (!tcpClient.connected() && (cmdMode == false) && (DTR == 0))
    {
        tcpClient.flush();
        tcpClient.stop();
        cmdMode = true;
        at_cmd_println("NO CARRIER");
        if (listenPort > 0)
            tcpServer.begin();
    }
    else if ((!tcpClient.connected()) && (cmdMode == false))
    {
        cmdMode = true;
        at_cmd_println("NO CARRIER");
        if (listenPort > 0)
            tcpServer.begin();
    }
}

/*
  Process command
*/
void sioModem::sio_process()
{
#ifdef DEBUG
    Debug_println("sioModem::sio_process() called");
    static int i3F = 0;
    static int i21 = 0;
    static int i26 = 0;
    static int i40 = 0;
    if (cmdFrame.comnd != SIO_MODEMCMD_LOAD_RELOCATOR)
        i21 = 0;
    if (cmdFrame.comnd != SIO_MODEMCMD_LOAD_HANDLER)
        i26 = 0;
    if (cmdFrame.comnd != SIO_MODEMCMD_TYPE1_POLL)
        i3F = 0;
    if (cmdFrame.comnd != SIO_MODEMCMD_TYPE3_POLL)
        i40 = 0;
#endif
    switch (cmdFrame.comnd)
    {
    case SIO_MODEMCMD_LOAD_RELOCATOR:
#ifdef DEBUG
    Debug_printf("$21 RELOCATOR #%d\n", ++i21);
#endif    
        sio_send_firmware(cmdFrame.comnd);
        break;
    case SIO_MODEMCMD_LOAD_HANDLER:
#ifdef DEBUG
    Debug_printf("$26 HANDLER DL #%d\n", ++i26);
#endif    
        sio_send_firmware(cmdFrame.comnd);
        break;
    case SIO_MODEMCMD_TYPE1_POLL:
#ifdef DEBUG
    Debug_printf("$3F TYPE 1 POLL #%d\n", ++i3F);
#endif    
        sio_poll_1();
        break;
    case SIO_MODEMCMD_TYPE3_POLL:
#ifdef DEBUG
    Debug_printf("$40 TYPE 3 POLL #%d\n", ++i40);
#endif    
        // ignore for now
        break;        
    case SIO_MODEMCMD_CONTROL:
        sio_ack();
        sio_control();
        break;
    case SIO_MODEMCMD_CONFIGURE:
        sio_ack();
        sio_config();
        break;
    case SIO_MODEMCMD_LISTEN:
        sio_listen();
        break;
    case SIO_MODEMCMD_UNLISTEN:
        sio_unlisten();
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
