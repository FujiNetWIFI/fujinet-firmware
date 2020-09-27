#ifndef NETWORK_H
#define NETWORK_H

#include "esp_timer.h"
#include "sio.h"
#include "networkProtocol.h"
#include "EdUrlParser.h"
#include "json.h"

#define NUM_DEVICES 8

#define INPUT_BUFFER_SIZE 65535
#define OUTPUT_BUFFER_SIZE 65535

#define SPECIAL_BUFFER_SIZE 256
#define DEVICESPEC_SIZE 256

#define OPEN_STATUS_NOT_CONNECTED 128
#define OPEN_STATUS_DEVICE_ERROR 144
#define OPEN_STATUS_INVALID_DEVICESPEC 165

//#include <Arduino.h>
// *** Pulled these out since they're only used in network.cpp
// For the interrupt rate limiter timer
//extern volatile bool interruptProceed;
//extern hw_timer_t *rateTimer;
//extern portMUX_TYPE timerMux;

class sioNetwork : public sioDevice
{

private:
    bool allocate_buffers();
    void deallocate_buffers();
    bool open_protocol();
    void start_timer();

protected:
    union
    {
        struct
        {
            unsigned short rx_buf_len;
            unsigned char connection_status;
            unsigned char error;
        };
        uint8_t rawData[4];
    } status_buf;

    unsigned char previous_connection_status;

public:
    virtual void sio_open();
    virtual void sio_close();
    virtual void sio_read();
    virtual void sio_write();
    virtual void sio_special();

    void sio_assert_interrupts();

    static void sio_enable_interrupts(bool enable = true);

    void sio_status_local();

    void sio_special_00();
    void sio_special_40();
    void sio_special_80();

    void sio_special_protocol_00();
    void sio_special_protocol_40();
    void sio_special_protocol_80();

    void sio_special_set_translation();
    void sio_special_parse_json();
    void sio_special_json_read_query();

    bool sio_special_supported_00_command(unsigned char c);
    bool sio_special_supported_40_command(unsigned char c);
    bool sio_special_supported_80_command(unsigned char c);

    virtual void sio_status() override;
    void sio_process(uint32_t commanddata, uint8_t checksum) override;

private:
    string deviceSpec;
    networkProtocol *protocol = nullptr;
    EdUrlParser *urlParser = nullptr;
    unsigned char err;
    uint8_t ck;
    uint8_t *rx_buf = nullptr;
    uint8_t *tx_buf = nullptr;
    uint8_t *sp_buf = nullptr;
    unsigned short rx_buf_len;
    unsigned short tx_buf_len = 256;
    unsigned short sp_buf_len;
    unsigned char aux1;
    unsigned char aux2;
    unsigned char trans_aux2;
    string prefix;
    string initial_prefix;
    char filespecBuf[256];
    JSON _json;
    enum _read_mode 
    {
        NORMAL,
        QUERY_JSON
    } read_mode;

    union
    {
        struct
        {
            unsigned short sector;
            unsigned short offset;
        };
        uint8_t rawData[3];
    } note_pos;

    bool parseURL();
    bool isValidURL(EdUrlParser *url);
};

#endif /* NETWORK_H */
