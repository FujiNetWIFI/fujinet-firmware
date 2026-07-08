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

    ByteBuffer buffer(len);
    for (uint16_t i=0; i<len; i++)
    {
        char b;

#ifdef ESP_PLATFORM
        xQueueReceive(rxq, &b, portMAX_DELAY);
#endif /* ESP_PLATFORM */
        buffer[i] = b;
    }

    transaction_put(buffer);
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

    status_response[0] = mw >> 8;
    status_response[1] = mw & 0xFF;

    transaction_put(&status_response, sizeof(status_response));
}

void drivewireCPM::process()
{
    fujiCommandID_t cmd = static_cast<fujiCommandID_t>(SYSTEM_BUS.read());

    switch(cmd)
    {
    case FUJICMD_DEVICE_READY:
        ready();
        break;
    case FUJICMD_SEND_RESPONSE:
        send_response(0);
        break;
    case CPMCMD_BOOT:
        boot();
        break;
    case CPMCMD_READ:
        read();
        break;
    case CPMCMD_WRITE:
        write();
        break;
    case CPMCMD_STATUS:
        status();
        break;
    default:
        break;
    }
}

drivewireCPM theCPM;

#endif /* ESP_PLATFORM */

#endif /* BUILD_COCO */
