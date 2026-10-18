#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"

#include <cassert>

class rs232Fuji : public fujiDevice
{
private:
    uint16_t _newDiskSectorSize = 0;
    uint16_t _newDiskNumSectors = 0;

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    // Passes `host` through so MediaTypeROM can open the ROM's memory-map
    // sibling through it -- see fujiDevice::mount_media().
    mediatype_t mount_media(DISK_DEVICE *disk_dev, fujiDisk &disk, fujiHost &host,
                            disk_access_flags_t mode) override
    {
        return disk_dev->mount(disk.fileh, disk.filename, disk.disk_size, mode,
                               MEDIATYPE_UNKNOWN, &host);
    }

    void rs232_net_set_ssid(bool save);    // 0xFB
    void rs232_test();                     // 0x00

    success_is_true fujicore_read_new_disk_success(const FUJI_COMMAND_PACKET &packet,
                                                   NewDiskRequest &req) override;
    success_is_true fujicore_write_blank_success(uint8_t deviceSlot, fnFile *f) override;

public:
    rs232Fuji();
    void setup() override;
    void rs232_status(FujiStatusReq reqType);
    void rs232_process(const FujiBusPacket &packet) override;

    // ============ Wrapped Fuji commands ============
    ByteBuffer appkey_read() override;
};

#endif /* RS232FUJI_H */
#endif /* BUILD_RS232 */
