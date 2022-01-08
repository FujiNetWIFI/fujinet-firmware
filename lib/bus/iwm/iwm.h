#ifndef IWM_H
#define IWM_H

#include "../../include/debug.h"

#include "bus.h"
#include <cstdint>
#include <forward_list>
#include "fnFS.h"

class iwmBus;

class iwmDevice
{
private:
    // temp device for disk image
    // todo: turn into FujiNet practice
  struct device
  {
    FILE *sdf;
    uint8_t device_id;    //to hold assigned device id's for the partitions
    unsigned long blocks;       //how many 512-byte blocks this image has
    unsigned int header_offset; //Some image files have headers, skip this many bytes to avoid them
    bool writeable;
};

    device d;
    // iwm packet handling
    uint8_t packet_buffer[605]; //smartport packet buffer

    int decode_data_packet(void); //decode smartport 512 byte data packet

    void encode_data_packet(uint8_t source); //encode smartport 512 byte data packet
    void encode_write_status_packet(uint8_t source, uint8_t status);
    void encode_init_reply_packet(uint8_t source, uint8_t status);
    void encode_status_reply_packet();
    void encode_error_reply_packet(uint8_t source);
    void encode_status_dib_reply_packet();

    void encode_extended_data_packet(uint8_t source);
    void encode_extended_status_reply_packet();
    void encode_extended_status_dib_reply_packet();


public:
    bool device_active;
    virtual void shutdown() = 0;

    /**
     * @brief Get the iwmBus object that this iwmDevice is attached to.
     */
    iwmBus iwm_get_bus();
};

class iwmBus
{
private:
    std::forward_list<iwmDevice *> _daisyChain;

    // low level bit-banging i/o functions
    struct iwm_timer_t
    {
        uint32_t tn;
        uint32_t t0;
    } iwm_timer;

    void timer_config();
    void iwm_timer_latch();
    void iwm_timer_read();
    void iwm_timer_alarm_set(int s);
    void iwm_timer_alarm_snooze(int s);
    void iwm_timer_wait();
    void iwm_timer_reset();

    void iwm_rddata_set();
    void iwm_rddata_clr();
    void iwm_rddata_disable();
    void iwm_rddata_enable();
    bool iwm_wrdata_val();
    bool iwm_req_val();
    void iwm_ack_set();
    void iwm_ack_clr();
    void iwm_ack_enable();
    void iwm_ack_disable();
    int iwm_handshake();
    bool iwm_phase_val(int p);
    
    enum class iwm_phases_t
    {
        idle = 0,
        reset,
        enable
    };
    iwm_phases_t iwm_phases();
#ifdef DEBUG
    iwm_phases_t oldphase;
#endif

    int iwm_rx_packet(uint8_t *a);
    int SendPacket(uint8_t *a);

public:
void shutdown() {};

};

extern iwmBus IWM;

#endif // guard

