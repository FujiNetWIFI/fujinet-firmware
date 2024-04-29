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

}

void drivewireCPM::write()
{

}

void drivewireCPM::status()
{
    unsigned short mw = uxQueueMessagesWaiting(rxq);

    fnUartBUS.write((mw << 8) & 0xFF);
    fnUartBUS.write(mw & 0xFF);
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