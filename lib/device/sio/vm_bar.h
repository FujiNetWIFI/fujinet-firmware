#ifndef VM_BAR_H
#define VM_BAR_H

#ifdef BUILD_ATARI

/**
 * vm_bar — RSX-style "bar command" gate between the VM (RunCPM) and FujiNet
 * firmware.
 *
 * Amstrad-CPC inspired: when an Atari at the CP/M prompt types a line whose
 * first non-space character is '|', the RunCPM CCP (lib/runcpm/ccp.h) hands
 * the remainder of the line off to this single firmware-side gate instead of
 * trying to load a .COM program.  The gate parses a verb (e.g. "wget"),
 * performs a host-side action (download a URL, etc.) and returns a short
 * status message which the CCP prints with its own console primitive.
 *
 * This is the ONE place where CP/M reaches out to the firmware.  The gate is
 * deliberately decoupled from RunCPM internals: it receives a plain
 * NUL-terminated command string plus the destination directory prefix that
 * the calling RunCPM instance uses for its CP/M files, and writes back a
 * status string.  It never touches Z80 RAM, the console queues, or RunCPM's
 * Status flag, so the same gate works for all three RunCPM instances
 * (SIO 'G'/R:, N:CPM://, and the loopback telnet console).
 *
 * Most verbs are "one string in, one string out": they perform their action
 * and return a status line in outmsg which the CCP prints.  A few verbs are
 * INTERACTIVE (currently |ftp): they run a sub-shell that needs continuous
 * terminal I/O.  The CCP's console primitives (_getch/_putch/_puts) are
 * per-translation-unit statics, so the once-compiled gate cannot call them
 * directly; instead the CCP hook passes them in via the vm_bar_io callback
 * struct below.  Interactive verbs print through io and leave outmsg empty;
 * non-interactive verbs ignore io.
 */
struct vm_bar_io
{
    int (*getch)(void);          // blocking read of one char (0..255), no echo
    void (*putch)(int c);        // write one char to the terminal
    void (*puts_)(const char *s);// write a NUL-terminated string to the terminal
};

/**
 * @param cmd         remainder of the command line after the leading '|'
 *                    (NUL-terminated, host pointer into the Z80 RAM array)
 * @param dir_prefix  host filesystem prefix for the active CP/M directory,
 *                    already ending in '/'  (e.g. "/CPM/A/0/")
 * @param outmsg      caller-provided buffer for a status/result message
 * @param outmsg_size size of outmsg in bytes
 * @param io          terminal I/O callbacks for interactive verbs (may be NULL
 *                    for non-interactive callers; interactive verbs require it)
 * @return 0 on success, non-zero on error (the message is in outmsg either way)
 */
extern "C" int vm_bar_command(const char *cmd, const char *dir_prefix,
                              char *outmsg, int outmsg_size,
                              const struct vm_bar_io *io);

#endif /* BUILD_ATARI */

#endif /* VM_BAR_H */
