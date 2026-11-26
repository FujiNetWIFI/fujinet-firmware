#ifdef BUILD_RS232
#ifndef RS232FUJI_H
#define RS232FUJI_H

#include "fujiDevice.h"
#include "RS232ROMFormats.h"

#include <cassert>

class rs232Fuji : public fujiDevice
{
private:

protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;
    mediatype_t directoryMediaType(const char *filename) override
    {
        return JoinedMediaFormats<RS232ROMFormats, rs232Disk::Formats>::entryType(filename);
    }

    // ROM formats need the host to open a .cfg mapper file beside the ROM
    std::unique_ptr<ROMMedia> openROM(const std::string &path, fnFile *fileh,
                                      fujiHost &host) override
    {
        return RS232ROMFormats::open<ROMMedia>(path, fileh, &host);
    }

    error_is_true mount_media(DISK_DEVICE *disk_dev, fnFile *fileh, fujiDisk &disk,
                              fujiHost &host, disk_access_flags_t mode) override
    {
        return disk_dev->mount(fileh, disk.filename, disk.disk_size, mode, &host);
    }

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
