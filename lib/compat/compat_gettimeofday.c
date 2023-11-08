#ifndef ESP_PLATFORM

#include <sys/time.h>

// precise gettimeofday on Windows
#if defined(_WIN32)


// a) use GetSystemTimePreciseAsFileTime
#include <windows.h>
int compat_gettimeofday(struct timeval *tv, struct timezone* tz)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;

    if (tv != NULL)
    {
#if !defined(_UCRT)
        GetSystemTimeAsFileTime(&ft); // low precision (16ms)
#else
        GetSystemTimePreciseAsFileTime(&ft); // high precision (1<ms)
#endif
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
        tmpres /= 10;  // convert into microseconds
        tmpres -= (__int64) 11644473600000000;
        tv->tv_sec = (long) (tmpres / 1000000UL);
        tv->tv_usec = (long) (tmpres % 1000000UL);
    }
    (void) tz;
    return 0;
}


// // b) use C++ chrono (portable, slower) (rename file to .cpp)
// #include <chrono>
// int compat_gettimeofday(struct timeval* tp, struct timezone* tzp) 
// {
//     namespace sc = std::chrono;
//     if (tv != NULL) 
//     {
//         sc::system_clock::duration d = sc::system_clock::now().time_since_epoch();
//         sc::seconds s = sc::duration_cast<sc::seconds>(d);
//         tp->tv_sec = s.count();
//         tp->tv_usec = sc::duration_cast<sc::microseconds>(d - s).count();
//     }
//     (void) tz;
//     return 0;
// }

#else
// Linux and macOS

int compat_gettimeofday(struct timeval* tp, struct timezone* tzp)
{
    return gettimeofday(tp, tzp);
}

#endif


// Test compat_gettimeofday
// example:
// test_gettimeofday(100000);
// test_gettimeofday(10000000);

// #include <stdint.h>
// #include <stdio.h>
// #include "compat_gettimeofday.h"

// void test_gettimeofday(int iter)
// {
//     struct timeval tv;
//     printf("test_gettimeofday(%d) started\n", iter);
//     compat_gettimeofday(&tv, NULL);
//     uint64_t t1 = (uint64_t)(tv.tv_sec*1000ULL+tv.tv_usec/1000ULL);
//     for(int i = 0; i < iter; i++)
//     {
//         compat_gettimeofday(&tv, NULL);
//     }
//     uint64_t t2 = (uint64_t)(tv.tv_sec*1000ULL+tv.tv_usec/1000ULL);
//     printf("test_gettimeofday ended in %lu ms\n", (unsigned long)(t2-t1));
// }

#endif // !ESP_PLATFORM