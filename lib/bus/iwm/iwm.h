#ifdef BUILD_APPLE
#ifndef IWM_H
#define IWM_H

#include "bus.h"
#include "FujiIWMPacket.h"
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

using iwm_decoded_cmd_t = FujiIWMPacket;
#define FUJI_COMMAND_PACKET iwm_decoded_cmd_t

// Windows defines this and it conflicts with the SmartPort erro
// code. We don't need it, just undef it.
#ifdef NOERROR
#undef NOERROR
#endif

typedef enum class SP_ERR {
    // see page 81-82 in Apple IIc ROM reference and Table 7-5 in IIgs firmware ref
    NOERROR    = 0x00, // no error
    BADCMD     = 0x01, // invalid command
    BUSERR     = 0x06, // communications error
    BADCTL     = 0x21, // invalid status or control code
    BADCTLPARM = 0x22, // invalid parameter list
    IOERROR    = 0x27, // i/o error on device side
    NODRIVE    = 0x28, // no device connected
    NOWRITE    = 0x2b, // disk write protected
    BADBLOCK   = 0x2d, // invalid block number
    DISKSW     = 0x2e, // media has been swapped - extended calls only
    OFFLINE    = 0x2f, // device offline or no disk in drive

    // $30-$3F are for device specific errors
    BADWIFI    = 0x30, // error connecting to new SSID - todo: implement usage

    ENDOFCHAIN = 0xff,
} spError_t;

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

// class def'ns
class iwmFuji;     // declare here so can reference it, but define in fuji.h
class iwmModem;    // declare here so can reference it, but define in modem.h
class iwmNetwork;  // declare here so can reference it, but define in network.h
class iwmPrinter;  // Printer device
class iwmDisk;     // disk device cause I need to use "iwmDisk smort" for prototyping in systemBus::service()
class iwmCPM;      // CPM Virtual Device
class iwmClock;    // Real Time Clock Device
class systemBus;      // forward declare bus so can be friend
class fujiDevice;

#define BLOCK_DATA_LEN      512
#define MAX_DATA_LEN        767

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

struct iwm_device_status_block_t
{
  uint8_t code; // byte with 8 flags indicating device status
  u24le_t block_size;
} __attribute__((packed));

struct iwm_device_info_block_t
{
  iwm_device_status_block_t dev_status;
  uint8_t name_len;
  char name[16];
  uint8_t type, subtype;
  u16le_t version;
} __attribute__((packed));

//#ifdef DEBUG
void print_packet(void *data, int bytes);
void print_packet(void *data);
//#endif

class virtualDevice
{
  friend systemBus; // put here for prototype, not sure if will need to keep it
  friend fujiDevice;

protected:
  // set these things in constructor or initializer?
  iwm_smartport_type_t device_type;
  iwm_fujinet_type_t internal_type;
  uint8_t _devnum; // assigned by Apple II during INIT
  bool _initialized;

  virtual void shutdown() = 0;

  // FIXME - these are all bus commands and belong in systemBus
  virtual void iwm_status(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_readblock(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_writeblock(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_format(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_ctrl(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_open(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_close(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_read(const iwm_decoded_cmd_t &cmd);
  virtual void iwm_write(const iwm_decoded_cmd_t &cmd);

  void iwm_return_badcmd(const iwm_decoded_cmd_t &cmd);

  virtual iwm_device_info_block_t create_dib_reply_packet() = 0;
  virtual iwm_device_status_block_t create_status_reply_packet() = 0;

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
  void set_id(uint8_t dn) { _devnum=dn; };
  int id() { return _devnum; };
};

class systemBus : public SystemBusBase
{
private:
  virtualDevice *_activeDev = nullptr;
  ByteBuffer _transaction_response;

  iwmPrinter *_printerdev = nullptr;

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
  void iwm_process(const iwm_decoded_cmd_t &cmd);
  error_is_true iwm_send_packet(uint8_t source, iwm_packet_type_t packet_type, spError_t err, const void* data, uint16_t num);

  void send_init_reply_packet(uint8_t source, spError_t err);
  void send_status_reply_packet();
  void send_status_dib_reply_packet();

  void handle_init();

  int old_track = -1;
  int new_track = -1;

public:
  std::forward_list<virtualDevice *> _daisyChain;

  cmdPacket_t command_packet;
  bool iwm_decode_data_packet(uint8_t *a, int &n);

  // these things stay for the most part
  void setup();
  void service();
  bool serviceSmartPort();
  bool serviceDiskII();
#ifndef DEV_RELAY_SLIP
  bool serviceDiskIIWrite();
#endif
  void shutdown();

  void transaction_accept(transState_t expectMoreData) override;
  void transaction_success() override;
  void transaction_error(spError_t err);
  void transaction_error() override { transaction_error(SP_ERR::IOERROR); }
  success_is_true transaction_get(void *data, size_t len) override;
  using SystemBusBase::transaction_send;
  void transaction_send(const void *data, size_t len, bool is_error=false) override;

  int numDevices();
  void addDevice(virtualDevice *pDevice, iwm_fujinet_type_t deviceType); // todo: probably get called by handle_init()
  void remDevice(virtualDevice *pDevice);
  virtualDevice *deviceById(int device_id);
  virtualDevice *firstDev() {return _daisyChain.front();}
  void enableDevice(uint8_t device_id);
  void disableDevice(uint8_t device_id);
  void changeDeviceId(virtualDevice *p, int device_id);
  iwmPrinter *getPrinter() { return _printerdev; }
  bool shuttingDown = false;                                  // TRUE if we are in shutdown process
  bool getShuttingDown() { return shuttingDown; };
  bool en35Host = false; // TRUE if we are connected to a host that supports the /EN35 signal

};

extern systemBus SYSTEM_BUS;

#define IWM_ACTIVE_DISK2 ((iwmDisk2 *) theFuji->get_disk_dev(MAX_SPDISK_DEVICES + diskii_xface.iwm_active_drive() - 1))
#endif // guard
#endif /* BUILD_APPLE */
