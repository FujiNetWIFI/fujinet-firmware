#ifdef BUILD_MAC
#ifndef MACFUJI_H
#define MACFUJI_H

#include "fujiDevice.h"

#include <cstdint>
#include <string>

#include "../../include/debug.h"
#include "bus.h"

#include "mac/floppy.h"
#include "mac/printer.h"
#include "mac/modem.h"

#include "../fuji/fujiHost.h"
#include "../fuji/fujiDisk.h"

/**
 * The Mac has no CONFIG program on the host side: hosts and disk slots
 * are managed entirely through the web UI, which talks to the shared
 * fujiDevice base class. This subclass only wires up the per-slot
 * floppy/DCD device numbers used by the Pico protocol.
 */
class macFuji : public fujiDevice
{
protected:
    size_t set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                           uint8_t maxlen) override;
    void fan_out_archive(uint8_t deviceSlot, const std::string &path, disk_access_flags_t mode);

public:
    macFuji();
    ~macFuji() {};

    void setup() override;
    void process(mac_cmd_t cmd) override {};

    // A slot filename may name one disk image of an archive, "Foo.sit#2". A
    // plain archive name mounts its first image and fans the others out.
    success_is_true fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                      disk_access_flags_t access_mode) override;
    macFloppy *bootdisk() { return &_fnDisks[MAC_FLOPPY_SLOT].disk_dev; }
};

extern macFuji platformFuji;

#endif // MACFUJI_H
#endif // BUILD_MAC

#if 0
#ifndef MACFUJI_H
#define MACFUJI_H
#include <cstdint>

#include "../../include/debug.h"
#include "bus.h"
#include "iwm/disk2.h"
#include "iwm/iwmNetwork.h"
#include "iwm/printer.h"
#include "iwm/cpm.h"
#include "iwm/iwmClock.h"
#include "iwm/modem.h"




#endif // MACFUJI_H
#endif /* BUILD_APPLE */
