/* These are the "driver" functions needed to register 
    with the ESP-IDF VFS
*/
#include <dirent.h>
#include <sys/errno.h>

#include "esp_vfs.h"
#include "../../include/debug.h"
#include "../TNFSlib/tnfslib.h"

/*
    These are the 23 functions that can be registered (not including 6 fucntions for select())
    from esp_vfs.h:

    int (*open_p)(void* ctx, const char * path, int flags, int mode);
    int (*close_p)(void* ctx, int fd);

    ssize_t (*read_p)(void* ctx, int fd, void * dst, size_t size);

    ssize_t (*write_p)(void* p, int fd, const void * data, size_t size);
    off_t (*lseek_p)(void* p, int fd, off_t size, int mode);
    int (*fstat_p)(void* ctx, int fd, struct stat * st);
    int (*stat_p)(void* ctx, const char * path, struct stat * st);
    int (*link_p)(void* ctx, const char* n1, const char* n2);
    int (*unlink_p)(void* ctx, const char *path);
    int (*rename_p)(void* ctx, const char *src, const char *dst);
    DIR* (*opendir_p)(void* ctx, const char* name);
    struct dirent* (*readdir_p)(void* ctx, DIR* pdir);
    int (*readdir_r_p)(void* ctx, DIR* pdir, struct dirent* entry, struct dirent** out_dirent);
    long (*telldir_p)(void* ctx, DIR* pdir);
    void (*seekdir_p)(void* ctx, DIR* pdir, long offset);
    int (*closedir_p)(void* ctx, DIR* pdir);
    int (*mkdir_p)(void* ctx, const char* name, mode_t mode);
    int (*rmdir_p)(void* ctx, const char* name);
    int (*fcntl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*ioctl_p)(void* ctx, int fd, int cmd, va_list args);
    int (*fsync_p)(void* ctx, int fd);
    int (*access_p)(void* ctx, const char *path, int amode);
    int (*truncate_p)(void* ctx, const char *path, off_t length);
*/


int vfs_tnfs_open(void* ctx, const char * path, int flags, int mode)
{
    tnfsMountInfo *mi = (tnfsMountInfo *)ctx;

    int16_t handle;

    int result = tnfs_open(mi, path, flags, mode, &handle);
    if(result != TNFS_RESULT_SUCCESS)
    {
        #ifdef DEBUG
        Debug_printf("vfs_tnfs_open = %d\n", result);
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

    if(result != TNFS_RESULT_SUCCESS)
    {
        errno = tnfs_code_to_errno(result);
        return -1;
    }
    errno = 0;
    return readcount;
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

    // We'll use the address of our tnfsMountInfo to provide a unique base path
    // for this instance wihtout keeping track of how many we create
    snprintf(basepath, basepathlen, "/tnfs%p", &m_info);
    esp_err_t e = esp_vfs_register(basepath, &vfs, &m_info);

    #ifdef DEBUG
    Debug_printf("vfs_tnfs_register \"%s\" @ %p = %d \"%s\"\n", basepath, &m_info, e, esp_err_to_name(e));
    #endif
    
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
