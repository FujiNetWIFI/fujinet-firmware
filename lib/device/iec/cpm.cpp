#ifdef BUILD_IEC

/**
 * CP/M for IEC
 */

#include "cpm.h"
#include <cstring>
#include <algorithm>

#include "../../include/debug.h"
#include "../../hardware/led.h"

#include "utils.h"

#include "../runcpm/abstraction_fujinet_apple2.h"

#include "../runcpm/globals.h"
#include "../runcpm/ram.h"     // ram.h - Implements the RAM
#include "../runcpm/console.h" // console.h - implements console.
#include "../runcpm/cpu.h"     // cpu.h - Implements the emulated CPU
#include "../runcpm/disk.h"    // disk.h - Defines all the disk access abstraction functions
#include "../runcpm/host.h"    // host.h - Custom host-specific BDOS call
#include "../runcpm/cpm.h"     // cpm.h - Defines the CPM structures and calls
#ifdef CCP_INTERNAL
#include "../runcpm/ccp.h" // ccp.h - Defines a simple internal CCP
#endif

#define CPM_TASK_PRIORITY 20

static void cpmTask(void *arg)
{
    Debug_printf("cpmTask()\r\n");
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
        vTaskDelay(100);
        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();
    }
}

iecCpm::iecCpm()
{
    rxq = xQueueCreate(2048, sizeof(char));
    txq = xQueueCreate(2048, sizeof(char));
}

iecCpm::~iecCpm()
{
    vQueueDelete(rxq);
    vQueueDelete(txq);
}

void iecCpm::iec_open()
{
    if (cpmTaskHandle != NULL)
        iec_close(); // Close task and restart.

    xTaskCreatePinnedToCore(cpmTask, "cpmtask", 32768, NULL, CPM_TASK_PRIORITY, &cpmTaskHandle, 1);
}

void iecCpm::iec_close()
{
    if (cpmTaskHandle != NULL)
        vTaskDelete(cpmTaskHandle);

    commanddata.init();
    device_state = DEVICE_IDLE;
    Debug_printv("device init");
}

void iecCpm::poll_interrupt(unsigned char c)
{    
    if (uxQueueMessagesWaiting(rxq))
        IEC.assert_interrupt();
}

void iecCpm::iec_reopen_talk()
{
    bool set_eoi = false;
    bool atn = true; // inverted

    if (cpmTaskHandle == NULL)
    {
        Debug_printf("iecCpm::iec_reopen_talk() - No CP/M task, ignoring.\r\n");
        return;
    }

    while (atn)
    {
        char b;
        atn = fnSystem.digital_read(PIN_IEC_ATN);

        if (!uxQueueMessagesWaiting(rxq))
        {
            IEC.senderTimeout();
            break;
        }

        xQueueReceive(rxq,&b,portMAX_DELAY);
        IEC.sendByte(b, set_eoi);
        atn = fnSystem.digital_read(PIN_IEC_ATN);
    }
}

void iecCpm::iec_reopen_listen()
{
    if (cpmTaskHandle == NULL)
    {
        Debug_printf("iecCpm::iec_reopen_talk() - No CP/M task, ignoring.\r\n");
        return;
    }

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        if (b<0)
        {
            Debug_printf("Error on receive.\r\n");
            return;
        }

        xQueueSend(txq,&b,portMAX_DELAY);
    }
}

void iecCpm::iec_reopen()
{
    switch (commanddata.primary)
    {
    case IEC_TALK:
        iec_reopen_talk();
        break;
    case IEC_LISTEN:
        iec_reopen_listen();
        break;
    }
}

device_state_t iecCpm::process()
{
    // Call base class
    virtualDevice::process(); // commanddata set here.

    switch (commanddata.secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN:
        iec_reopen();
        break;
    default:
        break;
    }

    return device_state;
}

#endif /* BUILD_IEC */