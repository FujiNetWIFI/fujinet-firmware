/*
    VSCODE WILL COMPLAIN THAT IT DOESN'T KNOW WHAT SETENV() and TZSET() ARE
    ** IT LIES **
    JUST CLOSE THE FILE AND COMPILE.
    HAS SOMETHING TO DO WITH THE WAY THE HEADERS ARE SET UP.
    https://community.platformio.org/t/issue-with-esp-idf-time-h/6986/8

*/
#include <stdlib.h>
#include <time.h>

#include "../hardware/fnSystem.h"
#include "fnSNTP.h"

void set_time_zone(long offset, int daylight)
{
    char cst[17] = {0};
    char cdt[17] = "DST";
    char tz[33] = {0};

    if(offset % 3600){
        sprintf(cst, "UTC%ld:%02ld:%02ld", offset / 3600, abs((offset % 3600) / 60), abs(offset % 60));
    } else {
        sprintf(cst, "UTC%ld", offset / 3600);
    }
    if(daylight != 3600){
        long tz_dst = offset - daylight;
        if(tz_dst % 3600){
            sprintf(cdt, "DST%ld:%02ld:%02ld", tz_dst / 3600, abs((tz_dst % 3600) / 60), abs(tz_dst % 60));
        } else {
            sprintf(cdt, "DST%ld", tz_dst / 3600);
        }
    }
    sprintf(tz, "%s%s", cst, cdt);
    setenv("TZ", tz, 1);
    tzset(); 
}

/*
 * configTime
 * Source: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/time.c
 * */
void config_time(long gmtOffset_sec, int daylightOffset_sec, const char* server1, const char* server2, const char* server3)
{
    if(sntp_enabled()){
        sntp_stop();
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char*)server1);
    sntp_setservername(1, (char*)server2);
    sntp_setservername(2, (char*)server3);
    sntp_init();
    set_time_zone(-gmtOffset_sec, daylightOffset_sec);
}

/*
 * configTzTime
 * sntp setup using TZ environment variable
 * */
void config_tz_time(const char* tz, const char* server1, const char* server2, const char* server3)
{
    if(sntp_enabled()){
        sntp_stop();
    }
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char*)server1);
    sntp_setservername(1, (char*)server2);
    sntp_setservername(2, (char*)server3);
    sntp_init();
    setenv("TZ", tz, 1);
    tzset();
}

bool get_local_time(struct tm * info, uint32_t ms)
{
    uint32_t start = fnSystem.millis();
    time_t now;
    while((fnSystem.millis()-start) <= ms) {
        time(&now);
        localtime_r(&now, info);
        if(info->tm_year > (2016 - 1900)){
            return true;
        }
        fnSystem.delay(10);
    }
    return false;
}
