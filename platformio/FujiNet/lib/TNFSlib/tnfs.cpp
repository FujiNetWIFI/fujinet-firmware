// based on SPIFFS.cpp

#include "tnfs.h"
extern tnfsPacket_t tnfsPacket;

TNFSFS::TNFSFS() : FS(FSImplPtr(new TNFSImpl()))
{
}

bool TNFSFS::begin(std::string host, uint16_t port, std::string location, std::string userid, std::string password)
{
    char numstr[5]; // enough to hold all numbers up to 16-bits
    sprintf(numstr, "%u", port);
    const char sep = ' ';

    if (strlen(mparray) != 0)
        return true;

    std::string mp = host + sep + numstr + sep + "0 0 " + location + sep + userid + sep + password;
    mp.copy(mparray, mp.length(), 0);
    _impl->mountpoint(mparray);
    tnfsSessionID_t tempID = tnfs_mount(_impl);

    if (tempID.session_idl == 0 && tempID.session_idh == 0)
    {
        mparray[0] = '\0';
        return false;
    }
    char lo[3];
    sprintf(lo, "%u", tempID.session_idl);
    char hi[3];
    sprintf(hi, "%u", tempID.session_idh);
    mp = host + sep + numstr + sep + lo + sep + hi + sep + location + sep + userid + sep + password;
    mp.copy(mparray, mp.length(), 0);
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
