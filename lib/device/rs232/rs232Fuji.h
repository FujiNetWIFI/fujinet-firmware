#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"

#include <cassert>

class rs232Fuji : public fujiDevice
{
private:

protected:
    bool _transaction_did_ack = false;
    void transaction_continue(bool expectMoreData) override {
        assert(!_transaction_did_ack);
        rs232_ack();
        _transaction_did_ack = true;
    }
    void transaction_complete() override {
        assert(_transaction_did_ack);
        rs232_complete();
        _transaction_did_ack = false;
    }
    void transaction_error() override {
        if (_transaction_did_ack)
            rs232_error();
        else
            rs232_nak();
        _transaction_did_ack = false;
    }
    bool transaction_get(void *data, size_t len) override {
        assert(_transaction_did_ack);
        uint8_t ck = bus_to_peripheral((uint8_t *) data, len);
        if (rs232_checksum((uint8_t *) data, len) != ck)
            return false;
        return true;
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        assert(_transaction_did_ack);
        bus_to_computer((uint8_t *) data, len, err);
        _transaction_did_ack = false;
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
