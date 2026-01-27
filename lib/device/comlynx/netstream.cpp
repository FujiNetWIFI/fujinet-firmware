#ifdef BUILD_LYNX
#include "netstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

#ifdef ESP_PLATFORM
#include "fnUART.h"
#define FN_BUS_LINK fnUartBUS
#else
#define FN_BUS_LINK fnUartBUS
#endif

//#define DEBUG_NETSTREAM

bool lynxNetStream::ensure_tcp_connected()
{
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
    return true;
}

void lynxNetStream::comlynx_enable_netstream()
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        // Open the UDP connection
        netStreamUdp.begin(netstream_port);
    }
    else
    {
        // Open the TCP connection
        ensure_tcp_connected();
    }

    netstreamActive = true;
#ifdef DEBUG
    Debug_println("NETSTREAM mode ENABLED");
#endif
}

void lynxNetStream::comlynx_disable_netstream()
{
    netStreamUdp.stop();
    netStreamTcp.stop();
    netstreamActive = false;
#ifdef DEBUG
    Debug_println("NETSTREAM mode DISABLED");
#endif
}

void lynxNetStream::comlynx_handle_netstream()
{
    int packetSize = 0;

    if (netstreamMode == NetStreamMode::UDP)
    {
        packetSize = netStreamUdp.parsePacket();
        if (packetSize > 0)
            netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);
    }
    else if (ensure_tcp_connected())
    {
        size_t available = netStreamTcp.available();
        if (available > 0)
        {
            size_t to_read = (available > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : available;
            packetSize = netStreamTcp.read(buf_net, to_read);
        }
    }

    if (packetSize > 0)
    {
        // Send to Lynx UART
        ComLynx.wait_for_idle();
        FN_BUS_LINK.write(buf_net, packetSize);
#ifdef DEBUG_NETSTREAM
        Debug_print("NET-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
        FN_BUS_LINK.readBytes(buf_net, packetSize); // Trash what we just sent over serial
    }

    // Read the data until there's a pause in the incoming stream
    if (FN_BUS_LINK.available() > 0)
    {
        while (true)
        {
            if (FN_BUS_LINK.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)FN_BUS_LINK.read();
                if (buf_stream_index < NETSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(NETSTREAM_PACKET_TIMEOUT);
                if (FN_BUS_LINK.available() <= 0)
                    break;
            }
        }

        // Send what we've collected over WiFi
        if (netstreamMode == NetStreamMode::UDP)
        {
            netStreamUdp.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
            netStreamUdp.write(buf_stream, buf_stream_index);
            netStreamUdp.endPacket();
        }
        else
        {
            if (!ensure_tcp_connected())
                return;
            netStreamTcp.write(buf_stream, buf_stream_index);
        }

#ifdef DEBUG_NETSTREAM
        Debug_print("NET-OUT: ");
        util_dump_bytes(buf_stream, buf_stream_index);
#endif
        buf_stream_index = 0;
    }
}

void lynxNetStream::comlynx_process(uint8_t b)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_LYNX */
