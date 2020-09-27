#include "networkProtocolUDP.h"
#include "../../include/debug.h"

networkProtocolUDP::networkProtocolUDP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::ctor\n");
#endif
    saved_rx_buffer_len = 0;
    strcpy(dest, "localhost");
}

networkProtocolUDP::~networkProtocolUDP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::dtor\n");
#endif
    saved_rx_buffer_len = 0;
}

bool networkProtocolUDP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::OPEN %s:%s \n", urlParser->hostName.c_str(), urlParser->port.c_str());
#endif
    if (!urlParser->hostName.empty())
    {
        strcpy(dest, urlParser->hostName.c_str());
        port = atoi(urlParser->port.c_str());
#ifdef DEBUG
        Debug_printf("Port: %d\n", port);
#endif
    }

    return udp.begin(atoi(urlParser->port.c_str()));
}

bool networkProtocolUDP::close()
{
    udp.stop();
    return true;
}

bool networkProtocolUDP::read(uint8_t *rx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::read %d bytes\n", len);
#endif

    memcpy(rx_buf, saved_rx_buffer, len);
    memset(saved_rx_buffer, 0, len);
    saved_rx_buffer_len = 0;
    return false;
}

bool networkProtocolUDP::write(uint8_t *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::write %d bytes to dest: %s port %d\n", len, dest, port);
#endif
    udp.beginPacket(dest, port);
    int l = udp.write(tx_buf, len);
    udp.endPacket();
#ifdef DEBUG
    Debug_printf("Output: ");
    for (int i = 0; i < len; i++)
        Debug_printf(" %02x", tx_buf[i]);
    Debug_printf("\n");
#endif
    if (l < len)
        return true;
    else
        return false;
}

bool networkProtocolUDP::status(uint8_t *status_buf)
{
    unsigned short len = udp.parsePacket();

    if (len > 0)
    {
        // Set destination automatically to remote address.
        //strcpy(dest, udp.remoteIP().toString().c_str());
        in_addr_t addr = udp.remoteIP();
        strcpy(dest, inet_ntoa(addr));
        //port = udp.remotePort();

        saved_rx_buffer_len = len;
        udp.read(saved_rx_buffer, len);
    }

    status_buf[0] = saved_rx_buffer_len & 0xFF;
    status_buf[1] = saved_rx_buffer_len >> 8;
    status_buf[2] = 1;
    status_buf[3] = 0x00;

    return false;
}

bool networkProtocolUDP::special_supported_80_command(unsigned char comnd)
{
    if (comnd == 'D') // Set DEST address
        return true;
    else
        return false;
}

bool networkProtocolUDP::special_set_destination(uint8_t *sp_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("Dest Path Passed in: %s\n", sp_buf);
#endif
    string path((const char *)sp_buf, 256);
    int device_colon = path.find_first_of(":");
    int port_colon = path.find_last_of(":");

    if (device_colon == string::npos)
        return true;

    if (port_colon == device_colon)
        return true;

    string new_dest_str = path.substr(device_colon+1, port_colon-2);
    string new_port_str = path.substr(port_colon + 1);

#ifdef DEBUG
    Debug_printf("New Destination %s port %s\n",new_dest_str.c_str(),new_port_str.c_str());
#endif

    port=atoi(new_port_str.c_str());
    strcpy(dest,new_dest_str.c_str());


    return false; // no error.
}

bool networkProtocolUDP::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    bool err = false;

    switch (cmdFrame->comnd)
    {
    case 'D':
        err = special_set_destination(sp_buf, len);
        break;
    }
    return err;
}

int networkProtocolUDP::available()
{
    return saved_rx_buffer_len;
}