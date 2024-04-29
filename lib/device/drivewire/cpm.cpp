#ifdef BUILD_COCO

#define CCP_INTERNAL

#include "cpm.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnWiFi.h"
#include "fuji.h"
#include "fnFS.h"
#include "fnFsSD.h"

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
    while (1)
    {
        Status = Debug = 0;
        Break = Step = -1;
        RAM = (uint8_t *)malloc(MEMSIZE);
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
}

drivewireCPM::~drivewireCPM()
{
    if (cpmTaskHandle != NULL)
    {
        vTaskDelete(cpmTaskHandle);
    }

    vQueueDelete(rxq);
    vQueueDelete(txq);
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
    uint8_t lenh = fnUartBUS.read();
    uint8_t lenl = fnUartBUS.read();
    uint16_t len = (lenh * 256) + lenl;
    uint16_t mw = uxQueueMessagesWaiting(rxq);

    if (!len)
        return;

    if (len > mw)
        return;
    
    response.clear();
    response.shrink_to_fit();

    for (uint16_t i=0; i<mw; i++)
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
    uint8_t lenh = fnUartBUS.read();
    uint8_t lenl = fnUartBUS.read();
    uint16_t len = (lenh * 256) + lenl;

    for (uint16_t i=0;i<len;i++)
    {
        char b = fnUartBUS.read();
#ifdef ESP_PLATFORM
        xQueueSend(txq, &b, portMAX_DELAY);
#endif /* ESP_PLATFORM */
    }
}

void drivewireCPM::status()
{
    unsigned short mw = uxQueueMessagesWaiting(rxq);

    response.clear();
    response.shrink_to_fit();

    response += mw << 8 & 0xFF;
    response += mw & 0xFF;
}

void drivewireCPM::process()
{
    uint8_t cmd = fnUartBUS.read();

    switch(cmd)
    {
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

#endif /* BUILD_COCO */