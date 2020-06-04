#ifndef NETWORKPROTOCOLTNFS
#define NETWORKPROTOCOLTNFS

#include "networkProtocol.h"
#include "sio.h"
#include "tnfslib.h"

class networkProtocolTNFS : public networkProtocol
{
public:
    networkProtocolTNFS();
    virtual ~networkProtocolTNFS();

    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool close();
    virtual bool read(byte *rx_buf, unsigned short len);
    virtual bool write(byte *tx_buf, unsigned short len);
    virtual bool status(byte *status_buf);
    virtual bool special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);
    virtual bool del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);
    virtual bool special_supported_00_command(unsigned char comnd);

private:
    bool block_read(byte *rx_buf, unsigned short len);
    bool block_write(byte *tx_buf, unsigned short len);

    tnfsMountInfo mountInfo;
    int16_t fileHandle;
    string filename;
    string directory;
    tnfsStat fileStat;
    char entryBuf[256];
    char aux1;
};

#endif /* NETWORKPROTOCOLTNFS */