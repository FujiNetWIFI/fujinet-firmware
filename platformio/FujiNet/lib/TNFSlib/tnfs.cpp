// based on SPIFFS.cpp

#include "tnfs.h"
extern tnfsPacket_t tnfsPacket;

TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

byte TNFSFS::begin(std::string host, uint16_t port, std::string location, std::string userid, std::string password)
{
    char numstr[5]; // enough to hold all numbers up to 16-bits
    sprintf(numstr, "%0u", port);
    const char sep = ' ';
    
    std::string mp = host + sep + numstr + sep + location + sep + userid + sep + password;
    mp.copy(mparray,128,0);
    _impl->mountpoint(mparray);
    bool err = tnfs_mount(_impl);
}

size_t TNFSFS::size() { return 0; }
size_t TNFSFS::free() { return 0; }
void TNFSFS::end()
{
    _impl->mountpoint(NULL);
}

// TNFSFS TNFS; // create pointer to filesystem implementation
