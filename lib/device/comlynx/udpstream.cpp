#ifdef BUILD_LYNX
#include "udpstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

//#define DEBUG_UDPSTREAM

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


void lynxUDPStream::comlynx_enable_redeye()         // also can be used to reset redeye mode
{
    redeye_mode = true;
    redeye_logon = true;
    redeye_game = 0;
    redeye_players = 0;

#ifdef DEBUG
    Debug_println("UDPSTREAM redeye mode ENABLED");
#endif
}


void lynxUDPStream::comlynx_disable_redeye()
{
    redeye_mode = false;
    redeye_logon = true;

#ifdef DEBUG
    Debug_println("UDPSTREAM redeye mode DISABLED");
#endif
}


void lynxUDPStream::comlynx_handle_udpstream()
{
    bool good_packet = true;

    // if there’s data available, read a packet
    int packetSize = udpStream.parsePacket();
    if (packetSize > 0)
    {
        udpStream.read(buf_net, UDPSTREAM_BUFFER_SIZE);

        // Lynx Redeye protocol handling
        if (redeye_mode) {
            if (packetSize < 3) {               // check that we have a packet at least 3 bytes
                good_packet = false;
            #ifdef DEBUG
                Debug_println("UDPStream Redeye IN - bad packet size < 3");
            #endif
            }
            else {
                if (!comlynx_redeye_checksum(buf_net)) {     // check the checksum
                    good_packet = false;
                #ifdef DEBUG
                    Debug_println("UDPStream Redeye IN - checksum failed");
                    util_dump_bytes(buf_net, packetSize);
                #endif
                }
            }
        }

        if (good_packet) {
            // Send to Lynx UART
            _comlynx_bus->wait_for_idle();
            SYSTEM_BUS.write(buf_net, packetSize);
        #ifdef DEBUG_UDPSTREAM
            Debug_print("UDP-IN: ");
            util_dump_bytes(buf_net, packetSize);
        #endif
            SYSTEM_BUS.read(buf_net, packetSize); // Trash what we just sent over serial
        }
    }

    // Read the data until there's a pause in the incoming stream

    SYSTEM_BUS.flush();

    buf_stream_index = 0;
    if (SYSTEM_BUS.available() > 0)
    {
        while (true)
        {
            if (SYSTEM_BUS.available() > 0)
            {
                // Collect bytes read in our buffer
                buf_stream[buf_stream_index] = (char)SYSTEM_BUS.read();
                if (redeye_mode && (buf_stream_index == 0)) {           // Check first byte
                  if ((buf_stream[0] < 1) || (buf_stream[0] > 6))       // discard bad size byte (must be between 1 and 6)
                    continue;
                }

                if (buf_stream_index < UDPSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
            }
            else
            {
                fnSystem.delay_microseconds(UDPSTREAM_PACKET_TIMEOUT);
                if (SYSTEM_BUS.available() <= 0)
                    break;
            }
        }

        // Did we get any data?
        if (buf_stream_index == 0)
            return;

        // Lynx Redeye protocol handling
        if (redeye_mode) {
            if (buf_stream_index < 3) {     // packets have to be at least three bytes
                #ifdef DEBUG
                    Debug_println("UDPStream Redeye OUT - bad packet size < 3");
                    util_dump_bytes(buf_stream, buf_stream_index);
                #endif
                return;                     // bail out
            }

            if (comlynx_redeye_checksum(buf_stream)) {
                if (redeye_logon) {                                         // Are we in logon phase?
                    if (buf_stream[0] == 5) {                               // and this is a logon packet (size = 5)
                        redeye_game = (buf_stream[4]+(buf_stream[5]<<8));   // collect redye game ID

                        if (remap_game_id) {                                // need to remap the game id?
                            redeye_remap_game_id();
                            redeye_recalculate_checksum();                  // recalculate the checksum
                        }

                        redeye_players = 0;                                 // extract number of players in game
                        for(int i = 0; i < 8; i++)
                            redeye_players += (buf_stream[3] >> i) & 0x01;

                        if (buf_stream[1] == 2) {                           // redeye logon phase ending?
                            redeye_logon = false;                           // stop handling logon phase packets
                            #ifdef DEBUG
                                Debug_println("UDPSTREAM redeye logon phase ending");
                                Debug_printf("UDPSTREAM redeye game: %d, redeye_players: %d\n", redeye_game, redeye_players);
                            #endif
                        }

                    }
                }

                #ifdef DEBUG
                    //Debug_printf("UDPSTREAM redeye_game: %d, redeye_players: %d\n", redeye_game, redeye_players);
                #endif

                // Send what we've collected over WiFi
                //udpStream.beginPacket(udpstream_host_ip, udpstream_port); // remote IP and port
                //udpStream.write(buf_stream, buf_stream_index);
                //udpStream.endPacket();
            }
            else {
                #ifdef DEBUG
                    Debug_println("UDPSTREAM Redeye OUT - checksum failed");
                    util_dump_bytes(buf_stream, buf_stream_index);
                #endif
                return;
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
    }
}


 /* Calculate the checksum of incoming from the lynx redeye packets
    Return true if ok, false if not

    typical message:
    05 00 00 01 FF FF F8

    Checksum is calculated on size, plus message bytes.
 */
 bool lynxUDPStream::comlynx_redeye_checksum(uint8_t *buf)
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf[0];                          // get message size
    if ((size == 0) || (size > 6)) {        // check packets are in range
        //Debug_printf("checksum size %d %d\n", size, buf[0]);
        return false;
    }

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf[i];
    }

    if ((ck & 0xFF) == buf[size+1])
        return true;
    else
        return false;

 }


 /* Recalculate the checksum of the lynx redeye packet.

    Checksum is calculated on size, plus message bytes.
 */
 void lynxUDPStream::redeye_recalculate_checksum()
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf_stream[0];  // get message size

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf_stream[i];
    }

    // set new checksum on packet
    buf_stream[size+1] = (ck & 0xFF);
    return;
 }


/* redeye_remap_game_id
 *
 * Remap certain game IDs (based on GUI setting) so that we
 * have a unique game id for each game.
 *
 * 0xFFFF       0xE001  Relief Pitcher
 * 0xFFFF       0xE002  Pit Fighter
 * 0xFFFF       0xE003  Double Dragon
 * 0xFFFF       0xE004  European Soccer
 * 0xFFFF       0xE005  Lynx Casino
 * 0xFFFF       0xE006  Super Off-Road
 *
 * redeye_game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxUDPStream::redeye_remap_game_id()
{
  uint16_t gid;


  // Double check that game id even needs to be remapped
  gid = (buf_stream[4]+(buf_stream[5]<<8));
  if (gid != 0xFFFF)
    return;

  // Set new game ID
  buf_stream[4] = new_game_id & 0xFF;
  buf_stream[5] = (new_game_id >> 8) & 0xFF;
  return;
}


void lynxUDPStream::comlynx_process(uint8_t b)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_LYNX */
