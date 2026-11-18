/**
 * RunCPM I/O abstraction for NetworkProtocol CPM adapter.
 *
 * Console I/O is routed through per-TU static queues (_cpm_rxq / _cpm_txq).
 * Filesystem I/O comes from abstraction_fujinet_disk.h.
 *
 * Include this AFTER #define RUNCPM_STATIC_IMPL, BEFORE other RunCPM headers.
 * All symbols are static so this TU coexists with bus-device CPM TUs.
 */

#ifndef ABSTRACTION_NETWORK_PROTOCOL_H
#define ABSTRACTION_NETWORK_PROTOCOL_H

#include <string.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "../../include/debug.h"
#include "fnFsSD.h"

#define FOLDERCHAR '/'
#define HostOS 0x07  // FUJINET

#include "abstraction_fujinet_disk.h"

/* -------------------------------------------------------------------------
 * Queue storage — TU-local due to RUNCPM_STATIC_IMPL / static keyword.
 *
 * _cpm_txq : user → CPM stdin   (write() pushes; _getch() pops)
 * _cpm_rxq : CPM stdout → user  (_putch() pushes; read() pops)
 * ------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
static QueueHandle_t _cpm_rxq = nullptr;
static QueueHandle_t _cpm_txq = nullptr;
#include "freertos/semphr.h"
/* Given by the CP/M task as its last act, so stopCPM() knows it is safe to
 * delete the queues. */
static SemaphoreHandle_t _cpm_exited = nullptr;
#else
#include <queue>
#include <mutex>
#include <condition_variable>
static std::queue<uint8_t> _cpm_rxq;
static std::queue<uint8_t> _cpm_txq;
static std::mutex _cpm_rxmtx;
static std::mutex _cpm_txmtx;
static std::condition_variable _cpm_txcv;
#endif

/* Set by _cpm_run() when the CCP exits of its own accord (e.g. user typed EXIT).
 * Polled by NetworkProtocolCPM::status() to drive EOF back to the host. */
static volatile bool _cpm_session_ended = false;

/* Set by stopCPM(). Unlike RunCPM's Status (which a CTRL-C at the prompt
 * resets to "warm boot"), nothing in RunCPM clears this, so the CP/M task
 * cannot miss a close. */
static volatile bool _cpm_stop_requested = false;

/* How long the ESP tasks wait on a queue before looking at the stop flag */
#define CPM_QUEUE_SLICE_MS 100

/* -------------------------------------------------------------------------
 * Hardware stubs (required by RunCPM cpu.h)
 * ------------------------------------------------------------------------- */
static void _HardwareOut(const uint32 Port, const uint32 Value) { (void)Port; (void)Value; }
static uint32 _HardwareIn(const uint32 Port) { (void)Port; return 0; }

/* -------------------------------------------------------------------------
 * Console abstraction — bridges RunCPM I/O to the per-TU queues
 * ------------------------------------------------------------------------- */

static int _kbhit(void)
{
#ifdef ESP_PLATFORM
    if (_cpm_txq == nullptr) return 0;
    return (int)uxQueueMessagesWaiting(_cpm_txq);
#else
    std::lock_guard<std::mutex> lk(_cpm_txmtx);
    return (int)_cpm_txq.size();
#endif
}

static uint8_t _getch(void)
{
    uint8_t c = 0;
#ifdef ESP_PLATFORM
    while (_cpm_txq != nullptr && !_cpm_stop_requested)
    {
        if (xQueueReceive(_cpm_txq, &c, pdMS_TO_TICKS(CPM_QUEUE_SLICE_MS)) == pdTRUE)
            return c;
    }
    /* closing: a CTRL-C gets RunCPM out of whatever it was waiting for */
    return 0x03;
#else
    std::unique_lock<std::mutex> lk(_cpm_txmtx);
    _cpm_txcv.wait(lk, [] { return !_cpm_txq.empty() || _cpm_stop_requested; });
    if (_cpm_txq.empty())
        return 0x03;
    c = _cpm_txq.front();
    _cpm_txq.pop();
    return c;
#endif
}

static void _putch(uint8_t ch);

static uint8_t _getche(void)
{
    uint8_t c = _getch();
    /* echo back through rxq so the terminal sees it */
    _putch(c);
    return c;
}

static void _putch(uint8_t ch)
{
#ifdef ESP_PLATFORM
    /* If nobody is reading (the host hung up), give up on the byte once a
     * stop is requested instead of blocking the task forever. */
    while (_cpm_rxq != nullptr && !_cpm_stop_requested)
    {
        if (xQueueSend(_cpm_rxq, &ch, pdMS_TO_TICKS(CPM_QUEUE_SLICE_MS)) == pdTRUE)
            break;
    }
#else
    {
        std::lock_guard<std::mutex> lk(_cpm_rxmtx);
        _cpm_rxq.push(ch);
    }
#endif
}

static void _clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    _putch(0x1B); _putch('['); _putch('1'); _putch(';');
    _putch('1');  _putch('H'); _putch(0x1B); _putch('[');
    _putch('2');  _putch('J');
}

#endif /* ABSTRACTION_NETWORK_PROTOCOL_H */
