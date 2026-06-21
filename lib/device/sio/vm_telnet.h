#ifndef VM_TELNET_H
#define VM_TELNET_H

#ifdef BUILD_ATARI

/**
 * vm_telnet — loopback TELNET console for the shared VM (currently RunCPM).
 *
 * Starts a TCP server bound to 127.0.0.1:8677.  An Atari client that does
 * N:TELNET://127.0.0.1:8677 lands directly in a CP/M session — no ATCPM,
 * no SIO takeover, at whatever speed the N: device is configured for.
 *
 * This is one of the thin console back-ends to the single shared RunCPM core
 * (lib/runcpm/runcpm_core.cpp), alongside the SIO 'G' R:/9600 path in
 * siocpm.cpp and the N:CPM:// path in lib/network-protocol/CPM.cpp.  It bridges
 * a socket <-> libtelnet <-> its own stdin/stdout queues, exposes those queues
 * to the core as a runcpm_console_ops, and asks the core to run a session.
 *
 * Named vm_ (not cpm_) because the gate is VM-generic: future non-CP/M VMs can
 * reuse the same telnet console plumbing.
 *
 * Call vm_telnet_start() once at boot (gated on Config.get_cpm_enabled()).
 */
void vm_telnet_start();

/**
 * vm_telnet_stop() — stop the acceptor (and any live session) and join its
 * thread.  Call this at process shutdown, BEFORE the global CP/M queue
 * mutexes/condition-variable are destroyed.
 *
 * Without it the detached acceptor/CP/M threads outlive those static globals
 * during exit and abort the process with "mutex lock failed: Invalid argument"
 * (pthread_mutex_lock returns EINVAL on a destroyed mutex).  No-op on ESP32,
 * where the whole chip reboots instead of unwinding.
 */
void vm_telnet_stop();

#endif /* BUILD_ATARI */

#endif /* VM_TELNET_H */
