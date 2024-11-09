#ifndef ESP_PLATFORM

#include <sys/time.h>

#if defined(_WIN32)
// precise gettimeofday on Windows

// a) use GetSystemTimePreciseAsFileTime (not available on Windows 7)
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define FILETIME2US(FT) ((((uint64_t)FT.dwHighDateTime) << 32 | ((uint64_t)FT.dwLowDateTime)) / 10 - (int64_t)11644473600000000)
#define PERFCOUNT2US(PC, FREQ) (PC.QuadPart / 1000.0 / FREQ)

int compat_gettimeofday(struct timeval *tv, struct timezone* tz)
{
    uint64_t tmpres = 0;
    FILETIME ft; // 100-nanosecond intervals since January 1, 1601

#ifndef USE_GETSYSTEMTIMEPRECISEASFILETIME
    // Workaround for missing GetSystemTimePreciseAsFileTime
    // This should work on Windows Xp and above
    static uint64_t tmref = 0; // reference time (microseconds)
    static int64_t pcref = -3600000000L; // reference counter (microseconds)
    static double frequency = -1.0; // performance frequency (GHz)

    // Obtain performance frequency
    if (frequency == -1.0)
    {
        LARGE_INTEGER freq;
        if (!QueryPerformanceFrequency(&freq))
        {
            // Cannot use QueryPerformanceCounter
            frequency = 0.0;
        }
        else
        {
            frequency = (double) freq.QuadPart / 1000000000.0; // GHz
        }
    }
#endif

    if (tv != NULL)
    {
#ifdef USE_GETSYSTEMTIMEPRECISEASFILETIME
        GetSystemTimePreciseAsFileTime(&ft);        // high precision wall time (<1 ms)
        tmpres = FILETIME2US(ft);
#else
        if (frequency == 0.0)
        {
            // Cannot use QueryPerformanceCounter, only low precision time is available
            GetSystemTimeAsFileTime(&ft);           // low precision wall time
            tmpres = FILETIME2US(ft);               // to us
        }
        else
        {
            // Combine low precision wall time with high precision counter
            int64_t pcpres;
            int64_t pcdelta;
            LARGE_INTEGER pc;
            QueryPerformanceCounter(&pc);           // high precision counter
            pcpres = PERFCOUNT2US(pc, frequency);   // to us
            pcdelta = pcpres - pcref;
            if (pcdelta >= 3600000000L) // sync low precision time at start and then once per hour
            {
                printf("time sync\n");
                // obtain new reference time
                GetSystemTimeAsFileTime(&ft);       // low precision wall time
                tmref = FILETIME2US(ft);            // to us
                pcref = pcpres;
                pcdelta = 0;
            }
            // Current time as low precision reference time + high precision counter delta
            tmpres = tmref + pcdelta;
        }
#endif
        tv->tv_sec = (long) (tmpres / 1000000UL);
        tv->tv_usec = (long) (tmpres % 1000000UL);
    }
    (void) tz;
    return 0;
}

// // b) use C++ chrono (portable, slower, MSYS2/CLANG64: fatal error: 'chrono' file not found)
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