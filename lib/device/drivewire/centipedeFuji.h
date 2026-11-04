#ifdef BUILD_COCO
#ifdef COCO_HS_UART
#ifndef CENTIPEDEFUJI_H
#define CENTIPEDEFUJI_H

#include "drivewireFuji.h"

// Centipede's One ROM companion shares the DriveWire wire with disk traffic,
// unlike fujiversal's dedicated side channel, so a mounted ROM image can only
// be pushed while the CoCo is blocked waiting for one of these two commands'
// reply - not from disk.cpp's generic mount() call site (front panel button,
// web config app), which drivewireFuji uses for every other board on this bus.
class centipedeFuji : public drivewireFuji
{
public:
    success_is_true fujicmd_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;
    success_is_true fujicmd_mount_all_success() override;
};

#endif /* CENTIPEDEFUJI_H */
#endif /* COCO_HS_UART */
#endif /* BUILD_COCO */
