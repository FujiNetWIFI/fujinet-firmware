/* ESP32 compatibility layer for afpfs-ng
 *
 * Weak-linked stub implementations for POSIX functions declared in newlib
 * headers but not provided in the ESP-IDF link libraries.
 *
 * Using __attribute__((weak)) means:
 *  - If the real implementation exists in some future ESP-IDF version, it
 *    silently overrides these stubs (no duplicate-symbol error).
 *  - Both afpfs-ng and libnfs can define their own copies without conflict
 *    because the linker picks one weak definition per symbol.
 */

#ifdef ESP_PLATFORM

#include "esp32_compat.h"

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * signal() — loop.c installs handlers for SIGUSR2, SIGTERM, SIGINT.  *
 * On ESP32/FreeRTOS there are no POSIX signals, so this is a no-op.  *
 * Return SIG_DFL so callers don't treat the result as SIG_ERR.       *
 * ------------------------------------------------------------------ */
typedef void (*_afp_sig_t)(int);

__attribute__((weak))
_afp_sig_t signal(int signum, _afp_sig_t handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

/* ------------------------------------------------------------------ *
 * sigprocmask() — loop.c blocks SIGUSR2 before calling pselect.      *
 * No-op: FreeRTOS has no per-thread signal masks.                    *
 * ------------------------------------------------------------------ */
__attribute__((weak))
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;
    if (oldset) sigemptyset(oldset);
    return 0;
}

/* ------------------------------------------------------------------ *
 * pthread_kill() — signal_main_thread() sends SIGUSR2 to wake up the *
 * main loop from pselect.  On ESP32 we can't send signals, so this  *
 * is a no-op.  The consequence is that pselect() is not interrupted  *
 * early; the main loop processes events on its 30 s timeout instead. *
 * ------------------------------------------------------------------ */
__attribute__((weak))
int pthread_kill(pthread_t thread, int sig)
{
    (void)thread;
    (void)sig;
    return 0;
}

/* ------------------------------------------------------------------ *
 * pselect() — the AFP main event loop in loop.c relies on pselect to *
 * wait for readable file descriptors.  We implement it as a regular  *
 * select(), ignoring the signal mask (which is a no-op anyway).      *
 * ------------------------------------------------------------------ */
__attribute__((weak))
int pselect(int nfds,
            fd_set *readfds,
            fd_set *writefds,
            fd_set *exceptfds,
            const struct timespec *timeout,
            const sigset_t *sigmask)
{
    (void)sigmask;
    if (timeout) {
        struct timeval tv;
        tv.tv_sec  = (long)timeout->tv_sec;
        tv.tv_usec = (long)(timeout->tv_nsec / 1000L);
        return select(nfds, readfds, writefds, exceptfds, &tv);
    }
    return select(nfds, readfds, writefds, exceptfds, NULL);
}

/* ------------------------------------------------------------------ *
 * geteuid() — afp_server_init passes this to getpwuid().             *
 * ESP32 has no multi-user support; always return 0 (root).           *
 * ------------------------------------------------------------------ */
__attribute__((weak))
uid_t geteuid(void)
{
    return 0;
}

/* ------------------------------------------------------------------ *
 * getpwuid() — afp_server_init immediately memcpy's the result into  *
 * afp_server.passwd without a NULL check, so we MUST return a valid  *
 * pointer.  Return a static struct with safe dummy values.           *
 *                                                                    *
 * The char* fields point to string literals in flash (.rodata); the  *
 * pointers survive the memcpy and are safe to dereference later.     *
 * ------------------------------------------------------------------ */
static struct passwd _afpng_static_pw = {
    /* pw_name   */ (char *)"esp32",
    /* pw_passwd */ (char *)"",
    /* pw_uid    */ 0,
    /* pw_gid    */ 0,
    /* pw_gecos  */ (char *)"ESP32",
    /* pw_dir    */ (char *)"/",
    /* pw_shell  */ (char *)"",
};

__attribute__((weak))
struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    return &_afpng_static_pw;
}

#endif /* ESP_PLATFORM */
