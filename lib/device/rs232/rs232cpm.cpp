#ifdef BUILD_RS232

#define CCP_INTERNAL

#include "rs232cpm.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnWiFi.h"
#include "fuji.h"
#include "fnFS.h"
#include "fnFsSD.h"

#include "../runcpm/globals.h"
#include "../runcpm/abstraction_fujinet.h"
#include "../runcpm/ram.h"     // ram.h - Implements the RAM
#include "../runcpm/console.h" // console.h - implements console.
#include "../runcpm/cpu.h"     // cpu.h - Implements the emulated CPU
#include "../runcpm/disk.h"    // disk.h - Defines all the disk access abstraction functions
#include "../runcpm/host.h"    // host.h - Custom host-specific BDOS call
#include "../runcpm/cpm.h"     // cpm.h - Defines the CPM structures and calls
#ifdef CCP_INTERNAL
# include "../runcpm/ccp.h" // ccp.h - Defines a simple internal CCP
#endif


void rs232CPM::rs232_status()
{
    // Nothing to do here
    return;
}

void rs232CPM::rs232_handle_cpm()
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
        free(RAM);
    }
}

void rs232CPM::init_cpm(int baud)
{
    fnUartBUS.set_baudrate(baud);
    Status = Debug = 0;
    Break = Step = -1;
    RAM = (uint8_t *)malloc(MEMSIZE);
    memset(RAM, 0, MEMSIZE);
    memset(filename, 0, sizeof(filename));
    memset(newname, 0, sizeof(newname));
    memset(fcbname, 0, sizeof(fcbname));
    memset(pattern, 0, sizeof(pattern));
}

void rs232CPM::rs232_process(cmdFrame_t *cmd_ptr)
{
    cmdFrame = *cmd_ptr;
    switch (cmdFrame.comnd)
    {
    case 'G':
        rs232_ack();
        fnSystem.delay(10);
        rs232_complete();
        fnSystem.delay(5000);
        init_cpm(9600);
        cpmActive = true;
        break;
    default:
        rs232_nak();
        break;
    }
}

#endif /* BUILD_RS232 */
