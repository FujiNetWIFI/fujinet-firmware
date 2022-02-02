/* Largely taken from Arduino WiFiUDP with some changes
*/
#ifndef _FN_UDP_
#define _FN_UDP_

#include <lwip/netdb.h>

#include "cbuf.h"


class fnUDP
{
private:
    int udp_server = -1;
    in_addr_t multicast_ip = IPADDR_NONE;
    in_addr_t remote_ip = IPADDR_NONE;
    uint16_t server_port = 0;
    uint16_t remote_port = 0;
    char * tx_buffer = nullptr;
    size_t tx_buffer_len = 0;
    cbuf * rx_buffer = nullptr;

public:
    fnUDP();
    ~fnUDP();

    void stop();

    bool begin(in_addr_t a, uint16_t p);
    bool begin(uint16_t p);

    bool beginMulticast(in_addr_t a, uint16_t p);
    bool beginMulticastPacket();

    bool beginPacket();
    bool beginPacket(in_addr_t ip, uint16_t port);
    bool beginPacket(const char *host, uint16_t port);

    bool endPacket();

    size_t write(uint8_t);
    size_t write(const uint8_t *buffer, size_t size);

    int parsePacket();

    int read();
    int read(unsigned char* buffer, size_t len);
    int read(char* buffer, size_t len);

    int peek();
    int available();
    void flush();

    in_addr_t remoteIP();
    uint16_t remotePort();
};

#endif //_FN_UDP_
