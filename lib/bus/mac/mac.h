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
    size_t write(uint8_t c) { return _serial.write(c); }
    size_t write(const void *buffer, size_t length) { return _serial.write(buffer, length); }
    void flush() { _serial.flushOutput(); }
};

extern systemBus SYSTEM_BUS;

#endif // MAC_H
#endif // BUILD_MAC
