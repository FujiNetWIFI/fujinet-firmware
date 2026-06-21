#ifndef RUNCPM_SESSION_H
#define RUNCPM_SESSION_H

/*
 * runcpm_session — public, RunCPM-internals-free entry point to the single
 * shared RunCPM core (lib/runcpm/runcpm_core.cpp).
 *
 * Background: RunCPM is a header-only Z80/CP/M 2.2 emulator.  It used to be
 * #included directly by three transports (SIO 'G' / siocpm.cpp, the telnet
 * console / vm_telnet.cpp, and N:CPM:// / CPM.cpp).  Because globals.h tags
 * symbols `static` under RUNCPM_STATIC_IMPL, every transport that included the
 * header chain got its OWN private copy of all RunCPM state, the ~10 KB Z80
 * flag tables and every function body — triplicating ~30 KB of ESP32 DRAM.
 *
 * Now exactly ONE translation unit (runcpm_core.cpp) compiles the RunCPM
 * engine and owns the state.  The transports include only this header and act
 * as thin console back-ends: each supplies a set of console callbacks and asks
 * the core to run a session.  Only one CP/M session runs at a time (no
 * concurrent modem + TCP), enforced by a simple busy interlock in the core.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Console back-end supplied by a transport.  These mirror RunCPM's console
 * primitives (_getch/_getche/_kbhit/_putch/_clrscr); the core routes its
 * internal console I/O through whichever back-end started the active session.
 */
typedef struct runcpm_console_ops
{
    int  (*getch)(void);     /* blocking read of one char (0..255), no echo  */
    int  (*getche)(void);    /* blocking read of one char, with echo         */
    int  (*kbhit)(void);     /* non-blocking: nonzero if a char is ready     */
    void (*putch)(uint8_t c);/* write one byte to the terminal               */
    void (*clrscr)(void);    /* clear the screen (may be a no-op)            */
} runcpm_console_ops;

/*
 * Run a full CP/M session (CCP + warm-boot loop) using the supplied console
 * back-end.  BLOCKS until the session ends (clean exit or teardown request).
 * Returns false immediately, running nothing, if a session is already active
 * (the single-instance interlock); returns true once a session it started has
 * finished.
 */
bool runcpm_session_run(const runcpm_console_ops *ops);

/*
 * Ask the active session's CCP (and any running Z80 program) to stop at the
 * next opportunity.  Safe to call from another thread/task than the one that
 * is blocked in runcpm_session_run().  The caller is still responsible for
 * unblocking its own console back-end (e.g. feeding a CR/CTRL-C so a blocking
 * getch returns), exactly as the transports did before.
 */
void runcpm_session_request_exit(void);

/* True while a session is running. */
bool runcpm_session_active(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNCPM_SESSION_H */
