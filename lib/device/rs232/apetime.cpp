#ifdef BUILD_RS232

#include "apetime.h"

#include <cstring>
#include <ctime>

#include "../../include/debug.h"


#define RS232_APETIMECMD_GETTIME 0x93
#define RS232_APETIMECMD_SETTZ 0x99
#define RS232_APETIMECMD_GETTZTIME 0x9A

char * ape_timezone = NULL;

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

    if (ape_timezone != NULL && use_timezone) {
        Debug_printf("Using time zone %s\n", ape_timezone);
        strncpy(old_tz, getenv("TZ"), sizeof(old_tz));
        setenv("TZ", ape_timezone, 1);
        tzset();
    }

    struct tm * now = localtime(&tt);

    if (ape_timezone != NULL && use_timezone) {
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

void rs232ApeTime::_rs232_set_tz()
{
    int bufsz;

    Debug_println("APETIME set TZ request");

    if (ape_timezone != NULL) {
      free(ape_timezone);
    }

    bufsz = rs232_get_aux16_lo();
    if (bufsz > 0) {
      ape_timezone = (char *) malloc((bufsz + 1) * sizeof(char));

      uint8_t ck = bus_to_peripheral((uint8_t *) ape_timezone, bufsz);
      if (rs232_checksum((uint8_t *) ape_timezone, bufsz) != ck) {
        rs232_error();
      } else {
        ape_timezone[bufsz] = '\0';

        rs232_complete();

        Debug_printf("TZ set to <%s>\n", ape_timezone);
      }
    } else {
      Debug_printf("TZ unset\n");
    }
}

void rs232ApeTime::rs232_process(cmdFrame_t *cmd_ptr)
{
    cmdFrame = *cmd_ptr;
    switch (cmdFrame.comnd)
    {
    case RS232_APETIMECMD_GETTIME:
        rs232_ack();
        _rs232_get_time(false);
        break;
    case RS232_APETIMECMD_SETTZ:
        rs232_ack();
        _rs232_set_tz();
        break;
    case RS232_APETIMECMD_GETTZTIME:
        rs232_ack();
        _rs232_get_time(true);
        break;
    default:
        rs232_nak();
        break;
    };
}
#endif /* BUILD_RS232 */
