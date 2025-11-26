#ifdef BUILD_APPLE
#ifndef IWM_H
#define IWM_H

#include "../../include/debug.h"

// for ESP IWM-SLIP build, DEV_RELAY_SLIP should be defined in platformio.ini
// for PC IWM-SLIP build DEV_RELAY_SLIP should be defined in fujinet_pc.cmake

#ifdef DEV_RELAY_SLIP
#include "iwm_slip.h"
#else
#include "iwm_ll.h"
#endif

#include <array>
#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

#include "fnFS.h"
#include "fujiCommandID.h"

enum {
  SP_CMD_STATUS         = 0x00,
  SP_CMD_READBLOCK      = 0x01,
  SP_CMD_WRITEBLOCK     = 0x02,
  SP_CMD_FORMAT         = 0x03,
  SP_CMD_CONTROL        = 0x04,
  SP_CMD_INIT           = 0x05,
  SP_CMD_OPEN           = 0x06,
  SP_CMD_CLOSE          = 0x07,
  SP_CMD_READ           = 0x08,
  SP_CMD_WRITE          = 0x09,
  SP_ECMD_STATUS        = 0x40,
  SP_ECMD_READBLOCK     = 0x41,
  SP_ECMD_WRITEBLOCK    = 0x42,
  SP_ECMD_FORMAT        = 0x43,
  SP_ECMD_CONTROL       = 0x44,
  SP_ECMD_INIT          = 0x45,
  SP_ECMD_OPEN          = 0x46,
  SP_ECMD_CLOSE         = 0x47,
  SP_ECMD_READ          = 0x48,
  SP_ECMD_WRITE         = 0x49,
};

// see page 81-82 in Apple IIc ROM reference and Table 7-5 in IIgs firmware ref
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
#define STATCODE_DISK_SWITCHED 0x01 << 0 // disk switched status for block devices, same bit as device open for char devices

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
#define SP_TYPE_BYTE_FUJINET_NETWORK 0x11
#define SP_TYPE_BYTE_FUJINET_CPM 0x12
#define SP_TYPE_BYTE_FUJINET_CLOCK 0x13
#define SP_TYPE_BYTE_FUJINET_PRINTER 0x14
#define SP_TYPE_BYTE_FUJINET_MODEM 0x15

#define SP_SUBTYPE_BYTE_FUJINET 0x00
#define SP_SUBTYPE_BYTE_FUJINET_NETWORK 0x00
#define SP_SUBTYPE_BYTE_FUJINET_CPM 0x00
#define SP_SUBTYPE_BYTE_FUJINET_CLOCK 0x00
#define SP_SUBTYPE_BYTE_FUJINET_PRINTER 0x00
#define SP_SUBTYPE_BYTE_FUJINET_MODEM 0x00

#define IWM_CTRL_RESET 0x00
#define IWM_CTRL_SET_DCB 0x01
#define IWM_CTRL_SET_NEWLINE 0x02
#define IWM_CTRL_SERVICE_INT 0x03
#define IWM_CTRL_EJECT_DISK 0x04
#define IWM_CTRL_RUN_ROUTINE 0x05
#define IWM_CTRL_DWNLD_ADDRESS 0x06
#define IWM_CTRL_DOWNLOAD 0x07
#define IWM_CTRL_CLEAR_ENSEEN 0x08

#define IWM_STATUS_STATUS 0x00
#define IWM_STATUS_DCB 0x01
#define IWM_STATUS_NEWLINE 0x02
#define IWM_STATUS_DIB 0x03
#define IWM_STATUS_UNI35 0x05
#define IWM_STATUS_ENSEEN 0x08

// class def'ns
class iwmFuji;     // declare here so can reference it, but define in fuji.h
class iwmModem;    // declare here so can reference it, but define in modem.h
class iwmNetwork;  // declare here so can reference it, but define in network.h
class iwmPrinter;  // Printer device
class iwmDisk;     // disk device cause I need to use "iwmDisk smort" for prototyping in systemBus::service()
class iwmCPM;      // CPM Virtual Device
class iwmClock;    // Real Time Clock Device
class systemBus;      // forward declare bus so can be friend

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

#define BLOCK_DATA_LEN      512
#define MAX_DATA_LEN        767

union iwm_decoded_cmd_t
{
  struct
  {
    uint8_t command;
    uint8_t count;
    uint8_t params[7];
  };
  uint8_t decoded[9];
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
  Clock,
  Other
};

enum class iwm_enable_state_t
{
  off,
  off2on,
  on2off,
  on,
};

struct iwm_device_info_block_t
{
  uint8_t stat_code; // byte with 8 flags indicating device status
  std::string device_name; // limited to 16 chars std ascii (<128), no zero terminator
  uint8_t device_type;
  uint8_t device_subtype;
  uint8_t firmware_rev;
};

typedef uint8_t SPUnitNum;

//#ifdef DEBUG
  void print_packet(uint8_t *data, int bytes);
  void print_packet(uint8_t* data);
//#endif

class virtualDevice
{
friend systemBus; // put here for prototype, not sure if will need to keep it

protected:
  // set these things in constructor or initializer?
  iwm_smartport_type_t device_type;
  iwm_fujinet_type_t internal_type;
  iwm_device_info_block_t dib;   // device information block
  SPUnitNum _devnum; // assigned by Apple II during INIT
  bool _initialized;

  uint8_t status_wait_count = 5;

   // void send_data_packet(); //encode smartport 512 byte data packet
  // void encode_data_packet(uint16_t num = 512); //encode smartport "num" byte data packet
  void send_init_reply_packet(uint8_t source, uint8_t status);
  virtual void send_status_reply_packet() = 0;
  void send_reply_packet(uint8_t status);
  // void send_reply_packet(uint8_t source, uint8_t status) { send_reply_packet(status); };
  virtual void send_status_dib_reply_packet() = 0;

  virtual void send_extended_status_reply_packet() = 0;
  virtual void send_extended_status_dib_reply_packet() = 0;

  virtual void shutdown() = 0;
  virtual void process(iwm_decoded_cmd_t cmd) = 0;

  // these are good for the high level device
  virtual void iwm_status(iwm_decoded_cmd_t cmd);
  virtual void iwm_readblock(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_writeblock(iwm_decoded_cmd_t cmd) {};
  // virtual void iwm_handle_eject(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_format(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_ctrl(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_open(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_close(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_read(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_write(iwm_decoded_cmd_t cmd) {};

  fujiCommandID_t get_status_code(iwm_decoded_cmd_t cmd) {return (fujiCommandID_t) cmd.params[2];}
  uint16_t get_numbytes(iwm_decoded_cmd_t cmd) { return cmd.params[2] + (cmd.params[3] << 8); };
  uint32_t get_address(iwm_decoded_cmd_t cmd) { return cmd.params[4] + (cmd.params[5] << 8) + (cmd.params[6] << 16); }

  void iwm_return_badcmd(iwm_decoded_cmd_t cmd);
  void iwm_return_device_offline(iwm_decoded_cmd_t cmd);
  void iwm_return_ioerror();
  void iwm_return_noerror();

  // iwm packet handling
  static uint8_t data_buffer[MAX_DATA_LEN]; // un-encoded binary data (512 bytes for a block)
  static int data_len; // how many bytes in the data buffer

  std::vector<uint8_t> create_dib_reply_packet(const std::string& device_name, uint8_t status, const std::vector<uint8_t>& block_size, const std::array<uint8_t, 2>& type, const std::array<uint8_t, 2>& version);

public:
  bool device_active;
  uint8_t prevtype = SP_TYPE_BYTE_HARDDISK; //preserve previous device type when offline
  bool switched = false; //indicate disk switched condition
  bool readonly = true;  //write protected
  bool is_config_device;
  /**
   * @brief get the IWM device Number (1-255)
   * @return The device number registered for this device
   */
  void set_id(SPUnitNum dn) { _devnum=dn; };
  SPUnitNum id() { return _devnum; };
  //void assign_id(SPUnitNUm n) { _devnum = n; };

  void assign_name(std::string name) {dib.device_name = name;}
};

class systemBus
{
private:


  virtualDevice *_activeDev = nullptr;

  iwmFuji *_fujiDev = nullptr;
  iwmModem *_modemDev = nullptr;
  // iwmNetwork *_netDev[4] = {nullptr};
  //sioMIDIMaze *_midiDev = nullptr;
  //sioCassette *_cassetteDev = nullptr;
  iwmCPM *_cpmDev = nullptr;
  iwmPrinter *_printerdev = nullptr;
  iwmClock *_clockDev = nullptr;

  #ifndef DEV_RELAY_SLIP
  bool iwm_phase_val(uint8_t p);
  #endif

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
  uint8_t current_disk2 = 0;

  iwm_enable_state_t iwm_motor_state();
  iwm_enable_state_t _old_enable_state;
  iwm_enable_state_t _new_enable_state;
  // uint8_t enable_values;

  void iwm_ack_deassert();
  void iwm_ack_assert();
  bool iwm_req_deassert_timeout(int t) { return smartport.req_wait_for_falling_timeout(t); };
  bool iwm_req_assert_timeout(int t) { return smartport.req_wait_for_rising_timeout(t); };

  iwm_decoded_cmd_t command;

  void handle_init();

  int old_track = -1;
  int new_track = -1;

public:
  std::forward_list<virtualDevice *> _daisyChain;

  cmdPacket_t command_packet;
  bool iwm_decode_data_packet(uint8_t *a, int &n);
   int iwm_send_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t* data, uint16_t num);

  // these things stay for the most part
  void setup();
  void service();
  bool serviceSmartPort();
  bool serviceDiskII();
#ifndef DEV_RELAY_SLIP
  bool serviceDiskIIWrite();
#endif
  void shutdown();

  int numDevices();
  void addDevice(virtualDevice *pDevice, iwm_fujinet_type_t deviceType); // todo: probably get called by handle_init()
  void remDevice(virtualDevice *pDevice);
  virtualDevice *deviceById(SPUnitNum device_id);
  virtualDevice *firstDev() {return _daisyChain.front();}
  uint8_t* devBuffer() {return (uint8_t *)virtualDevice::data_buffer;}
  void enableDevice(SPUnitNum device_id);
  void disableDevice(SPUnitNum device_id);
  void changeDeviceId(virtualDevice *p, int device_id);
  iwmPrinter *getPrinter() { return _printerdev; }
  bool shuttingDown = false;                                  // TRUE if we are in shutdown process
  bool getShuttingDown() { return shuttingDown; };
  bool en35Host = false; // TRUE if we are connected to a host that supports the /EN35 signal

  // For compatibility with other platforms, used by fujiDevice.cpp
  void setUDPHost(const char *newhost, int port);
  void setUltraHigh(bool _enable, int _ultraHighBaud = 0);
};

extern systemBus SYSTEM_BUS;

#define IWM_ACTIVE_DISK2 ((iwmDisk2 *) theFuji->get_disk_dev(MAX_SPDISK_DEVICES + diskii_xface.iwm_active_drive() - 1))
#endif // guard
#endif /* BUILD_APPLE */
