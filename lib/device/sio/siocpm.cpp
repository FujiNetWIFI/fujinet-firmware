#ifdef BUILD_ATARI

#include "siocpm.h"

#include "fnSystem.h"

void sioCPM::sio_status()
{
    // Nothing to do here
    return;
}

// Console endpoint: bytes go straight out the Atari SIO link.  CP/M is a
// 7-bit world, so both directions are masked to 7 bits, exactly as the old
// _getch/_putch did.
int sioCPM::ep_kbhit()
{
    return SYSTEM_BUS.available();
}

uint8_t sioCPM::ep_getch()
{
    while (SYSTEM_BUS.available() <= 0)
    {
    }
    return SYSTEM_BUS.read() & 0x7f;
}

void sioCPM::ep_putch(uint8_t ch)
{
    SYSTEM_BUS.write(ch & 0x7f);
}

void sioCPM::init_cpm(int baud)
{
    SYSTEM_BUS.setBaudrate(baud);
}

void sioCPM::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case 'G':
        transaction_begin(TRANS_STATE::NO_GET);
        fnSystem.delay(10);
        transaction_complete();
        fnSystem.delay(5000);
        init_cpm(9600);
        cpmActive = true;
        break;
    default:
        transaction_error();
        break;
    }
}

#endif /* BUILD_ATARI */
