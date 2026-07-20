#ifdef BUILD_LYNX
#include "netstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <errno.h>
#include <sys/socket.h>
#endif

//#define DEBUG_NETSTREAM

bool lynxNetStream::ensure_netstream_ready()
{
    if (netstreamMode == NetStreamMode::UDP)
        return true;
    if (netStreamTcp.connected())
        return true;
    if (netstream_host_ip == IPADDR_NONE || netstream_port <= 0)
        return false;
    if (!netStreamTcp.connect(netstream_host_ip, (uint16_t)netstream_port))
    {
#ifdef DEBUG_NETSTREAM
        Debug_println("NETSTREAM: TCP connect failed");
#endif
        return false;
    }
    netStreamTcp.setNoDelay(true);
    if (netstreamRegisterEnabled)
    {
        const char *str = "REGISTER";
        netStreamTcp.write((const uint8_t *)str, strlen(str));
    }
    buf_stream_index = 0;
    buf_net_index = 0;
    return true;
}

void lynxNetStream::send_net_packet(const uint8_t *buf, size_t len)
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        netStreamUdp.beginPacket(netstream_host_ip, netstream_port);
        netStreamUdp.write(buf, len);
        netStreamUdp.endPacket();
    }
    else if (ensure_netstream_ready())
    {
        netStreamTcp.write(buf, len);
    }
}

void lynxNetStream::comlynx_enable_netstream()
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        netStreamUdp.begin(netstream_port);
        if (netstreamRegisterEnabled)
        {
            const char *str = "REGISTER";
            send_net_packet((const uint8_t *)str, strlen(str));
        }
    }
    else
    {
        ensure_netstream_ready();
    }

    netstreamActive = true;
#ifdef DEBUG
    Debug_println("---");
    Debug_println("NETSTREAM mode ENABLED");
#endif
}

void lynxNetStream::comlynx_disable_netstream()
{
    netStreamTcp.stop();
    netStreamUdp.stop();
    buf_stream_index = 0;
    buf_net_index = 0;
    netstreamActive = false;
#ifdef DEBUG
    Debug_println("NETSTREAM mode DISABLED");
    Debug_println("---");
#endif
}

void lynxNetStream::process_net_packet(const uint8_t *buf, size_t len)
{
    SYSTEM_BUS.wait_for_idle();
    SYSTEM_BUS.write(buf, len);
#ifdef DEBUG_NETSTREAM
    Debug_print(netstreamMode == NetStreamMode::UDP ? "UDP-IN: " : "TCP-IN: ");
    util_dump_bytes(buf, len);
#endif
    SYSTEM_BUS.read(buf_stream, len); // Trash what we just sent over serial
}

void lynxNetStream::drain_tcp_to_lynx()
{
    if (!ensure_netstream_ready())
        return;

    while (true)
    {
#ifdef ESP_PLATFORM
        int bytes_read = recv(netStreamTcp.fd(), (char *)buf_net, NETSTREAM_BUFFER_SIZE, MSG_DONTWAIT);
        if (bytes_read <= 0)
        {
            if (bytes_read == 0)
                netStreamTcp.stop();
            else if (errno != EWOULDBLOCK && errno != EAGAIN)
                netStreamTcp.stop();
            break;
        }
#else
        size_t available = netStreamTcp.available();
        if (available == 0)
            break;
        size_t to_read = (available > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : available;
        int bytes_read = netStreamTcp.read(buf_net, to_read);
        if (bytes_read <= 0)
            break;
#endif
        process_net_packet(buf_net, bytes_read);
    }
}

void lynxNetStream::comlynx_handle_netstream()
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        int packetSize = netStreamUdp.parsePacket();
        if (packetSize > 0)
        {
            netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);
            process_net_packet(buf_net, packetSize);
        }
    }
    else
    {
        drain_tcp_to_lynx();
    }

    // Read the data until there's a pause in the incoming stream
    buf_stream_index = 0;
    if (SYSTEM_BUS.available() > 0)
    {
        while (true)
        {
            if (SYSTEM_BUS.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)SYSTEM_BUS.read();

                if (buf_stream_index < NETSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(NETSTREAM_PACKET_TIMEOUT);
                if (SYSTEM_BUS.available() <= 0)
                    break;
            }
        }

        // Did we get any data?
        if (buf_stream_index == 0)
            return;

        send_net_packet(buf_stream, buf_stream_index);

#ifdef DEBUG_NETSTREAM
        Debug_print(netstreamMode == NetStreamMode::UDP ? "UDP-OUT: " : "TCP-OUT: ");
        util_dump_bytes(buf_stream, buf_stream_index);
#endif
    }
}

void lynxNetStream::comlynx_process()
{
    // Nothing to do here
    return;
}

#endif /* BUILD_LYNX */
