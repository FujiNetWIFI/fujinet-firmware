// Minimal stub of the firmware system bus for the standalone TNFS harness.
// tnfslib.cpp only calls SYSTEM_BUS.getShuttingDown().
#ifndef _STUB_BUS_H
#define _STUB_BUS_H

class StubSystemBus
{
public:
    bool getShuttingDown() { return false; }
};

extern StubSystemBus SYSTEM_BUS;
#endif
