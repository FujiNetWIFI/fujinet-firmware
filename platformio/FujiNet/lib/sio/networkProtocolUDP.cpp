#include "networkProtocolUDP.h"

networkProtocolUDP::networkProtocolUDP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::ctor\n");
#endif
}

bool networkProtocolUDP::open(networkDeviceSpec *spec)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::OPEN %s \n",spec->toChar());
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
    Debug_printf("networkProtocolUDP::read %d bytes\n",len);
#endif    
    return (udp.read(rx_buf, len) == len);
}

bool networkProtocolUDP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("networkProtocolUDP::write %d bytes\n",len);
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
    status_buf[0] = len & 0xFF;
    status_buf[1] = len >> 8;
    status_buf[2] = status_buf[3] = 0;
    return true;
}