#include "lwip/apps/sntp.h"

void set_time_zone(long offset, int daylight);
void config_time(long gmtOffset_sec, int daylightOffset_sec, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
void config_tz_time(const char* tz, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
bool get_local_time(struct tm * info, uint32_t ms = 5000);
