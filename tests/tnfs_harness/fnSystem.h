#ifndef TNFS_HARNESS_FNSYSTEM_H
#define TNFS_HARNESS_FNSYSTEM_H

// Test stand-in for lib/hardware/fnSystem.h: a virtual clock that only moves when the code
// under test waits.

#include <cstdint>

class HarnessSystem
{
public:
    uint64_t millis() const { return now_us / 1000; }
    void delay(uint32_t ms) { now_us += uint64_t(ms) * 1000; }
    void delay_microseconds(uint32_t us) { now_us += us; }
    void yield() {}

    uint64_t now_us = 0;
};

extern HarnessSystem fnSystem;

#endif
