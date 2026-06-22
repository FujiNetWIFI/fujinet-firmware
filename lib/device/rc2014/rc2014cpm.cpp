#ifdef BUILD_RC2014

/*
 * rc2014cpm.cpp — RC2014 CP/M transport, now a thin console back-end.
 *
 * The RunCPM engine + state live once in lib/runcpm/runcpm_core.cpp.  This file
 * no longer includes the RunCPM header chain (which, since RunCPM 6.9, defines
 * the `Command` struct and the `CPM` macro that collide with the FujiNet bus
 * headers); it only supplies an RC2014 console back-end (runcpm_console_ops)
 * and asks the shared core to run a session.  The RC2014 bus has no
 * character-stream console, so the back-end is a no-op stub (see below).
 */

#include "rc2014cpm.h"

#include <string.h>

#include "fnSystem.h"

#include "../runcpm/runcpm_session.h"

/* ----- console back-end (runcpm_console_ops) -----
 *
 * The RC2014 systemBus exposes a block/transaction API (busTxBuffer /
 * busRxBuffer / busTxAvail), not a character-stream console (read / write /
 * available).  Historically the RC2014 CP/M console went through
 * abstraction_fujinet.h, whose console was only wired to the bus when
 * BYPASS_BUS was defined — and BYPASS_BUS is defined *only* for BUILD_ATARI.
 * For RC2014 the console was therefore a no-op stub (and _getch busy-looped
 * forever), so RC2014 CP/M has never had a working interactive console.
 *
 * To preserve that "no console transport" reality while compiling against the
 * shared core, these ops are clean no-ops.  getch returns CP/M EOF (^Z) instead
 * of busy-looping, so the session ends gracefully rather than hanging.
 */

static int rc2014_kbhit(void)
{
    return 0;
}

static int rc2014_getch(void)
{
    return 0x1a; /* ^Z / CP/M EOF */
}

static int rc2014_getche(void)
{
    return rc2014_getch();
}

static void rc2014_putch(uint8_t ch)
{
    (void)ch;
}

static void rc2014_clrscr(void)
{
}

static const runcpm_console_ops rc2014_console_ops = {
    rc2014_getch,
    rc2014_getche,
    rc2014_kbhit,
    rc2014_putch,
    rc2014_clrscr,
};

/* =========================== rc2014CPM device ========================== */

void rc2014CPM::rc2014_status()
{
    // Nothing to do here
    return;
}

void rc2014CPM::rc2014_handle_cpm()
{
    // Run a full CP/M session on the shared core using the RC2014 console.
    // Blocks until the session ends; the bus service loop then stops calling
    // us because cpmActive is cleared.
    runcpm_session_run(&rc2014_console_ops);
    cpmActive = false;
}

void rc2014CPM::init_cpm(int baud)
{
    // RAM/globals are owned by the shared core now; the RC2014 SPI bus does not
    // use a UART baud rate, so nothing transport-specific to set up here.
    (void)baud;
}

void rc2014CPM::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case 'G':
        rc2014_send_ack();
        fnSystem.delay(10);
        rc2014_send_complete();
        fnSystem.delay(5000);
        init_cpm(115200);
        cpmActive = true;
        break;
    default:
        rc2014_send_nak();
        break;
    }
}

#endif /* BUILD_RC2014 */
