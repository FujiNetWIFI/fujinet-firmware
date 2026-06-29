#ifndef RUNCPM_SESSION_H
#define RUNCPM_SESSION_H

/*
 * Public, RunCPM-internals-free entry point to the single shared RunCPM core
 * (runcpm_core.cpp).  Exactly one TU compiles the engine and owns its state;
 * transports include only this header and drive it as thin console back-ends.
 * One session runs at a time, enforced by a busy interlock in the core.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Console primitives a transport supplies for the active session. */
typedef struct runcpm_console_ops
{
    int  (*getch)(void);      /* blocking read, no echo (0..255) */
    int  (*getche)(void);     /* blocking read, with echo */
    int  (*kbhit)(void);      /* non-blocking: nonzero if a char is ready */
    void (*putch)(uint8_t c); /* write one byte */
    void (*clrscr)(void);     /* clear screen (may be a no-op) */
} runcpm_console_ops;

/* Run a CP/M session; blocks until it ends.  Returns false immediately if a
 * session is already active, true once the session it started has finished. */
bool runcpm_session_run(const runcpm_console_ops *ops);

/* Ask the active session to stop at the next opportunity.  Thread-safe; the
 * caller must still unblock its own getch (e.g. feed a CR/CTRL-C). */
void runcpm_session_request_exit(void);

/* True while a session is running. */
bool runcpm_session_active(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNCPM_SESSION_H */
