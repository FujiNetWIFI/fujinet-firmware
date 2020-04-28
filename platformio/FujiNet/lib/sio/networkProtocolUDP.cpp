#include "networkProtocolUDP.h"

networkProtocolUDP::networkProtocolUDP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::ctor\n");
#endif
}

networkProtocolUDP::~networkProtocolUDP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::dtor\n");
#endif
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
    }

    return udp.begin(atoi(urlParser->port.c_str()));
}

bool networkProtocolUDP::close()
{
    udp.stop();
    return true;
}

bool networkProtocolUDP::read(byte *rx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::read %d bytes\n", len);
#endif

    if (saved_rx_buffer_len > 0)
    {
        memcpy(rx_buf, saved_rx_buffer, len);
    }
    else
    {
        memset(rx_buf, 0, len);
    }

    saved_rx_buffer_len = 0;

    if (len == saved_rx_buffer_len)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool networkProtocolUDP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::write %d bytes to dest: %s port %d\n", len, dest, port);
#endif
    udp.beginPacket(dest, port);
    int l = udp.write(tx_buf, len);
    udp.endPacket();
    if (l < len)
        return true;
    else
        return false;
}

bool networkProtocolUDP::status(byte *status_buf)
{
    unsigned short len = udp.parsePacket();

    if (len > 0)
    {
        // Set destination automatically to remote address.
        strcpy(dest, udp.remoteIP().toString().c_str());
        port = udp.remotePort();

        saved_rx_buffer_len = len;
        status_buf[0] = len & 0xFF;
        status_buf[1] = len >> 8;
        status_buf[3] = 0;
        udp.read(saved_rx_buffer, len);
    }
    status_buf[2] = 1;
    return false;
}

bool networkProtocolUDP::special_supported_80_command(unsigned char comnd)
{
    if (comnd == 'D') // Set DEST address
        return true;
    else
        return false;
}

bool networkProtocolUDP::special_set_destination(byte *sp_buf, unsigned short len, unsigned short new_port)
{
    strncpy(dest, (const char *)sp_buf, sizeof(dest));
    port = new_port;
    return false; // no error.
}

bool networkProtocolUDP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    bool err = false;

    switch (cmdFrame->comnd)
    {
    case 'D':
        err = special_set_destination(sp_buf, len, (cmdFrame->aux2 * 256 + cmdFrame->aux1));
        break;
    }
    return err;
}