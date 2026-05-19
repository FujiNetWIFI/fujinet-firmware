/**
 * NetworkProtocolCPM — CP/M emulator as a network protocol adapter.
 *
 * IMPORTANT: RUNCPM_STATIC_IMPL must be defined before any RunCPM header so
 * that every RunCPM symbol in this TU gets static (internal) linkage.  This
 * lets the network-protocol CPM adapter and any bus-device CPM adapter (sio,
 * drivewire, iwm, …) coexist in the same binary without linker conflicts.
 */

#define RUNCPM_STATIC_IMPL
#define CCP_INTERNAL

#include "CPM.h"
#include "../../include/debug.h"
#include "status_error_codes.h"

/* The network-protocol abstraction defines the static queue variables
 * (_cpm_rxq / _cpm_txq) and the _kbhit / _getch / _putch / _clrscr
 * functions.  It must come before the RunCPM headers that use them. */
#include "../runcpm/abstraction_network_protocol.h"

/* Standard RunCPM header chain */
#include "../runcpm/globals.h"
#include "../runcpm/ram.h"
#include "../runcpm/console.h"
#include "../runcpm/cpu.h"
#include "../runcpm/disk.h"
#include "../runcpm/host.h"
#include "../runcpm/cpm.h"
#include "../runcpm/ccp.h"

/* -------------------------------------------------------------------------
 * CPM task / thread entry point
 *
 * Runs the CCP in a loop.  Status == 1 is the conventional "exit" signal
 * set by the CCP itself on a warm-boot, or by stopCPM() on close().
 * The outer loop re-boots CP/M (as a real machine would on warm-boot)
 * unless a clean exit was requested.
 * ------------------------------------------------------------------------- */
static void _cpm_run(void)
{
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

        /* Status == 1: clean exit requested — stop looping */
        if (Status == 1)
            break;
        /* Status == 2: warm-boot — re-enter the CCP loop */
    }

    /* Notify the protocol object that the session ended from the CP/M side. */
    _cpm_session_ended = true;
}

#ifdef ESP_PLATFORM
static void _cpm_task_entry(void *arg)
{
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
                            32768,
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

    /* Signal the CCP to stop re-booting */
    Status = 1;

    /* Unblock any _getch() call waiting for user input */
    uint8_t sentinel = 0x03;  // CTRL-C
#ifdef ESP_PLATFORM
    if (_cpm_txq != nullptr)
        xQueueSend(_cpm_txq, &sentinel, pdMS_TO_TICKS(200));

    /* Give the FreeRTOS task time to notice Status==1 and self-delete */
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
