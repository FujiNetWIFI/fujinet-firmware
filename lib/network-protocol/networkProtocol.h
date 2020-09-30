#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include "sio.h"
#include "EdUrlParser.h"
#include "networkStatus.h"

class NetworkProtocol
{
public:
    virtual ~NetworkProtocol() {}

    bool connectionIsServer = false;

    uint8_t *saved_rx_buffer;
    unsigned short *saved_rx_buffer_len;

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame) = 0;
    virtual bool close() = 0;
    virtual bool read(uint8_t *rx_buf, unsigned short len) = 0;
    virtual bool write(uint8_t *tx_buf, unsigned short len) = 0;
    virtual bool status(NetworkStatus *status) = 0;
    virtual uint8_t special_inquiry(uint8_t cmd);
    virtual bool special_00(cmdFrame_t *cmdFrame) { return false; }
    virtual bool special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) { return false; }
    virtual bool special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) { return false; }

    virtual int available() = 0;

    // Todo: move these out to network-protocol/FS.h
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

};

#endif /* NETWORKPROTOCOL_H */
