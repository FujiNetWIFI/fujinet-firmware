#ifndef MODEM_H
#define MODEM_H

#include <string>
#include "fnTcpServer.h"
#include "fnTcpClient.h"
#include "fnFsSD.h"
#include "fnFsSPIF.h"
#include "sio.h"
#include "../modem-sniffer/modem-sniffer.h"
#include "libtelnet.h"

/* Keep strings under 40 characters, for the benefit of 40-column users! */
#define HELPL01 "       FujiNet Virtual Modem 850"
#define HELPL02 "======================================="
#define HELPL03 ""
#define HELPL04 "ATWIFILIST        | List avail networks"
#define HELPL05 "ATWIFICONNECT<ssid>,<key>"
#define HELPL06 "                  | Connect to WiFi net"
#define HELPL07 "ATDT<host>:<port> | Connect by TCP"
#define HELPL08 "ATIP              | See my IP address"
#define HELPL09 "ATNET<0|1>        | Dis/enable TELNET"
#define HELPL10 "                  | command handling"
#define HELPL11 "ATPORT<port>      | Set listening port"
#define HELPL12 "ATS0=<0|1>        | Auto-answer in-"
#define HELPL13 "                  | coming connections"
#define HELPL14 "ATGET<URL>        | HTTP GET"
#define HELPL15 "ATTERM<termtype>  | Set telnet term"
#define HELPL16 "                  | type ('DUMB',"
#define HELPL17 "                  | 'VT52', or 'VT100')"
#define HELPL18 "AT[UN]SNIFF       | Dis/enable sniffing"
/* Not explicitly mentioned at this time, since they are commonly known:
 * (these are sioModem class's _at_cmds enums)
 * - AT
 * - ATA (mentioned below)
 * - AT? (the help command itself)
 * - AT_H / AT_H1 / AT_OFFHOOK (hangup)
 * - AT_E0 / AT_E1 (echo off/on)
 * - AT_V0 / AT_V1 (verbose off/on)
*/

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

#define RESULT_CODE_OK              0
#define RESULT_CODE_CONNECT         1
#define RESULT_CODE_RING            2
#define RESULT_CODE_NO_CARRIER      3
#define RESULT_CODE_ERROR           4
#define RESULT_CODE_CONNECT_1200    5
#define RESULT_CODE_BUSY            7
#define RESULT_CODE_NO_ANSWER       8
#define RESULT_CODE_CONNECT_2400    10
#define RESULT_CODE_CONNECT_9600    13
#define RESULT_CODE_CONNECT_4800    18
#define RESULT_CODE_CONNECT_19200   85

    /* The actual strings expected for these can be
     * found in `modem.cpp`'s at_cmds[] array. */
    enum _at_cmds
    {
        AT_AT = 0,         // no-op
        AT_NET0,           // disable telnet command handling
        AT_NET1,           // enable telnet command handling
        AT_A,              // answer incoming connection (RINGing)
        AT_IP,             // see my IP address
        AT_HELP,           // show brief AT command help
        AT_H,              // hang-up
        AT_H1,             // hang-up
        AT_DT,             // dial (<host>:<port>)
        AT_DP,             // dial (<host>:<port>)
        AT_DI,             // dial (<host>:<port>)
        AT_WIFILIST,       // list available WiFi networks
        AT_WIFICONNECT,    // connect to a WiFi network (<ssid>,<key>)
        AT_GET,            // HTTP GET (<url>)
        AT_PORT,           // set listening port (<port>)
        AT_V0,             // disable verbose (i.e., numeric result codes)
        AT_V1,             // enable verbose (i.e., English result codes; "CONNECT", "NO CARRIER", etc.)
        AT_ANDF_ignored,   // (ignored; return all settings to factory settings)
        AT_S0E0,           // disable auto-answer
        AT_S0E1,           // enable auto-answer
        AT_S2E43_ignored,  // (ignored; set escape character to ASCII 43 ('+'))
        AT_S5E8_ignored,   // (ignored; set backspace character to ASCII 8)
        AT_S6E2_ignored,   // (ignored; set wait time before blind dialing)
        AT_S7E30_ignored,  // (ignored; set wait for carrier after dial)
        AT_S12E20_ignored, // (ignored; set escape code guard time)
        AT_E0,             // disable echo
        AT_E1,             // enable echo
        AT_M0_ignored,     // (ignored; speaker off)
        AT_M1_ignored,     // (ignored; speaker on)
        AT_X1_ignored,     // (ignored; show connection speed on CONNECT)
        AT_AC1_ignored,    // (ignored; DCD active on carrier detect, inactive on hangup)
        AT_AD2_ignored,    // (ignored; modem resets when DTR dropped)
        AT_AW_ignored,     // (ignored; save settings)
        AT_OFFHOOK,        // hang-up (should be ignored?)
        AT_ZPPP_ignored,   // (ignored; +++ATZ reset modem to config profile 0)
        AT_BBSX_ignored,   // (ignored; combo command (common with BBSes?))
        AT_SNIFF,          // enable MODEM sniffer
        AT_UNSNIFF,        // disable MODEM sniffer
        AT_TERMVT52,       // set TELNET term mode to VT52
        AT_TERMVT100,      // set TELNET term mode to VT100
        AT_TERMDUMB,       // set TELNET term mode to dumb
        AT_ENUMCOUNT};

    uint modemBaud = 2400; // Holds modem baud rate, Default 2400
    bool DTR = false;
    bool RTS = false;
    bool XMT = false;
    bool baudLock = false; // lock modem baud rate from further changes.

    int count_PollType1 = 0; // Keep track of how many times we've seen command 0x3F
    int count_PollType3 = 0;

    int count_ReqRelocator = 0;
    int count_ReqHandler = 0;
    bool firmware_sent = false;

    /* Modem Active Variables */
    std::string cmd = "";          // Gather a new AT command to this string from serial
    bool cmdMode = true;           // Are we in AT command mode or connected mode
    bool cmdAtascii = false;       // last CMD contained an ATASCII EOL?
    unsigned short listenPort = 0; // Listen to this if not connected. Set to zero to disable.
    fnTcpClient tcpClient;         // Modem client
    fnTcpServer tcpServer;         // Modem server
    unsigned long lastRingMs = 0;  // Time of last "RING" message (millis())
    char plusCount = 0;            // Go to AT mode at "+++" sequence, that has to be counted
    unsigned long plusTime = 0;    // When did we last receive a "+++" sequence
    uint8_t txBuf[TX_BUF_SIZE];
    bool cmdOutput=true;            // toggle whether to emit command output
    bool numericResultCode=false;   // Use numeric result codes? (ATV0)
    bool autoAnswer=false;          // Auto answer? (ATS0?)
    bool commandEcho=true;          // Echo MODEM input. (ATEx)
    bool CRX=false;                 // CRX flag.
    unsigned char crxval=0;         // CRX value.
    bool answerHack=false;          // ATA answer hack on SIO write.
    FileSystem *activeFS;           // Active Filesystem for ModemSniffer.
    ModemSniffer* modemSniffer;     // ptr to modem sniffer.
    time_t _lasttime;               // most recent timestamp of data activity.
    telnet_t *telnet;               // telnet FSM state.
    bool use_telnet=false;          // Use telnet mode?
    bool do_echo;                   // telnet echo toggle.
    string term_type;               // telnet terminal type.

    void sio_send_firmware(uint8_t loadcommand); // $21 and $26: Booter/Relocator download; Handler download
    void sio_poll_1();                           // $3F, '?', Type 1 Poll
    void sio_poll_3(uint8_t device, uint8_t aux1, uint8_t aux2); // $40, '@', Type 3 Poll
    void sio_control();                          // $41, 'A', Control
    void sio_config();                           // $42, 'B', Configure
    void sio_set_dump();                         // $$4, 'D', Dump
    void sio_listen();                           // $4C, 'L', Listen
    void sio_unlisten();                         // $4D, 'M', Unlisten
    void sio_baudlock();                         // $4E, 'N', Baud lock
    void sio_status() override;                  // $53, 'S', Status
    void sio_write();                            // $57, 'W', Write
    void sio_stream();                           // $58, 'X', Concurrent/Stream
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    
    void crx_toggle(bool toggle);                // CRX active/inactive?

    void modemCommand(); // Execute modem AT command

    // CR/EOL aware println() functions for AT mode
    void at_connect_resultCode(int modemBaud);
    void at_cmd_resultCode(int resultCode);
    void at_cmd_println();
    void at_cmd_println(const char *s, bool addEol = true);
    void at_cmd_println(int i, bool addEol = true);
    void at_cmd_println(std::string s, bool addEol = true);

    // Command handlers
    void at_handle_answer();
    void at_handle_dial();
    void at_handle_wifilist();
    void at_handle_wificonnect();
    void at_handle_help();
    void at_handle_get();
    void at_handle_port();

protected:
    void shutdown() override;

public:

    bool modemActive = false; // If we are in modem mode or not
    void sio_handle_modem();  // Handle incoming & outgoing data for modem

    sioModem(FileSystem *_fs, bool snifferEnable);
    virtual ~sioModem();

    time_t get_last_activity_time() { return _lasttime; } // timestamp of last input or output.
    ModemSniffer *get_modem_sniffer() { return modemSniffer; }
    fnTcpClient get_tcp_client() { return tcpClient; } // Return TCP client.
    bool get_do_echo() { return do_echo; }
    void set_do_echo(bool _do_echo) { do_echo = _do_echo; }
    string get_term_type() {return term_type; }
    void set_term_type(string _term_type) { term_type = _term_type; }

};

#endif
