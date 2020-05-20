/* These are the "driver" functions needed to register 
    with the ESP-IDF VFS
*/
#include "../TNFSlib/tnfslib.h"

/*
    These are the functions that can be registered (not including serveral availble for select())

    ssize_t (*write_p)(void* p, int fd, const void * data, size_t size);
    off_t (*lseek_p)(void* p, int fd, off_t size, int mode);
    int (*open_p)(void* ctx, const char * path, int flags, int mode);
    int (*close_p)(void* ctx, int fd);
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
    int (*truncate)(const char *path, off_t length);
*/

#include <esp_vfs.h>

int vfs_tnfs_open(void* ctx, const char * path, int flags, int mode)
{
    return 0;
}


void test()
{
    esp_vfs_t myfs = 
    {
    

    };
    esp_vfs_register("/tnfs", &myfs, NULL);

}