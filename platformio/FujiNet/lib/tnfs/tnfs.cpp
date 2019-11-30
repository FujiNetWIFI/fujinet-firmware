
#include "vfs_api.h"
#include "FS.h"
#include "TNFS.h"
#include "tnfs_things.h"

using namespace fs;

TNFSFS::TNFSFS(FSImplPtr impl) : FS(impl) {}

bool TNFSFS::begin(const char *tnfs_server, uint16_t tnfs_port)
{
    tnfs_mount(tnfs_server, tnfs_port);
    //_impl->mountpoint(mountpoint);
    return true;
}

void TNFSFS::end()
{
    //_impl->mountpoint(NULL);
}

TNFSFS TNFS = TNFSFS(FSImplPtr(new VFSImpl()));
