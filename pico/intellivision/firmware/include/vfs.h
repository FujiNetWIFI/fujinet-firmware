
#pragma once
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_FILES 2
#define VFS_MAX_DIRS  1
#define VFS_MAX_MOUNTS  1
#define VFS_NAME_MAX    512
#if CONFIG_FLASH_LFS_STORAGE
#define VFS_BACKEND_SIZE (512 + 512)
#else
#define VFS_BACKEND_SIZE (512 + 64)
#endif
#define VFS_TREE_PATH_MAX 512

typedef struct vfs_file vfs_file_t;
typedef struct vfs_dir  vfs_dir_t;
typedef struct vfs_mount vfs_mount_t;
typedef struct vfs_driver vfs_driver_t;

/* entry type */
typedef enum {
    VFS_TYPE_DIR = 1,
    VFS_TYPE_HIDDEN = 2,
    VFS_TYPE_SYSTEM = 4
} vfs_type_t;

/* directory entry */
typedef struct {
    char name[VFS_NAME_MAX];
    vfs_type_t type;
} vfs_dirent_t;

/* stat info */
typedef struct {
    vfs_type_t type;
    size_t size;
    char name[VFS_NAME_MAX];
} vfs_stat_t;

typedef struct vfs_driver {

   const char *name;

   int (*init)(const int drive);
   vfs_file_t* (*open)(const char *path, const char *mode, vfs_file_t *out);
   int (*stat)(const char *path, vfs_stat_t *st, const vfs_mount_t *mnt);

   vfs_dir_t* (*opendir)(const char *path, vfs_dir_t *out);
   int (*readdir)(vfs_dir_t *dir, vfs_dirent_t *out);
   int (*closedir)(vfs_dir_t *dir);

   int (*read)(vfs_file_t *f, void *buf, size_t len);
   char* (*gets)(vfs_file_t *f, void *buf, size_t len);
   int (*lseek)(vfs_file_t *f, size_t offset);
   int (*tell)(vfs_file_t *f);
   int (*write)(vfs_file_t *f, const void *buf, size_t len);
   int (*close)(vfs_file_t *f);
} vfs_fs_t;

/* file handle */
struct vfs_file {
   uint8_t backend[VFS_BACKEND_SIZE];
   const vfs_mount_t *mount;
   int used;
   int eof;
};

/* directory handle */
struct vfs_dir {
   uint8_t backend[VFS_BACKEND_SIZE];
   const vfs_mount_t *mount;
   int used;
};

struct vfs_mount {
    int used;
    const char *mount_point;
    const vfs_driver_t *driver;
    int drive_id;
    void *ctx;
};

/* API */
void vfs_init(void);
int vfs_add_mount(const vfs_driver_t *drv, const char *mount_point, int drive_id, void *ctx);
vfs_file_t* vfs_open(const char *path, const char *mode);

vfs_dir_t* vfs_opendir(const char *path);
int vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *out);
int vfs_closedir(vfs_dir_t *dir);
int vfs_stat(const char *path, vfs_stat_t *st);

int vfs_read(vfs_file_t *f, void *buf, size_t len);
char* vfs_gets(vfs_file_t *f, void *buf, size_t len);
int vfs_lseek(vfs_file_t *f, size_t offset);
int vfs_tell(vfs_file_t *f);
int vfs_write(vfs_file_t *f, const void *buf, size_t len);
int vfs_close(vfs_file_t *f);

static inline int vfs_eof(vfs_file_t *f) {
   return f->eof;
}
