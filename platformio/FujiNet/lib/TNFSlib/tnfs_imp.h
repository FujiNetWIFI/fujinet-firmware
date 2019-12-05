#ifndef _TNFS_IMP_H
#define _TNFS_IMP_H

#include "tnfs.h"
#include <FS.h>
#include <FSImpl.h>
#include "tnfs_udp.h"



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

private:
    byte fd;
};

#endif //_TNFS_IMP_H
