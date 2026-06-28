#ifdef BUILD_ATARI

/*
 * siocpm.cpp — SIO 'G' (R:) CP/M transport: a thin console back-end for the
 * shared RunCPM core (lib/runcpm/runcpm_core.cpp). Supplies the SIO console
 * ops (batched writer + optional TCP tee) and runs a session on the core.
 */

#include "siocpm.h"

#include <errno.h>
#include <string.h>

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fujiDevice.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fnTcpServer.h"
#include "fnTcpClient.h"

#include "../runcpm/runcpm_session.h"

// Why is CP/M writing directly to the SYSTEM_BUS?
#define FN_CPM_LINK SYSTEM_BUS

/* Optional TCP tee mirror of the SIO console (dormant: enabled only by
   bios_tcpTeeAccept(), which the current CCP never calls — preserved for
   parity with the historical abstraction). */
static fnTcpClient client;
static fnTcpServer *server = nullptr;
static bool teeMode = false;
static unsigned short portActive = 0;

/* Batch console output: per-byte SYSTEM_BUS.write() pays a fixed select/write/
 * select cost each call (~500 char/s). Accumulate and emit as a block (like the
 * N:/SIO frame path); flush on full and whenever CP/M polls input (kbhit). */
#define CPM_TX_BUFSZ 512
static uint8_t _cpm_txbuf[CPM_TX_BUFSZ];
static size_t _cpm_txlen = 0;

static inline void _cflush(void)
{
    if (_cpm_txlen)
    {
        SYSTEM_BUS.write(_cpm_txbuf, _cpm_txlen);
        _cpm_txlen = 0;
    }
}

static inline void _cput(uint8_t ch)
{
    if (_cpm_txlen >= CPM_TX_BUFSZ)
        _cflush();
    _cpm_txbuf[_cpm_txlen++] = ch;
}

/* ----- console back-end (runcpm_console_ops) ----- */

// kbhit flushes pending output before reporting input availability.
static int sio_kbhit(void)
{
    _cflush();
    return SYSTEM_BUS.available();
}

static int sio_getch(void)
{
    if (teeMode == true)
    {
        while (sio_kbhit() > 0)
        {
            if (client.available())
            {
                uint8_t ch;
                client.read(&ch, 1);
                return ch & 0x7F;
            }
        }
        return SYSTEM_BUS.read() & 0x7F;
    }
    else
    {
        while (sio_kbhit() <= 0)
        {
        }
        return SYSTEM_BUS.read() & 0x7f;
    }
}

static int sio_getche(void)
{
    uint8_t ch = sio_getch() & 0x7f;
    _cput(ch);
    if (teeMode == true)
        client.write(ch);
    return ch;
}

static void sio_putch(uint8_t ch)
{
    _cput(ch & 0x7f);
    if (teeMode == true)
        client.write(ch);
}

static void sio_clrscr(void)
{
}

static const runcpm_console_ops sio_console_ops = {
    sio_getch,
    sio_getche,
    sio_kbhit,
    sio_putch,
    sio_clrscr,
};

/* ----- optional TCP tee BIOS helpers (dormant; preserved for parity) ----- */

uint8_t bios_tcpListen(uint16_t port)
{
    Debug_printf("Do we get here?\r\n");

    if (client.connected())
        client.stop();

    if (server != nullptr && port != portActive)
    {
        server->stop();
        delete server;
    }

    server = new fnTcpServer(port, 1);

    int res = server->begin(port);
    if (res == 0)
    {
        Debug_printf("bios_tcpListen - failed to open port %u\nError (%d): %s\r\n", port, errno, strerror(errno));
        return true;
    }
    else
    {
        Debug_printf("bios_tcpListen - Now listening on port %u\r\n", port);
        return false;
    }
}

uint8_t bios_tcpAvailable(void)
{
    if (server == nullptr)
        return 0;

    return server->hasClient();
}

uint8_t bios_tcpTeeAccept(void)
{
    if (server == nullptr)
        return false;

    if (server->hasClient())
        client = server->accept();

    teeMode = true;

    return client.connected();
}

uint8_t bios_tcpDrop(void)
{
    if (server == nullptr)
        return false;

    client.stop();

    return true;
}

/* ============================ sioCPM device ============================== */

void sioCPM::sio_status()
{
    // Nothing to do here
    return;
}

void sioCPM::sio_handle_cpm()
{
    // Run a full CP/M session on the shared core using the SIO console.
    // Blocks until the session ends; the bus service loop then stops calling
    // us because cpmActive is cleared.
    runcpm_session_run(&sio_console_ops);
    cpmActive = false;
}

void sioCPM::init_cpm(int baud)
{
    // RAM/globals are owned by the shared core now; only the serial link rate
    // is an SIO-transport concern.
    SYSTEM_BUS.setBaudrate(baud);
}

void sioCPM::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case 'G':
        transaction_begin(TRANS_STATE::NO_GET);
        fnSystem.delay(10);
        transaction_complete();
        fnSystem.delay(5000);
        init_cpm(9600);
        cpmActive = true;
        break;
    default:
        transaction_error();
        break;
    }
}

#endif /* BUILD_ATARI */
