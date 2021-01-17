#include "siocpm.h"
#include "../hardware/fnUART.h"
#include "../hardware/fnSystem.h"
#include "../runcpm/abstraction_fujinet.h"
#include "../runcpm/ccp.h"
#include "../runcpm/console.h"
#include "../runcpm/cpu.h"
#include "../runcpm/cpm.h"
#include "../runcpm/globals.h"
#include "../runcpm/disk.h"
#include "../runcpm/host.h"
#include <string.h>

void sioCPM::sio_status()
{
    // Nothing to do here
    return;
}

void sioCPM::sio_handle_cpm()
{
    _puts(CCPHEAD);
    _PatchCPM();
    Status = 0;
#ifdef CCP_INTERNAL
    _ccp();
#else
    if (!_sys_exists((uint8 *)CCPname))
    {
        _puts("Unable to load CP/M CCP.\r\nCPU halted.\r\n");
        break;
    }
    _RamLoad((uint8 *)CCPname, CCPaddr);    // Loads the CCP binary file into memory
    Z80reset();                             // Resets the Z80 CPU
    SET_LOW_REGISTER(BC, _RamRead(0x0004)); // Sets C to the current drive/user
    PC = CCPaddr;                           // Sets CP/M application jump point
    Z80run();                               // Starts simulation
#endif
    if (Status == 1) // This is set by a call to BIOS 0 - ends CP/M
    {
        cpmActive = false;
        //free(RAM);
    }
}

void sioCPM::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case 'G':
        sio_ack();
        fnSystem.delay(10);
        sio_complete();
        fnUartSIO.set_baudrate(9600);
        Status = Debug = 0;
        Break = Step = -1;
        //RAM = (uint8_t *)malloc(MEMSIZE);
        memset(RAM,0,MEMSIZE);
        memset(filename,0,sizeof(filename));
        memset(newname,0,sizeof(newname));
        memset(fcbname,0,sizeof(fcbname));
        memset(pattern,0,sizeof(pattern));
        fnSystem.delay(5000);
        cpmActive = true;
        break;
    default:
        sio_nak();
        break;
    }
}