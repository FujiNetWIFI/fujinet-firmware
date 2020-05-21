#ifndef _FNFSNTFS_VFS_H
#define _FNFSNTFS_VFS_H

int vfs_tnfs_open(void* ctx, const char * path, int flags, int mode);
int vfs_tnfs_close(void* ctx, int fd);

ssize_t vfs_tnfs_read(void* ctx, int fd, void * dst, size_t size);

esp_err_t vfs_tnfs_register(tnfsMountInfo & m_info, char * basepath, int basepathlen);
esp_err_t vfs_tnfs_unregister(const char * basepath);

#endif // _FNFSNTFS_VFS_H
