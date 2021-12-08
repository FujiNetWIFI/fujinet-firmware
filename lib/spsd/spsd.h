#ifndef _SPSD_H
#define _SPSD_H

#include <cstdint>
#include <string>

#include "fnSystem.h"
#include "fnFsSD.h"

class spDevice 
{
private:
  unsigned long int block_num;
  unsigned char LBH, LBL, LBN, LBT, LBX;

  int number_partitions_initialised = 1;
  int noid = 0;
  int count;
  int ui_command;
  bool sdstato;
  unsigned char source, status, phases, status_code;

  bool reset_state = false;

public:
  void print_hd_info(void);
  void encode_data_packet(unsigned char source); //encode smartport 512 byte data packet
  int decode_data_packet(void);                  //decode smartport 512 byte data packet
  void encode_write_status_packet(unsigned char source, unsigned char status);
  void encode_init_reply_packet(unsigned char source, unsigned char status);
  void encode_status_reply_packet(struct device d);
  int packet_length(void);
  int partition;
  bool is_valid_image(FILE imageFile);

  //unsigned char packet_buffer[768];   //smartport packet buffer
  unsigned char packet_buffer[605]; //smartport packet buffer
  //unsigned char sector_buffer[512];   //ata sector data buffer
  unsigned char packet_byte;
  //int count;
  int initPartition;

  // We need to remember several things about a device, not just its ID
  struct device
  {
    FILE *sdf;
    unsigned char device_id;    //to hold assigned device id's for the partitions
    unsigned long blocks;       //how many 512-byte blocks this image has
    unsigned int header_offset; //Some image files have headers, skip this many bytes to avoid them
    //bool online;                          //Whether this image is currently available
    //No longer used, user devices[...].sdf.isOpen() instead
    bool writeable;
};

#define NUM_PARTITIONS 4
device devices[NUM_PARTITIONS];

enum uiState{
  smartport,
  gotch,
  startup
};

uiState state=startup;

// Arduino UNO pin assignments for SD card
//The circuit:
//    SD card attached to SPI bus as follows:
// ** MOSI - pin 11 on Arduino Uno/Duemilanove/Diecimila
// ** MISO - pin 12 on Arduino Uno/Duemilanove/Diecimila
// ** CLK - pin 13 on Arduino Uno/Duemilanove/Diecimila
// ** CS - depends on your SD card shield or module.
//     Pin 10 used here

// Change the value of chipSelect if your hardware does
// not use the default value, SS.  Common values are:
// Arduino Ethernet shield: pin 4
// Sparkfun SD shield: pin 8
// Adafruit SD shields and modules: pin 10

// Arduino UNO pin assignments for control pins
const uint8_t chipSelect = 10;
// const uint8_t ejectPin = 17;
const uint8_t statusledPin = 18;

// Don't actually use this, deprecated for simplicity
// Set USE_SDIO to zero for SPI card access.
//
// Initialize at highest supported speed not over 50 MHz.
// Reduce max speed if errors occur.

/*
   Set DISABLE_CHIP_SELECT to disable a second SPI device.
   For example, with the Ethernet shield, set DISABLE_CHIP_SELECT
   to 10 to disable the Ethernet controller.
*/
const int8_t DISABLE_CHIP_SELECT = 0;  // -1





// todo SdFat sdcard;


// Name the SD object different from the above "sd"
// so that if we acciedntally use "sd" anywhere the
// compiler will catch it
//SdBaseFile sdf;
//todo: dynamic(?) array of files selected by user
//File partition1;

//File sdf[4];


// todo - make Receive and Send functions for ESP32
// extern "C" unsigned char ReceivePacket(unsigned char*); //Receive smartport packet assembler function
// extern "C" unsigned char SendPacket(unsigned char*);    //send smartport packet assembler function
unsigned char ReceivePacket(unsigned char *a);
unsigned char SendPacket(unsigned char *a);
//void encode_data_packet (unsigned char source);
void encode_extended_data_packet (unsigned char source);
//int decode_data_packet (void);
//void encode_write_status_packet(unsigned char source, unsigned char status);
//void encode_init_reply_packet (unsigned char source, unsigned char status);
void encode_status_reply_packet (device d);
void encode_extended_status_reply_packet (device d);
void encode_error_reply_packet (unsigned char source);
void encode_status_dib_reply_packet (device d);
void encode_extended_status_dib_reply_packet (device d);
int verify_cmdpkt_checksum(void);
void print_packet (unsigned char* data, int bytes);
//int packet_length (void);
void led_err(void);
//void print_hd_info(void);
int rotate_boot (void);
void mcuInit(void);
int freeMemory();
bool open_image( device &d, std::string filename );
bool is_ours(unsigned char source);
void spsd_setup();
void spsd_loop();
};

#endif // guard
