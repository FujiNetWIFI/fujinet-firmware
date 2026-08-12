
#include "fatfs_backend.h"

#include "ff.h"

#include <stdio.h>
#include <string.h>

static FATFS FatFs;

typedef struct {
    FIL fil;
} fat_file_t;

typedef struct {
    DIR dir;
    FILINFO info;
} fat_dir_t;

static void make_path(char *out, size_t size, const char *path, const vfs_mount_t *mnt) {
    snprintf(out, size, "%d:/%s", mnt->drive_id, path);
}

static int fat_init(const int drive_id) {

   char mnt[8];

   sprintf(mnt, "%d:/", drive_id);

   if(f_mount(&FatFs, mnt, 1) == FR_OK)
      return 1;

   return 0;
}

static vfs_file_t* fat_open(const char *path, const char *mode, vfs_file_t *out) {

   fat_file_t *f = (fat_file_t*)out->backend;
   
   BYTE flags = 0;
   
   if (mode[0] == 'r') {
      flags = FA_READ;
      if (mode[1] == '+')
         flags |= FA_WRITE;
   }
   else if (mode[0] == 'w')
      flags = FA_WRITE | FA_CREATE_ALWAYS;
   else if (mode[0] == 'a')
      flags = FA_WRITE | FA_OPEN_APPEND;
   else
      return NULL;
   
   char full[512];
   make_path(full, sizeof(full), path, out->mount);
   
   if ( f_open(&f->fil, full, flags) != FR_OK)
      return NULL;
   
   out->eof = 0;
   
   return out;
}

static int fat_read(vfs_file_t *vf, void *buf, size_t len) {

   FIL *fil = (FIL*)vf->backend;
   unsigned int br = 0;
   
   if (f_read(fil, buf, len, &br) != FR_OK)
      return -1;

   if (br == 0 || f_eof(fil))
      vf->eof = 1;
   
   return (int)br;
}

static char* fat_gets(vfs_file_t *vf, void *buf, size_t len) {

   FIL *fil = (FIL*)vf->backend;
   
   if (f_eof(fil))
      vf->eof = 1;
   
   return(f_gets(buf, len, fil));
}

static int fat_lseek(vfs_file_t *vf, size_t offset) {

   FIL *fil = (FIL*)vf->backend;

   if(f_lseek(fil, offset) != FR_OK)
      return -1;

   if (f_eof(fil))
      vf->eof = 1;

   return 0;
}

static int fat_tell(vfs_file_t *vf) {

   FIL *fil = (FIL*)vf->backend;

   return f_tell(fil);
}

static int fat_write(vfs_file_t *vf, const void *buf, size_t len) {

   FIL *fil = &((fat_file_t*)vf->backend)->fil;
   unsigned int bw = 0;
   
   if (f_write(fil, buf, len, &bw) != FR_OK)
      return -1;
   
   return (int)bw;
}

static int fat_close(vfs_file_t *vf) {

   FIL *fil = &((fat_file_t*)vf->backend)->fil;
   
   return (f_close(fil) == FR_OK) ? 0 : -1;
}

static vfs_dir_t* fat_opendir(const char *path, vfs_dir_t *out) {

   fat_dir_t *d = (fat_dir_t*) out->backend;
   
   char full[512];
   
   make_path(full, sizeof(full), path, out->mount);
   
   if (f_opendir(&d->dir, full) != FR_OK)
      return NULL;
   
   return out;
}

static int fat_readdir(vfs_dir_t *vd, vfs_dirent_t *out) {

   fat_dir_t *d = (fat_dir_t*) vd->backend;
   
   if (f_readdir(&d->dir, &d->info) != FR_OK)
      return -1;
   
   if (d->info.fname[0] == 0)
      return 0;
   
   strncpy(out->name, d->info.fname, sizeof(out->name));
   out->name[sizeof(out->name)-1] = 0;

   out->type = 0;

   if (d->info.fattrib & AM_DIR)
      out->type |= VFS_TYPE_DIR;

   if (d->info.fattrib & AM_SYS)
      out->type |= VFS_TYPE_SYSTEM;

   if (d->info.fattrib & AM_HID)
      out->type |= VFS_TYPE_HIDDEN;
   
   return 1;
}

static int fat_closedir(vfs_dir_t *vd) {

    (void)vd;
    return 0;
}

static int fat_stat(const char *path, vfs_stat_t *st, const vfs_mount_t *mnt) {

   FILINFO info;
   char full[512];

   make_path(full, sizeof(full), path, mnt);

   if (f_stat(full, &info) != FR_OK)
      return -1;

   strncpy(st->name, info.fname, sizeof(st->name) - 1);
   st->name[sizeof(st->name) - 1] = '\0';

   st->size = info.fsize;

   st->type = 0;

   if (info.fattrib & AM_DIR)
      st->type |= VFS_TYPE_DIR;

   if (info.fattrib & AM_SYS)
      st->type |= VFS_TYPE_SYSTEM;

   if (info.fattrib & AM_HID)
      st->type |= VFS_TYPE_HIDDEN;

   return 0;
}

const vfs_driver_t fatfs_driver = {
    .name = "fatfs",
    .init = fat_init,
    .open = fat_open,
    .read = fat_read,
    .gets = fat_gets,
    .lseek = fat_lseek,
    .tell = fat_tell,
    .write = fat_write,
    .close = fat_close,
    .opendir = fat_opendir,
    .readdir = fat_readdir,
    .closedir = fat_closedir,
    .stat = fat_stat
};
