#ifndef _TNFS_IMP_H
#define _TNFS_IMP_H

#include "tnfs.h"
#include <FS.h>
#include <FSImpl.h>
#include "tnfs_udp.h"

  
#define TNFS_RDONLY 0x0001 //Open read only
#define TNFS_WRONLY 0x0002 //Open write only
#define TNFS_RDWR 0x0003   //Open read/write
#define TNFS_APPEND 0x0008 //Append to the file, if it exists (write only)
#define TNFS_CREAT 0x0100  //Create the file if it doesn't exist (write only)
#define TNFS_TRUNC 0x0200  //Truncate the file on open for writing
#define TNFS_EXCL 0x0400   //With TNFS_CREAT, returns an error if the file exists


using namespace fs;

class TNFSFileImpl;

class TNFSImpl : public FSImpl
{
/*
This class implements the physical interface for built-in functions in FS.h
*/
protected:
    friend class TNFSFileImpl;

public:
    char *_host;
    uint16_t _port;
    uint16_t sessionID=0;
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
This class implements the physical interface for built-in functions in the File class defined in FS.h
*/

protected:
    TNFSImpl *_fs;
    char *_path;
    char *_mode;
    byte _fd;

public:
    TNFSFileImpl(TNFSImpl *fs, byte fd);
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

#endif //_TNFS_IMP_H
