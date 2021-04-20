#include "iecApeTime.h"

void iecApeTime::_process(void)
{
    Debug_println("APETIME time query");

    uint8_t _reply[6] = { 0 };

    time_t tt = time(nullptr);
    struct tm * now = localtime(&tt);

    now->tm_mon++;
    now->tm_year-=100;

    _reply[0] = now->tm_mday;
    _reply[1] = now->tm_mon;
    _reply[2] = now->tm_year;
    _reply[3] = now->tm_hour;
    _reply[4] = now->tm_min;
    _reply[5] = now->tm_sec;

    iec_to_computer(_reply, sizeof(_reply), false);
}
