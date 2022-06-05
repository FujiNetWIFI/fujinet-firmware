#ifdef BUILD_LYNX
#include "udpstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

void lynxUDPStream::comlynx_enable_udpstream()
{
    // Open the UDP connection
    udpStream.begin(udpstream_port);

    udpstreamActive = true;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode ENABLED");
#endif
}

void lynxUDPStream::comlynx_disable_udpstream()
{
    udpStream.stop();
    udpstreamActive = false;
#ifdef DEBUG
    Debug_println("UDPSTREAM mode DISABLED");
#endif
}

void lynxUDPStream::comlynx_handle_udpstream()
{
    // if there’s data available, read a packet
    int packetSize = udpStream.parsePacket();
    if (packetSize > 0)
    {
        udpStream.read(buf_net, UDPSTREAM_BUFFER_SIZE);
        // Send to Lynx UART
        _comlynx_bus->wait_for_idle();
        fnUartSIO.write(buf_net, packetSize);
#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-IN: ");
        util_dump_bytes(buf_net, packetSize);
#endif
    }

    // Read the data until there's a pause in the incoming stream
    if (fnUartSIO.available() > 0)
    {
        while (true)
        {
            if (fnUartSIO.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)fnUartSIO.read();
                if (buf_stream_index < UDPSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(UDPSTREAM_PACKET_TIMEOUT);
                if (fnUartSIO.available() <= 0)
                    break;
            }
        }

        // Send what we've collected over WiFi
        udpStream.beginPacket(udpstream_host_ip, udpstream_port); // remote IP and port
        udpStream.write(buf_stream, buf_stream_index);
        udpStream.endPacket();

#ifdef DEBUG_UDPSTREAM
        Debug_print("UDP-OUT: ");
        util_dump_bytes(buf_stream, buf_stream_index);
#endif
        buf_stream_index = 0;
    }
}

void lynxUDPStream::comlynx_process(uint8_t b)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */