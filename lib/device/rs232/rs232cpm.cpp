#ifdef BUILD_RS232

/*
 * rs232cpm.cpp — RS232 CP/M transport, now a thin console back-end.
 *
 * The RunCPM engine + state live once in lib/runcpm/runcpm_core.cpp.  This file
 * no longer includes the RunCPM header chain (which, since RunCPM 6.9, defines
 * the `Command` struct and the `CPM` macro that collide with the FujiNet bus
 * headers); it only supplies an RS232 console back-end (runcpm_console_ops) and
 * asks the shared core to run a session.  The RS232 bus has no character-stream
 * console, so the back-end is a no-op stub (see below).
 */

#include "rs232cpm.h"

#include <string.h>

#include "fnSystem.h"

#include "../runcpm/runcpm_session.h"

/*
 * ----- console back-end (runcpm_console_ops) -----
 *
 * The RS232 systemBus does not expose a character-stream console API
 * (read/write/available); it is a transaction-based bus.  Historically the
 * RS232 CP/M console went through abstraction_fujinet.h, whose console was only
 * wired to the bus when BYPASS_BUS was defined — and BYPASS_BUS is defined
 * *only* for BUILD_ATARI.  For RS232 the console was therefore a no-op stub
 * (and _getch actually busy-looped forever), so RS232 CP/M has never had a
 * working interactive console.
 *
 * To preserve that "no console transport" reality while still compiling against
 * the shared core, these ops are clean no-ops.  getch returns CP/M EOF (^Z)
 * instead of busy-looping, so the session terminates gracefully rather than
 * hanging the bus service loop as the old code did.
 */

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
