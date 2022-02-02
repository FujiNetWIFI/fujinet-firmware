#ifndef DEVICE_DISK_H
#define DEVICE_DISK_H

#ifdef BUILD_ATARI
# include "sio/disk.h"
# define DEVICE_TYPE sioDisk
#endif

#ifdef BUILD_CBM
# include "iec/disk.h"
# define DEVICE_TYPE iecDisk
#endif

#ifdef BUILD_ADAM
# include "adamnet/disk.h"
# define DEVICE_TYPE adamDisk
#endif

#ifdef NEW_TARGET
# include "new/disk.h"
# define DEVICE_TYPE adamDisk
#endif

#endif // DEVICE_DISK_H