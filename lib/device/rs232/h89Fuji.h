#ifdef BUILD_RS232
#ifndef H89FUJI_H
#define H89FUJI_H

#include "rs232Fuji.h"

// The H89 PPI card. The RS232 boot disk is an MS-DOS one: nothing on the H89
// boots it, and CP/M or HDOS must not take it for one of their own disks.
class h89Fuji : public rs232Fuji
{
public:
    void insert_boot_device(uint8_t image_id, mediatype_t disk_type,
                            DISK_DEVICE *disk_dev) override {}
};

#endif /* H89FUJI_H */
#endif /* BUILD_RS232 */
