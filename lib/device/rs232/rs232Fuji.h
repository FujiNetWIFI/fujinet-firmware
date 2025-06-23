#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"

#define STATUS_MOUNT_TIME       0x01

class rs232Fuji : public fujiDevice
{
private:

protected:
    void transaction_complete() override { rs232_complete(); }
    void transaction_error() override { rs232_error(); }
    bool transaction_get(void *data, size_t len) {
        uint8_t ck = bus_to_peripheral((uint8_t *) data, len);
        if (rs232_checksum((uint8_t *) data, len) != ck)
            return false;
        return true;
    }
    void transaction_put(void *data, size_t len, bool err) override {
        bus_to_computer((uint8_t *) data, len, err);
    }

    void rs232_net_set_ssid();           // 0xFB
    void rs232_open_directory();         // 0xF7
    void rs232_read_directory_entry();   // 0xF6
    void rs232_enable_udpstream();       // 0xF0
    void rs232_net_get_wifi_enabled();   // 0xEA
    void rs232_new_disk();               // 0xE7
    void rs232_get_directory_position(); // 0xE5
    void rs232_set_hrs232_index();         // 0xE3
    void rs232_set_device_filename();    // 0xE2
    void rs232_set_host_prefix();        // 0xE1
    void rs232_get_host_prefix();        // 0xE0
    void rs232_set_rs232_external_clock(); // 0xDF
    void rs232_write_app_key();          // 0xDE
    void rs232_read_app_key();           // 0xDD
    void rs232_open_app_key();           // 0xDC
    void rs232_close_app_key();          // 0xDB
    void rs232_copy_file();              // 0xD8
    void rs232_test();                   // 0x00

public:
    void setup(systemBus *sysbus) override;
    void rs232_status();
    void rs232_process(cmdFrame_t *cmd_ptr);

    // ============ Wrapped Fuji commands ============
};

#endif /* RS232FUJI_H */
#endif /* BUILD_RS232 */
