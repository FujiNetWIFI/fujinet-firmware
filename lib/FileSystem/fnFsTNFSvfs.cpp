/* These are the "driver" functions needed to register 
    with the ESP-IDF VFS
*/

#include "fnFsTNFSvfs.h"

#include <dirent.h>
#include <esp_vfs.h>

#include "../../include/debug.h"

#include "tnfslib.h"

/*
    These are the 23 functions that can be registered (not including 6 fucntions for select())
    from esp_vfs.h:

    IMPLEMENTED:
    int (*open_p)(void* ctx, const char * path, int flags, int mode);
    int (*close_p)(void* ctx, int fd);
    ssize_t (*read_p)(void* ctx, int fd, void * dst, size_t size);
    int (*stat_p)(void* ctx, const char * path, struct stat * st);
    ssize_t (*write_p)(void* p, int fd, const void * data, size_t size);
    off_t (*lseek_p)(void* p, int fd, off_t size, int mode);
    int (*fstat_p)(void* ctx, int fd, struct stat * st);
    int (*unlink_p)(void* ctx, const char *path);
    int (*rename_p)(void* ctx, const char *src, const char *dst);
    int (*mkdir_p)(void* ctx, const char* name, mode_t mode);
    int (*rmdir_p)(void* ctx, const char* name);

    NOT IMPLEMENTED:
    DIR* (*opendir_p)(void* ctx, const char* name);
    int (*closedir_p)(void* ctx, DIR* pdir);
    struct dirent* (*readdir_p)(void* ctx, DIR* pdir);
    int (*readdir_r_p)(void* ctx, DIR* pdir, struct dirent* entry, struct dirent** out_dirent);
    long (*telldir_p)(void* ctx, DIR* pdir);
    void (*seekdir_p)(void* ctx, DIR* pdir, long offset);

    int (*access_p)(void* ctx, const char *path, int amode);
    int (*truncate_p)(void* ctx, const char *path, off_t length);

    int (*link_p)(void* ctx, const char* n1, const char* n2);
    int (*fcntl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*ioctl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*fsync_p)(void* ctx, int fd);
*/

int vfs_tnfs_mkdir(void* ctx, const char* name, mode_t mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    // Note that we ignore 'mode'
    int result = tnfs_mkdir(mi, name);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_mkdir = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_rmdir(void* ctx, const char* name)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_rmdir(mi, name);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_rmdir = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_unlink(void* ctx, const char *path)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_unlink(mi, path);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_unlink = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_rename(void* ctx, const char *src, const char *dst)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_rename(mi, src, dst);
    if(result != TNFS_RESULT_SUCCESS)
    {
        //Debug_printf("vfs_tnfs_rename = %d\n", result);
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

int vfs_tnfs_open(void* ctx, const char * path, int flags, int mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int16_t handle;
    // Translate the flags
    uint16_t tflags = 0;
    if(flags == 0)
        tflags = TNFS_OPENMODE_READ;
    else
    {
        tflags |= (flags & O_WRONLY) ? TNFS_OPENMODE_WRITE : 0;
        tflags |= (flags & O_CREAT) ? TNFS_OPENMODE_WRITE_CREATE : 0;
        tflags |= (flags & O_TRUNC) ? TNFS_OPENMODE_WRITE_TRUNCATE : 0;
        tflags |= (flags & O_APPEND) ? TNFS_OPENMODE_WRITE_APPEND : 0;
        tflags |= (flags & O_RDWR) ? TNFS_OPENMODE_READWRITE : 0;
        tflags |= (flags & O_EXCL) ? TNFS_OPENMODE_CREATE_EXCLUSIVE : 0;
    }

    int result = tnfs_open(mi, path, tflags, mode, &handle);
    if(result != TNFS_RESULT_SUCCESS)
    {
        #ifdef DEBUG
        //Debug_printf("vfs_tnfs_open = %d\n", result);
        #endif
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return handle;
}

int vfs_tnfs_close(void* ctx, int fd)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int result = tnfs_close(mi, fd);
    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return 0;
}

ssize_t vfs_tnfs_read(void* ctx, int fd, void * dst, size_t size)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    uint16_t readcount;
    int result = tnfs_read(mi, fd, (uint8_t *)dst, size, &readcount);

    if(result == TNFS_RESULT_SUCCESS || (result == TNFS_RESULT_END_OF_FILE && readcount > 0))
    {
        errno = 0;
        return readcount;
    }
    errno = tnfs_code_to_errno(result);

    return -1;
}

ssize_t vfs_tnfs_write(void* ctx, int fd, const void * data, size_t size)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    uint16_t writecount;
    int result = tnfs_write(mi, fd, (uint8_t *)data, size, &writecount);

    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return writecount;
}

off_t vfs_tnfs_lseek(void* ctx, int fd, off_t size, int mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    // Debug_printf("vfs_tnfs_lseek: fd=%d, off=%ld, mod=%d\n", fd, size, mode);
    uint32_t new_pos;
    int result = tnfs_lseek(mi, fd, size, mode, &new_pos);

    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    //Debug_printf("\treturning %u\n", new_pos);
    return new_pos;
}


int vfs_tnfs_stat(void* ctx, const char * path, struct stat * st)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    tnfsStat tstat;

    //Debug_printf("vfs_tnfs_stat: \"%s\"\n", path);

    int result = tnfs_stat(mi, &tstat, path);
    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_size = tstat.filesize;
    st->st_atime = tstat.a_time;
    st->st_mtime = tstat.m_time;
    st->st_ctime = tstat.c_time;
    st->st_mode = tstat.isDir ? S_IFDIR : S_IFREG;

    errno = 0;
    return 0;
}

int vfs_tnfs_fstat(void* ctx, int fd, struct stat * st)
{
    //Debug_printf("vfs_tnfs_fstat: %d\n", fd);    
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    const char *path = tnfs_filepath(mi, fd);
    return vfs_tnfs_stat(mi, path, st);
}


// Register our functions and use tnfsMountInfo as our context
// New basepath will be stored in basepath
esp_err_t vfs_tnfs_register(tnfsMountInfo &m_info, char *basepath, int basepathlen)
{
    // Trying to initialze the struct as coumented (e.g. ".write_p = &function")
    // results in compiloer error "non-trivial desginated initializers not supported"
    esp_vfs_t vfs;
    memset(&vfs, 0, sizeof(vfs));
    vfs.flags = ESP_VFS_FLAG_CONTEXT_PTR;
    vfs.open_p = &vfs_tnfs_open;
    vfs.close_p = &vfs_tnfs_close;
    vfs.read_p = &vfs_tnfs_read;
    vfs.write_p = &vfs_tnfs_write;
    vfs.stat_p = &vfs_tnfs_stat;
    vfs.fstat_p = &vfs_tnfs_fstat;
    vfs.lseek_p = &vfs_tnfs_lseek;
    vfs.unlink_p = &vfs_tnfs_unlink;
    vfs.rename_p = &vfs_tnfs_rename;

    // We'll use the address of our tnfsMountInfo to provide a unique base path
    // for this instance wihtout keeping track of how many we create
    snprintf(basepath, basepathlen, "/tnfs%p", &m_info);
    esp_err_t e = esp_vfs_register(basepath, &vfs, &m_info);

    Debug_printf("vfs_tnfs_register \"%s\" @ %p = %d \"%s\"\n", basepath, &m_info, e, esp_err_to_name(e));
    
    return e;
}

// Remove our driver from VFS
esp_err_t vfs_tnfs_unregister(const char * basepath)
{
    esp_err_t e = esp_vfs_unregister(basepath);

    #ifdef DEBUG
    Debug_printf("vfs_tnfs_unregister \"%s\" = %d \"%s\"\n", basepath, e, esp_err_to_name(e));
    #endif
    return e;
}
