// Minimal stub of the firmware SystemManager for the standalone TNFS harness.
// Only the members tnfslib.cpp actually calls are provided.
#ifndef _STUB_FNSYSTEM_H
#define _STUB_FNSYSTEM_H
#include <cstdint>

class SystemManager
{
public:
    SystemManager() = default;
    uint64_t millis();
    void delay(uint32_t ms);
    void delay_microseconds(uint32_t us);
    void yield();
};

extern SystemManager fnSystem;
#endif
