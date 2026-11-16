#ifndef DEVICE_DISK_H
#define DEVICE_DISK_H

#ifdef BUILD_ATARI
#include "sio/disk.h"
#define DISK_DEVICE sioDisk
#endif

#ifdef BUILD_RS232
#include "rs232/disk.h"
#define DISK_DEVICE rs232Disk
#endif

#ifdef BUILD_APPLE
#include "iwm/disk.h"
#define DISK_DEVICE iwmDisk
#endif

#ifdef BUILD_MAC
#include "mac/floppy.h"
#define DISK_DEVICE macFloppy
#endif

#ifdef BUILD_IEC
#include "iec/drive.h"
#define DISK_DEVICE iecDrive
#endif

#ifdef BUILD_ADAM
#include "adamnet/disk.h"
#define DISK_DEVICE adamDisk
#endif

#ifdef BUILD_LYNX
#include "comlynx/disk.h"
#define DISK_DEVICE lynxDisk
#endif

#ifdef BUILD_S100
#include "s100spi/disk.h"
#define DISK_DEVICE s100spiDisk
#endif

#ifdef NEW_TARGET
#include "new/disk.h"
#define DISK_DEVICE adamDisk
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/disk.h"
#define DISK_DEVICE cx16Disk
#endif

#ifdef BUILD_RC2014
#include "rc2014/disk.h"
#define DISK_DEVICE rc2014Disk
#endif

#ifdef BUILD_COCO
#include "drivewire/disk.h"
#define DISK_DEVICE drivewireDisk
#endif

#endif // DEVICE_DISK_H
