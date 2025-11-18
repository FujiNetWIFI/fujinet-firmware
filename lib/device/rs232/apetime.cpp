#ifdef BUILD_RS232

#include "apetime.h"
#include "fujiCommandID.h"

#include <cstring>
#include <ctime>

#include "../../include/debug.h"

void rs232ApeTime::_rs232_get_time(bool use_timezone)
{
    char old_tz[64];

    if (use_timezone) {
      Debug_println("APETIME time query (timezone)");
    } else {
      Debug_println("APETIME time query (classic)");
    }

    uint8_t rs232_reply[6] = { 0 };

    time_t tt = time(nullptr);

    if (ape_timezone.size() && use_timezone) {
        Debug_printf("Using time zone %s\n", ape_timezone);
        strncpy(old_tz, getenv("TZ"), sizeof(old_tz));
        setenv("TZ", ape_timezone.c_str(), 1);
        tzset();
    }

    struct tm * now = localtime(&tt);

    if (ape_timezone.size() && use_timezone) {
        setenv("TZ", old_tz, 1);
        tzset();
    }

    now->tm_mon++;
    now->tm_year-=100;

    rs232_reply[0] = now->tm_mday;
    rs232_reply[1] = now->tm_mon;
    rs232_reply[2] = now->tm_year;
    rs232_reply[3] = now->tm_hour;
    rs232_reply[4] = now->tm_min;
    rs232_reply[5] = now->tm_sec;

    Debug_printf("Returning %02d/%02d/%02d %02d:%02d:%02d\n", now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);

    bus_to_computer(rs232_reply, sizeof(rs232_reply), false);
}

void rs232ApeTime::_rs232_set_tz(std::string newTZ)
{
    if (newTZ.size())
    {
        ape_timezone = newTZ;
        Debug_printf("TZ set to <%s>\n", ape_timezone.c_str());
    }
    else
      Debug_printf("TZ unset\n");
    return;
}

void rs232ApeTime::rs232_process(FujiBusPacket &packet)
{
    switch (packet.command())
    {
    case FUJICMD_GETTIME:
        rs232_ack();
        _rs232_get_time(false);
        break;
    case FUJICMD_SETTZ:
        rs232_ack();
        _rs232_set_tz(packet.data_as_string().value_or(""));
        break;
    case FUJICMD_GETTZTIME:
        rs232_ack();
        _rs232_get_time(true);
        break;
    default:
        rs232_nak();
        break;
    };
}
#endif /* BUILD_RS232 */
