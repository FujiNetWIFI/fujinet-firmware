#ifndef SIOFUJI_H
#define SIOFUJI_H

#include "fujiDevice.h"
#include "cassette.h"
#include "../../qrcode/qrmanager.h"

#include <cassert>

class sioFuji : public fujiDevice
{
private:
    sioCassette _cassetteDev;
    QRManager _qrManager = QRManager();

protected:
    AtariSIODirection _transaction_direction = SIO_DIRECTION_INVALID;
    void transaction_continue(bool expectMoreData) override {
        assert(_transaction_direction == SIO_DIRECTION_INVALID);
        // For some reason NetSIO needs a hint that this is a WRITE transaction
        if (expectMoreData) {
            sio_late_ack();
            _transaction_direction = SIO_DIRECTION_WRITE;
        }
        else {
            sio_ack();
            _transaction_direction = SIO_DIRECTION_READ; // FIXME - do we care about NONE?
        }
    }
    void transaction_complete() override {
        assert(_transaction_direction != SIO_DIRECTION_INVALID);
        sio_complete();
        _transaction_direction = SIO_DIRECTION_INVALID;
    }
    void transaction_error() override {
        if (_transaction_direction == SIO_DIRECTION_INVALID)
            sio_error();
        else
            sio_nak();
        _transaction_direction = SIO_DIRECTION_INVALID;
    }
    bool transaction_get(void *data, size_t len) override {
        assert(_transaction_direction == SIO_DIRECTION_WRITE);
        uint8_t ck = bus_to_peripheral((uint8_t *) data, len);
        if (sio_checksum((uint8_t *) data, len) != ck)
            return false;
        return true;
    }
    void transaction_put(const void *data, size_t len, bool err) override {
        assert(_transaction_direction == SIO_DIRECTION_READ);
        bus_to_computer((uint8_t *) data, len, err);
        _transaction_direction = SIO_DIRECTION_INVALID;
    }

    size_t setDirEntryDetails(fsdir_entry_t *f, uint8_t *dest, uint8_t maxlen) override;

    void sio_net_set_ssid();           // 0xFB
    void sio_read_directory_block();   // 0xF6
    void sio_set_baudrate();           // 0xEB
    void sio_new_disk();               // 0xE7
    void sio_set_hsio_index();         // 0xE3
    void sio_copy_file();              // 0xD8

    // FIXME - move to fujiDevice mixin
    void sio_random_number();          // 0xD3
    void sio_base64_encode_input();    // 0xD0
    void sio_base64_encode_compute();  // 0xCF
    void sio_base64_encode_length();   // 0xCE
    void sio_base64_encode_output();   // 0xCD
    void sio_base64_decode_input();    // 0xCC
    void sio_base64_decode_compute();  // 0xCB
    void sio_base64_decode_length();   // 0xCA
    void sio_base64_decode_output();   // 0xC9

    // FIXME - move to fujiDevice mixin
    void sio_hash_input();             // 0xC8
    void sio_hash_compute(bool clear_data); // 0xC7, 0xC3
    void sio_hash_length();            // 0xC6
    void sio_hash_output();            // 0xC5
    void sio_hash_clear();             // 0xC2

    // FIXME - move to fujiDevice mixin
    void sio_qrcode_input();           // 0xBC
    void sio_qrcode_encode();          // 0xBD
    void sio_qrcode_length();          // OxBE
    void sio_qrcode_output();          // 0xBF

    void sio_status() override { fujicmd_status(); }
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

public:
    sioFuji();
    void setup();

    // Used by sio.cpp
    void debug_tape();
    sioCassette *cassette() { return &_cassetteDev; };
};

extern sioFuji platformFuji;

#endif // SIOFUJI_H
