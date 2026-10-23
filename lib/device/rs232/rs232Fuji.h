#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"
#include "Cartridge.h"

#include <cassert>

class rs232Fuji : public fujiDevice
{
private:
    Cartridge _cartridge{SYSTEM_BUS, Cartridge::PICO_STREAMS};

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;

    // A ROM is pushed to our Cartridge and closed; anything else mounts as
    // usual, with `host` passed through for media that reach sibling files
    // -- see fujiDevice::mount_media().
    mediatype_t mount_media(DISK_DEVICE *disk_dev, fujiDisk &disk, fujiHost &host,
                            disk_access_flags_t mode) override;
    success_is_true push_rom(fujiDisk &disk, fujiHost &host);

    void rs232_net_set_ssid(bool save);    // 0xFB
    void rs232_test();                     // 0x00

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
