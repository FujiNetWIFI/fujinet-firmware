#ifdef BUILD_RS232

/*
 * rs232cpm.cpp — RS232 CP/M transport: a thin console back-end for the shared
 * RunCPM core (lib/runcpm/runcpm_core.cpp). The RS232 bus has no
 * character-stream console, so the back-end is a no-op stub (see below).
 */

#include "rs232cpm.h"

#include <string.h>

#include "fnSystem.h"

#include "../runcpm/runcpm_session.h"

/* console back-end: the RS232 bus has no character-stream console (it never
 * had a working one), so these ops are no-ops; getch returns CP/M EOF (^Z) so
 * the session ends gracefully instead of busy-looping. */

static int rs232_kbhit(void)
{
    return 0;
}

static int rs232_getch(void)
{
    return 0x1a; /* ^Z / CP/M EOF */
}

static int rs232_getche(void)
{
    return rs232_getch();
}

static void rs232_putch(uint8_t ch)
{
    (void)ch;
}

static void rs232_clrscr(void)
{
}

static const runcpm_console_ops rs232_console_ops = {
    rs232_getch,
    rs232_getche,
    rs232_kbhit,
    rs232_putch,
    rs232_clrscr,
};

/* ============================ rs232CPM device =========================== */

void rs232CPM::rs232_status(FujiStatusReq reqType)
{
    // Nothing to do here
    return;
}

void rs232CPM::rs232_handle_cpm()
{
    // Run a full CP/M session on the shared core using the RS232 console.
    // Blocks until the session ends; the bus service loop then stops calling
    // us because cpmActive is cleared.
    runcpm_session_run(&rs232_console_ops);
    cpmActive = false;
}

void rs232CPM::init_cpm(int baud)
{
    // RAM/globals are owned by the shared core now; nothing transport-specific
    // to set up here (the RS232 link rate is managed elsewhere).
    (void)baud;
}

void rs232CPM::rs232_process(FujiBusPacket &packet)
{
    switch (packet.command())
    {
    case CPMCMD_INIT:
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

#endif /* BUILD_RS232 */
