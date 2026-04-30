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
   // Temporary until all platforms have transaction_ methods in virtualDevice base class
    void transaction_continue(transState_t expectMoreData) override {
        virtualDevice::transaction_continue(expectMoreData);
    }
    void transaction_complete() override {
        virtualDevice::transaction_complete();
    }
    void transaction_error() override {
        virtualDevice::transaction_error();
    }
    success_is_true transaction_get(void *data, size_t len) override {
        return virtualDevice::transaction_get(data, len);
    }
    void transaction_put(const void *data, size_t len, bool err=false) override {
        virtualDevice::transaction_put(data, len, err);
    }

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
