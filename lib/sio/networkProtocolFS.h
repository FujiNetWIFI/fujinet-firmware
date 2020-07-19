#ifndef NETWORKPROTOCOLFS_H
#define NETWORKPROTOCOLFS_H

#include "networkProtocol.h"
#include "sio.h"
#include "EdUrlParser.h"

/**
 * Class for common logic for all N: accessible filesystems 
 */
class networkProtocolFS : public networkProtocol
{
public:
    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool close();
    virtual bool read(uint8_t *rx_buf, unsigned short len);
    virtual bool write(uint8_t *tx_buf, unsigned short len);
    virtual bool status(uint8_t *status_buf);
    virtual bool special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool note(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool point(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);

protected:
    bool canRead = false;           // Can read 
    bool canWrite = false;          // Can write
    bool dirRead = false;           // Reading directory?
    unsigned long filePos;          // File position for note/point
};

#endif /* NETWORKPROTOCOLFS_H */