// based on SPIFFS.cpp

#include "tnfs.h"
extern tnfsPacket_t tnfsPacket;

TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

byte TNFSFS::begin(String host, uint16_t port, String location, String userid, String password)
{
    bool err = tnfs_mount(host, port, location, userid, password);
    /*    Return cases:
    true - successful mount.
    false with error code in tnfsPacket.data[0] 
    false with zero in tnfsPacket.data[0] - timeout
    */
    if (err)
    {
        String mp = "//" + host + ":" + String(port) + location;
        BUG_UART.println(mp);
        int n = mp.length();
        mp.toCharArray(mparray,n+1);
        _impl->mountpoint(mparray);
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
    //_id = 0;
    _impl->mountpoint(NULL);
}

TNFSFS TNFS; // create pointer to filesystem implementation
