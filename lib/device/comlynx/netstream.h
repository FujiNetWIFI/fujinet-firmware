#ifndef NETSTREAM_H
#define NETSTREAM_H

#include "bus.h"
#include "fnUDP.h"
#include "redeye.h"


#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_1_BIT

#define NETSTREAM_BUFFER_SIZE 8192
#define NETSTREAM_PACKET_TIMEOUT 80
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

public:
    bool netstreamActive = false; // If we are in netstream mode or not
    in_addr_t netstream_host_ip = IPADDR_NONE;
    int netstream_port;

    void comlynx_enable_netstream();  // setup netstream
    void comlynx_disable_netstream(); // stop netstream
    void comlynx_handle_netstream();  // Handle incoming & outgoing data for netstream

    bool redeye_mode = true;        // redeye UDP stream mode
    GAME_T game;                    // redeye game info and state

    void comlynx_enable_redeye();                       // setup redeye mode overtop netstream
    void comlynx_disable_redeye();                      // disable redeye mode
    void redeye_reset_game();
    void comlynx_handle_redeye_netstream();             // handle redeye netstream (when in redeye mode)
    bool redeye_checksum(uint8_t *buf);                 // check the redeye checksum
    void redeye_recalculate_checksum(uint8_t *buf);     // recalculate redeye packet checksum (for remapped game_id)
    void redeye_remap_game_id(uint8_t *buf, uint16_t remap);          // remap game_id to provide a unique game_id
    uint8_t redeye_find_game(uint16_t gid);
    bool redeye_valid_sequence_data(uint8_t seq, uint8_t player_mask);
    bool redeye_check_data_recv();
    bool redeye_validate_packet(uint8_t *buf, uint8_t bufsize);
    bool redeye_check_logon_state();

    void redeye_process_logon_packet_from_net(uint8_t *buf);
    void redeye_process_game_packet_from_net(uint8_t *buf);
    void redeye_process_logon_packet_from_lynx(uint8_t *buf);
    void redeye_process_game_packet_from_lynx(uint8_t *buf);

};

#endif