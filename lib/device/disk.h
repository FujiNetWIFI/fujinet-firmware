#ifndef DEVICE_DISK_H
#define DEVICE_DISK_H

#ifdef BUILD_ATARI
# include "sio/disk.h"
# define MEDIA_TYPE disktype_t
# define MEDIA_TYPE_UNKNOWN DISKTYPE_UNKNOWN
# define DEVICE_TYPE sioDisk
#endif

#ifdef BUILD_CBM
# include "iec/disk.h"
# define MEDIA_TYPE mediatype_t
# define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
# define DEVICE_TYPE iecDisk
#endif

#ifdef BUILD_ADAM
# include "adamnet/disk.h"
# define MEDIA_TYPE mediatype_t
# define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
# define DEVICE_TYPE adamDisk
#endif

#ifdef NEW_TARGET
# include "new/disk.h"
# define MEDIA_TYPE mediatype_t
# define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
# define DEVICE_TYPE adamDisk
#endif

#endif // DEVICE_DISK_H