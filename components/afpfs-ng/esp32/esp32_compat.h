/* ESP32 compatibility layer for afpfs-ng
 *
 * Provides weak-linked stubs for POSIX APIs that are declared in newlib
 * headers but not implemented in ESP-IDF/FreeRTOS:
 *
 *   signal()        - loop.c (termination_handler, afp_main_loop)
 *   sigprocmask()   - loop.c
 *   pthread_kill()  - loop.c (signal_main_thread)
 *   pselect()       - loop.c (main event loop)
 *   geteuid()       - afp.c (afp_server_init)
 *   getpwuid()      - afp.c (afp_server_init, memcpy'd without NULL check)
 *
 * All implementations live in esp32_compat.c as __attribute__((weak))
 * functions so that a stronger system definition silently wins if present.
 *
 * This header is force-included for every afpfs-ng compilation unit via
 *   target_compile_options(-include ...) in CMakeLists.txt
 * so library source files do not need to be modified.
 *
 * pselect() is the only function declared here because it is absent from
 * every newlib header; all other stubs are already declared in their
 * respective system headers and only need a link-time definition.
 */

#ifndef _AFPFS_ESP32_COMPAT_H_
#define _AFPFS_ESP32_COMPAT_H_

#ifdef ESP_PLATFORM

#include <sys/types.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>   /* sigset_t, sigemptyset/sigaddset macros, SIG_DFL */

#ifdef __cplusplus
extern "C" {
#endif

/* pselect - not present in newlib at all; forward-declare so loop.c compiles
 * without an implicit-function-declaration error.  Implementation (weak) is
 * in esp32_compat.c. */
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *sigmask);

#ifdef __cplusplus
}
#endif

#endif /* ESP_PLATFORM */

#endif /* _AFPFS_ESP32_COMPAT_H_ */
