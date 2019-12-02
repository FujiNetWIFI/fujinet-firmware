#ifndef _TNFS_API_H
#define _TNFS_API_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <FSImpl.h>

#define TNFS_SERVER "mozzwald.com"
#define TNFS_PORT 16384

/*
Directories
-----------
opendir - Opens a directory for reading *
readdir - Reads a directory entry *
closedir - Closes the directory *
rmdir - Removes a directory
mkdir - Creates a directory

Devices
-------
size - Get the size of the filesystem *
free - Get the remaining free space on the filesystem *

Files
-----
open - Opens a file *
read - reads from an open file *
write - writes to an open file
close - closes a file *
stat - gets information about a file *
lseek - set the position in the file where the next byte will be read/written
chmod - change file access
unlink - remove a file
*/

void tnfs_mount(const char *host, uint16_t port);
void tnfs_open();
void tnfs_read();
void tnfs_seek(uint32_t offset);

using namespace fs;

class TNFSFileImpl;

class TNFSImpl : public FSImpl
{
/*
Connection management
---------------------
mount - Connect to a TNFS filesystem *
umount - Disconnect from a TNFS filesystem *

Directories
-----------
opendir - Opens a directory for reading *
readdir - Reads a directory entry *
closedir - Closes the directory *
rmdir - Removes a directory
mkdir - Creates a directory

Devices
-------
size - Get the size of the filesystem *
free - Get the remaining free space on the filesystem *

Files
-----
open - Opens a file *
*/

protected:
    friend class TNFSFileImpl;

public:
    TNFSImpl();
    ~TNFSImpl() {}
    FileImplPtr open(const char *path, const char *mode) override;
    bool exists(const char *path) override;
    bool rename(const char *pathFrom, const char *pathTo) override;
    bool remove(const char *path) override;
    bool mkdir(const char *path) override;
    bool rmdir(const char *path) override;
};

class TNFSFileImpl : public FileImpl
{
/*
Files
-----
read - reads from an open file *
write - writes to an open file
close - closes a file *
stat - gets information about a file *
lseek - set the position in the file where the next byte will be read/written
chmod - change file access
unlink - remove a file
*/
protected:
    TNFSImpl *_fs;
    // File*                _f; // ?? not sure. SPIFFS uses FILE*
    //DIR *               _d;
    //char *              _path;
    //bool                _isDirectory;
    //mutable struct stat _stat;
    //mutable bool        _written;
    //void _getStat() const;

public:
    TNFSFileImpl(TNFSImpl *fs, const char *path, const char *mode);
    ~TNFSFileImpl(){};
    size_t write(const uint8_t *buf, size_t size) override;
    size_t read(uint8_t *buf, size_t size) override;
    void flush() override;
    bool seek(uint32_t pos, SeekMode mode) override;
    size_t position() const override;
    size_t size() const override;
    void close() override;
    const char *name() const override;
    time_t getLastWrite() override;
    boolean isDirectory(void) override;
    FileImplPtr openNextFile(const char *mode) override;
    void rewindDirectory(void) override;
    operator bool();
};

#endif //_TNFS_API_H
