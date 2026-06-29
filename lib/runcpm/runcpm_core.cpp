/**
 * runcpm_core.cpp - the one and only build of the RunCPM engine.
 *
 * Historically every transport (the SIO/Atari bus device, the IWM/Apple and
 * DriveWire/CoCo background tasks, the RS232 bus device and the N:CPM://
 * network adapter) #included the whole header-only engine into its own
 * translation unit.  That meant several independent 64K RAM images and several
 * copies of the BDOS/BIOS/CCP code in the firmware, kept apart only by the
 * RUNCPM_STATIC_IMPL "make every symbol static" hack.
 *
 * This file compiles the engine exactly once, with normal (external) linkage,
 * for every platform.  Transports no longer include the engine; they call
 * runcpm_session_run() with a small set of console callbacks (see
 * runcpm_session.h) and the engine talks to them through g_runcpm_console.
 */

#include <atomic>
#include <cstdlib>
#include <cstring>

#define CCP_INTERNAL

#include "runcpm_session.h"

#include "globals.h"
#include "abstraction_fujinet.h" // filesystem + console-dispatch glue
#include "ram.h"                 // RAM access
#include "console.h"             // _putcon/_puts built on the console callbacks
#include "cpu.h"                 // Z80 core + Status/Debug/Break/Step
#include "disk.h"                // CP/M disk abstraction
#include "host.h"                // custom host-specific BDOS call
#include "cpm.h"                 // CP/M structures and BDOS/BIOS
#include "ccp.h"                 // internal CCP

// The live console endpoint.  Declared extern in abstraction_fujinet.h and read
// by the _kbhit/_getch/_putch/_clrscr glue there.
runcpm_console_ops g_runcpm_console{};

// The engine owns a single 64K RAM image and is not re-entrant, so only one
// session may run at a time.  g_busy guards that; g_exit lets another task ask
// the running session to stop.
static std::atomic<bool> g_busy{false};
static volatile bool     g_exit = false;

bool runcpm_session_active(void)
{
    return g_busy.load();
}

void runcpm_session_request_exit(void)
{
    // Status == 1 is the engine's "BIOS BOOT / exit CP/M" signal; setting it
    // makes the CCP fall out of its loop at the next iteration.  g_exit also
    // breaks our own warm-boot loop below.
    g_exit = true;
    Status = 1;
}

bool runcpm_session_run(const runcpm_console_ops *ops)
{
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true))
        return false; // a session is already running

    g_exit = false;
    g_runcpm_console = *ops;

    // One-time machine setup for the whole session.
    Status = Debug = 0;
    Break = Step = -1;
    RAM = (uint8 *)malloc(MEMSIZE);
    if (RAM != nullptr)
    {
        memset(RAM, 0, MEMSIZE);
        memset(filename, 0, sizeof(filename));
        memset(newname, 0, sizeof(newname));
        memset(fcbname, 0, sizeof(fcbname));
        memset(pattern, 0, sizeof(pattern));

        // CCP loop: a warm boot (Status == 2, e.g. ^C at the prompt or a
        // program that RETs) re-enters the CCP and reprints the banner, exactly
        // like real CP/M.  An exit (Status == 1) or an external exit request
        // ends the session.
        while (true)
        {
            _puts(CCPHEAD);
            _PatchCPM();
            Status = 0;
            _ccp();
            if (Status == 1 || g_exit)
                break;
        }

        free(RAM);
        RAM = nullptr;
    }

    g_runcpm_console = runcpm_console_ops{};
    g_busy.store(false);
    return true;
}
