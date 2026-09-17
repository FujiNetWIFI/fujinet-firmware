#ifdef BUILD_MAC
#ifndef MAC_H
#define MAC_H

/**
 * Macintosh 68k floppy-port bus.
 *
 * The ESP32 does not talk to the Mac directly. A Raspberry Pi Pico
 * (see pico/mac/commands.c) sits on the DB-19 floppy port and speaks
 * the drive protocol (800K GCR floppy via MCI, HD20 hard disk via DCD)
 * in PIO state machines. The Pico and the ESP32 are linked by a 2 Mbaud
 * UART using a tiny single-character command protocol:
 *
 *   Pico -> ESP32
 *     '0'..'7'  floppy control command (SEL/CA2/CA1/CA0 value, see mac.cpp)
 *     'A'..'E'  select active DCD (HD20) drive
 *     'R' n n n read DCD block n (24-bit big endian)
 *     'W' n n n <512 bytes> write DCD block n
 *     'T'       request DCD status block
 *
 *   ESP32 -> Pico
 *     'h' n     n DCD drives are mounted in a contiguous chain
 *     's' t     single-sided floppy mounted, head at track t|0x80
 *     'd' t     double-sided floppy mounted, head at track t|0x80
 *     'M'/'F'   motor on / off acknowledgement
 *     'S'       track buffers updated after a step
 *     'N'       step rejected (no disk)
 *     'E'       eject acknowledgement
 *     'w'/'e'   DCD write ok / error
 *
 * The floppy read data itself is streamed out on SP_WRDATA by the RMT
 * peripheral (see mac_ll.cpp), not over the UART.
 */

#include "bus.h"
#include "FujiBusPacket.h"
#include "UARTChannel.h"
#include "fujiDeviceID.h"
#include "global_types.h"

#include <cstdint>
#include <cstddef>

// The Mac has no CONFIG program; everything is driven from the web UI.
// fujiDevice still needs a packet type to instantiate its command table.
#define FUJI_COMMAND_PACKET FujiBusPacket

// Device slot layout (fujiDisk index):
//   0..3  HD20 / DCD hard disks ('0'..'3' on the Pico side)
//   4     800K GCR floppy
#define MAC_DCD_SLOTS   4
#define MAC_FLOPPY_SLOT 4

typedef char mac_cmd_t;

class systemBus;
class fujiDevice;

/**
 * @brief A device on the Mac bus
 */
class virtualDevice
{
    friend systemBus;
    friend fujiDevice;

protected:
    char _devnum = 0;

    virtual void shutdown() {}

    /**
     * @brief Handle a command character forwarded by the bus.
     */
    virtual void process(mac_cmd_t cmd) {}

public:
    /**
     * @brief Is this device active (for disks: is media mounted)?
     */
    bool device_active = false;

    /**
     * @brief Is this the virtual disk used to boot CONFIG? (unused on Mac)
     */
    bool is_config_device = false;

    /**
     * @brief Disk-switched flag (unused on Mac, kept for shared code)
     */
    bool switched = false;

    char id() { return _devnum; }
};

// Older Mac code used this name; keep it as an alias.
using macDevice = virtualDevice;

/**
 * @brief The Mac bus: UART link to the Pico plus RMT floppy output
 */
class systemBus : public SystemBusBase
{
private:
    UARTChannel _serial;

    static constexpr int _mac_baud_rate = 2000000;

    int _active_DCD_disk = 0;
    uint8_t _mounted_dcd_disks = 0;

    unsigned long t0 = 0;
    bool track_not_copied = false;

    char num_dcd_mounts();
    bool stepper_timeout();
    void send_dcd_count();

    void handle_floppy_command(int c);
    void handle_dcd_command(int c);

public:
    void setup();
    void service();
    void shutdown();

    bool shuttingDown = false; // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    void add_dcd_mount(char c);
    void rem_dcd_mount(char c);

    // SystemBusBase transaction contract. Nothing on the Mac issues
    // Fuji commands over the bus, so these are inert.
    void transaction_accept(transState_t expectMoreData) override {}
    void transaction_success() override {}
    void transaction_error() override {}
    using SystemBusBase::transaction_get;
    success_is_true transaction_get(void *data, size_t len) override { RETURN_ERROR_AS_FALSE(); }
    using SystemBusBase::transaction_send;
    void transaction_send(const void *data, size_t len, bool is_error = false) override {}

    // Pico UART link
    size_t available() { return _serial.available(); }
    int read() { return _serial.read(); }
    size_t read(void *buffer, size_t length) { return _serial.read(buffer, length); }
    // Block until exactly `length` bytes arrive or `timeout_ms` elapses.
    // Returns the number of bytes actually read.
    size_t read_exact(void *buffer, size_t length, unsigned timeout_ms = 1000);
    void handle_write_frame();
    size_t write(uint8_t c) { return _serial.write(c); }
    size_t write(const void *buffer, size_t length) { return _serial.write(buffer, length); }
    void flush() { _serial.flushOutput(); }
};

extern systemBus SYSTEM_BUS;

#endif // MAC_H
#endif // BUILD_MAC

#if 0
#ifndef IWM_H
#define IWM_H

#include "../../include/debug.h"

#include "bus.h"
#include "iwm_ll.h"

#include <cstdint>
#include <forward_list>
#include <string>

#include "fnFS.h"

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
#define SP_ERR_BADWIFI 0x30 // error connecting to new SSID - todo: implement usage

#define STATCODE_BLOCK_DEVICE 0x01 << 7 // block device = 1, char device = 0
#define STATCODE_WRITE_ALLOWED 0x01 << 6
#define STATCODE_READ_ALLOWED 0x01 << 5
#define STATCODE_DEVICE_ONLINE 0x01 << 4 // or disk in drive
#define STATCODE_FORMAT_ALLOWED 0x01 << 3
#define STATCODE_WRITE_PROTECT 0x01 << 2 // block devices only
#define STATCODE_INTERRUPTING 0x01 << 1  // apple IIc only
#define STATCODE_DEVICE_OPEN 0x01 << 0   // char devices only
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

#define IWM_STATUS_STATUS 0x00
#define IWM_STATUS_DCB 0x01
#define IWM_STATUS_NEWLINE 0x02
#define IWM_STATUS_DIB 0x03
#define IWM_STATUS_UNI35 0x05

// class def'ns
class iwmFuji;    // declare here so can reference it, but define in fuji.h
class iwmModem;   // declare here so can reference it, but define in modem.h
class iwmNetwork; // declare here so can reference it, but define in network.h
class iwmPrinter; // Printer device
class iwmDisk;    // disk device cause I need to use "iwmDisk smort" for prototyping in iwmBus::service()
class iwmCPM;     // CPM Virtual Device
class iwmClock;   // Real Time Clock Device
class iwmBus;     // forward declare bus so can be friend

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

#define COMMAND_PACKET_LEN 27 // 28     - max length changes suggested by robj
#define BLOCK_DATA_LEN 512
#define MAX_DATA_LEN 767
#define MAX_SP_PACKET_LEN 891
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
  00 CLEAR    zero after packet for FujiNet use
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
  uint8_t data[COMMAND_PACKET_LEN + 1];
};

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
  on,
  on2off
};

struct iwm_device_info_block_t
{
  uint8_t stat_code;       // byte with 8 flags indicating device status
  std::string device_name; // limited to 16 chars std ascii (<128), no zero terminator
  uint8_t device_type;
  uint8_t device_subtype;
  uint8_t firmware_rev;
};

// #ifdef DEBUG
void print_packet(uint8_t *data, int bytes);
void print_packet(uint8_t *data);
// #endif

class iwmDevice
{
  friend iwmBus; // put here for prototype, not sure if will need to keep it

protected:
  // set these things in constructor or initializer?
  iwm_smartport_type_t device_type;
  iwm_fujinet_type_t internal_type;
  iwm_device_info_block_t dib; // device information block
  uint8_t _devnum;             // assigned by Apple II during INIT
  bool _initialized;

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
  virtual void iwm_readblock(iwm_decoded_cmd_t cmd){};
  virtual void iwm_writeblock(iwm_decoded_cmd_t cmd){};
  // virtual void iwm_handle_eject(iwm_decoded_cmd_t cmd) {};
  virtual void iwm_format(iwm_decoded_cmd_t cmd){};
  virtual void iwm_ctrl(iwm_decoded_cmd_t cmd){};
  virtual void iwm_open(iwm_decoded_cmd_t cmd){};
  virtual void iwm_close(iwm_decoded_cmd_t cmd){};
  virtual void iwm_read(iwm_decoded_cmd_t cmd){};
  virtual void iwm_write(iwm_decoded_cmd_t cmd){};

  uint8_t get_status_code(iwm_decoded_cmd_t cmd) { return cmd.params[2]; }
  uint16_t get_numbytes(iwm_decoded_cmd_t cmd) { return cmd.params[2] + (cmd.params[3] << 8); };
  uint32_t get_address(iwm_decoded_cmd_t cmd) { return cmd.params[4] + (cmd.params[5] << 8) + (cmd.params[6] << 16); }

  void iwm_return_badcmd(iwm_decoded_cmd_t cmd);
  void iwm_return_ioerror();
  void iwm_return_noerror();

  // iwm packet handling
  static uint8_t data_buffer[MAX_DATA_LEN]; // un-encoded binary data (512 bytes for a block)
  static int data_len;                      // how many bytes in the data buffer

public:
  bool device_active;
  uint8_t prevtype = SP_TYPE_BYTE_HARDDISK; // preserve previous device type when offline
  bool switched = false;                    // indicate disk switched condition
  bool readonly = true;                     // write protected
  bool is_config_device;
  /**
   * @brief get the IWM device Number (1-255)
   * @return The device number registered for this device
   */
  void set_id(uint8_t dn) { _devnum = dn; };
  int id() { return _devnum; };
  // void assign_id(uint8_t n) { _devnum = n; };

  void assign_name(std::string name) { dib.device_name = name; }
};

class iwmBus
{
private:
  iwmDevice *_activeDev = nullptr;

  iwmFuji *_fujiDev = nullptr;
  iwmModem *_modemDev = nullptr;
  iwmNetwork *_netDev[4] = {nullptr};
  // sioMIDIMaze *_midiDev = nullptr;
  // sioCassette *_cassetteDev = nullptr;
  iwmCPM *_cpmDev = nullptr;
  iwmPrinter *_printerdev = nullptr;
  iwmClock *_clockDev = nullptr;

  bool iwm_phase_val(uint8_t p);

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

  iwm_enable_state_t iwm_drive_enabled();
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
  int new_track;

public:
  std::forward_list<iwmDevice *> _daisyChain;

  cmdPacket_t command_packet;
  bool iwm_decode_data_packet(uint8_t *a, int &n);
  int iwm_send_packet(uint8_t source, iwm_packet_type_t packet_type, uint8_t status, const uint8_t *data, uint16_t num);

  // these things stay for the most part
  void setup();
  void service();
  void shutdown();

  int numDevices();
  void addDevice(iwmDevice *pDevice, iwm_fujinet_type_t deviceType); // todo: probably get called by handle_init()
  void remDevice(iwmDevice *pDevice);
  iwmDevice *deviceById(int device_id);
  iwmDevice *firstDev() { return _daisyChain.front(); }
  uint8_t *devBuffer() { return (uint8_t *)iwmDevice::data_buffer; }
  void enableDevice(uint8_t device_id);
  void disableDevice(uint8_t device_id);
  void changeDeviceId(iwmDevice *p, int device_id);
  iwmPrinter *getPrinter() { return _printerdev; }
  bool shuttingDown = false; // TRUE if we are in shutdown process
  bool getShuttingDown() { return shuttingDown; };
  bool en35Host = false; // TRUE if we are connected to a host that supports the /EN35 signal
};

extern systemBus SYSTEM_BUS;

#endif // guard
#endif /* BUILD_APPLE */
