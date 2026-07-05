#ifndef RUNCPM_SESSION_H
#define RUNCPM_SESSION_H

#include <cstdint>

// Single shared entry point into the RunCPM engine.
//
// The engine itself is compiled exactly once (runcpm_core.cpp, global
// linkage).  Every transport that wants to run CP/M - the SIO/Atari bus, the
// IWM/Apple and DriveWire/CoCo background tasks, the RS232 bus and the
// N:CPM:// network adapter - drives that one engine copy by supplying a small
// set of console callbacks and calling runcpm_session_run().
//
// Only the four console primitives differ between transports; all of the BDOS,
// BIOS, disk and CCP logic is shared.  The callbacks are deliberately plain C
// function pointers so the engine (compiled as C-style code) can call back into
// whichever device object owns the current session.
typedef struct runcpm_console_ops {
    int     (*kbhit)(void);        // non-zero if a character is waiting
    uint8_t (*getch)(void);        // blocking read of one character
    void    (*putch)(uint8_t c);   // write one character
    void    (*clrscr)(void);       // clear screen; may be NULL
} runcpm_console_ops;

// Run a full CP/M session using the supplied console callbacks.  Blocks for the
// lifetime of the session (until the program exits CP/M or an exit is
// requested).  Returns false immediately if another session is already active
// (the engine has a single 64K RAM image and is not re-entrant).
bool runcpm_session_run(const runcpm_console_ops *ops);

// Ask the currently running session to terminate at the next CCP iteration.
// Safe to call from another task/transport (e.g. when the bus tears the link
// down out from under a blocked session).
void runcpm_session_request_exit(void);

// True while a session is running.
bool runcpm_session_active(void);

#endif // RUNCPM_SESSION_H
