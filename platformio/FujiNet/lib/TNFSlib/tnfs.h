#ifndef _TNFS_H_
#define _TNFS_H_
#include <Arduino.h>
#include <string.h>
#include "debug.h"


// UDP Implementation of TNFS for ESP Arduino
// I read SPIFFS.h and associated files to understand how to structure a custom FS for Arduino

/*
These functions are part of TNFS and are implemented in the following files, classes, and methods.
Minumum set of TNFSD functions are deonted with *

Connection management
---------------------
mount - Connect to a TNFS filesystem *                                          tnfs.h, TNFSFS, begin
umount - Disconnect from a TNFS filesystem *                                    tnfs.h, TNFSFS, end

Devices
-------
size - Get the size of the filesystem *                                         tnfs.h, TNFSFS, size
free - Get the remaining free space on the filesystem *                         tnfs.h, TNFSFS, free

Directories
-----------
opendir - Opens a directory for reading *                                       tnfs.h, TNFSFS, opendir
readdir - Reads a directory entry *                                             tnfs.h, TNFSFS, readdir
closedir - Closes the directory *                                               tnfs.h, TNFSFS, closedir
rmdir - Removes a directory                                                     tnfs_api.h, TNFSImpl, rmdir
mkdir - Creates a directory                                                     tnfs_api.h, TNFSImpl, mkdir

Files
-----
open - Opens a file *                                                           tnfs_api.h, TNFSImpl, open
read - reads from an open file *                                                tnfs_api.h, TNFSFileImpl, read
write - writes to an open file                                                  tnfs_api.h, TNFSFileImpl, write
close - closes a file *                                                         tnfs_api.h, TNFSFileImpl, close
stat - gets information about a file *                                          tnfs.h, TNFSFS, stat?
lseek - set the position in the file where the next byte will be read/written   tnfs_api.h, TNFSFileImpl, seek
chmod - change file access                                                      tnfs.h, TNFSFS, chmod? 
unlink - remove a file                                                          tnfs_api.h, TNFSImpl, remove
*/

#include "FS.h"
#include "FSImpl.h"
#include "tnfs_imp.h"

#define TNFS_PORT 16384

namespace fs
{

class TNFSFS : public FS
{
/*
The following functions are defined by FS and implented in TNFSImpl. 

File open(const char* path, const char* mode = FILE_READ);
bool exists(const char* path);
bool remove(const char* path);
bool rename(const char* pathFrom, const char* pathTo);
bool mkdir(const char *path);
bool rmdir(const char *path);

Anything extra needs to be declared here in TNFSFS.
*/

private:
    char mparray[256]="\0";

public:
    TNFSFS();
    ~TNFSFS();
    bool begin(std::string host, uint16_t port=16384, std::string location="/", std::string userid="!", std::string password="!");
    size_t size();
    size_t free();
    void end();
}; // class TNFSFS

} // namespace fs

// extern fs::TNFSFS TNFS; // declare TNFS for use in main.cpp and elsewhere.

#endif
