#ifdef BUILD_COCO

#ifdef ESP_PLATFORM

#define CCP_INTERNAL

#include "cpm.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fujiDevice.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fujiCommandID.h"

#include "../runcpm/globals.h"
#include "../runcpm/abstraction_fujinet_apple2.h"
#include "../runcpm/ram.h"     // ram.h - Implements the RAM
#include "../runcpm/console.h" // console.h - implements console.
#include "../runcpm/cpu.h"     // cpu.h - Implements the emulated CPU
#include "../runcpm/disk.h"    // disk.h - Defines all the disk access abstraction functions
#include "../runcpm/host.h"    // host.h - Custom host-specific BDOS call
#include "../runcpm/cpm.h"     // cpm.h - Defines the CPM structures and calls
#ifdef CCP_INTERNAL
# include "../runcpm/ccp.h" // ccp.h - Defines a simple internal CCP
#endif

static void cpmTask(void *arg)
{
    Debug_printf("cpmTask()\n");
    // RAM survives CCP restarts and boot()'s task kill+recreate; allocating per
    // iteration leaked 64K each pass.
    if (RAM == NULL)
        RAM = (uint8_t *)malloc(MEMSIZE);
    if (RAM == NULL)
    {
        Debug_printv("could not allocate 64K CP/M RAM, free heap: %lu", fnSystem.get_free_heap_size());
        while (true)
            fnSystem.delay(1000);
    }
    while (1)
    {
        Status = Debug = 0;
        Break = Step = -1;
        memset(RAM, 0, MEMSIZE);
        memset(filename, 0, sizeof(filename));
        memset(newname, 0, sizeof(newname));
        memset(fcbname, 0, sizeof(fcbname));
        memset(pattern, 0, sizeof(pattern));
#ifdef ESP_PLATFORM // OS
        vTaskDelay(100);
#endif
        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();
    }
}

drivewireCPM::drivewireCPM()
{
    rxq = xQueueCreate(2048, sizeof(char));
    txq = xQueueCreate(2048, sizeof(char));
    if (rxq == nullptr || txq == nullptr)
    {
        Debug_printv("could not create CP/M queues, free internal/total heap: %lu/%lu",
                     esp_get_free_internal_heap_size(), esp_get_free_heap_size());
        if (rxq != nullptr)
            vQueueDelete(rxq);
        if (txq != nullptr)
            vQueueDelete(txq);
        rxq = txq = nullptr;
    }
}

void drivewireCPM::boot()
{
#ifdef ESP_PLATFORM
    if (rxq == nullptr || txq == nullptr)
        return;

    if (cpmTaskHandle != NULL)
    {
        vTaskDelete(cpmTaskHandle);
        cpmTaskHandle = NULL;
    }

    // boot() has no error channel to the host; the CoCo just retries.
    if (xTaskCreatePinnedToCore(cpmTask, "cpmtask", 32768, NULL, 20, &cpmTaskHandle, 1) != pdPASS)
    {
        cpmTaskHandle = NULL;
        Debug_printv("could not create cpmtask (32K stack), free internal/total heap: %lu/%lu",
                     esp_get_free_internal_heap_size(), esp_get_free_heap_size());
    }
#endif /* ESP_PLATFORM */
}

void drivewireCPM::read(uint16_t len)
{
    uint16_t mw = rxq != nullptr ? uxQueueMessagesWaiting(rxq) : 0;

    if (!len)
        return;

    if (!mw)
        return;

    ByteBuffer buffer(len);
    for (uint16_t i=0; i<len; i++)
    {
        char b;

#ifdef ESP_PLATFORM
        xQueueReceive(rxq, &b, portMAX_DELAY);
#endif /* ESP_PLATFORM */
        buffer[i] = b;
    }

    SYSTEM_BUS.transaction_send(buffer);
}

void drivewireCPM::write(uint16_t len)
{
    if (!len)
        return;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    ByteBuffer data(len, 0);
    SYSTEM_BUS.transaction_get(data.data(), data.size());
#ifdef ESP_PLATFORM
    if (txq != nullptr)
        for (uint16_t i=0;i<data.size();i++)
            xQueueSend(txq, &data[i], portMAX_DELAY);
#endif /* ESP_PLATFORM */
}

void drivewireCPM::status()
{
    unsigned short mw = rxq != nullptr ? uxQueueMessagesWaiting(rxq) : 0;
    unsigned char status_response[2] = {0,0};

    status_response[0] = mw >> 8;
    status_response[1] = mw & 0xFF;

    SYSTEM_BUS.transaction_send(&status_response, sizeof(status_response));
}

bool drivewireCPM::processCommand(const FujiDWPacket &packet)
{
    switch(packet.command())
    {
    case CMD::CPM_BOOT:
        boot();
        break;
    case CMD::CPM_READ:
        read(packet.param(0));
        break;
    case CMD::CPM_WRITE:
        write(packet.param(0));
        break;
    case CMD::CPM_STATUS:
        status();
        break;
    default:
        return false;
    }

    return true;
}

drivewireCPM theCPM;

#endif /* ESP_PLATFORM */

#endif /* BUILD_COCO */
