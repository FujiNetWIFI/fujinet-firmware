#ifndef NETSTREAM_H
#define NETSTREAM_H

#include "bus.h"

#include "fnUDP.h"

#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#define NETSTREAM_BUFFER_SIZE 8192
#define NETSTREAM_PACKET_TIMEOUT 5000
#define MIDI_PORT 5004
#define MIDI_BAUDRATE 31250

class lynxNetStream : public virtualDevice
{
private:
    fnUDP netStream;
    systemBus *_comlynx_bus = nullptr;

    uint8_t buf_net[NETSTREAM_BUFFER_SIZE];
    uint8_t buf_stream[NETSTREAM_BUFFER_SIZE];

    uint8_t buf_stream_index=0;

    void comlynx_process() override;
    bool comlynx_redeye_checksum(uint8_t *buf);         // check the redeye checksum
    void redeye_recalculate_checksum();                 // recalculate redeye packet checksum (for remapped game_id)
    void redeye_remap_game_id();                        // remap game_id to provide a unique game_id

public:
    bool netstreamActive = false; // If we are in netstream mode or not
    in_addr_t netstream_host_ip = IPADDR_NONE;
    int netstream_port;
    bool redeye_mode = true;        // redeye UDP stream mode
    bool redeye_logon = true;       // in redeye logon phase
    uint16_t redeye_game = 0;       // redeye game ID
    uint8_t redeye_players = 0;     // redeye number of players - not sure we need this
    bool remap_game_id = false;     // should we be remapping the game id?
    uint16_t new_game_id = 0xFFFF;  // the new game ID to remap, set from Web GUI


    void comlynx_enable_netstream();  // setup netstream
    void comlynx_disable_netstream(); // stop netstream
    void comlynx_handle_netstream();  // Handle incoming & outgoing data for netstream

    void comlynx_enable_redeye();     // setup redeye mode overtop netstream
    void comlynx_disable_redeye();    // disable redeye mode

};

#endif