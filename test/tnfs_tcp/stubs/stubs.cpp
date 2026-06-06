// Stub implementations of the firmware-only symbols that tnfslib.cpp references.
#include "fnSystem.h"
#include "bus.h"

#include <chrono>
#include <thread>
#include <cstdarg>
#include <cstdio>

SystemManager fnSystem;
StubSystemBus SYSTEM_BUS;

static const std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();

uint64_t SystemManager::millis()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _start).count();
}
void SystemManager::delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void SystemManager::delay_microseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
void SystemManager::yield() {}

// Debug_printf/println route here on non-ESP builds (see include/debug.h).
extern "C" {} // keep linkage tidy
void util_debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}
