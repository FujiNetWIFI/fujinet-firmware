#ifndef LYNXFUJI_H
#define LYNXFUJI_H

#include "fujiDevice.h"

#include <cstdint>
#include <cassert>

#include "network.h"
#include "disk.h"
#include "netstream.h"

#include "fujiHost.h"
#include "fujiDisk.h"
#include "fujiDevice.h"



class lynxFuji : public fujiDevice
{
private:
    lynxNetStream _streamDev;

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void comlynx_new_disk();               // 0xE7
    void fujicmd_random_number();          // 0xD3
    void fujicmd_get_time();               // 0xD2

    void comlynx_process() override;

    void shutdown() override;

public:
    void setup() override;

    lynxFuji();
};

#endif // LYNXFUJI_H
