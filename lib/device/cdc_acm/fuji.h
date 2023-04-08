#ifndef FUJI_H
#define FUJI_H

#include <string>

#include "bus.h"
#include "fujiHost.h"
#include "fujiDisk.h"

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8

class cdcFuji : public virtualDevice
{
private:
    fujiHost _fnHosts[MAX_HOSTS];
    fujiDisk _fnDisks[MAX_DISK_DEVICES];

public:
    bool boot_config = true;

    cdcFuji();

    void setup(systemBus *systembus) {}

    int get_disk_id(int drive_slot) { return -1; }
    std::string get_host_prefix(int host_slot) { return std::string(); }

    fujiHost *get_hosts(int i) { return &_fnHosts[i]; }
    fujiDisk *get_disks(int i) { return &_fnDisks[i]; }

    void _populate_slots_from_config() {}

    void mount_all() {}
};

extern cdcFuji theFuji;

#endif /* FUJI_H */
