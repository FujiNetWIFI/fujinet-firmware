#ifdef BUILD_APPLE
#ifndef IWM_H
#define IWM_H

#include "../../include/debug.h"

#include "bus.h"

#include <cstdint>
#include <forward_list>
#include <string>
#include "fnFS.h"

// todo - see page 81-82 in Apple IIc ROM reference and Table 7-5 in IIgs firmware ref
#define SP_ERR_NOERROR 0x00    // no error
#define SP_ERR_BADCMD 0x01     // invalid command
#define SP_ERR_BUSERR 0x06     // communications error
#define SP_ERR_BADCTL 0x21     // invalid status or control code
#define SP_ERR_BADCTLPARM 0x22 // invalid parameter list
#define SP_ERR_IOERROR 0x27    // i/o error on device side
#define SP_ERR_NODRIVE 0x28    // no device connected
#define SP_ERR_NOWRITE 0x2b    // disk write protected
#define SP_ERR_BADBLOCK 0x2d   // invalid block number
#define SP_ERR_DISKSW 0x2e     // media has been swapped - extended calls only
#define SP_ERR_OFFLINE 0x2f    // device offline or no disk in drive
// $30-$3F are for device specific errors
#define SP_ERR_BADWIFI 0x30    // error connecting to new SSID - todo: implement usage


#define STATCODE_BLOCK_DEVICE 0x01 << 7   // block device = 1, char device = 0
#define STATCODE_WRITE_ALLOWED 0x01 << 6
#define STATCODE_READ_ALLOWED 0x01 << 5
#define STATCODE_DEVICE_ONLINE 0x01 << 4  // or disk in drive
#define STATCODE_FORMAT_ALLOWED 0x01 << 3
#define STATCODE_WRITE_PROTECT 0x01 << 2  // block devices only
#define STATCODE_INTERRUPTING 0x01 << 1   // apple IIc only
#define STATCODE_DEVICE_OPEN 0x01 << 0    // char devices only

// valid types and subtypes for block devices per smartport documentation
#define SP_TYPE_BYTE_35DISK 0x01
#define SP_SUBTYPE_BYTE_UNI35 0x00
#define SP_SUBTYPE_BYTE_APPLE35 0xC0

#define SP_TYPE_BYTE_HARDDISK 0x02
#define SP_SUBTYPE_BYTE_REMOVABLE 0x00
#define SP_SUBTYPE_BYTE_HARDDISK 0x20 // fixed media
#define SP_SUBTYPE_BYTE_SWITCHED 0x40 // removable and supports disk switched errors
#define SP_SUBTYPE_BYTE_HARDDISK_EXTENDED 0xA0 
#define SP_SUBTYPE_BYTE_REMOVABLE_EXTENDED 0xC0 // removable and extended and supports disk switched errors

#define SP_TYPE_BYTE_SCSI 0x03
#define SP_SUBTYPE_BYTE_SCSI_REMOVABLE 0xC0 // removable and extended and supports disk switched errors

#define SP_TYPE_BYTE_FUJINET 0x10
#define SP_SUBTYPE_BYTE_FUJINET 0x00

#define PACKET_TYPE_CMD 0x80
#define PACKET_TYPE_STATUS 0x81
#define PACKET_TYPE_DATA 0x82

#define IWM_CTRL_RESET 0x00
#define IWM_CTRL_SET_DCB 0x01
#define IWM_CTRL_SET_NEWLINE 0x02
#define IWM_CTRL_SERVICE_INT 0x03
#define IWM_CTRL_EJECT_DISK 0x04
#define IWM_CTRL_RUN_ROUTINE 0x05
#define IWM_CTRL_DWNLD_ADDRESS 0x06
#define IWM_CTRL_DOWNLOAD 0x07

#define IWM_STATUS_STATUS 0x00
#define IWM_STATUS_DCB 0x01
#define IWM_STATUS_NEWLINE 0x02
#define IWM_STATUS_DIB 0x03
#define IWM_STATUS_UNI35 0x05

#undef TESTTX
//#define TESTTX

// these are for the temporary disk functions
//#include "fnFsSD.h"
//#include <string>

// class def'ns
class iwmFuji;     // declare here so can reference it, but define in fuji.h
class iwmModem;    // declare here so can reference it, but define in modem.h
class iwmNetwork;  // declare here so can reference it, but define in network.h
class iwmPrinter;  // Printer device
class iwmDisk;     // disk device cause I need to use "iwmDisk smort" for prototyping in iwmBus::service()

class iwmBus;      // forward declare bus so can be friend

// Sorry, this  is the protocol adapter's fault. -Thom
union cmdFrame_t
{
    struct
    {
        uint8_t device;
        uint8_t comnd;
        uint8_t aux1;
        uint8_t aux2;
        uint8_t cksum;
    };
    struct
    {
        uint32_t commanddata;
        uint8_t checksum;
    } __attribute__((packed));
};

#define COMMAND_PACKET_LEN  28
#define BLOCK_PACKET_LEN    606
#define MAX_DATA_LEN        767
#define MAX_PACKET_LEN         891
// to do - make block packet compatible up to 767 data bytes?

union cmdPacket_t
{
/*
C3 PBEGIN   MARKS BEGINNING OF PACKET 32 micro Sec.
81 DEST     DESTINATION UNIT NUMBER 32 micro Sec.
80 SRC      SOURCE UNIT NUMBER 32 micro Sec.
80 TYPE     PACKET TYPE FIELD 32 micro Sec.
80 AUX      PACKET AUXILLIARY TYPE FIELD 32 micro Sec.
80 STAT     DATA STATUS FIELD 32 micro Sec.
82 ODDCNT   ODD BYTES COUNT 32 micro Sec.
81 GRP7CNT  GROUP OF 7 BYTES COUNT 32 micro Sec.
80 ODDMSB   ODD BYTES MSB's 32 micro Sec.
81 COMMAND  1ST ODD BYTE = Command Byte 32 micro Sec.
83 PARMCNT  2ND ODD BYTE = Parameter Count 32 micro Sec.
80 GRP7MSB  MSB's FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE1  BYTE 1 FOR 1ST GROUP OF 7 32 micro Sec.
98 G7BYTE2  BYTE 2 FOR 1ST GROUP OF 7 32 micro Sec.
82 G7BYTE3  BYTE 3 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE4  BYTE 4 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE5  BYTE 5 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE5  BYTE 6 FOR 1ST GROUP OF 7 32 micro Sec.
80 G7BYTE6  BYTE 7 FOR 1ST GROUP OF 7 32 micro Sec.
BB CHKSUM1  1ST BYTE OF CHECKSUM 32 micro Sec.
EE CHKSUM2  2ND BYTE OF CHECKSUM 32 micro Sec.
C8 PEND     PACKET END BYTE 32 micro Sec.
*/
struct
{
  uint8_t sync1;   // 0
  uint8_t sync2;   // 1
  uint8_t sync3;   // 2
  uint8_t sync4;   // 3
  uint8_t sync5;   // 4
  uint8_t pbegin;  // 5
  uint8_t dest;    // 6
  uint8_t source;  // 7
  uint8_t type;    // 8
  uint8_t aux;     // 9
  uint8_t stat;    // 10
  uint8_t oddcnt;  // 11
  uint8_t grp7cnt; // 12
  uint8_t oddmsb;  // 13
  uint8_t command; // 14
  uint8_t parmcnt; // 15
  uint8_t grp7msb; // 16
  uint8_t g7byte1; // 17
  uint8_t g7byte2; // 18
  uint8_t g7byte3; // 19
  uint8_t g7byte4; // 20
  uint8_t g7byte5; // 21
  uint8_t g7byte6; // 22
  uint8_t g7byte7; // 23
  uint8_t chksum1; // 24
  uint8_t chksum2; // 25
  uint8_t pend;    // 26
  uint8_t clear;   // 27
  };
  uint8_t data[COMMAND_PACKET_LEN];
};

enum class iwm_smartport_type_t
{
  Block_Device,
  Character_Device
};

enum class iwm_fujinet_type_t
{
  BlockDisk,
  FujiNet,
  Modem,
  Network,
  CPM,
  Printer,
  Voice,
  Other
};


struct iwm_device_info_block_t
{
  uint8_t stat_code; // byte with 8 flags indicating device status
  std::string device_name; // limited to 16 chars std ascii (<128), no zero terminator
  uint8_t device_type;
  uint8_t device_subtype;
  uint8_t firmware_rev;
};

//#ifdef DEBUG
  void print_packet(uint8_t *data, int bytes);
  void print_packet(uint8_t* data);
//#endif

class iwmDevice
{
friend iwmBus; // put here for prototype, not sure if will need to keep it

protected:
  // set these things in constructor or initializer?
  iwm_smartport_type_t device_type;
  iwm_fujinet_type_t internal_type;
  iwm_device_info_block_t dib;   // device information block
  uint8_t _devnum; // assigned by Apple II during INIT
  bool _initialized;

  // iwm packet handling
  uint8_t packet_buffer[BLOCK_PACKET_LEN]; //smartport packet buffer
  uint16_t packet_len;

  bool decode_data_packet(void); //decode smartport 512 byte data packet
  uint16_t num_decoded;

  void encode_data_packet(); //encode smartport 512 byte data packet
  void encode_data_packet(uint16_t num); //encode smartport "num" byte data packet
  void encode_write_status_packet(uint8_t source, uint8_t status);
  void encode_init_reply_packet(uint8_t source, uint8_t status);
  virtual void encode_status_reply_packet() = 0;
  void encode_error_reply_packet(uint8_t stat);
  virtual void encode_status_dib_reply_packet() = 0;

  void encode_extended_data_packet(uint8_t source);
  virtual void encode_extended_status_reply_packet() = 0;
  virtual void encode_extended_status_dib_reply_packet() = 0;
  
  int get_packet_length(void);

  virtual void shutdown() = 0;
  virtual void process(cmdPacket_t cmd) = 0;

  virtual void iwm_status(cmdPacket_t cmd);
  virtual void iwm_readblock(cmdPacket_t cmd) {};
  virtual void iwm_writeblock(cmdPacket_t cmd) {};
  virtual void iwm_format(cmdPacket_t cmd) {};
  virtual void iwm_ctrl(cmdPacket_t cmd) {};
  virtual void iwm_open(cmdPacket_t cmd) {};
  virtual void iwm_close(cmdPacket_t cmd) {};
  virtual void iwm_read(cmdPacket_t cmd) {};
  virtual void iwm_write(cmdPacket_t cmd) {};

  void iwm_return_badcmd(cmdPacket_t cmd);
  void iwm_return_ioerror(cmdPacket_t cmd);

public:
  bool device_active;
  bool is_config_device;
  /**
   * @brief get the IWM device Number (1-255)
   * @return The device number registered for this device
   */
  int id() { return _devnum; };
  //void assign_id(uint8_t n) { _devnum = n; };

  void assign_name(std::string name) {dib.device_name = name;}

  /**
   * @brief Get the iwmBus object that this iwmDevice is attached to.
   */
  iwmBus iwm_get_bus();

  /**
   * Startup hack for now
   */
  virtual void startup_hack() = 0;
};

class iwmBus
{
private:
  std::forward_list<iwmDevice *> _daisyChain;

  iwmDevice *_activeDev = nullptr;

  iwmFuji *_fujiDev = nullptr;
  iwmModem *_modemDev = nullptr;
  iwmNetwork *_netDev[8] = {nullptr};
  //sioMIDIMaze *_midiDev = nullptr;
  //sioCassette *_cassetteDev = nullptr;
  //iwmCPM *_cpmDev = nullptr;
  iwmPrinter *_printerdev = nullptr;

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
  void iwm_extra_set();
  void iwm_extra_clr();

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

  cmdPacket_t command_packet;
  bool verify_cmdpkt_checksum(void);

public:
  int iwm_read_packet(uint8_t *a, int n);
  int iwm_read_packet_timeout(int tout, uint8_t *a, int n);
  int iwm_send_packet(uint8_t *a);

  void setup();
  void service();
  void shutdown();

  void handle_init(); // todo: put this function in the right place

  int numDevices();
  void addDevice(iwmDevice *pDevice, iwm_fujinet_type_t deviceType); // todo: probably get called by handle_init()
  void remDevice(iwmDevice *pDevice);
  iwmDevice *deviceById(int device_id);
  iwmDevice *firstDev() {return _daisyChain.front();}
  void enableDevice(uint8_t device_id);
  void disableDevice(uint8_t device_id);
  void changeDeviceId(iwmDevice *p, int device_id);
  // iwmDevice *smort;

#ifdef TESTTX
  void test_send(iwmDevice* smort);
#endif

  void startup_hack();

};

extern iwmBus IWM;

#endif // guard
#endif /* BUILD_APPLE */