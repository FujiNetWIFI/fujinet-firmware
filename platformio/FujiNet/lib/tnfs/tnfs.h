
#ifndef _TNFS_H_
#define _TNFS_H_

#include "FS.h"

namespace fs
{

class TNFSFS : public FS
{
protected:

public:
    TNFSFS(FSImplPtr impl);
    bool begin(const char *tnfs_server, uint16_t tnfs_port=16384);
    void end();
};

}

extern fs::TNFSFS TNFS;

// using namespace fs;
// typedef fs::File        SDFile;
// typedef fs::SDFS        SDFileSystemClass;
// #define SDFileSystem    SD

#endif /* _TNFS_H_ */
