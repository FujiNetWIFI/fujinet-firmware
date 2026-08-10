
#include <stdio.h>
#include <string.h>

#include "vfs.h"

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static vfs_file_t files[VFS_MAX_FILES];
static vfs_dir_t dirs[VFS_MAX_DIRS];

void vfs_init(void) {

   memset(mounts, 0, sizeof(mounts));
   memset(files, 0, sizeof(files));
   memset(dirs, 0, sizeof(dirs));
}

int vfs_add_mount(const vfs_driver_t *drv, const char *mount_point, int drive_id, void *ctx) {

   for (int i=0; i<VFS_MAX_MOUNTS; i++) {
      if (!mounts[i].used) {
         mounts[i].driver = drv;
         if (mounts[i].driver->init(drive_id)) {
            mounts[i].used = 1;
            mounts[i].mount_point = mount_point;
            mounts[i].drive_id = drive_id;
            mounts[i].ctx = ctx;
            return 0;
         } else {
            mounts[i].driver = 0;
            return -1;
         }
      }
   }

   // no mount slot available
   return -1;
}

static const vfs_mount_t* resolve_mount(const char *path, const char **out) {

   for (int i = 0; i < VFS_MAX_MOUNTS; i++) {

      if (!mounts[i].used)
         continue;

      size_t len = strlen(mounts[i].mount_point);

      if (strncmp(path, mounts[i].mount_point, len) == 0) {

         const char *p = path + len;

         if (*p == '/')
            p++;

         *out = p;

         return &mounts[i];
      }
   }

   return NULL;
}

static vfs_file_t* alloc_file(void) {

   for (int i = 0; i < VFS_MAX_FILES; i++) {
      if (!files[i].used) {
         files[i].used = 1;
         return &files[i];
      }
   }
   return NULL;
}

static vfs_dir_t* alloc_dir(void) {

   for (int i = 0; i < VFS_MAX_DIRS; i++) {
      if (!dirs[i].used) {
         dirs[i].used = 1;
         return &dirs[i];
      }
   }
   return NULL;
}

vfs_file_t* vfs_open(const char *path, const char *mode) {

   const char *real_path;
   const vfs_mount_t *mnt = resolve_mount(path, &real_path);

   if (!mnt)
      return NULL;
   
   vfs_file_t *f= alloc_file();
   if (!f) {
      printf("E: no VFS file slot available\n");
      return NULL;
   }
   
   f->mount = mnt;

   vfs_file_t *ret;
   
   ret = mnt->driver->open(real_path, mode, f);

   if(ret == NULL)
      f->used = 0;

   return(ret);
}

int vfs_stat(const char *path, vfs_stat_t *st) {

   const char *real;

   const vfs_mount_t *mnt = resolve_mount(path, &real);

   if (!mnt)
      return -1;

   return mnt->driver->stat(real, st, mnt);
}

int vfs_read(vfs_file_t *f, void *buf, size_t len) {
   return f->mount->driver->read(f, buf, len);
}

char* vfs_gets(vfs_file_t *f, void *buf, size_t len) {
   return f->mount->driver->gets(f, buf, len);
}

int vfs_lseek(vfs_file_t *f, size_t offset) {
    return f->mount->driver->lseek(f, offset);
}

int vfs_tell(vfs_file_t *f) {
   return f->mount->driver->tell(f);
}

int vfs_write(vfs_file_t *f, const void *buf, size_t len) {
    return f->mount->driver->write(f, buf, len);
}

int vfs_close(vfs_file_t *f) {

    int r = f->mount->driver->close(f);
    f->used = 0;
    return r;
}

vfs_dir_t* vfs_opendir(const char *path) {

   const char *real;

   const vfs_mount_t *mnt = resolve_mount(path, &real);
   
   if (!mnt)
      return NULL;
   
   vfs_dir_t *d = alloc_dir();
   
   if (!d)
      return NULL;
   
   d->mount = mnt;

   vfs_dir_t *res = mnt->driver->opendir(real, d);

   if(res == NULL)
      d->used = 0;

   return res;
}

int vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *out) {
   return dir->mount->driver->readdir(dir, out);
}

int vfs_closedir(vfs_dir_t *dir) {

   int r = dir->mount->driver->closedir(dir);
   dir->used = 0;
   return r;
}
