#ifndef _SPSD_H
#define _SPSD_H

#include <cstdint>
#include <string>

#include "fnSystem.h"
#include "fnFsSD.h"

// info for porting over to FujiNet Bus and Device classes. 
// I'm putting comments by functions and variables indicating 
// FNBUS FNDEV 

#define FNBUS 
#define FNDEV 

class spDevice 
{
private:

FNBUS enum class phasestate_t {
    idle = 0,
    reset,
    enable
  } phasestate;
#ifdef DEBUG  
  phasestate_t oldphase = phasestate_t::reset; // can use for debug printing of phase changes
#endif 

    bool reset_state = false; // maybe not needed if control is passed to a reset handler

public:

FNDEV void encode_data_packet(uint8_t source); //encode smartport 512 byte data packet
FNDEV  int decode_data_packet(void);                  //decode smartport 512 byte data packet
FNDEV  void encode_write_status_packet(uint8_t source, uint8_t status);
FNDEV  void encode_init_reply_packet(uint8_t source, uint8_t status);
FNDEV  void encode_status_reply_packet(struct device d);
  int packet_length(void);
  bool is_valid_image(FILE imageFile);

FNDEV  void handle_readblock();
FNBUS void handle_init(); // put this is bus so the bus can assign device numbers to all the devices

  //unsigned char packet_buffer[768];   //smartport packet buffer
  uint8_t packet_buffer[605]; //smartport packet buffer
  //unsigned char sector_buffer[512];   //ata sector data buffer
  uint8_t packet_byte;
  //int count;

  // We need to remember several things about a device, not just its ID
  // todo: turn into FujiNet practice
  struct device
  {
    FILE *sdf;
    uint8_t device_id;    //to hold assigned device id's for the partitions
    unsigned long blocks;       //how many 512-byte blocks this image has
    unsigned int header_offset; //Some image files have headers, skip this many bytes to avoid them
    bool writeable;
};

#define NUM_PARTITIONS 1
device devices[NUM_PARTITIONS];

// these are low-level bus phyical layer helper functions
// make these two vars part of a structure for clarity?
uint32_t tn;
uint32_t t0;
void hw_timer_latch();
void hw_timer_read();
void hw_timer_alarm_set(int s);
void hw_timer_alarm_snooze(int s); 
void hw_timer_wait();
void hw_timer_reset();
void smartport_rddata_set();
void smartport_rddata_clr();
void smartport_rddata_disable();
void smartport_rddata_enable();
bool smartport_wrdata_val(); // can these be bool with implicit typecast?
bool smartport_req_val(); // can these be bool with implicit typecast?
void smartport_ack_set();
void smartport_ack_clr();
void smartport_ack_enable();
void smartport_ack_disable();
void smartport_extra_set();
void smartport_extra_clr();
int smartport_handshake();
bool smartport_phase_val(int p);
phasestate_t smartport_phases();


int ReceivePacket(uint8_t *a);
int SendPacket(const uint8_t *a);
//void encode_data_packet (uint8_t source);
void encode_extended_data_packet (uint8_t source);
//int decode_data_packet (void);
//void encode_write_status_packet(unsigned char source, unsigned char status);
//void encode_init_reply_packet (unsigned char source, unsigned char status);
void encode_status_reply_packet (device d);
void encode_extended_status_reply_packet (device d);
void encode_error_reply_packet (uint8_t source);
void encode_status_dib_reply_packet (device d);
void encode_extended_status_dib_reply_packet (device d);
int verify_cmdpkt_checksum(void);


void print_packet (uint8_t* data, int bytes);
//int packet_length (void);
void led_err(void);
//void print_hd_info(void);

FNBUS void mcuInit(void);

// these are smartportsd functions that have fujinet replacement algorithms
int freeMemory();
bool open_image( device &d, std::string filename );
bool open_tnfs_image( device &d);

FNBUS void spsd_setup(); // put as much as possible in bus setup function
FNBUS void spsd_loop(); // split this amongst bus and dev ... bus watches the phasestate, determines device # then kicks over to device

// these were development tests that can be thrown out
void timer_1us_example();
void timer_config();
void hw_timer_pulses();
void hw_timer_direct_reg();
void test_send();
void test_edge_capture();
};

#endif // guard
