#include "apetime.h"

#define SIO_APETIMECMD_GETTIME 0x93

void sioApeTime::_sio_get_time()
{
    Debug_println("APETIME time query");

    uint8_t sio_reply[6] = { 0 };

    time_t tt = time(nullptr);
    struct tm * now = localtime(&tt);

    now->tm_mon++;
    now->tm_year-=100;

    sio_reply[0] = now->tm_mday;
    sio_reply[1] = now->tm_mon;
    sio_reply[2] = now->tm_year;
    sio_reply[3] = now->tm_hour;
    sio_reply[4] = now->tm_min;
    sio_reply[5] = now->tm_sec;

    sio_to_computer(sio_reply, sizeof(sio_reply), false);
}

void sioApeTime::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    switch (cmdFrame.comnd)
    {
    case SIO_APETIMECMD_GETTIME:
        sio_ack();
        _sio_get_time();
        break;
    default:
        sio_nak();
        break;
    };
}
