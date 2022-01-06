#ifndef _SPSD_H
#define _SPSD_H

#include <cstdint>
#include <string>

#include "fnSystem.h"
#include "fnFsSD.h"



class spDevice 
{
private:

  bool phi[4];
  enum class phasestate_t {
    idle = 0,
    reset,
    enable
  };
#ifdef DEBUG  
  phasestate_t oldphase = phasestate_t::reset; // can use for debug printing of phase changes
#endif 
  phasestate_t phases;

  unsigned long int block_num;
  uint8_t LBH, LBL, LBN, LBT, LBX;

  int number_partitions_initialised = 1;
  int noid = 0;
  int count;
  int ui_command;
  size_t sdstato;
  uint8_t source, status, status_code;

  bool reset_state = false;

public:
  void print_hd_info(void);
  void encode_data_packet(uint8_t source); //encode smartport 512 byte data packet
  int decode_data_packet(void);                  //decode smartport 512 byte data packet
  void encode_write_status_packet(uint8_t source, uint8_t status);
  void encode_init_reply_packet(uint8_t source, uint8_t status);
  void encode_status_reply_packet(struct device d);
  int packet_length(void);
  int partition;
  bool is_valid_image(FILE imageFile);

  void handle_readblock();
  void handle_init();

  //unsigned char packet_buffer[768];   //smartport packet buffer
  uint8_t packet_buffer[605]; //smartport packet buffer
  //unsigned char sector_buffer[512];   //ata sector data buffer
  uint8_t packet_byte;
  //int count;
  int initPartition;

  // We need to remember several things about a device, not just its ID
  struct device
  {
    FILE *sdf;
    uint8_t device_id;    //to hold assigned device id's for the partitions
    unsigned long blocks;       //how many 512-byte blocks this image has
    unsigned int header_offset; //Some image files have headers, skip this many bytes to avoid them
    //bool online;                          //Whether this image is currently available
    //No longer used, user devices[...].sdf.isOpen() instead
    bool writeable;
};

#define NUM_PARTITIONS 1
device devices[NUM_PARTITIONS];

enum uiState{
  smartport,
  gotch,
  startup
};

uiState state=startup;

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
int SendPacket(uint8_t *a);
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
int rotate_boot (void);
void mcuInit(void);
int freeMemory();
bool open_image( device &d, std::string filename );
bool open_tnfs_image( device &d);
bool is_ours(uint8_t source);
void spsd_setup();
void spsd_loop();
void timer_1us_example();
void timer_config();
void hw_timer_pulses();
void hw_timer_direct_reg();
void test_send();
void test_edge_capture();
};

#endif // guard
