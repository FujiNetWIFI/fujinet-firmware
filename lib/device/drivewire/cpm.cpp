#ifdef BUILD_COCO

#ifdef ESP_PLATFORM

/*
 * drivewire/cpm.cpp — CoCo DriveWire CP/M transport: a thin console back-end
 * for the shared RunCPM core (lib/runcpm/runcpm_core.cpp). Supplies a
 * queue-based console over the DriveWire rx/tx queues and runs a session.
 */

#include "cpm.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fujiDevice.h"
#include "fnFS.h"
#include "fnFsSD.h"

#include "../runcpm/runcpm_session.h"

/*
 * DriveWire console queues (this TU owns them).
 *   rxq : CP/M stdout -> host read  (putch pushes; read() pops)
 *   txq : host write  -> CP/M stdin (write() pushes; getch pops)
 */
static QueueHandle_t rxq = nullptr;
static QueueHandle_t txq = nullptr;

/* ----- console back-end (runcpm_console_ops) ----- */

static int cpm_kbhit(void)
{
    return txq ? (int)uxQueueMessagesWaiting(txq) : 0;
}

static int cpm_getch(void)
{
    uint8_t c = 0;
    if (txq)
        xQueueReceive(txq, &c, portMAX_DELAY);
    return c;
}

static int cpm_getche(void)
{
    uint8_t c = (uint8_t)cpm_getch();
    if (rxq)
        xQueueSend(rxq, &c, portMAX_DELAY);
    return c;
}

static void cpm_putch(uint8_t ch)
{
    if (rxq)
        xQueueSend(rxq, &ch, portMAX_DELAY);
}

static void cpm_clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    static const uint8_t seq[] = {0x1B, '[', '1', ';', '1', 'H',
                                  0x1B, '[', '2', 'J'};
    for (size_t i = 0; i < sizeof(seq); i++)
        cpm_putch(seq[i]);
}

static const runcpm_console_ops cpm_console_ops = {
    cpm_getch,
    cpm_getche,
    cpm_kbhit,
    cpm_putch,
    cpm_clrscr,
};

static void cpmTask(void *arg)
{
    (void)arg;
    Debug_printf("cpmTask()\n");
    // The shared core owns the CCP + warm-boot loop; keep re-running it so the
    // DriveWire console behaves like the original always-on task.
    while (1)
        runcpm_session_run(&cpm_console_ops);
}

drivewireCPM::drivewireCPM()
{
    rxq = xQueueCreate(2048, sizeof(char));
    txq = xQueueCreate(2048, sizeof(char));
}

// drivewireCPM::~drivewireCPM()
// {
//     if (cpmTaskHandle != NULL)
//     {
//         vTaskDelete(cpmTaskHandle);
//     }

//     vQueueDelete(rxq);
//     vQueueDelete(txq);
// }

void drivewireCPM::ready()
{
    SYSTEM_BUS.write(0x01);
}

void drivewireCPM::send_response()
{
    // Send body
    SYSTEM_BUS.write((uint8_t *)response.c_str(),response.length());

    // Clear the response
    response.clear();
    response.shrink_to_fit();
}

void drivewireCPM::boot()
{
#ifdef ESP_PLATFORM
    if (cpmTaskHandle != NULL)
    {
        vTaskDelete(cpmTaskHandle);
        cpmTaskHandle = NULL;
    }

    xTaskCreatePinnedToCore(cpmTask, "cpmtask", 32768, NULL, 20, &cpmTaskHandle, 1);
#endif /* ESP_PLATFORM */
}

void drivewireCPM::read()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = (lenh * 256) + lenl;
    uint16_t mw = uxQueueMessagesWaiting(rxq);

    if (!len)
        return;

    if (!mw)
        return;

    response.clear();
    response.shrink_to_fit();

    for (uint16_t i=0; i<len; i++)
    {
        char b;

#ifdef ESP_PLATFORM
        xQueueReceive(rxq, &b, portMAX_DELAY);
#endif /* ESP_PLATFORM */
        response += b;
    }
}

void drivewireCPM::write()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = (lenh * 256) + lenl;

    if (!len)
        return;

    for (uint16_t i=0;i<len;i++)
    {
        char b = SYSTEM_BUS.read();
#ifdef ESP_PLATFORM
        xQueueSend(txq, &b, portMAX_DELAY);
#endif /* ESP_PLATFORM */
    }
}

void drivewireCPM::status()
{
    unsigned short mw = uxQueueMessagesWaiting(rxq);
    unsigned char status_response[2] = {0,0};

    response.clear();
    response.shrink_to_fit();

    status_response[0] = mw >> 8;
    status_response[1] = mw & 0xFF;

    response = std::string((const char *)status_response, 2);
}

void drivewireCPM::process()
{
    uint8_t cmd = SYSTEM_BUS.read();

    switch(cmd)
    {
        case 0x00:
            ready();
            break;
        case 0x01:
            send_response();
            break;
        case 'B':
            boot();
            break;
        case 'R':
            read();
            break;
        case 'W':
            write();
            break;
        case 'S':
            status();
            break;
    }
}

drivewireCPM theCPM;

#endif /* ESP_PLATFORM */

#endif /* BUILD_COCO */
