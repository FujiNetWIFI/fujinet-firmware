#ifdef BUILD_APPLE

#include <string.h>
#include <lwip/netdb.h>

#include "../../include/atascii.h"
#include "modem.h"
#include "../hardware/fnUART.h"
#include "fnWiFi.h"
#include "fnFsSPIFFS.h"
#include "fnSystem.h"
#include "../utils/utils.h"
#include "fnConfig.h"
#include "led.h"

#define RECVBUFSIZE 1024

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
    iwmModem *modem = (iwmModem *)user_data; // somehow it thinks this is unused?

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        if (ev->data.size && modem->modem_write((uint8_t *)ev->data.buffer, ev->data.size) != ev->data.size)
            Debug_printf("_telnet_event_handler(%d) - Could not write complete buffer to SIO.\n", ev->type);
        break;
    case TELNET_EV_SEND:
        modem->get_tcp_client().write((uint8_t *)ev->data.buffer, ev->data.size);
        break;
    case TELNET_EV_WILL:
        if (ev->neg.telopt == TELNET_TELOPT_ECHO)
            modem->set_do_echo(false);
        break;
    case TELNET_EV_WONT:
        if (ev->neg.telopt == TELNET_TELOPT_ECHO)
            modem->set_do_echo(true);
        break;
    case TELNET_EV_DO:
        break;
    case TELNET_EV_DONT:
        break;
    case TELNET_EV_TTYPE:
        if (ev->ttype.cmd == TELNET_TTYPE_SEND)
            telnet_ttype_is(telnet, modem->get_term_type().c_str());
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

static void _modem_task(void *arg)
{
    iwmModem *m = (iwmModem *)arg;

    while (true)
    {
        m->handle_modem();
        vTaskDelay(10);    
    }
}

iwmModem::iwmModem(FileSystem *_fs, bool snifferEnable)
{
    activeFS = _fs;
    modemSniffer = new ModemSniffer(activeFS, snifferEnable);
    set_term_type("dumb");
    telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
    mrxq = xQueueCreate(2048,sizeof(char));
    mtxq = xQueueCreate(2048,sizeof(char));
    xTaskCreatePinnedToCore(_modem_task,"modemTask",4096,this,50,&modemTask,0);
}

iwmModem::~iwmModem()
{
    if (modemSniffer != nullptr)
    {
        delete modemSniffer;
    }

    if (telnet != nullptr)
    {
        telnet_free(telnet);
    }

    vTaskDelete(&modemTask);
    vQueueDelete(mrxq);
    vQueueDelete(mtxq);
}

unsigned short iwmModem::modem_write(uint8_t* buf, unsigned short len)
{
    unsigned short l=0;

    while (len>0)
    {
        xQueueSend(mrxq,&buf[len--],portMAX_DELAY);
        l++;
    }
    return l;
}

unsigned short iwmModem::modem_write(char c)
{
    xQueueSend(mrxq,&c,portMAX_DELAY);
    return 1;
}

unsigned short iwmModem::modem_print(const char *s)
{
    unsigned short l=0;

    while (*s!=0x00)
    {
        xQueueSend(mrxq,s++,portMAX_DELAY);
        l++;
    }

    return l;
}

unsigned short iwmModem::modem_print(std::string s)
{
    return modem_print(s.c_str());
}

unsigned short iwmModem::modem_print(int i)
{
    char out[80];

    itoa(i,out,10);

    return modem_print(out);
}

unsigned short iwmModem::modem_read(uint8_t *buf, unsigned short len)
{
    unsigned short i, l=0;

    for (i=0;i<len;i++)
        l += xQueueReceive(mtxq,&buf[i],portMAX_DELAY);

    return l;
}

void iwmModem::at_connect_resultCode(int modemBaud)
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
    modem_print(resultCode);
    modem_write(ASCII_CR);
}

/**
 * Emit result code if ATV0
 * No Atascii translation here, as this is intended for machine reading.
 */
void iwmModem::at_cmd_resultCode(int resultCode)
{
    modem_print(resultCode);
    modem_write(ASCII_CR);
    modem_write(ASCII_LF);
}

/**
   replacement println for AT that is CR/EOL aware
*/
void iwmModem::at_cmd_println()
{
    if (cmdOutput == false)
        return;

    if (cmdAtascii == true)
    {
        modem_write(ATASCII_EOL);
    }
    else
    {
        modem_write(ASCII_CR);
        modem_write(ASCII_LF);
    }
}

void iwmModem::at_cmd_println(const char *s, bool addEol)
{
    if (cmdOutput == false)
        return;

    modem_print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            modem_write(ATASCII_EOL);
        }
        else
        {
            modem_write(ASCII_CR);
            modem_write(ASCII_LF);
        }
    }
}

void iwmModem::at_cmd_println(int i, bool addEol)
{
    if (cmdOutput == false)
        return;

    modem_print(i);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            modem_write(ATASCII_EOL);
        }
        else
        {
            modem_write(ASCII_CR);
            modem_write(ASCII_LF);
        }
    }
}

void iwmModem::at_cmd_println(std::string s, bool addEol)
{
    if (cmdOutput == false)
        return;

    modem_print(s);
    if (addEol)
    {
        if (cmdAtascii == true)
        {
            modem_write(ATASCII_EOL);
        }
        else
        {
            modem_write(ASCII_CR);
            modem_write(ASCII_LF);
        }
    }
}

void iwmModem::at_handle_wificonnect()
{
    int keyIndex = cmd.find(',');
    std::string ssid, key;
    if (keyIndex != std::string::npos)
    {
        ssid = cmd.substr(13, keyIndex - 13 + 1);
        key = cmd.substr(keyIndex + 1);
    }
    else
    {
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

void iwmModem::at_handle_port()
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
        tcpServer.begin(listenPort);
        if (numericResultCode == true)
            at_cmd_resultCode(RESULT_CODE_OK);
        else
            at_cmd_println("OK");
    }
}

void iwmModem::at_handle_get()
{
    // From the URL, acquire required variables
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

void iwmModem::at_handle_help()
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
        at_cmd_println(HELPPORT2);
        at_cmd_println(HELPPORT3);
    }
    else
    {
        at_cmd_println(HELPPORT4);
    }
    at_cmd_println();

    if (numericResultCode == true)
        at_cmd_resultCode(RESULT_CODE_OK);
    else
        at_cmd_println("OK");
}

void iwmModem::at_handle_wifilist()
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

        char ssid[33];
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

void iwmModem::at_handle_answer()
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
        answerHack = false;
    }
}

void iwmModem::at_handle_dial()
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
void iwmModem::at_handle_pblist()
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
void iwmModem::at_handle_pb()
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
void iwmModem::modemCommand()
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
void iwmModem::handle_modem()
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
                // Print RING every now and then while the new incoming connection exists
                if ((fnSystem.millis() - lastRingMs) > RING_INTERVAL)
                {
                    if (numericResultCode == true)
                        at_cmd_resultCode(RESULT_CODE_RING);
                    else
                        at_cmd_println("RING");
                    lastRingMs = fnSystem.millis();
                }
            }
        }

        // In command mode - don't exchange with TCP but gather characters to a string
        //if (SIO_UART.available() /*|| blockWritePending == true */ )
        if (uxQueueMessagesWaiting(mtxq))
        {
            char chr;

            xQueueReceive(mtxq,&chr,portMAX_DELAY);

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
                        modem_write(ASCII_BACKSPACE);
                        modem_write(' ');
                        modem_write(ASCII_BACKSPACE);
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
                        modem_write(ATASCII_BACKSPACE);
                }
            }
            // Take into account arrow key movement and clear screen
            else if (chr == ATASCII_CLEAR_SCREEN ||
                     ((chr >= ATASCII_CURSOR_UP) && (chr <= ATASCII_CURSOR_RIGHT)))
            {
                if (commandEcho == true)
                    modem_write(chr);
            }
            else
            {
                if (cmd.length() < MAX_CMD_LENGTH)
                {
                    //cmd.concat(chr);
                    cmd += chr;
                }
                if (commandEcho == true)
                    modem_write(chr);
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

        int sioBytesAvail = uxQueueMessagesWaiting(mtxq);

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
            int sioBytesRead = modem_read(&txBuf[0], //SIO_UART.readBytes(&txBuf[0],
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
                modem_write(buf, bytesRead);
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

void iwmModem::shutdown()
{
    if (modemSniffer != nullptr)
        if (modemSniffer->getEnable())
            modemSniffer->closeOutput();
}

void iwmModem::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_WRITE_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    IWM.iwm_send_packet(id(),iwm_packet_type_t::status,SP_ERR_NOERROR, data, 4);
}

void iwmModem::send_status_dib_reply_packet()
{
    uint8_t data[25];

    //* write data buffer first (25 bytes) 3 grp7 + 4 odds
    // General Status byte
    // Bit 7: Block  device
    // Bit 6: Write allowed
    // Bit 5: Read allowed
    // Bit 4: Device online or disk in drive
    // Bit 3: Format allowed
    // Bit 2: Media write protected (block devices only)
    // Bit 1: Currently interrupting (//c only)
    // Bit 0: Currently open (char devices only)
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0;    // block size 1
    data[2] = 0;    // block size 2
    data[3] = 0;    // block size 3
    data[4] = 0x05; // ID string length - 11 chars
    data[5] = 'M';
    data[6] = 'O';
    data[7] = 'D';
    data[8] = 'E';
    data[9] = 'M';
    data[10] = ' ';
    data[11] = ' ';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';                         // ID string (16 chars total)
    data[21] = SP_TYPE_BYTE_FUJINET_MODEM;    // Device type    - 0x02  harddisk
    data[22] = SP_SUBTYPE_BYTE_FUJINET_MODEM; // Device Subtype - 0x0a
    data[23] = 0x00;                        // Firmware version 2 bytes
    data[24] = 0x01;                        //
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

void iwmModem::iwm_read(iwm_decoded_cmd_t cmd)
{
    uint16_t numbytes = get_numbytes(cmd); // cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    uint32_t addy = get_address(cmd); // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);

    Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x\n", id(), numbytes, addy);

    memset(data_buffer,0,sizeof(data_buffer));

    for (int i=0;i<numbytes;i++)
    {
        //char b;
        //xQueueReceive(rxq,&b,portMAX_DELAY);
        //data_buffer[i] = b;
        //data_len++;
    }

    Debug_printf("\r\nsending block packet ...");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmModem::iwm_write(iwm_decoded_cmd_t cmd)
{
    uint16_t num_bytes = get_numbytes(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);

    Debug_printf("\nWrite %u bytes\n", num_bytes);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking
    data_len = 512;
    if (IWM.iwm_read_packet_timeout(100, data_buffer, data_len))
    {
        Debug_printf("\r\nTIMEOUT in read packet!");
        return;
    }
    // partition number indicates which 32mb block we access
    if (data_len == -1)
        iwm_return_ioerror();
    else
    {
        // DO write
    }
}

void iwmModem::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    uint8_t control_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // ctrl codes 00-FF
    Debug_printf("\r\nDevice %02x Control Code %02x", id(), control_code);
    data_len = 512;
    IWM.iwm_read_packet_timeout(100, data_buffer, data_len);
    print_packet(data_buffer);

    if (data_len > 0)
        switch (control_code)
        {
        }
    else
        err_result = SP_ERR_IOERROR;
    
    send_reply_packet(err_result);
}

void iwmModem::iwm_modem_status()
{
        unsigned short mw = uxQueueMessagesWaiting(mrxq);
        data_buffer[0] = mw & 0xFF;
        data_buffer[1] = mw >> 8;
        data_len = 2;
}

void iwmModem::iwm_status(iwm_decoded_cmd_t cmd)
{
    // uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    uint8_t status_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\nDevice %02x Status Code %02x\n", id(), status_code);
    // Debug_printf("\r\nStatus List is at %02x %02x\n", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        send_status_reply_packet();
        return;
        break;
    case IWM_STATUS_DIB: // 0x03
        send_status_dib_reply_packet();
        return;
        break;
    case 'S': // Status
        iwm_modem_status();
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
}

void iwmModem::process(iwm_decoded_cmd_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case 0x00: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x04: // control
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case 0x08: // read
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

#endif /* BUILD_APPLE */