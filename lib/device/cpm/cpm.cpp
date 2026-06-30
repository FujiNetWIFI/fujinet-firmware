#include "cpm.h"

#include <cstring>

// ---------------------------------------------------------------------------
// cpmDevice: drive the single shared engine through this device's endpoint.
// ---------------------------------------------------------------------------

cpmDevice *cpmDevice::s_active = nullptr;

int cpmDevice::s_kbhit()
{
    return s_active ? s_active->ep_kbhit() : 0;
}

uint8_t cpmDevice::s_getch()
{
    return s_active ? s_active->ep_getch() : 0x03;
}

void cpmDevice::s_putch(uint8_t c)
{
    if (s_active)
        s_active->ep_putch(c);
}

void cpmDevice::s_clrscr()
{
    if (s_active)
        s_active->ep_clrscr();
}

void cpmDevice::handle_cpm()
{
    s_active = this;

    runcpm_console_ops ops;
    ops.kbhit  = &cpmDevice::s_kbhit;
    ops.getch  = &cpmDevice::s_getch;
    ops.putch  = &cpmDevice::s_putch;
    ops.clrscr = &cpmDevice::s_clrscr;

    runcpm_session_run(&ops);

    cpmActive = false;
    s_active = nullptr;
}

// ---------------------------------------------------------------------------
// cpmQueueDevice: byte queues between the bus task and the engine task.
// ---------------------------------------------------------------------------

#ifdef ESP_PLATFORM

cpmQueueDevice::cpmQueueDevice()
{
    rxq = xQueueCreate(2048, sizeof(uint8_t));
    txq = xQueueCreate(2048, sizeof(uint8_t));
}

cpmQueueDevice::~cpmQueueDevice()
{
    if (rxq)
        vQueueDelete(rxq);
    if (txq)
        vQueueDelete(txq);
}

size_t cpmQueueDevice::host_available()
{
    return uxQueueMessagesWaiting(rxq);
}

size_t cpmQueueDevice::host_read(uint8_t *buf, size_t max)
{
    size_t n = 0;
    while (n < max && xQueueReceive(rxq, &buf[n], 0) == pdTRUE)
        n++;
    return n;
}

void cpmQueueDevice::host_write(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        xQueueSend(txq, &buf[i], portMAX_DELAY);
}

int cpmQueueDevice::ep_kbhit()
{
    return uxQueueMessagesWaiting(txq) ? 1 : 0;
}

uint8_t cpmQueueDevice::ep_getch()
{
    uint8_t c = 0;
    xQueueReceive(txq, &c, portMAX_DELAY);
    return c;
}

void cpmQueueDevice::ep_putch(uint8_t c)
{
    xQueueSend(rxq, &c, portMAX_DELAY);
}

#else // !ESP_PLATFORM

cpmQueueDevice::cpmQueueDevice() = default;
cpmQueueDevice::~cpmQueueDevice() = default;

size_t cpmQueueDevice::host_available()
{
    std::lock_guard<std::mutex> lock(rxmtx);
    return rxq.size();
}

size_t cpmQueueDevice::host_read(uint8_t *buf, size_t max)
{
    std::lock_guard<std::mutex> lock(rxmtx);
    size_t n = 0;
    while (n < max && !rxq.empty())
    {
        buf[n++] = rxq.front();
        rxq.pop();
    }
    return n;
}

void cpmQueueDevice::host_write(const uint8_t *buf, size_t len)
{
    {
        std::lock_guard<std::mutex> lock(txmtx);
        for (size_t i = 0; i < len; i++)
            txq.push(buf[i]);
    }
    txcv.notify_one();
}

int cpmQueueDevice::ep_kbhit()
{
    std::lock_guard<std::mutex> lock(txmtx);
    return txq.empty() ? 0 : 1;
}

uint8_t cpmQueueDevice::ep_getch()
{
    std::unique_lock<std::mutex> lock(txmtx);
    txcv.wait(lock, [this] { return !txq.empty(); });
    uint8_t c = txq.front();
    txq.pop();
    return c;
}

void cpmQueueDevice::ep_putch(uint8_t c)
{
    std::lock_guard<std::mutex> lock(rxmtx);
    rxq.push(c);
}

#endif // ESP_PLATFORM

void cpmQueueDevice::ep_clrscr()
{
    static const uint8_t seq[] = {0x1B, '[', '1', ';', '1', 'H', 0x1B, '[', '2', 'J'};
    for (uint8_t c : seq)
        ep_putch(c);
}
