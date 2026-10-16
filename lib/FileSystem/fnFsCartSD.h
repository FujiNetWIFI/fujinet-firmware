#ifndef _FN_FSCARTSD_
#define _FN_FSCARTSD_

#include "../../include/pinmap.h"

// Only meaningful on boards where the ESP32-S3 has no SD of its own wired to
// it, and the only card in the system is reachable by asking the cart MCU
// over the same USB-CDC link the FujiNet mailbox itself uses -- see
// HAS_CART_SD_BRIDGE's own comment in include/pinmap/fujiversal-intv.h and
// pico/intellivision/firmware/src/cartfs_bridge.c for the other end of the
// wire. Absent everywhere else; fujiHost.cpp only references fnCartSD
// inside a matching #ifdef.
#if defined(ESP_PLATFORM) && defined(HAS_CART_SD_BRIDGE)

#include "fnFS.h"

/*
 * FileHandlerCartFS: read-only FileHandler over CARTFS_FILE_*. The RP2040
 * side has two file slots (vfs.c's own VFS_MAX_FILES=2) -- enough for a ROM's
 * main image plus its .cfg memory-map sidecar to stay open at once, which
 * MediaTypeROM::mount() needs (see cartfs_bridge.c's banner comment). `_handle`
 * is the opaque byte CARTFS_FILE_OPEN handed back; every subsequent read/close
 * names it explicitly so two FileHandlerCartFS instances never collide. The
 * destructor closes the slot if close() never ran, so it can't leak.
 */
class FileHandlerCartFS : public FileHandler
{
private:
    long _size;
    long _position;
    uint8_t _handle;
    bool _closed = false;

public:
    FileHandlerCartFS(long size, uint8_t handle);
    virtual ~FileHandlerCartFS() override;

    virtual int close(bool destroy = true) override;
    virtual int seek(long int off, int whence) override;
    virtual long int tell() override;
    virtual size_t read(void *ptr, size_t size, size_t n) override;
    virtual size_t write(const void *ptr, size_t size, size_t n) override;
    virtual int flush() override;
    virtual int eof() override;
};

/*
 * FileSystemCartSD: proxies every FileSystem call over CARTFS_* to the cart
 * MCU (see fujiHost::mount_local()'s fallback, lib/fuji/fujiHost.cpp), which
 * services them against its own local SD via vfs.c. Read-only: mkdir/rmdir/
 * remove/rename are stubs (FujiNet CONFIG's browse-and-boot flow never calls
 * them) that exist only to satisfy FileSystem's pure virtuals.
 */
class FileSystemCartSD : public FileSystem
{
public:
    virtual bool is_global() override { return true; };

    fsType type() override { return FSTYPE_CARTSD; };
    const char * typestring() override { return type_to_string(FSTYPE_CARTSD); };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    success_is_true remove(const char* path) override { RETURN_ERROR_AS_FALSE(); };
    success_is_true rename(const char* pathFrom, const char* pathTo) override { RETURN_ERROR_AS_FALSE(); };

    bool is_dir(const char *path) override;
    success_is_true mkdir(const char* path) override { RETURN_ERROR_AS_FALSE(); };
    success_is_true rmdir(const char* path) override { RETURN_ERROR_AS_FALSE(); };
    bool dir_exists(const char* path) override { return true; };

    success_is_true dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    success_is_true dir_seek(uint16_t) override;
};

extern FileSystemCartSD fnCartSD;

#endif // ESP_PLATFORM && HAS_CART_SD_BRIDGE

#endif // _FN_FSCARTSD_
