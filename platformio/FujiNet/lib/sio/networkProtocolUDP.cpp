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

bool networkProtocolUDP::open(networkDeviceSpec *spec)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::OPEN %s \n", spec->toChar());
#endif
    return udp.begin(spec->port);
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
    return (udp.read(rx_buf, len) == len);
}

bool networkProtocolUDP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::write %d bytes\n", len);
#endif
    udp.beginPacket(dest, port);
    int l = udp.write(tx_buf, len);
    udp.endPacket();
    if (l < len)
        return false;
    else
        return true;
}

bool networkProtocolUDP::status(byte *status_buf)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::status\n");
#endif
    unsigned short len = udp.parsePacket();

    // Set destination automatically to remote address.
    strcpy(dest, udp.remoteIP().toString().c_str());
    port = udp.remotePort();

    status_buf[0] = len & 0xFF;
    status_buf[1] = len >> 8;
    status_buf[2] = status_buf[3] = 0;
    return true;
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