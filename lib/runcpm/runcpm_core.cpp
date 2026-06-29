/*
 * The single shared RunCPM core: the only TU that compiles the Z80/CP/M engine
 * (built without RUNCPM_STATIC_IMPL, so all state/tables/functions are external
 * and defined here once).  Transports include runcpm_session.h only and drive
 * it as console back-ends.  One session at a time, enforced by g_busy.
 */

#define CCP_INTERNAL

/* Use cpu.h's precomputed const Z80 tables (placed in flash) instead of
   building them into ~11 KB of DRAM .bss at boot.  Must precede cpu.h. */
#define preTables

#include <atomic>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(ARDUINO)
#include <unistd.h> // cpu.h's Z80 throttle calls usleep() on POSIX/ESP-IDF
#endif

#include "fnSystem.h"
#include "fnFS.h"
#include "fnFsSD.h"

#include "runcpm_session.h"

/* RunCPM header chain (external linkage).  abstraction_fujinet_core.h supplies
   the disk/SD family and the console shims that dispatch through
   g_runcpm_console. */
#include "globals.h"
#include "abstraction_fujinet_core.h"
#include "ram.h"
#include "console.h"
#include "cpu.h"
#include "disk.h"
#include "host.h"
#include "cpm.h"
#include "ccp.h"

/* Active transport's console back-end; the shims in abstraction_fujinet_core.h
   route _getch/_putch/etc. through it. */
extern "C" runcpm_console_ops g_runcpm_console = {};

/* Single-instance interlock against clobbering the shared RAM/state. */
static std::atomic<bool> g_busy{false};

extern "C" bool runcpm_session_run(const runcpm_console_ops *ops)
{
    if (ops == nullptr)
        return false;

    if (g_busy.exchange(true))
        return false;

    g_runcpm_console = *ops;

    /* CCP + warm-boot loop: STATUS_EXIT ends the session, STATUS_RESTART warm-
       boots CP/M as real hardware would. */
    while (true)
    {
        Status = Debug = 0;
        Break = Step = Watch = -1;

        /* Drop any cached sequential-read handle so a file replaced between
           boots is never read through a stale handle. */
        _seq_cache_close();

        RAM = (uint8_t *)malloc(MEMSIZE);
        if (!RAM)
            break;

        memset(RAM,      0, MEMSIZE);
        memset(filename, 0, sizeof(filename));
        memset(newname,  0, sizeof(newname));
        memset(fcbname,  0, sizeof(fcbname));
        memset(pattern,  0, sizeof(pattern));

        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();

        free(RAM);
        RAM = nullptr;

        if (Status == STATUS_EXIT)
            break;
    }

    _seq_cache_close();

    g_busy.store(false);
    return true;
}

extern "C" void runcpm_session_request_exit(void)
{
    /* Stop at the next warm boot; caller must unblock its own getch. */
    Status = STATUS_EXIT;
}

extern "C" bool runcpm_session_active(void)
{
    return g_busy.load();
}
