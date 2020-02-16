// based on SPIFFS.cpp

#include "tnfs.h"
extern tnfsPacket_t tnfsPacket;

TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

bool TNFSFS::begin(std::string host, uint16_t port, std::string location, std::string userid, std::string password)
{
    std::string mp;
    char portstr[6]; // enough to hold all numbers up to 16-bits
    char hi[4];
    char lo[4];
    const char sep = ' ';

    sprintf(portstr, "%hu", port);

    if (strlen(mparray) != 0)
        return true;

    mp = host + sep + portstr + sep + "0 0 " + location + sep + userid + sep + password;

    mp.copy(mparray, mp.length(), 0);
    _impl->mountpoint(mparray);
#ifdef DEBUG
    Debug_printf("mounting %s\n", mparray);
#endif
    tnfsSessionID_t tempID = tnfs_mount(_impl);

    if (tempID.session_idl == 0 && tempID.session_idh == 0)
    {
        mparray[0] = '\0';
#ifdef DEBUG
        Debug_println("TNFS mount failed.");
#endif
        return false;
    }

    sprintf(lo, "%hhu", tempID.session_idl);
    sprintf(hi, "%hhu", tempID.session_idh);
    //mp.clear(); // rebuild mountpoint with session ID
    mp = host + sep + portstr + sep + lo + sep + hi + sep + location + sep + userid + sep + password;
    mp.copy(mparray, mp.length(), 0);
#ifdef DEBUG
    Debug_printf("TNFS mount successful: %s\n\n", mparray);
#endif
    _impl->mountpoint(mparray);

    return true;
}

size_t TNFSFS::size() { return 0; }
size_t TNFSFS::free() { return 0; }
void TNFSFS::end()
{
    _impl->mountpoint(NULL);
}

// TNFSFS TNFS; // create pointer to filesystem implementation
