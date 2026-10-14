#ifndef TNFS_HARNESS_BUS_H
#define TNFS_HARNESS_BUS_H

// Test stand-in for lib/bus/bus.h: never shutting down.

class HarnessBus
{
public:
    bool getShuttingDown() const { return false; }
};

extern HarnessBus SYSTEM_BUS;

#endif
