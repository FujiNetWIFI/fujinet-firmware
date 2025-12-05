#ifndef LYNXFUJI_H
#define LYNXFUJI_H

#include "fujiDevice.h"

#include <cstdint>

#include "network.h"
#include "disk.h"
#include "serial.h"

#include "fujiHost.h"
#include "fujiDisk.h"
#include "fujiDevice.h"

class lynxFuji : public fujiDevice
{
protected:
    void transaction_continue(bool expectMoreData) override {}
    void transaction_complete() override {
        comlynx_response_ack();
    }
    void transaction_error() override {
        comlynx_response_nack();
    }
    bool transaction_get(void *data, size_t len) override {
        unsigned short rlen = comlynx_recv_buffer((uint8_t *) data, len);
        return rlen == len;
    }
    void transaction_put(const void *data, size_t len, bool err=false) override {
        memcpy(response, data, len);
        response_len = len;
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void comlynx_new_disk();               // 0xE7
    void comlynx_write_app_key();          // 0xDE
    void comlynx_set_boot_config();        // 0xD9
    void comlynx_enable_device();          // 0xD5
    void comlynx_disable_device();         // 0xD4
    void comlynx_random_number();          // 0xD3
    void comlynx_get_time();               // 0xD2
    void comlynx_device_enable_status();   // 0xD1

    void comlynx_hello(); // test

    void comlynx_test_command();

    void comlynx_control_status() override;
    void comlynx_control_send();
    void comlynx_control_clr();

    void comlynx_process(uint8_t b) override;

    void shutdown() override;

public:
    void setup() override;

    lynxFuji();
};

#endif // LYNXFUJI_H
