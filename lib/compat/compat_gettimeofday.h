#ifndef COMPAT_GETTIMEOFDAY_H
#define COMPAT_GETTIMEOFDAY_H

#ifndef ESP_PLATFORM

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int compat_gettimeofday(struct timeval* tp, struct timezone* tzp);

// void test_gettimeofday(int iter);

#ifdef __cplusplus
}
#endif

#endif // !ESP_PLATFORM

#endif // COMPAT_GETTIMEOFDAY_H