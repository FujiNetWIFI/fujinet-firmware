#ifndef DEVICE_DISK_H
#define DEVICE_DISK_H

#ifdef BUILD_ATARI
#include "sio/disk.h"
// MSYS2: DEVICE_TYPE is defined in winioctl.h
#undef DEVICE_TYPE
#define DEVICE_TYPE sioDisk
#endif

#ifdef BUILD_RS232
#include "rs232/disk.h"
// MSYS2: DEVICE_TYPE is defined in winioctl.h
#undef DEVICE_TYPE
#define DEVICE_TYPE rs232Disk
#endif

#ifdef BUILD_APPLE
#include "iwm/disk.h"
// MSYS2: DEVICE_TYPE is defined in winioctl.h
#undef DEVICE_TYPE
#define DEVICE_TYPE iwmDisk
#endif

#ifdef BUILD_MAC
#include "mac/floppy.h"
#define DEVICE_TYPE macFloppy
#endif

#ifdef BUILD_IEC
#include "iec/drive.h"
#define DEVICE_TYPE iecDrive
#endif

#ifdef BUILD_ADAM
#include "adamnet/disk.h"
#define DEVICE_TYPE adamDisk
#endif

#ifdef BUILD_LYNX
#include "comlynx/disk.h"
#define DEVICE_TYPE lynxDisk
#endif

#ifdef BUILD_S100
#include "s100spi/disk.h"
#define DEVICE_TYPE s100spiDisk
#endif

#ifdef NEW_TARGET
#include "new/disk.h"
#define DEVICE_TYPE adamDisk
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/disk.h"
#define DEVICE_TYPE cx16Disk
#endif

#ifdef BUILD_RC2014
#include "rc2014/disk.h"
#define DEVICE_TYPE rc2014Disk
#endif

#ifdef BUILD_H89
#include "h89/disk.h"
#define DEVICE_TYPE H89Disk
#endif

#ifdef BUILD_COCO
#include "drivewire/disk.h"
// MSYS2: DEVICE_TYPE is defined in winioctl.h
#undef DEVICE_TYPE
#define DEVICE_TYPE drivewireDisk
#endif

#endif // DEVICE_DISK_H
