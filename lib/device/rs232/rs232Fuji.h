#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"

class rs232Fuji : public fujiDevice
{
private:

protected:
    void transaction_continue(bool expectMoreData) override {}
    void transaction_complete() override { rs232_complete(); }
    void transaction_error() override { rs232_error(); }
    bool transaction_get(void *data, size_t len) override {
        uint8_t ck = bus_to_peripheral((uint8_t *) data, len);
        if (rs232_checksum((uint8_t *) data, len) != ck)
            return false;
        return true;
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        bus_to_computer((uint8_t *) data, len, err);
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void rs232_net_set_ssid(bool save);    // 0xFB
    void rs232_new_disk();                 // 0xE7
    void rs232_copy_file();                // 0xD8
    void rs232_test();                     // 0x00

public:
    rs232Fuji() : fujiDevice(MAX_DISK_DEVICES) {}
    void setup() override;
    void rs232_status() override;
    void rs232_process(cmdFrame_t *cmd_ptr) override;

    // ============ Wrapped Fuji commands ============
};

#endif /* RS232FUJI_H */
#endif /* BUILD_RS232 */
