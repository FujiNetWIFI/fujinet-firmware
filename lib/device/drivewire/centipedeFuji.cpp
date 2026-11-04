#ifdef BUILD_COCO
#ifdef COCO_HS_UART

#include "centipedeFuji.h"

success_is_true centipedeFuji::fujicmd_mount_disk_image_success(uint8_t deviceSlot,
                                                                 disk_access_flags_t access_mode)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_println("Fuji cmd: MOUNT IMAGE");

    if (!fujicore_mount_disk_image_success(deviceSlot, access_mode))
    {
        SYSTEM_BUS.transaction_error();
        RETURN_ERROR_AS_FALSE();
    }

    if (get_disk_dev(deviceSlot)->disktype() == MEDIATYPE_ROM &&
        !get_disk_dev(deviceSlot)->push_rom())
    {
        Debug_printf("MOUNT IMAGE: DBC push failed for device slot %u\r\n", deviceSlot);
        SYSTEM_BUS.transaction_error();
        RETURN_ERROR_AS_FALSE();
    }

    SYSTEM_BUS.transaction_success();
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true centipedeFuji::fujicmd_mount_all_success()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (!fujicore_mount_all_success())
    {
        SYSTEM_BUS.transaction_error();
        RETURN_ERROR_AS_FALSE();
    }

    for (unsigned int i = 0; i < _totalDiskDevices; i++)
    {
        if (get_disk_dev(i)->disktype() == MEDIATYPE_ROM && !get_disk_dev(i)->push_rom())
        {
            Debug_printf("MOUNT ALL: DBC push failed for device slot %u\r\n", i);
            SYSTEM_BUS.transaction_error();
            RETURN_ERROR_AS_FALSE();
        }
    }

    SYSTEM_BUS.transaction_success();
    RETURN_SUCCESS_AS_TRUE();
}

#endif /* COCO_HS_UART */
#endif /* BUILD_COCO */
