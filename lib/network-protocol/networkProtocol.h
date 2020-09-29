#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include "sio.h"
#include "EdUrlParser.h"

class networkProtocol
{
public:
    virtual ~networkProtocol() {}

    bool connectionIsServer = false;
    bool assertInterrupt = false;
    bool assertProceed = false;

    uint8_t *saved_rx_buffer;
    unsigned short *saved_rx_buffer_len;

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) = 0;
    virtual bool close() = 0;
    virtual bool read(uint8_t *rx_buf, unsigned short len) = 0;
    virtual bool write(uint8_t *tx_buf, unsigned short len) = 0;
    virtual bool status(uint8_t *status_buf) = 0;
    virtual bool special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) = 0;

    virtual int available() = 0;

    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) { return false; }
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) { return false; }
    virtual bool mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) { return false; }
    virtual bool rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) { return false; }
    virtual bool note(uint8_t *rx_buf) { return false; }
    virtual bool point(uint8_t *tx_buf) { return false; }

    virtual bool isConnected() { return true; }

    void set_saved_rx_buffer(uint8_t *rx_buf, unsigned short *len)
    {
        saved_rx_buffer = rx_buf;
        saved_rx_buffer_len = len;
    }

    virtual bool special_supported_40_command(unsigned char comnd) { return false; }
    virtual bool special_supported_80_command(unsigned char comnd) { return false; }
    virtual bool special_supported_00_command(unsigned char comnd) { return false; }
};

#endif /* NETWORKPROTOCOL_H */
