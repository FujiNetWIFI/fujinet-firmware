#ifdef BUILD_MAC

#include "macFuji.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "fnWiFi.h"
#include "fnFsSPIFFS.h"
#include "utils.h"
#include "string_utils.h"

#include <string>

#define DIR_MAX_LEN 40

macFuji platformFuji;
macFuji *theFuji = &platformFuji; // Global fuji object.

macFuji::macFuji()
{
  Debug_printf("Announcing the MacFuji!!!\n");
  // Helpful for debugging
  for (int i = 0; i < MAX_HOSTS; i++)
  {
    _fnHosts[i].slotid = i;
  };

}

macFloppy *macFuji::bootdisk()
{
  return _bootDisk;
}

// Initializes base settings and adds our devices to the SIO bus
void macFuji::setup(macBus *macbus)
{
  // set up Fuji device
  _mac_bus = macbus;

  _populate_slots_from_config();

  // Disable booting from CONFIG if our settings say to turn it off
  boot_config = false; // to do - understand?

  // Disable status_wait if our settings say to turn it off
  status_wait_enabled = false; // to do - understand?

  //  27-aug-23 use something similar for Mac floppy/dcd disks
   for (int i = MAX_DISK_DEVICES - MAX_FLOPPY_DEVICES -1; i >= 0; i--)
   {
     _fnDisks[i].disk_dev.set_disk_number('0' + i);
    //  _mac_bus->addDevice(&_fnDisks[i].disk_dev, mac_fujinet_type_t::HardDisk);
   }
   for (int i = MAX_DISK_DEVICES - 1; i >= MAX_DISK_DEVICES - MAX_FLOPPY_DEVICES; i--)
   {
     _fnDisks[MAX_DISK_DEVICES - 1].disk_dev.set_disk_number('0' + i);
    //  _mac_bus->addDevice(&_fnDisks[i].disk_dev, mac_fujinet_type_t::Floppy);
   }
}


// Temporary(?) function while we move from old config storage to new
void macFuji::_populate_slots_from_config()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_INVALID)
            _fnHosts[i].set_hostname("");
        else
            _fnHosts[i].set_hostname(Config.get_host_name(i).c_str());
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].reset();

        if (Config.get_mount_host_slot(i) != HOST_SLOT_INVALID)
        {
            if (Config.get_mount_host_slot(i) >= 0 && Config.get_mount_host_slot(i) <= MAX_HOSTS)
            {
                strlcpy(_fnDisks[i].filename,
                        Config.get_mount_path(i).c_str(), sizeof(fujiDisk::filename));
                _fnDisks[i].host_slot = Config.get_mount_host_slot(i);
                if (Config.get_mount_mode(i) == fnConfig::mount_modes::MOUNTMODE_WRITE)
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_WRITE;
                else
                    _fnDisks[i].access_mode = DISK_ACCESS_MODE_READ;
            }
        }
    }
}

// Temporary(?) function while we move from old config storage to new
void macFuji::_populate_config_from_slots()
{
    for (int i = 0; i < MAX_HOSTS; i++)
    {
        fujiHostType htype = _fnHosts[i].get_type();
        const char *hname = _fnHosts[i].get_hostname();

        if (hname[0] == '\0')
        {
            Config.clear_host(i);
        }
        else
        {
            Config.store_host(i, hname,
                              htype == HOSTTYPE_TNFS ? fnConfig::host_types::HOSTTYPE_TNFS : fnConfig::host_types::HOSTTYPE_SD);
        }
    }

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        if (_fnDisks[i].host_slot >= MAX_HOSTS || _fnDisks[i].filename[0] == '\0')
            Config.clear_mount(i);
        else
            Config.store_mount(i, _fnDisks[i].host_slot, _fnDisks[i].filename,
                               _fnDisks[i].access_mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE : fnConfig::mount_modes::MOUNTMODE_READ);
    }
}

// Mount all
bool macFuji::mount_all()
{
    bool nodisks = true; // Check at the end if no disks are in a slot and disable config

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        fujiDisk &disk = _fnDisks[i];
        fujiHost &host = _fnHosts[disk.host_slot];
        char flag[3] = {'r', 0, 0};

        if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            flag[1] = '+';

        if (disk.host_slot != 0xFF)
        {
            nodisks = false; // We have a disk in a slot

            if (host.mount() == false)
            {
                return true;
            }

            Debug_printf("Selecting '%s' from host #%u as %s on D%u:\n",
                         disk.filename, disk.host_slot, flag, i + 1);

            disk.fileh = host.file_open(disk.filename, disk.filename, sizeof(disk.filename), flag);

            if (disk.fileh == nullptr)
            {
                return true;
            }

            // We've gotten this far, so make sure our bootable CONFIG disk is disabled
            boot_config = false;

            // We need the file size for loading XEX files and for CASSETTE, so get that too
            disk.disk_size = host.file_size(disk.fileh);

            // And now mount it
            disk.disk_type = disk.disk_dev.mount(disk.fileh, disk.filename, disk.disk_size);
            disk.disk_dev.readonly = true;
            if (disk.access_mode == DISK_ACCESS_MODE_WRITE)
            {
              disk.disk_dev.readonly = false;
            }
        }
    }

    if (nodisks){
        // No disks in a slot, disable config
        boot_config = false;
    }

    // Go ahead and respond ok
    return false;
}

int macFuji::get_disk_id(int drive_slot)
{
    return _fnDisks[drive_slot].disk_dev.id();
}

std::string macFuji::get_host_prefix(int host_slot)
{
    return _fnHosts[host_slot].get_prefix();
}

// Public method to update host in specific slot
fujiHost *macFuji::set_slot_hostname(int host_slot, char *hostname)
{
    _fnHosts[host_slot].set_hostname(hostname);
    _populate_config_from_slots();
    return &_fnHosts[host_slot];
}

// This gets called when we're about to shutdown/reboot
void macFuji::shutdown()
{
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
        _fnDisks[i].disk_dev.unmount();
}


#endif // BUILD_MAC
