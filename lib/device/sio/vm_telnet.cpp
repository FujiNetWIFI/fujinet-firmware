/**
 * vm_telnet — loopback TELNET console for the shared VM (currently RunCPM).
 *
 * Reachable by an Atari that does N:TELNET://127.0.0.1:8677.  It is one of the
 * thin console back-ends to the single shared RunCPM core
 * (lib/runcpm/runcpm_core.cpp), alongside:
 *   - siocpm.cpp                       (SIO 'G' / R: / 9600)
 *   - lib/network-protocol/CPM.cpp     (N:CPM://)
 *
 * This TU no longer includes the RunCPM header chain.  It owns its own console
 * queues (_vm_txq / _vm_rxq), exposes them to the core as a runcpm_console_ops,
 * and asks the core to run a session:
 *   _vm_txq : user -> VM stdin   (pump pushes; getch pops)
 *   _vm_rxq : VM stdout -> user  (putch pushes; pump drains)
 *
 * Transport: a loopback TCP listen socket (127.0.0.1:8677) bridged through
 * libtelnet.  One live session at a time (one Z80, one shared SD CP/M drive);
 * additional connections wait in the listen backlog.
 *
 * Named vm_ (not cpm_) because the gate is VM-generic: future non-CP/M VMs can
 * reuse the same telnet console plumbing.
 */

#ifdef BUILD_ATARI

#include "vm_telnet.h"

#include "../runcpm/runcpm_session.h"

#include <string.h>

#include "libtelnet.h"
#include "compat_inet.h"
#include "fnConfig.h"
#include "fnSystem.h"
#include "../../include/debug.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "fnWiFi.h" // wait for the STA to obtain an IP before bind()/listen()
#else
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdlib> // atexit
#endif

/* -------------------------------------------------------------------------
 * Console queue storage (this TU owns the telnet console queues).
 *
 * _vm_txq : user -> VM stdin   (pump pushes; getch pops)
 * _vm_rxq : VM stdout -> user  (putch pushes; pump drains)
 * ------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
static QueueHandle_t _vm_rxq = nullptr;
static QueueHandle_t _vm_txq = nullptr;
#else
static std::queue<uint8_t> _vm_rxq;
static std::queue<uint8_t> _vm_txq;
static std::mutex _vm_rxmtx;
static std::mutex _vm_txmtx;
static std::condition_variable _vm_txcv;
#endif

/* Set by _vm_run() when the session ends (CCP exited, or teardown requested).
 * Polled by the pump/teardown to know the worker has unwound. */
static volatile bool _vm_session_ended = false;

/* Shutdown plumbing (PC build only).  The acceptor runs on its own thread and,
 * while a session is live, the VM worker thread parks on the queue mutex /
 * condition-variable below.  vm_telnet_stop() (called from main() before
 * teardown) sets _vm_telnet_quit, which both the accept loop and the session
 * pump poll, then joins the acceptor thread so every telnet thread is gone
 * while the mutexes are still valid. */
#ifndef ESP_PLATFORM
static std::atomic<bool> _vm_telnet_quit{false};
static std::thread       _vm_acceptor_thread;
#endif

/* TCP listen endpoint — loopback only, non-privileged port.
 * 127.0.0.1 avoids the macOS lo0-alias issue of 127.0.0.2; :8677 avoids the
 * privileged-port issue of :23. */
#define VM_TELNET_BIND_ADDR "127.0.0.1"
#define VM_TELNET_PORT      8677

/* TEST-ONLY: bind the console to all interfaces (0.0.0.0) instead of loopback,
 * so it can be reached from another machine on the LAN for testing.  WARNING:
 * this exposes an UNAUTHENTICATED CP/M shell (with |wget/|ftp host-side file
 * access) to the network.  Keep this 0 for any committed/upstream build; only
 * flip to 1 on a trusted test network. */
#define VM_TELNET_BIND_ANY  0

/* -------------------------------------------------------------------------
 * Console back-end (runcpm_console_ops) — bridges the core's console I/O to
 * the per-TU queues.
 * ------------------------------------------------------------------------- */
static int _vm_kbhit(void)
{
#ifdef ESP_PLATFORM
    if (_vm_txq == nullptr) return 0;
    UBaseType_t waiting = uxQueueMessagesWaiting(_vm_txq);
    if (waiting == 0)
    {
        /* No input pending — yield core 1 back to the SIO loop and idle task.
         *
         * The VM worker (this task) is pinned to core 1 at priority 20, ABOVE
         * fn_service_loop ("fnLoop", the SIO bus pump, priority 17, also core 1)
         * and IDLE1 (priority 0).  CP/M full-screen programs (vi, etc.) poll
         * console status (BDOS 6/11 -> _kbhit) in a tight loop while waiting for
         * a keypress.  If we returned immediately the worker would never block,
         * so fnLoop would never run (every SIO command — including the Atari's
         * own N:TELNET reads back into this console — times out) and IDLE1 would
         * never run (the task watchdog is never fed and deleted tasks are never
         * reaped), wedging the device into an unrecoverable hang where even the
         * safe-reset button stops responding.  taskYIELD() cannot fix this: it
         * only yields to ready tasks of EQUAL OR HIGHER priority, and this
         * worker is the highest-priority task on core 1.  Block for one tick so
         * the lower-priority SIO loop and idle task get the core back.
         *
         * Cost is latency on the idle path only: real Z80 computation does not
         * poll kbhit, and a screen redraw polls it once after many putch calls,
         * so throughput is unaffected. */
        vTaskDelay(1);
        return 0;
    }
    return (int)waiting;
#else
    std::lock_guard<std::mutex> lk(_vm_txmtx);
    return (int)_vm_txq.size();
#endif
}

static int _vm_getch(void)
{
    uint8_t c = 0;
#ifdef ESP_PLATFORM
    if (_vm_txq != nullptr)
        xQueueReceive(_vm_txq, &c, portMAX_DELAY);
#else
    std::unique_lock<std::mutex> lk(_vm_txmtx);
    _vm_txcv.wait(lk, [] { return !_vm_txq.empty(); });
    c = _vm_txq.front();
    _vm_txq.pop();
#endif
    return c;
}

static void _vm_push_rx(uint8_t c)
{
#ifdef ESP_PLATFORM
    if (_vm_rxq != nullptr)
        xQueueSend(_vm_rxq, &c, portMAX_DELAY);
#else
    {
        std::lock_guard<std::mutex> lk(_vm_rxmtx);
        _vm_rxq.push(c);
    }
#endif
}

static int _vm_getche(void)
{
    uint8_t c = (uint8_t)_vm_getch();
    /* echo back through rxq so the terminal sees it */
    _vm_push_rx(c);
    return c;
}

static void _vm_putch(uint8_t ch)
{
    _vm_push_rx(ch);
}

static void _vm_clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    _vm_push_rx(0x1B); _vm_push_rx('['); _vm_push_rx('1'); _vm_push_rx(';');
    _vm_push_rx('1');  _vm_push_rx('H'); _vm_push_rx(0x1B); _vm_push_rx('[');
    _vm_push_rx('2');  _vm_push_rx('J');
}

static const runcpm_console_ops vm_console_ops = {
    _vm_getch,
    _vm_getche,
    _vm_kbhit,
    _vm_putch,
    _vm_clrscr,
};

/* -------------------------------------------------------------------------
 * VM session worker — run a full session on the shared core, then signal end.
 * ------------------------------------------------------------------------- */
static void _vm_run(void)
{
    runcpm_session_run(&vm_console_ops);
    _vm_session_ended = true;
}

#ifdef ESP_PLATFORM
static void _vm_task_entry(void *arg)
{
    (void)arg;
    _vm_run();
    vTaskDelete(nullptr);
}
#endif

/* -------------------------------------------------------------------------
 * Queue helpers — bridge the socket pump to the console queues.
 * ------------------------------------------------------------------------- */
static void _push_stdin(uint8_t c)
{
#ifdef ESP_PLATFORM
    if (_vm_txq != nullptr)
        xQueueSend(_vm_txq, &c, portMAX_DELAY);
#else
    {
        std::lock_guard<std::mutex> lk(_vm_txmtx);
        _vm_txq.push(c);
    }
    _vm_txcv.notify_one();
#endif
}

static bool _pop_stdout(uint8_t &c)
{
#ifdef ESP_PLATFORM
    if (_vm_rxq == nullptr)
        return false;
    return xQueueReceive(_vm_rxq, &c, 0) == pdTRUE;
#else
    std::lock_guard<std::mutex> lk(_vm_rxmtx);
    if (_vm_rxq.empty())
        return false;
    c = _vm_rxq.front();
    _vm_rxq.pop();
    return true;
#endif
}

#ifndef ESP_PLATFORM
static void _drain_queues(void)
{
    {
        std::lock_guard<std::mutex> lk(_vm_txmtx);
        std::queue<uint8_t> empty;
        std::swap(_vm_txq, empty);
    }
    {
        std::lock_guard<std::mutex> lk(_vm_rxmtx);
        std::queue<uint8_t> empty;
        std::swap(_vm_rxq, empty);
    }
}
#endif

/* -------------------------------------------------------------------------
 * libtelnet server side
 * ------------------------------------------------------------------------- */
struct vm_telnet_ctx
{
    int fd;
};

/* Server echoes (the core's getche provides the single echo) and runs
 * character-at-a-time.  Matches the N: client telopts in Telnet.cpp
 * ({ECHO, WONT, DO} / {SGA, WONT, DO}): server WILL ECHO + WILL SGA. */
static const telnet_telopt_t srv_telopts[] = {
    {TELNET_TELOPT_ECHO, TELNET_WILL, TELNET_DONT},
    {TELNET_TELOPT_SGA,  TELNET_WILL, TELNET_DONT},
    {-1, 0, 0}
};

static void _telnet_evt(telnet_t *tn, telnet_event_t *ev, void *user)
{
    (void)tn;
    vm_telnet_ctx *ctx = (vm_telnet_ctx *)user;

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        /* Decoded input from the client -> VM stdin.
         * Normalise line endings to a single CR (0x0D): pass CR through, drop
         * bare LF (0x0A, the second byte of CR-LF) and NUL (the second byte of
         * CR-NUL). */
        for (size_t i = 0; i < ev->data.size; ++i)
        {
            uint8_t c = (uint8_t)ev->data.buffer[i];
            if (c == 0x0A || c == 0x00)
                continue;
            _push_stdin(c);
        }
        break;

    case TELNET_EV_SEND:
        /* libtelnet wants raw bytes written to the socket. */
        {
            const char *p = ev->data.buffer;
            size_t left = ev->data.size;
            while (left > 0)
            {
                int n = (int)send(ctx->fd, p, left, 0);
                if (n <= 0)
                    break;
                p += n;
                left -= (size_t)n;
            }
        }
        break;

    default:
        break;
    }
}

/* True if a recv() returning < 0 just means "no data right now". */
static bool _recv_would_block(int err)
{
#if defined(_WIN32)
    return err == WSAEWOULDBLOCK;
#else
    return err == EWOULDBLOCK || err == EAGAIN || err == EINTR;
#endif
}

/* -------------------------------------------------------------------------
 * One telnet session: bridge the accepted socket to a fresh VM session.
 * ------------------------------------------------------------------------- */
static void _handle_session(int cfd)
{
    Debug_printf("vm_telnet: session start (fd %d)\r\n", cfd);

    _vm_session_ended = false;

#ifdef ESP_PLATFORM
    _vm_rxq = xQueueCreate(2048, sizeof(uint8_t));
    _vm_txq = xQueueCreate(2048, sizeof(uint8_t));
#else
    _drain_queues();
#endif

    compat_socket_set_nonblocking(cfd);

    vm_telnet_ctx ctx;
    ctx.fd = cfd;

    telnet_t *tn = telnet_init(srv_telopts, _telnet_evt, 0, &ctx);
    if (tn == nullptr)
    {
        Debug_printf("vm_telnet: telnet_init failed\r\n");
#ifdef ESP_PLATFORM
        if (_vm_rxq) { vQueueDelete(_vm_rxq); _vm_rxq = nullptr; }
        if (_vm_txq) { vQueueDelete(_vm_txq); _vm_txq = nullptr; }
#endif
        return;
    }

    /* Drive option negotiation so the client disables local echo. */
    telnet_negotiate(tn, TELNET_WILL, TELNET_TELOPT_ECHO);
    telnet_negotiate(tn, TELNET_WILL, TELNET_TELOPT_SGA);

    /* Launch the VM worker (it blocks in getch waiting on _vm_txq). */
#ifdef ESP_PLATFORM
    TaskHandle_t vmTask = nullptr;
    xTaskCreatePinnedToCore(_vm_task_entry,
                            "cpmtel",
#ifdef BUILD_APPLE
                            4096,
#else
                            32768,
#endif
                            nullptr,
                            20,
                            &vmTask,
                            1);
#else
    std::thread vmThread(_vm_run);
#endif

    /* Pump: socket -> telnet_recv (-> _vm_txq via EV_DATA);
     *       _vm_rxq -> telnet_send (-> socket via EV_SEND). */
    char rbuf[256];
    char obuf[256];
    bool peer_closed = false;

    while (!_vm_session_ended && !peer_closed
#ifndef ESP_PLATFORM
           && !_vm_telnet_quit
#endif
          )
    {
        int n = (int)recv(cfd, rbuf, sizeof(rbuf), 0);
        if (n == 0)
        {
            peer_closed = true; /* peer performed orderly shutdown */
        }
        else if (n > 0)
        {
            telnet_recv(tn, rbuf, (size_t)n);
        }
        else /* n < 0 */
        {
            if (!_recv_would_block(compat_getsockerr()))
            {
                peer_closed = true;
            }
        }

        /* Drain VM stdout to the client. */
        int oi = 0;
        uint8_t c;
        while (_pop_stdout(c))
        {
            obuf[oi++] = (char)c;
            if (oi == (int)sizeof(obuf))
            {
                telnet_send(tn, obuf, (size_t)oi);
                oi = 0;
            }
        }
        if (oi > 0)
            telnet_send(tn, obuf, (size_t)oi);

        fnSystem.delay(2);
    }

    /* Teardown.  Ask the core's CCP loop (and any running Z80 program, via
     * Z80run's `while(!Status)`) to stop, then feed CR bytes to unblock the
     * blocking console read (BDOS C_READSTR -> getch).
     *
     * We deliberately do NOT send CTRL-C (0x03): at column 0 the CCP treats ^C
     * as a warm boot (Status=STATUS_RESTART), which makes the session loop
     * re-enter and reset Status to STATUS_RUNNING — the worker then blocks in
     * getch forever and the join below hangs, stalling the acceptor so no
     * further connection is served.  A CR just completes an empty command line;
     * the CCP loop then observes the exit request and returns cleanly. */
    runcpm_session_request_exit();
    for (int i = 0; i < 100 && !_vm_session_ended; ++i)
    {
        _push_stdin('\r');
        fnSystem.delay(20);
    }

#ifdef ESP_PLATFORM
    /* Task self-deletes when _vm_run returns; ensure it has unwound. */
    if (!_vm_session_ended)
        vTaskDelay(pdMS_TO_TICKS(300));
    (void)vmTask;
    if (_vm_rxq) { vQueueDelete(_vm_rxq); _vm_rxq = nullptr; }
    if (_vm_txq) { vQueueDelete(_vm_txq); _vm_txq = nullptr; }
#else
    if (vmThread.joinable())
        vmThread.join();
#endif

    telnet_free(tn);
    Debug_printf("vm_telnet: session end (fd %d)\r\n", cfd);
}

/* -------------------------------------------------------------------------
 * Acceptor loop — owns the listen socket and serves one session at a time.
 * ------------------------------------------------------------------------- */
static void _acceptor_loop(void)
{
#ifdef ESP_PLATFORM
    /* Wait for the STA to actually obtain an IP before we bind/listen.  On
     * ESP-IDF lwIP the listen socket must be created once the network stack is
     * fully up; binding INADDR_ANY while the STA interface is still
     * address-less leaves the listener unable to accept on the eventual STA
     * address (the SYN is answered at the PCB level — so the port looks "open"
     * to a probe — but accept() never yields a usable session).  fnWiFi.start()
     * has already run esp_netif_init() (so socket() is safe here); we just hold
     * off the bind until IP_EVENT_STA_GOT_IP has set fnWiFi.connected().  This
     * mirrors the firmware's documented async-WiFi bring-up pattern. */
    while (!fnWiFi.connected())
        vTaskDelay(pdMS_TO_TICKS(250));
#endif

    int lfd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
    {
        Debug_printf("vm_telnet: socket() failed, err %d\r\n", compat_getsockerr());
        return;
    }

    int enable = 1;
#if defined(_WIN32)
    setsockopt(lfd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&enable, sizeof(enable));
#else
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#endif

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(VM_TELNET_PORT);
#if VM_TELNET_BIND_ANY
    srv.sin_addr.s_addr = htonl(INADDR_ANY); /* TEST-ONLY: all interfaces */
#else
    srv.sin_addr.s_addr = inet_addr(VM_TELNET_BIND_ADDR);
#endif

    if (bind(lfd, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
        Debug_printf("vm_telnet: bind(%s:%d) failed, err %d\r\n",
                     VM_TELNET_BIND_ADDR, VM_TELNET_PORT, compat_getsockerr());
        closesocket(lfd);
        return;
    }

    if (listen(lfd, 1) < 0)
    {
        Debug_printf("vm_telnet: listen() failed, err %d\r\n", compat_getsockerr());
        closesocket(lfd);
        return;
    }

    Debug_printf("vm_telnet: CP/M telnet server listening on %s:%d\r\n",
#if VM_TELNET_BIND_ANY
                 "0.0.0.0",
#else
                 VM_TELNET_BIND_ADDR,
#endif
                 VM_TELNET_PORT);

    /* PC build: make accept() non-blocking so the loop can poll the shutdown
     * flag and exit cleanly at process teardown.  (ESP runs forever and reboots
     * the chip, so it keeps the original blocking accept.) */
#ifndef ESP_PLATFORM
    compat_socket_set_nonblocking(lfd);
#endif

    while (true)
    {
#ifndef ESP_PLATFORM
        if (_vm_telnet_quit)
            break;
#endif
        struct sockaddr_in cli;
        socklen_t cl = sizeof(cli);
        int cfd = (int)accept(lfd, (struct sockaddr *)&cli, &cl);
        if (cfd < 0)
        {
            fnSystem.delay(100);
            continue;
        }

        _handle_session(cfd);
        closesocket(cfd);
    }

#ifndef ESP_PLATFORM
    closesocket(lfd);
#endif
}

#ifdef ESP_PLATFORM
static void _acceptor_task_entry(void *arg)
{
    (void)arg;
    _acceptor_loop();
    vTaskDelete(nullptr);
}
#endif

/* -------------------------------------------------------------------------
 * Public entry — spawn the acceptor once at boot.
 * ------------------------------------------------------------------------- */
void vm_telnet_start()
{
    static bool started = false;
    if (started)
        return;
    started = true;

#ifdef ESP_PLATFORM
    /* Pin the acceptor to CORE 0, NOT core 1.  fn_service_loop ("fnLoop", the
     * SIO bus pump) runs at priority 17 pinned to core 1 in a tight
     * non-blocking loop (SYSTEM_BUS.service() + taskYIELD()).  taskYIELD() only
     * yields to ready tasks of EQUAL OR HIGHER priority, so a priority-10
     * acceptor on core 1 would be permanently starved: its blocking accept()
     * wakeup could never get CPU and no telnet session would ever start (the
     * port still listens, but connecting produced no CP/M banner).  Core 0 is
     * where the WiFi / lwIP tcpip thread lives; those idle-sleep when there is
     * no traffic, so the acceptor gets scheduled there. */
    xTaskCreatePinnedToCore(_acceptor_task_entry, "cpmtelsrv", 8192,
                            nullptr, 10, nullptr, 0);
#else
    /* Keep the thread joinable (not detached) so vm_telnet_stop() can wind it
     * down before the global queue mutexes are destroyed at process exit. */
    _vm_acceptor_thread = std::thread(_acceptor_loop);

    /* Safety net: not every shutdown returns through main() — some paths call
     * exit() directly (e.g. SystemManager::reboot()).  Register vm_telnet_stop
     * with atexit so the acceptor is always joined before the C++ runtime
     * destroys the global queue mutexes AND before this std::thread's own
     * destructor runs (which would std::terminate() on a still-joinable
     * thread).  atexit handlers registered here (after static init) run before
     * those static destructors; on _exit() no destructors run at all, so
     * skipping it there is harmless.  vm_telnet_stop() is idempotent. */
    atexit(vm_telnet_stop);
#endif
}

void vm_telnet_stop()
{
#ifndef ESP_PLATFORM
    if (!_vm_acceptor_thread.joinable())
        return;

    /* Ask the accept loop and the live-session pump to exit, then join.  The
     * pump runs on this same acceptor thread, so its existing teardown (request
     * exit, feed CR, join the VM worker) runs first; the loop then sees
     * _vm_telnet_quit and returns, closing the listen socket. */
    _vm_telnet_quit = true;
    _vm_acceptor_thread.join();
#endif
}

#endif /* BUILD_ATARI */
