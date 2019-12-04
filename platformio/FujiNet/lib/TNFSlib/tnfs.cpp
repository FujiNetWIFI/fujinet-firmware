// based on SPIFFS.cpp

#include "tnfs.h" // chnanged to TNFS
extern tnfsPacket_t tnfsPacket;

TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

byte TNFSFS::begin(const char *host, uint16_t port, const char *location, const char *userid, const char *password)
{
    bool err = tnfs_mount(host, port, location, userid, password); 
    /*    Return cases:
    true - successful mount.
    false with error code in tnfsPacket.data[0] 
    false with zero in tnfsPacket.data[0] - timeout
    */
    if (err)
    {
        sessionID = tnfsPacket.session_idh * 256 + tnfsPacket.session_idl;
        _impl->mountpoint(location);
        return 0;
    }
    else if (tnfsPacket.data[0] == 0x00)
    {
        return 138; // timeout!
    }
    else 
    {
        return tnfsPacket.data[0]; // error code
    }
}

size_t TNFSFS::size() { return 0; }
size_t TNFSFS::free() { return 0; }
void TNFSFS::end()
{
    sessionID = 0;
    _impl->mountpoint(NULL);
}

TNFSFS TNFS; // create pointer to filesystem implementation
