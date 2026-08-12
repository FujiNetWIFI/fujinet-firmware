
#include "littlefs_backend.h"

#include "pico_lfs.h"

#include <stdio.h>
#include <string.h>

#define LFS_PARTITION_SIZE    (1 * 1024 * 1024)

static struct lfs_config *lfs_cfg;
static lfs_t lfs;

typedef struct {
   lfs_file_t fil;
} littlefs_file_t;

typedef struct {
   lfs_dir_t dir;
   struct lfs_info info;
} littlefs_dir_t;

static int littlefs_init(const int drive_id) {

   lfs_cfg = pico_lfs_init(PICO_FLASH_SIZE_BYTES - LFS_PARTITION_SIZE, LFS_PARTITION_SIZE);

   if (!lfs_cfg)
      return 0;

   if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) {
      // init LFS filesystem
      printf("LFS: init filesystem\n");
      if (lfs_format(&lfs, lfs_cfg) != LFS_ERR_OK) {
         printf("LFS: failed to format filesystem\n"); 
         return 0;
      }
      if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) {
         printf("LFS: failed to mount filesystem\n");
         return 0;
      }
   }

   printf("LFS: filesystem ready\n");

   return 1;
}

static vfs_file_t* littlefs_open(const char *path, const char *mode, vfs_file_t *out) {

   littlefs_file_t *f = (littlefs_file_t*)out->backend;
   
   uint16_t flags = 0;

   if (mode[0] == 'r') {
      if (mode[1] == '+')
         flags = LFS_O_RDWR;
      else  
         flags = LFS_O_RDONLY;
   }
   else if (mode[0] == 'w')
      flags = LFS_O_RDWR | LFS_O_CREAT;
   else if (mode[0] == 'a')
      flags = LFS_O_RDWR | LFS_O_APPEND;

   if (lfs_file_open(&lfs, &f->fil, path, flags) < 0)
      return NULL;

   out->eof = 0;

   return out;
}

static int lfs_eof(lfs_t *lfs, lfs_file_t *fil) {

   lfs_soff_t pos  = lfs_file_tell(lfs, fil);
   lfs_soff_t size = lfs_file_size(lfs, fil);

   return (pos >= size);
}

static char *lfs_gets(lfs_t *lfs, lfs_file_t *file, char *buf, size_t size) {

   if (!buf || size == 0)
      return NULL;

   size_t i = 0;

   while (i < (size - 1)) {

      char c;
      int res = lfs_file_read(lfs, file, &c, 1);

      if (res < 0) 
         return NULL;

      if (res == 0)
         break;

      buf[i++] = c;

      if (c == '\n')
         break;
   }

   if (i == 0)
      return NULL;

    buf[i] = '\0';

    return buf;
}

static int littlefs_read(vfs_file_t *vf, void *buf, size_t len) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;
   int br = 0;

   if( (br = lfs_file_read(&lfs, fil, buf, len)) < 0)
      return -1;

   if (br == 0 || lfs_eof(&lfs, fil))
      vf->eof = 1;

   return br;   
}

static char* littlefs_gets(vfs_file_t *vf, void *buf, size_t len) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;

   if (lfs_eof(&lfs, fil))
      vf->eof = 1;

   return(lfs_gets(&lfs, fil, buf, len));
}

static int littlefs_lseek(vfs_file_t *vf, size_t offset) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;

   if (lfs_file_seek(&lfs, fil, offset, LFS_SEEK_SET) < 0)
      return -1;

   if (lfs_eof(&lfs, fil))
      vf->eof = 1;

   return 0;
}

static int littlefs_tell(vfs_file_t *vf) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;

   return lfs_file_tell(&lfs, fil);
}

static int littlefs_write(vfs_file_t *vf, const void *buf, size_t len) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;
   int bw = 0;

   if ( (bw = lfs_file_write(&lfs, fil, buf, len)) < 0)
      return -1;

   return bw;
}

static int littlefs_close(vfs_file_t *vf) {

   lfs_file_t *fil = (lfs_file_t *)vf->backend;

   return (lfs_file_close(&lfs, fil) >= 0) ? 0 : -1;
}

static vfs_dir_t* littlefs_opendir(const char *path, vfs_dir_t *out) {

   littlefs_dir_t *d = (littlefs_dir_t*) out->backend;

   char fullpath[512];

   if(strlen(path) == 0)
      strcpy(fullpath, "/");
   else
      strcpy(fullpath, path);

   if (lfs_dir_open(&lfs, &d->dir, fullpath) < 0)
      return NULL;

   return out;
}

static int littlefs_readdir(vfs_dir_t *vd, vfs_dirent_t *out) {

   littlefs_dir_t *d = (littlefs_dir_t*) vd->backend;
   int ret;

   if ( (ret = lfs_dir_read(&lfs, &d->dir, &d->info)) < 0)
      return -1;

   if (ret == 0)
      return 0;

   strncpy(out->name, d->info.name, sizeof(out->name));
   out->name[sizeof(out->name)-1] = 0;

   out->type = 0;

   if (d->info.type == LFS_TYPE_DIR)
      out->type |= VFS_TYPE_DIR;

   return 1;
}

static int littlefs_closedir(vfs_dir_t *vd) {

   littlefs_dir_t *d = (littlefs_dir_t*) vd->backend;

   lfs_dir_close(&lfs, &d->dir);
   return 0;
}

static int littlefs_stat(const char *path, vfs_stat_t *st, const vfs_mount_t *mnt) {

   struct lfs_info info;

   if (lfs_stat(&lfs, path, &info) < 0)
      return -1;

   strncpy(st->name, info.name, sizeof(st->name) - 1);
   st->name[sizeof(st->name) - 1] = '\0';

   st->size = info.size;

   st->type = 0;

   if (info.type == LFS_TYPE_DIR)
      st->type |= VFS_TYPE_DIR;

   return 0;
}

const vfs_driver_t littlefs_driver = {
    .name = "littlefs",
    .init = littlefs_init,
    .open = littlefs_open,
    .read = littlefs_read,
    .gets = littlefs_gets,
    .lseek = littlefs_lseek,
    .tell = littlefs_tell,
    .write = littlefs_write,
    .close = littlefs_close,
    .opendir = littlefs_opendir,
    .readdir = littlefs_readdir,
    .closedir = littlefs_closedir,
    .stat = littlefs_stat
};

