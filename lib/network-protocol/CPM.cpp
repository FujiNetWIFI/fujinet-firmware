/**
 * NetworkProtocolCPM — CP/M emulator as a network protocol adapter.
 *
 * This TU no longer includes the RunCPM header chain.  The Z80/CP/M engine +
 * state live once in lib/runcpm/runcpm_core.cpp; this adapter is a thin console
 * back-end.  It owns its own stdin/stdout queues, exposes them to the shared
 * core as a runcpm_console_ops, and asks the core to run a session:
 *   _cpm_txq : host write() -> CPM stdin   (write() pushes; getch pops)
 *   _cpm_rxq : CPM stdout  -> host read()  (putch pushes; read() pops)
 */

#include "CPM.h"
#include "../../include/debug.h"
#include "status_error_codes.h"

#include "../runcpm/runcpm_session.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include <queue>
#include <mutex>
#include <condition_variable>
#endif

/* -------------------------------------------------------------------------
 * Console queue storage (this TU owns the N:CPM:// console queues).
 *
 * _cpm_txq : host write() -> CPM stdin   (write() pushes; getch pops)
 * _cpm_rxq : CPM stdout  -> host read()  (putch pushes; read() pops)
 * ------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
static QueueHandle_t _cpm_rxq = nullptr;
static QueueHandle_t _cpm_txq = nullptr;
#else
static std::queue<uint8_t> _cpm_rxq;
static std::queue<uint8_t> _cpm_txq;
static std::mutex _cpm_rxmtx;
static std::mutex _cpm_txmtx;
static std::condition_variable _cpm_txcv;
#endif

/* Set by _cpm_run() when the session ends (CCP exited of its own accord, e.g.
 * user typed EXIT, or stopCPM() requested teardown).  Polled by status() to
 * drive EOF back to the host. */
static volatile bool _cpm_session_ended = false;

/* -------------------------------------------------------------------------
 * Console back-end (runcpm_console_ops) — bridges the core's console I/O to
 * the per-TU queues.
 * ------------------------------------------------------------------------- */
static int _cpm_kbhit(void)
{
#ifdef ESP_PLATFORM
    if (_cpm_txq == nullptr) return 0;
    return (int)uxQueueMessagesWaiting(_cpm_txq);
#else
    std::lock_guard<std::mutex> lk(_cpm_txmtx);
    return (int)_cpm_txq.size();
#endif
}

static int _cpm_getch(void)
{
    uint8_t c = 0;
#ifdef ESP_PLATFORM
    if (_cpm_txq != nullptr)
        xQueueReceive(_cpm_txq, &c, portMAX_DELAY);
#else
    std::unique_lock<std::mutex> lk(_cpm_txmtx);
    _cpm_txcv.wait(lk, [] { return !_cpm_txq.empty(); });
    c = _cpm_txq.front();
    _cpm_txq.pop();
#endif
    return c;
}

static void _cpm_push_rx(uint8_t c)
{
#ifdef ESP_PLATFORM
    if (_cpm_rxq != nullptr)
        xQueueSend(_cpm_rxq, &c, portMAX_DELAY);
#else
    {
        std::lock_guard<std::mutex> lk(_cpm_rxmtx);
        _cpm_rxq.push(c);
    }
#endif
}

static int _cpm_getche(void)
{
    uint8_t c = (uint8_t)_cpm_getch();
    /* echo back through rxq so the terminal sees it */
    _cpm_push_rx(c);
    return c;
}

static void _cpm_putch(uint8_t ch)
{
    _cpm_push_rx(ch);
}

static void _cpm_clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    _cpm_push_rx(0x1B); _cpm_push_rx('['); _cpm_push_rx('1'); _cpm_push_rx(';');
    _cpm_push_rx('1');  _cpm_push_rx('H'); _cpm_push_rx(0x1B); _cpm_push_rx('[');
    _cpm_push_rx('2');  _cpm_push_rx('J');
}

static const runcpm_console_ops cpm_console_ops = {
    _cpm_getch,
    _cpm_getche,
    _cpm_kbhit,
    _cpm_putch,
    _cpm_clrscr,
};

/* -------------------------------------------------------------------------
 * CPM task / thread entry point — run a full session on the shared core, then
 * signal end.  The core's runcpm_session_run() owns the CCP + warm-boot loop.
 * ------------------------------------------------------------------------- */
static void _cpm_run(void)
{
    runcpm_session_run(&cpm_console_ops);

    /* Notify the protocol object that the session ended from the CP/M side. */
    _cpm_session_ended = true;
}

#ifdef ESP_PLATFORM
static void _cpm_task_entry(void *arg)
{
    (void)arg;
    _cpm_run();
    vTaskDelete(nullptr);
}
#endif

/* =========================================================================
 * NetworkProtocolCPM implementation
 * ========================================================================= */

NetworkProtocolCPM::NetworkProtocolCPM(std::string *rx_buf,
                                       std::string *tx_buf,
                                       std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolCPM::ctor\r\n");
}

NetworkProtocolCPM::~NetworkProtocolCPM()
{
    Debug_printf("NetworkProtocolCPM::dtor\r\n");
    if (running)
        stopCPM();
}

fujiError_t NetworkProtocolCPM::open(PeoplesUrlParser *urlParser,
                                     fileAccessMode_t access,
                                     netProtoTranslation_t translate)
{
    Debug_printf("NetworkProtocolCPM::open\r\n");

    if (running)
        stopCPM();

    _cpm_session_ended = false;

#ifdef ESP_PLATFORM
    _cpm_rxq = xQueueCreate(2048, sizeof(uint8_t));
    _cpm_txq = xQueueCreate(2048, sizeof(uint8_t));
    xTaskCreatePinnedToCore(_cpm_task_entry,
                            "cpmnet",
#ifdef BUILD_APPLE
                            4096,
#else
                            32768,
#endif
                            nullptr,
                            20,
                            &cpmTaskHandle,
                            1);
#else
    cpmStopped.store(false);
    cpmThread = std::thread(_cpm_run);
#endif

    running = true;
    NetworkProtocol::open(urlParser, access, translate);
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCPM::close()
{
    Debug_printf("NetworkProtocolCPM::close\r\n");
    NetworkProtocol::close();
    stopCPM();
    return FUJI_ERROR::NONE;
}

void NetworkProtocolCPM::stopCPM()
{
    if (!running) return;
    running = false;

    /* Ask the shared core's CCP to stop re-booting and end the session. */
    runcpm_session_request_exit();

    /* Unblock any getch() call waiting for user input */
    uint8_t sentinel = 0x03;  // CTRL-C
#ifdef ESP_PLATFORM
    if (_cpm_txq != nullptr)
        xQueueSend(_cpm_txq, &sentinel, pdMS_TO_TICKS(200));

    /* Give the FreeRTOS task time to notice the exit request and self-delete */
    vTaskDelay(pdMS_TO_TICKS(300));

    if (_cpm_rxq != nullptr) { vQueueDelete(_cpm_rxq); _cpm_rxq = nullptr; }
    if (_cpm_txq != nullptr) { vQueueDelete(_cpm_txq); _cpm_txq = nullptr; }
    cpmTaskHandle = nullptr;
#else
    {
        std::lock_guard<std::mutex> lk(_cpm_txmtx);
        _cpm_txq.push(sentinel);
        _cpm_txcv.notify_all();
    }
    if (cpmThread.joinable())
        cpmThread.join();
    cpmStopped.store(true);
#endif
}

/**
 * read() — drain up to `len` bytes from CPM stdout into receiveBuffer.
 */
fujiError_t NetworkProtocolCPM::read(unsigned short len)
{
    if (receiveBuffer->length() == 0)
    {
        unsigned short collected = 0;
        uint8_t ch;

        while (collected < len)
        {
#ifdef ESP_PLATFORM
            if (_cpm_rxq == nullptr) break;
            if (xQueueReceive(_cpm_rxq, &ch, pdMS_TO_TICKS(10)) != pdTRUE)
                break;
#else
            {
                std::lock_guard<std::mutex> lk(_cpm_rxmtx);
                if (_cpm_rxq.empty()) break;
                ch = _cpm_rxq.front();
                _cpm_rxq.pop();
            }
#endif
            receiveBuffer->push_back((char)ch);
            ++collected;
        }

        if (collected == 0)
        {
            error = NDEV_STATUS::SOCKET_TIMEOUT;
            return FUJI_ERROR::UNSPECIFIED;
        }
    }

    error = NDEV_STATUS::SUCCESS;
    return NetworkProtocol::read(len);
}

/**
 * write() — feed bytes from transmitBuffer into CPM stdin.
 */
fujiError_t NetworkProtocolCPM::write(unsigned short len)
{
    if (!running)
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    len = translate_transmit_buffer();

    for (unsigned short i = 0; i < len; ++i)
    {
        uint8_t ch = (uint8_t)(*transmitBuffer)[i];
#ifdef ESP_PLATFORM
        if (_cpm_txq == nullptr)
        {
            error = NDEV_STATUS::NOT_CONNECTED;
            return FUJI_ERROR::UNSPECIFIED;
        }
        xQueueSend(_cpm_txq, &ch, portMAX_DELAY);
#else
        {
            std::lock_guard<std::mutex> lk(_cpm_txmtx);
            _cpm_txq.push(ch);
        }
        _cpm_txcv.notify_one();
#endif
    }

    transmitBuffer->erase(0, len);
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCPM::status(NetworkStatus *status)
{
    /* If the CCP exited on its own, clean up and report EOF to the host. */
    if (_cpm_session_ended && running)
        stopCPM();

    status->connected = running ? 1 : 0;
    status->error     = running ? error : NDEV_STATUS::END_OF_FILE;
    NetworkProtocol::status(status);
    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolCPM::available()
{
    size_t avail = receiveBuffer->size();
    if (!avail)
    {
#ifdef ESP_PLATFORM
        if (_cpm_rxq != nullptr)
            avail = (size_t)uxQueueMessagesWaiting(_cpm_rxq);
#else
        std::lock_guard<std::mutex> lk(_cpm_rxmtx);
        avail = _cpm_rxq.size();
#endif
    }
    return avail;
}
