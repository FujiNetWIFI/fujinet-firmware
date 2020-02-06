#ifndef _TNFS_IMP_H
#define _TNFS_IMP_H
#include <Arduino.h>

#include "tnfs.h"
#include <FS.h>
#include <FSImpl.h>
#include "tnfs_udp.h"

// https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html  
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
//This class implements the physical interface for built-in functions in FS.h
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
//This class implements the physical interface for built-in functions in the File class defined in FS.h

protected:
    TNFSImpl *_fs;
    byte _fd;
    String _host;
    int _port;
    //char *_path; // used?
    //char *_mode; // used?

    // Feb 6, 2020:
    // Can we store TNFS session info here? 


public:
    TNFSFileImpl(TNFSImpl *fs, byte fd, String host, int port);
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
