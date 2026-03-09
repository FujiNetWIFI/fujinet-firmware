#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"

#include <cassert>

class rs232Fuji : public fujiDevice
{
private:

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
    bool transaction_get(void *data, size_t len) override {
        return virtualDevice::transaction_get(data, len);
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        virtualDevice::transaction_put(data, len, err);
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void rs232_net_set_ssid(bool save);    // 0xFB
    void rs232_new_disk();                 // 0xE7
    void rs232_test();                     // 0x00

public:
    rs232Fuji();
    void setup() override;
    void rs232_status(FujiStatusReq reqType) override;
    void rs232_process(FujiBusPacket &packet) override;

    // ============ Wrapped Fuji commands ============
    std::optional<std::vector<uint8_t>> fujicore_read_app_key() override;
};

#endif /* RS232FUJI_H */
#endif /* BUILD_RS232 */
