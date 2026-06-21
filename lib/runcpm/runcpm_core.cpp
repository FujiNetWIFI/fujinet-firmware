/**
 * runcpm_core.cpp — the SINGLE shared RunCPM core.
 *
 * This is the ONLY translation unit that compiles the RunCPM (Z80/CP/M 2.2)
 * engine.  It is built WITHOUT RUNCPM_STATIC_IMPL, so every RunCPM symbol — the
 * ~10 KB Z80 flag tables, all of the .bss state, and every function body — gets
 * external linkage and is defined here exactly once.  The three transports
 * (SIO 'G' / siocpm.cpp, the telnet console / vm_telnet.cpp, and N:CPM:// /
 * CPM.cpp) no longer include the RunCPM header chain at all; they include only
 * runcpm_session.h and act as thin console back-ends.  This removes the two
 * redundant copies of the engine + state that previously triplicated ~30 KB of
 * ESP32 DRAM.
 *
 * Only one CP/M session runs at a time (no concurrent modem + TCP), enforced by
 * the g_busy interlock below.
 */

#define CCP_INTERNAL

/*
 * Use RunCPM's precomputed Z80 flag/arithmetic tables (cpu.h's `#ifdef preTables`
 * branch) instead of computing them at boot into ~11 KB of writable .bss.  With
 * preTables the 23 tables are `static const` and the linker places them in flash
 * (.rodata) rather than DRAM, and initTables() is skipped.  This is the final
 * piece that fits the single shared RunCPM core into the ESP32 dram0_0_seg:
 * consolidating the three engine copies reclaimed ~22 KB, and moving these
 * tables to flash reclaims the remaining ~11 KB of DRAM.  Must be defined before
 * cpu.h is included.
 */
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

/*
 * RunCPM header chain (external linkage — NO RUNCPM_STATIC_IMPL).
 * abstraction_fujinet_core.h supplies the disk/SD family and the transport-
 * agnostic console shims that dispatch through g_runcpm_console (defined below).
 */
#include "globals.h"
#include "abstraction_fujinet_core.h"
#include "ram.h"
#include "console.h"
#include "cpu.h"
#include "disk.h"
#include "host.h"
#include "cpm.h"
#include "ccp.h"

/* The active transport's console back-end.  Declared (extern "C") in
   abstraction_fujinet_core.h; the console shims there route _getch/_getche/
   _kbhit/_putch/_clrscr through it. */
extern "C" runcpm_console_ops g_runcpm_console = {};

/* Single-instance interlock: a second runcpm_session_run() returns false
   immediately rather than clobbering the shared RAM/state. */
static std::atomic<bool> g_busy{false};

extern "C" bool runcpm_session_run(const runcpm_console_ops *ops)
{
    if (ops == nullptr)
        return false;

    /* Refuse a second concurrent session (no modem + TCP at once). */
    if (g_busy.exchange(true))
        return false;

    g_runcpm_console = *ops;

    /*
     * CCP + warm-boot loop (mirrors the per-transport _cpm_run loops this
     * consolidates).  Status == STATUS_EXIT (1) ends the session; STATUS_RESTART
     * (2) re-boots CP/M as a real machine would on warm boot.
     */
    while (true)
    {
        Status = Debug = 0;
        Break = Step = Watch = -1;

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

    g_busy.store(false);
    return true;
}

extern "C" void runcpm_session_request_exit(void)
{
    /* BIOS-0-style end request: the CCP/Z80 loop stops at the next warm boot.
       The caller is responsible for unblocking its own console back-end (e.g.
       feeding a CR/CTRL-C so a blocking getch returns). */
    Status = STATUS_EXIT;
}

extern "C" bool runcpm_session_active(void)
{
    return g_busy.load();
}
