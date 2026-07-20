#ifdef BUILD_LYNX
#include "netstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

//#define DEBUG_NETSTREAM

void lynxNetStream::comlynx_enable_netstream()
{
    // Open the connection
    netStream.begin(netstream_port);

    netstreamActive = true;
#ifdef DEBUG
    Debug_println("---");
    Debug_println("NETSTREAM mode ENABLED");
#endif
}

void lynxNetStream::comlynx_disable_netstream()
{
    netStream.stop();
    netstreamActive = false;
#ifdef DEBUG
    Debug_println("NETSTREAM mode DISABLED");
    Debug_println("---");
#endif
}


void lynxNetStream::comlynx_handle_netstream()
{
    uint8_t n;

    // if there’s data available, read a packet
    int packetSize = netStream.parsePacket();
    if (packetSize > 0) {
        netStream.read(buf_net, NETSTREAM_BUFFER_SIZE);

        #ifdef DEBUG_NETSTREAM
            Debug_print("UDP-IN: ");
            util_dump_bytes(buf_net, packetSize);
        #endif

        _comlynx_bus->wait_for_idle();
        SYSTEM_BUS.write(buf_net, packetSize);
        SYSTEM_BUS.read(buf_net, packetSize); // Trash what we just sent over serial
    }

    //SYSTEM_BUS.flush();
    
    // Read the data until there's a pause in the incoming stream
    buf_stream_index = 0;
    if (SYSTEM_BUS.available() > 0) {
        while (true) {
            n = 0;  
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

        // Send what we've collected over WiFi
        netStream.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
        netStream.write(buf_stream, buf_stream_index);
        netStream.endPacket();

        #ifdef DEBUG_NETSTREAM
            Debug_print("UDP-OUT: ");
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
