#ifndef _FNFSNTFS_VFS_H
#define _FNFSNTFS_VFS_H

#include "tnfslibMountInfo.h"

esp_err_t vfs_tnfs_register(tnfsMountInfo & m_info, char * basepath, int basepathlen);
esp_err_t vfs_tnfs_unregister(const char * basepath);

#endif // _FNFSNTFS_VFS_H
