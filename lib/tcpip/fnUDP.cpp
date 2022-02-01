/* Largely taken from Arduino WiFiUDP with some changes
*/

#include "fnUDP.h"

#include "../../include/debug.h"

#include "fnDNS.h"

#define UDP_RXTX_BUFLEN 1460

fnUDP::fnUDP()
{
}

fnUDP::~fnUDP()
{
    stop();
}

void fnUDP::stop()
{
    if (tx_buffer)
    {
        delete[] tx_buffer;
        tx_buffer = NULL;
    }
    tx_buffer_len = 0;

    if (rx_buffer)
    {
        cbuf *b = rx_buffer;
        rx_buffer = nullptr;
        delete b;
    }

    if (udp_server == -1)
        return;

    if (multicast_ip != IPADDR_NONE)
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = (in_addr_t)multicast_ip;
        mreq.imr_interface.s_addr = (in_addr_t)0;
        setsockopt(udp_server, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        multicast_ip = IPADDR_NONE;
    }
    close(udp_server);
    udp_server = -1;
}

bool fnUDP::begin(uint16_t p)
{
    return begin(IPADDR_ANY, p);
}

bool fnUDP::begin(in_addr_t address, uint16_t port)
{
    stop();

    server_port = port;

    tx_buffer = new char[UDP_RXTX_BUFLEN];
    if (!tx_buffer)
    {
        Debug_printf("could not create tx buffer: %d", errno);
        return false;
    }

    if ((udp_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1)
    {
        Debug_printf("could not create socket: %d", errno);
        return false;
    }

    int yes = 1;
    if (setsockopt(udp_server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        Debug_printf("could not set socket option: %d", errno);
        stop();
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    addr.sin_addr.s_addr = address;
    if (bind(udp_server, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        Debug_printf("could not bind socket: %d", errno);
        stop();
        return false;
    }

    fcntl(udp_server, F_SETFL, O_NONBLOCK);

    return true;
}

bool fnUDP::beginPacket()
{
    // We need a port
    if (!remote_port)
        return false;
    // We need an address
    if (remote_ip == IPADDR_NONE)
        return false;

    // allocate tx_buffer if is necessary
    if (!tx_buffer)
    {
        tx_buffer = new char[UDP_RXTX_BUFLEN];
        if (!tx_buffer)
        {
            Debug_printf("could not create tx buffer: %d", errno);
            return false;
        }
    }
    tx_buffer_len = 0;

    // check if socket is already open
    if (udp_server != -1)
        return true;

    if ((udp_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1)
    {
        Debug_printf("could not create socket: %d", errno);
        return false;
    }

    fcntl(udp_server, F_SETFL, O_NONBLOCK);

    return true;
}

bool fnUDP::beginPacket(in_addr_t ip, uint16_t port)
{
    remote_ip = ip;
    remote_port = port;
    return beginPacket();
}

bool fnUDP::beginPacket(const char *host, uint16_t port)
{

    remote_ip = get_ip4_addr_by_name(host);
    remote_port = port;
    return beginPacket();
}

bool fnUDP::beginMulticastPacket()
{
    if (!server_port || multicast_ip == IPADDR_ANY)
        return 0;

    remote_ip = multicast_ip;
    remote_port = server_port;

    return beginPacket();
}

bool fnUDP::beginMulticast(in_addr_t a, uint16_t p)
{
    if (begin(IPADDR_ANY, p))
    {
        if (a != INADDR_NONE)
        {
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = a;
            mreq.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(udp_server, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            {
                Debug_printf("could not join igmp: %d", errno);
                stop();
                return false;
            }
            multicast_ip = a;
        }
        return true;
    }
    return false;
}

// Put byte in buffer until full. Send if full and continue.
size_t fnUDP::write(uint8_t data)
{
    if (tx_buffer_len == UDP_RXTX_BUFLEN)
    {
        endPacket();
        tx_buffer_len = 0;
    }
    tx_buffer[tx_buffer_len++] = data;
    return 1;
}

// Put bytes in buffer until full. Send if full and continue.
size_t fnUDP::write(const uint8_t *buffer, size_t size)
{
    size_t i;
    for (i = 0; i < size; i++)
        write(buffer[i]);
    return i;
}

int fnUDP::parsePacket()
{
    if (rx_buffer)
        return 0;

    struct sockaddr_in si_other;
    int slen = sizeof(si_other);
    int len;
    char *buf = new char[UDP_RXTX_BUFLEN];
    if (!buf)
        return 0;

    if ((len = recvfrom(udp_server, buf, UDP_RXTX_BUFLEN, MSG_DONTWAIT, (struct sockaddr *)&si_other, (socklen_t *)&slen)) == -1)
    {
        delete[] buf;
        if (errno != EWOULDBLOCK)
            Debug_printf("could not receive data: %d", errno);
        return 0;
    }

    remote_ip = si_other.sin_addr.s_addr;
    remote_port = ntohs(si_other.sin_port);

    if (len > 0)
    {
        rx_buffer = new cbuf(len);
        rx_buffer->write(buf, len);
    }
    delete[] buf;
    return len;
}

int fnUDP::read()
{
    if (!rx_buffer)
        return -1;
    int out = rx_buffer->read();
    if (!rx_buffer->available())
    {
        cbuf *b = rx_buffer;
        rx_buffer = nullptr;
        delete b;
    }
    return out;
}

int fnUDP::read(char *buffer, size_t len)
{
    if (!rx_buffer)
        return 0;
    int out = rx_buffer->read(buffer, len);
    if (!rx_buffer->available())
    {
        cbuf *b = rx_buffer;
        rx_buffer = nullptr;
        delete b;
    }
    return out;
}

int fnUDP::read(unsigned char *buffer, size_t len)
{
    return read((char *)buffer, len);
}

int fnUDP::available()
{
    if (!rx_buffer)
        return 0;
    return rx_buffer->available();
}

int fnUDP::peek()
{
    if (!rx_buffer)
        return -1;
    return rx_buffer->peek();
}

void fnUDP::flush()
{
    if (!rx_buffer)
        return;
    cbuf *b = rx_buffer;
    rx_buffer = nullptr;
    delete b;
}

// Send any data currently in buffer to client
bool fnUDP::endPacket()
{
    struct sockaddr_in recipient;
    recipient.sin_addr.s_addr = remote_ip;
    recipient.sin_family = AF_INET;
    recipient.sin_port = htons(remote_port);

    int sent = sendto(udp_server, tx_buffer, tx_buffer_len, 0, (struct sockaddr *)&recipient, sizeof(recipient));
    if (sent < 0)
    {
        Debug_printf("could not send data: %d", errno);
        return false;
    }
    return true;
}

in_addr_t fnUDP::remoteIP()
{
    return remote_ip;
}

uint16_t fnUDP::remotePort()
{
    return remote_port;
}
