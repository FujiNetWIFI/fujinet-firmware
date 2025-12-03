#ifndef DRIVEWIREFUJI_H
#define DRIVEWIREFUJI_H

#include "fujiDevice.h"

#include "cassette.h"

#define MAX_DWDISK_DEVICES 4

#define IMAGE_EXTENSION ".dsk"

class drivewireFuji : public fujiDevice
{
private:
    std::string _response;

    uint8_t _errorCode;

#ifdef ESP_PLATFORM
    drivewireCassette _cassetteDev;
#endif

protected:
    void transaction_continue(bool expectMoreData) override {}
    void transaction_complete() override {
        _errorCode = 1;
        _response.clear();
        _response.shrink_to_fit();
    }
    void transaction_error() override {
        _errorCode = 144;
    }
    bool transaction_get(void *data, size_t len) override {
        return SYSTEM_BUS.read((uint8_t *) data, len) == len;
    }
    void transaction_put(const void *data, size_t len, bool err=false) override {
        transaction_complete();
        _response.append((char *) data, len);
        if (err)
            transaction_error();
    }

    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    void new_disk();               // 0xE7
    void random();                 // 0xD3
    void base64_encode_input();    // 0xD0
    void base64_encode_compute();  // 0xCF
    void base64_encode_length();   // 0xCE
    void base64_encode_output();   // 0xCD
    void base64_decode_input();    // 0xCC
    void base64_decode_compute();  // 0xCB
    void base64_decode_length();   // 0xCA
    void base64_decode_output();   // 0xC9
    void hash_input();             // 0xC8
    void hash_compute(bool clear_data); // 0xC7, 0xC3
    void hash_length();            // 0xC6
    void hash_output();            // 0xC5
    void get_adapter_config_extended(); // 0xC4
    void hash_clear();             // 0xC2

    void send_error();             // 0x02
    void send_response();          // 0x01
    void ready();                  // 0x00
    void shutdown() override;

public:
    drivewireNetwork *network();

#ifdef ESP_PLATFORM
    drivewireCassette *cassette() { return &_cassetteDev; };
#endif
    void debug_tape();

    void setup() override;
    void process();
    drivewireFuji();
};

extern drivewireFuji platformFuji;

#endif // DRIVEWIREFUJI_H
