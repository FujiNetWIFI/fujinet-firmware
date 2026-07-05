#ifdef BUILD_RS232

#include "rs232cpm.h"

#include "fnSystem.h"

void rs232CPM::rs232_status(FujiStatusReq reqType)
{
    // Nothing to do here
    return;
}

void rs232CPM::rs232_process(FujiBusPacket &packet)
{
    switch (packet.command())
    {
    case CPMCMD_INIT:
        transaction_begin(TRANS_STATE::NO_GET);
        fnSystem.delay(10);
        transaction_complete();
        fnSystem.delay(5000);
        // No CP/M console is wired to the RS232 bus yet, so the base class's
        // default endpoint makes the session exit cleanly instead of hanging
        // on the first console read.
        init_cpm(9600);
        cpmActive = true;
        break;
    default:
        transaction_error();
        break;
    }
}

#endif /* BUILD_RS232 */
