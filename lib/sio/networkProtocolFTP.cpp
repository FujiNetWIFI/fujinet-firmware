#include <string.h>
#include "fnSystem.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

#include "networkProtocolFTP.h"

networkProtocolFTP::networkProtocolFTP()
{
}

networkProtocolFTP::~networkProtocolFTP()
{
}

string networkProtocolFTP::ftpResult()
{
    char buf[512];
    long tstart=fnSystem.millis();
    long tdur=0;

    memset(buf, 0, sizeof(buf));

    if (!control.connected())
        return "";

    while (tdur < 10000)
    {
        if (control.available() > 0)
            break;

        tdur = fnSystem.millis() - tstart;
    }

    int l = control.read_until('\n', buf, sizeof(buf));
    Debug_printf("We got %d bytes back\n", l);

    if (l < 4)
        return "";

    string sbuf = string(buf);
    controlResponse = sbuf.substr(4);

    Debug_printf("FTP Result: %s -  %s\n",sbuf.substr(0,3).c_str(),sbuf.c_str());
    
    return sbuf.substr(0,3);
}

unsigned short networkProtocolFTP::parsePort(string response)
{
    size_t pos_start = 0;
    size_t pos_end = 0;
    unsigned short port;

    pos_start = response.find_first_of("|");
    pos_start += 2;

    Debug_printf("pos_start %d\n", pos_start);

    pos_end = response.find_last_of("|");
    Debug_printf("pos_end %d\n", pos_end);
    pos_start++;
    pos_end--;
    port = (atoi(response.substr(pos_start, pos_end).c_str()));
    Debug_printf("port string %s\n", response.substr(pos_start, pos_end).c_str());
    Debug_printf("Parsed port is: %d\n", port);
    return port;
}

bool networkProtocolFTP::ftpLogin(EdUrlParser *urlParser)
{
    string tmpPath;

    if (!control.connect(urlParser->hostName.c_str(), atoi(urlParser->port.c_str())))
        return false; // Error

    Debug_printf("Waiting for banner.\n");
    if (ftpResult()!="220")
        return false; // error

    Debug_printf("Got user, sending USER.\n");
    control.write("USER anonymous\r\n");
    if (ftpResult()!="331")
        return false;

    Debug_printf("User Ok, sending password.\n");
    control.write("PASS fujinet@fujinet.online\r\n");
    if (ftpResult()!="230")
        return false;

    Debug_printf("Logged in.\n");

    Debug_printf("Setting type to IMAGE\n");
    control.write("TYPE I\r\n");
    if (ftpResult()!="200")
        return false;

    tmpPath = urlParser->path.substr(0, urlParser->path.find("*") - 1);

    if (tmpPath.empty())
        tmpPath = "/";

    Debug_printf("Type I OK.\nAttempting to CWD to \"%s\"\n", tmpPath.c_str());

    control.write("CWD ");
    control.write(tmpPath.c_str());
    control.write("\r\n");

    if (ftpResult()!="250")
    {
        // Trim off last part of filename, hopefully to just a dir path
        string tmp = tmpPath;
        int lastof = tmp.find_last_of("/");
        if(lastof != string::npos)
        {
            tmp = tmp.substr(0, lastof + 1);
            Debug_printf("Workaround, trying again... with \"%s\"\n", tmp.c_str());

            control.write("CWD ");
            control.write(tmp.c_str());
            control.write("\r\n");

            if (ftpResult()=="250")
                return true; // OK!
        }
        Debug_println("Failed CWD");
        return false;
    }
    return true;
}

bool networkProtocolFTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    string tmpPath;
    Debug_println("networkProtocolFTP::open()");

    if (urlParser->port.empty())
        urlParser->port = "21";

    hostName = urlParser->hostName;
    aux1 = cmdFrame->aux1;

    if (ftpLogin(urlParser) == false)
        return false;

    Debug_printf("Attempting to get passive port\n");
    control.write("EPSV\r\n");

    if (ftpResult()!="229")
        return false;

    dataPort = parsePort(controlResponse);
    Debug_printf("Received EPSV response. Port %d\n", dataPort);

    switch (cmdFrame->aux1)
    {
    case 4:
        Debug_printf("Attempting to open RETR. to %s\n", urlParser->path.c_str());

        control.write("RETR ");
        control.write(urlParser->path.c_str());
        control.write("\r\n");
        break;
    case 6:
        tmpPath = urlParser->path.substr(urlParser->path.find_last_of("/") + 1);
        Debug_printf("Attempting NLST to %s\n", tmpPath.c_str());
        control.write("NLST");
        if ((tmpPath != "*.*") && (tmpPath != "*") && (tmpPath != "**.*") && (tmpPath != "**") && (tmpPath != "-"))
        {
            control.write(" ");
            control.write(tmpPath.c_str());
        }
        control.write("\r\n");
        break;
    case 8:
        Debug_printf("Storing file %s\n", urlParser->path.c_str());
        control.write("STOR ");
        control.write(urlParser->path.c_str());
        control.write("\r\n");
        break;
    default:
        Debug_printf("Unimplemented aux1 = %d\n", cmdFrame->aux1);
        return false;
    }

    if (!data.connect(hostName.c_str(), dataPort))
        return false;

    Debug_printf("%s Connected to data port: %d\n", fnSystem.get_uptime_str(), dataPort);

    if (cmdFrame->aux1 != 8) // do not do this for write!
    {
        // Wait for data to become available before letting the Atari cut loose...
        int delaymax = 0;
        while (data.available() == 0)
        {
            if (delaymax >= 8000)
            {
                Debug_println("Timed out waiting for data on DATA channel");
                data.stop();
                return false;
            }

            Debug_println("Waiting for data on DATA channel");
            fnSystem.delay(250);
            delaymax += 250;
        }
    }
    return true;
}

bool networkProtocolFTP::close()
{
    Debug_println("networkProtocolFTP::close()");
    if (data.connected())
        data.stop();

    if (control.connected())
    {
        Debug_printf("Connected to data port, closing it.\n");
        control.write("QUIT\r\n");
        control.stop();
    }

    return true;
}

bool networkProtocolFTP::read(uint8_t *rx_buf, unsigned short len)
{
    Debug_print("networkProtocolFTP::read()... ");
    size_t z = data.read(rx_buf, len);
    Debug_printf("%u of %hu bytes\n", z, len);

    if (z != len)
        return true;
    else
        dataSize -= len;

    if (aux1 == 6)
    {
        for (int i = 0; i < len; i++)
        {
            if (rx_buf[i] == 0x0D)
                rx_buf[i] = 0x20;
            else if (rx_buf[i] == 0x0A)
                rx_buf[i] = 0x9B;
        }
    }
    return false;
}

bool networkProtocolFTP::write(uint8_t *tx_buf, unsigned short len)
{
    Debug_print("networkProtocolFTP::write()... ");
    size_t z = data.write(tx_buf, len);
    Debug_printf("%u of %hu bytes\n", z, len);

    if (z != len)
        return true;

    return false;
}

bool networkProtocolFTP::status(uint8_t *status_buf)
{
    int a = data.available();
    __IGNORE_UNUSED_VAR(a);
    status_buf[0] = a & 0xFF;
    status_buf[1] = a >> 8;
    status_buf[2] = 0;
    status_buf[3] = (a==0 ? 136 : 1);
    return false;
}

bool networkProtocolFTP::del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    if (urlParser->port.empty())
        urlParser->port = "21";

    if (ftpLogin(urlParser) == false)
        return false;

    // Remove leading slash!
    urlParser->path=urlParser->path.substr(1);

    Debug_printf("Deleting file %s\n", urlParser->path.c_str());
    control.write("DELE ");
    control.write(urlParser->path.c_str());
    control.write("\r\n");

    return ftpResult()=="250";
}

bool networkProtocolFTP::mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    if (urlParser->port.empty())
        urlParser->port = "21";

    if (ftpLogin(urlParser) == false)
        return false;

    // Remove leading slash!
    urlParser->path=urlParser->path.substr(1);

    Debug_printf("Making directory %s\n", urlParser->path.c_str());
    control.write("MKD ");
    control.write(urlParser->path.c_str());
    control.write("\r\n");

    return ftpResult()=="250";
}

bool networkProtocolFTP::rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    if (urlParser->port.empty())
        urlParser->port = "21";

    if (ftpLogin(urlParser) == false)
        return false;

    // Remove leading slash!
    urlParser->path=urlParser->path.substr(1);

    Debug_printf("Removing directory %s\n", urlParser->path.c_str());
    control.write("RMD ");
    control.write(urlParser->path.c_str());
    control.write("\r\n");

    return ftpResult()=="250";
}

bool networkProtocolFTP::rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    string rnFrom;
    string rnTo;
    size_t comma_pos;

    if (urlParser->port.empty())
        urlParser->port = "21";

    // Remove leading slash!
    urlParser->path=urlParser->path.substr(1);

    comma_pos=urlParser->path.find(",");

    if (comma_pos==string::npos)
        return false;
    
    rnFrom=urlParser->path.substr(0,comma_pos);
    rnTo=urlParser->path.substr(comma_pos+1);

    // Remove dest from path, for login.
    urlParser->path=urlParser->path.substr(0,comma_pos);

    if (ftpLogin(urlParser) == false)
        return false;

    Debug_printf("Renaming %s to %s\n",rnFrom.c_str(),rnTo.c_str());
    control.write("RNFR ");
    control.write(rnFrom.c_str());
    control.write("\r\n");

    if (ftpResult()=="350")
        return false;

    control.write("RNTO ");
    control.write(rnTo.c_str());
    control.write("\r\n");

    return ftpResult()=="250";
}

bool networkProtocolFTP::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolFTP::special_supported_00_command(unsigned char comnd)
{
    return false;
}

int networkProtocolFTP::available()
{
    return data.available();
}