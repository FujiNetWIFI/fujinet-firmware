#ifdef BUILD_RC2014

#include "rc2014cpm.h"

#include "fnSystem.h"

void rc2014CPM::rc2014_status()
{
    // Nothing to do here
    return;
}

void rc2014CPM::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case CPMCMD_INIT:
        rc2014_send_ack();
        fnSystem.delay(10);
        rc2014_send_complete();
        fnSystem.delay(5000);
        // No CP/M console is wired to the RC2014 SPI bus yet, so the base
        // class's default endpoint makes the session exit cleanly instead of
        // hanging on the first console read.
        init_cpm(115200);
        cpmActive = true;
        break;
    default:
        rc2014_send_nak();
        break;
    }
}

#endif /* BUILD_RC2014 */
