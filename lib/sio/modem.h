#ifndef MODEM_H
#define MODEM_H

#include <string>
#include "fnTcpServer.h"
#include "fnTcpClient.h"
#include "sio.h"

#define HELPL01 "       FujiNet Virtual Modem 850"
#define HELPL02 "======================================="
#define HELPL03 ""
#define HELPL04 "ATWIFILIST        | List avail networks"
#define HELPL05 "ATWIFICONNECT<ssid>,<key>"
#define HELPL06 "                  | Connect to WiFi net"
#define HELPL07 "ATDT<host>:<port> | Connect by TCP"
#define HELPL08 "ATIP              | See my IP address"
#define HELPL09 "ATNET0            | Disable TELNET"
#define HELPL10 "                  | command handling"
#define HELPL11 "ATPORT<port>      | Set listening port"
#define HELPL12 "ATGET<URL>        | HTTP GET"

#define HELPPORT1 "Listening to connections on port "
#define HELPPORT2 "which result in RING that you can"
#define HELPPORT3 "answer with ATA."
#define HELPPORT4 "No incoming connections are enabled."

#define HELPSCAN1 "Scanning..."
#define HELPSCAN2 "No networks found"
#define HELPSCAN3 " networks found:"
#define HELPSCAN4 " (open)"
#define HELPSCAN5 " (encrypted)"

#define HELPNOWIFI "WiFi is not connected."
#define HELPWIFICONNECTING "Connecting to "

#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP (that direction is very blocking by the ESP TCP stack, so we can't do one byte a time.)

class sioModem : public sioDevice
{
private:
    enum _at_cmds
    {
        AT_AT = 0,
        AT_NET0,
        AT_NET1,
        AT_A,
        AT_IP,
        AT_HELP,
        AT_H,
        AT_H2,
        AT_DT,
        AT_DP,
        AT_DI,
        AT_WIFILIST,
        AT_WIFICONNECT,
        AT_GET,
        AT_PORT,
        AT_ENUMCOUNT
    };

    uint modemBaud = 2400; // Holds modem baud rate, Default 2400
    bool DTR = false;
    bool RTS = false;
    bool XMT = false;

    int count_PollType1 = 0; // Keep track of how many times we've seen command 0x3F
    int load_firmware(const char *filename, char **buffer);

    /* Modem Active Variables */
    std::string cmd = "";          // Gather a new AT command to this string from serial
    bool cmdMode = true;           // Are we in AT command mode or connected mode
    bool cmdAtascii = false;       // last CMD contained an ATASCII EOL?
    bool telnet = false;           // Is telnet control code handling enabled
    unsigned short listenPort = 0; // Listen to this if not connected. Set to zero to disable.
    fnTcpClient tcpClient;          // Modem client
    fnTcpServer tcpServer;          // Modem server
    unsigned long lastRingMs = 0;  // Time of last "RING" message (millis())
    char plusCount = 0;            // Go to AT mode at "+++" sequence, that has to be counted
    unsigned long plusTime = 0;    // When did we last receive a "+++" sequence
    uint8_t txBuf[TX_BUF_SIZE];
    bool blockWritePending = false; // is a BLOCK WRITE pending for the modem?
    uint8_t *blockPtr;                 // pointer in the block write (points somewhere in sector)

    void sio_send_firmware(uint8_t loadcommand); // $21 and $26: Booter/Relocator download; Handler download
    void sio_poll_1();                        // $3F, '?', Type 1 Poll
    void sio_control();                       // $41, 'A', Control
    void sio_config();                        // $42, 'B', Configure
    void sio_listen();                        // $4C, 'L', Listen
    void sio_unlisten();                      // $4D, 'M', Unlisten
    void sio_status() override;               // $53, 'S', Status
    void sio_write();                         // $57, 'W', Write
    void sio_stream();                        // $58, 'X', Concurrent/Stream
    void sio_process() override;              // Process the command

    void modemCommand(); // Execute modem AT command

    // CR/EOL aware println() functions for AT mode
    void at_cmd_println();
    void at_cmd_println(const char *s, bool addEol = true);
    void at_cmd_println(int i, bool addEol = true);
    void at_cmd_println(std::string s, bool addEol = true);
    void at_cmd_println(in_addr_t ipa, bool addEol = true);

    // Command handlers
    void at_handle_dial();
    void at_handle_wifilist();
    void at_handle_wificonnect();
    void at_handle_help();
    void at_handle_get();
    void at_handle_port();

public:
    bool modemActive = false; // If we are in modem mode or not
    void sio_handle_modem();  // Handle incoming & outgoing data for modem

    sioModem()
    {
        listen_to_type3_polls = true;
    }
};

#endif
