// based on SPIFFS.h

#ifndef _TNFS_H_
#define _TNFS_H_

#include "FS.h"


namespace fs
{

class TNFSFS : public FS
{
public:
    TNFSFS();
    bool begin(const char * server, int port = 16384, const char * basePath="/tnfs", uint8_t maxOpenFiles=10);
    bool format();
    size_t totalBytes();
    size_t usedBytes();
    void end();
};

}

extern fs::TNFSFS TNFS;


#endif
