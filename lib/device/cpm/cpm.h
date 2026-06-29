#ifndef DEVICE_CPM_BASE_H
#define DEVICE_CPM_BASE_H

// Shared CP/M device base.
//
// Every bus that can run CP/M (SIO/Atari, IWM/Apple, DriveWire/CoCo, RS232,
// RC2014, and the N:CPM:// network adapter) drives the one shared RunCPM
// engine (lib/runcpm/runcpm_core.cpp) through runcpm_session_run().  The only
// thing that differs between buses is how a console byte gets to and from the
// far end of the link - a modem/stream endpoint.  cpmDevice captures that as
// four virtual endpoint primitives (ep_kbhit/ep_getch/ep_putch/ep_clrscr) and
// turns them into the C callbacks the engine wants.
//
// The default endpoint is a "no console" stub that asks the session to exit at
// the first read - that is the right behaviour for a bus whose CP/M console is
// not wired up yet (RC2014, RS232): the engine cold-boots, the CCP tries to
// read a command, gets ^C and a clean exit instead of hanging.
//
// cpmQueueDevice adds a pair of byte queues for buses whose bus thread and CP/M
// engine run concurrently (IWM, DriveWire): the bus side calls host_read /
// host_write / host_available, the engine side blocks on the queues.

#include <cstddef>
#include <cstdint>

#include "bus.h"
#include "../runcpm/runcpm_session.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#else
#include <queue>
#include <mutex>
#include <condition_variable>
#endif

class cpmDevice : public virtualDevice
{
public:
    bool cpmActive = false;

    // Set the link baud rate (and anything else) before a session starts.
    // Buses that have nothing to do here leave it as the no-op default.
    virtual void init_cpm(int baud) { (void)baud; }

    // Run one blocking CP/M session against this device's endpoint.  Returns
    // when the program exits CP/M (or an exit is requested).
    void handle_cpm();

protected:
    // Console endpoint - override per bus.  The default is the "no console"
    // stub used by buses without a working CP/M console.
    virtual int     ep_kbhit()        { return 1; }
    virtual uint8_t ep_getch()        { runcpm_session_request_exit(); return 0x03; }
    virtual void    ep_putch(uint8_t) { }
    virtual void    ep_clrscr()       { }

private:
    // The engine talks to plain C function pointers; these trampolines forward
    // to the device that owns the running session (only one runs at a time).
    static cpmDevice *s_active;
    static int     s_kbhit();
    static uint8_t s_getch();
    static void    s_putch(uint8_t c);
    static void    s_clrscr();
};

// Base for buses whose bus service and the CP/M engine run on separate tasks
// and shuttle bytes through queues (IWM/Apple, DriveWire/CoCo).
class cpmQueueDevice : public cpmDevice
{
public:
    cpmQueueDevice();
    ~cpmQueueDevice();

    // Bus side (host) view of the link.
    size_t host_available();
    size_t host_read(uint8_t *buf, size_t max);
    void   host_write(const uint8_t *buf, size_t len);

protected:
    // Engine side of the link.
    int     ep_kbhit() override;
    uint8_t ep_getch() override;
    void    ep_putch(uint8_t c) override;
    void    ep_clrscr() override;

private:
#ifdef ESP_PLATFORM
    QueueHandle_t rxq = nullptr; // CP/M -> host
    QueueHandle_t txq = nullptr; // host -> CP/M
#else
    std::queue<uint8_t>     rxq, txq;
    std::mutex              rxmtx, txmtx;
    std::condition_variable txcv;
#endif
};

#endif // DEVICE_CPM_BASE_H
